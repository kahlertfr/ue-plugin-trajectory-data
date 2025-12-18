// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataLoader.h"
#include "TrajectoryDataSettings.h"
#include "TrajectoryDataMemoryEstimator.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/RunnableThread.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Async/Async.h"

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
	int32 StartTime = (Params.StartTimeStep < 0) ? 0 : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? DatasetMeta.TimeStepIntervalSize : Params.EndTimeStep;

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
	// Each sample: FVector (12 bytes) + int32 (4 bytes) + bool (1 byte) = ~20 bytes with padding
	int64 SampleMemory = (int64)TrajectoryIds.Num() * NumSamples * 20;
	
	// Trajectory metadata overhead
	int64 TrajMetaMemory = (int64)TrajectoryIds.Num() * 128; // Approximate overhead per trajectory
	
	Validation.EstimatedMemoryBytes = SampleMemory + TrajMetaMemory;

	// Check against available memory
	UTrajectoryDataMemoryEstimator* MemEstimator = UTrajectoryDataMemoryEstimator::Get();
	int64 MaxMemory = MemEstimator->GetMaxTrajectoryDataMemory();
	int64 CurrentUsage = MemEstimator->GetEstimatedUsage() + CurrentMemoryUsage;
	int64 Available = MaxMemory - CurrentUsage;

	if (Validation.EstimatedMemoryBytes > Available)
	{
		Validation.bCanLoad = false;
		Validation.Message = FString::Printf(
			TEXT("Insufficient memory: requires %s, available %s"),
			*UTrajectoryDataMemoryEstimator::FormatMemorySize(Validation.EstimatedMemoryBytes),
			*UTrajectoryDataMemoryEstimator::FormatMemorySize(Available));
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
	int32 StartTime = (Params.StartTimeStep < 0) ? 0 : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? DatasetMeta.TimeStepIntervalSize : Params.EndTimeStep;

	Result.LoadedStartTimeStep = StartTime;
	Result.LoadedEndTimeStep = EndTime;

	// Create map of trajectory ID to metadata for quick lookup
	TMap<int64, FTrajectoryMetaBinary> TrajMetaMap;
	for (const FTrajectoryMetaBinary& TrajMeta : TrajMetas)
	{
		TrajMetaMap.Add(TrajMeta.TrajectoryId, TrajMeta);
	}

	// Load trajectories
	TArray<FLoadedTrajectory> NewTrajectories;
	int32 LoadedCount = 0;
	int64 MemoryUsed = 0;

	for (int64 TrajId : TrajectoryIds)
	{
		const FTrajectoryMetaBinary* TrajMeta = TrajMetaMap.Find(TrajId);
		if (!TrajMeta)
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Trajectory ID %lld not found in metadata"), TrajId);
			continue;
		}

		// Determine which shard file(s) to read
		int32 ShardIndex = TrajMeta->DataFileIndex;
		FString ShardPath = GetShardFilePath(Params.DatasetPath, ShardIndex);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.FileExists(*ShardPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Shard file not found: %s"), *ShardPath);
			continue;
		}

		// Read shard header
		FDataBlockHeaderBinary ShardHeader;
		if (!ReadShardHeader(ShardPath, ShardHeader))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to read shard header: %s"), *ShardPath);
			continue;
		}

		// Load trajectory data from shard
		FLoadedTrajectory LoadedTraj;
		if (LoadTrajectoryFromShard(ShardPath, ShardHeader, *TrajMeta, Params, LoadedTraj))
		{
			// Calculate memory used by this trajectory
			int64 TrajMemory = sizeof(FLoadedTrajectory) + LoadedTraj.Samples.Num() * sizeof(FTrajectoryPositionSample);
			MemoryUsed += TrajMemory;

			NewTrajectories.Add(LoadedTraj);
			LoadedCount++;

			// Report progress for async loads
			if (bIsLoadingAsync)
			{
				float Progress = (float)LoadedCount / TrajectoryIds.Num() * 100.0f;
				
				// Broadcast on game thread
				AsyncTask(ENamedThreads::GameThread, [this, LoadedCount, TrajectoryIds, Progress]()
				{
					if (OnLoadProgress.IsBound())
					{
						OnLoadProgress.Broadcast(LoadedCount, TrajectoryIds.Num(), Progress);
					}
				});
			}
		}
	}

	// Update loaded data
	LoadedTrajectories = MoveTemp(NewTrajectories);
	CurrentMemoryUsage = MemoryUsed;

	Result.bSuccess = true;
	Result.Trajectories = LoadedTrajectories;
	Result.MemoryUsedBytes = MemoryUsed;

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataLoader: Successfully loaded %d trajectories, using %s memory"),
		LoadedCount, *UTrajectoryDataMemoryEstimator::FormatMemorySize(MemoryUsed));

	return Result;
}

