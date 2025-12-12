# ue-plugin-trajectory-data
UE plugin for loading and visualizing trajectory data

## Overview

This repository contains an Unreal Engine 5.6 plugin for loading and managing trajectory data from simulation outputs. The plugin provides C++ classes and Blueprint-callable functions to scan, discover, and access metadata from trajectory datasets.

## Features

- **Configuration Management**: Specify the location of trajectory datasets via a config file
- **Dataset Discovery**: Automatically scan directories to find available trajectory datasets
- **Metadata Reading**: Parse and access metadata from trajectory data shards (number of trajectories, samples, time steps, etc.)
- **Blueprint Integration**: Full Blueprint support with easy-to-use functions for building UIs and visualizations
- **Spatially Correlated Data**: Support for multiple related shards with the same spatial origin

## Installation

1. Clone this repository
2. Copy the `TrajectoryData` directory to your Unreal Engine project's `Plugins` directory
3. Regenerate project files
4. Compile your project

## Quick Start

1. Configure the datasets directory in `TrajectoryData/Config/DefaultTrajectoryData.ini`
2. In your Blueprint, call **Scan Trajectory Datasets** to discover available datasets
3. Use **Get Available Datasets** to retrieve all dataset information
4. Access shard metadata to determine what data to load and visualize

## Documentation

- [Plugin Documentation](TrajectoryData/README.md) - Detailed usage guide and API reference
- [Trajectory Data Shard Specification](specification-trajectory-data-shard.md) - File format specification

## Dataset Structure

Datasets should be organized with each shard in its own subdirectory:

```
DatasetsDirectory/
├── dataset_name/
│   ├── shard_0/
│   │   ├── shard-manifest.json  # Human-readable manifest
│   │   ├── shard-meta.bin       # Binary metadata
│   │   ├── shard-trajmeta.bin   # Per-trajectory metadata
│   │   └── shard-data.bin       # Trajectory position data
│   └── shard_1/
│       ├── shard-manifest.json
│       ├── shard-meta.bin
│       ├── shard-trajmeta.bin
│       └── shard-data.bin
```

See the specification document for complete details on the file format.

## License

See [LICENSE](LICENSE) for details.
