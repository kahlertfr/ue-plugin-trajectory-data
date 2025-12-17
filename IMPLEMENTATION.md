# Implementation Summary

This document provides a technical overview of the Trajectory Data plugin implementation.

## Architecture

The plugin consists of several key components:

### 1. Configuration System

**Files:**
- `Config/DefaultTrajectoryData.ini` - Configuration file
- `TrajectoryDataSettings.h/cpp` - Settings class

**Purpose:** Manages plugin settings including the scenarios directory path.

**Key Features:**
- Uses Unreal Engine's config system (`UCLASS(config=TrajectoryData)`)
- Provides singleton access pattern
- Settings can be modified at runtime and persisted

### 2. Data Structures

**File:** `TrajectoryDataTypes.h`

**Structures:**
- `FTrajectoryShardMetadata` - Represents a single trajectory data shard
- `FTrajectoryDatasetInfo` - Represents a complete dataset with multiple shards

**Key Features:**
- All structures are `USTRUCT(BlueprintType)` for Blueprint exposure
- Properties marked with `UPROPERTY(BlueprintReadOnly)` for Blueprint access
- Comprehensive metadata including spatial origin, time steps, and counts

### 3. Dataset Manager

**Files:**
- `TrajectoryDataManager.h/cpp`

**Purpose:** Core logic for scanning directories and parsing metadata files.

**Key Features:**
- Singleton pattern for global access
- Scans the three-level hierarchy: scenarios → datasets → shards
- Parses JSON metadata files using Unreal's JSON utilities
- Caches all discovered datasets in memory with their parent scenario names
- Provides query functions for accessing dataset information

**Algorithm:**
1. Read scenarios directory from settings
2. Iterate through all scenario subdirectories
3. For each scenario:
   - Iterate through dataset subdirectories
   - For each dataset:
     - Find all shard subdirectories containing `shard-manifest.json` files
     - Parse each manifest file to extract shard metadata
     - Aggregate shards into a dataset
     - Associate dataset with its parent scenario
4. Sort shards by name within each dataset
5. Return all discovered datasets across all scenarios

### 4. Blueprint Function Library

**Files:**
- `TrajectoryDataBlueprintLibrary.h/cpp`

**Purpose:** Provides Blueprint-callable static functions.

**Functions:**
- `ScanTrajectoryDatasets()` - Triggers dataset scanning across all scenarios
- `GetAvailableDatasets()` - Returns all datasets from all scenarios
- `GetDatasetInfo()` - Gets specific dataset by name
- `GetNumDatasets()` - Returns total dataset count across all scenarios
- `ClearDatasets()` - Clears cached data
- `GetScenariosDirectory()` / `SetScenariosDirectory()` - Config access
- `CalculateMaxDisplayPoints()` - Utility for visualization planning

**Key Features:**
- All functions are static for easy Blueprint access
- Uses the manager singleton internally
- Provides both query and mutation operations

### 5. Module System

**Files:**
- `TrajectoryDataModule.h/cpp`
- `TrajectoryData.Build.cs`
- `TrajectoryData.uplugin`

**Purpose:** Standard Unreal Engine plugin module setup.

**Dependencies:**
- Core
- CoreUObject
- Engine
- Json
- JsonUtilities

## Data Flow

```
User Configuration (INI file - ScenariosDirectory)
    ↓
UTrajectoryDataSettings (reads config)
    ↓
Blueprint calls ScanTrajectoryDatasets()
    ↓
UTrajectoryDataManager::ScanDatasets()
    ↓
Scan scenarios directory for scenario folders
    ↓
For each scenario, scan for dataset folders
    ↓
For each dataset, scan for shard folders
    ↓
Parse shard-manifest.json files in each shard folder
    ↓
Build FTrajectoryDatasetInfo structures (with scenario name)
    ↓
Cache in manager
    ↓
Blueprint calls GetAvailableDatasets()
    ↓
Return cached datasets to user
```

## Design Decisions

### 1. Singleton Pattern
Both `UTrajectoryDataSettings` and `UTrajectoryDataManager` use singletons to ensure global access and single source of truth.

### 2. Lazy Scanning
Dataset scanning is not automatic; it must be triggered explicitly. This gives users control over when the potentially expensive I/O operations occur.

### 3. Caching
All dataset information is cached after scanning. This means:
- Fast subsequent queries
- No repeated file I/O
- Manual rescan required if files change on disk

### 4. Separation of Concerns
- Settings: Configuration management
- Manager: Business logic (scanning, parsing)
- Types: Data structures
- Blueprint Library: API facade for Blueprints

### 5. Error Handling
The implementation uses Unreal's logging system (`UE_LOG`) for diagnostics and returns boolean success indicators where appropriate.

## Extension Points

The plugin is designed to be extended:

### Future Enhancements

1. **Binary Data Loading**
   - Add functions to load actual trajectory data from `.tds` files
   - Return data as arrays or custom data structures

2. **Data Streaming**
   - Implement windowed loading for large datasets
   - Add functions to load specific time ranges

3. **Filtering**
   - Add spatial filtering (bounding box queries)
   - Add temporal filtering (time step ranges)
   - Add trajectory count limiting

4. **Visualization Helpers**
   - Functions to generate particle systems
   - Functions to generate Niagara systems
   - Functions to create line traces for trajectories

5. **Performance Optimizations**
   - Async loading with progress callbacks
   - Multi-threaded scanning
   - Incremental updates

## Testing

To test the plugin:

1. **Setup Test Data**
   - Create a scenario directory (e.g., `test_scenario`)
   - Copy the `examples/sample_dataset` directory inside it
   - Update `Config/DefaultTrajectoryData.ini` with the scenarios root path

2. **Blueprint Testing**
   - Create a test Blueprint actor
   - Call `ScanTrajectoryDatasets` on BeginPlay
   - Print the results from `GetAvailableDatasets`

3. **Expected Results**
   - Should find one dataset named "sample_dataset"
   - Dataset should belong to scenario "test_scenario"
   - Should have 2 shards
   - Total trajectories: 2000

## Directory Naming Convention

The plugin expects a three-level hierarchy:

**Scenario → Dataset → Shard**

- **Scenario**: Top-level directory representing a simulation run
- **Dataset**: Subdirectory within a scenario containing related trajectory data
- **Shard**: Subdirectory containing manifest and data files

File naming within each shard directory:
- `shard-manifest.json` - Human-readable metadata
- `shard-meta.bin` - Binary metadata summary
- `shard-trajmeta.bin` - Per-trajectory metadata
- `shard-data.bin` - Actual trajectory position data

Multiple datasets within the same scenario are spatially and temporally related to each other.

## Platform Compatibility

The implementation uses Unreal's platform abstraction layer:
- `IPlatformFile` for file system operations
- `FPaths` for path manipulation
- `FFileHelper` for file I/O

This ensures the plugin works on all platforms supported by Unreal Engine.

## Memory Management

- Manager uses `AddToRoot()` to prevent garbage collection
- All data structures are UPROPERTIES to participate in UE's GC
- No manual memory management required
- Consider memory impact when scanning large dataset directories

## Performance Considerations

- Directory scanning is O(n) where n = number of files
- JSON parsing is relatively fast but dependent on file size
- Caching prevents repeated I/O operations
- Consider implementing pagination for very large dataset lists

## Limitations

Current implementation:
- Does not load actual binary trajectory data
- No async/threaded operations
- No file watching for dynamic updates
- Limited error recovery
- No data validation beyond JSON parsing

These can be addressed in future updates based on requirements.
