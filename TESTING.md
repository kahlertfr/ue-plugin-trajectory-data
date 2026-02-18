# Testing Guide for C++ API

This document describes how to test the C++ API for trajectory data loading.

## Manual Testing

Since this is an Unreal Engine plugin without a standalone test framework, testing should be performed in an Unreal Engine project.

### Setup

1. Add the plugin to your UE project
2. Add `TrajectoryData` to your module's dependencies in `.Build.cs`
3. Ensure you have trajectory data available in the format specified by the plugin

### Test Dataset Structure

For testing, you need a dataset with the following structure:
```
TestDataset/
├── dataset-meta.bin
├── dataset-trajmeta.bin
└── shard-0.bin
```

### Test 1: Single Time Step Query

Create a test component or actor with the following code:

```cpp
#include "TrajectoryDataCppApi.h"

void TestSingleTimeStepQuery()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    FString DatasetPath = TEXT("C:/Path/To/TestDataset");
    TArray<int64> TrajectoryIds = {0, 1, 2};  // Adjust based on your test data
    int32 TimeStep = 10;  // Adjust based on your test data time range
    
    UE_LOG(LogTemp, Log, TEXT("Starting single time step query test..."));
    
    bool bStarted = Api->QuerySingleTimeStepAsync(
        DatasetPath,
        TrajectoryIds,
        TimeStep,
        FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
        {
            if (Result.bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✓ Single time step query succeeded"));
                UE_LOG(LogTemp, Log, TEXT("  Retrieved %d samples"), Result.Samples.Num());
                
                for (const FTrajectorySample& Sample : Result.Samples)
                {
                    UE_LOG(LogTemp, Log, TEXT("  Trajectory %lld: Valid=%d, Position=(%f,%f,%f)"),
                        Sample.TrajectoryId,
                        Sample.bIsValid,
                        Sample.Position.X,
                        Sample.Position.Y,
                        Sample.Position.Z);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("✗ Single time step query failed: %s"), *Result.ErrorMessage);
            }
        })
    );
    
    if (bStarted)
    {
        UE_LOG(LogTemp, Log, TEXT("✓ Query started successfully"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("✗ Failed to start query"));
    }
}
```

**Expected Results:**
- Query should start successfully (bStarted = true)
- Callback should be invoked with bSuccess = true
- Should retrieve samples for each requested trajectory ID
- Valid samples should have reasonable position values (not NaN)

### Test 2: Time Range Query

```cpp
void TestTimeRangeQuery()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    FString DatasetPath = TEXT("C:/Path/To/TestDataset");
    TArray<int64> TrajectoryIds = {0, 1};
    int32 StartTime = 0;
    int32 EndTime = 50;
    
    UE_LOG(LogTemp, Log, TEXT("Starting time range query test..."));
    
    bool bStarted = Api->QueryTimeRangeAsync(
        DatasetPath,
        TrajectoryIds,
        StartTime,
        EndTime,
        FOnTrajectoryTimeRangeComplete::CreateLambda([StartTime, EndTime](const FTrajectoryTimeRangeResult& Result)
        {
            if (Result.bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✓ Time range query succeeded"));
                UE_LOG(LogTemp, Log, TEXT("  Retrieved %d trajectories"), Result.TimeSeries.Num());
                
                for (const FTrajectoryTimeSeries& Series : Result.TimeSeries)
                {
                    UE_LOG(LogTemp, Log, TEXT("  Trajectory %lld:"), Series.TrajectoryId);
                    UE_LOG(LogTemp, Log, TEXT("    Samples: %d"), Series.Samples.Num());
                    UE_LOG(LogTemp, Log, TEXT("    Time range: %d - %d"), Series.StartTimeStep, Series.EndTimeStep);
                    UE_LOG(LogTemp, Log, TEXT("    Extent: (%f,%f,%f)"), 
                        Series.Extent.X, Series.Extent.Y, Series.Extent.Z);
                    
                    // Verify time range matches request
                    if (Series.StartTimeStep != StartTime || Series.EndTimeStep != EndTime)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("    ⚠ Time range mismatch!"));
                    }
                    
                    // Verify sample count
                    int32 ExpectedSamples = EndTime - StartTime + 1;
                    if (Series.Samples.Num() != ExpectedSamples)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("    ⚠ Sample count mismatch! Expected %d, got %d"),
                            ExpectedSamples, Series.Samples.Num());
                    }
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("✗ Time range query failed: %s"), *Result.ErrorMessage);
            }
        })
    );
    
    if (bStarted)
    {
        UE_LOG(LogTemp, Log, TEXT("✓ Query started successfully"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("✗ Failed to start query"));
    }
}
```

