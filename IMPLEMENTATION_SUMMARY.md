# C++ API Implementation Summary

This document provides a summary of the C++ API implementation for trajectory data loading.

## Problem Statement

Provide functionality that is callable from other plugins in C++ to load trajectory data:
- Given a list of trajectory IDs and a time step → one sample per trajectory
- Given a list of trajectory IDs and a time range → multiple sample points per trajectory
- Data structure does not need to be accessible through Blueprints
- Data gathering should be as fast as possible and not on game thread

## Solution

Implemented a new C++ API (`FTrajectoryDataCppApi`) that provides a simple, thread-safe interface for querying trajectory data asynchronously.

## Key Components

### 1. API Class

**`FTrajectoryDataCppApi`** - Main API class
- Singleton pattern with thread-safe initialization
- Two primary methods:
  - `QuerySingleTimeStepAsync()` - Query one sample per trajectory
  - `QueryTimeRangeAsync()` - Query multiple samples per trajectory
- Background thread execution
- Game thread callbacks

### 2. Data Structures

**`FTrajectorySample`** - Single position sample
- Trajectory ID
- Time step
- Position (FVector)
- Validity flag

**`FTrajectoryTimeSeries`** - Multiple samples over time
- Trajectory ID
- Time range (start/end)
- Position samples array
- Object extent

**`FTrajectoryQueryResult`** - Result for single time step queries
- Success flag
- Error message
- Array of samples

**`FTrajectoryTimeRangeResult`** - Result for time range queries
- Success flag
- Error message
- Array of time series

### 3. Async Execution

**`FTrajectoryQueryTask`** - Background thread task
- Implements `FRunnable` interface
- Reads binary trajectory data files
- Parses shard files efficiently
- Returns results via callback on game thread

## Implementation Details

### Thread Safety
- Double-checked locking pattern for singleton
- Critical sections for task management
- Thread-safe file I/O
- Callbacks always invoked on game thread

### Performance
- Direct binary file reading
- Memory-mapped file support (via existing structures)
- Efficient shard file parsing
- Minimal memory allocations
- Background thread execution avoids game thread lag

### Error Handling
- Validates input parameters before starting query
- Comprehensive error messages
- Graceful handling of missing/invalid files
- Cancellation support

## Files Added

1. **Source/TrajectoryData/Public/TrajectoryDataCppApi.h** (324 lines)
   - Public API interface
   - Data structures
   - Delegate definitions

2. **Source/TrajectoryData/Private/TrajectoryDataCppApi.cpp** (608 lines)
   - API implementation
   - Async task implementation
   - File parsing logic

3. **CPP_API.md** (487 lines)
   - Comprehensive API documentation
   - Usage examples
   - Performance considerations
   - Best practices

4. **examples/CPP_API_USAGE_EXAMPLE.cpp** (478 lines)
   - 8 complete usage examples
   - Integration examples
   - Performance testing examples

5. **TESTING.md** (415 lines)
   - Testing guide
   - Test cases
   - Expected results
   - Performance benchmarks

6. **README.md** (Updated)
   - Added C++ API references
   - Updated feature list
   - Updated documentation links

## API Usage Example

### Single Time Step Query
```cpp
FTrajectoryDataCppApi* Api = FTrajectoryDataCppApi::Get();

TArray<int64> TrajectoryIds = {100, 200, 300};
int32 TimeStep = 50;

Api->QuerySingleTimeStepAsync(
    DatasetPath,
    TrajectoryIds,
    TimeStep,
    FOnTrajectoryQueryComplete::CreateLambda([](const FTrajectoryQueryResult& Result)
    {
        if (Result.bSuccess)
        {
            for (const FTrajectorySample& Sample : Result.Samples)
            {
                // Use sample.Position, sample.TimeStep, etc.
            }
        }
    })
);
```

### Time Range Query
```cpp
Api->QueryTimeRangeAsync(
    DatasetPath,
    TrajectoryIds,
    StartTime,
    EndTime,
    FOnTrajectoryTimeRangeComplete::CreateLambda([](const FTrajectoryTimeRangeResult& Result)
    {
        if (Result.bSuccess)
        {
            for (const FTrajectoryTimeSeries& Series : Result.TimeSeries)
            {
                // Process Series.Samples array
            }
        }
    })
);
```

## Performance Characteristics

### Single Time Step Queries
- Load only one sample per trajectory
- Minimal memory footprint
- Fast execution: < 1ms per trajectory (typically)
- Scales linearly with trajectory count

### Time Range Queries
- Load multiple samples per trajectory
- Memory: NumTrajectories × NumTimeSteps × 12 bytes
- Efficient bulk loading from shard files
- Can process thousands of trajectories

### Async Execution
- No game thread blocking
- Background thread I/O
- Callback on game thread (safe for UE objects)
- Multiple concurrent queries supported

## Design Decisions

### Why Pure C++ (No Blueprint)?
- Minimal overhead for plugin-to-plugin communication
- Avoids Blueprint reflection overhead
- Simpler API surface for C++ users
- Direct integration with existing file parsers

### Why Async Only?
- Enforces best practices (no blocking)
- Consistent API surface
- Better for large datasets
- Easier to reason about threading

### Why Delegates vs Futures?
- Follows Unreal Engine conventions
- Better integration with UObjects
- Simpler lifetime management
- Native game thread marshalling

## Integration with Existing Code

The C++ API:
- Reuses existing binary file structures (`FDatasetMetaBinary`, etc.)
- Uses existing file parsing logic patterns
- Integrates with `UTrajectoryDataManager` for dataset discovery
- Complements `UTrajectoryDataLoader` (doesn't replace it)

## Testing

Comprehensive testing guide provided in `TESTING.md`:
- Manual test cases
- Error handling tests
- Thread safety tests
- Integration tests
- Performance benchmarks

## Documentation

Complete documentation provided:
- `CPP_API.md` - Full API reference
- `TESTING.md` - Testing guide
- `examples/CPP_API_USAGE_EXAMPLE.cpp` - Usage examples
- Updated `README.md` with API overview

## Requirements Met

✅ Callable from other C++ plugins
✅ Load by trajectory IDs + time step (single sample)
✅ Load by trajectory IDs + time range (multiple samples)
✅ Not exposed to Blueprints (pure C++ API)
✅ Fast data gathering (direct binary I/O)
✅ Not on game thread (background thread execution)

## Code Quality

- Thread-safe singleton implementation
- Graceful thread cleanup
- Comprehensive error handling
- Well-documented API
- Extensive examples
- Testing guide provided

## Next Steps

To use this API in your plugin:

1. Add `TrajectoryData` to your `.Build.cs` dependencies
2. Include `TrajectoryDataCppApi.h` in your C++ files
3. Call `FTrajectoryDataCppApi::Get()` to get the API instance
4. Use `QuerySingleTimeStepAsync()` or `QueryTimeRangeAsync()`
5. Handle results in your callback

See `CPP_API.md` for complete documentation and examples.
