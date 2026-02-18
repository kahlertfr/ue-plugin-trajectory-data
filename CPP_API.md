# C++ API for Trajectory Data Loading

This document describes the C++ API for loading trajectory data from other plugins. This API is specifically designed for plugin-to-plugin communication and does not require Blueprint integration.

## Overview

The `FTrajectoryDataCppApi` class provides a simple, thread-safe interface for querying trajectory data asynchronously. All queries execute on background threads to avoid blocking the game thread and causing lag.

## Key Features

- **Async Execution**: All queries run on background threads
- **Two Query Modes**: Single time step or time range queries
- **Simple Callbacks**: Lambda or UObject-based callbacks
- **Thread-Safe**: Safe to call from any thread
- **No Blueprint Dependency**: Pure C++ API
- **Memory Efficient**: Loads only requested data

## API Classes and Structures

### FTrajectoryDataCppApi

The main API class. Get the singleton instance with `FTrajectoryDataCppApi::Get()`.

**Methods:**

- `QuerySingleTimeStepAsync()` - Query one sample per trajectory at a specific time step
- `QueryTimeRangeAsync()` - Query multiple samples per trajectory over a time range

### Data Structures

#### FTrajectorySample

Represents a single position sample at a specific time step.

```cpp
struct FTrajectorySample
{
    int64 TrajectoryId;    // Trajectory ID
    int32 TimeStep;        // Time step of this sample
    FVector Position;      // 3D position
    bool bIsValid;         // Whether sample is valid (not NaN)
};
```

#### FTrajectoryTimeSeries

Represents multiple samples for a trajectory over a time range.

```cpp
struct FTrajectoryTimeSeries
{
    int64 TrajectoryId;           // Trajectory ID
    int32 StartTimeStep;          // Start time step (inclusive)
    int32 EndTimeStep;            // End time step (inclusive)
    TArray<FVector> Samples;      // Position samples indexed by (TimeStep - StartTimeStep)
    FVector Extent;               // Object half-extent in meters
};
```

#### FTrajectoryQueryResult

Result for single time step queries.

```cpp
struct FTrajectoryQueryResult
{
    bool bSuccess;                      // Whether query succeeded
    FString ErrorMessage;               // Error message if failed
    TArray<FTrajectorySample> Samples;  // Loaded samples
};
```

#### FTrajectoryTimeRangeResult

Result for time range queries.

```cpp
struct FTrajectoryTimeRangeResult
{
    bool bSuccess;                           // Whether query succeeded
    FString ErrorMessage;                    // Error message if failed
    TArray<FTrajectoryTimeSeries> TimeSeries; // Loaded time series
};
```

## Usage Examples

### 1. Single Time Step Query

Query the position of multiple trajectories at a specific time step:

```cpp
#include "TrajectoryDataCppApi.h"

void QueryPositionsAtTimeStep()
{
    // Get API instance
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    // Setup query parameters
    FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/scenario1/dataset1");
    TArray<int64> TrajectoryIds = {100, 200, 300};
    int32 TimeStep = 50;
    
    // Execute async query
    bool bStarted = Api->QuerySingleTimeStepAsync(
        DatasetPath,
        TrajectoryIds,
        TimeStep,
        FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
        {
            if (Result.bSuccess)
            {
                for (const FTrajectorySample& Sample : Result.Samples)
                {
                    if (Sample.bIsValid)
                    {
                        UE_LOG(LogTemp, Log, TEXT("Trajectory %lld at time %d: (%f, %f, %f)"),
                            Sample.TrajectoryId, Sample.TimeStep,
                            Sample.Position.X, Sample.Position.Y, Sample.Position.Z);
                    }
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Query failed: %s"), *Result.ErrorMessage);
            }
        })
    );
}
```

### 2. Time Range Query

Query trajectory paths over a time range:

```cpp
void QueryTrajectoryPaths()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    FString DatasetPath = TEXT("C:/Data/TrajectoryScenarios/scenario1/dataset1");
    TArray<int64> TrajectoryIds = {100, 200};
    int32 StartTimeStep = 0;
    int32 EndTimeStep = 100;
    
    Api->QueryTimeRangeAsync(
        DatasetPath,
        TrajectoryIds,
        StartTimeStep,
        EndTimeStep,
        FOnTrajectoryTimeRangeComplete::CreateLambda([](const FTrajectoryTimeRangeResult& Result)
        {
            if (Result.bSuccess)
            {
                for (const FTrajectoryTimeSeries& Series : Result.TimeSeries)
                {
                    UE_LOG(LogTemp, Log, TEXT("Trajectory %lld: %d samples"),
                        Series.TrajectoryId, Series.Samples.Num());
                    
                    // Access individual samples
                    for (int32 i = 0; i < Series.Samples.Num(); ++i)
                    {
                        int32 TimeStep = Series.StartTimeStep + i;
                        const FVector& Position = Series.Samples[i];
                        // Use position data...
                    }
                }
            }
        })
    );
}
```