**Expected Results:**
- Query should start successfully
- Callback should be invoked with bSuccess = true
- Should retrieve time series for each requested trajectory
- Number of samples should match the time range (EndTime - StartTime + 1)
- StartTimeStep and EndTimeStep should match the request

### Test 3: Error Handling

```cpp
void TestErrorHandling()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    // Test 1: Invalid dataset path
    UE_LOG(LogTemp, Log, TEXT("Test: Invalid dataset path"));
    Api->QuerySingleTimeStepAsync(
        TEXT("/NonExistent/Path"),
        {0, 1, 2},
        10,
        FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
        {
            if (!Result.bSuccess && Result.ErrorMessage.Contains(TEXT("does not exist")))
            {
                UE_LOG(LogTemp, Log, TEXT("✓ Correctly detected invalid path"));
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("✗ Failed to detect invalid path"));
            }
        })
    );
    
    // Test 2: Empty trajectory list
    UE_LOG(LogTemp, Log, TEXT("Test: Empty trajectory list"));
    bool bStarted = Api->QuerySingleTimeStepAsync(
        TEXT("C:/Valid/Path"),
        TArray<int64>(),  // Empty array
        10,
        FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result) {})
    );
    
    if (!bStarted)
    {
        UE_LOG(LogTemp, Log, TEXT("✓ Correctly rejected empty trajectory list"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("✗ Should have rejected empty trajectory list"));
    }
    
    // Test 3: Invalid time range
    UE_LOG(LogTemp, Log, TEXT("Test: Invalid time range"));
    bStarted = Api->QueryTimeRangeAsync(
        TEXT("C:/Valid/Path"),
        {0, 1},
        100,  // Start > End
        50,
        FOnTrajectoryTimeRangeComplete::CreateLambda([](const FTrajectoryTimeRangeResult& Result) {})
    );
    
    if (!bStarted)
    {
        UE_LOG(LogTemp, Log, TEXT("✓ Correctly rejected invalid time range"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("✗ Should have rejected invalid time range"));
    }
}
```

**Expected Results:**
- Invalid dataset path should result in error message
- Empty trajectory list should be rejected before starting query
- Invalid time range (start > end) should be rejected

### Test 4: Thread Safety

```cpp
void TestThreadSafety()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    FString DatasetPath = TEXT("C:/Path/To/TestDataset");
    
    UE_LOG(LogTemp, Log, TEXT("Test: Multiple concurrent queries"));
    
    // Start multiple queries in parallel
    for (int32 i = 0; i < 10; ++i)
    {
        TArray<int64> TrajectoryIds = {i, i+1, i+2};
        int32 TimeStep = i * 10;
        
        Api->QuerySingleTimeStepAsync(
            DatasetPath,
            TrajectoryIds,
            TimeStep,
            FOnTrajectoryQueryComplete::CreateLambda([i](const FTrajectoryQueryResult& Result)
            {
                if (Result.bSuccess)
                {
                    UE_LOG(LogTemp, Log, TEXT("✓ Query %d completed successfully"), i);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("⚠ Query %d failed: %s"), i, *Result.ErrorMessage);
                }
            })
        );
    }
    
    UE_LOG(LogTemp, Log, TEXT("Started 10 concurrent queries"));
}
```

**Expected Results:**
- All queries should complete without crashes
- No race conditions or data corruption
- Each callback should receive its own result

### Test 5: Integration with UTrajectoryDataManager

