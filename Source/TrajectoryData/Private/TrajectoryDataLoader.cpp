// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataLoader.h"
#include "TrajectoryDataSettings.h"
#include "TrajectoryDataMemoryEstimator.h"
#include "TrajectoryDataBlueprintLibrary.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/RunnableThread.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"

UTrajectoryDataLoader* UTrajectoryDataLoader::Instance = nullptr;

UTrajectoryDataLoader::UTrajectoryDataLoader()
	: CurrentMemoryUsage(0)
	, bIsLoadingAsync(false)
{
}

UTrajectoryDataLoader::~UTrajectoryDataLoader()
{
	CancelAsyncLoad();
}

UTrajectoryDataLoader* UTrajectoryDataLoader::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UTrajectoryDataLoader>(GetTransientPackage());
		Instance->AddToRoot(); // Prevent garbage collection
	}
	return Instance;
}

FTrajectoryLoadValidation UTrajectoryDataLoader::ValidateLoadParams(const FTrajectoryLoadParams& Params)
{
	FTrajectoryLoadValidation Validation;
	Validation.bCanLoad = false;

	// Validate dataset path
	if (Params.DatasetPath.IsEmpty())
	{
		Validation.Message = TEXT("Dataset path is empty");
		return Validation;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Params.DatasetPath))
	{
		Validation.Message = FString::Printf(TEXT("Dataset directory does not exist: %s"), *Params.DatasetPath);
		return Validation;
	}

	// Check for required files
	FString MetaPath = FPaths::Combine(Params.DatasetPath, TEXT("dataset-meta.bin"));
	FString TrajMetaPath = FPaths::Combine(Params.DatasetPath, TEXT("dataset-trajmeta.bin"));

	if (!PlatformFile.FileExists(*MetaPath))
	{
		Validation.Message = TEXT("dataset-meta.bin not found");
		return Validation;
	}

	if (!PlatformFile.FileExists(*TrajMetaPath))
	{
		Validation.Message = TEXT("dataset-trajmeta.bin not found");
		return Validation;
	}

	// Read dataset metadata
	FDatasetMetaBinary DatasetMeta;
	if (!ReadDatasetMeta(Params.DatasetPath, DatasetMeta))
	{
		Validation.Message = TEXT("Failed to read dataset metadata");
		return Validation;
	}

	// Validate time range
	int32 StartTime = (Params.StartTimeStep < 0) ? DatasetMeta.FirstTimeStep : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? DatasetMeta.LastTimeStep : Params.EndTimeStep;

	if (StartTime >= EndTime)
	{
		Validation.Message = TEXT("Invalid time range: start must be less than end");
		return Validation;
	}

	// Validate sample rate
	if (Params.SampleRate < 1)
	{
		Validation.Message = TEXT("Sample rate must be at least 1");
		return Validation;
	}

	// Read trajectory metadata
	TArray<FTrajectoryMetaBinary> TrajMetas;
	if (!ReadTrajectoryMeta(Params.DatasetPath, TrajMetas))
	{
		Validation.Message = TEXT("Failed to read trajectory metadata");
		return Validation;
	}

	// Build trajectory ID list based on selection strategy
	TArray<int64> TrajectoryIds = BuildTrajectoryIdList(Params, DatasetMeta, TrajMetas);
	
	if (TrajectoryIds.Num() == 0)
	{
		Validation.Message = TEXT("No trajectories selected to load");
		return Validation;
	}

	// Calculate memory requirement
	int32 NumSamples = (EndTime - StartTime) / Params.SampleRate;
	Validation.NumTrajectoriesToLoad = TrajectoryIds.Num();
	Validation.NumSamplesPerTrajectory = NumSamples;
	
	// Memory calculation: trajectory metadata + sample data
	// Approximate bytes per sample (FVector + int32 + bool + padding)
	static constexpr int32 BytesPerSample = 20;
	int64 SampleMemory = (int64)TrajectoryIds.Num() * NumSamples * BytesPerSample;
	
	// Trajectory metadata overhead
	int64 TrajMetaMemory = (int64)TrajectoryIds.Num() * 128; // Approximate overhead per trajectory
	
	Validation.EstimatedMemoryBytes = SampleMemory + TrajMetaMemory;

	// Check against available memory
	UTrajectoryDataMemoryEstimator* MemEstimator = UTrajectoryDataMemoryEstimator::Get();
	FTrajectoryDataMemoryInfo MemInfo = MemEstimator->GetMemoryInfo();
	int64 CurrentUsage = MemInfo.CurrentEstimatedUsage + CurrentMemoryUsage;
	int64 Available = MemInfo.MaxTrajectoryDataMemory - CurrentUsage;

	if (Validation.EstimatedMemoryBytes > Available)
	{
		Validation.bCanLoad = false;
		Validation.Message = FString::Printf(
			TEXT("Insufficient memory: requires %s, available %s"),
			*UTrajectoryDataBlueprintLibrary::FormatMemorySize(Validation.EstimatedMemoryBytes),
			*UTrajectoryDataBlueprintLibrary::FormatMemorySize(Available));
	}
	else
	{
		Validation.bCanLoad = true;
		Validation.Message = FString::Printf(
			TEXT("Can load %d trajectories with %d samples each"),
			Validation.NumTrajectoriesToLoad, Validation.NumSamplesPerTrajectory);
	}

	return Validation;
}

