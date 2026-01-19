# Niagara Trajectory Visualization with Texture2DArray

## Overview

This guide covers the **Texture2DArray** implementation which provides dynamic texture support. No need to know trajectory count at design time!

### Key Advantages
- ✅ **Single texture parameter** in Niagara (not PositionTexture0, 1, 2, etc.)
- ✅ **Dynamic slice count** determined at runtime
- ✅ **Simpler setup** - one parameter instead of multiple
- ✅ **HLSL indexing** - `Texture2DArraySample(PositionTextureArray, Sampler, UV, SliceIndex)`
- ✅ **Unlimited trajectories** - array grows as needed

## Architecture

### Data Flow

```
LoadedTrajectories (CPU)
    ↓
UTrajectoryTextureProvider
    ↓
Pack into Texture2DArray (multiple slices)
(Slice 0: Trajectories 0-1023)
(Slice 1: Trajectories 1024-2047)
(Slice N: Trajectories N*1024 to end)
    ↓
Upload to GPU as single Texture2DArray
    ↓
Niagara GPU Simulation
    ↓
Custom HLSL: Calculate slice index from trajectory ID
    ↓
Sample from Texture2DArray
    ↓
Ribbon Renderer
    ↓
Trajectory lines on screen
```

### Texture2DArray Layout

**Structure**:
- Single Texture2DArray with multiple slices
- Each slice: Width × 1024 (all slices same dimensions)
- Width = Actual MaxSamplesPerTrajectory (dynamic, based on data)
- Format: PF_FloatRGBA (Float16, 8 bytes per texel)

**Slice Organization**:
```
Slice 0: Trajectories [0, 1023]
Slice 1: Trajectories [1024, 2047]
Slice 2: Trajectories [2048, 3071]
...
Slice N: Trajectories [N*1024, min((N+1)*1024-1, NumTrajectories-1)]
```

**Addressing**:
```
GlobalTrajectoryIndex = Particle.UniqueID / MaxSamplesPerTrajectory
SliceIndex = GlobalTrajectoryIndex / 1024
LocalTrajectoryIndex = GlobalTrajectoryIndex % 1024
UV = (SampleIndex / Width, LocalTrajectoryIndex / 1024)
```

## Step-by-Step Niagara Setup

### Step 1: Load Trajectory Data (Blueprint)

Same as before - load your trajectory data using TrajectoryDataLoader.

### Step 2: Create Texture Provider Component (Blueprint)

```
Create TrajectoryTextureProvider Component
  ↓
Set as variable: "TextureProvider"
  ↓
Call: UpdateFromDataset
  ├─ DatasetIndex: 0 (or your dataset index)
  └─ Returns: True if successful
  ↓
Get Metadata
  └─ Store: NumTrajectories, MaxSamplesPerTrajectory, NumTextureSlices
```

### Step 3: Create Niagara System

1. **Create New Niagara System**:
   - Content Browser → Right Click → Niagara System
   - Select "Empty" template
   - Name: `NS_TrajectoryVisualization`

2. **Set Simulation Target**:
   - Select Emitter
   - Details → Emitter Properties
   - Sim Target: **GPUCompute Sim**
   - Fixed Bounds: Enable and set to cover your data bounds

### Step 4: Add User Parameters (Simplified!)

In the Niagara System, add these User Parameters:

| Name | Type | Description |
|------|------|-------------|
| `PositionTextureArray` | Texture2DArray | Single texture array with all trajectory data |
| `PositionSampler` | Texture Sampler | Sampler for the texture array |
| `NumTrajectories` | int32 | Total number of trajectories |
| `MaxSamplesPerTrajectory` | int32 | Maximum samples per trajectory |
| `MaxTrajectoriesPerSlice` | int32 | Max trajectories per slice (1024) |
| `NumTextureSlices` | int32 | Number of slices in array |
| `AnimationTime` | float | Animation time (0.0 to 1.0) |

**Note**: Only ONE texture parameter needed! Much simpler than before.

### Step 5: Configure Emitter Spawn

**Add Module: Spawn Burst Instantaneous**

Settings:
- Spawn Count: `NumTrajectories * MaxSamplesPerTrajectory`
- Spawn Time: `0.0`

### Step 6: Initialize Particles (Custom HLSL Module)

**Create Custom Module: "Initialize Trajectory Particle"**

Add this HLSL code:

