# Example Trajectory Dataset

This directory contains example metadata files for a sample trajectory dataset. These files demonstrate the expected format for trajectory data shards according to the Trajectory Data Shard specification.

## Contents

- `sample_dataset/` - Example dataset directory
  - `shard_0/` - First shard subdirectory
    - `shard-manifest.json` - Manifest for shard 0 (1000 trajectories, IDs 1-1000)
  - `shard_1/` - Second shard subdirectory
    - `shard-manifest.json` - Manifest for shard 1 (1000 trajectories, IDs 1001-2000)

Note: The binary data files (`shard-meta.bin`, `shard-trajmeta.bin`, `shard-data.bin`) are not included in this example. These would contain the actual trajectory data according to the specification in `specification-trajectory-data-shard.md`.

## File Structure

According to the specification, each shard is stored in its own subdirectory and contains:
- `shard-manifest.json` - Human-readable JSON manifest with metadata
- `shard-meta.bin` - Binary summary for fast lookup (not included in example)
- `shard-trajmeta.bin` - Per-trajectory metadata (not included in example)
- `shard-data.bin` - Actual trajectory position data (not included in example)

## Using This Example

1. Copy the `sample_dataset` directory to your configured datasets directory
2. Run the plugin's scan function to discover the dataset
3. The plugin will read the `shard-manifest.json` files from each shard subdirectory

## Testing Without Binary Data

The plugin will scan and read manifest files even if the binary files (`.bin`) are not present. This allows you to:
- Test the scanning functionality
- View dataset metadata in your UI
- Design your Blueprint workflows before you have actual data files

## Manifest Format

Each `shard-manifest.json` follows the format specified in the Trajectory Data Shard specification:
- `shard_name`: Name identifier for the shard
- `format_version`: Format version (currently 1)
- `trajectory_count`: Number of trajectories in this shard
- `time_step_interval_size`: Number of time steps per trajectory
- `bounding_box`: Spatial bounds of the data
- And more fields as documented in the specification
