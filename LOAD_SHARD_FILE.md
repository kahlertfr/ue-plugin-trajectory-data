# Loading Single Shard Files

## Overview

The `LoadShardFile` function provides a **C++ only** API for loading and parsing the complete content of a single shard file into structured, easily accessible data. This is useful for C++ components that need direct access to trajectory data, such as:

- Hash table builders
- Custom data indexing systems
- External data processing pipelines
- Performance analysis tools

**Note:** This API is not exposed to Blueprints. For Blueprint trajectory loading, use `LoadTrajectoriesSync` or `LoadTrajectoriesAsync` instead.

## API

### Function Signature

```cpp
// C++ Only - Not exposed to Blueprints
FShardFileData LoadShardFile(const FString& ShardFilePath);
```

### Parameters

- `ShardFilePath`: Full path to the shard file (e.g., `"C:/Data/Scenarios/my_scenario/my_dataset/shard-0.bin"`)

### Return Value

Returns an `FShardFileData` struct containing:

- `Header`: The shard file header (`FDataBlockHeaderBinary`) with metadata
- `Entries`: Array of parsed trajectory entries (`TArray<FShardTrajectoryEntry>`)
- `FilePath`: Path to the loaded file
- `bSuccess`: Whether the load was successful
- `ErrorMessage`: Error description if load failed

### FShardTrajectoryEntry Structure

Each entry contains:

- `TrajectoryId` (int64): Unique trajectory identifier
- `StartTimeStepInInterval` (int32): First valid time step in this interval (-1 if none)
- `ValidSampleCount` (int32): Number of valid samples
- `Positions` (TArray<FVector3f>): All position samples (NaN indicates invalid samples)

## Usage Example

```cpp
#include "TrajectoryDataLoader.h"

void LoadAndProcessShard()
{
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    // Load and parse a shard file
    FString ShardPath = TEXT("C:/Data/MyScenario/MyDataset/shard-0.bin");
    FShardFileData ShardData = Loader->LoadShardFile(ShardPath);
    
    if (!ShardData.bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load: %s"), *ShardData.ErrorMessage);
        return;
    }
    
    // Access header information
    UE_LOG(LogTemp, Log, TEXT("Global interval index: %d"), 
        ShardData.Header.GlobalIntervalIndex);
    UE_LOG(LogTemp, Log, TEXT("Time step interval size: %d"), 
        ShardData.Header.TimeStepIntervalSize);
    UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectory entries"), 
        ShardData.Entries.Num());
    
    // Process each trajectory entry with structured access
    for (const FShardTrajectoryEntry& Entry : ShardData.Entries)
    {
        UE_LOG(LogTemp, Log, TEXT("Trajectory ID: %lld"), Entry.TrajectoryId);
        UE_LOG(LogTemp, Log, TEXT("  Start time step: %d"), Entry.StartTimeStepInInterval);
        UE_LOG(LogTemp, Log, TEXT("  Valid samples: %d"), Entry.ValidSampleCount);
        
        // Skip entries with no valid data
        if (Entry.StartTimeStepInInterval == -1)
        {
            continue;
        }
        
        // Access positions directly
        for (int32 i = 0; i < Entry.Positions.Num(); ++i)
        {
            const FVector3f& Pos = Entry.Positions[i];
            
            // Check for NaN (invalid sample)
            if (!FMath::IsNaN(Pos.X))
            {
                // Process valid position
                ProcessPosition(Entry.TrajectoryId, i, Pos);
            }
        }
    }
}
```

## Performance Optimization

The implementation uses **efficient bulk memory operations** for maximum performance:

### Single Memcpy for Positions Array

Instead of iterating over individual positions, the entire positions array is copied with a single `memcpy` operation:

```cpp
// OLD APPROACH (slow - iterates over each position)
for (int32 i = 0; i < TimeStepIntervalSize; ++i)
{
    Entry.Positions[i].X = PositionsData[i * 3 + 0];
    Entry.Positions[i].Y = PositionsData[i * 3 + 1];
    Entry.Positions[i].Z = PositionsData[i * 3 + 2];
}

// NEW APPROACH (fast - single bulk copy)
FMemory::Memcpy(Entry.Positions.GetData(), PositionsDataPtr, PositionsDataSize);
```