```hlsl
// ----- Initialize Trajectory Particle (Texture2DArray Version) -----
// Module Inputs (User Parameters):
//   - Texture2DArray PositionTextureArray
//   - SamplerState PositionSampler
//   - int32 NumTrajectories
//   - int32 MaxSamplesPerTrajectory
//   - int32 MaxTrajectoriesPerSlice (1024)
//   - int32 NumTextureSlices

// Calculate trajectory and sample indices from particle ID
int GlobalTrajectoryIndex = Particles.UniqueID / MaxSamplesPerTrajectory;
int SampleIndex = Particles.UniqueID % MaxSamplesPerTrajectory;

// Calculate slice index and local trajectory index
int SliceIndex = GlobalTrajectoryIndex / MaxTrajectoriesPerSlice;
int LocalTrajectoryIndex = GlobalTrajectoryIndex % MaxTrajectoriesPerSlice;

// Calculate UV coordinates for texture sampling
float U = (float(SampleIndex) + 0.5) / float(MaxSamplesPerTrajectory);
float V = (float(LocalTrajectoryIndex) + 0.5) / float(MaxTrajectoriesPerSlice);
float2 UV = float2(U, V);

// Sample from Texture2DArray using slice index
float4 TexelData = Texture2DArraySample(PositionTextureArray, PositionSampler, UV, SliceIndex);

// Extract position and time step
float3 Position = TexelData.rgb;
float TimeStep = TexelData.a;

// Check for invalid position (NaN marker)
bool bIsValidPosition = !isnan(Position.x) && !isnan(Position.y) && !isnan(Position.z);

// Set particle attributes
Particles.Position = Position;
Particles.RibbonID = GlobalTrajectoryIndex;
Particles.RibbonLinkOrder = SampleIndex;

// Hide invalid particles
if (!bIsValidPosition)
{
    Particles.Scale = float3(0, 0, 0);
}

// Optional: Color by trajectory using HSV
float Hue = float(GlobalTrajectoryIndex) / float(NumTrajectories);
Particles.Color = float4(HSVtoRGB(float3(Hue, 0.8, 1.0)), 1.0);

// Store trajectory ID
Particles.TrajectoryID = GlobalTrajectoryIndex;
```

**Module Inputs** (map to user parameters):
- `PositionTextureArray` → User.PositionTextureArray
- `PositionSampler` → User.PositionSampler
- `NumTrajectories` → User.NumTrajectories
- `MaxSamplesPerTrajectory` → User.MaxSamplesPerTrajectory
- `MaxTrajectoriesPerSlice` → User.MaxTrajectoriesPerSlice
- `NumTextureSlices` → User.NumTextureSlices

### Step 7: Add Custom Particle Attribute (Optional)

To store trajectory ID:

1. In Emitter → Particle Attributes, click "+"
2. Add new attribute:
   - Name: `TrajectoryID`
   - Type: `int32`
   - Default: `0`

### Step 8: Configure Ribbon Renderer

**Add Renderer: Ribbon Renderer**

Settings:
- **Ribbon Link Order**: `Particles.RibbonLinkOrder`
- **Ribbon ID**: `Particles.RibbonID`
- **Ribbon Width**: `2.0` (adjust as needed)
- **Ribbon Facing**: `Screen Aligned`
- **UV0 Settings**: Distribution Mode: Scaled Uniformly
- **UV1 Settings**: Leading: 0.0, Trailing: 1.0

### Step 9: Connect in Blueprint

In your Actor/Level Blueprint:

```
Event BeginPlay
  ↓
Get TextureProvider Component
Get Niagara Component
  ↓
Get Metadata from TextureProvider
  ↓
Get PositionTextureArray from TextureProvider
  ↓
Set Niagara User Parameters:
  - SetTextureParameter("PositionTextureArray", TextureProvider->GetPositionTextureArray())
  - SetIntParameter("NumTrajectories", Metadata.NumTrajectories)
  - SetIntParameter("MaxSamplesPerTrajectory", Metadata.MaxSamplesPerTrajectory)
  - SetIntParameter("MaxTrajectoriesPerSlice", Metadata.MaxTrajectoriesPerTexture)
  - SetIntParameter("NumTextureSlices", Metadata.NumTextureSlices)
  - SetFloatParameter("AnimationTime", 1.0)
  ↓
Activate Niagara System
```

## Complete HLSL Module Example

