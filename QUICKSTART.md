# Quick Start Guide

This guide will help you get started with the Trajectory Data plugin in 5 minutes.

## Step 1: Installation

1. Copy the `TrajectoryData` folder to your Unreal Engine project's `Plugins` directory:
   ```
   YourProject/
   └── Plugins/
       └── TrajectoryData/  <-- Copy here
   ```

2. If your project doesn't have a `Plugins` folder, create one at the project root level.

3. Regenerate project files:
   - Windows: Right-click on your `.uproject` file → "Generate Visual Studio project files"
   - Mac: Right-click on your `.uproject` file → "Generate Xcode project"

4. Open your project in Unreal Engine. The plugin will be automatically detected and compiled.

## Step 2: Prepare Test Data

1. Copy the example dataset to a location on your computer:
   ```
   examples/sample_dataset/  →  C:/TestData/sample_dataset/
   ```
   (Or any directory you prefer)

2. Your directory structure should look like:
   ```
   C:/TestData/
   └── sample_dataset/
       ├── shard_0/
       │   └── shard-manifest.json
       └── shard_1/
           └── shard-manifest.json
   ```

## Step 3: Configure the Plugin

1. Navigate to your project's `Config` directory
2. Create or edit `DefaultTrajectoryData.ini`
3. Add the following:
   ```ini
   [/Script/TrajectoryData.TrajectoryDataSettings]
   DatasetsDirectory=C:/TestData
   bAutoScanOnStartup=True
   bDebugLogging=True
   ```

   **Important:** Set `DatasetsDirectory` to the **parent** directory containing your datasets.

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
LogTemp: TrajectoryDataManager: Scanning datasets directory: C:/TestData
LogTemp: TrajectoryDataManager: Found dataset 'sample_dataset' with 2 shards, 2000 trajectories, 1000 samples
LogTemp: TrajectoryDataManager: Scan complete. Found 1 datasets
LogBlueprintUserMessages: Dataset: sample_dataset
LogBlueprintUserMessages: Trajectories: 2000
LogBlueprintUserMessages: Samples: 1000
LogBlueprintUserMessages: Shards: 2
```

## Step 6: Access Shard Details

To access detailed information about each shard:

```
Get Available Datasets
  → ForEachLoop (Array = Return Value)
     → ForEachLoop (Array = Array Element.Shards)
        → Print String (In String = "Shard ID: " + ToString(Array Element.Shard Id))
        → Print String (In String = "Time Range: " + ToString(Array Element.Time Step Start) + " to " + ToString(Array Element.Time Step End))
        → Print String (In String = "Origin: " + ToString(Array Element.Origin))
```

## Common Blueprint Functions

### Scanning and Querying
- **Scan Trajectory Datasets**: Refresh dataset list from disk
- **Get Available Datasets**: Get all discovered datasets
- **Get Dataset Info**: Get specific dataset by name
- **Get Number of Datasets**: Count of available datasets

### Configuration
- **Get Datasets Directory**: Current configured path
- **Set Datasets Directory**: Change path at runtime

### Utilities
- **Calculate Max Display Points**: Total sample points (trajectories × samples)
- **Calculate Shard Display Points**: Sample points for one shard

## Troubleshooting

### "Found 0 datasets"

**Check:**
1. Is `DatasetsDirectory` pointing to the correct parent directory?
2. Does the directory contain subdirectories (one per dataset)?
3. Do those dataset subdirectories contain shard subdirectories?
4. Do the shard subdirectories contain `shard-manifest.json` files?
5. Are the `shard-manifest.json` files valid JSON?

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
- Plugin API: `TrajectoryData/README.md`
- Data Format: `specification-trajectory-data-shard.md`
- Implementation: `IMPLEMENTATION.md`

For issues, check the Output Log with `bDebugLogging=True` enabled.
