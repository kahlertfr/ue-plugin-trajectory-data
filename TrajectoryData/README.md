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

Each shard (FTrajectoryShardMetadata) contains:
- **Shard Id**: Unique identifier within the dataset
- **Num Trajectories**: Number of trajectories in this shard
- **Num Samples**: Number of time samples per trajectory
- **Time Step Start**: Starting time step index
- **Time Step End**: Ending time step index
- **Origin**: Spatial origin coordinates (FVector)
- **Data Type**: Type of data (e.g., "particle", "bubble")
- **Version**: Format version
- **Metadata File Path**: Path to the JSON metadata file
- **Data File Path**: Path to the binary data file

### Calculating Display Points

To determine if a dataset can be visualized:

- **Calculate Max Display Points** - Returns total sample points for a dataset (num_trajectories × num_samples across all shards)
- **Calculate Shard Display Points** - Returns sample points for a single shard

## Directory Structure

Trajectory datasets should be organized as follows:

```
DatasetsDirectory/
├── dataset_A/
│   ├── dataset_A_0.tds
│   ├── dataset_A_0.json
│   ├── dataset_A_1.tds
│   └── dataset_A_1.json
├── dataset_B/
│   ├── dataset_B_0.tds
│   └── dataset_B_0.json
└── flotation_simulation/
    ├── flotation_simulation_bubbles_0.tds
    ├── flotation_simulation_bubbles_0.json
    ├── flotation_simulation_particles_0.tds
    └── flotation_simulation_particles_0.json
```

Each dataset is a subdirectory containing:
- Binary data files (`.tds`)
- Metadata JSON files (`.json`)

## Metadata File Format

Each shard requires a JSON metadata file with the following structure:

```json
{
  "shard_id": 0,
  "num_trajectories": 1000,
  "num_samples": 500,
  "time_step_start": 0,
  "time_step_end": 499,
  "origin": [0.0, 0.0, 0.0],
  "dataset_name": "example_dataset",
  "data_type": "particle",
  "version": "1.0"
}
```

See `trajectory-converter/specification-trajectory-data-shard.md` for the complete specification.

## Spatially Correlated Data

Multiple shards in the same dataset directory with the same origin coordinates are considered spatially correlated. This is useful for representing different particle types (e.g., bubbles and particles) that share the same coordinate system.

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
