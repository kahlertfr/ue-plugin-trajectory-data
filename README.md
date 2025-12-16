# Trajectory Data Plugin for Unreal Engine 5.6

This plugin enables loading and management of trajectory data from simulation outputs in Unreal Engine.

## Overview

The Trajectory Data plugin provides C++ classes and Blueprint-callable functions to:
- Configure the location of trajectory datasets
- Scan and discover available datasets
- Read metadata from trajectory data shards
- Access dataset information from Blueprints for visualization

## Features

- **Configuration Management**: Specify the location of trajectory datasets via a config file
- **Dataset Discovery**: Automatically scan directories to find available trajectory datasets
- **Metadata Reading**: Parse and access metadata from trajectory data shards (number of trajectories, samples, time steps, etc.)
- **Memory Monitoring**: Real-time memory estimation and capacity monitoring for trajectory data loading
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

### Setting the Datasets Directory

The plugin reads its configuration from `Config/DefaultTrajectoryData.ini`. You can specify the datasets directory in one of two ways:

#### Option 1: Edit the Config File

Edit `Config/DefaultTrajectoryData.ini`:

```ini
[/Script/TrajectoryData.TrajectoryDataSettings]
DatasetsDirectory=C:/Data/TrajectoryDatasets
bAutoScanOnStartup=True
bDebugLogging=False
```

#### Option 2: Use Blueprint Functions

You can also set the datasets directory at runtime using the Blueprint function:
- **Set Datasets Directory** - Sets the path to the datasets directory

### Configuration Options

- **DatasetsDirectory**: Path to the root directory containing trajectory datasets
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
- **Dataset Name**: Name derived from the directory
- **Dataset Path**: Full path to the dataset directory
- **Shards**: Array of shard metadata
- **Total Trajectories**: Sum of trajectories across all shards
- **Total Samples**: Sum of samples across all shards

### Shard Metadata Structure

Each shard (FTrajectoryShardMetadata) contains metadata from the shard-manifest.json file:
- **Shard Name**: Name identifier for the shard
- **Format Version**: Format version (currently 1)
- **Trajectory Count**: Number of trajectories in this shard
- **Time Step Interval Size**: Number of time steps per trajectory
- **Time Interval Seconds**: Time duration per interval in seconds
- **Bounding Box Min/Max**: Spatial bounds of the data
- **First/Last Trajectory Id**: Range of trajectory IDs
- **Coordinate Units**: Units for spatial coordinates (e.g., "millimeters")
- **Created At**: Timestamp when the shard was created
- **Converter Version**: Git commit hash of the converter tool
- **Manifest File Path**: Path to the shard-manifest.json file
- **Shard Directory**: Directory containing all shard files

### Calculating Display Points

To determine if a dataset can be visualized:

- **Calculate Max Display Points** - Returns total sample points for a dataset (trajectory_count × time_step_interval_size across all shards)
- **Calculate Shard Display Points** - Returns sample points for a single shard

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

## Dataset Structure

Trajectory datasets should be organized with each shard in its own subdirectory:

```
DatasetsDirectory/
├── dataset_A/
│   ├── shard_0/
│   │   ├── shard-manifest.json
│   │   ├── shard-meta.bin
│   │   ├── shard-trajmeta.bin
│   │   └── shard-data.bin
│   └── shard_1/
│       ├── shard-manifest.json
│       ├── shard-meta.bin
│       ├── shard-trajmeta.bin
│       └── shard-data.bin
└── dataset_B/
    └── shard_0/
        ├── shard-manifest.json
        ├── shard-meta.bin
        ├── shard-trajmeta.bin
        └── shard-data.bin
```

Each shard subdirectory contains:
- `shard-manifest.json` - Human-readable JSON manifest
- `shard-meta.bin` - Binary metadata summary
- `shard-trajmeta.bin` - Per-trajectory metadata
- `shard-data.bin` - Actual trajectory position data

## Manifest File Format

Each shard requires a `shard-manifest.json` file with the following structure:

```json
{
  "shard_name": "shard_0",
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

## Shard Organization

Multiple shards within a dataset can represent:
- Different time ranges of the same simulation
- Different spatial regions
- Different types of particles (when bounding boxes overlap)

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

## Documentation

- [Trajectory Data Shard Specification](specification-trajectory-data-shard.md) - File format specification
- [Quick Start Guide](QUICKSTART.md) - Getting started guide
- [Implementation Details](IMPLEMENTATION.md) - Technical implementation details
- [Memory Monitoring Blueprint Example](examples/MEMORY_MONITORING_BLUEPRINT.md) - Complete example for creating memory monitoring UI

## Future Enhancements

Planned features include:
- Binary data loading (actual trajectory data)
- Data streaming for large datasets
- Time window filtering
- Spatial filtering
- Visualization helpers

## License

See [LICENSE](LICENSE) for details.