```hlsl
// ===== Trajectory Position Sampling Module (Texture2DArray) =====
// GPU-based trajectory visualization with dynamic slice count
// Supports unlimited trajectories via Texture2DArray

// ----- MODULE INPUTS (from User Parameters) -----
// Texture2DArray PositionTextureArray
// SamplerState PositionSampler
// int32 NumTrajectories
// int32 MaxSamplesPerTrajectory
// int32 MaxTrajectoriesPerSlice (1024)
// int32 NumTextureSlices
// float AnimationTime

// ----- HELPER: HSV to RGB -----
float3 HSVtoRGB(float3 HSV)
{
    float3 RGB = 0;
    float C = HSV.z * HSV.y;
    float H = HSV.x * 6.0;
    float X = C * (1.0 - abs(fmod(H, 2.0) - 1.0));
    
    if (HSV.y != 0)
    {
        float m = HSV.z - C;
        if (H < 1.0)      RGB = float3(C, X, 0);
        else if (H < 2.0) RGB = float3(X, C, 0);
        else if (H < 3.0) RGB = float3(0, C, X);
        else if (H < 4.0) RGB = float3(0, X, C);
        else if (H < 5.0) RGB = float3(X, 0, C);
        else              RGB = float3(C, 0, X);
        RGB += m;
    }
    else
    {
        RGB = float3(HSV.z, HSV.z, HSV.z);
    }
    return RGB;
}

// ----- MAIN MODULE CODE -----

// Calculate trajectory and sample indices
int GlobalTrajectoryIndex = Particles.UniqueID / MaxSamplesPerTrajectory;
int SampleIndex = Particles.UniqueID % MaxSamplesPerTrajectory;

// Calculate Texture2DArray addressing
int SliceIndex = GlobalTrajectoryIndex / MaxTrajectoriesPerSlice;
int LocalTrajectoryIndex = GlobalTrajectoryIndex % MaxTrajectoriesPerSlice;

// Calculate UV coordinates
float U = (float(SampleIndex) + 0.5) / float(MaxSamplesPerTrajectory);
float V = (float(LocalTrajectoryIndex) + 0.5) / float(MaxTrajectoriesPerSlice);
float2 UV = float2(U, V);

// Sample from Texture2DArray - single call, no conditional branching!
float4 TexelData = Texture2DArraySample(PositionTextureArray, PositionSampler, UV, SliceIndex);

// Extract data
float3 Position = TexelData.rgb;
float TimeStep = TexelData.a;

// Check for invalid position
bool bIsValidPosition = !isnan(Position.x) && !isnan(Position.y) && !isnan(Position.z);

// Set particle attributes
Particles.Position = Position;
Particles.RibbonID = GlobalTrajectoryIndex;
Particles.RibbonLinkOrder = SampleIndex;

// Hide invalid particles
if (!bIsValidPosition)
{
    Particles.Scale = float3(0, 0, 0);
}

// Color by trajectory
float Hue = float(GlobalTrajectoryIndex) / float(NumTrajectories);
Particles.Color = float4(HSVtoRGB(float3(Hue, 0.8, 1.0)), 1.0);

// Store trajectory ID
Particles.TrajectoryID = GlobalTrajectoryIndex;
```

## Comparison: Texture2DArray vs Multiple Texture2D

### Multiple Texture2D (Old Approach)
```hlsl
// Need conditional branching for each texture
if (TextureIndex == 0) TexelData = Texture2DSample(PositionTexture0, ...);
else if (TextureIndex == 1) TexelData = Texture2DSample(PositionTexture1, ...);
else if (TextureIndex == 2) TexelData = Texture2DSample(PositionTexture2, ...);
// ... up to max textures
```

**Issues**:
- Must define max textures at design time
- Multiple parameters in Niagara
- Conditional branching in HLSL (slower)
- Manual binding for each texture

### Texture2DArray (New Approach)
```hlsl
// Single call with dynamic index
float4 TexelData = Texture2DArraySample(PositionTextureArray, Sampler, UV, SliceIndex);
```

**Benefits**:
- One parameter in Niagara
- No conditional branching (faster)
- Dynamic slice count at runtime
- Cleaner, simpler code

## Performance

### Memory Usage

Same as before - based on actual trajectory data:

```
Per slice: Width × 1024 × 8 bytes
Example: 512 × 1024 × 8 = 4 MB per slice

For 5000 trajectories:
- Slices needed: 5 (ceil(5000/1024))
- Total memory: 5 × 4 MB = 20 MB (with 512-sample width)
```

### GPU Performance

**Improved**:
- No conditional branching (faster than multi-texture approach)
- Better texture cache utilization
- Hardware-optimized array indexing

## Troubleshooting

### Issue: Texture2DArray parameter not showing in Niagara

**Solution**: Ensure UE5+ and proper module setup. Texture2DArray is supported in UE5 Niagara.

### Issue: Invalid positions everywhere

**Cause**: Slice index calculation incorrect
**Solution**: Verify `MaxTrajectoriesPerSlice = 1024` and `SliceIndex = GlobalTrajectoryIndex / 1024`

### Issue: Lines disconnected

**Cause**: Ribbon ID or Link Order incorrect
**Solution**: Verify `RibbonID = GlobalTrajectoryIndex` and `RibbonLinkOrder = SampleIndex`

## Summary

The Texture2DArray approach provides:

✅ **Dynamic Support** - No design-time texture count limit
✅ **Simpler Setup** - One texture parameter instead of many
✅ **Better Performance** - No conditional branching
✅ **Cleaner Code** - Single Texture2DArraySample call
✅ **Version Independent** - Standard texture API (no NDI)

This is the recommended approach for trajectory visualization in Niagara!