FTrajectoryLoadResult UTrajectoryDataLoader::LoadTrajectoriesSync(const FTrajectoryLoadParams& Params)
{
	FScopeLock Lock(&LoadMutex);
	return LoadTrajectoriesInternal(Params);
}

bool UTrajectoryDataLoader::LoadTrajectoriesAsync(const FTrajectoryLoadParams& Params)
{
	FScopeLock Lock(&LoadMutex);

	if (bIsLoadingAsync)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Async load already in progress"));
		return false;
	}

	// Validate parameters first
	FTrajectoryLoadValidation Validation = ValidateLoadParams(Params);
	if (!Validation.bCanLoad)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Validation failed: %s"), *Validation.Message);
		return false;
	}

	// Create and start async task
	AsyncLoadTask = MakeShared<FTrajectoryLoadTask>(this, Params);
	bIsLoadingAsync = true;

	return true;
}

void UTrajectoryDataLoader::CancelAsyncLoad()
{
	FScopeLock Lock(&LoadMutex);

	if (AsyncLoadTask.IsValid())
	{
		AsyncLoadTask->Stop();
		AsyncLoadTask.Reset();
	}

	bIsLoadingAsync = false;
}

void UTrajectoryDataLoader::UnloadAll()
{
	FScopeLock Lock(&LoadMutex);

	LoadedTrajectories.Empty();
	CurrentMemoryUsage = 0;
}

