# Trajectory Data Loading and Memory Management

Complete guide for loading trajectory data and managing memory efficiently in Unreal Engine.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Loading API](#loading-api)
3. [Memory Management](#memory-management)
4. [Best Practices](#best-practices)
5. [Performance Tips](#performance-tips)

---

## Quick Start

### Basic Async Loading (Blueprint)

```
Event BeginPlay
  ↓
Get Trajectory Loader
  ↓
Bind Delegates (OnLoadProgress, OnLoadComplete)
  ↓
Create Load Params
  → Dataset Path: "C:/Data/MyDataset"
  → Selection Strategy: FirstN
  → Num Trajectories: 100
  → Sample Rate: 1
  ↓
Load Trajectories Async
```

### Basic Sync Loading (C++)

```cpp
UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();

FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/Scenarios/Test/Dataset1");
Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
Params.NumTrajectories = 100;

FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
if (Result.bSuccess)
{
    // Process Result.Trajectories
}
```

---

## Loading API

### Key Data Structures

#### FTrajectoryLoadParams

Configure what and how to load:

```cpp
FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/MyDataset");
Params.StartTimeStep = -1;  // -1 = use dataset start
Params.EndTimeStep = -1;    // -1 = use dataset end
Params.SampleRate = 1;      // 1 = every sample, 2 = every 2nd sample
Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
Params.NumTrajectories = 100;
```

**Selection Strategies:**
- `FirstN`: Load first N trajectories
- `Distributed`: Load every Ith trajectory to distribute N across dataset
- `ExplicitList`: Load specific trajectories by ID

#### FTrajectoryLoadResult

Result of loading operation:

```cpp
FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
if (Result.bSuccess)
{
    int32 Count = Result.Trajectories.Num();
    int64 Memory = Result.MemoryUsedBytes;
    // Process trajectories
}
else
{
    UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
}
```

### Loading Methods

#### Validate Before Loading

**Always validate** before loading to check memory requirements:

```cpp
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (Validation.bCanLoad)
{
    UE_LOG(LogTemp, Log, TEXT("Will load %d trajectories, using %lld bytes"),
        Validation.NumTrajectoriesToLoad, Validation.EstimatedMemoryBytes);
    
    // Proceed with load
    FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
}
else
{
    UE_LOG(LogTemp, Warning, TEXT("Cannot load: %s"), *Validation.Message);
}
```

#### Async Loading (Recommended)

Use async loading to avoid blocking the render thread:

```cpp
void UMyClass::LoadDataAsync()
{
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    // Bind delegates
    Loader->OnLoadProgress.AddDynamic(this, &UMyClass::OnLoadProgress);
    Loader->OnLoadComplete.AddDynamic(this, &UMyClass::OnLoadComplete);
    
    // Start load
    FTrajectoryLoadParams Params;
    Params.DatasetPath = TEXT("C:/Data/MyDataset");
    Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Params.NumTrajectories = 500;
    
    Loader->LoadTrajectoriesAsync(Params);
}

void UMyClass::OnLoadProgress(int32 Loaded, int32 Total, float Percent)
{
    UE_LOG(LogTemp, Log, TEXT("Loading: %d/%d (%.1f%%)"), Loaded, Total, Percent);
}

void UMyClass::OnLoadComplete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    if (bSuccess)
    {
        // Data ready to use
        ProcessTrajectories(Result.Trajectories);
    }
}
```

#### Sync Loading

Use sync loading only for small datasets or when you need immediate results:

```cpp
FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
// Blocks until loading completes
```

⚠️ **Warning**: Sync loading blocks the game thread. Use async for datasets > 50MB.

### Explicit Trajectory Selection

Load specific trajectories with custom time ranges:

```cpp
FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/MyDataset");
Params.SelectionStrategy = ETrajectorySelectionStrategy::ExplicitList;

// Add specific trajectories
FTrajectoryLoadSelection Sel1;
Sel1.TrajectoryId = 42;
Sel1.StartTimeStep = 0;
Sel1.EndTimeStep = 100;
Params.TrajectorySelections.Add(Sel1);

FTrajectoryLoadSelection Sel2;
Sel2.TrajectoryId = 123;
Sel2.StartTimeStep = -1;  // Use dataset start
Sel2.EndTimeStep = -1;    // Use dataset end
Params.TrajectorySelections.Add(Sel2);

FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
```

---

## Memory Management

### Memory Estimation

**Calculate memory before loading:**

```cpp
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (Validation.bCanLoad)
{
    int64 EstimatedBytes = Validation.EstimatedMemoryBytes;
    float EstimatedMB = EstimatedBytes / (1024.0f * 1024.0f);
    
    UE_LOG(LogTemp, Log, TEXT("Estimated memory: %.2f MB"), EstimatedMB);
}
```

**Memory per sample:** ~20 bytes (FVector + int32 + bool + padding)

**Example calculation:**
- 1000 trajectories × 500 samples = ~10 MB
- 10,000 trajectories × 2,000 samples = ~400 MB

### Real-Time Memory Monitoring (Blueprint)

Create a widget to monitor memory usage:

```
Event Construct
  ↓
Get Memory Info
  ↓
Break FTrajectoryDataMemoryInfo
  → TotalPhysicalMemory
  → MaxTrajectoryDataMemory (75% of total)
  → CurrentEstimatedUsage
  → RemainingCapacity
  → UsagePercentage
  ↓
Update UI (text blocks, progress bars)
```

**Blueprint Helper Functions:**

- `Get Memory Info` - Returns current memory state
- `Calculate Dataset Memory Requirement` - Estimate memory for a dataset
- `Add Estimated Usage` - Add dataset to estimate
- `Remove Estimated Usage` - Remove dataset from estimate
- `Reset Estimated Usage` - Clear all estimates
- `Can Load Dataset?` - Check if dataset fits in memory

### Interactive Memory Feedback

Show users memory impact before loading:

```
Event: On Dataset Selected
  ↓
Get Dataset Info (by name)
  ↓
Calculate Dataset Memory Requirement
  ↓
Add Estimated Usage
  ↓
Update Memory Display
  ↓
Can Load Dataset?
  ├─> TRUE: Show "Can Load" (green)
  └─> FALSE: Show "Insufficient Memory" (red)
```

### Memory Budget

**System assumes 75% of total physical memory** can be used for trajectory data.

**Example with 16 GB RAM:**
- Total Physical Memory: 16 GB
- Max Trajectory Data Memory: 12 GB (75%)
- Safety margin: 4 GB for system

### Unloading Data

Free memory when data is no longer needed:

```cpp
// Unload all trajectories
UTrajectoryDataLoader::Get()->UnloadAll();

// Or via Blueprint Library
UTrajectoryDataBlueprintLibrary::UnloadAllTrajectories();
```

### Releasing CPU Memory After Niagara Binding

**NEW FEATURE:** After binding trajectory data to Niagara, you can release CPU-side position data to save memory:

```cpp
// After binding to Niagara
ADatasetVisualizationActor* Actor = /* your actor */;
Actor->LoadAndBindDataset(0);

// Get buffer provider and release CPU data
UTrajectoryBufferProvider* Provider = UTrajectoryBufferProvider::Get();
Provider->ReleaseCPUPositionData();

// GPU still has the data, but CPU memory is freed
```

**When to use:**
- After binding data to Niagara
- When you don't need CPU access to positions anymore
- To reduce memory footprint for large datasets

**Memory savings:**
- CPU position data released
- GPU data remains accessible
- Can save hundreds of MB for large datasets

---

## Best Practices

### 1. Always Use Async Loading

❌ **Bad:**
```cpp
// Blocks render thread!
FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(LargeParams);
```

✅ **Good:**
```cpp
// Non-blocking, reports progress
Loader->LoadTrajectoriesAsync(LargeParams);
```

**Rule of thumb:** Use async loading for datasets > 50MB.

### 2. Validate Before Loading

❌ **Bad:**
```cpp
// No validation - might fail or run out of memory
FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
```

✅ **Good:**
```cpp
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (Validation.bCanLoad)
{
    FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
}
```

### 3. Use Sample Rate for Large Datasets

❌ **Bad:**
```cpp
// Loading every sample - high memory usage
Params.SampleRate = 1;  // 10 million samples
```

✅ **Good:**
```cpp
// Load every 2nd sample - 50% memory reduction
Params.SampleRate = 2;  // 5 million samples
// Load every 4th sample - 75% memory reduction
Params.SampleRate = 4;  // 2.5 million samples
```

### 4. Filter by Time Range

❌ **Bad:**
```cpp
// Load entire timeline
Params.StartTimeStep = -1;
Params.EndTimeStep = -1;  // 10,000 time steps
```

✅ **Good:**
```cpp
// Load only region of interest
Params.StartTimeStep = 0;
Params.EndTimeStep = 500;  // 500 time steps
```

### 5. Unload When Done

❌ **Bad:**
```cpp
// Data stays loaded forever
LoadTrajectoriesAsync(Params);
// Never unload
```

✅ **Good:**
```cpp
// Load, use, unload
LoadTrajectoriesAsync(Params);
// ... use data ...
UnloadAll();  // Free memory
```

### 6. Release CPU Data After GPU Binding

✅ **Best:**
```cpp
// Bind to Niagara
Actor->LoadAndBindDataset(0);

// Release CPU memory (GPU keeps data)
Buffer.ReleaseCPUPositionData();
```

**Benefits:**
- Saves CPU memory
- GPU data still accessible
- Optimal for large visualizations

---

## Performance Tips

### Loading Performance

**File I/O:**
- Use SSD for best performance
- Loading from HDD is 5-10x slower

**Optimization:**
- Higher sample rates reduce load time
- Smaller time ranges load faster
- Distributed strategy loads faster than FirstN (less I/O)

**Benchmark (SSD, 10,000 trajectories):**
- Full dataset (2000 samples): ~5 seconds
- Half dataset (1000 samples): ~2.5 seconds
- Sample rate 2: ~2.5 seconds
- Sample rate 4: ~1.25 seconds

### Memory Optimization

**Reduce memory usage:**
1. Increase sample rate (skip samples)
2. Load fewer trajectories
3. Use smaller time ranges
4. Use ExplicitList to load only needed trajectories
5. Release CPU data after GPU binding

**Example optimization:**
```cpp
// Original: 1000 traj × 2000 samples = ~40 MB
Params.NumTrajectories = 1000;
Params.SampleRate = 1;

// Optimized: 1000 traj × 500 samples = ~10 MB
Params.NumTrajectories = 1000;
Params.SampleRate = 4;  // Every 4th sample
```

### Multi-Threading

**Async loading uses background threads:**
- File I/O on worker threads
- Progress callbacks on game thread
- No render thread blocking
- Multiple loaders can run concurrently

**File I/O is the bottleneck**, not CPU processing.

### Error Handling

**Common errors:**
1. `"Dataset directory does not exist"` - Check path
2. `"dataset-meta.bin not found"` - Incomplete dataset
3. `"Invalid magic number"` - Corrupted file
4. `"Insufficient memory"` - Reduce load parameters
5. `"Invalid time range"` - Check StartTimeStep < EndTimeStep

**Robust error handling:**
```cpp
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (!Validation.bCanLoad)
{
    UE_LOG(LogTemp, Error, TEXT("Cannot load: %s"), *Validation.Message);
    return;
}

FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
if (!Result.bSuccess)
{
    UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
    return;
}

// Success - use data
```

---

## Memory Usage Examples

### Small Dataset (Demo/Testing)
- 100 trajectories × 100 samples
- Memory: ~200 KB
- Load time: < 100 ms
- Use: Sync loading OK

### Medium Dataset (Typical)
- 1,000 trajectories × 500 samples
- Memory: ~10 MB
- Load time: ~500 ms
- Use: Async recommended

### Large Dataset (Production)
- 10,000 trajectories × 2,000 samples
- Memory: ~400 MB
- Load time: ~5 seconds
- Use: Async required, use sample rate

### Very Large Dataset (Requires Optimization)
- 50,000 trajectories × 5,000 samples
- Memory: ~5 GB
- Load time: ~30 seconds
- Use: Async, sample rate 4-8, filter by region

---

## Complete Example: Memory-Aware Loading

```cpp
void UMyGameMode::LoadTrajectoriesWithMemoryCheck()
{
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    // Configure load parameters
    FTrajectoryLoadParams Params;
    Params.DatasetPath = TEXT("C:/Data/LargeDataset");
    Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Params.NumTrajectories = 5000;
    Params.SampleRate = 2;  // Reduce memory by 50%
    
    // Validate memory requirements
    FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
    if (!Validation.bCanLoad)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot load: %s"), *Validation.Message);
        
        // Try with reduced parameters
        Params.NumTrajectories = 2500;
        Params.SampleRate = 4;
        Validation = Loader->ValidateLoadParams(Params);
        
        if (!Validation.bCanLoad)
        {
            UE_LOG(LogTemp, Error, TEXT("Still cannot load - system has insufficient memory"));
            return;
        }
    }
    
    // Show memory estimate
    float MemoryMB = Validation.EstimatedMemoryBytes / (1024.0f * 1024.0f);
    UE_LOG(LogTemp, Log, TEXT("Loading %d trajectories, estimated memory: %.2f MB"),
        Validation.NumTrajectoriesToLoad, MemoryMB);
    
    // Bind delegates
    Loader->OnLoadProgress.AddDynamic(this, &UMyGameMode::OnLoadProgress);
    Loader->OnLoadComplete.AddDynamic(this, &UMyGameMode::OnLoadComplete);
    
    // Start async load
    Loader->LoadTrajectoriesAsync(Params);
}

void UMyGameMode::OnLoadProgress(int32 Loaded, int32 Total, float Percent)
{
    UE_LOG(LogTemp, Log, TEXT("Progress: %.1f%% (%d/%d)"), Percent, Loaded, Total);
}

void UMyGameMode::OnLoadComplete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully loaded %d trajectories"), 
            Result.Trajectories.Num());
        
        // Use data for visualization
        BindToNiagara(Result.Trajectories);
        
        // Optional: Release CPU memory after GPU binding
        ReleaseUnneededCPUMemory();
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
    }
}
```

---

## Additional Resources

- **specification-trajectory-data-shard.md**: Binary format specification
- **VISUALIZATION.md**: Guide for visualizing loaded data in Niagara
- **QUICKSTART.md**: Quick setup guide
- **MULTI_DATASET_SUPPORT.md**: Managing multiple datasets

---

**Key Takeaways:**
1. ✅ Always use async loading for large datasets
2. ✅ Validate memory requirements before loading
3. ✅ Use sample rate and time range to reduce memory
4. ✅ Monitor memory usage with real-time feedback
5. ✅ Release CPU data after GPU binding to save memory
6. ✅ Unload data when no longer needed
