# Trajectory Data Plugin for Unreal Engine 5.6

This plugin enables loading and management of trajectory data from simulation outputs in Unreal Engine.

## Directory Structure Convention

The plugin uses a two-level naming hierarchy:

**Scenario → Dataset**

- **Scenario**: Top-level directory representing a simulation scenario or experiment
- **Dataset**: Contains the actual trajectory data files (manifest, metadata, and binary shard files)
  - Multiple datasets in the same scenario are spatially and temporally related
  - Each dataset can contain multiple shard files for different time intervals

Example:
```
ScenariosDirectory/
└── experiment_2025_01/          ← Scenario
    ├── bubbles/                  ← Dataset (contains files directly)
    │   ├── dataset-manifest.json
    │   ├── dataset-meta.bin
    │   ├── dataset-trajmeta.bin
    │   ├── shard-0.bin          ← Time interval 0
    │   └── shard-1.bin          ← Time interval 1
    └── particles/                ← Dataset (contains files directly)
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        └── shard-0.bin          ← Single time interval
```

## Overview

The Trajectory Data plugin provides C++ classes and Blueprint-callable functions to:
- Configure the location of trajectory datasets
- Scan and discover available datasets
- Read metadata from trajectory data shards
- **Load actual trajectory data** from binary files
- Access dataset information from Blueprints for visualization
- Stream and manage large trajectory datasets efficiently

## Features

- **Configuration Management**: Specify the location of trajectory datasets via a config file
- **Dataset Discovery**: Automatically scan directories to find available trajectory datasets
- **Metadata Reading**: Parse and access metadata from trajectory data shards (number of trajectories, samples, time steps, etc.)
- **Trajectory Data Loading**: Load actual 3D position data with flexible filtering and sampling
  - Multiple loading strategies (first N, distributed, explicit list)
  - Configurable time ranges and sample rates
  - Per-trajectory time range support
  - Synchronous and asynchronous loading
- **Data Streaming**: Load and unload data dynamically to manage memory usage
- **Memory Monitoring**: Real-time memory estimation and capacity monitoring for trajectory data loading
- **Multi-threading**: Background loading with progress callbacks
- **Blueprint Integration**: Full Blueprint support with easy-to-use functions for building UIs and visualizations
- **Spatially Correlated Data**: Support for multiple related shards with the same spatial origin

## Installation

### Option 1: As a Git Submodule (Recommended)

Add this plugin as a git submodule directly to your Unreal Engine project's `Plugins` directory:

```bash
cd YourProject/Plugins
git submodule add https://github.com/kahlertfr/ue-plugin-trajectory-data.git
```

Then regenerate project files and compile your project. You can now edit the plugin directly from within your project and commit changes back to this repository.

### Option 2: Manual Installation

1. Clone or download this repository
2. Copy the entire repository folder to your Unreal Engine project's `Plugins` directory
3. Regenerate project files
4. Compile your project

## Configuration

### Setting the Scenarios Directory

The plugin reads its configuration from `Config/DefaultTrajectoryData.ini`. You can specify the scenarios directory in one of two ways:

#### Option 1: Edit the Config File

Edit `Config/DefaultTrajectoryData.ini`:

```ini
[/Script/TrajectoryData.TrajectoryDataSettings]
ScenariosDirectory=C:/Data/TrajectoryScenarios
bAutoScanOnStartup=True
bDebugLogging=False
```

#### Option 2: Use Blueprint Functions

You can also set the scenarios directory at runtime using the Blueprint function:
- **Set Scenarios Directory** - Sets the path to the scenarios directory

### Configuration Options

- **ScenariosDirectory**: Path to the root directory containing scenario folders
- **bAutoScanOnStartup**: Whether to automatically scan for datasets when the plugin loads
- **bDebugLogging**: Enable detailed logging for debugging

## Usage in Blueprints

### Scanning for Datasets

Before accessing dataset information, you must scan the configured directory:

1. Call **Scan Trajectory Datasets** to discover all available datasets
2. This function returns `true` if scanning succeeded

### Getting Dataset Information

Once datasets are scanned, you can access their information:

- **Get Available Datasets** - Returns an array of all discovered datasets
- **Get Dataset Info** - Gets information about a specific dataset by name
- **Get Number of Datasets** - Returns the count of available datasets

### Dataset Information Structure

Each dataset (FTrajectoryDatasetInfo) contains:
- **Dataset Name**: Name derived from the dataset directory
- **Dataset Path**: Full path to the dataset directory
- **Scenario Name**: Name of the parent scenario this dataset belongs to
- **Metadata**: Dataset metadata from the manifest file
- **Total Trajectories**: Number of trajectories in the dataset

### Dataset Metadata Structure

