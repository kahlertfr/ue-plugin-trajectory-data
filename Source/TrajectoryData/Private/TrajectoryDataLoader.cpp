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

	// Discover all shard files and build time-range information table
	TMap<int32, FShardInfo> ShardInfoTable = DiscoverShardFiles(Params.DatasetPath, DatasetMeta);

	// Filter shards to only load those containing data in the requested time range
	TArray<int32> RelevantShards;
	for (const auto& ShardEntry : ShardInfoTable)
	{
		const FShardInfo& ShardInfo = ShardEntry.Value;
		if (ShardInfo.ContainsTimeRange(StartTime, EndTime))
		{
			RelevantShards.Add(ShardEntry.Key);
			UE_LOG(LogTemp, Verbose, TEXT("TrajectoryDataLoader: Will load from shard %d (time steps %d-%d)"),
				ShardEntry.Key, ShardInfo.StartTimeStep, ShardInfo.EndTimeStep);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataLoader: Loading from %d shard(s) for time range %d-%d"),
		RelevantShards.Num(), StartTime, EndTime);

	// Initialize trajectory data structures - one per requested trajectory
	TMap<int64, FLoadedTrajectory> TrajectoryMap;
	for (int64 TrajId : TrajectoryIds)
	{
		const FTrajectoryMetaBinary* TrajMeta = TrajMetaMap.Find(TrajId);
		if (TrajMeta)
		{
			FLoadedTrajectory& LoadedTraj = TrajectoryMap.Add(TrajId);
			LoadedTraj.TrajectoryId = TrajId;
			LoadedTraj.StartTimeStep = TrajMeta->StartTimeStep;
			LoadedTraj.EndTimeStep = TrajMeta->EndTimeStep;
			LoadedTraj.Extent = FVector(TrajMeta->Extent[0], TrajMeta->Extent[1], TrajMeta->Extent[2]);
			LoadedTraj.Samples.Reserve((EndTime - StartTime) / Params.SampleRate);
		}
	}
	
	int64 MemoryUsed = 0;
	FCriticalSection ResultMutex;  // For thread-safe access to shared state

	// Process each relevant shard to accumulate trajectory data across time intervals
	for (int32 ShardIndex : RelevantShards)
	{
		const FShardInfo* ShardInfo = ShardInfoTable.Find(ShardIndex);
		if (!ShardInfo)
		{
			continue;
		}

		FString ShardPath = ShardInfo->FilePath;

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.FileExists(*ShardPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Shard file not found: %s"), *ShardPath);
			continue;
		}

		// Memory-map the shard file once for all trajectories
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

		UE_LOG(LogTemp, Verbose, TEXT("TrajectoryDataLoader: Processing shard %d with %d trajectory entries"),
			ShardIndex, ShardHeader.TrajectoryEntryCount);

		// For each requested trajectory, try to load samples from this shard (in parallel)
		// Note: Not all trajectories will have data in every shard
		TArray<int64> TrajIdsArray = TrajectoryIds;
		
		ParallelFor(TrajIdsArray.Num(), [this, &TrajIdsArray, &TrajMetaMap, &TrajectoryMap, &ResultMutex,
			MappedData, MappedSize, &ShardHeader, &DatasetMeta, &Params, &ShardInfo](int32 Index)
		{
			int64 TrajId = TrajIdsArray[Index];
			const FTrajectoryMetaBinary* TrajMeta = TrajMetaMap.Find(TrajId);
			
			if (!TrajMeta)
			{
				return;
			}
			
			// Check if this trajectory has data in this time interval
			if (TrajMeta->EndTimeStep < ShardInfo->StartTimeStep || TrajMeta->StartTimeStep > ShardInfo->EndTimeStep)
			{
				// Trajectory doesn't exist in this time interval
				return;
			}
			
			// Load samples from this shard for this trajectory
			// We need to find the trajectory's entry in this shard
			// The shard contains entries for multiple trajectories, we need to search for ours
			// For now, we'll scan through entries (can be optimized with an index later)
			
			// Calculate data section start
			int64 DataSectionStart = ShardHeader.DataSectionOffset;
			int32 EntrySize = DatasetMeta.EntrySizeBytes;
			
			// Search for this trajectory's entry in the shard
			bool FoundEntry = false;
			for (int32 EntryIdx = 0; EntryIdx < ShardHeader.TrajectoryEntryCount; ++EntryIdx)
			{
				int64 EntryOffset = DataSectionStart + (EntryIdx * EntrySize);
				if (EntryOffset + sizeof(uint64) > MappedSize)
				{
					break;
				}
				
				// Read trajectory ID from entry
				uint64 EntryTrajId;
				FMemory::Memcpy(&EntryTrajId, MappedData + EntryOffset, sizeof(uint64));
				
				if (EntryTrajId == TrajId)
				{
					// Found the entry for this trajectory in this shard
					FoundEntry = true;
					
					// Parse the entry to extract samples
					int32 Offset = sizeof(uint64);
					
					int32 StartTimeStepInInterval;
					FMemory::Memcpy(&StartTimeStepInInterval, MappedData + EntryOffset + Offset, sizeof(int32));
					Offset += sizeof(int32);
					
					int32 ValidSampleCount;
					FMemory::Memcpy(&ValidSampleCount, MappedData + EntryOffset + Offset, sizeof(int32));
					Offset += sizeof(int32);
					
					// Extract samples for the requested time range
					TArray<FTrajectoryPositionSample> ShardSamples;
					
					for (int32 TimeStepIdx = 0; TimeStepIdx < ShardHeader.TimeStepIntervalSize; TimeStepIdx += Params.SampleRate)
					{
						// Calculate absolute time step
						int32 AbsoluteTimeStep = ShardInfo->StartTimeStep + TimeStepIdx;
						
						// Check if this time step is within the requested range
						if (AbsoluteTimeStep < Params.StartTimeStep && Params.StartTimeStep >= 0)
							continue;
						if (AbsoluteTimeStep > Params.EndTimeStep && Params.EndTimeStep >= 0)
							continue;
						
						// Check if this sample is valid (within the trajectory's valid range in this interval)
						int32 RelativeTimeStep = TimeStepIdx - StartTimeStepInInterval;
						if (RelativeTimeStep < 0 || RelativeTimeStep >= ValidSampleCount)
						{
							continue;
						}
						
						// Read position data
						int32 PosOffset = Offset + (TimeStepIdx * 12);
						if (EntryOffset + PosOffset + 12 > MappedSize)
						{
							break;
						}
						
						float X, Y, Z;
						FMemory::Memcpy(&X, MappedData + EntryOffset + PosOffset, sizeof(float));
						FMemory::Memcpy(&Y, MappedData + EntryOffset + PosOffset + 4, sizeof(float));
						FMemory::Memcpy(&Z, MappedData + EntryOffset + PosOffset + 8, sizeof(float));
						
						FTrajectoryPositionSample Sample;
						Sample.TimeStep = AbsoluteTimeStep;
						Sample.Position = FVector(X, Y, Z);
						Sample.bIsValid = !FMath::IsNaN(X) && !FMath::IsNaN(Y) && !FMath::IsNaN(Z);
						
						ShardSamples.Add(Sample);
					}
					
					// Add samples to the trajectory (thread-safe)
					if (ShardSamples.Num() > 0)
					{
						FScopeLock Lock(&ResultMutex);
						FLoadedTrajectory* LoadedTraj = TrajectoryMap.Find(TrajId);
						if (LoadedTraj)
						{
							LoadedTraj->Samples.Append(ShardSamples);
						}
					}
					
					break; // Found and processed this trajectory's entry
				}
			}
		});
	}

	// Convert map to array and sort samples by time step
	TArray<FLoadedTrajectory> NewTrajectories;
	for (auto& TrajEntry : TrajectoryMap)
	{
		FLoadedTrajectory& Traj = TrajEntry.Value;
		
		// Sort samples by time step (since they came from multiple shards)
		Traj.Samples.Sort([](const FTrajectoryPositionSample& A, const FTrajectoryPositionSample& B)
		{
			return A.TimeStep < B.TimeStep;
		});
		
		// Calculate memory usage
		int64 TrajMemory = sizeof(FLoadedTrajectory) + Traj.Samples.Num() * sizeof(FTrajectoryPositionSample);
		MemoryUsed += TrajMemory;
		
		NewTrajectories.Add(MoveTemp(Traj));
	}

	// Update loaded data
	LoadedTrajectories = MoveTemp(NewTrajectories);
	CurrentMemoryUsage = MemoryUsed;

	Result.bSuccess = true;
	Result.Trajectories = LoadedTrajectories;
	Result.MemoryUsedBytes = MemoryUsed;

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataLoader: Successfully loaded %d trajectories, using %s memory"),
		LoadedTrajectories.Num(), *UTrajectoryDataBlueprintLibrary::FormatMemorySize(MemoryUsed));

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

