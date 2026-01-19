# Niagara Integration Proposal for Trajectory Visualization

## Overview

This document proposes a method for integrating trajectory data with Unreal Engine's Niagara particle system to enable visualization of loaded trajectories as lines.

## Current State

The plugin currently supports:
- Loading trajectory data from binary files
- Storing position data as arrays of FVector (FTrajectoryPositionSample)
- Managing multiple datasets and trajectories
- Memory-efficient data loading with configurable parameters

## Proposed Integration Method

### Approach: Niagara Data Interface (NDI)

We propose implementing a **Niagara Data Interface** as the primary method for exposing trajectory data to Niagara systems. This approach offers:

1. **Efficient Data Access**: Direct access to trajectory data from Niagara modules
2. **GPU-Friendly**: Can be optimized for GPU readbacks or structured buffers
3. **Extensible**: Easy to add more attributes (velocity, acceleration, etc.) in future iterations
4. **Standard UE Pattern**: Follows Unreal Engine's recommended approach for custom data in Niagara

### Architecture Components

#### 1. Niagara Data Interface (C++)
**Class**: `UNiagaraDataInterfaceTrajectoryData`

**Purpose**: Bridge between trajectory data loader and Niagara systems

**Key Functions**:
- `GetNumTrajectories()` - Returns number of loaded trajectories
- `GetTrajectoryNumSamples(int32 TrajectoryIndex)` - Returns sample count for a trajectory
- `GetTrajectoryPosition(int32 TrajectoryIndex, int32 SampleIndex)` - Returns position at index
- `GetTrajectoryTimeStep(int32 TrajectoryIndex, int32 SampleIndex)` - Returns time step at index
- `GetTrajectoryStartTime(int32 TrajectoryIndex)` - Returns start time step
- `GetTrajectoryEndTime(int32 TrajectoryIndex)` - Returns end time step
- `GetTrajectoryExtent(int32 TrajectoryIndex)` - Returns object extent
- `GetTrajectoryId(int32 TrajectoryIndex)` - Returns trajectory ID

**Data Source**: References `UTrajectoryDataLoader` singleton to access loaded data

#### 2. Data Interface Provider Component (C++)
**Class**: `UTrajectoryDataProviderComponent` (Actor Component)

**Purpose**: Allows selecting which dataset to visualize in the Niagara system

**Key Properties**:
- `FString DatasetName` - Which dataset to visualize
- `int32 DatasetIndex` - Which loaded dataset index to use
- `bool bAutoRefresh` - Auto-update when data changes

**Usage**: Attach to an actor that hosts the Niagara system, configure which dataset to visualize

#### 3. Niagara Module Scripts (HLSL/Simulation)

**Module**: `Generate Trajectory Particles`
- Generates particles for each trajectory sample point
- Outputs: Position, TrajectoryID, SampleIndex, TimeStep

**Module**: `Generate Trajectory Lines`
- Generates particle ribbons/lines following trajectory paths
- Uses Niagara Ribbon Renderer for line visualization
- Outputs: Position, RibbonID, RibbonWidth

## Visualization Strategy

### Method 1: Line/Ribbon Rendering (Recommended)

**Niagara System Setup**:
1. **Emitter**: Trajectory Line Emitter
   - Spawn particles for each trajectory sample
   - Use Ribbon Renderer to connect particles into lines
   - Particle lifetime: Persistent (or based on time step)

2. **Data Flow**:
   ```
   Trajectory Data Interface
   → Read trajectory positions
   → Spawn particles at each position
   → Ribbon renderer connects sequential particles
   → Result: Continuous lines following trajectories
   ```

3. **Advantages**:
   - Native Niagara ribbon system handles line rendering efficiently
   - Automatic line smoothing and width control
   - Easy to add colors, fade effects, and animations
   - Good performance for thousands of trajectories

### Method 2: Mesh Rendering (Alternative)

**For Future Consideration**:
- Spawn mesh particles (spheres/cylinders) at trajectory points
- More expensive but allows for complex per-point visualization
- Useful for showing trajectory metadata (extent, velocity vectors)