Each dataset's metadata (FTrajectoryShardMetadata) contains information from the dataset-manifest.json file:
- **Dataset Name**: Name identifier for the dataset
- **Format Version**: Format version (currently 1)
- **Trajectory Count**: Number of trajectories in this dataset
- **Time Step Interval Size**: Number of time steps per trajectory
- **Time Interval Seconds**: Time duration per interval in seconds
- **Bounding Box Min/Max**: Spatial bounds of the data
- **First/Last Trajectory Id**: Range of trajectory IDs
- **Coordinate Units**: Units for spatial coordinates (e.g., "millimeters")
- **Created At**: Timestamp when the dataset was created
- **Converter Version**: Git commit hash of the converter tool
- **Manifest File Path**: Path to the dataset-manifest.json file
- **Dataset Directory**: Directory containing all dataset files

### Calculating Display Points

To determine if a dataset can be visualized:

- **Calculate Max Display Points** - Returns total sample points for a dataset (trajectory_count × time_step_interval_size)
- **Calculate Shard Display Points** - Returns sample points for a dataset (same as above)

### Memory Monitoring

The plugin provides real-time memory monitoring to prevent loading data that exceeds system capacity:

#### Memory Information Functions:
- **Get Total Physical Memory** - Returns total system memory in bytes
- **Get Max Trajectory Data Memory** - Returns maximum memory for trajectory data (75% of total)
- **Get Memory Info** - Returns complete memory usage information structure
- **Format Memory Size** - Converts bytes to human-readable format (e.g., "1.5 GB")

#### Memory Calculation Functions:
- **Calculate Shard Memory Requirement** - Calculates memory needed for a shard
- **Calculate Dataset Memory Requirement** - Calculates memory needed for a dataset

Memory is calculated based on the Trajectory Data Shard specification:
- Shard Meta: 76 bytes
- Trajectory Meta: 40 bytes per trajectory
- Data Block Header: 32 bytes
- Data Entries: entry_size_bytes per trajectory

#### Capacity Management Functions:
- **Add Estimated Usage** - Add memory to usage estimate (for immediate feedback)
- **Remove Estimated Usage** - Remove memory from usage estimate
- **Reset Estimated Usage** - Clear all usage estimates
- **Can Load Shard** - Check if a shard fits in remaining capacity
- **Can Load Dataset** - Check if a dataset fits in remaining capacity

#### Example Usage:
```
1. User selects datasets to load
2. For each selection:
   - Calculate memory requirement
   - Add to estimated usage
   - Update UI to show remaining capacity
3. User sees immediate feedback before loading
4. Check Can Load Dataset before actual loading
```

See [examples/MEMORY_MONITORING_BLUEPRINT.md](examples/MEMORY_MONITORING_BLUEPRINT.md) for a complete Blueprint example.

## Data Structure

### Directory Hierarchy

The plugin follows a three-level hierarchy: **Scenario → Dataset → Shard**

- **Scenario**: A directory representing a simulation scenario (e.g., "experiment_2025_01")
- **Dataset**: A subdirectory within a scenario containing related trajectory data (e.g., "bubbles", "particles")
  - Multiple datasets within the same scenario are spatially and temporally related
- **Shard**: A subdirectory containing the actual trajectory data files

```
ScenariosDirectory/
├── scenario_A/
│   ├── dataset_bubbles/
│   │   ├── dataset-manifest.json
│   │   ├── dataset-meta.bin
│   │   ├── dataset-trajmeta.bin
│   │   ├── shard-0.bin
│   │   ├── shard-1.bin
│   │   └── shard-2.bin
│   └── dataset_particles/
│       ├── dataset-manifest.json
│       ├── dataset-meta.bin
│       ├── dataset-trajmeta.bin
│       └── shard-0.bin
└── scenario_B/
    └── dataset_combined/
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        ├── shard-0.bin
        └── shard-1.bin
```

Each dataset directory contains:
- `dataset-manifest.json` - Human-readable JSON manifest
- `dataset-meta.bin` - Binary metadata summary
- `dataset-trajmeta.bin` - Per-trajectory metadata
- `shard-<interval>.bin` - Shard files containing trajectory position data for time intervals (e.g., `shard-0.bin`, `shard-1.bin`, etc.)

## Manifest File Format

Each dataset requires a `dataset-manifest.json` file with the following structure:

```json
{
  "scenario_name": "scenario_A",
  "dataset_name": "bubbles",
  "format_version": 1,
  "endianness": "little",
  "coordinate_units": "millimeters",
  "float_precision": "float32",
  "time_units": "seconds",
  "time_step_interval_size": 50,
  "time_interval_seconds": 0.1,
  "entry_size_bytes": 616,
  "bounding_box": {
    "min": [-1000.0, -1000.0, -1000.0],
    "max": [1000.0, 1000.0, 1000.0]
  },
  "trajectory_count": 1000,
  "first_trajectory_id": 1,
  "last_trajectory_id": 1000,
  "created_at": "2025-12-12T12:00:00Z",
  "converter_version": "b95b7d2"
}
```

See [specification-trajectory-data-shard.md](specification-trajectory-data-shard.md) for the complete specification.

## Naming Convention Details