FTrajectoryLoadResult UTrajectoryDataLoader::LoadTrajectoriesInternal(const FTrajectoryLoadParams& Params)
{
	FTrajectoryLoadResult Result;
	Result.bSuccess = false;

	// Read dataset metadata
	FDatasetMetaBinary DatasetMeta;
	if (!ReadDatasetMeta(Params.DatasetPath, DatasetMeta))
	{
		Result.ErrorMessage = TEXT("Failed to read dataset metadata");
		return Result;
	}

	// Read trajectory metadata
	TArray<FTrajectoryMetaBinary> TrajMetas;
	if (!ReadTrajectoryMeta(Params.DatasetPath, TrajMetas))
	{
		Result.ErrorMessage = TEXT("Failed to read trajectory metadata");
		return Result;
	}

	// Build trajectory ID list
	TArray<int64> TrajectoryIds = BuildTrajectoryIdList(Params, DatasetMeta, TrajMetas);
	
	if (TrajectoryIds.Num() == 0)
	{
		Result.ErrorMessage = TEXT("No trajectories to load");
		return Result;
	}

	// Determine time range
	int32 StartTime = (Params.StartTimeStep < 0) ? DatasetMeta.FirstTimeStep : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? DatasetMeta.LastTimeStep : Params.EndTimeStep;

	Result.LoadedStartTimeStep = StartTime;
	Result.LoadedEndTimeStep = EndTime;

	// Create map of trajectory ID to metadata for quick lookup
	TMap<int64, FTrajectoryMetaBinary> TrajMetaMap;
	for (const FTrajectoryMetaBinary& TrajMeta : TrajMetas)
	{
		TrajMetaMap.Add(TrajMeta.TrajectoryId, TrajMeta);
	}

	// Group trajectories by shard file for efficient batch loading
	TMap<int32, TArray<const FTrajectoryMetaBinary*>> ShardGroups = GroupTrajectoriesByShard(TrajectoryIds, TrajMetaMap);

	// Load trajectories using memory-mapped files and parallel processing
	TArray<FLoadedTrajectory> NewTrajectories;
	NewTrajectories.Reserve(TrajectoryIds.Num());
	
	int32 LoadedCount = 0;
	int64 MemoryUsed = 0;
	FCriticalSection ResultMutex;  // For thread-safe access to shared state

	// Process each shard group
	for (const auto& ShardGroup : ShardGroups)
	{
		int32 ShardIndex = ShardGroup.Key;
		const TArray<const FTrajectoryMetaBinary*>& TrajMetasInShard = ShardGroup.Value;

		// Get shard file path
		FString ShardPath = GetShardFilePath(Params.DatasetPath, ShardIndex);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.FileExists(*ShardPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Shard file not found: %s"), *ShardPath);
			continue;
		}

		// Memory-map the shard file once for all trajectories in this shard
		TSharedPtr<FMappedShardFile> MappedShard = MapShardFile(ShardPath);
		if (!MappedShard.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to map shard file: %s"), *ShardPath);
			continue;
		}

		// Read shard header from mapped memory
		FDataBlockHeaderBinary ShardHeader;
		const uint8* MappedData = MappedShard->MappedRegion->GetMappedPtr();
		int64 MappedSize = MappedShard->MappedRegion->GetMappedSize();
		
		if (!ReadShardHeaderMapped(MappedData, MappedSize, ShardHeader))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to read shard header: %s"), *ShardPath);
			continue;
		}

		// Load trajectories from this shard in parallel
		TArray<FLoadedTrajectory> ShardTrajectories;
		ShardTrajectories.SetNum(TrajMetasInShard.Num());

		ParallelFor(TrajMetasInShard.Num(), [&](int32 Index)
		{
			const FTrajectoryMetaBinary* TrajMeta = TrajMetasInShard[Index];
			
			// Load trajectory using memory-mapped data (zero-copy)
			FLoadedTrajectory& LoadedTraj = ShardTrajectories[Index];
			if (!LoadTrajectoryFromShardMapped(MappedData, MappedSize, ShardHeader, *TrajMeta, DatasetMeta, Params, LoadedTraj))
			{
				// Mark as invalid by setting TrajectoryId to 0
				LoadedTraj.TrajectoryId = 0;
			}
		});

		// Accumulate results in thread-safe manner
		for (FLoadedTrajectory& LoadedTraj : ShardTrajectories)
		{
			if (LoadedTraj.TrajectoryId != 0) // Valid trajectory
			{
				// Calculate memory used by this trajectory
				int64 TrajMemory = sizeof(FLoadedTrajectory) + LoadedTraj.Samples.Num() * sizeof(FTrajectoryPositionSample);
				
				FScopeLock Lock(&ResultMutex);
				MemoryUsed += TrajMemory;
				NewTrajectories.Add(MoveTemp(LoadedTraj));
				LoadedCount++;

				// Report progress for async loads
				if (bIsLoadingAsync)
				{
					float Progress = (float)LoadedCount / TrajectoryIds.Num() * 100.0f;
					int32 TotalTraj = TrajectoryIds.Num();
					
					// Broadcast on game thread - use weak reference to prevent crashes
					TWeakObjectPtr<UTrajectoryDataLoader> WeakThis(this);
					AsyncTask(ENamedThreads::GameThread, [WeakThis, LoadedCount, TotalTraj, Progress]()
					{
						if (WeakThis.IsValid() && WeakThis->OnLoadProgress.IsBound())
						{
							WeakThis->OnLoadProgress.Broadcast(LoadedCount, TotalTraj, Progress);
						}
					});
				}
			}
		}
		
		// Mapped file will be automatically unmapped when MappedShard goes out of scope
	}

	// Update loaded data
	// Note: This replaces any previously loaded data
	LoadedTrajectories = MoveTemp(NewTrajectories);
	CurrentMemoryUsage = MemoryUsed;

	Result.bSuccess = true;
	Result.Trajectories = LoadedTrajectories;
	Result.MemoryUsedBytes = MemoryUsed;

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataLoader: Successfully loaded %d trajectories, using %s memory"),
		LoadedCount, *UTrajectoryDataBlueprintLibrary::FormatMemorySize(MemoryUsed));

	return Result;
}