### 3. Integration with UTrajectoryDataManager

Use the existing manager to discover datasets:

```cpp
void IntegratedQuery()
{
    // Scan for available datasets
    UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
    if (Manager && Manager->ScanDatasets())
    {
        // Get dataset information
        FTrajectoryDatasetInfo DatasetInfo;
        if (Manager->GetDatasetInfo(TEXT("my_dataset"), DatasetInfo))
        {
            // Use dataset path for query
            FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
            
            TArray<int64> TrajectoryIds = {100, 200, 300};
            int32 TimeStep = DatasetInfo.Metadata.FirstTimeStep + 10;
            
            Api->QuerySingleTimeStepAsync(
                DatasetInfo.DatasetPath,
                TrajectoryIds,
                TimeStep,
                FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
                {
                    // Handle result...
                })
            );
        }
    }
}
```

### 4. Using with UObject Callbacks

For class member functions as callbacks:

```cpp
class UMyComponent : public UActorComponent
{
    GENERATED_BODY()
    
public:
    void QueryTrajectories()
    {
        FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
        
        Api->QuerySingleTimeStepAsync(
            DatasetPath,
            TrajectoryIds,
            CurrentTimeStep,
            FOnTrajectoryQueryComplete::CreateUObject(this, &UMyComponent::OnQueryComplete)
        );
    }
    
private:
    void OnQueryComplete(const FTrajectoryQueryResult& Result)
    {
        if (Result.bSuccess)
        {
            // Update visualization
            UpdatePositions(Result.Samples);
        }
    }
};
```

## Performance Considerations

### Background Thread Execution

All queries execute on background threads automatically. The callback is invoked on the game thread, making it safe to update game objects.

### Memory Efficiency

**Single Time Step Queries:**
- Load only one sample per trajectory
- Minimal memory footprint
- Fast execution (typically < 10ms for 100 trajectories)

**Time Range Queries:**
- Load multiple samples per trajectory
- Memory usage: `NumTrajectories * NumTimeSteps * 12 bytes` (FVector is 12 bytes)
- For large ranges, consider chunking (see example below)

**Performance Measurement Note:**
- Timing measurements that capture start time before calling the async API include queueing overhead
- For pure query execution time, measure inside the query task's Run() method
- Queueing overhead is typically negligible (< 1ms) but can vary under heavy load

### Chunked Loading for Large Ranges

When querying large time ranges, split into chunks:

```cpp
void QueryLargeRangeInChunks()
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    int32 FullStartTime = 0;
    int32 FullEndTime = 10000;  // Large range
    int32 ChunkSize = 1000;     // Process in chunks of 1000
    
    for (int32 ChunkStart = FullStartTime; ChunkStart < FullEndTime; ChunkStart += ChunkSize)
    {
        int32 ChunkEnd = FMath::Min(ChunkStart + ChunkSize - 1, FullEndTime);
        
        Api->QueryTimeRangeAsync(
            DatasetPath,
            TrajectoryIds,
            ChunkStart,
            ChunkEnd,
            FOnTrajectoryTimeRangeComplete::CreateLambda([](const FTrajectoryTimeRangeResult& Result)
            {
                // Process chunk
                // Memory is automatically freed when Result goes out of scope
            })
        );
    }
}
```

## Comparison with Existing API

### When to Use C++ API vs. UTrajectoryDataLoader

**Use C++ API (`FTrajectoryDataCppApi`) when:**
- You need to query specific trajectories at specific times
- You don't need Blueprint integration
- You want minimal API surface
- You're calling from another C++ plugin
- You need single time step queries

**Use UTrajectoryDataLoader when:**
- You need Blueprint integration
- You want to load entire datasets with multiple trajectories
- You need the full feature set (validation, memory estimation, etc.)
- You're building editor tools or Blueprint-heavy features

### Feature Comparison

| Feature | C++ API | UTrajectoryDataLoader |
|---------|---------|----------------------|
| Single time step queries | ✓ | ✗ |
| Time range queries | ✓ | ✓ |
| Blueprint accessible | ✗ | ✓ |
| Async execution | ✓ | ✓ |
| Memory validation | ✗ | ✓ |
| Load entire datasets | ✗ | ✓ |
| Trajectory selection strategies | ✗ | ✓ |
| Sample rate control | ✗ | ✓ |
| Minimal overhead | ✓ | ✗ |

