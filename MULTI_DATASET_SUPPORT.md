# Multi-Dataset Support

## Overview

The Trajectory Data Loader now supports loading and managing multiple datasets simultaneously. Each loaded dataset maintains its own loading parameters, metadata, and trajectory data, allowing you to work with multiple related datasets in the same session.

## Key Features

- **Multiple Dataset Storage**: Load trajectories from multiple datasets without overwriting previous loads
- **Per-Dataset Metadata**: Each dataset retains its loading parameters and metadata
- **Memory Tracking**: Individual and cumulative memory usage tracking across all datasets
- **Backward Compatibility**: Existing code continues to work with the unified trajectory view

## Architecture

### FLoadedDataset Structure

Each loaded dataset is stored as an `FLoadedDataset` structure containing:

```cpp
struct FLoadedDataset
{
    FTrajectoryLoadParams LoadParams;           // Original loading parameters (includes time steps)
    FTrajectoryDatasetInfo DatasetInfo;         // Complete dataset information (path, metadata, etc.)
    TArray<FLoadedTrajectory> Trajectories;     // Array of loaded trajectories
    int64 MemoryUsedBytes;                      // Memory used by this dataset
};
```

### Loader Changes

The `UTrajectoryDataLoader` now maintains:
- `TArray<FLoadedDataset> LoadedDatasets` - Array of all loaded datasets
- Per-dataset memory tracking
- Cumulative memory usage across all datasets

### API Changes

Loading methods now require `FTrajectoryDatasetInfo` to be passed explicitly:
- `ValidateLoadParams(DatasetInfo, Params)`
- `LoadTrajectoriesSync(DatasetInfo, Params)`
- `LoadTrajectoriesAsync(DatasetInfo, Params)`

This removes the need for `DatasetPath` in `FTrajectoryLoadParams` and ensures all dataset metadata is available.

## Usage Examples

### Loading Multiple Datasets

```cpp
UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();

// Get dataset info for first dataset
FTrajectoryDatasetInfo Dataset1Info;
if (Manager->GetDatasetInfo(TEXT("bubbles"), Dataset1Info))
{
    FTrajectoryLoadParams Params1;
    Params1.NumTrajectories = 50;
    Params1.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    
    FTrajectoryLoadResult Result1 = Loader->LoadTrajectoriesSync(Dataset1Info, Params1);
}

// Get dataset info for second dataset (appends to first, doesn't replace)
FTrajectoryDatasetInfo Dataset2Info;
if (Manager->GetDatasetInfo(TEXT("particles"), Dataset2Info))
{
    FTrajectoryLoadParams Params2;
    Params2.NumTrajectories = 30;
    Params2.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    
    FTrajectoryLoadResult Result2 = Loader->LoadTrajectoriesSync(Dataset2Info, Params2);
}
```

### Accessing Individual Datasets

```cpp
// Get all loaded datasets
const TArray<FLoadedDataset>& LoadedDatasets = Loader->GetLoadedDatasets();

// Iterate through each dataset
for (const FLoadedDataset& Dataset : LoadedDatasets)
{
    UE_LOG(LogTemp, Log, TEXT("Dataset: %s"), *Dataset.DatasetInfo.DatasetPath);
    UE_LOG(LogTemp, Log, TEXT("  Unique Name: %s"), *Dataset.DatasetInfo.UniqueDSName);
    UE_LOG(LogTemp, Log, TEXT("  Trajectories: %d"), Dataset.Trajectories.Num());
    UE_LOG(LogTemp, Log, TEXT("  Time range: %d - %d"), 
        Dataset.LoadParams.StartTimeStep, Dataset.LoadParams.EndTimeStep);
    UE_LOG(LogTemp, Log, TEXT("  Memory: %lld bytes"), Dataset.MemoryUsedBytes);
    
    // Access dataset metadata
    UE_LOG(LogTemp, Log, TEXT("  Total trajectories in dataset: %lld"), 
        Dataset.DatasetInfo.TotalTrajectories);
    UE_LOG(LogTemp, Log, TEXT("  Sample rate: %d"), Dataset.LoadParams.SampleRate);
}
```