bool UTrajectoryDataLoader::ReadDatasetMeta(const FString& DatasetPath, FDatasetMetaBinary& OutMeta)
{
	FString MetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-meta.bin"));
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Get file size first
	int64 FileSize = PlatformFile.FileSize(*MetaPath);
	if (FileSize != sizeof(FDatasetMetaBinary))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid file size for dataset-meta.bin: %lld (expected %d)"),
			FileSize, sizeof(FDatasetMetaBinary));
		return false;
	}

	// Memory-map the file for fast reading
	TUniquePtr<IMappedFileHandle> MappedFileHandle(PlatformFile.OpenMapped(*MetaPath));
	if (!MappedFileHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to memory-map file: %s"), *MetaPath);
		return false;
	}

	TUniquePtr<IMappedFileRegion> MappedRegion(MappedFileHandle->MapRegion(0, FileSize));
	if (!MappedRegion.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to map file region: %s"), *MetaPath);
		return false;
	}

	// Direct copy from mapped memory
	const uint8* MappedData = MappedRegion->GetMappedPtr();
	FMemory::Memcpy(&OutMeta, MappedData, sizeof(FDatasetMetaBinary));

	// Validate magic number
	if (FMemory::Memcmp(OutMeta.Magic, "TDSH", 4) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid magic number in dataset-meta.bin"));
		return false;
	}

	return true;
}

bool UTrajectoryDataLoader::ReadTrajectoryMeta(const FString& DatasetPath, TArray<FTrajectoryMetaBinary>& OutMetas)
{
	FString TrajMetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-trajmeta.bin"));
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Get file size
	int64 FileSize = PlatformFile.FileSize(*TrajMetaPath);
	if (FileSize <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid file size for dataset-trajmeta.bin"));
		return false;
	}

	int32 NumTrajectories = FileSize / sizeof(FTrajectoryMetaBinary);
	if (FileSize % sizeof(FTrajectoryMetaBinary) != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: File size not a multiple of trajectory meta size"));
	}

	// Memory-map the file for fast reading
	TUniquePtr<IMappedFileHandle> MappedFileHandle(PlatformFile.OpenMapped(*TrajMetaPath));
	if (!MappedFileHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to memory-map file: %s"), *TrajMetaPath);
		return false;
	}

	TUniquePtr<IMappedFileRegion> MappedRegion(MappedFileHandle->MapRegion(0, FileSize));
	if (!MappedRegion.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to map file region: %s"), *TrajMetaPath);
		return false;
	}

	// Direct copy from mapped memory
	const uint8* MappedData = MappedRegion->GetMappedPtr();
	OutMetas.SetNum(NumTrajectories);
	FMemory::Memcpy(OutMetas.GetData(), MappedData, NumTrajectories * sizeof(FTrajectoryMetaBinary));

	return true;
}

