// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TrajectoryDataStructures.h"
#include "TrajectoryDataTypes.h"
#include "HAL/Runnable.h"
#include "TrajectoryDataLoader.generated.h"

// Forward declarations
class FTrajectoryLoadTask;
class IMappedFileHandle;
class IMappedFileRegion;

/**
 * Helper structure to manage memory-mapped shard files
 */
struct FMappedShardFile
{
	TUniquePtr<IMappedFileHandle> MappedFileHandle;
	TUniquePtr<IMappedFileRegion> MappedRegion;
	FString ShardPath;
};

/**
 * Information about a discovered shard file
 */
struct FShardInfo
{
	int32 GlobalIntervalIndex;    // From shard header
	int32 StartTimeStep;          // Calculated: GlobalIntervalIndex * TimeStepIntervalSize + FirstTimeStep
	int32 EndTimeStep;            // Calculated: StartTimeStep + TimeStepIntervalSize - 1
	FString FilePath;             // Full path to shard file
	
	FShardInfo()
		: GlobalIntervalIndex(-1)
		, StartTimeStep(0)
		, EndTimeStep(0)
	{
	}
	
	/** Check if this shard contains data for the given time range */
	bool ContainsTimeRange(int32 RangeStart, int32 RangeEnd) const
	{
		return EndTimeStep >= RangeStart && StartTimeStep <= RangeEnd;
	}
};

/**
 * Delegate for trajectory loading progress updates
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTrajectoryLoadProgress, int32, TrajectoriesLoaded, int32, TotalTrajectories, float, ProgressPercent);

/**
 * Delegate for trajectory loading completion
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTrajectoryLoadComplete, bool, bSuccess, const FTrajectoryLoadResult&, Result);

/**
 * Manager class for loading trajectory data from binary files
 * Handles memory mapping, multi-threading, and data streaming
 */
UCLASS(BlueprintType)
class TRAJECTORYDATA_API UTrajectoryDataLoader : public UObject
{
	GENERATED_BODY()

public:
	UTrajectoryDataLoader();
	virtual ~UTrajectoryDataLoader();

	/**
	 * Validate load parameters before actually loading
	 * @param Params Load parameters to validate
	 * @return Validation result with memory estimates
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading")
	FTrajectoryLoadValidation ValidateLoadParams(const FTrajectoryLoadParams& Params);

	/**
	 * Load trajectory data synchronously (blocking)
	 * @param Params Load parameters
	 * @return Load result with trajectory data
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading")
	FTrajectoryLoadResult LoadTrajectoriesSync(const FTrajectoryLoadParams& Params);

	/**
	 * Load trajectory data asynchronously (non-blocking)
	 * Results delivered via OnLoadComplete delegate
	 * @param Params Load parameters
	 * @return True if loading started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading")
	bool LoadTrajectoriesAsync(const FTrajectoryLoadParams& Params);

	/**
	 * Cancel ongoing async loading operation
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading")
	void CancelAsyncLoad();

	/**
	 * Check if an async load is currently in progress
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Loading")
	bool IsLoadingAsync() const { return bIsLoadingAsync; }

	/**
	 * Unload all currently loaded trajectory data to free memory
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading")
	void UnloadAll();

	/**
	 * Get currently loaded trajectories
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Loading")
	const TArray<FLoadedTrajectory>& GetLoadedTrajectories() const { return LoadedTrajectories; }

	/**
	 * Get current memory usage for loaded data
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Loading")
	int64 GetLoadedDataMemoryUsage() const { return CurrentMemoryUsage; }

	/** Progress callback for async loading */
	UPROPERTY(BlueprintAssignable, Category = "Trajectory Data|Loading")
	FOnTrajectoryLoadProgress OnLoadProgress;

	/** Completion callback for async loading */
	UPROPERTY(BlueprintAssignable, Category = "Trajectory Data|Loading")
	FOnTrajectoryLoadComplete OnLoadComplete;

