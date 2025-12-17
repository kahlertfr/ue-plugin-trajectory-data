# Naming Convention Update

## Overview

This document describes the naming convention used throughout the Trajectory Data plugin for Unreal Engine 5.6.

## Three-Level Hierarchy

The plugin implements a **Scenario → Dataset → Shard** hierarchy for organizing trajectory data:

### Level 1: Scenario
- **Definition**: A top-level directory representing a complete simulation scenario or experiment
- **Purpose**: Groups related datasets that share spatial and temporal relationships
- **Example**: `experiment_2025_01`, `wind_tunnel_test_03`, `bubble_simulation_final`

### Level 2: Dataset
- **Definition**: A subdirectory within a scenario containing trajectory data for a specific type or subset of entities
- **Purpose**: Separates different types of trajectories within the same scenario
- **Relationship**: Multiple datasets in the same scenario are spatially and temporally related
- **Example**: `bubbles`, `particles`, `debris`, `combined`

### Level 3: Shard
- **Definition**: A subdirectory containing the actual trajectory data files
- **Purpose**: Contains manifest and binary data files for a portion of the dataset
- **Contents**: 
  - `shard-manifest.json` (required)
  - `shard-meta.bin` (optional)
  - `shard-trajmeta.bin` (optional)
  - `shard-data.bin` (optional)
- **Example**: `shard_0`, `shard_1`, `shard_time_0_100`

## Directory Structure Example

```
ScenariosDirectory/                         ← Root configuration setting
├── scenario_experiment_2025_01/            ← Scenario
│   ├── dataset_bubbles/                    ← Dataset
│   │   ├── shard_0/                        ← Shard
│   │   │   ├── shard-manifest.json
│   │   │   ├── shard-meta.bin
│   │   │   ├── shard-trajmeta.bin
│   │   │   └── shard-data.bin
│   │   └── shard_1/                        ← Shard
│   │       ├── shard-manifest.json
│   │       └── ...
│   └── dataset_particles/                  ← Dataset (related to bubbles)
│       └── shard_0/                        ← Shard
│           ├── shard-manifest.json
│           └── ...
└── scenario_wind_tunnel_test_03/           ← Scenario
    └── dataset_combined/                   ← Dataset
        └── shard_0/                        ← Shard
            ├── shard-manifest.json
            └── ...
```

## Configuration

The plugin is configured via `Config/DefaultTrajectoryData.ini`:

```ini
[/Script/TrajectoryData.TrajectoryDataSettings]
ScenariosDirectory=C:/Data/TrajectoryScenarios
bAutoScanOnStartup=True
bDebugLogging=False
```

**Important**: `ScenariosDirectory` points to the root directory containing scenario folders, NOT directly to datasets or shards.

## How the Plugin Scans

1. **Read Configuration**: Load `ScenariosDirectory` from settings
2. **Scan Scenarios**: Iterate through all subdirectories in `ScenariosDirectory`
3. **Scan Datasets**: For each scenario, iterate through dataset subdirectories
4. **Scan Shards**: For each dataset, find subdirectories containing `shard-manifest.json`
5. **Parse Metadata**: Read and parse each `shard-manifest.json` file
6. **Aggregate Data**: Group shards into datasets, associate datasets with scenarios
7. **Cache Results**: Store all discovered information for fast access

## Data Structures

### FTrajectoryDatasetInfo
Contains information about a dataset:
- `DatasetName`: Name of the dataset directory
- `DatasetPath`: Full path to the dataset directory
- `ScenarioName`: Name of the parent scenario
- `Shards`: Array of all shards in this dataset
- `TotalTrajectories`: Sum of trajectories across all shards

### FTrajectoryShardMetadata
Contains information about a single shard:
- `ShardName`: Name identifier from the manifest
- `ShardDirectory`: Full path to the shard directory
- `ManifestFilePath`: Full path to `shard-manifest.json`
- Plus all fields from the manifest (trajectory count, bounding box, etc.)

## Blueprint API

All Blueprint functions work with the three-level hierarchy:

### Scanning
- `ScanTrajectoryDatasets()`: Scans all scenarios, datasets, and shards

### Querying
- `GetAvailableDatasets()`: Returns all datasets from all scenarios
- `GetDatasetInfo(DatasetName)`: Gets a specific dataset by name
- `GetNumDatasets()`: Returns total number of datasets across all scenarios

### Configuration
- `GetScenariosDirectory()`: Gets the root scenarios directory
- `SetScenariosDirectory(Path)`: Sets the root scenarios directory

## Usage Notes

### Related Datasets
Multiple datasets within the same scenario are spatially and temporally related. For example:
- A scenario `bubble_simulation_2025` might contain:
  - `dataset_bubbles`: Trajectory data for bubbles
  - `dataset_particles`: Trajectory data for particles
  - Both datasets share the same spatial origin and time reference

### Multiple Shards
A dataset can have multiple shards for various reasons:
- **Time ranges**: Different time windows of the same simulation
- **Spatial regions**: Different spatial areas
- **File size management**: Splitting large datasets into manageable chunks

### Naming Consistency
While the plugin doesn't enforce specific naming patterns for scenario/dataset/shard directories, using descriptive names improves organization:
- Good scenario names: `experiment_2025_01_15`, `baseline_test`, `high_velocity_run`
- Good dataset names: `bubbles`, `particles`, `combined_entities`
- Good shard names: `shard_0`, `shard_1`, `shard_time_0_100`, `shard_region_a`

## Migration from Previous Version

If you were using an earlier version without scenario support:

### Old Structure
```
DatasetsDirectory/
├── dataset_A/
│   ├── shard_0/
│   └── shard_1/
└── dataset_B/
    └── shard_0/
```

### New Structure
```
ScenariosDirectory/
└── default_scenario/          ← Add scenario level
    ├── dataset_A/
    │   ├── shard_0/
    │   └── shard_1/
    └── dataset_B/
        └── shard_0/
```

### Configuration Change
Update your `DefaultTrajectoryData.ini`:
- Old: `DatasetsDirectory=C:/Data/Trajectories`
- New: `ScenariosDirectory=C:/Data/Trajectories` and restructure with scenario folders

## Benefits of This Convention

1. **Organization**: Clear grouping of related simulation data
2. **Scalability**: Easy to add new scenarios without reorganizing existing data
3. **Relationships**: Explicit association of related datasets through shared scenarios
4. **Flexibility**: Scenarios can have one or many datasets as needed
5. **Clarity**: Consistent naming makes code and documentation easier to understand