bool UTrajectoryDataLoader::ReadShardHeader(const FString& ShardPath, FDataBlockHeaderBinary& OutHeader)
{
	// Use file handle to read only the header portion
	IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*ShardPath);
	if (!FileHandle)
	{
		return false;
	}

	// Read only the header bytes
	TArray<uint8> HeaderData;
	HeaderData.SetNumUninitialized(sizeof(FDataBlockHeaderBinary));
	
	if (!FileHandle->Read(HeaderData.GetData(), sizeof(FDataBlockHeaderBinary)))
	{
		delete FileHandle;
		return false;
	}

	delete FileHandle;

	FMemory::Memcpy(&OutHeader, HeaderData.GetData(), sizeof(FDataBlockHeaderBinary));

	// Validate magic number
	if (FMemory::Memcmp(OutHeader.Magic, "TDDB", 4) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid magic number in shard file"));
		return false;
	}

	return true;
}

bool UTrajectoryDataLoader::LoadTrajectoryFromShard(const FString& ShardPath, const FDataBlockHeaderBinary& Header,
	const FTrajectoryMetaBinary& TrajMeta, const FDatasetMetaBinary& DatasetMeta,
	const FTrajectoryLoadParams& Params, FLoadedTrajectory& OutTrajectory)
{
	// Set trajectory metadata
	OutTrajectory.TrajectoryId = TrajMeta.TrajectoryId;
	OutTrajectory.StartTimeStep = TrajMeta.StartTimeStep;
	OutTrajectory.EndTimeStep = TrajMeta.EndTimeStep;
	OutTrajectory.Extent = FVector(TrajMeta.Extent[0], TrajMeta.Extent[1], TrajMeta.Extent[2]);

	// Calculate entry offset in file
	// According to spec: file_offset = data_section_offset + (entry_offset_index * entry_size_bytes)
	int64 EntryOffset = Header.DataSectionOffset + (TrajMeta.EntryOffsetIndex * (int64)DatasetMeta.EntrySizeBytes);

	// Read trajectory entry data
	// Entry structure: uint64 trajectory_id + int32 start_time_step + int32 valid_sample_count + positions
	TArray<uint8> EntryData;
	int32 TotalEntrySize = DatasetMeta.EntrySizeBytes;

	IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*ShardPath);
	if (!FileHandle)
	{
		return false;
	}

	// Seek to entry
	FileHandle->Seek(EntryOffset);

	// Read entry data
	EntryData.SetNumUninitialized(TotalEntrySize);
	if (!FileHandle->Read(EntryData.GetData(), TotalEntrySize))
	{
		delete FileHandle;
		return false;
	}

	delete FileHandle;

	// Parse entry header
	int32 Offset = 0;
	uint64 EntryTrajId;
	FMemory::Memcpy(&EntryTrajId, EntryData.GetData() + Offset, sizeof(uint64));
	Offset += sizeof(uint64);

	int32 StartTimeStepInInterval;
	FMemory::Memcpy(&StartTimeStepInInterval, EntryData.GetData() + Offset, sizeof(int32));
	Offset += sizeof(int32);

	int32 ValidSampleCount;
	FMemory::Memcpy(&ValidSampleCount, EntryData.GetData() + Offset, sizeof(int32));
	Offset += sizeof(int32);

	// Verify trajectory ID matches
	if (EntryTrajId != TrajMeta.TrajectoryId)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Trajectory ID mismatch in shard entry: expected %lld, got %lld"),
			TrajMeta.TrajectoryId, EntryTrajId);
		return false;
	}

	// Determine time range to load relative to the interval
	// NOTE: Current implementation limitation - this loads only from a single shard.
	// For proper multi-shard support, we would need to:
	// 1. Calculate the global interval index from Header.GlobalIntervalIndex
	// 2. Map Params time steps to the local interval range
	// 3. Load additional shards if the requested time range spans multiple intervals
	int32 StartTime = (Params.StartTimeStep < 0) ? 0 : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? Header.TimeStepIntervalSize : Params.EndTimeStep;

	// Parse positions
	// Note: StartTimeStepInInterval indicates where this trajectory's data begins within the interval
	// We need to adjust for this when calculating offsets
	for (int32 TimeStep = StartTime; TimeStep < EndTime && TimeStep < Header.TimeStepIntervalSize; TimeStep += Params.SampleRate)
	{
		// Calculate offset accounting for where the trajectory actually starts in this interval
		int32 RelativeTimeStep = TimeStep - StartTimeStepInInterval;
		if (RelativeTimeStep < 0 || RelativeTimeStep >= ValidSampleCount)
		{
			// This time step is outside the valid range for this trajectory
			continue;
		}
		
		int32 PosOffset = Offset + (RelativeTimeStep * 12);
		
		float X, Y, Z;
		FMemory::Memcpy(&X, EntryData.GetData() + PosOffset, sizeof(float));
		FMemory::Memcpy(&Y, EntryData.GetData() + PosOffset + 4, sizeof(float));
		FMemory::Memcpy(&Z, EntryData.GetData() + PosOffset + 8, sizeof(float));

		FTrajectoryPositionSample Sample;
		Sample.TimeStep = TimeStep;
		Sample.Position = FVector(X, Y, Z);
		
		// Check for NaN (invalid sample)
		Sample.bIsValid = !FMath::IsNaN(X) && !FMath::IsNaN(Y) && !FMath::IsNaN(Z);

		OutTrajectory.Samples.Add(Sample);
	}

	return true;
}