## Implementation Details

### C++ Data Interface Implementation

```cpp
// UNiagaraDataInterfaceTrajectoryData.h
UCLASS(EditInlineNew, Category = "Trajectory Data", meta = (DisplayName = "Trajectory Data"))
class TRAJECTORYDATA_API UNiagaraDataInterfaceTrajectoryData : public UNiagaraDataInterface
{
    GENERATED_BODY()

public:
    // Which loaded dataset to use (by index in LoadedDatasets array)
    UPROPERTY(EditAnywhere, Category = "Trajectory Data")
    int32 DatasetIndex = 0;

    // Override UNiagaraDataInterface methods
    virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
    
    // VM functions (callable from Niagara)
    void GetNumTrajectories(FVectorVMExternalFunctionContext& Context);
    void GetTrajectoryNumSamples(FVectorVMExternalFunctionContext& Context);
    void GetTrajectoryPosition(FVectorVMExternalFunctionContext& Context);
    void GetTrajectoryTimeStep(FVectorVMExternalFunctionContext& Context);
    void GetTrajectoryStartTime(FVectorVMExternalFunctionContext& Context);
    void GetTrajectoryEndTime(FVectorVMExternalFunctionContext& Context);
    
private:
    // Cached reference to loader
    UTrajectoryDataLoader* GetLoader() const;
};
```

### Niagara System Setup Guide

#### Step 1: Create Niagara System
1. Create new Niagara System (Content Browser → Niagara System)
2. Choose "Empty" template