### Processing Datasets Separately

```cpp
// Process each dataset with different logic
for (const FLoadedDataset& Dataset : LoadedDatasets)
{
    if (Dataset.DatasetInfo.DatasetPath.Contains(TEXT("bubbles")))
    {
        // Visualize as bubbles with specific color/size
        for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
        {
            // Blue visualization for bubbles
        }
    }
    else if (Dataset.DatasetInfo.DatasetPath.Contains(TEXT("particles")))
    {
        // Visualize as particles with different style
        for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
        {
            // Red visualization for particles
        }
    }
}
```

### Backward Compatibility

Existing code that uses `GetLoadedTrajectories()` continues to work:

```cpp
// This returns all trajectories from all loaded datasets
TArray<FLoadedTrajectory> AllTrajectories = Loader->GetLoadedTrajectories();

// Process all trajectories together (original behavior)
for (const FLoadedTrajectory& Traj : AllTrajectories)
{
    // Your existing processing code
}
```

### Memory Management

```cpp
// Get total memory usage across all datasets
int64 TotalMemory = Loader->GetLoadedDataMemoryUsage();
UE_LOG(LogTemp, Log, TEXT("Total memory: %s"),
    *UTrajectoryDataBlueprintLibrary::FormatMemorySize(TotalMemory));

// Get individual dataset memory
for (const FLoadedDataset& Dataset : Loader->GetLoadedDatasets())
{
    UE_LOG(LogTemp, Log, TEXT("Dataset %s uses %s"),
        *Dataset.DatasetInfo.DatasetPath,
        *UTrajectoryDataBlueprintLibrary::FormatMemorySize(Dataset.MemoryUsedBytes));
}

// Unload all datasets
Loader->UnloadAll();
```

## Blueprint Support

All new functionality is exposed to Blueprints:

- **Get Loaded Datasets**: Returns array of all loaded datasets
- **Get Loaded Trajectories**: Returns all trajectories from all datasets (backward compatible)
- **Get Loaded Data Memory Usage**: Returns total memory usage across all datasets

## Use Cases

### Multiple Spatially-Related Datasets

Load different types of objects from the same simulation:

```cpp
UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();

// Load vehicles
FTrajectoryDatasetInfo VehicleInfo;
if (Manager->GetDatasetInfo(TEXT("vehicles"), VehicleInfo))
{
    FTrajectoryLoadParams VehicleParams;
    VehicleParams.NumTrajectories = 100;
    VehicleParams.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Loader->LoadTrajectoriesSync(VehicleInfo, VehicleParams);
}

// Load pedestrians
FTrajectoryDatasetInfo PedestrianInfo;
if (Manager->GetDatasetInfo(TEXT("pedestrians"), PedestrianInfo))
{
    FTrajectoryLoadParams PedestrianParams;
    PedestrianParams.NumTrajectories = 50;
    PedestrianParams.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Loader->LoadTrajectoriesSync(PedestrianInfo, PedestrianParams);
}

// Visualize both simultaneously with different styles
```

### Comparative Analysis

Load the same scene with different parameters:

```cpp
FTrajectoryDatasetInfo DatasetInfo;
if (Manager->GetDatasetInfo(TEXT("experiment1"), DatasetInfo))
{
    // Load with low sample rate
    FTrajectoryLoadParams LowResParams;
    LowResParams.SampleRate = 10;
    LowResParams.NumTrajectories = 100;
    LowResParams.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Loader->LoadTrajectoriesSync(DatasetInfo, LowResParams);

    // Load with high sample rate for comparison
    FTrajectoryLoadParams HighResParams;
    HighResParams.SampleRate = 1;
    HighResParams.NumTrajectories = 100;
    HighResParams.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Loader->LoadTrajectoriesSync(DatasetInfo, HighResParams);

    // Compare the two loaded versions
}
```

### Time Window Management

Load different time windows without losing previous data:

```cpp
FTrajectoryDatasetInfo DatasetInfo;
if (Manager->GetDatasetInfo(TEXT("experiment1"), DatasetInfo))
{
    FTrajectoryLoadParams Params;
    Params.NumTrajectories = 50;
    Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    
    // Load early time window
    Params.StartTimeStep = 0;
    Params.EndTimeStep = 1000;
    Loader->LoadTrajectoriesSync(DatasetInfo, Params);

    // Load late time window (appends, doesn't replace)
    Params.StartTimeStep = 5000;
    Params.EndTimeStep = 6000;
    Loader->LoadTrajectoriesSync(DatasetInfo, Params);

    // Both time windows are now available
}
```

## Migration Guide

### API Changes

**Old API:**
```cpp
FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/dataset");
Params.NumTrajectories = 100;
Loader->LoadTrajectoriesSync(Params);
```

**New API:**
```cpp
UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
FTrajectoryDatasetInfo DatasetInfo;
if (Manager->GetDatasetInfo(TEXT("dataset_name"), DatasetInfo))
{
    FTrajectoryLoadParams Params;
    Params.NumTrajectories = 100;
    // No need to set DatasetPath - it comes from DatasetInfo
    Loader->LoadTrajectoriesSync(DatasetInfo, Params);
}
```

### If You Were Using LoadTrajectoriesSync/Async

The loader now requires `FTrajectoryDatasetInfo` and appends instead of replacing:

```cpp
// Old approach:
Loader->UnloadAll();  // Clear previous
Params.DatasetPath = TEXT("...");
Loader->LoadTrajectoriesSync(Params);

// New approach:
Loader->UnloadAll();  // Clear previous (optional)
FTrajectoryDatasetInfo Info;
Manager->GetDatasetInfo(TEXT("dataset_name"), Info);
Loader->LoadTrajectoriesSync(Info, Params);  // Appends to existing datasets
```

### If You Were Iterating Over Loaded Trajectories

No changes needed! `GetLoadedTrajectories()` still returns all trajectories:

```cpp
// This still works exactly as before
for (const FLoadedTrajectory& Traj : Loader->GetLoadedTrajectories())
{
    // Your code
}
```

### If You Want to Use the New Multi-Dataset Features

Switch to using `GetLoadedDatasets()`:

```cpp
// New way - access per-dataset information
for (const FLoadedDataset& Dataset : Loader->GetLoadedDatasets())
{
    // Access dataset-specific information
    ProcessDataset(Dataset);
}
```

## API Reference

### New Methods

#### GetLoadedDatasets()

```cpp
const TArray<FLoadedDataset>& GetLoadedDatasets() const;
```

Returns array of all loaded datasets with their parameters and data.

#### GetLoadedTrajectories()

```cpp
TArray<FLoadedTrajectory> GetLoadedTrajectories() const;
```

Returns all trajectories from all loaded datasets (backward compatible, creates a combined array).

#### GetLoadedDataMemoryUsage()

```cpp
int64 GetLoadedDataMemoryUsage() const;
```

Returns total memory usage across all loaded datasets in bytes.

### Modified Behavior

- **LoadTrajectoriesSync()**: Now appends to the datasets array instead of replacing
- **LoadTrajectoriesAsync()**: Now appends to the datasets array instead of replacing
- **UnloadAll()**: Clears all loaded datasets and resets memory usage

## Performance Considerations

- Appending datasets is efficient (no reallocation of existing data)
- Memory usage is tracked per-dataset for granular monitoring
- `GetLoadedTrajectories()` creates a new combined array, so cache the result if calling multiple times
- Consider calling `UnloadAll()` before loading if you don't need previous datasets

## Best Practices

1. **Clear When Appropriate**: Call `UnloadAll()` when switching contexts to free memory
2. **Monitor Memory**: Use per-dataset memory tracking to identify large datasets
3. **Separate Processing**: Process datasets individually when they need different treatment
4. **Cache Combined Results**: If calling `GetLoadedTrajectories()` multiple times, cache the result
5. **Use Descriptive Paths**: Dataset paths help identify datasets later during iteration
