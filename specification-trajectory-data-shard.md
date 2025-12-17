# Trajectory Dataset — Specification

version: b95b7d24eae of trajectory-converter - changed externaly

This document defines the on-disk layout, semantics, and recommended reading/writing conventions for the Trajectory Dataset format.

The trajectory data is organized in the following structure: scenarios - datasets - shards. Each scenario can contain multiple datasets that are spatially and temporally related. Each dataset consists of multiple data shards that are described below.

## Overview

### Purpose

Store many trajectories (3D positions over time) in fixed-size, mmap-friendly binary files that allow fast sequential and random access.

### High-level components

- **dataset-manifest.json** (human readable) (JSON): describes dataset-level metadata (version, endianness, ranges)
- **dataset-meta.bin** (binary): compact binary summary used by tools for fast lookup
- **dataset-trajmeta.bin** (binary): one fixed-size record per trajectory with per-trajectory metadata
- **shard-<interval>.bin** (binary): one or more data files, each covering a consecutive time-interval block (optional chunking parameter), where `<interval>` is the first time step covered by that interval

### Common conventions

- **Endianness**: little-endian for all binary files (documented explicitly in manifest)
- **Floating point**: IEEE-754 single-precision (float32) unless specified otherwise
- **Coordinate units**: stored in original units from source data; unit type is documented in the manifest's "coordinate_units" field (e.g., "millimeters", "meters")
- **Time**: store absolute times in seconds as float64 in manifest and trajectory-meta; internal per-entry time step indices are integers (int32).
- **Struct packing**: all binary structs are packed (no implicit padding). pragma pack(1) for C/C++.
- **Versioning**: every file starts with a 4-byte magic + 1-byte format version. The manifest includes a "converter_version" field containing the git commit hash of the converter tool used to generate the shard.

## File Descriptions

### Manifest (JSON) — Example (human readable)

```json
{
  "scenario_name" : "scenario_name",
  "dataset_name": "dataset-name",
  "format_version": 1,
  "endianness": "little",
  "coordinate_units": "millimeters",
  "float_precision": "float32",
  "time_units": "seconds",
  "time_step_interval_size": 50,
  "time_interval_seconds": 0.1,
  "entry_size_bytes": 616,
  "bounding_box": { "min": [-1000, -1000, -1000], "max": [1000, 1000, 1000] },
  "trajectory_count": 123456,
  "first_trajectory_id": 1000,
  "last_trajectory_id": 124455,
  "created_at": "2025-12-10T12:00:00Z",
  "converter_version": "140a9d5"
}
```

### Dataset-Meta (binary)

Purpose: quick programmatic access to global dataset parameters.

Layout (packed, little-endian):

- offset 0: 4 bytes magic: ASCII "TDSH"
- offset 4: 1 byte format_version (uint8) = 1
- offset 5: 1 byte endianness_flag (0 = little, 1 = big) (uint8)
- offset 6: 2 bytes reserved (padding)
- offset 8: float64 time_interval_seconds
- offset 16: int32 time_step_interval_size
- offset 20: int32 entry_size_bytes (bytes per trajectory entry in data files)
- offset 24: float32 bbox_min[3] (12 bytes)
- offset 36: float32 bbox_max[3] (12 bytes)
- offset 48: uint64 trajectory_count
- offset 56: uint64 first_trajectory_id
- offset 64: uint64 last_trajectory_id
- offset 72: uint32 reserved2
- total size: 76 bytes

### Trajectory-Meta (binary) — one fixed-size record per trajectory

Purpose: store per-trajectory immutable metadata for quick filtering.

Layout (packed, little-endian):

