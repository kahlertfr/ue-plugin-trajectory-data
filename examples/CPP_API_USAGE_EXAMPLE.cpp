// Example C++ usage of the Trajectory Data C++ API for other plugins
// This demonstrates the new C++ API that can be called from other plugins

#include "TrajectoryDataCppApi.h"
#include "TrajectoryDataManager.h"

/**
 * Example 1: Query single time step asynchronously
 * This is useful when you need the position of multiple trajectories at a specific point in time
 */
void ExampleQuerySingleTimeStep()
{
	// Get the C++ API instance
	FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
	
	// Specify the dataset path (you can get this from UTrajectoryDataManager)
	FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/my_scenario/my_dataset");
	
	// Specify which trajectories you want to query
	TArray<int64> TrajectoryIds = {100, 200, 300, 400, 500};
	
	// Specify the time step you're interested in
	int32 TimeStep = 50;
	
	// Execute async query with callback
	bool bStarted = Api->QuerySingleTimeStepAsync(
		DatasetPath,
		TrajectoryIds,
		TimeStep,
		FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
		{
			if (Result.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("Single time step query succeeded! Retrieved %d samples"), 
					Result.Samples.Num());
				
				for (const FTrajectorySample& Sample : Result.Samples)
				{
					if (Sample.bIsValid)
					{
						UE_LOG(LogTemp, Log, TEXT("Trajectory %lld at time %d: Position = (%f, %f, %f)"),
							Sample.TrajectoryId,
							Sample.TimeStep,
							Sample.Position.X,
							Sample.Position.Y,
							Sample.Position.Z);
						
						// Use the position data
						// Example: Spawn an actor at this position
						// GetWorld()->SpawnActor<AMyActor>(AMyActor::StaticClass(), Sample.Position, FRotator::ZeroRotator);
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("Trajectory %lld has invalid data at time %d"),
							Sample.TrajectoryId, Sample.TimeStep);
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Single time step query failed: %s"), *Result.ErrorMessage);
			}
		})
	);
	
	if (bStarted)
	{
		UE_LOG(LogTemp, Log, TEXT("Single time step query started successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to start single time step query"));
	}
}

/**
 * Example 2: Query time range asynchronously
 * This is useful when you need the complete trajectory path over a time period
 */
void ExampleQueryTimeRange()
{
	FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
	
	FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/my_scenario/my_dataset");
	TArray<int64> TrajectoryIds = {100, 200};
	int32 StartTimeStep = 0;
	int32 EndTimeStep = 100;
	
	bool bStarted = Api->QueryTimeRangeAsync(
		DatasetPath,
		TrajectoryIds,
		StartTimeStep,
		EndTimeStep,
		FOnTrajectoryTimeRangeComplete::CreateLambda([](const FTrajectoryTimeRangeResult& Result)
		{
			if (Result.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("Time range query succeeded! Retrieved %d trajectories"), 
					Result.TimeSeries.Num());
				
				for (const FTrajectoryTimeSeries& Series : Result.TimeSeries)
				{
					UE_LOG(LogTemp, Log, TEXT("Trajectory %lld: %d samples from time %d to %d"),
						Series.TrajectoryId,
						Series.Samples.Num(),
						Series.StartTimeStep,
						Series.EndTimeStep);
					
					UE_LOG(LogTemp, Log, TEXT("  Extent: (%f, %f, %f)"),
						Series.Extent.X, Series.Extent.Y, Series.Extent.Z);
					
					// Process trajectory samples
					for (int32 i = 0; i < Series.Samples.Num(); ++i)
					{
						int32 TimeStep = Series.StartTimeStep + i;
						const FVector& Position = Series.Samples[i];
						
						// Check if sample is valid (non-zero indicates valid data)
						if (!Position.IsNearlyZero())
						{
							// Example: Draw debug line between consecutive positions
							if (i > 0)
							{
								const FVector& PrevPosition = Series.Samples[i - 1];
								// DrawDebugLine(GetWorld(), PrevPosition, Position, FColor::Green, false, 10.0f);
							}
						}
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Time range query failed: %s"), *Result.ErrorMessage);
			}
		})
	);
	
	if (bStarted)
	{
		UE_LOG(LogTemp, Log, TEXT("Time range query started successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to start time range query"));
	}
}

/**
 * Example 3: Using the API with UTrajectoryDataManager to get dataset paths
 * This shows how to integrate with the existing plugin infrastructure
 */
void ExampleIntegratedUsage()
{
	// First, scan available datasets using the manager
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	if (!Manager || !Manager->ScanDatasets())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to scan datasets"));
		return;
	}
	
	// Get dataset information
	FTrajectoryDatasetInfo DatasetInfo;
	if (!Manager->GetDatasetInfo(TEXT("my_dataset"), DatasetInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("Dataset not found"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("Found dataset: %s"), *DatasetInfo.DatasetName);
	UE_LOG(LogTemp, Log, TEXT("  Path: %s"), *DatasetInfo.DatasetPath);
	UE_LOG(LogTemp, Log, TEXT("  Trajectories: %lld"), DatasetInfo.TotalTrajectories);
	UE_LOG(LogTemp, Log, TEXT("  Time range: %d - %d"), 
		DatasetInfo.Metadata.FirstTimeStep, DatasetInfo.Metadata.LastTimeStep);
	
	// Now use the C++ API to query specific trajectories
	FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
	
	// Query first few trajectory IDs at a specific time
	TArray<int64> TrajectoryIds;
	for (int64 i = DatasetInfo.Metadata.FirstTrajectoryId; 
		i < DatasetInfo.Metadata.FirstTrajectoryId + 10; 
		++i)
	{
		TrajectoryIds.Add(i);
	}
	
	int32 MidTimeStep = (DatasetInfo.Metadata.FirstTimeStep + DatasetInfo.Metadata.LastTimeStep) / 2;
	
	Api->QuerySingleTimeStepAsync(
		DatasetInfo.DatasetPath,
		TrajectoryIds,
		MidTimeStep,
		FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
		{
			if (Result.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("Query completed: %d samples retrieved"), Result.Samples.Num());
			}
		})
	);
}