	/** Get singleton instance */
	static UTrajectoryDataLoader* Get();

private:
	/** Read dataset-meta.bin file */
	bool ReadDatasetMeta(const FString& DatasetPath, FDatasetMetaBinary& OutMeta);

	/** Read dataset-trajmeta.bin file */
	bool ReadTrajectoryMeta(const FString& DatasetPath, TArray<FTrajectoryMetaBinary>& OutMetas);

	/** Read shard file header */
	bool ReadShardHeader(const FString& ShardPath, FDataBlockHeaderBinary& OutHeader);

	/** Load trajectory data from shard file */
	bool LoadTrajectoryFromShard(const FString& ShardPath, const FDataBlockHeaderBinary& Header,
		const FTrajectoryMetaBinary& TrajMeta, const FDatasetMetaBinary& DatasetMeta,
		const FTrajectoryLoadParams& Params, FLoadedTrajectory& OutTrajectory);

	/** Memory-mapped version: Read shard file header from mapped region */
	bool ReadShardHeaderMapped(const uint8* MappedData, int64 MappedSize, FDataBlockHeaderBinary& OutHeader);

	/** Memory-mapped version: Load trajectory data from mapped shard file */
	bool LoadTrajectoryFromShardMapped(const uint8* MappedData, int64 MappedSize,
		const FDataBlockHeaderBinary& Header, const FTrajectoryMetaBinary& TrajMeta,
		const FDatasetMetaBinary& DatasetMeta, const FTrajectoryLoadParams& Params,
		FLoadedTrajectory& OutTrajectory);

	/** Open and map a shard file for reading */
	TSharedPtr<FMappedShardFile> MapShardFile(const FString& ShardPath);

	/** Discover all shard files in dataset and build information table */
	TMap<int32, FShardInfo> DiscoverShardFiles(const FString& DatasetPath, const FDatasetMetaBinary& DatasetMeta);

	/** Internal implementation of synchronous loading */
	FTrajectoryLoadResult LoadTrajectoriesInternal(const FTrajectoryLoadParams& Params);

	/** Build list of trajectory IDs to load based on selection strategy */
	TArray<int64> BuildTrajectoryIdList(const FTrajectoryLoadParams& Params,
		const FDatasetMetaBinary& DatasetMeta, const TArray<FTrajectoryMetaBinary>& TrajMetas);

	/** Calculate memory requirement for load parameters */
	int64 CalculateMemoryRequirement(const FTrajectoryLoadParams& Params,
		const FDatasetMetaBinary& DatasetMeta);

	/** Get shard file path for a given interval index */
	FString GetShardFilePath(const FString& DatasetPath, int32 IntervalIndex);

	/** Currently loaded trajectories */
	UPROPERTY()
	TArray<FLoadedTrajectory> LoadedTrajectories;

	/** Current memory usage in bytes */
	int64 CurrentMemoryUsage;

	/** Whether async loading is in progress */
	bool bIsLoadingAsync;

	/** Async loading task */
	TSharedPtr<FTrajectoryLoadTask> AsyncLoadTask;

	/** Singleton instance */
	static UTrajectoryDataLoader* Instance;

	/** Critical section for thread safety */
	FCriticalSection LoadMutex;

	friend class FTrajectoryLoadTask;
};

/**
 * Async task for loading trajectory data in background thread
 */
class FTrajectoryLoadTask : public FRunnable
{
public:
	FTrajectoryLoadTask(UTrajectoryDataLoader* InLoader, const FTrajectoryLoadParams& InParams);
	virtual ~FTrajectoryLoadTask();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Check if task should stop */
	bool ShouldStop() const { return bShouldStop; }

	/** Get result after completion */
	FTrajectoryLoadResult GetResult() const { return Result; }

private:
	UTrajectoryDataLoader* Loader;
	FTrajectoryLoadParams Params;
	FTrajectoryLoadResult Result;
	FRunnableThread* Thread;
	bool bShouldStop;
};
