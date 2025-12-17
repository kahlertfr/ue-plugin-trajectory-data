# Quick Start Guide

This guide will help you get started with the Trajectory Data plugin in 5 minutes.

## Step 1: Installation

### Option A: Git Submodule (Recommended)

1. Add this plugin as a git submodule to your project's `Plugins` directory:
   ```bash
   cd YourProject/Plugins
   git submodule add https://github.com/kahlertfr/ue-plugin-trajectory-data.git
   ```

2. Regenerate project files:
   - Windows: Right-click on your `.uproject` file → "Generate Visual Studio project files"
   - Mac: Right-click on your `.uproject` file → "Generate Xcode project"

3. Open your project in Unreal Engine. The plugin will be automatically detected and compiled.

### Option B: Manual Copy

1. Copy the entire repository to your Unreal Engine project's `Plugins` directory:
   ```
   YourProject/
   └── Plugins/
       └── ue-plugin-trajectory-data/  <-- Copy here
   ```

2. If your project doesn't have a `Plugins` folder, create one at the project root level.

3. Follow steps 2-3 from Option A above.

## Step 2: Prepare Test Data

1. Create a scenario directory structure with the example dataset:
   ```
   examples/sample_dataset/  →  C:/TestData/sample_scenario/sample_dataset/
   ```
   (Or any directory you prefer)

2. Your directory structure should follow the **scenario → dataset** hierarchy:
   ```
   C:/TestData/
   └── sample_scenario/
       └── sample_dataset/
           ├── dataset-manifest.json
           ├── dataset-meta.bin
           ├── dataset-trajmeta.bin
           ├── shard-0.bin
           └── shard-1.bin
   ```
   
   Note: A dataset can have one or more shard files (e.g., `shard-0.bin`, `shard-1.bin`) for different time intervals.

## Step 3: Configure the Plugin

1. Navigate to your project's `Config` directory
2. Create or edit `DefaultTrajectoryData.ini`
3. Add the following:
   ```ini
   [/Script/TrajectoryData.TrajectoryDataSettings]
   ScenariosDirectory=C:/TestData
   bAutoScanOnStartup=True
   bDebugLogging=True
   ```

   **Important:** Set `ScenariosDirectory` to the **root** directory containing your scenario folders.

## Step 4: Create a Test Blueprint

1. In Unreal Editor, create a new Blueprint Actor:
   - Content Browser → Right-click → Blueprint Class → Actor
   - Name it `BP_TrajectoryDataTest`

2. Open the Blueprint and add the following nodes in the Event Graph:

### Event Begin Play
```
Event Begin Play
  → Scan Trajectory Datasets (returns Boolean)
     → Branch (Condition = Return Value)
        True → Get Available Datasets
               → ForEachLoop (Array = Return Value)
                  → Print String (In String = "Dataset: " + Array Element.Dataset Name)
                  → Print String (In String = "Trajectories: " + ToString(Array Element.Total Trajectories))
                  → Print String (In String = "Samples: " + ToString(Array Element.Total Samples))
                  → Print String (In String = "Shards: " + ToString(Array.Length(Array Element.Shards)))
        False → Print String (In String = "Failed to scan datasets!")
```

### Simplified Version (Minimal Test)
```
Event Begin Play
  → Scan Trajectory Datasets
     → Get Number of Datasets
        → Print String (In String = "Found " + ToString(Return Value) + " datasets")
```

## Step 5: Test in Editor

1. Drag `BP_TrajectoryDataTest` into your level
2. Click Play (Alt+P)
3. Check the Output Log (Window → Developer Tools → Output Log)

### Expected Output:
```
LogTemp: TrajectoryDataManager: Scanning scenarios directory: C:/TestData
LogTemp: TrajectoryDataManager: Dataset 'sample_dataset' in scenario 'sample_scenario': 1000 trajectories
LogTemp: TrajectoryDataManager: Found 1 dataset(s) in scenario 'sample_scenario'
LogTemp: TrajectoryDataManager: Scan complete. Found 1 datasets across all scenarios
LogBlueprintUserMessages: Dataset: sample_dataset
LogBlueprintUserMessages: Trajectories: 1000
```

## Step 6: Access Dataset Details

To access detailed information about each dataset:

```
Get Available Datasets
  → ForEachLoop (Array = Return Value)
     → Print String (In String = "Dataset: " + Array Element.Dataset Name)
     → Print String (In String = "Scenario: " + Array Element.Scenario Name)
     → Print String (In String = "Trajectories: " + ToString(Array Element.Total Trajectories))
     → Print String (In String = "Bounding Box: " + ToString(Array Element.Metadata.Bounding Box Min) + " to " + ToString(Array Element.Metadata.Bounding Box Max))
```

## Common Blueprint Functions

### Scanning and Querying
- **Scan Trajectory Datasets**: Refresh dataset list from disk
- **Get Available Datasets**: Get all discovered datasets
- **Get Dataset Info**: Get specific dataset by name
- **Get Number of Datasets**: Count of available datasets

### Configuration
- **Get Scenarios Directory**: Current configured path
- **Set Scenarios Directory**: Change path at runtime

### Utilities
- **Calculate Max Display Points**: Total sample points (trajectories × samples)
- **Calculate Shard Display Points**: Sample points for one shard

## Troubleshooting

### "Found 0 datasets"

**Check:**
1. Is `ScenariosDirectory` pointing to the correct root directory?
2. Does the directory contain scenario subdirectories?
3. Do those scenario subdirectories contain dataset subdirectories?
4. Do the dataset subdirectories contain `dataset-manifest.json` files?
5. Are the `dataset-manifest.json` files valid JSON?

**Enable debug logging:**
```ini
bDebugLogging=True
```

### "Failed to scan datasets"

**Check:**
1. Does the directory exist?
2. Do you have read permissions?
3. Check the Output Log for error messages

### Path Issues (Windows)

Use forward slashes OR double backslashes:
- ✅ `C:/TestData`
- ✅ `C:\\TestData`
- ❌ `C:\TestData` (single backslash may cause issues)

## Next Steps

Once you have successfully loaded dataset metadata, you can:

1. **Build a UI**: Create a Widget Blueprint to display available datasets
2. **Filter Data**: Use time step ranges to determine what to load
3. **Calculate Capacity**: Use `Calculate Max Display Points` to check if data fits in memory
4. **Prepare for Loading**: Plan your visualization based on the metadata

## Example Use Cases

### Dataset Selection UI
Create a list of available datasets with their properties, allowing users to select which dataset to visualize.

### Time Range Filtering
Use `Time Step Start` and `Time Step End` to create a slider showing the available time range for visualization.

### Performance Planning
Calculate total display points to determine if you need streaming or can load everything at once.

### Multi-Dataset Correlation
Check if datasets share the same origin to identify spatially correlated data (e.g., bubbles + particles).

## Support

For detailed documentation:
- Plugin API: See main `README.md`
- Data Format: `specification-trajectory-data-shard.md`
- Implementation: `IMPLEMENTATION.md`

For issues, check the Output Log with `bDebugLogging=True` enabled.
