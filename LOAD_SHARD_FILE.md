# Loading Single Shard Files

## Overview

The `LoadShardFile` function provides a public API for loading the complete content of a single shard file into memory. This is useful for external components that need direct access to raw shard data, such as:

- Hash table builders
- Custom data indexing systems
- External data processing pipelines
- Performance analysis tools

## API

### Function Signature

```cpp
FShardFileData LoadShardFile(const FString& ShardFilePath);
```

### Parameters

- `ShardFilePath`: Full path to the shard file (e.g., `"C:/Data/Scenarios/my_scenario/my_dataset/shard-0.bin"`)

### Return Value

Returns an `FShardFileData` struct containing:

- `Header`: The shard file header (`FDataBlockHeaderBinary`) with metadata
- `RawData`: Complete file content as a byte array (`TArray<uint8>`)
- `FilePath`: Path to the loaded file
- `bSuccess`: Whether the load was successful
- `ErrorMessage`: Error description if load failed

## Usage Example

```cpp
#include "TrajectoryDataLoader.h"

void LoadAndProcessShard()
{
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    
    // Load a shard file
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
    UE_LOG(LogTemp, Log, TEXT("Trajectory count: %d"), 
        ShardData.Header.TrajectoryEntryCount);
    UE_LOG(LogTemp, Log, TEXT("Time step interval size: %d"), 
        ShardData.Header.TimeStepIntervalSize);
    
    // Access raw data for processing
    const uint8* RawDataPtr = ShardData.RawData.GetData();
    int32 DataSize = ShardData.RawData.Num();
    
    // Pass to external processing component
    ProcessShardData(RawDataPtr, DataSize, ShardData.Header);
}
```

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

```cpp
void ProcessTrajectoryEntries(const FShardFileData& ShardData)
{
    // Calculate entry size
    int32 EntrySizeBytes = 16 + (ShardData.Header.TimeStepIntervalSize * 3 * sizeof(float));
    
    // Get pointer to data section
    const uint8* DataSection = ShardData.RawData.GetData() + 
        ShardData.Header.DataSectionOffset;
    
    // Iterate through all entries
    for (int32 i = 0; i < ShardData.Header.TrajectoryEntryCount; ++i)
    {
        const uint8* EntryPtr = DataSection + (i * EntrySizeBytes);
        
        // Read trajectory ID
        uint64 TrajectoryId;
        FMemory::Memcpy(&TrajectoryId, EntryPtr, sizeof(uint64));
        
        // Read metadata
        int32 StartTimeStepInInterval;
        FMemory::Memcpy(&StartTimeStepInInterval, EntryPtr + 8, sizeof(int32));
        
        int32 ValidSampleCount;
        FMemory::Memcpy(&ValidSampleCount, EntryPtr + 12, sizeof(int32));
        
        // Skip entries with no valid data
        if (StartTimeStepInInterval == -1)
        {
            continue;
        }
        
        // Access position data (starts at offset 16)
        const float* PositionsArray = reinterpret_cast<const float*>(EntryPtr + 16);
        
        // Process positions (NaN values indicate invalid samples)
        for (int32 TimeStep = 0; TimeStep < ShardData.Header.TimeStepIntervalSize; ++TimeStep)
        {
            float X = PositionsArray[TimeStep * 3 + 0];
            float Y = PositionsArray[TimeStep * 3 + 1];
            float Z = PositionsArray[TimeStep * 3 + 2];
            
            // Check for NaN (invalid sample)
            if (!FMath::IsNaN(X))
            {
                // Process valid position (X, Y, Z)
                ProcessPosition(TrajectoryId, TimeStep, FVector(X, Y, Z));
            }
        }
    }
}
```

## Error Handling

The function validates several conditions and returns error information if any fail:

- **Empty path**: `"Shard file path is empty"`
- **File not found**: `"Shard file does not exist: <path>"`
- **Invalid size**: `"Invalid shard file size: <size>"`
- **Too small**: `"Shard file too small (size: <size>, minimum: <min>)"`
- **Open failed**: `"Failed to open shard file: <path>"`
- **Read failed**: `"Failed to read shard file content"`
- **Invalid magic**: `"Invalid shard file format: magic number mismatch"`
- **Unsupported version**: `"Unsupported shard file format version: <version>"`

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

The function loads the **entire file** into memory in a single operation. For large datasets:

- Monitor memory usage when loading multiple shards
- Consider processing shards sequentially rather than all at once
- Free memory by allowing `FShardFileData` to go out of scope when done

Example file sizes (approximate):
- 50 time steps, 1000 trajectories: ~600 KB per shard
- 100 time steps, 10000 trajectories: ~12 MB per shard
- 200 time steps, 50000 trajectories: ~120 MB per shard

## Integration with Other Components

The function is designed for integration with external components:

```cpp
// Example: Hash table builder
class UMyHashTableBuilder
{
public:
    void BuildFromShard(const FShardFileData& ShardData)
    {
        // Use ShardData.RawData for direct memory access
        // Build hash table indexes from trajectory IDs
        // etc.
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
4. Access header fields and raw data array as needed