- offset 0: uint64 trajectory_id
- offset 8: int32 start_time_step
- offset 12: int32 end_time_step
- offset 16: float32 extent[3] (object half-extent in meters; default 0.1 -> 10 cm)
- offset 28: uint32 data_file_index (which shard file contains this trajectory's entries for first interval)
- offset 32: uint64 entry_offset_index (index of entry within the data file block for direct seek)
- total size: 40 bytes

Notes:

- All entries are present and Trajectory-Meta must be sorted ascending by trajectory_id.

### Trajectory Data file (binary) — layout for a single time-interval block

Purpose: store position samples for a set of trajectories for a fixed set of consecutive time steps (time_step_interval_size).

File header (packed):

- offset 0: 4 bytes magic: "TDDB" (Trajectory Data Block)
- offset 4: uint8 format_version = 1
- offset 5: uint8 endianness_flag
- offset 6: uint16 reserved
- offset 8: int32 global_interval_index (which interval this file represents; zero-based)
- offset 12: int32 time_step_interval_size (must match shard-meta)
- offset 16: int32 trajectory_entry_count (number of trajectory entries stored in this file)
- offset 20: int64 data_section_offset (byte offset where fixed-size entries begin; normally 32)
- offset 28: 4 bytes reserved
- header size: 32 bytes

Entry layout (per-trajectory) — fixed size for fast mmap:

- Each trajectory entry occupies exactly entry_size_bytes as declared in shard-meta (recommended: small fixed size computed as below).
- Proposed per-entry binary layout:
	- offset 0: uint64 trajectory_id
	- offset 8: int32 start_time_step_in_interval (0..time_step_interval_size-1) — -1 if none valid
	- offset 12: int32 valid_sample_count (number of valid samples within interval)
	- offset 16: positions: time_step_interval_size * (3 * float32) = time_step_interval_size * 12 bytes
	- trailing padding to fill entry_size_bytes

- Fixed-size rationale:
  	- Fixed size = 8 (id) + 4 + 4 + (time_step_interval_size * 12)
	- Indexing and fast seeks: because entries are fixed-size and Trajectory-Meta contains entry_offset_index, readers can compute:
  file_offset = data_section_offset + (entry_offset_index * entry_size_bytes)

- Store positions as float32 x,y,z per sample. Invalid samples are represented by IEEE-754 NaN for x,y and z

#### Byte offsets and example calculation

- Example: time_step_interval_size = 50
  - positions = 50 * 12 = 600 bytes
  - trajectory_id + start + count = 8 + 4 + 4 = 16
  - entry_size_bytes = 16 + 600 = 616
  - Document entry_size_bytes in shard-meta to avoid ambiguity.

### Examples: C/C++ struct definitions (packed, little-endian)

```cpp
/* dataset-meta.bin */
#pragma pack(push,1)
struct DatasetMeta {
  char magic[4]; // "TDSH"
  uint8_t format_version;
  uint8_t endianness_flag;
  uint16_t _reserved;
  double time_interval_seconds;
  int32_t time_step_interval_size;
  int32_t entry_size_bytes;
  float bbox_min[3];
  float bbox_max[3];
  uint64_t trajectory_count;
  uint64_t first_trajectory_id;
  uint64_t last_trajectory_id;
  uint32_t reserved2;
};
#pragma pack(pop)

/* dataset-trajmeta.bin */
#pragma pack(push,1)
struct TrajectoryMeta {
  uint64_t trajectory_id;
  int32_t start_time_step;
  int32_t end_time_step;
  float extent[3];                // object half-extent in meters
  uint32_t data_file_index;
  uint64_t entry_offset_index;
};
#pragma pack(pop)

/* shard.bin header */
#pragma pack(push,1)
struct DataBlockHeader {
  char magic[4];                  // "TDDB"
  uint8_t format_version;         // 1
  uint8_t endianness_flag;        // 0 = little, 1 = big
  uint16_t reserved;
  int32_t global_interval_index;
  int32_t time_step_interval_size;
  int32_t trajectory_entry_count;
  int64_t data_section_offset;
  uint32_t reserved2;
};
#pragma pack(pop)

/* example data entry: NaN sentinel approach */
#pragma pack(push,1)
struct TrajEntryNaN {
  uint64_t trajectory_id;
  int32_t start_time_step_in_interval;
  int32_t valid_sample_count;
  // positions: time_step_interval_size * (float x3) follow directly
  float positions[time_step_interval_size][3];
};
#pragma pack(pop)
```

## Change log

- format_version = 2: consistent naming convention
- format_version = 1: initial stable definition for the project.