## Thread Safety

The C++ API is thread-safe and can be called from any thread. The callback will always be invoked on the game thread, making it safe to update game objects.

## Error Handling

Always check `bSuccess` in the result:

```cpp
Api->QuerySingleTimeStepAsync(
    DatasetPath, TrajectoryIds, TimeStep,
    FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
    {
        if (!Result.bSuccess)
        {
            UE_LOG(LogTemp, Error, TEXT("Query failed: %s"), *Result.ErrorMessage);
            return;
        }
        
        // Process successful result
        for (const FTrajectorySample& Sample : Result.Samples)
        {
            if (Sample.bIsValid)
            {
                // Use valid sample
            }
        }
    })
);
```

## Common Error Messages

- `"Dataset directory does not exist"` - Check that the dataset path is correct
- `"dataset-meta.bin not found"` - Dataset directory is missing required files
- `"Time step X is out of range [Y, Z]"` - Requested time step is outside dataset bounds
- `"Shard file not found"` - Required shard file is missing
- `"Query was cancelled"` - Query was cancelled (usually during shutdown)

## Best Practices

1. **Always check bSuccess** - Don't assume queries will succeed
2. **Check bIsValid for samples** - NaN values indicate invalid/missing data
3. **Use chunking for large ranges** - Avoid loading too much data at once
4. **Capture by value in lambdas** - Avoid capturing `this` pointer unless you're sure the object will outlive the query
5. **Cleanup on shutdown** - The API automatically cancels queries on destruction
6. **Reuse API instance** - Get the singleton once and reuse it

## Example: Complete Component

Here's a complete example of a component using the C++ API:

```cpp
// MyTrajectoryComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TrajectoryDataCppApi.h"
#include "MyTrajectoryComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MYPLUGIN_API UMyTrajectoryComponent : public UActorComponent
{
    GENERATED_BODY()
    
public:
    UMyTrajectoryComponent();
    
    UFUNCTION(BlueprintCallable)
    void LoadTrajectoryData(const FString& DatasetPath, const TArray<int64>& TrajectoryIds, int32 StartTime, int32 EndTime);
    
private:
    void OnDataLoaded(const FTrajectoryTimeRangeResult& Result);
    void UpdateVisualization(const TArray<FTrajectoryTimeSeries>& TimeSeries);
};

// MyTrajectoryComponent.cpp
#include "MyTrajectoryComponent.h"

UMyTrajectoryComponent::UMyTrajectoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UMyTrajectoryComponent::LoadTrajectoryData(
    const FString& DatasetPath,
    const TArray<int64>& TrajectoryIds,
    int32 StartTime,
    int32 EndTime)
{
    FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();
    
    Api->QueryTimeRangeAsync(
        DatasetPath,
        TrajectoryIds,
        StartTime,
        EndTime,
        FOnTrajectoryTimeRangeComplete::CreateUObject(this, &UMyTrajectoryComponent::OnDataLoaded)
    );
}

void UMyTrajectoryComponent::OnDataLoaded(const FTrajectoryTimeRangeResult& Result)
{
    if (!Result.bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load trajectory data: %s"), *Result.ErrorMessage);
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectories"), Result.TimeSeries.Num());
    UpdateVisualization(Result.TimeSeries);
}

void UMyTrajectoryComponent::UpdateVisualization(const TArray<FTrajectoryTimeSeries>& TimeSeries)
{
    // Implement your visualization logic here
    for (const FTrajectoryTimeSeries& Series : TimeSeries)
    {
        UE_LOG(LogTemp, Log, TEXT("Trajectory %lld: %d samples from %d to %d"),
            Series.TrajectoryId, Series.Samples.Num(),
            Series.StartTimeStep, Series.EndTimeStep);
    }
}
```

## Integration with Other Plugins

To use this API from another plugin:

1. Add `TrajectoryData` to your plugin's `PublicDependencyModuleNames` in your `.Build.cs` file:

```csharp
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "TrajectoryData"  // Add this
    }
);
```

2. Include the header in your C++ files:

```cpp
#include "TrajectoryDataCppApi.h"
```

3. Use the API as shown in the examples above.

## See Also

- [CPP_API_USAGE_EXAMPLE.cpp](../examples/CPP_API_USAGE_EXAMPLE.cpp) - Comprehensive usage examples
- [CPP_USAGE_EXAMPLE.cpp](../examples/CPP_USAGE_EXAMPLE.cpp) - Examples using UTrajectoryDataLoader
- [LOADING_AND_MEMORY.md](LOADING_AND_MEMORY.md) - Memory management guide
- [README.md](README.md) - Plugin overview
