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

FTrajectoryLoadValidation UTrajectoryDataLoader::ValidateLoadParams(const FTrajectoryDatasetInfo& DatasetInfo, const FTrajectoryLoadParams& Params)
{
	FTrajectoryLoadValidation Validation;
	Validation.bCanLoad = false;

	// Validate dataset path from DatasetInfo
	if (DatasetInfo.DatasetPath.IsEmpty())
	{
		Validation.Message = TEXT("Dataset path is empty");
		return Validation;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DatasetInfo.DatasetPath))
	{
		Validation.Message = FString::Printf(TEXT("Dataset directory does not exist: %s"), *DatasetInfo.DatasetPath);
		return Validation;
	}

	// Check for required files
	FString MetaPath = FPaths::Combine(DatasetInfo.DatasetPath, TEXT("dataset-meta.bin"));
	FString TrajMetaPath = FPaths::Combine(DatasetInfo.DatasetPath, TEXT("dataset-trajmeta.bin"));

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
	if (!ReadDatasetMeta(DatasetInfo.DatasetPath, DatasetMeta))
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
	if (!ReadTrajectoryMeta(DatasetInfo.DatasetPath, TrajMetas))
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
	// Bytes per sample: FVector Position (12 bytes: 3 floats)
	static constexpr int32 BytesPerSample = sizeof(FVector);
	int64 SampleMemory = (int64)TrajectoryIds.Num() * NumSamples * BytesPerSample;
	
	// Trajectory metadata overhead
	int64 TrajMetaMemory = (int64)TrajectoryIds.Num() * 128; // Approximate overhead per trajectory
	
	Validation.EstimatedMemoryBytes = SampleMemory + TrajMetaMemory;

	// Check against available memory
	UTrajectoryDataMemoryEstimator* MemEstimator = UTrajectoryDataMemoryEstimator::Get();
	FTrajectoryDataMemoryInfo MemInfo = MemEstimator->GetMemoryInfo();
	// Convert GB to bytes for comparison
	constexpr double BytesToGB = 1.0 / (1024.0 * 1024.0 * 1024.0);
	int64 CurrentUsage = static_cast<int64>(MemInfo.CurrentEstimatedUsageGB / BytesToGB) + CurrentMemoryUsage;
	int64 Available = static_cast<int64>(MemInfo.MaxTrajectoryDataMemoryGB / BytesToGB) - CurrentUsage;

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
		// Convert to GB for display
		float RequiredGB = static_cast<float>(Validation.EstimatedMemoryBytes * BytesToGB);
		Validation.Message = FString::Printf(
			TEXT("Can load %d trajectories with %d samples each (Estimated memory: %.2f GB)"),
			Validation.NumTrajectoriesToLoad, Validation.NumSamplesPerTrajectory, RequiredGB);
	}

	return Validation;
}

FTrajectoryLoadResult UTrajectoryDataLoader::LoadTrajectoriesSync(const FTrajectoryDatasetInfo& DatasetInfo, const FTrajectoryLoadParams& Params)
{
	FScopeLock Lock(&LoadMutex);
	return LoadTrajectoriesInternal(DatasetInfo, Params);
}

bool UTrajectoryDataLoader::LoadTrajectoriesAsync(const FTrajectoryDatasetInfo& DatasetInfo, const FTrajectoryLoadParams& Params)
{
	FScopeLock Lock(&LoadMutex);

	if (bIsLoadingAsync)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Async load already in progress"));
		return false;
	}

	// Validate parameters first
	FTrajectoryLoadValidation Validation = ValidateLoadParams(DatasetInfo, Params);
	if (!Validation.bCanLoad)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataLoader: Validation failed: %s"), *Validation.Message);
		return false;
	}

	// Create and start async task
	AsyncLoadTask = MakeShared<FTrajectoryLoadTask>(this, DatasetInfo, Params);
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

	LoadedDatasets.Empty();
	CurrentMemoryUsage = 0;
}

