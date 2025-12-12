# Trajectory Data Shard Specification

## Overview
This document specifies the format for trajectory data shards used by the UE Plugin Trajectory Data system.

## File Structure
Each trajectory dataset is organized in a directory containing one or more data shards. Each shard consists of:
- A binary data file: `<dataset_name>_<shard_id>.tds` (Trajectory Data Shard)
- A metadata JSON file: `<dataset_name>_<shard_id>.json`

## Metadata File Format
The metadata JSON file contains information about the shard:

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

### Metadata Fields
- `shard_id` (integer): Unique identifier for the shard within the dataset
- `num_trajectories` (integer): Number of trajectories in this shard
- `num_samples` (integer): Number of time samples per trajectory
- `time_step_start` (integer): Starting time step index
- `time_step_end` (integer): Ending time step index (inclusive)
- `origin` (array of 3 floats): Spatial origin coordinates [x, y, z]
- `dataset_name` (string): Name of the parent dataset
- `data_type` (string): Type of data (e.g., "particle", "bubble", "general")
- `version` (string): Format version

## Binary Data File Format
The binary data file (.tds) contains the actual trajectory data in the following structure:

### Header (32 bytes)
- Magic number (4 bytes): "TRDS" (0x54524453)
- Version (4 bytes): Format version as uint32
- Num trajectories (4 bytes): uint32
- Num samples (4 bytes): uint32
- Reserved (16 bytes): For future use

### Trajectory Data
Following the header, trajectory data is stored as:
- For each trajectory (num_trajectories):
  - For each sample (num_samples):
    - Position X (4 bytes): float
    - Position Y (4 bytes): float
    - Position Z (4 bytes): float
    - Optional: Velocity X, Y, Z (12 bytes): float
    - Optional: Additional attributes

Total size per sample (minimal): 12 bytes (3 floats for position)

## Dataset Directory Structure
```
/path/to/datasets/
├── dataset_A/
│   ├── dataset_A_0.tds
│   ├── dataset_A_0.json
│   ├── dataset_A_1.tds
│   └── dataset_A_1.json
├── dataset_B/
│   ├── dataset_B_0.tds
│   ├── dataset_B_0.json
│   ├── dataset_B_1.tds
│   └── dataset_B_1.json
└── flotation_simulation/
    ├── flotation_simulation_bubbles_0.tds
    ├── flotation_simulation_bubbles_0.json
    ├── flotation_simulation_particles_0.tds
    └── flotation_simulation_particles_0.json
```

## Spatially Correlated Data
When multiple shards in the same directory have the same origin coordinates, they are considered spatially correlated. This is useful for representing different types of particles (e.g., bubbles and particles in a flotation simulation) that share the same coordinate system.

Alternatively, correlation can be indicated by using the same origin field in the metadata.

## Notes
- All floating-point values are stored in little-endian format
- File paths should use forward slashes or be platform-independent
- Metadata files must be valid JSON
- Shard IDs should be sequential but don't need to start at 0
