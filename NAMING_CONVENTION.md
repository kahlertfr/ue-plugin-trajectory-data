# Naming Convention Update

## Overview

This document describes the naming convention used throughout the Trajectory Data plugin for Unreal Engine 5.6.

## Two-Level Hierarchy

The plugin implements a **Scenario → Dataset** hierarchy for organizing trajectory data:

### Level 1: Scenario
- **Definition**: A top-level directory representing a complete simulation scenario or experiment
- **Purpose**: Groups related datasets that share spatial and temporal relationships
- **Example**: `experiment_2025_01`, `wind_tunnel_test_03`, `bubble_simulation_final`

### Level 2: Dataset
- **Definition**: A subdirectory within a scenario containing the actual trajectory data files
- **Purpose**: Separates different types of trajectories within the same scenario
- **Relationship**: Multiple datasets in the same scenario are spatially and temporally related
- **Contents**: 
  - `dataset-manifest.json` (required)
  - `dataset-meta.bin` (optional)
  - `dataset-trajmeta.bin` (optional)
  - `shard.bin` (optional)
- **Example**: `bubbles`, `particles`, `debris`, `combined`

## Directory Structure Example

```
ScenariosDirectory/                         ← Root configuration setting
├── scenario_experiment_2025_01/            ← Scenario
│   ├── dataset_bubbles/                    ← Dataset
│   │   ├── dataset-manifest.json
│   │   ├── dataset-meta.bin
│   │   ├── dataset-trajmeta.bin
│   │   └── shard.bin
│   └── dataset_particles/                  ← Dataset (related to bubbles)
│       ├── dataset-manifest.json
│       ├── dataset-meta.bin
│       ├── dataset-trajmeta.bin
│       └── shard.bin
└── scenario_wind_tunnel_test_03/           ← Scenario
    └── dataset_combined/                   ← Dataset
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        └── shard.bin
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
4. **Find Manifests**: For each dataset, look for `dataset-manifest.json` directly in the dataset directory
5. **Parse Metadata**: Read and parse each `dataset-manifest.json` file
6. **Associate Data**: Associate datasets with their parent scenarios
7. **Cache Results**: Store all discovered information for fast access

## Data Structures

### FTrajectoryDatasetInfo
Contains information about a dataset:
- `DatasetName`: Name of the dataset directory
- `DatasetPath`: Full path to the dataset directory
- `ScenarioName`: Name of the parent scenario
- `Metadata`: Dataset metadata from the manifest file
- `TotalTrajectories`: Number of trajectories in this dataset

### FTrajectoryShardMetadata (Dataset Metadata)
Contains metadata about a dataset (note: despite the legacy name, this now represents dataset metadata):
- `ShardName`: Dataset name identifier from the manifest
- `ShardDirectory`: Full path to the dataset directory
- `ManifestFilePath`: Full path to `dataset-manifest.json`
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

### Naming Consistency
While the plugin doesn't enforce specific naming patterns for scenario/dataset directories, using descriptive names improves organization:
- Good scenario names: `experiment_2025_01_15`, `baseline_test`, `high_velocity_run`
- Good dataset names: `bubbles`, `particles`, `combined_entities`

## Migration from Previous Version

If you were using an earlier version with shard subdirectories:

### Old Structure
```
ScenariosDirectory/
└── scenario_name/
    └── dataset_A/
        ├── shard_0/
        │   └── shard-manifest.json
        └── shard_1/
            └── shard-manifest.json
```

### New Structure
```
ScenariosDirectory/
└── scenario_name/
    └── dataset_A/
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        └── shard.bin
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