/**
 * Example 4: Building a custom component that uses the C++ API
 * This shows how a plugin component might use the API for interactive features
 */
class MYPLUGIN_API UMyTrajectoryQueryComponent : public UActorComponent
{
	GENERATED_BODY()
	
public:
	/** Query trajectories at the current simulation time */
	void QueryCurrentTimeStep(const FString& DatasetPath, const TArray<int64>& TrajectoryIds, int32 TimeStep)
	{
		FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
		
		Api->QuerySingleTimeStepAsync(
			DatasetPath,
			TrajectoryIds,
			TimeStep,
			FOnTrajectoryQueryComplete::CreateUObject(this, &UMyTrajectoryQueryComponent::OnQueryComplete)
		);
	}
	
	/** Query trajectory paths for visualization */
	void QueryTrajectoryPaths(const FString& DatasetPath, const TArray<int64>& TrajectoryIds, int32 StartTime, int32 EndTime)
	{
		FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
		
		Api->QueryTimeRangeAsync(
			DatasetPath,
			TrajectoryIds,
			StartTime,
			EndTime,
			FOnTrajectoryTimeRangeComplete::CreateUObject(this, &UMyTrajectoryQueryComponent::OnPathQueryComplete)
		);
	}
	
private:
	void OnQueryComplete(const FTrajectoryQueryResult& Result)
	{
		if (!Result.bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("Query failed: %s"), *Result.ErrorMessage);
			return;
		}
		
		// Update visualization
		UpdatePositions(Result.Samples);
	}
	
	void OnPathQueryComplete(const FTrajectoryTimeRangeResult& Result)
	{
		if (!Result.bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("Path query failed: %s"), *Result.ErrorMessage);
			return;
		}
		
		// Update trajectory visualization
		UpdateTrajectoryPaths(Result.TimeSeries);
	}
	
	void UpdatePositions(const TArray<FTrajectorySample>& Samples)
	{
		// Example: Update actor positions
		for (const FTrajectorySample& Sample : Samples)
		{
			if (Sample.bIsValid)
			{
				// Find or create actor for this trajectory
				// AActor* Actor = FindOrCreateActor(Sample.TrajectoryId);
				// if (Actor)
				// {
				//     Actor->SetActorLocation(Sample.Position);
				// }
			}
		}
	}
	
	void UpdateTrajectoryPaths(const TArray<FTrajectoryTimeSeries>& TimeSeries)
	{
		// Example: Create spline components for each trajectory
		for (const FTrajectoryTimeSeries& Series : TimeSeries)
		{
			// USplineComponent* Spline = CreateSplineForTrajectory(Series.TrajectoryId);
			// if (Spline)
			// {
			//     Spline->ClearSplinePoints();
			//     for (int32 i = 0; i < Series.Samples.Num(); ++i)
			//     {
			//         if (!Series.Samples[i].IsNearlyZero())
			//         {
			//             Spline->AddSplinePoint(Series.Samples[i], ESplineCoordinateSpace::World);
			//         }
			//     }
			// }
		}
	}
};

/**
 * Example 5: Batch processing multiple queries
 * Demonstrates handling multiple concurrent queries
 */
