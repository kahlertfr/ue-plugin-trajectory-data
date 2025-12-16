# Memory Monitoring Blueprint Example

This document provides an example of how to create a Blueprint that monitors system memory capacity for trajectory data loading.

## Overview

The memory monitoring system allows you to:
- Check available system memory
- Calculate memory requirements for datasets/shards before loading
- Show immediate feedback to users when they adjust loading parameters
- Prevent loading data that exceeds available capacity

## Memory Calculation

The plugin calculates memory requirements based on the Trajectory Data Shard specification:

### Per Shard:
- **Shard Meta**: 76 bytes (fixed)
- **Trajectory Meta**: 40 bytes × trajectory_count
- **Data Block Header**: 32 bytes (per data file)
- **Data Entries**: entry_size_bytes × trajectory_count

For example, with the sample dataset (shard_0):
- Trajectory Count: 1000
- Entry Size: 616 bytes
- **Total**: 76 + (40 × 1000) + 32 + (616 × 1000) = **656,108 bytes (~641 KB)**

### Memory Budget:
The system assumes **75% of total physical memory** can be used for trajectory data.

## Blueprint Example: Memory Monitor Widget

### Step 1: Create a Widget Blueprint

Create a new Widget Blueprint (e.g., `WBP_TrajectoryMemoryMonitor`) with the following UI elements:

#### Text Blocks:
- `TXT_TotalMemory` - Shows total system memory
- `TXT_MaxTrajectoryMemory` - Shows maximum memory for trajectory data (75% of total)
- `TXT_CurrentUsage` - Shows current estimated usage
- `TXT_RemainingCapacity` - Shows remaining capacity
- `TXT_UsagePercentage` - Shows usage percentage

#### Progress Bar:
- `PB_MemoryUsage` - Visual representation of memory usage (0.0 to 1.0)

#### Optional UI for Dataset Selection:
- Combo Box or List View to select datasets/shards
- Button to simulate adding/removing data selections

### Step 2: Event Graph Setup

#### On Widget Constructed:
```
Event Construct
  └─> Update Memory Display
```

#### Update Memory Display Function:
```
Function: UpdateMemoryDisplay

1. Call "Get Memory Info"
   └─> Returns FTrajectoryDataMemoryInfo struct

2. Format and display values:
   ├─> Set Text (TXT_TotalMemory)
   │    └─> Format Memory Size (TotalPhysicalMemory)
   │
   ├─> Set Text (TXT_MaxTrajectoryMemory)
   │    └─> Format Memory Size (MaxTrajectoryDataMemory)
   │
   ├─> Set Text (TXT_CurrentUsage)
   │    └─> Format Memory Size (CurrentEstimatedUsage)
   │
   ├─> Set Text (TXT_RemainingCapacity)
   │    └─> Format Memory Size (RemainingCapacity)
   │
   ├─> Set Text (TXT_UsagePercentage)
   │    └─> Format: "{UsagePercentage}%"
   │
   └─> Set Progress Bar Percent (PB_MemoryUsage)
        └─> UsagePercentage / 100.0
```

### Step 3: Interactive Parameter Adjustment

When the user selects a dataset to load (without actually loading it):

```
Event: On Dataset Selected

1. Get Dataset Info by name
   └─> Returns FTrajectoryDatasetInfo

2. Calculate Dataset Memory Requirement
   └─> Returns int64 RequiredMemory

3. Add Estimated Usage
   └─> Input: RequiredMemory
   
4. Update Memory Display
   └─> Shows immediate feedback

5. Can Load Dataset?
   ├─> TRUE: Show "Can Load" indicator (green)
   └─> FALSE: Show "Insufficient Memory" warning (red)
```

When the user deselects or changes selection:

```
Event: On Dataset Deselected

1. Calculate Dataset Memory Requirement
   └─> Returns int64 RequiredMemory

2. Remove Estimated Usage
   └─> Input: RequiredMemory
   
3. Update Memory Display
   └─> Shows updated capacity
```

### Step 4: Reset Button

```
Event: On Reset Button Clicked

1. Reset Estimated Usage
   └─> Clears all estimates

2. Update Memory Display
   └─> Shows full available capacity
```

## Complete Blueprint Flow Example

### Simple Memory Monitor:

```
Event Construct
  └─> Update Memory Display (Custom Event)

Custom Event: Update Memory Display
  ├─> Get Memory Info
  │    └─> Break FTrajectoryDataMemoryInfo
  │         ├─> TotalPhysicalMemory
  │         ├─> MaxTrajectoryDataMemory
  │         ├─> CurrentEstimatedUsage
  │         ├─> RemainingCapacity
  │         └─> UsagePercentage
  │
  ├─> Format Memory Size (TotalPhysicalMemory)
  │    └─> Set Text: TXT_TotalMemory
  │
  ├─> Format Memory Size (MaxTrajectoryDataMemory)
  │    └─> Set Text: TXT_MaxTrajectoryMemory
  │
  ├─> Format Memory Size (CurrentEstimatedUsage)
  │    └─> Set Text: TXT_CurrentUsage
  │
  ├─> Format Memory Size (RemainingCapacity)
  │    └─> Set Text: TXT_RemainingCapacity
  │
  ├─> Format String: "{0}%"
  │    └─> Set Text: TXT_UsagePercentage
  │
  └─> Set Percent (UsagePercentage / 100.0)
       └─> Progress Bar: PB_MemoryUsage
```

### Dataset Selection with Immediate Feedback:

```
Event: On Dataset Checkbox Changed
  │
  ├─> Branch: Is Checked?
  │    │
  │    ├─> TRUE:
  │    │    ├─> Get Dataset Info (DatasetName)
  │    │    ├─> Calculate Dataset Memory Requirement
  │    │    ├─> Add Estimated Usage (RequiredMemory)
  │    │    ├─> Can Load Dataset?
  │    │    │    ├─> TRUE: Set Checkbox Color (Green)
  │    │    │    └─> FALSE: Set Checkbox Color (Red) + Show Warning
  │    │    └─> Update Memory Display
  │    │
  │    └─> FALSE:
  │         ├─> Get Dataset Info (DatasetName)
  │         ├─> Calculate Dataset Memory Requirement
  │         ├─> Remove Estimated Usage (RequiredMemory)
  │         └─> Update Memory Display
```

## Example with Sample Dataset

Using the provided sample dataset:

### Dataset: sample_dataset
- Shard 0: 1000 trajectories, 616 bytes per entry = ~641 KB
- Shard 1: 1000 trajectories, 616 bytes per entry = ~641 KB
- **Total: ~1.28 MB**

### System with 16 GB RAM:
- Total Physical Memory: 16 GB
- Max Trajectory Data Memory: 12 GB (75%)
- Sample Dataset: 1.28 MB
- **Usage: 0.01%** ✓ Can load

### System with 4 GB RAM:
- Total Physical Memory: 4 GB
- Max Trajectory Data Memory: 3 GB (75%)
- Sample Dataset: 1.28 MB
- **Usage: 0.04%** ✓ Can load

## Real-World Usage Pattern

1. **On Application Start:**
   - Create the memory monitor widget
   - Display available capacity

2. **User Browses Datasets:**
   - Scan Trajectory Datasets
   - Display list of available datasets

3. **User Selects Multiple Shards:**
   - For each selection:
     - Calculate memory requirement
     - Add to estimated usage
     - Update display immediately
   - User sees real-time feedback

4. **User Clicks "Load Data":**
   - Check if all selections can fit
   - If yes: proceed with actual loading
   - If no: show error and prevent loading

5. **After Loading:**
   - Keep estimated usage for monitoring
   - User can unload data and see capacity restored

## Helper Functions You Can Create

### Blueprint Function: Get System Memory as String
```
Function: GetSystemMemoryString
Output: String

├─> Get Total Physical Memory
├─> Format Memory Size
└─> Return formatted string
```

### Blueprint Function: Get Available Memory Percentage
```
Function: GetAvailableMemoryPercentage
Output: Float (0.0-100.0)

├─> Get Memory Info
├─> Break struct
├─> Divide: RemainingCapacity / MaxTrajectoryDataMemory
├─> Multiply: × 100.0
└─> Return percentage
```

### Blueprint Function: Check Dataset Fits
```
Function: CheckDatasetFits
Input: FTrajectoryDatasetInfo
Output: Bool

├─> Can Load Dataset?
└─> Return result
```

## Color Coding Recommendations

- **Green (0-60% usage)**: Plenty of capacity available
- **Yellow (60-85% usage)**: Moderate usage, monitor carefully
- **Orange (85-95% usage)**: High usage, be cautious
- **Red (95-100% usage)**: Critical, risk of exceeding capacity

## Notes

- The memory estimator provides **estimates** based on the specification
- Actual memory usage may vary slightly due to system overhead
- The 75% threshold provides a safety margin for system stability
- Always test with your specific datasets and hardware configurations

## Advanced: Custom Parameter Sliders

For a more advanced UI, you could add sliders to adjust:
- Number of trajectories to load
- Time step range
- Spatial filtering

Update the memory estimate in real-time as users adjust these parameters before committing to load the data.

Example:
```
Event: On Trajectory Count Slider Changed
  ├─> Calculate memory for new count
  ├─> Update estimated usage
  └─> Show immediate feedback to user
```

This provides the best user experience by showing exactly what the system can handle before any actual data loading occurs.