#### Step 2: Add Emitter
1. Add new emitter to system
2. Configure emitter properties:
   - Emitter State: Active
   - Spawn Rate: 0 (we'll spawn programmatically)

#### Step 3: Add Trajectory Data Interface
1. In System parameters, add User Parameter
2. Type: Niagara Data Interface → Trajectory Data
3. Name: "TrajectoryDataSource"

#### Step 4: Configure Spawn Module
1. Add module: "Spawn Burst Instantaneous"
2. Set Spawn Count = `TrajectoryDataSource.GetNumTrajectories()`
3. This spawns one particle per trajectory

#### Step 5: Configure Particle Update Module
1. Add custom module or script
2. Read positions from data interface:
   ```
   int TrajectoryIndex = Particles.UniqueID;
   int NumSamples = TrajectoryDataSource.GetTrajectoryNumSamples(TrajectoryIndex);
   
   // Calculate which sample to show based on time
   float NormalizedTime = (Engine.Time - StartTime) / Duration;
   int SampleIndex = floor(NormalizedTime * NumSamples);
   
   // Get position
   Particles.Position = TrajectoryDataSource.GetTrajectoryPosition(TrajectoryIndex, SampleIndex);
   ```

#### Step 6: Add Ribbon Renderer
1. Add "Ribbon Renderer" to emitter
2. Configure:
   - Ribbon Width: 5-10 units
   - Material: Default sprite material or custom line material
   - UV Mode: Normalized Age

#### Step 7: Connect Data in Blueprint
1. Place Niagara System in level
2. Set "TrajectoryDataSource" parameter to reference the data interface
3. The data interface automatically pulls from loaded datasets

## Data Flow Diagram

```
[Trajectory Data Loader]
         ↓
[Loaded Datasets Array]
         ↓
[Niagara Data Interface] ← DatasetIndex selection
         ↓
[Niagara System]
    ↓         ↓
[Emitter]  [Renderer]
    ↓         ↓
[Particles] [Lines/Ribbons]
```

## Performance Considerations

### Memory
- Data Interface doesn't copy data (references existing loaded data)
- Each trajectory spawns particles for visible samples only
- Memory usage scales with: NumTrajectories × NumVisibleSamples × ParticleSize

### CPU/GPU
- Data Interface functions execute per-particle per-frame
- For static trajectories, consider baking to texture/buffer
- Optimize by:
  - Reducing sample rate when loading data
  - Culling trajectories outside view frustum
  - LOD based on camera distance

### Scalability
- **Small datasets** (< 1000 trajectories): Direct particle spawning works well
- **Medium datasets** (1000-10000): Use LOD, culling, sample rate reduction
- **Large datasets** (> 10000): Consider GPU-driven rendering, instance rendering

## Future Extensibility

The proposed architecture easily supports adding more attributes:

### Phase 2 Attributes
- Velocity vectors (for motion blur, flow visualization)
- Acceleration (for force field visualization)
- Custom metadata (color coding, selection state)

### Implementation
Simply add new functions to the Data Interface:
```cpp
void GetTrajectoryVelocity(FVectorVMExternalFunctionContext& Context);
void GetTrajectoryAcceleration(FVectorVMExternalFunctionContext& Context);
void GetTrajectoryColor(FVectorVMExternalFunctionContext& Context);
```

Update data structures to include these attributes when loading.

## Alternative Approaches Considered

### 1. Blueprint-Only Approach
**Pros**: No C++ required
**Cons**: 
- Poor performance for large datasets
- Limited GPU access
- Difficult to optimize

### 2. Direct Texture Upload
**Pros**: Fastest GPU access
**Cons**: 
- Complex implementation
- Limited to texture size constraints
- Less flexible for queries

### 3. Actor-Based Spawning
**Pros**: Simple to implement
**Cons**: 
- Not using Niagara's strengths
- Poor performance
- No particle effects support

## Recommendation

Proceed with **Niagara Data Interface approach** for the following reasons:

1. **Standard UE Pattern**: Uses Unreal's recommended method for custom Niagara data
2. **Performance**: Efficient access with potential for GPU optimization
3. **Flexibility**: Easy to extend with more attributes
4. **Maintainability**: Clean separation between data loading and visualization
5. **User-Friendly**: Standard Niagara workflow for artists/designers

## Implementation Phases

### Phase 1: Core Data Interface (This Iteration)
- Implement `UNiagaraDataInterfaceTrajectoryData`
- Expose position data only
- Basic VM functions for trajectory queries
- Simple example Niagara system

### Phase 2: Enhanced Features (Future)
- Add velocity, acceleration attributes
- GPU buffer optimization
- Advanced rendering options
- Performance profiling tools

### Phase 3: Artist Tools (Future)
- Blueprint helper functions
- UI for dataset selection
- Visualization presets
- Material library for trajectories

## Required Files to Implement

### New C++ Files
1. `TrajectoryDataNiagaraDataInterface.h`
2. `TrajectoryDataNiagaraDataInterface.cpp`

### Modified Files
1. `TrajectoryData.Build.cs` - Add Niagara module dependencies
2. `README.md` - Add Niagara integration documentation

### New Content Files
1. `Content/Niagara/NE_TrajectoryLines.uasset` - Example Niagara Emitter
2. `Content/Niagara/NS_TrajectoryVisualization.uasset` - Example Niagara System
3. `Content/Materials/M_TrajectoryLine.uasset` - Example line material

### New Documentation
1. `NIAGARA_SETUP_GUIDE.md` - Detailed setup instructions
2. `examples/NIAGARA_USAGE_EXAMPLE.md` - Blueprint examples

## Questions for Approval

1. **Approve overall approach?** Is the Niagara Data Interface method acceptable?
2. **Scope for first iteration?** Is position-only data sufficient, or add more attributes now?
3. **Example content?** Should we include example Niagara systems and materials?
4. **Documentation depth?** How detailed should the setup guide be?
5. **Performance targets?** Any specific performance requirements for trajectory count/sample count?

## Next Steps (After Approval)

1. Update `TrajectoryData.Build.cs` to add Niagara dependencies
2. Implement `UNiagaraDataInterfaceTrajectoryData` class
3. Test with small dataset
4. Create example Niagara system
5. Document setup process
6. Add usage examples

---

**Awaiting approval to proceed with implementation.**