void ExampleBatchQueries()
{
	FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
	FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/my_scenario/my_dataset");
	
	// Query multiple time steps in parallel
	TArray<int32> TimeSteps = {0, 25, 50, 75, 100};
	TArray<int64> TrajectoryIds = {100, 200, 300};
	
	for (int32 TimeStep : TimeSteps)
	{
		Api->QuerySingleTimeStepAsync(
			DatasetPath,
			TrajectoryIds,
			TimeStep,
			FOnTrajectoryQueryComplete::CreateLambda([TimeStep](const FTrajectoryQueryResult& Result)
			{
				if (Result.bSuccess)
				{
					UE_LOG(LogTemp, Log, TEXT("Time step %d query complete: %d samples"), 
						TimeStep, Result.Samples.Num());
				}
			})
		);
	}
	
	UE_LOG(LogTemp, Log, TEXT("Started %d parallel queries"), TimeSteps.Num());
}

/**
 * Example 6: Interactive time scrubbing
 * Shows how to implement a time scrubber that updates in real-time
 */
class UMyTimeScrubbingComponent : public UActorComponent
{
	GENERATED_BODY()
	
public:
	void InitializeScrubbing(const FString& InDatasetPath, const TArray<int64>& InTrajectoryIds)
	{
		DatasetPath = InDatasetPath;
		TrajectoryIds = InTrajectoryIds;
		CurrentTimeStep = 0;
		bIsQuerying = false;
	}
	
	void ScrubToTimeStep(int32 TimeStep)
	{
		if (bIsQuerying)
		{
			// Ignore requests while a query is in progress
			return;
		}
		
		CurrentTimeStep = TimeStep;
		bIsQuerying = true;
		
		FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
		Api->QuerySingleTimeStepAsync(
			DatasetPath,
			TrajectoryIds,
			TimeStep,
			FOnTrajectoryQueryComplete::CreateUObject(this, &UMyTimeScrubbingComponent::OnScrubQueryComplete)
		);
	}
	
private:
	FString DatasetPath;
	TArray<int64> TrajectoryIds;
	int32 CurrentTimeStep;
	bool bIsQuerying;
	
	void OnScrubQueryComplete(const FTrajectoryQueryResult& Result)
	{
		bIsQuerying = false;
		
		if (Result.bSuccess)
		{
			// Update visualization with new positions
			for (const FTrajectorySample& Sample : Result.Samples)
			{
				if (Sample.bIsValid)
				{
					// Update actor position
					// UpdateActorPosition(Sample.TrajectoryId, Sample.Position);
				}
			}
		}
	}
};

/**
 * Example 7: Memory-efficient streaming for large time ranges
 * Query time ranges in chunks to avoid loading too much data at once
 */
void ExampleStreamingQuery()
{
	FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
	FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/my_scenario/my_dataset");
	TArray<int64> TrajectoryIds = {100, 200};
	
	// Instead of querying the full range at once, query in chunks
	int32 FullStartTime = 0;
	int32 FullEndTime = 1000;
	int32 ChunkSize = 100;
	
	for (int32 ChunkStart = FullStartTime; ChunkStart < FullEndTime; ChunkStart += ChunkSize)
	{
		int32 ChunkEnd = FMath::Min(ChunkStart + ChunkSize - 1, FullEndTime);
		
		Api->QueryTimeRangeAsync(
			DatasetPath,
			TrajectoryIds,
			ChunkStart,
			ChunkEnd,
			FOnTrajectoryTimeRangeComplete::CreateLambda([ChunkStart, ChunkEnd](const FTrajectoryTimeRangeResult& Result)
			{
				if (Result.bSuccess)
				{
					UE_LOG(LogTemp, Log, TEXT("Chunk [%d, %d] loaded: %d trajectories"), 
						ChunkStart, ChunkEnd, Result.TimeSeries.Num());
					
					// Process this chunk
					// ProcessTrajectoryChunk(Result.TimeSeries);
					
					// Optionally free memory after processing
					// ...
				}
			})
		);
	}
}

/**
 * Example 8: Performance comparison
 * Shows how to measure query performance
 */
void ExamplePerformanceMeasurement()
{
	FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
	FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/my_scenario/my_dataset");
	
	// Create a large trajectory ID list
	TArray<int64> TrajectoryIds;
	for (int64 i = 0; i < 1000; ++i)
	{
		TrajectoryIds.Add(i);
	}
	
	double StartTime = FPlatformTime::Seconds();
	
	Api->QuerySingleTimeStepAsync(
		DatasetPath,
		TrajectoryIds,
		50,
		FOnTrajectoryQueryComplete::CreateLambda([StartTime](const FTrajectoryQueryResult& Result)
		{
			double EndTime = FPlatformTime::Seconds();
			double ElapsedMs = (EndTime - StartTime) * 1000.0;
			
			if (Result.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("Query completed in %.2f ms"), ElapsedMs);
				UE_LOG(LogTemp, Log, TEXT("Retrieved %d samples"), Result.Samples.Num());
				UE_LOG(LogTemp, Log, TEXT("Average time per trajectory: %.4f ms"), 
					ElapsedMs / Result.Samples.Num());
			}
		})
	);
}
