# Trajectory Data Loading - Blueprint Examples

This guide shows how to use the trajectory data loading functionality from Blueprints.

## Overview

The plugin now supports loading actual trajectory data from binary files, not just metadata. You can:

1. **Load full datasets** with customizable parameters (time range, sample rate, trajectory count)
2. **Load specific trajectories** by ID with individual time ranges
3. **Validate before loading** to check memory requirements
4. **Load asynchronously** with progress callbacks
5. **Stream data** by adjusting time ranges dynamically

## Basic Loading Workflow

### 1. Scan Available Datasets

First, scan for available datasets as before:

```
BeginPlay
  └─> Scan Trajectory Datasets
```

### 2. Create Load Parameters

Create a `FTrajectoryLoadParams` structure with your desired parameters:

**Required Fields:**
- **Dataset Path**: Full path to the dataset directory
- **Start Time Step**: Start of time range (-1 for dataset start)
- **End Time Step**: End of time range (-1 for dataset end)
- **Sample Rate**: 1 for all samples, 2 for every 2nd, etc.

**Trajectory Selection:**
- **Selection Strategy**: Choose how to select trajectories
  - `FirstN`: Load first N trajectories
  - `Distributed`: Load N trajectories distributed across dataset
  - `ExplicitList`: Load specific trajectory IDs
- **Num Trajectories**: Number of trajectories (for FirstN/Distributed)
- **Trajectory Selections**: Array of specific trajectories (for ExplicitList)

### 3. Validate Parameters

Before loading, validate that the configuration can be loaded:

```
Create Load Params
  └─> Validate Trajectory Load Params
       └─> Branch (Can Load?)
            ├─> True: Proceed with loading
            └─> False: Show error message
```

The validation returns:
- **Can Load**: Whether memory is sufficient
- **Message**: Validation result or error
- **Estimated Memory Bytes**: How much memory will be used
- **Num Trajectories To Load**: How many trajectories will load
- **Num Samples Per Trajectory**: Samples per trajectory

### 4. Load Trajectories

#### Synchronous Loading (Blocking)

For small datasets or when you want to wait for completion:

```
Validate Success?
  └─> Load Trajectories Sync
       └─> Process Result
            ├─> Success: Access loaded trajectories
            └─> Failed: Show error message
```

#### Asynchronous Loading (Non-Blocking)

For large datasets with progress feedback:

```
Get Trajectory Loader
  └─> Bind Event to OnLoadProgress
  └─> Bind Event to OnLoadComplete
  └─> Load Trajectories Async
```

**Progress Callback:**
```
OnLoadProgress Event
  ├─> Trajectories Loaded (int)
  ├─> Total Trajectories (int)
  └─> Progress Percent (float)
       └─> Update UI progress bar
```

**Completion Callback:**
```
OnLoadComplete Event
  ├─> Success (bool)
  └─> Result (FTrajectoryLoadResult)
       └─> Process loaded data or show error
```

## Example Scenarios

### Scenario 1: Load First 100 Trajectories

```blueprint
1. Get Dataset Info by Name
2. Create Load Params:
   - Dataset Path: from Dataset Info
   - Start Time Step: -1 (use dataset start)
   - End Time Step: -1 (use dataset end)
   - Sample Rate: 1 (all samples)
   - Selection Strategy: FirstN
   - Num Trajectories: 100
3. Validate Trajectory Load Params
4. If Can Load:
   - Load Trajectories Sync
   - Process Result
```

### Scenario 2: Load Every 10th Trajectory (Distributed)

```blueprint
1. Get Dataset Info by Name
2. Create Load Params:
   - Dataset Path: from Dataset Info
   - Start Time Step: 0
   - End Time Step: 500
   - Sample Rate: 2 (every 2nd sample)
   - Selection Strategy: Distributed
   - Num Trajectories: 50
3. Validate Trajectory Load Params
4. If Can Load:
   - Load Trajectories Sync
   - Process Result
```

### Scenario 3: Load Specific Trajectories with Custom Time Ranges

```blueprint
1. Create Trajectory Selection Array:
   - Selection 1:
     * Trajectory ID: 42
     * Start Time Step: 0
     * End Time Step: 100
   - Selection 2:
     * Trajectory ID: 123
     * Start Time Step: 50
     * End Time Step: 200
   - Selection 3:
     * Trajectory ID: 456
     * Start Time Step: -1 (use dataset start)
     * End Time Step: -1 (use dataset end)

2. Create Load Params:
   - Dataset Path: from Dataset Info
   - Start Time Step: -1
   - End Time Step: -1
   - Sample Rate: 1
   - Selection Strategy: ExplicitList
   - Trajectory Selections: from step 1

3. Validate Trajectory Load Params
4. If Can Load:
   - Load Trajectories Sync
   - Process Result
```

### Scenario 4: Async Loading with Progress UI

