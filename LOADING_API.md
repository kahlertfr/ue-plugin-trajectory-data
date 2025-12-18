# Trajectory Data Loading API Reference

This document provides detailed API documentation for the trajectory data loading functionality.

## Table of Contents

1. [Data Structures](#data-structures)
2. [Trajectory Loader Class](#trajectory-loader-class)
3. [Blueprint Library Functions](#blueprint-library-functions)
4. [C++ API](#c-api)
5. [Delegates and Callbacks](#delegates-and-callbacks)

## Data Structures

### FTrajectoryPositionSample

Represents a single 3D position sample at a specific time step.

**Fields:**
- `int32 TimeStep` - Time step index
- `FVector Position` - 3D position (x, y, z)
- `bool bIsValid` - Whether this sample is valid (false if NaN)

**Blueprint Type:** Yes

---

### FLoadedTrajectory

Contains all position samples for a single trajectory.

**Fields:**
- `int64 TrajectoryId` - Unique trajectory identifier
- `int32 StartTimeStep` - Start time step from metadata
- `int32 EndTimeStep` - End time step from metadata
- `FVector Extent` - Object half-extent in meters
- `TArray<FTrajectoryPositionSample> Samples` - Array of position samples

**Blueprint Type:** Yes

---

### FTrajectoryLoadSelection

Specifies which trajectory to load with optional time range.

**Fields:**
- `int64 TrajectoryId` - Trajectory ID to load
- `int32 StartTimeStep` - Start time step (-1 for dataset start)
- `int32 EndTimeStep` - End time step (-1 for dataset end)

**Blueprint Type:** Yes

**Usage:**
```cpp
FTrajectoryLoadSelection Selection;
Selection.TrajectoryId = 42;
Selection.StartTimeStep = 0;
Selection.EndTimeStep = 100;
```

---

### ETrajectorySelectionStrategy

Enum for trajectory selection strategy.

**Values:**
- `FirstN` - Load first N trajectories
- `Distributed` - Load every Ith trajectory to distribute N across dataset
- `ExplicitList` - Load trajectories by explicit ID list

**Blueprint Type:** Yes

---

### FTrajectoryLoadParams

Parameters for loading trajectory data.

**Fields:**
- `FString DatasetPath` - Dataset directory path
- `int32 StartTimeStep` - Start time step of interest (-1 for dataset start)
- `int32 EndTimeStep` - End time step of interest (-1 for dataset end)
- `int32 SampleRate` - Sample rate (1 = every sample, 2 = every 2nd, etc.)
- `ETrajectorySelectionStrategy SelectionStrategy` - How to select trajectories
- `int32 NumTrajectories` - Number of trajectories (FirstN/Distributed)
- `TArray<FTrajectoryLoadSelection> TrajectorySelections` - Explicit trajectory list

**Blueprint Type:** Yes

**Example: Load First 100 Trajectories**
```cpp
FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/Scenarios/MyScenario/MyDataset");
Params.StartTimeStep = -1;  // Use dataset start
Params.EndTimeStep = -1;    // Use dataset end
Params.SampleRate = 1;      // Load every sample
Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
Params.NumTrajectories = 100;
```

**Example: Load Specific Trajectories**
```cpp
FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/Scenarios/MyScenario/MyDataset");
Params.StartTimeStep = 0;
Params.EndTimeStep = 500;
Params.SampleRate = 2;  // Every 2nd sample
Params.SelectionStrategy = ETrajectorySelectionStrategy::ExplicitList;

FTrajectoryLoadSelection Sel1;
Sel1.TrajectoryId = 42;
Sel1.StartTimeStep = 0;
Sel1.EndTimeStep = 100;
Params.TrajectorySelections.Add(Sel1);

FTrajectoryLoadSelection Sel2;
Sel2.TrajectoryId = 123;
Sel2.StartTimeStep = -1;
Sel2.EndTimeStep = -1;
Params.TrajectorySelections.Add(Sel2);
```

---

### FTrajectoryLoadResult

Result structure for loaded trajectory data.

**Fields:**
- `bool bSuccess` - Whether loading succeeded
- `FString ErrorMessage` - Error message if loading failed
- `TArray<FLoadedTrajectory> Trajectories` - Loaded trajectories
- `int32 LoadedStartTimeStep` - Actual start time step loaded
- `int32 LoadedEndTimeStep` - Actual end time step loaded
- `int64 MemoryUsedBytes` - Total memory used in bytes

**Blueprint Type:** Yes

**Usage:**
```cpp
FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
if (Result.bSuccess)
{
    UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectories"), Result.Trajectories.Num());
    for (const FLoadedTrajectory& Traj : Result.Trajectories)
    {
        // Process trajectory
    }
}
else
{
    UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
}
```

---

### FTrajectoryLoadValidation

Validation result for load parameters.

**Fields:**
- `bool bCanLoad` - Whether the load configuration is valid
- `FString Message` - Validation message (error or info)
- `int64 EstimatedMemoryBytes` - Estimated memory required
- `int32 NumTrajectoriesToLoad` - Number of trajectories that would be loaded
- `int32 NumSamplesPerTrajectory` - Number of samples per trajectory

**Blueprint Type:** Yes

**Usage:**
```cpp
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (Validation.bCanLoad)
{
    UE_LOG(LogTemp, Log, TEXT("Can load %d trajectories with %d samples each"),
        Validation.NumTrajectoriesToLoad, Validation.NumSamplesPerTrajectory);
    UE_LOG(LogTemp, Log, TEXT("Memory required: %lld bytes"), 
        Validation.EstimatedMemoryBytes);
}
else
{
    UE_LOG(LogTemp, Warning, TEXT("Cannot load: %s"), *Validation.Message);
}
```

---

## Trajectory Loader Class

### UTrajectoryDataLoader

Main class for loading trajectory data from binary files.

**Type:** UObject singleton

**Access:** `UTrajectoryDataLoader::Get()`

### Methods

#### ValidateLoadParams

```cpp
FTrajectoryLoadValidation ValidateLoadParams(const FTrajectoryLoadParams& Params)
```

Validate load parameters before actually loading.

**Parameters:**
- `Params` - Load parameters to validate

**Returns:** Validation result with memory estimates

**Blueprint:** `Validate Trajectory Load Params`

---

#### LoadTrajectoriesSync

```cpp
FTrajectoryLoadResult LoadTrajectoriesSync(const FTrajectoryLoadParams& Params)
```

Load trajectory data synchronously (blocking).

**Parameters:**
- `Params` - Load parameters

**Returns:** Load result with trajectory data

**Blueprint:** `Load Trajectories Sync`

**Note:** This function blocks until loading completes. Use for small datasets or when you need immediate results.

---

#### LoadTrajectoriesAsync

```cpp
bool LoadTrajectoriesAsync(const FTrajectoryLoadParams& Params)
```

Load trajectory data asynchronously (non-blocking).

**Parameters:**
- `Params` - Load parameters

**Returns:** True if loading started successfully

**Blueprint:** `Load Trajectories Async`

**Note:** Results delivered via OnLoadComplete delegate. Progress updates via OnLoadProgress delegate.

---

#### CancelAsyncLoad

```cpp
void CancelAsyncLoad()
```

Cancel ongoing async loading operation.

**Blueprint:** `Cancel Async Load`

---

#### IsLoadingAsync

```cpp
bool IsLoadingAsync() const
```

Check if an async load is currently in progress.

**Returns:** True if loading is in progress

**Blueprint:** `Is Loading Async`

---

#### UnloadAll

```cpp
void UnloadAll()
```

Unload all currently loaded trajectory data to free memory.

**Blueprint:** `Unload All`

---

#### GetLoadedTrajectories

```cpp
const TArray<FLoadedTrajectory>& GetLoadedTrajectories() const
```

Get currently loaded trajectories.

**Returns:** Array of loaded trajectories

**Blueprint:** `Get Loaded Trajectories`

---

#### GetLoadedDataMemoryUsage

```cpp
int64 GetLoadedDataMemoryUsage() const
```

Get current memory usage for loaded data.

**Returns:** Memory usage in bytes

**Blueprint:** `Get Loaded Data Memory Usage`

---

### Delegates

#### OnLoadProgress

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTrajectoryLoadProgress, 
    int32, TrajectoriesLoaded, 
    int32, TotalTrajectories, 
    float, ProgressPercent);
```

Progress callback for async loading.

**Parameters:**
- `TrajectoriesLoaded` - Number of trajectories loaded so far
- `TotalTrajectories` - Total number of trajectories to load
- `ProgressPercent` - Progress as percentage (0-100)

**Usage:**
```cpp
Loader->OnLoadProgress.AddDynamic(this, &UMyClass::OnProgressUpdate);

void UMyClass::OnProgressUpdate(int32 Loaded, int32 Total, float Percent)
{
    UE_LOG(LogTemp, Log, TEXT("Progress: %d/%d (%.1f%%)"), Loaded, Total, Percent);
}
```

---

#### OnLoadComplete

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTrajectoryLoadComplete, 
    bool, bSuccess, 
    const FTrajectoryLoadResult&, Result);
```

Completion callback for async loading.

**Parameters:**
- `bSuccess` - Whether loading succeeded
- `Result` - Load result structure

**Usage:**
```cpp
Loader->OnLoadComplete.AddDynamic(this, &UMyClass::OnLoadFinished);

void UMyClass::OnLoadFinished(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectories"), Result.Trajectories.Num());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
    }
}
```

---

## Blueprint Library Functions

### UTrajectoryDataBlueprintLibrary

Static Blueprint function library for trajectory data operations.

### Loading Functions

#### ValidateTrajectoryLoadParams

```cpp
static FTrajectoryLoadValidation ValidateTrajectoryLoadParams(
    const FTrajectoryLoadParams& Params)
```

Validate trajectory load parameters.

**Blueprint:** `Validate Trajectory Load Params`

---

#### LoadTrajectoriesSync

```cpp
static FTrajectoryLoadResult LoadTrajectoriesSync(const FTrajectoryLoadParams& Params)
```

Load trajectory data synchronously.

**Blueprint:** `Load Trajectories Sync`

---

#### GetTrajectoryLoader

```cpp
static UTrajectoryDataLoader* GetTrajectoryLoader()
```

Get the trajectory data loader singleton.

**Returns:** Trajectory data loader instance

**Blueprint:** `Get Trajectory Loader`

**Usage:** Use this to access async loading and delegates

---

#### UnloadAllTrajectories

```cpp
static void UnloadAllTrajectories()
```

Unload all currently loaded trajectory data.

**Blueprint:** `Unload All Trajectories`

---

#### GetLoadedDataMemoryUsage

```cpp
static int64 GetLoadedDataMemoryUsage()
```

Get current memory usage for loaded data.

**Returns:** Memory usage in bytes

**Blueprint:** `Get Loaded Data Memory Usage`

---

#### GetNumLoadedTrajectories

```cpp
static int32 GetNumLoadedTrajectories()
```

Get number of currently loaded trajectories.

**Returns:** Number of loaded trajectories

**Blueprint:** `Get Num Loaded Trajectories`

---

## C++ API

### Complete Loading Example

```cpp
#include "TrajectoryDataLoader.h"
#include "TrajectoryDataStructures.h"

void UMyClass::LoadTrajectoryData()
{
    // Get loader singleton
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    // Create load parameters
    FTrajectoryLoadParams Params;
    Params.DatasetPath = TEXT("C:/Data/Scenarios/Test/Dataset1");
    Params.StartTimeStep = 0;
    Params.EndTimeStep = 500;
    Params.SampleRate = 1;
    Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Params.NumTrajectories = 100;
    
    // Validate first
    FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
    if (!Validation.bCanLoad)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot load: %s"), *Validation.Message);
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Will load %d trajectories, using %lld bytes"),
        Validation.NumTrajectoriesToLoad, Validation.EstimatedMemoryBytes);
    
    // Load synchronously
    FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
    
    if (Result.bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully loaded %d trajectories"), 
            Result.Trajectories.Num());
        
        // Process trajectories
        for (const FLoadedTrajectory& Traj : Result.Trajectories)
        {
            UE_LOG(LogTemp, Log, TEXT("Trajectory %lld: %d samples"), 
                Traj.TrajectoryId, Traj.Samples.Num());
            
            for (const FTrajectoryPositionSample& Sample : Traj.Samples)
            {
                if (Sample.bIsValid)
                {
                    // Use sample position
                    FVector Pos = Sample.Position;
                    // ...
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
    }
}
```

### Async Loading Example

```cpp
void UMyClass::LoadTrajectoryDataAsync()
{
    // Get loader singleton
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    // Bind delegates
    Loader->OnLoadProgress.AddDynamic(this, &UMyClass::OnLoadProgress);
    Loader->OnLoadComplete.AddDynamic(this, &UMyClass::OnLoadComplete);
    
    // Create load parameters
    FTrajectoryLoadParams Params;
    Params.DatasetPath = TEXT("C:/Data/Scenarios/Test/Dataset1");
    Params.StartTimeStep = -1;
    Params.EndTimeStep = -1;
    Params.SampleRate = 1;
    Params.SelectionStrategy = ETrajectorySelectionStrategy::Distributed;
    Params.NumTrajectories = 500;
    
    // Start async load
    if (Loader->LoadTrajectoriesAsync(Params))
    {
        UE_LOG(LogTemp, Log, TEXT("Async loading started"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start async load"));
    }
}

void UMyClass::OnLoadProgress(int32 Loaded, int32 Total, float Percent)
{
    UE_LOG(LogTemp, Log, TEXT("Loading: %d/%d (%.1f%%)"), Loaded, Total, Percent);
    // Update UI progress bar
}

void UMyClass::OnLoadComplete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectories"), Result.Trajectories.Num());
        // Process trajectories
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
    }
}
```

### Explicit Trajectory List Example

```cpp
void UMyClass::LoadSpecificTrajectories()
{
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    FTrajectoryLoadParams Params;
    Params.DatasetPath = TEXT("C:/Data/Scenarios/Test/Dataset1");
    Params.SampleRate = 1;
    Params.SelectionStrategy = ETrajectorySelectionStrategy::ExplicitList;
    
    // Add specific trajectories with custom time ranges
    FTrajectoryLoadSelection Sel1;
    Sel1.TrajectoryId = 42;
    Sel1.StartTimeStep = 0;
    Sel1.EndTimeStep = 100;
    Params.TrajectorySelections.Add(Sel1);
    
    FTrajectoryLoadSelection Sel2;
    Sel2.TrajectoryId = 123;
    Sel2.StartTimeStep = 50;
    Sel2.EndTimeStep = 200;
    Params.TrajectorySelections.Add(Sel2);
    
    FTrajectoryLoadSelection Sel3;
    Sel3.TrajectoryId = 456;
    Sel3.StartTimeStep = -1;  // Use dataset start
    Sel3.EndTimeStep = -1;    // Use dataset end
    Params.TrajectorySelections.Add(Sel3);
    
    // Load
    FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
    
    if (Result.bSuccess)
    {
        // Process specific trajectories
        for (const FLoadedTrajectory& Traj : Result.Trajectories)
        {
            // Each trajectory will have its requested time range
            UE_LOG(LogTemp, Log, TEXT("Loaded trajectory %lld with %d samples"),
                Traj.TrajectoryId, Traj.Samples.Num());
        }
    }
}
```

## Performance Considerations

### Memory Usage

- Each sample: ~20 bytes (FVector + int32 + bool + padding)
- Each trajectory: ~128 bytes + (num_samples × 20 bytes)
- Example: 1000 trajectories × 500 samples = ~10 MB

### Loading Performance

- **File I/O**: Reading from SSD is recommended
- **Sample Rate**: Higher sample rates reduce memory and load time
- **Trajectory Count**: More trajectories = longer load time
- **Time Range**: Smaller time ranges load faster
- **Async Loading**: Use for datasets > 50MB

### Multi-threading

- Async loading uses background threads
- Progress callbacks are dispatched to game thread
- Multiple loaders can run concurrently
- File I/O is the main bottleneck

## Error Handling

### Common Error Codes

1. **"Dataset directory does not exist"** - Invalid path
2. **"dataset-meta.bin not found"** - Missing metadata file
3. **"Invalid magic number"** - Corrupted binary file
4. **"Insufficient memory"** - Not enough available memory
5. **"Invalid time range"** - Start >= End time step
6. **"No trajectories selected"** - Empty selection

### Validation Best Practices

```cpp
// Always validate before loading
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (!Validation.bCanLoad)
{
    // Handle error
    UE_LOG(LogTemp, Error, TEXT("Validation failed: %s"), *Validation.Message);
    return;
}

// Check memory
if (Validation.EstimatedMemoryBytes > AvailableMemory)
{
    // Reduce load parameters
    Params.NumTrajectories /= 2;
    Params.SampleRate *= 2;
}

// Load
FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
if (!Result.bSuccess)
{
    // Handle load error
    UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
}
```

## Thread Safety

The loader uses internal mutexes for thread safety:
- Multiple calls to async load are serialized
- Sync load acquires lock during operation
- Delegates are dispatched to game thread

**Safe:**
```cpp
Loader->LoadTrajectoriesAsync(Params1);  // OK
Loader->LoadTrajectoriesAsync(Params2);  // Queued/rejected
```

**Unsafe:**
```cpp
// Don't access loaded data during async load
const TArray<FLoadedTrajectory>& Trajs = Loader->GetLoadedTrajectories();
// Data may be incomplete or invalid during load
```

**Safe Pattern:**
```cpp
// Wait for completion delegate
Loader->OnLoadComplete.AddDynamic(this, &UMyClass::OnComplete);
Loader->LoadTrajectoriesAsync(Params);

void UMyClass::OnComplete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    // Now safe to access loaded data
    const TArray<FLoadedTrajectory>& Trajs = Result.Trajectories;
}
```