bool UTrajectoryDataLoader::ReadDatasetMeta(const FString& DatasetPath, FDatasetMetaBinary& OutMeta)
{
	FString MetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-meta.bin"));
	
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *MetaPath))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to read file: %s"), *MetaPath);
		return false;
	}

	if (FileData.Num() != sizeof(FDatasetMetaBinary))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Invalid file size for dataset-meta.bin: %d (expected %d)"),
			FileData.Num(), sizeof(FDatasetMetaBinary));
		return false;
	}

	FMemory::Memcpy(&OutMeta, FileData.GetData(), sizeof(FDatasetMetaBinary));

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
	
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *TrajMetaPath))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Failed to read file: %s"), *TrajMetaPath);
		return false;
	}

	int32 NumTrajectories = FileData.Num() / sizeof(FTrajectoryMetaBinary);
	if (FileData.Num() % sizeof(FTrajectoryMetaBinary) != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: File size not a multiple of trajectory meta size"));
	}

	OutMetas.SetNum(NumTrajectories);
	FMemory::Memcpy(OutMetas.GetData(), FileData.GetData(), NumTrajectories * sizeof(FTrajectoryMetaBinary));

	return true;
}

bool UTrajectoryDataLoader::ReadShardHeader(const FString& ShardPath, FDataBlockHeaderBinary& OutHeader)
{
	TArray<uint8> HeaderData;
	if (!FFileHelper::LoadFileToArray(HeaderData, *ShardPath, 0, sizeof(FDataBlockHeaderBinary)))
	{
		return false;
	}

	if (HeaderData.Num() < sizeof(FDataBlockHeaderBinary))
	{
		return false;
	}

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
	const FTrajectoryMetaBinary& TrajMeta, const FTrajectoryLoadParams& Params, FLoadedTrajectory& OutTrajectory)
{
	// Set trajectory metadata
	OutTrajectory.TrajectoryId = TrajMeta.TrajectoryId;
	OutTrajectory.StartTimeStep = TrajMeta.StartTimeStep;
	OutTrajectory.EndTimeStep = TrajMeta.EndTimeStep;
	OutTrajectory.Extent = FVector(TrajMeta.Extent[0], TrajMeta.Extent[1], TrajMeta.Extent[2]);

	// Calculate entry offset in file
	int64 EntryOffset = Header.DataSectionOffset + (TrajMeta.EntryOffsetIndex * Header.TimeStepIntervalSize * 12);

	// Read trajectory entry data
	// Entry structure: uint64 trajectory_id + int32 start_time_step + int32 valid_sample_count + positions
	TArray<uint8> EntryData;
	int32 EntryHeaderSize = sizeof(uint64) + sizeof(int32) + sizeof(int32);
	int32 PositionsSize = Header.TimeStepIntervalSize * 12; // 3 floats per sample
	int32 TotalEntrySize = EntryHeaderSize + PositionsSize;

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

	// Parse entry
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
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Trajectory ID mismatch in shard entry"));
		return false;
	}

	// Determine time range to load
	int32 StartTime = (Params.StartTimeStep < 0) ? 0 : Params.StartTimeStep;
	int32 EndTime = (Params.EndTimeStep < 0) ? Header.TimeStepIntervalSize : Params.EndTimeStep;

	// Parse positions
	for (int32 TimeStep = StartTime; TimeStep < EndTime && TimeStep < Header.TimeStepIntervalSize; TimeStep += Params.SampleRate)
	{
		int32 PosOffset = Offset + (TimeStep * 12);
		
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

int64 UTrajectoryDataLoader::CalculateMemoryRequirement(const FTrajectoryLoadParams& Params,
	const FDatasetMetaBinary& DatasetMeta)
{
	// This is a rough estimate
	int32 TimeSteps = DatasetMeta.TimeStepIntervalSize;
	if (Params.StartTimeStep >= 0 && Params.EndTimeStep >= 0)
	{
		TimeSteps = (Params.EndTimeStep - Params.StartTimeStep) / Params.SampleRate;
	}

	int64 NumTrajectories = Params.NumTrajectories;
	if (Params.SelectionStrategy == ETrajectorySelectionStrategy::ExplicitList)
	{
		NumTrajectories = Params.TrajectorySelections.Num();
	}

	return NumTrajectories * TimeSteps * 20; // Approximate size per sample
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
		Thread->Kill(true);
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
	Result = Loader->LoadTrajectoriesInternal(Params);

	// Notify completion on game thread
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		if (Loader && Loader->OnLoadComplete.IsBound())
		{
			Loader->OnLoadComplete.Broadcast(Result.bSuccess, Result);
		}

		if (Loader)
		{
			Loader->bIsLoadingAsync = false;
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
