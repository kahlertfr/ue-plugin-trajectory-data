# Trajectory Data Plugin for Unreal Engine 5.6

Load and visualize large-scale trajectory datasets from simulation outputs in Unreal Engine.

## Quick Links

- **[Quick Start Guide](QUICKSTART.md)** - Get started in 5 minutes
- **[Loading & Memory Management](LOADING_AND_MEMORY.md)** - Load data and manage memory
- **[Load Single Shard Files](LOAD_SHARD_FILE.md)** - Direct access to shard file data for external components
- **[Visualization Guide](VISUALIZATION.md)** - Visualize with Niagara systems
- **[Naming Conventions](NAMING_CONVENTION.md)** - Directory structure and organization
- **[Data Format Specification](specification-trajectory-data-shard.md)** - Technical specification

## Features

- **Async Loading** - Non-blocking background loading with progress callbacks
- **Memory Management** - Real-time monitoring and capacity validation
- **Flexible Selection** - Load first N, distributed, or specific trajectories
- **Niagara Integration** - Built-in Position Array NDI support (10x faster than texture approach)
- **Multi-Dataset Support** - Load and visualize multiple related datasets
- **Time-Range Filtering** - Load only needed time ranges to save memory
- **Thread-Safe** - All loading happens on background threads
- **Direct Shard Access** - Load complete shard files for external processing (hash tables, custom indexing)

## Installation

### Option 1: Git Submodule (Recommended)

```bash
cd YourProject/Plugins
git submodule add https://github.com/kahlertfr/ue-plugin-trajectory-data.git
```

### Option 2: Manual Copy

Copy this repository to `YourProject/Plugins/ue-plugin-trajectory-data/`

Then regenerate project files and compile your project.

## Configuration

Create or edit `YourProject/Config/DefaultTrajectoryData.ini`:

```ini
[/Script/TrajectoryData.TrajectoryDataSettings]
ScenariosDirectory=C:/Data/TrajectoryScenarios
bAutoScanOnStartup=True
bDebugLogging=False
```

## Data Structure

The plugin uses a **Scenario → Dataset** hierarchy:

```
ScenariosDirectory/
└── my_scenario/                ← Scenario (simulation run)
    ├── bubbles/                ← Dataset 1
    │   ├── dataset-manifest.json
    │   ├── dataset-meta.bin
    │   ├── dataset-trajmeta.bin
    │   └── shard-0.bin
    └── particles/              ← Dataset 2 (spatially related)
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        ├── shard-0.bin
        └── shard-1.bin
```

**Key points:**
- **Scenario** = simulation run or experiment
- **Dataset** = related trajectory data (e.g., bubbles, particles)
- **Shards** = time-interval data files (shard-0.bin, shard-1.bin, etc.)

See [NAMING_CONVENTION.md](NAMING_CONVENTION.md) for details.

## Quick Start

### 1. Scan for Datasets

**Blueprint:**
```
Event Begin Play
  → Scan Trajectory Datasets
     → Get Number of Datasets
        → Print String
```

**C++:**
```cpp
#include "TrajectoryDataManager.h"

UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
if (Manager && Manager->ScanDatasets())
{
    TArray<FTrajectoryDatasetInfo> Datasets = Manager->GetAvailableDatasets();
}
```

### 2. Load Trajectory Data

**Blueprint:**
```
Get Trajectory Loader
  → Load Trajectories Async
     - Dataset Path: "C:/Data/Scenarios/MyScenario/MyDataset"
     - Selection Strategy: First N
     - Num Trajectories: 100
     → On Load Complete → Visualize
```

**C++:**
```cpp
#include "TrajectoryDataLoader.h"

FTrajectoryLoadParams Params;
Params.DatasetPath = TEXT("C:/Data/Scenarios/Test/Dataset");
Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
Params.NumTrajectories = 100;

UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
Loader->LoadTrajectoriesAsync(Params, OnLoadComplete);
```

### 3. Visualize with Niagara

**Easiest approach - Use DatasetVisualizationActor:**