TMap<int32, FShardInfo> UTrajectoryDataLoader::DiscoverShardFiles(const FString& DatasetPath, const FDatasetMetaBinary& DatasetMeta)
{
	TMap<int32, FShardInfo> ShardInfoTable;
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Scan dataset directory for shard files (pattern: shard-*.bin)
	TArray<FString> FoundFiles;
	FString SearchPattern = FPaths::Combine(DatasetPath, TEXT("shard-*.bin"));
	PlatformFile.IterateDirectory(*DatasetPath, [&FoundFiles](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (!bIsDirectory)
		{
			FString Filename = FPaths::GetCleanFilename(FilenameOrDirectory);
			// Only include files matching shard-*.bin pattern
			if (Filename.StartsWith(TEXT("shard-")) && Filename.EndsWith(TEXT(".bin")))
			{
				FoundFiles.Add(Filename);
			}
		}
		return true; // Continue iteration
	});
	
	for (const FString& FileName : FoundFiles)
	{
		// Extract interval index from filename (this should match DataFileIndex in trajectory metadata)
		FString NumberPart = FileName.Mid(6); // Skip "shard-"
		NumberPart = NumberPart.LeftChop(4); // Remove ".bin"
		
		if (!NumberPart.IsNumeric())
		{
			continue;
		}
		
		int32 FileIndex = FCString::Atoi(*NumberPart);
		FString FullPath = FPaths::Combine(DatasetPath, FileName);
		
		// Try to read the shard header to get GlobalIntervalIndex
		TUniquePtr<IMappedFileHandle> MappedFileHandle(PlatformFile.OpenMapped(*FullPath));
		if (!MappedFileHandle.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to map shard file for discovery: %s"), *FullPath);
			continue;
		}
		
		int64 FileSize = PlatformFile.FileSize(*FullPath);
		if (FileSize < sizeof(FDataBlockHeaderBinary))
		{
			continue;
		}
		
		TUniquePtr<IMappedFileRegion> MappedRegion(MappedFileHandle->MapRegion(0, FileSize));
		if (!MappedRegion.IsValid())
		{
			continue;
		}
		
		const uint8* MappedData = MappedRegion->GetMappedPtr();
		
		// Read header
		FDataBlockHeaderBinary Header;
		if (!ReadShardHeaderMapped(MappedData, FileSize, Header))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to read shard header for: %s"), *FullPath);
			continue;
		}
		
		// Build shard info
		FShardInfo Info;
		Info.GlobalIntervalIndex = Header.GlobalIntervalIndex;
		Info.FilePath = FullPath;
		
		// Calculate time step range
		// Each shard covers: [GlobalIntervalIndex * TimeStepIntervalSize, (GlobalIntervalIndex + 1) * TimeStepIntervalSize - 1]
		// Adjusted by the dataset's FirstTimeStep
		Info.StartTimeStep = DatasetMeta.FirstTimeStep + (Header.GlobalIntervalIndex * DatasetMeta.TimeStepIntervalSize);
		Info.EndTimeStep = Info.StartTimeStep + DatasetMeta.TimeStepIntervalSize - 1;
		
		// Use FileIndex (from filename) as key to match with DataFileIndex in trajectory metadata
		ShardInfoTable.Add(FileIndex, Info);
		
		UE_LOG(LogTemp, Verbose, TEXT("TrajectoryDataLoader: Discovered shard file %d (global interval %d): time steps %d-%d"),
			FileIndex, Header.GlobalIntervalIndex, Info.StartTimeStep, Info.EndTimeStep);
	}
	
	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataLoader: Discovered %d shard files in dataset"), ShardInfoTable.Num());
	
	return ShardInfoTable;
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
