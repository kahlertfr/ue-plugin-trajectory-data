# Trajectory Data Plugin for Unreal Engine 5.6

This plugin enables loading and management of trajectory data from simulation outputs in Unreal Engine.

## Overview

The Trajectory Data plugin provides C++ classes and Blueprint-callable functions to:
- Configure the location of trajectory datasets
- Scan and discover available datasets
- Read metadata from trajectory data shards
- Access dataset information from Blueprints for visualization

## Configuration

### Setting the Datasets Directory

The plugin reads its configuration from `Config/DefaultTrajectoryData.ini`. You can specify the datasets directory in one of two ways:

#### Option 1: Edit the Config File

Edit `TrajectoryData/Config/DefaultTrajectoryData.ini`:

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

## Directory Structure

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

See `specification-trajectory-data-shard.md` for the complete specification.

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

## Future Enhancements

Planned features include:
- Data streaming for large datasets
- Time window filtering
- Spatial filtering
- Visualization helpers
- Binary data loading

## License

See the LICENSE file in the root directory.
