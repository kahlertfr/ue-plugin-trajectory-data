# Quick Start Guide

Get started with the Trajectory Data plugin for Unreal Engine in 5 minutes.

## Step 1: Installation

### Option A: Git Submodule (Recommended)

```bash
cd YourProject/Plugins
git submodule add https://github.com/kahlertfr/ue-plugin-trajectory-data.git
```

Then regenerate project files and open your project in Unreal Engine.

### Option B: Manual Copy

1. Copy this repository to `YourProject/Plugins/ue-plugin-trajectory-data/`
2. Regenerate project files (right-click `.uproject` → Generate project files)
3. Open your project in Unreal Engine

## Step 2: Configure the Plugin

Create or edit `YourProject/Config/DefaultTrajectoryData.ini`:

```ini
[/Script/TrajectoryData.TrajectoryDataSettings]
ScenariosDirectory=C:/Data/TrajectoryScenarios
bAutoScanOnStartup=True
bDebugLogging=False
```

**Important:** `ScenariosDirectory` points to the root directory containing scenario folders.

## Step 3: Prepare Test Data

Organize your trajectory data following the **Scenario → Dataset** hierarchy:

```
C:/Data/TrajectoryScenarios/        ← ScenariosDirectory
└── my_scenario/                     ← Scenario folder
    └── my_dataset/                  ← Dataset folder
        ├── dataset-manifest.json
        ├── dataset-meta.bin
        ├── dataset-trajmeta.bin
        └── shard-0.bin              ← One or more shard files
```

Each dataset directory must contain:
- `dataset-manifest.json` - Dataset metadata (required)
- `dataset-meta.bin` - Binary metadata summary
- `dataset-trajmeta.bin` - Per-trajectory metadata
- `shard-*.bin` - Position data files (one or more)

See [specification-trajectory-data-shard.md](specification-trajectory-data-shard.md) for data format details.

## Step 4: Scan for Datasets

### In Blueprint

Create a Blueprint Actor (`BP_TrajectoryTest`):

```
Event Begin Play
  → Scan Trajectory Datasets
     → Branch (if success)
        True  → Get Number of Datasets
               → Print String ("Found {count} datasets")
        False → Print String ("Failed to scan datasets")
```

### In C++

```cpp
#include "TrajectoryDataManager.h"

void AMyActor::BeginPlay()
{
    Super::BeginPlay();
    
    UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
    if (Manager && Manager->ScanDatasets())
    {
        TArray<FTrajectoryDatasetInfo> Datasets = Manager->GetAvailableDatasets();
        UE_LOG(LogTemp, Log, TEXT("Found %d datasets"), Datasets.Num());
    }
}
```

## Step 5: Load and Visualize Data

### Quick Visualization (Recommended)

Use `ADatasetVisualizationActor` for one-function visualization:

1. **Add to Level:** Drag `DatasetVisualizationActor` into your level
2. **Configure:**
   - Set `Niagara System Template` to your particle system
   - Set `Auto Load On Begin Play` to true
   - Set `Auto Load Dataset Index` to 0
3. **Play:** The actor will automatically load and visualize the first dataset

### Manual Loading in Blueprint

```
Event Begin Play
  → Get Trajectory Loader
     → Create Load Params
        - Dataset Path: "C:/Data/TrajectoryScenarios/my_scenario/my_dataset"
        - Selection Strategy: First N
        - Num Trajectories: 100
        - Start Time Step: -1 (dataset start)
        - End Time Step: -1 (dataset end)
        - Sample Rate: 1 (every sample)
     → Load Trajectories Async
        - Params: (from above)
        - On Load Complete → Bind to Niagara
```

See [LOADING_AND_MEMORY.md](LOADING_AND_MEMORY.md) for detailed loading API.

### Bind to Niagara System

After loading data:

```
On Load Complete
  → Get Dataset Visualization Actor
     → Load And Bind Dataset (index: 0)
        → On Success: Visualization Active
```

See [VISUALIZATION.md](VISUALIZATION.md) for detailed visualization guide.

## Step 6: Memory Management

**Important:** Always check memory before loading large datasets.

### In Blueprint

```
Before Loading
  → Calculate Dataset Memory Requirement
     - Dataset Info: (selected dataset)
  → Can Load Dataset
     → Branch
        True  → Load Trajectories Async
        False → Print String ("Insufficient memory")
```

### Memory Optimization

After binding data to Niagara, release CPU memory:

```
After Binding to Niagara
  → Get Buffer Provider
     → Release CPU Position Data  ← Saves significant memory
```

This can save 100-240MB per dataset depending on size.

See [LOADING_AND_MEMORY.md](LOADING_AND_MEMORY.md) for detailed memory management.

## Common Issues

### "Found 0 datasets"

**Check:**
- Is `ScenariosDirectory` correct in `DefaultTrajectoryData.ini`?
- Does the directory exist and have read permissions?
- Is the directory structure correct (Scenario → Dataset)?
- Does each dataset folder contain `dataset-manifest.json`?

**Enable debug logging:**
```ini
bDebugLogging=True
```

### Path Issues (Windows)

Use forward slashes or double backslashes:
- ✅ `C:/TestData`
- ✅ `C:\\TestData`
- ❌ `C:\TestData` (single backslash may fail)

### "Failed to load dataset"

**Check Output Log for specific error:**
- Invalid dataset path
- Corrupt or missing binary files
- Insufficient memory
- Invalid manifest JSON

## Next Steps

### Learn More

- **[LOADING_AND_MEMORY.md](LOADING_AND_MEMORY.md)** - Loading API and memory management
- **[VISUALIZATION.md](VISUALIZATION.md)** - Visualization approaches and Niagara integration
- **[NAMING_CONVENTION.md](NAMING_CONVENTION.md)** - Directory structure and naming rules
- **[specification-trajectory-data-shard.md](specification-trajectory-data-shard.md)** - Data format specification

### Example Workflows

**Dataset Selection UI:**
```
1. Scan datasets
2. Display list with metadata (name, trajectory count, bounding box)
3. Let user select dataset to visualize
4. Check memory capacity
5. Load and visualize if sufficient memory
```

**Multi-Dataset Visualization:**
```
1. Load multiple datasets with LoadTrajectoriesAsync
2. Create one DatasetVisualizationActor per dataset
3. Bind each to a different Niagara system
4. Position actors using dataset bounding boxes
```

**Time-Range Filtering:**
```
1. Get dataset metadata (time step range)
2. Create UI slider for time range selection
3. Load only selected time range with StartTimeStep/EndTimeStep
4. Update visualization dynamically
```

## Performance Tips

1. **Use Async Loading** - Always use `LoadTrajectoriesAsync` for large datasets
2. **Sample Rate** - Use `SampleRate > 1` to reduce memory for high-frequency data
3. **Time Ranges** - Load only needed time ranges with `StartTimeStep`/`EndTimeStep`
4. **Release CPU Data** - Call `ReleaseCPUPositionData()` after binding to Niagara
5. **Selection Strategy** - Use `Distributed` for representative sample across dataset

## Support

- **Documentation:** See files in repository root
- **Specification:** `specification-trajectory-data-shard.md`
- **Issues:** Check Output Log with `bDebugLogging=True`