```blueprint
Event BeginPlay:
  └─> Get Trajectory Loader
       └─> Bind Event: OnLoadProgress → Update Progress Bar
       └─> Bind Event: OnLoadComplete → Show Results

Button Click Event:
  └─> Create Load Params (100 trajectories)
  └─> Validate Trajectory Load Params
  └─> If Can Load:
       └─> Get Trajectory Loader
            └─> Load Trajectories Async

Update Progress Bar:
  └─> Set Progress (Progress Percent / 100)
  └─> Set Text: "Loading {Trajectories Loaded} / {Total Trajectories}"

Show Results:
  └─> Branch (Success?)
       ├─> True: Show "Loaded {Num Trajectories} trajectories"
       └─> False: Show Error Message
```

## Accessing Loaded Data

### Get Loaded Trajectories

```blueprint
Get Trajectory Loader
  └─> Get Loaded Trajectories
       └─> For Each Loop
            └─> Access Trajectory Data:
                 - Trajectory ID
                 - Start/End Time Step
                 - Extent
                 - Samples Array
```

### Access Individual Samples

```blueprint
For Each Trajectory:
  └─> Get Samples
       └─> For Each Sample:
            └─> Access Sample Data:
                 - Time Step
                 - Position (FVector)
                 - Is Valid (bool)
```

### Memory Management

```blueprint
Get Loaded Data Memory Usage
  └─> Format Memory Size
       └─> Display: "Using {Formatted Size}"

Get Num Loaded Trajectories
  └─> Display: "{Count} trajectories loaded"

Unload All Trajectories
  └─> Clears all loaded data and frees memory
```

## Data Streaming / Time Window Adjustment

To implement data streaming where users can adjust the time range:

```blueprint
Event: Time Range Changed
  ├─> Create New Load Params with updated time range
  ├─> Validate Trajectory Load Params
  ├─> If Can Load:
  │    ├─> Unload All Trajectories (free old data)
  │    └─> Load Trajectories Async (load new range)
  └─> Else: Show warning
```

## Memory Monitoring Integration

Combine with existing memory monitoring:

```blueprint
Before Loading:
  └─> Calculate Dataset Memory Requirement
       └─> Add Estimated Usage
            └─> Update UI with remaining capacity
            └─> Validate Trajectory Load Params
                 └─> Proceed if Can Load

After Loading:
  └─> Get Loaded Data Memory Usage
       └─> Verify actual usage matches estimate
       └─> Update UI with actual usage
```

## Converting to Niagara Data

Loaded trajectory data can be converted for Niagara visualization:

### Prepare Positions for Niagara

```blueprint
Get Loaded Trajectories
  └─> For Each Trajectory:
       └─> Get Samples
            └─> For Each Valid Sample:
                 ├─> Add Position to Array
                 ├─> Add Time Step to Array
                 └─> Add Trajectory ID to Array

Create Niagara Texture or Data Interface
  └─> Upload position data
  └─> Upload time data
  └─> Set particle count
```

### Create Time-Based Animation

```blueprint
Update Loop (Tick):
  └─> Current Time += Delta Time
  └─> For Each Trajectory:
       └─> Find Sample at Current Time
            └─> Update Particle Position
            └─> Update Particle Visibility (based on Valid flag)
```

## Best Practices

### 1. Always Validate First
```blueprint
Never load without validation:
  Validate → Check Can Load → Load
```

### 2. Use Appropriate Sample Rates
```blueprint
For preview: Sample Rate = 5 or 10
For analysis: Sample Rate = 1
For visualization: Sample Rate = 2 or 3
```

### 3. Load Progressively
```blueprint
For large datasets:
  - Start with FirstN (small count)
  - Let user inspect data
  - Offer "Load More" option
```

### 4. Unload When Done
```blueprint
Always clean up:
  Unload All Trajectories
  Reset Estimated Usage
```

### 5. Handle Errors Gracefully
```blueprint
Check Success flags:
  - Validation Result → Can Load
  - Load Result → Success
Show user-friendly error messages
```

## Performance Tips

1. **Sample Rate**: Use higher sample rates (2-5) for initial visualization
2. **Trajectory Count**: Start small (10-100) and increase as needed
3. **Time Range**: Load only the time window needed
4. **Async Loading**: Use async for datasets > 50MB
5. **Memory Monitoring**: Always check available memory first
6. **Unload Unused Data**: Free memory when switching datasets

## Common Issues

### Issue: "Insufficient Memory" Error
**Solution**: 
- Reduce number of trajectories
- Increase sample rate
- Reduce time range
- Unload other datasets first

### Issue: Slow Loading Performance
**Solution**:
- Use async loading for large datasets
- Increase sample rate for preview
- Load in smaller chunks (time windows)

### Issue: Invalid Samples (NaN)
**Solution**:
- Check Sample.IsValid flag before using
- Filter out invalid samples
- Some trajectories may have gaps in data

### Issue: Trajectory Not Found
**Solution**:
- Verify trajectory ID exists in dataset
- Check FirstTrajectoryId and LastTrajectoryId in metadata
- Ensure dataset files are not corrupted

## See Also

- [Memory Monitoring Blueprint Example](MEMORY_MONITORING_BLUEPRINT.md)
- [Trajectory Dataset Specification](../specification-trajectory-data-shard.md)
- [Quick Start Guide](../QUICKSTART.md)
