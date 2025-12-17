# Example Trajectory Dataset

This directory contains example metadata files for a sample trajectory dataset. These files demonstrate the expected format for trajectory datasets according to the Trajectory Dataset specification.

## Directory Structure

The plugin uses a two-level hierarchy: **Scenario → Dataset**

## Contents

- `sample_dataset/` - Example dataset directory (should be placed inside a scenario folder)
  - `dataset-manifest.json` - Manifest with metadata (1000 trajectories, IDs 1-1000)

Note: The binary data files (`dataset-meta.bin`, `dataset-trajmeta.bin`, `shard.bin`) are not included in this example. These would contain the actual trajectory data according to the specification in `specification-trajectory-data-shard.md`.

## File Structure

According to the specification, each dataset directory contains files directly (no subdirectories):
- `dataset-manifest.json` - Human-readable JSON manifest with metadata
- `dataset-meta.bin` - Binary summary for fast lookup (not included in example)
- `dataset-trajmeta.bin` - Per-trajectory metadata (not included in example)
- `shard.bin` - Actual trajectory position data (not included in example)

## Using This Example

To use this example with the plugin:

1. Create a scenario directory in your configured scenarios root directory (e.g., `C:/Data/TrajectoryScenarios/sample_scenario/`)
2. Copy the `sample_dataset` directory into the scenario folder
3. Your final structure should be: `ScenariosDirectory/sample_scenario/sample_dataset/dataset-manifest.json`
4. Run the plugin's scan function to discover the dataset
5. The plugin will read the `dataset-manifest.json` file from the dataset directory

Example directory structure:
```
C:/Data/TrajectoryScenarios/          ← ScenariosDirectory (root)
└── sample_scenario/                   ← Scenario
    └── sample_dataset/                ← Dataset
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        └── shard.bin
```

## Testing Without Binary Data

The plugin will scan and read manifest files even if the binary files (`.bin`) are not present. This allows you to:
- Test the scanning functionality
- View dataset metadata in your UI
- Design your Blueprint workflows before you have actual data files

## Manifest Format

Each `dataset-manifest.json` follows the format specified in the Trajectory Dataset specification:
- `dataset_name`: Name identifier for the dataset
- `format_version`: Format version (currently 1)
- `trajectory_count`: Number of trajectories in this dataset
- `time_step_interval_size`: Number of time steps per trajectory
- `bounding_box`: Spatial bounds of the data
- And more fields as documented in the specification