TArray<int64> UTrajectoryDataLoader::BuildTrajectoryIdList(const FTrajectoryLoadParams& Params,
	const FDatasetMetaBinary& DatasetMeta, const TArray<FTrajectoryMetaBinary>& TrajMetas)
{
	TArray<int64> TrajectoryIds;

	switch (Params.SelectionStrategy)
	{
	case ETrajectorySelectionStrategy::FirstN:
		{
			int32 NumToLoad = FMath::Min(Params.NumTrajectories, TrajMetas.Num());
			for (int32 i = 0; i < NumToLoad; ++i)
			{
				TrajectoryIds.Add(TrajMetas[i].TrajectoryId);
			}
		}
		break;

	case ETrajectorySelectionStrategy::Distributed:
		{
			int32 NumToLoad = FMath::Min(Params.NumTrajectories, TrajMetas.Num());
			if (NumToLoad > 0 && TrajMetas.Num() > 0)
			{
				int32 Step = FMath::Max(1, TrajMetas.Num() / NumToLoad);
				for (int32 i = 0; i < TrajMetas.Num() && TrajectoryIds.Num() < NumToLoad; i += Step)
				{
					TrajectoryIds.Add(TrajMetas[i].TrajectoryId);
				}
			}
		}
		break;

	case ETrajectorySelectionStrategy::ExplicitList:
		{
			// Create a set of available trajectory IDs
			TSet<int64> AvailableIds;
			for (const FTrajectoryMetaBinary& TrajMeta : TrajMetas)
			{
				AvailableIds.Add(TrajMeta.TrajectoryId);
			}

			// Add only trajectory IDs that exist in the dataset
			for (const FTrajectoryLoadSelection& Selection : Params.TrajectorySelections)
			{
				if (AvailableIds.Contains(Selection.TrajectoryId))
				{
					TrajectoryIds.Add(Selection.TrajectoryId);
				}
			}
		}
		break;
	}

	return TrajectoryIds;
}

FString UTrajectoryDataLoader::GetShardFilePath(const FString& DatasetPath, int32 IntervalIndex)
{
	return FPaths::Combine(DatasetPath, FString::Printf(TEXT("shard-%d.bin"), IntervalIndex));
}

TSharedPtr<FMappedShardFile> UTrajectoryDataLoader::MapShardFile(const FString& ShardPath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Get file size
	int64 FileSize = PlatformFile.FileSize(*ShardPath);
	if (FileSize <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid file size for: %s"), *ShardPath);
		return nullptr;
	}

	// Open mapped file handle
	TUniquePtr<IMappedFileHandle> MappedFileHandle(PlatformFile.OpenMapped(*ShardPath));
	if (!MappedFileHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to memory-map file: %s"), *ShardPath);
		return nullptr;
	}

	// Map the entire file into memory
	TUniquePtr<IMappedFileRegion> MappedRegion(MappedFileHandle->MapRegion(0, FileSize));
	if (!MappedRegion.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to map file region: %s"), *ShardPath);
		return nullptr;
	}

	// Create wrapper structure
	TSharedPtr<FMappedShardFile> MappedFile = MakeShared<FMappedShardFile>();
	MappedFile->MappedFileHandle = MoveTemp(MappedFileHandle);
	MappedFile->MappedRegion = MoveTemp(MappedRegion);
	MappedFile->ShardPath = ShardPath;
	
	return MappedFile;
}