FTrajectoryLoadResult UTrajectoryDataLoader::LoadTrajectoriesInternal(const FTrajectoryDatasetInfo& DatasetInfo, const FTrajectoryLoadParams& Params)
{
	FTrajectoryLoadResult Result;
	Result.bSuccess = false;

	// Read dataset metadata
	FDatasetMetaBinary DatasetMeta;
	if (!ReadDatasetMeta(DatasetInfo.DatasetPath, DatasetMeta))
	{
		Result.ErrorMessage = TEXT("Failed to read dataset metadata");
		return Result;
	}

	// Read trajectory metadata
	TArray<FTrajectoryMetaBinary> TrajMetas;
	if (!ReadTrajectoryMeta(DatasetInfo.DatasetPath, TrajMetas))
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
	TMap<int32, FShardInfo> ShardInfoTable = DiscoverShardFiles(DatasetInfo.DatasetPath, DatasetMeta);

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

	// Sort shards by index to ensure they are processed in chronological order
	// This is critical for maintaining temporal ordering of samples when using Append()
	RelevantShards.Sort();

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

	// OPTIMIZATION 1 & 2: Parallelize shard processing and eliminate lock contention
	// Process shards in parallel using per-shard result maps, then merge sequentially
	// This removes all locking from the hot path while maintaining temporal ordering
	
	// Structure to hold per-shard trajectory samples before merging
	struct FShardTrajectoryData
	{
		TMap<int64, TArray<FVector>> TrajectorySamples;  // Per-trajectory samples for this shard
		int32 ShardIndex;
		FCriticalSection Mutex;  // Per-shard mutex for thread-safe map access
	};
	
	// Array to collect results from all shards (indexed by position in RelevantShards array)
	TArray<FShardTrajectoryData> ShardResults;
	ShardResults.SetNum(RelevantShards.Num());
	
	// OPTIMIZATION 3: Shard prefetching with futures
	// Pre-start async mapping for all shards to enable parallel I/O
	TArray<TFuture<TSharedPtr<FMappedShardFile>>> MappedShardFutures;
	MappedShardFutures.Reserve(RelevantShards.Num());
	
	for (int32 ShardArrayIndex = 0; ShardArrayIndex < RelevantShards.Num(); ++ShardArrayIndex)
	{
		int32 ShardIndex = RelevantShards[ShardArrayIndex];
		const FShardInfo* ShardInfo = ShardInfoTable.Find(ShardIndex);
		if (ShardInfo)
		{
			FString ShardPath = ShardInfo->FilePath;
			// Start async memory-mapping immediately to hide I/O latency
			// Note: Capture ShardPath by value since async task may outlive this scope
			// Note: Capturing 'this' is safe because we wait for futures via Get() before returning
			MappedShardFutures.Add(Async(EAsyncExecution::ThreadPool, [this, ShardPath]()
			{
				return MapShardFile(ShardPath);
			}));
		}
		else
		{
			// Add empty future as placeholder
			MappedShardFutures.Add(MakeFulfilledPromise<TSharedPtr<FMappedShardFile>>(nullptr).GetFuture());
		}
	}
	
	// OPTIMIZATION 4: Pre-compute requested trajectory ID set (used by all shards)
	// Create once outside the parallel loop to avoid redundant construction
	TSet<int64> RequestedTrajIdSet(TrajectoryIds);
	
	// Pre-compute array copy once (used by inner ParallelFor in each shard)
	TArray<int64> TrajIdsArray = TrajectoryIds;
	
	// Process each relevant shard to accumulate trajectory data across time intervals
	// Use ParallelFor to process multiple shards concurrently
	ParallelFor(RelevantShards.Num(), [&](int32 ShardArrayIndex)
	{
		int32 ShardIndex = RelevantShards[ShardArrayIndex];
		const FShardInfo* ShardInfo = ShardInfoTable.Find(ShardIndex);
		if (!ShardInfo)
		{
			return;
		}

		FString ShardPath = ShardInfo->FilePath;

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.FileExists(*ShardPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Shard file not found: %s"), *ShardPath);
			return;
		}

		// OPTIMIZATION 3: Wait for prefetched shard mapping to complete
		// By the time we reach this shard, its I/O may already be done
		TSharedPtr<FMappedShardFile> MappedShard = MappedShardFutures[ShardArrayIndex].Get();
		if (!MappedShard.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to map shard file: %s"), *ShardPath);
			return;
		}

		// Read shard header from mapped memory
		FDataBlockHeaderBinary ShardHeader;
		const uint8* MappedData = MappedShard->MappedRegion->GetMappedPtr();
		int64 MappedSize = MappedShard->MappedRegion->GetMappedSize();
		
		if (!ReadShardHeaderMapped(MappedData, MappedSize, ShardHeader))
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: Failed to read shard header: %s"), *ShardPath);
			return;
		}

		UE_LOG(LogTemp, Verbose, TEXT("TrajectoryDataLoader: Processing shard %d with %d trajectory entries"),
			ShardIndex, ShardHeader.TrajectoryEntryCount);

		// OPTIMIZATION 4: Build index only for requested trajectory IDs
		// Instead of indexing all entries, only index those we're interested in
		TMap<int64, int32> TrajIdToEntryIndex;
		TrajIdToEntryIndex.Reserve(FMath::Min(TrajectoryIds.Num(), ShardHeader.TrajectoryEntryCount));
		
		int64 DataSectionStart = ShardHeader.DataSectionOffset;
		int32 EntrySize = DatasetMeta.EntrySizeBytes;
		
		for (int32 EntryIdx = 0; EntryIdx < ShardHeader.TrajectoryEntryCount; ++EntryIdx)
		{
			int64 EntryOffset = DataSectionStart + (EntryIdx * EntrySize);
			if (EntryOffset + (int64)sizeof(uint64) > MappedSize)
			{
				break;
			}
			
			// Read trajectory ID from entry
			uint64 EntryTrajId;
			FMemory::Memcpy(&EntryTrajId, MappedData + EntryOffset, sizeof(uint64));
			
			// OPTIMIZATION 4: Only index entries for trajectories we're loading
			if (RequestedTrajIdSet.Contains(EntryTrajId))
			{
				TrajIdToEntryIndex.Add(EntryTrajId, EntryIdx);
			}
		}
		
		UE_LOG(LogTemp, Verbose, TEXT("TrajectoryDataLoader: Built index for %d requested entries in shard %d"),
			TrajIdToEntryIndex.Num(), ShardIndex);

		// OPTIMIZATION 2: Use per-shard result structure to eliminate global lock contention
		// Collect samples for this shard into a local structure with per-shard mutex
		FShardTrajectoryData& ShardResult = ShardResults[ShardArrayIndex];
		ShardResult.ShardIndex = ShardIndex;
		
		// Capture ShardInfo values to avoid pointer lifetime issues
		int32 ShardStartTimeStep = ShardInfo->StartTimeStep;
		int32 ShardEndTimeStep = ShardInfo->EndTimeStep;
		
		// Note: ParallelFor in UE uses the task graph system which automatically manages thread pools
		// The task graph naturally limits parallelism based on available worker threads
		// UE's scheduler will balance work between game thread and worker threads automatically
		
		ParallelFor(TrajIdsArray.Num(), [this, &TrajIdsArray, &TrajMetaMap, &ShardResult,
			MappedData, MappedSize, &ShardHeader, &DatasetMeta, &Params, ShardStartTimeStep, ShardEndTimeStep,
			&TrajIdToEntryIndex, DataSectionStart, EntrySize](int32 Index)
		{
			int64 TrajId = TrajIdsArray[Index];
			const FTrajectoryMetaBinary* TrajMeta = TrajMetaMap.Find(TrajId);
			
			if (!TrajMeta)
			{
				return;
			}
			
			// Check if this trajectory has data in this time interval
			if (TrajMeta->EndTimeStep < ShardStartTimeStep || TrajMeta->StartTimeStep > ShardEndTimeStep)
			{
				// Trajectory doesn't exist in this time interval
				return;
			}
			
			// Use index for O(1) lookup instead of O(M) linear search
			const int32* EntryIdxPtr = TrajIdToEntryIndex.Find(TrajId);
			if (!EntryIdxPtr)
			{
				// This trajectory doesn't have an entry in this shard
				return;
			}
			
			int32 EntryIdx = *EntryIdxPtr;
			int64 EntryOffset = DataSectionStart + (EntryIdx * EntrySize);
			
			if (EntryOffset + (int64)sizeof(uint64) > MappedSize)
			{
				return;
			}
			
			// Parse the entry to extract samples
			int32 Offset = sizeof(uint64);
			
			int32 StartTimeStepInInterval;
			FMemory::Memcpy(&StartTimeStepInInterval, MappedData + EntryOffset + Offset, sizeof(int32));
			Offset += sizeof(int32);
			
			int32 ValidSampleCount;
			FMemory::Memcpy(&ValidSampleCount, MappedData + EntryOffset + Offset, sizeof(int32));
			Offset += sizeof(int32);
			
			// Extract samples for the requested time range
			TArray<FVector> ShardSamples;
			
			// Determine the valid sample range within this shard's interval
			// StartTimeStepInInterval is relative to the start of this shard interval (0..TimeStepIntervalSize-1)
			// ValidSampleCount tells us how many consecutive samples are valid starting from StartTimeStepInInterval
			int32 ValidRangeStart = StartTimeStepInInterval;
			int32 ValidRangeEnd = StartTimeStepInInterval + ValidSampleCount;
			
			// Clamp to requested time range (relative to shard start)
			int32 LoadStart = ValidRangeStart;
			int32 LoadEnd = ValidRangeEnd;
			
			if (Params.StartTimeStep >= 0)
			{
				int32 RequestedStartRelative = Params.StartTimeStep - ShardStartTimeStep;
				LoadStart = FMath::Max(LoadStart, RequestedStartRelative);
			}
			
			if (Params.EndTimeStep >= 0)
			{
				int32 RequestedEndRelative = Params.EndTimeStep - ShardStartTimeStep + 1;
				LoadEnd = FMath::Min(LoadEnd, RequestedEndRelative);
			}
			
			// Ensure we stay within the shard's time step interval
			LoadStart = FMath::Clamp(LoadStart, 0, ShardHeader.TimeStepIntervalSize);
			LoadEnd = FMath::Clamp(LoadEnd, 0, ShardHeader.TimeStepIntervalSize);
			
			if (LoadStart >= LoadEnd)
			{
				return; // No samples to load from this shard for this trajectory
			}
			
			// Calculate position data offset for the first sample to load
			int32 PosDataStart = Offset + (LoadStart * 12);
			
			// Validate we have enough data
			int64 PosDataSize = (LoadEnd - LoadStart) * 12;
			if (EntryOffset + PosDataStart + PosDataSize > MappedSize)
			{
				return; // Not enough data
			}
			
			// Get pointer to the position data array (as binary structs)
			const uint8* PosDataPtr = MappedData + EntryOffset + PosDataStart;
			const FPositionSampleBinary* BinarySamples = reinterpret_cast<const FPositionSampleBinary*>(PosDataPtr);
			
			// FAST PATH: Sample rate 1 - bulk load all consecutive samples with single memcpy
			if (Params.SampleRate == 1)
			{
				// Calculate exact number of samples
				int32 NumSamples = LoadEnd - LoadStart;
				
				// Pre-allocate array to exact size
				ShardSamples.Reserve(NumSamples);
				ShardSamples.SetNum(NumSamples);
				
				// Bulk copy all position data at once (most efficient method)
				// Source: binary struct array from mapped memory (FPositionSampleBinary = 3 floats = FVector layout)
				// Dest: FVector array in ShardSamples
				// FPositionSampleBinary and FVector have identical memory layout (3 consecutive floats)
				FMemory::Memcpy(ShardSamples.GetData(), BinarySamples, NumSamples * sizeof(FPositionSampleBinary));
			}
			else
			{
				// SLOW PATH: Sample rate > 1 - load individual samples with skipping
				int32 NumSamplesToLoad = ((LoadEnd - LoadStart) + Params.SampleRate - 1) / Params.SampleRate;
				ShardSamples.Reserve(NumSamplesToLoad);
				
				for (int32 TimeStepIdx = LoadStart; TimeStepIdx < LoadEnd; TimeStepIdx += Params.SampleRate)
				{
					int32 SampleIdx = TimeStepIdx - LoadStart;
					const FPositionSampleBinary& BinarySample = BinarySamples[SampleIdx];
					
					// Filter out NaN samples and add position directly
					if (!FMath::IsNaN(BinarySample.X) && !FMath::IsNaN(BinarySample.Y) && !FMath::IsNaN(BinarySample.Z))
					{
						ShardSamples.Add(FVector(BinarySample.X, BinarySample.Y, BinarySample.Z));
					}
				}
			}
			
			// OPTIMIZATION 2: Store in per-shard result map with per-shard locking
			// Locking scope is much smaller (per-shard instead of global)
			if (ShardSamples.Num() > 0)
			{
				FScopeLock Lock(&ShardResult.Mutex);
				ShardResult.TrajectorySamples.Add(TrajId, MoveTemp(ShardSamples));
			}
		});
	});
	
	// OPTIMIZATION 1 & 2: Merge shard results sequentially in chronological order
	// This maintains temporal ordering while avoiding all locking during parallel processing
	UE_LOG(LogTemp, Verbose, TEXT("TrajectoryDataLoader: Merging results from %d shards"), ShardResults.Num());
	
	for (const FShardTrajectoryData& ShardResult : ShardResults)
	{
		for (const auto& SampleEntry : ShardResult.TrajectorySamples)
		{
			int64 TrajId = SampleEntry.Key;
			const TArray<FVector>& ShardSamples = SampleEntry.Value;
			
			FLoadedTrajectory* LoadedTraj = TrajectoryMap.Find(TrajId);
			if (LoadedTraj)
			{
				// Append samples in chronological order (shards are processed in sorted order)
				LoadedTraj->Samples.Append(ShardSamples);
			}
		}
	}

	// Convert map to array
	TArray<FLoadedTrajectory> NewTrajectories;
	for (auto& TrajEntry : TrajectoryMap)
	{
		FLoadedTrajectory& Traj = TrajEntry.Value;
		
		// Note: Samples are already in temporal order because:
		// 1. Shards are merged sequentially in chronological order (sorted by time)
		// 2. Within each shard, samples are loaded in consecutive order
		// No sorting needed!
		
		// Calculate memory usage
		int64 TrajMemory = sizeof(FLoadedTrajectory) + Traj.Samples.Num() * sizeof(FVector);
		MemoryUsed += TrajMemory;
		
		NewTrajectories.Add(MoveTemp(Traj));
	}

	// Create a new loaded dataset entry
	FLoadedDataset LoadedDataset;
	LoadedDataset.LoadParams = Params;
	LoadedDataset.DatasetInfo = DatasetInfo;
	LoadedDataset.Trajectories = MoveTemp(NewTrajectories);
	LoadedDataset.MemoryUsedBytes = MemoryUsed;

	// Add to loaded datasets array
	LoadedDatasets.Add(MoveTemp(LoadedDataset));
	CurrentMemoryUsage += MemoryUsed;

	Result.bSuccess = true;
	Result.Trajectories = LoadedDatasets.Last().Trajectories;
	Result.MemoryUsedBytes = MemoryUsed;

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataLoader: Successfully loaded %d trajectories, using %s memory (Total datasets: %d, Total memory: %s)"),
		Result.Trajectories.Num(), *UTrajectoryDataBlueprintLibrary::FormatMemorySize(MemoryUsed),
		LoadedDatasets.Num(), *UTrajectoryDataBlueprintLibrary::FormatMemorySize(CurrentMemoryUsage));

	// Warn if accumulating many datasets or high memory usage
	if (LoadedDatasets.Num() > 10)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: %d datasets are currently loaded. Consider calling UnloadAll() if you no longer need previous datasets to free memory."),
			LoadedDatasets.Num());
	}

	// Warn if memory usage is high (e.g., > 10 GB)
	constexpr int64 HighMemoryThreshold = 10LL * 1024 * 1024 * 1024; // 10 GB
	if (CurrentMemoryUsage > HighMemoryThreshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataLoader: High memory usage detected (%s). Consider calling UnloadAll() to free memory if you no longer need previous datasets."),
			*UTrajectoryDataBlueprintLibrary::FormatMemorySize(CurrentMemoryUsage));
	}

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

	// Bytes per sample: FVector Position (12 bytes: 3 floats)
	static constexpr int32 BytesPerSample = sizeof(FVector);
	
	// Memory overhead adjustment factor: accounts for container overhead, alignment, and internal structures
	// Set to 5.0 to match empirically observed memory usage
	static constexpr float MemoryOverheadFactor = 5.0f;
	
	int64 BaseMemory = NumTrajectories * TimeSteps * BytesPerSample;
	return static_cast<int64>(BaseMemory * MemoryOverheadFactor);
}

int64 UTrajectoryDataLoader::GetLoadedDataMemoryUsage() const
{
	return CurrentMemoryUsage;
}

// FTrajectoryLoadTask implementation

FTrajectoryLoadTask::FTrajectoryLoadTask(UTrajectoryDataLoader* InLoader, const FTrajectoryDatasetInfo& InDatasetInfo, const FTrajectoryLoadParams& InParams)
	: Loader(InLoader)
	, DatasetInfo(InDatasetInfo)
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
		Result = Loader->LoadTrajectoriesInternal(DatasetInfo, Params);
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
			// Reset the async loading flag BEFORE broadcasting the delegate
			// This allows OnLoadComplete handlers to start new async loads
			WeakLoader->bIsLoadingAsync = false;
			
			if (WeakLoader->OnLoadComplete.IsBound())
			{
				WeakLoader->OnLoadComplete.Broadcast(ResultCopy.bSuccess, ResultCopy);
			}
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