### Binary-Packed Header Struct

The entry header is accessed using a binary-packed struct (`FTrajectoryEntryHeaderBinary`) that exactly matches the file format:

```cpp
#pragma pack(push, 1)
struct FTrajectoryEntryHeaderBinary
{
    uint64 TrajectoryId;                // 8 bytes
    int32 StartTimeStepInInterval;      // 4 bytes
    int32 ValidSampleCount;             // 4 bytes
};
#pragma pack(pop)
```

This allows direct casting and field access without manual offset calculations.

### Performance Benefits

- **Eliminates per-position loops**: For a shard with 50 time steps and 1000 trajectories, this eliminates 50,000 individual field assignments
- **Better CPU cache utilization**: Bulk memcpy is highly optimized for sequential memory operations
- **Reduced function call overhead**: Single memcpy instead of thousands of individual assignments
- **Memory layout matches binary format**: `FVector3f` (3 consecutive floats) matches the file's float[3] layout exactly

## Shard File Structure

Each shard file follows this binary layout (see [specification-trajectory-data-shard.md](specification-trajectory-data-shard.md)):

### Header (32 bytes)
```
Offset  Type    Field
0       char[4] Magic ("TDDB")
4       uint8   FormatVersion (1)
5       uint8   EndiannessFlag (0=little, 1=big)
6       uint16  Reserved
8       int32   GlobalIntervalIndex
12      int32   TimeStepIntervalSize
16      int32   TrajectoryEntryCount
20      int64   DataSectionOffset (usually 32)
28      uint32  Reserved2
```

### Data Section
Following the header at `DataSectionOffset`, the file contains `TrajectoryEntryCount` entries, each with this structure:

```
Offset  Type        Field
0       uint64      TrajectoryId
8       int32       StartTimeStepInInterval (-1 if no valid samples)
12      int32       ValidSampleCount
16      float[N][3] Positions (N = TimeStepIntervalSize)
```

Where each position is 3 floats (x, y, z) and N depends on the dataset's `TimeStepIntervalSize`.

## Processing Trajectory Entries

With structured data access, processing entries is straightforward:

```cpp
void ProcessTrajectoryEntries(const FShardFileData& ShardData)
{
    // Iterate through all parsed entries
    for (const FShardTrajectoryEntry& Entry : ShardData.Entries)
    {
        UE_LOG(LogTemp, Log, TEXT("Processing trajectory %lld"), Entry.TrajectoryId);
        
        // Skip entries with no valid data
        if (Entry.StartTimeStepInInterval == -1)
        {
            UE_LOG(LogTemp, Verbose, TEXT("  No valid samples"));
            continue;
        }
        
        UE_LOG(LogTemp, Log, TEXT("  Valid samples: %d starting at time step %d"),
            Entry.ValidSampleCount, Entry.StartTimeStepInInterval);
        
        // Process positions (NaN values indicate invalid samples)
        for (int32 TimeStep = 0; TimeStep < Entry.Positions.Num(); ++TimeStep)
        {
            const FVector3f& Pos = Entry.Positions[TimeStep];
            
            // Check for NaN (invalid sample)
            if (!FMath::IsNaN(Pos.X))
            {
                // Process valid position
                ProcessPosition(Entry.TrajectoryId, TimeStep, Pos);
            }
        }
    }
}
```

## Building Hash Tables

Example of using structured data for hash table construction:

```cpp
void BuildHashTable(const FShardFileData& ShardData)
{
    // Build hash table mapping trajectory ID to entry index
    TMap<int64, int32> TrajectoryIndexMap;
    
    for (int32 i = 0; i < ShardData.Entries.Num(); ++i)
    {
        TrajectoryIndexMap.Add(ShardData.Entries[i].TrajectoryId, i);
    }
    
    // Quick lookup by trajectory ID
    int64 SearchId = 1234;
    if (int32* FoundIndex = TrajectoryIndexMap.Find(SearchId))
    {
        const FShardTrajectoryEntry& Entry = ShardData.Entries[*FoundIndex];
        UE_LOG(LogTemp, Log, TEXT("Found trajectory %lld with %d positions"),
            Entry.TrajectoryId, Entry.Positions.Num());
    }
}

// Alternative: Copy entry data into hash table for independent lifetime
void BuildPersistentHashTable(const FShardFileData& ShardData)
{
    TMap<int64, FShardTrajectoryEntry> TrajectoryHashTable;
    
    // Copy entries into hash table (data persists independently)
    for (const FShardTrajectoryEntry& Entry : ShardData.Entries)
    {
        TrajectoryHashTable.Add(Entry.TrajectoryId, Entry);
    }
    
    // Hash table can be used after ShardData goes out of scope
}
```

## Error Handling

The function validates several conditions and returns error information if any fail:

- **Empty path**: `"Shard file path is empty"`
- **File not found**: `"Shard file does not exist: <path>"`
- **Invalid size**: `"Invalid shard file size: <size>"`
- **Too small**: `"Shard file too small (size: <size>, minimum: <min>)"`
- **Open failed**: `"Failed to open shard file: <path>"`
- **Read failed**: `"Failed to read shard file header"` or `"Failed to read trajectory entry <n>"`
- **Invalid magic**: `"Invalid shard file format: magic number mismatch"`
- **Unsupported version**: `"Unsupported shard file format version: <version>"`
- **Invalid offset**: `"Invalid data section offset: <offset>"`
- **File too small**: `"File too small for declared entry count"`

Always check `bSuccess` before using the data:

```cpp
FShardFileData ShardData = Loader->LoadShardFile(ShardPath);
if (!ShardData.bSuccess)
{
    UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *ShardData.ErrorMessage);
    return;
}
```

## Memory Considerations

The function parses all trajectory entries into structured data. Memory usage is proportional to:
- Number of trajectories
- Time step interval size
- Position data (12 bytes per FVector3f)

Example memory usage (approximate):
- Header + struct overhead: ~100 bytes per entry
- Positions: TimeStepIntervalSize × 12 bytes per entry
- 50 time steps, 1000 trajectories: ~600 KB per shard
- 100 time steps, 10000 trajectories: ~12 MB per shard
- 200 time steps, 50000 trajectories: ~120 MB per shard

For large datasets:
- Process shards sequentially rather than loading all at once
- Free memory by allowing `FShardFileData` to go out of scope when done
- Consider filtering entries if you only need specific trajectory IDs

## Integration with Other Components

The structured data makes integration straightforward:

```cpp
// Example: Hash table builder
class UMyHashTableBuilder
{
public:
    void BuildFromShard(const FShardFileData& ShardData)
    {
        // Direct access to parsed trajectory entries
        for (const FShardTrajectoryEntry& Entry : ShardData.Entries)
        {
            // Build hash table indexes from trajectory IDs
            AddToHashTable(Entry.TrajectoryId, Entry);
        }
    }
};

// Usage
UMyHashTableBuilder* Builder = NewObject<UMyHashTableBuilder>();
FShardFileData ShardData = Loader->LoadShardFile(ShardPath);
if (ShardData.bSuccess)
{
    Builder->BuildFromShard(ShardData);
}
```

## Blueprint Support

The function is also exposed to Blueprints via `UFUNCTION(BlueprintCallable)`:

1. In Blueprint, get the Trajectory Data Loader singleton
2. Call "Load Shard File" node with the file path
3. Check "Success" boolean before using the data
4. Access header fields and parsed trajectory entries directly
5. Iterate through `Entries` array to process each trajectory

The structured data is fully accessible in Blueprints, making it easy to:
- Loop through trajectory entries
- Access trajectory IDs and metadata
- Read position arrays
- Build custom data structures