bool UTrajectoryDataLoader::ReadShardHeaderMapped(const uint8* MappedData, int64 MappedSize, FDataBlockHeaderBinary& OutHeader)
{
	if (MappedSize < sizeof(FDataBlockHeaderBinary))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Mapped region too small for header"));
		return false;
	}

	// Direct memory copy from mapped region
	FMemory::Memcpy(&OutHeader, MappedData, sizeof(FDataBlockHeaderBinary));

	// Validate magic number
	if (FMemory::Memcmp(OutHeader.Magic, "TDDB", 4) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid magic number in mapped shard file"));
		return false;
	}

	return true;
}

bool UTrajectoryDataLoader::LoadTrajectoryFromShardMapped(const uint8* MappedData, int64 MappedSize,
	const FDataBlockHeaderBinary& Header, const FTrajectoryMetaBinary& TrajMeta,
	const FDatasetMetaBinary& DatasetMeta, const FTrajectoryLoadParams& Params,
	FLoadedTrajectory& OutTrajectory)
{
	// Set trajectory metadata
	OutTrajectory.TrajectoryId = TrajMeta.TrajectoryId;
	OutTrajectory.StartTimeStep = TrajMeta.StartTimeStep;
	OutTrajectory.EndTimeStep = TrajMeta.EndTimeStep;
	OutTrajectory.Extent = FVector(TrajMeta.Extent[0], TrajMeta.Extent[1], TrajMeta.Extent[2]);

	// Calculate entry offset in file
	int64 EntryOffset = Header.DataSectionOffset + (TrajMeta.EntryOffsetIndex * (int64)DatasetMeta.EntrySizeBytes);
	int32 TotalEntrySize = DatasetMeta.EntrySizeBytes;

	// Validate bounds
	if (EntryOffset + TotalEntrySize > MappedSize)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Entry data exceeds mapped file size"));
		return false;
	}

	// Direct pointer to entry data in mapped memory (zero-copy)
	const uint8* EntryData = MappedData + EntryOffset;

	// Parse entry header
	int32 Offset = 0;
	uint64 EntryTrajId;
	FMemory::Memcpy(&EntryTrajId, EntryData + Offset, sizeof(uint64));
	Offset += sizeof(uint64);

	int32 StartTimeStepInInterval;
	FMemory::Memcpy(&StartTimeStepInInterval, EntryData + Offset, sizeof(int32));
	Offset += sizeof(int32);

	int32 ValidSampleCount;
	FMemory::Memcpy(&ValidSampleCount, EntryData + Offset, sizeof(int32));
	Offset += sizeof(int32);

	// Verify trajectory ID matches
	if (EntryTrajId != TrajMeta.TrajectoryId)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Trajectory ID mismatch in shard entry: expected %lld, got %lld"),
			TrajMeta.TrajectoryId, EntryTrajId);
		return false;
	}

	// Determine time range to load
	int32 StartTime = (Params.StartTimeStep < 0) ? 0 : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? Header.TimeStepIntervalSize : Params.EndTimeStep;

	// Parse positions (zero-copy read from mapped memory)
	for (int32 TimeStep = StartTime; TimeStep < EndTime && TimeStep < Header.TimeStepIntervalSize; TimeStep += Params.SampleRate)
	{
		int32 RelativeTimeStep = TimeStep - StartTimeStepInInterval;
		if (RelativeTimeStep < 0 || RelativeTimeStep >= ValidSampleCount)
		{
			continue;
		}
		
		int32 PosOffset = Offset + (RelativeTimeStep * 12);
		
		// Direct read from mapped memory
		float X, Y, Z;
		FMemory::Memcpy(&X, EntryData + PosOffset, sizeof(float));
		FMemory::Memcpy(&Y, EntryData + PosOffset + 4, sizeof(float));
		FMemory::Memcpy(&Z, EntryData + PosOffset + 8, sizeof(float));

		FTrajectoryPositionSample Sample;
		Sample.TimeStep = TimeStep;
		Sample.Position = FVector(X, Y, Z);
		Sample.bIsValid = !FMath::IsNaN(X) && !FMath::IsNaN(Y) && !FMath::IsNaN(Z);

		OutTrajectory.Samples.Add(Sample);
	}

	return true;
}