```cpp
void TestIntegration()
{
    // First use the manager to scan datasets
    UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
    if (!Manager)
    {
        UE_LOG(LogTemp, Error, TEXT("✗ Failed to get UTrajectoryDataManager"));
        return;
    }
    
    if (!Manager->ScanDatasets())
    {
        UE_LOG(LogTemp, Error, TEXT("✗ Failed to scan datasets"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("✓ Scanned datasets"));
    
    const TArray<FTrajectoryDatasetInfo>& Datasets = Manager->GetAvailableDatasetsRef();
    if (Datasets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠ No datasets found"));
        return;
    }
    
    // Use the first dataset for testing
    const FTrajectoryDatasetInfo& Dataset = Datasets[0];
    UE_LOG(LogTemp, Log, TEXT("Testing with dataset: %s"), *Dataset.DatasetName);
    
    // Calculate a valid trajectory ID range
    TArray<int64> TrajectoryIds;
    for (int64 i = Dataset.Metadata.FirstTrajectoryId; 
         i < Dataset.Metadata.FirstTrajectoryId + 5; 
         ++i)
    {
        TrajectoryIds.Add(i);
    }
    
    // Calculate a valid time step (using safe arithmetic to avoid overflow)
    int32 TimeStep = Dataset.Metadata.FirstTimeStep + 
        (Dataset.Metadata.LastTimeStep - Dataset.Metadata.FirstTimeStep) / 2;
    
    // Query using C++ API
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    Api->QuerySingleTimeStepAsync(
        Dataset.DatasetPath,
        TrajectoryIds,
        TimeStep,
        FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
        {
            if (Result.bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✓ Integration test succeeded"));
                UE_LOG(LogTemp, Log, TEXT("  Retrieved %d samples"), Result.Samples.Num());
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("✗ Integration test failed: %s"), *Result.ErrorMessage);
            }
        })
    );
}
```

**Expected Results:**
- Should successfully scan datasets
- Should retrieve valid dataset information
- Should successfully query using the dataset path from the manager

## Performance Testing

### Test 6: Performance Benchmark

```cpp
void TestPerformance()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    FString DatasetPath = TEXT("C:/Path/To/TestDataset");
    
    // Test with increasing number of trajectories
    TArray<int32> TrajectoryCounts = {10, 100, 1000};
    
    for (int32 Count : TrajectoryCounts)
    {
        TArray<int64> TrajectoryIds;
        for (int64 i = 0; i < Count; ++i)
        {
            TrajectoryIds.Add(i);
        }
        
        double StartTime = FPlatformTime::Seconds();
        
        Api->QuerySingleTimeStepAsync(
            DatasetPath,
            TrajectoryIds,
            10,
            FOnTrajectoryQueryComplete::CreateLambda([Count, StartTime](const FTrajectoryQueryResult& Result)
            {
                double EndTime = FPlatformTime::Seconds();
                double ElapsedMs = (EndTime - StartTime) * 1000.0;
                
                if (Result.bSuccess)
                {
                    double MsPerTrajectory = ElapsedMs / Count;
                    UE_LOG(LogTemp, Log, TEXT("Performance (%d trajectories): %.2f ms total, %.4f ms/trajectory"),
                        Count, ElapsedMs, MsPerTrajectory);
                }
            })
        );
    }
}
```

**Expected Performance:**
- Single time step queries: < 1ms per trajectory for 10-100 trajectories
- Should scale linearly with trajectory count
- No memory leaks (check with profiler)

## Checklist

- [ ] Test 1: Single time step query with valid data
- [ ] Test 2: Time range query with valid data
- [ ] Test 3: Error handling (invalid paths, empty lists, invalid ranges)
- [ ] Test 4: Thread safety (multiple concurrent queries)
- [ ] Test 5: Integration with UTrajectoryDataManager
- [ ] Test 6: Performance benchmarking
- [ ] Verify no memory leaks (use Unreal Insights or similar profiler)
- [ ] Test with various dataset sizes
- [ ] Test callback behavior (ensure called on game thread)
- [ ] Test cleanup on shutdown (no crashes)

## Notes

- Always test with actual trajectory data files
- Monitor the output log for any warnings or errors
- Use Unreal Insights or similar tools to check for thread safety issues
- Test in both Development and Shipping builds
- Verify that callbacks are executed on the game thread (use `IsInGameThread()`)