```
Add DatasetVisualizationActor to level
  → Set Niagara System Template
  → Set Auto Load On Begin Play = true
  → Set Auto Load Dataset Index = 0
  → Play
```

The actor automatically loads and binds data to Niagara using the built-in Position Array NDI.

See [VISUALIZATION.md](VISUALIZATION.md) for detailed visualization guide.

## Key Classes

### Blueprint-Accessible

- **UTrajectoryDataManager** - Scan and discover datasets
- **UTrajectoryDataLoader** - Load trajectory data (async/sync)
- **UTrajectoryDataBlueprintLibrary** - Helper functions for memory, validation, formatting
- **ADatasetVisualizationActor** - One-function visualization with Niagara
- **UTrajectoryBufferProvider** - GPU buffer management (built-in Position Array NDI)

### C++ Only

- **FLoadedDataset** - Loaded trajectory data structure
- **FTrajectoryLoadParams** - Loading parameters
- **FTrajectoryShardMetadata** - Dataset metadata

## Memory Management

**Always validate memory before loading:**

```
Calculate Dataset Memory Requirement
  → Can Load Dataset
     → Branch
        True  → Load Trajectories Async
        False → Show Error
```

**Optimize memory after loading:**

```
After Binding to Niagara
  → Get Buffer Provider
     → Release CPU Position Data  ← Saves 100-240MB per dataset
```

See [LOADING_AND_MEMORY.md](LOADING_AND_MEMORY.md) for detailed memory management.

## Visualization Approaches

The plugin supports two visualization methods:

### 1. Position Array NDI (Recommended)

- **Performance:** 10x faster than texture approach
- **Memory:** Efficient, with optional CPU data release
- **Setup:** One function call with `DatasetVisualizationActor`
- **Use case:** Production visualizations, large datasets

### 2. Texture2DArray (Legacy)

- **Performance:** Slower (10ms vs 1ms for buffer approach)
- **Memory:** More GPU memory due to texture overhead
- **Setup:** Custom Niagara Data Interface required
- **Use case:** Custom workflows requiring texture access

See [VISUALIZATION.md](VISUALIZATION.md) for comparison and setup guides.

## Performance Tips

1. **Use Async Loading** - `LoadTrajectoriesAsync` for large datasets
2. **Check Memory First** - `CanLoadDataset` before loading
3. **Sample Rate** - Use `SampleRate > 1` to reduce memory (e.g., `SampleRate=2` loads every 2nd sample)
4. **Time Ranges** - Load only needed ranges with `StartTimeStep`/`EndTimeStep`
5. **Release CPU Data** - Call `ReleaseCPUPositionData()` after binding to Niagara
6. **Distributed Selection** - Use `ETrajectorySelectionStrategy::Distributed` for representative samples

## Example Use Cases

### Dataset Selection UI
Create a widget showing available datasets with metadata, let users select which to visualize based on memory capacity.

### Multi-Dataset Visualization
Load multiple related datasets (e.g., bubbles + particles) and visualize them together using their shared spatial origin.

### Time-Range Filtering
Allow users to select time ranges with a slider and dynamically load only the selected range.

### Performance Monitoring
Use memory estimation to show users how much data can fit and adjust loading parameters accordingly.

## Documentation

- **[QUICKSTART.md](QUICKSTART.md)** - 5-minute getting started guide
- **[LOADING_AND_MEMORY.md](LOADING_AND_MEMORY.md)** - Loading API and memory management
- **[VISUALIZATION.md](VISUALIZATION.md)** - Niagara integration and visualization
- **[NAMING_CONVENTION.md](NAMING_CONVENTION.md)** - Directory structure and organization
- **[specification-trajectory-data-shard.md](specification-trajectory-data-shard.md)** - Data format specification

## System Requirements

- Unreal Engine 5.6 or later
- C++17 compiler
- Sufficient RAM for trajectory data (plugin uses 75% of total physical memory as limit)

## License

See [LICENSE](LICENSE) for details.
