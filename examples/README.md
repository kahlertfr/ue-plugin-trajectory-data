# Example Trajectory Dataset

This directory contains example metadata files for a sample trajectory dataset. These files demonstrate the expected JSON format for trajectory data shards.

## Contents

- `sample_dataset/` - Example dataset directory
  - `sample_dataset_0.json` - Metadata for shard 0 (time steps 0-499)
  - `sample_dataset_1.json` - Metadata for shard 1 (time steps 500-999)

Note: The binary data files (.tds) are not included in this example. These would contain the actual trajectory position data according to the specification in `trajectory-converter/specification-trajectory-data-shard.md`.

## Using This Example

1. Copy the `sample_dataset` directory to your configured datasets directory
2. Add corresponding `.tds` files if you have actual trajectory data
3. Run the plugin's scan function to discover the dataset

## Testing Without Binary Data

The plugin will scan and read metadata even if the binary `.tds` files are not present. This allows you to:
- Test the scanning functionality
- View dataset metadata in your UI
- Design your Blueprint workflows before you have actual data files
