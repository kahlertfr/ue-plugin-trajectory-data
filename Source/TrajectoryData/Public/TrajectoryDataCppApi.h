// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

/**
 * Structure representing a single position sample at a specific time step
 * C++ Only: Not exposed to Blueprints
 */
struct TRAJECTORYDATA_API FTrajectorySample
{
	/** Trajectory ID */
	int64 TrajectoryId;
	
	/** Time step of this sample */
	int32 TimeStep;
	
	/** 3D position */
	FVector Position;
	
	/** Whether this sample is valid (not NaN) */
	bool bIsValid;
	
	FTrajectorySample()
		: TrajectoryId(0)
		, TimeStep(0)
		, Position(FVector::ZeroVector)
		, bIsValid(false)
	{
	}
	
	FTrajectorySample(int64 InTrajectoryId, int32 InTimeStep, const FVector& InPosition, bool bInIsValid)
		: TrajectoryId(InTrajectoryId)
		, TimeStep(InTimeStep)
		, Position(InPosition)
		, bIsValid(bInIsValid)
	{
	}
};

/**
 * Structure representing multiple samples for a trajectory over a time range
 * C++ Only: Not exposed to Blueprints
 */
struct TRAJECTORYDATA_API FTrajectoryTimeSeries
{
	/** Trajectory ID */
	int64 TrajectoryId;
	
	/** Start time step (inclusive) */
	int32 StartTimeStep;
	
	/** End time step (inclusive) */
	int32 EndTimeStep;
	
	/** Position samples indexed by (TimeStep - StartTimeStep) */
	TArray<FVector> Samples;
	
	/** Object half-extent in meters */
	FVector Extent;
	
	FTrajectoryTimeSeries()
		: TrajectoryId(0)
		, StartTimeStep(0)
		, EndTimeStep(0)
		, Extent(FVector(0.1f, 0.1f, 0.1f))
	{
	}
};

/**
 * Result structure for single time step queries
 * C++ Only: Not exposed to Blueprints
 */
struct TRAJECTORYDATA_API FTrajectoryQueryResult
{
	/** Whether the query succeeded */
	bool bSuccess;
	
	/** Error message if query failed */
	FString ErrorMessage;
	
	/** Loaded samples (one per trajectory for single time step queries) */
	TArray<FTrajectorySample> Samples;
	
	FTrajectoryQueryResult()
		: bSuccess(false)
	{
	}
};

/**
 * Result structure for time range queries
 * C++ Only: Not exposed to Blueprints
 */
struct TRAJECTORYDATA_API FTrajectoryTimeRangeResult
{
	/** Whether the query succeeded */
	bool bSuccess;
	
	/** Error message if query failed */
	FString ErrorMessage;
	
	/** Loaded time series (one per trajectory with multiple samples) */
	TArray<FTrajectoryTimeSeries> TimeSeries;
	
	FTrajectoryTimeRangeResult()
		: bSuccess(false)
	{
	}
};

// Forward declaration
class FTrajectoryQueryTask;

/**
 * Callback signature for single time step query completion
 * Called on game thread after async query completes
 */
DECLARE_DELEGATE_OneParam(FOnTrajectoryQueryComplete, const FTrajectoryQueryResult&);

/**
 * Callback signature for time range query completion
 * Called on game thread after async query completes
 */
DECLARE_DELEGATE_OneParam(FOnTrajectoryTimeRangeComplete, const FTrajectoryTimeRangeResult&);

/**
 * C++ API for loading trajectory data from other plugins
 * 
 * This class provides a simple, thread-safe interface for querying trajectory data
 * without requiring Blueprint integration. All queries execute on background threads
 * to avoid game thread lag.
 * 
 * Usage Example - Single Time Step:
 * @code
 * FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
 * TArray<int64> TrajectoryIds = {100, 200, 300};
 * Api->QuerySingleTimeStepAsync(DatasetPath, TrajectoryIds, 50, 
 *     FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result) {
 *         if (Result.bSuccess) {
 *             for (const FTrajectorySample& Sample : Result.Samples) {
 *                 UE_LOG(LogTemp, Log, TEXT("Trajectory %lld at time %d: (%f, %f, %f)"),
 *                     Sample.TrajectoryId, Sample.TimeStep, 
 *                     Sample.Position.X, Sample.Position.Y, Sample.Position.Z);
 *             }
 *         }
 *     })
 * );
 * @endcode
 * 
 * Usage Example - Time Range:
 * @code
 * FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
 * TArray<int64> TrajectoryIds = {100, 200};
 * Api->QueryTimeRangeAsync(DatasetPath, TrajectoryIds, 0, 100,
 *     FOnTrajectoryTimeRangeComplete::CreateLambda([](const FTrajectoryTimeRangeResult& Result) {
 *         if (Result.bSuccess) {
 *             for (const FTrajectoryTimeSeries& Series : Result.TimeSeries) {
 *                 UE_LOG(LogTemp, Log, TEXT("Trajectory %lld: %d samples from time %d to %d"),
 *                     Series.TrajectoryId, Series.Samples.Num(), 
 *                     Series.StartTimeStep, Series.EndTimeStep);
 *             }
 *         }
 *     })
 * );
 * @endcode
 */