### Scenario Level
A scenario represents a complete simulation run or experiment. All datasets within a scenario are spatially and temporally related to each other.

### Dataset Level
A dataset within a scenario contains trajectory data for a specific type or subset of entities. For example:
- A scenario might have separate datasets for "bubbles" and "particles"
- These datasets share the same spatial origin and time reference
- A scenario can have a single dataset or multiple related datasets
- Each dataset directory contains the manifest and data files directly (no subdirectories)

### Shard Files
Each dataset can contain **one or more shard files** representing different time intervals:
- Files are named `shard-<interval>.bin` where `<interval>` is the time interval index
- Examples: `shard-0.bin`, `shard-1.bin`, `shard-2.bin`, etc.
- Each shard file covers a consecutive time-interval block as specified in the dataset manifest
- This allows datasets to be split across multiple files for manageable file sizes or different time ranges

## Example Blueprint Usage

### Basic Setup

1. Create a Blueprint Actor or Widget
2. In the Event Graph:
   - Call **Scan Trajectory Datasets** on BeginPlay
   - Call **Get Available Datasets** to retrieve all datasets
   - Loop through the datasets to display them in your UI

### Example Blueprint Flow

```
BeginPlay
  └─> Scan Trajectory Datasets
       └─> Get Available Datasets
            └─> For Each Dataset
                 └─> Print String (Dataset Name)
                 └─> Print String (Total Trajectories)
                 └─> Print String (Total Samples)
```

## C++ API

You can also use the plugin from C++:

```cpp
#include "TrajectoryDataManager.h"
#include "TrajectoryDataBlueprintLibrary.h"

// Scan for datasets
UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
Manager->ScanDatasets();

// Get all datasets
TArray<FTrajectoryDatasetInfo> Datasets = Manager->GetAvailableDatasets();

// Access dataset information
for (const FTrajectoryDatasetInfo& Dataset : Datasets)
{
    UE_LOG(LogTemp, Log, TEXT("Dataset: %s, Trajectories: %d, Samples: %d"),
        *Dataset.DatasetName, Dataset.TotalTrajectories, Dataset.TotalSamples);
}
```

## Trajectory Data Loading

The plugin now supports loading actual trajectory data from binary files:

### Loading Features

- **Full Dataset Loading**: Load trajectories with customizable parameters
  - Configurable time range (start/end time steps)
  - Sample rate control (load every Nth sample)
  - Trajectory selection strategies (first N, distributed, explicit list)
- **Partial Loading**: Load specific trajectories by ID with individual time ranges
- **Memory Validation**: Check memory requirements before loading
- **Async Loading**: Non-blocking loading with progress callbacks
- **Data Streaming**: Adjust time ranges dynamically to manage memory

### Loading Strategies

1. **First N Trajectories**: Load the first N trajectories from the dataset
2. **Distributed**: Load N trajectories evenly distributed across the dataset
3. **Explicit List**: Load specific trajectories by ID with custom time ranges per trajectory

### Blueprint Functions

- **Validate Trajectory Load Params** - Validate before loading
- **Load Trajectories Sync** - Synchronous (blocking) loading
- **Load Trajectories Async** - Asynchronous loading with progress callbacks
- **Get Trajectory Loader** - Access loader for async operations
- **Unload All Trajectories** - Free memory
- **Get Loaded Data Memory Usage** - Monitor memory usage

### C++ API

```cpp
#include "TrajectoryDataLoader.h"

// Create load parameters
FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/Scenarios/Test/Dataset1");
Params.StartTimeStep = 0;
Params.EndTimeStep = 500;
Params.SampleRate = 1;
Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
Params.NumTrajectories = 100;

// Validate and load
UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
if (Validation.bCanLoad)
{
    FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
    if (Result.bSuccess)
    {
        // Access loaded trajectories
        for (const FLoadedTrajectory& Traj : Result.Trajectories)
        {
            // Process trajectory samples
        }
    }
}
```

See [Loading API Reference](LOADING_API.md) and [Loading Blueprint Examples](examples/LOADING_BLUEPRINTS.md) for detailed documentation.

## Documentation

- [Trajectory Data Shard Specification](specification-trajectory-data-shard.md) - File format specification
- [Quick Start Guide](QUICKSTART.md) - Getting started guide
- [Implementation Details](IMPLEMENTATION.md) - Technical implementation details
- [Loading API Reference](LOADING_API.md) - Complete API documentation for loading functionality
- [Loading Blueprint Examples](examples/LOADING_BLUEPRINTS.md) - Blueprint usage examples for loading
- [Memory Monitoring Blueprint Example](examples/MEMORY_MONITORING_BLUEPRINT.md) - Complete example for creating memory monitoring UI

## Future Enhancements

Planned features include:
- Memory-mapped file I/O for ultra-fast access
- Spatial filtering (bounding box queries)
- Additional trajectory selection strategies
- Niagara system integration helpers
- Texture-based data export for GPU rendering

## License

See [LICENSE](LICENSE) for details.