TMap<int32, TArray<const FTrajectoryMetaBinary*>> UTrajectoryDataLoader::GroupTrajectoriesByShard(
	const TArray<int64>& TrajectoryIds, const TMap<int64, FTrajectoryMetaBinary>& TrajMetaMap)
{
	TMap<int32, TArray<const FTrajectoryMetaBinary*>> ShardGroups;

	for (int64 TrajId : TrajectoryIds)
	{
		const FTrajectoryMetaBinary* TrajMeta = TrajMetaMap.Find(TrajId);
		if (TrajMeta)
		{
			int32 ShardIndex = TrajMeta->DataFileIndex;
			ShardGroups.FindOrAdd(ShardIndex).Add(TrajMeta);
		}
	}

	return ShardGroups;
}

int64 UTrajectoryDataLoader::CalculateMemoryRequirement(const FTrajectoryLoadParams& Params,
	const FDatasetMetaBinary& DatasetMeta)
{
	// This is a rough estimate
	int32 TimeSteps = DatasetMeta.LastTimeStep - DatasetMeta.FirstTimeStep;
	if (Params.StartTimeStep >= 0 && Params.EndTimeStep >= 0)
	{
		TimeSteps = (Params.EndTimeStep - Params.StartTimeStep) / Params.SampleRate;
	}

	int64 NumTrajectories = Params.NumTrajectories;
	if (Params.SelectionStrategy == ETrajectorySelectionStrategy::ExplicitList)
	{
		NumTrajectories = Params.TrajectorySelections.Num();
	}

	// Approximate bytes per sample (same as in ValidateLoadParams)
	static constexpr int32 BytesPerSample = 20;
	return NumTrajectories * TimeSteps * BytesPerSample;
}

// FTrajectoryLoadTask implementation

FTrajectoryLoadTask::FTrajectoryLoadTask(UTrajectoryDataLoader* InLoader, const FTrajectoryLoadParams& InParams)
	: Loader(InLoader)
	, Params(InParams)
	, Thread(nullptr)
	, bShouldStop(false)
{
	Thread = FRunnableThread::Create(this, TEXT("TrajectoryLoadTask"), 0, TPri_Normal);
}

FTrajectoryLoadTask::~FTrajectoryLoadTask()
{
	if (Thread)
	{
		// Properly stop the thread before destroying
		Stop();
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

bool FTrajectoryLoadTask::Init()
{
	return true;
}

uint32 FTrajectoryLoadTask::Run()
{
	// Perform loading on background thread
	if (Loader)
	{
		Result = Loader->LoadTrajectoriesInternal(Params);
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Loader is null");
	}

	// Notify completion on game thread - use weak reference to prevent crashes
	TWeakObjectPtr<UTrajectoryDataLoader> WeakLoader(Loader);
	FTrajectoryLoadResult ResultCopy = Result;
	AsyncTask(ENamedThreads::GameThread, [WeakLoader, ResultCopy]()
	{
		if (WeakLoader.IsValid())
		{
			if (WeakLoader->OnLoadComplete.IsBound())
			{
				WeakLoader->OnLoadComplete.Broadcast(ResultCopy.bSuccess, ResultCopy);
			}
			WeakLoader->bIsLoadingAsync = false;
		}
	});

	return 0;
}

void FTrajectoryLoadTask::Stop()
{
	bShouldStop = true;
}

void FTrajectoryLoadTask::Exit()
{
}