class TRAJECTORYDATA_API FTrajectoryDataCppApi
{
public:
	/**
	 * Get the singleton instance of the C++ API
	 * Thread-safe singleton initialization using double-checked locking
	 * @return Pointer to the API instance
	 */
	static FTrajectoryDataCppApi* Get();
	
	/**
	 * Query trajectory data for a single time step (async)
	 * Returns one sample per trajectory at the specified time step
	 * Executes on background thread, callback invoked on game thread
	 * 
	 * @param DatasetPath Full path to the dataset directory
	 * @param TrajectoryIds Array of trajectory IDs to query
	 * @param TimeStep Time step to query
	 * @param OnComplete Callback invoked when query completes
	 * @return True if query was successfully started, false otherwise
	 */
	bool QuerySingleTimeStepAsync(
		const FString& DatasetPath,
		const TArray<int64>& TrajectoryIds,
		int32 TimeStep,
		FOnTrajectoryQueryComplete OnComplete
	);
	
	/**
	 * Query trajectory data for a time range (async)
	 * Returns multiple samples per trajectory over the specified time range
	 * Executes on background thread, callback invoked on game thread
	 * 
	 * @param DatasetPath Full path to the dataset directory
	 * @param TrajectoryIds Array of trajectory IDs to query
	 * @param StartTimeStep Start time step (inclusive)
	 * @param EndTimeStep End time step (inclusive)
	 * @param OnComplete Callback invoked when query completes
	 * @return True if query was successfully started, false otherwise
	 */
	bool QueryTimeRangeAsync(
		const FString& DatasetPath,
		const TArray<int64>& TrajectoryIds,
		int32 StartTimeStep,
		int32 EndTimeStep,
		FOnTrajectoryTimeRangeComplete OnComplete
	);
	
	/**
	 * Destructor - ensures all async tasks are cleaned up
	 */
	~FTrajectoryDataCppApi();

private:
	/** Private constructor for singleton */
	FTrajectoryDataCppApi();
	
	/** Singleton instance */
	static FTrajectoryDataCppApi* Instance;
	
	/** Active query tasks */
	TArray<TSharedPtr<FTrajectoryQueryTask>> ActiveTasks;
	
	/** Critical section for thread safety */
	FCriticalSection TaskMutex;
	
	/** Clean up completed tasks */
	void CleanupCompletedTasks();
	
	friend class FTrajectoryQueryTask;
};

/**
 * Async task for executing trajectory queries on background thread
 */
class FTrajectoryQueryTask : public FRunnable
{
public:
	enum class EQueryType
	{
		SingleTimeStep,
		TimeRange
	};
	
	FTrajectoryQueryTask(
		EQueryType InQueryType,
		const FString& InDatasetPath,
		const TArray<int64>& InTrajectoryIds,
		int32 InStartTimeStep,
		int32 InEndTimeStep,
		FOnTrajectoryQueryComplete InSingleCallback,
		FOnTrajectoryTimeRangeComplete InRangeCallback
	);
	
	virtual ~FTrajectoryQueryTask();
	
	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	
	/** Check if task has completed */
	bool IsComplete() const { return bIsComplete; }
	
private:
	/** Type of query to execute */
	EQueryType QueryType;
	
	/** Dataset path */
	FString DatasetPath;
	
	/** Trajectory IDs to query */
	TArray<int64> TrajectoryIds;
	
	/** Start time step */
	int32 StartTimeStep;
	
	/** End time step */
	int32 EndTimeStep;
	
	/** Callback for single time step queries */
	FOnTrajectoryQueryComplete SingleTimeStepCallback;
	
	/** Callback for time range queries */
	FOnTrajectoryTimeRangeComplete TimeRangeCallback;
	
	/** Result for single time step query */
	FTrajectoryQueryResult SingleResult;
	
	/** Result for time range query */
	FTrajectoryTimeRangeResult RangeResult;
	
	/** Thread running this task */
	FRunnableThread* Thread;
	
	/** Whether task should stop */
	bool bShouldStop;
	
	/** Whether task has completed */
	bool bIsComplete;
	
	/** Execute single time step query */
	void ExecuteSingleTimeStepQuery();
	
	/** Execute time range query */
	void ExecuteTimeRangeQuery();
	
	/** Callback to invoke results on game thread */
	void InvokeCallbackOnGameThread();
};
