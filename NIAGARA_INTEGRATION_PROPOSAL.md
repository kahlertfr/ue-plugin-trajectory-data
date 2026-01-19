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

### Approach: Texture-Based Data Transfer with Direct HLSL

We propose using **texture-based data transfer** with direct HLSL code in Niagara custom modules. This approach offers:

1. **Version Independent**: No dependency on Niagara Data Interface API that changes between UE versions
2. **GPU-Optimized**: Trajectory data directly accessible in GPU texture memory
3. **Maximum Performance**: Efficient texture sampling, no CPU-GPU sync overhead
4. **Direct Control**: Complete HLSL code provided for copy-paste into Niagara modules
5. **Extensible**: Additional channels for velocity, acceleration in future iterations

### Architecture Components

#### 1. Texture Packing Strategy

**Trajectory Position Texture (2D)**
- Format: `RGBA32F` (4 × 32-bit float channels)
- Layout: Row = Trajectory, Column = Time Sample
- Channels:
  - **R**: Position X (float)
  - **G**: Position Y (float)  
  - **B**: Position Z (float)
  - **A**: Time Step (float) or metadata

**Texture Dimensions**:
```
Width = MaxSamplesPerTrajectory (e.g., 2048)
Height = NumTrajectories (e.g., 1024)
Max Capacity = 2048 × 1024 = ~2M samples
```

**Memory Calculation**:
```
MemoryPerSample = 4 channels × 4 bytes = 16 bytes
TotalMemory = Width × Height × 16 bytes
Example: 2048 × 1024 × 16 = 32 MB
```

#### 2. C++ Texture Provider Component

**Class**: `UTrajectoryTextureProvider` (Actor Component)

**Purpose**: Converts loaded trajectory data into GPU textures

**Key Functions**:
```cpp
// Update texture from loaded dataset
void UpdateFromDataset(int32 DatasetIndex);

// Get the trajectory position texture
UTexture2D* GetPositionTexture() const;

// Get metadata for indexing
FTrajectoryTextureMetadata GetMetadata() const;
```

**Texture Update Process**:
1. Access loaded dataset from `UTrajectoryDataLoader`
2. Pack trajectory positions into texture buffer
3. Update GPU texture resource
4. Expose texture as Niagara user parameter

#### 3. Niagara Custom HLSL Modules

Direct HLSL code to sample trajectory data from textures (see HLSL Code section below).

## Data Packing Format

### Position Texture Layout

```
       Sample0   Sample1   Sample2   ...   SampleN
Traj0  [XYZ0]   [XYZ1]    [XYZ2]          [XYZN]
Traj1  [XYZ0]   [XYZ1]    [XYZ2]          [XYZN]
Traj2  [XYZ0]   [XYZ1]    [XYZ2]          [XYZN]
...
TrajM  [XYZ0]   [XYZ1]    [XYZ2]          [XYZN]
```

Each texel stores: `(PosX, PosY, PosZ, TimeStep)` in RGBA channels

### Metadata Structure

Passed as Niagara user parameters:
```cpp
struct FTrajectoryTextureMetadata
{
    int32 NumTrajectories;           // Height of texture
    int32 MaxSamplesPerTrajectory;   // Width of texture
    FVector BoundsMin;               // Dataset bounding box
    FVector BoundsMax;
    int32 FirstTimeStep;
    int32 LastTimeStep;
};
```


## HLSL Code for Niagara Custom Modules

### Module 1: Sample Trajectory Position

**Purpose**: Sample position from trajectory texture

**Copy-Paste HLSL Code**:
```hlsl
// ----- Niagara Module: SampleTrajectoryPosition -----
// Module Inputs:
//   - Texture2D PositionTexture (User Parameter)
//   - SamplerState PositionTextureSampler (User Parameter)
//   - int TrajectoryIndex (Particle attribute or parameter)
//   - int SampleIndex (Particle attribute or parameter)
//   - int NumTrajectories (User Parameter)
//   - int MaxSamplesPerTrajectory (User Parameter)

// Calculate UV coordinates for texture sampling
float U = (float(SampleIndex) + 0.5) / float(MaxSamplesPerTrajectory);
float V = (float(TrajectoryIndex) + 0.5) / float(NumTrajectories);

// Sample the texture
float4 TexelData = Texture2DSample(PositionTexture, PositionTextureSampler, float2(U, V));

// Extract position (XYZ in RGB channels)
float3 Position = TexelData.rgb;

// Extract time step (stored in Alpha channel)
float TimeStep = TexelData.a;

// Output to particle attributes
Particles.Position = Position;
Particles.TimeStep = TimeStep;
```

### Module 2: Spawn Trajectory Ribbon Particles

**Purpose**: Initialize particles for ribbon rendering

**Copy-Paste HLSL Code**:
```hlsl
// ----- Niagara Module: SpawnTrajectoryRibbonParticles -----
// Module Inputs:
//   - Texture2D PositionTexture (User Parameter)
//   - SamplerState PositionTextureSampler (User Parameter)
//   - int NumTrajectories (User Parameter)
//   - int MaxSamplesPerTrajectory (User Parameter)
//   - float NormalizedAge (Built-in)

// Calculate which trajectory and sample this particle represents
int TrajectoryIndex = Particles.UniqueID;  // One particle stream per trajectory
int SampleIndex = floor(NormalizedAge * float(MaxSamplesPerTrajectory - 1));

// Calculate UV
float U = (float(SampleIndex) + 0.5) / float(MaxSamplesPerTrajectory);
float V = (float(TrajectoryIndex) + 0.5) / float(NumTrajectories);

// Sample position
float4 TexelData = Texture2DSample(PositionTexture, PositionTextureSampler, float2(U, V));
Particles.Position = TexelData.rgb;

// Store trajectory ID for ribbon identification
Particles.RibbonID = TrajectoryIndex;
Particles.RibbonLinkOrder = SampleIndex;

// Optional: Color by trajectory
float3 Color = HSVtoRGB(float3(float(TrajectoryIndex) / float(NumTrajectories), 0.8, 1.0));
Particles.Color = float4(Color, 1.0);
```

### Module 3: Update Trajectory Animation

**Purpose**: Animate trajectory visualization over time

**Copy-Paste HLSL Code**:
```hlsl
// ----- Niagara Module: UpdateTrajectoryAnimation -----
// Module Inputs:
//   - Texture2D PositionTexture (User Parameter)
//   - SamplerState PositionTextureSampler (User Parameter)
//   - int TrajectoryIndex (Particle attribute)
//   - int NumTrajectories (User Parameter)
//   - int MaxSamplesPerTrajectory (User Parameter)
//   - float AnimationTime (User Parameter - range 0 to 1)
//   - float EngineTime (Built-in)

// Calculate sample index based on animation time
int SampleIndex = floor(AnimationTime * float(MaxSamplesPerTrajectory - 1));

// Calculate UV
float U = (float(SampleIndex) + 0.5) / float(MaxSamplesPerTrajectory);
float V = (float(TrajectoryIndex) + 0.5) / float(NumTrajectories);

// Sample new position
float4 TexelData = Texture2DSample(PositionTexture, PositionTextureSampler, float2(U, V));
Particles.Position = TexelData.rgb;

// Update age for ribbon ordering
Particles.NormalizedAge = AnimationTime;
```

### Helper Function: HSV to RGB

**Include this in custom modules if using color coding**:
```hlsl
// ----- Helper: HSV to RGB Conversion -----
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
```

## Visualization Strategy: Ribbon Rendering

### Niagara System Setup

**Approach**: Use Niagara Ribbon Renderer with texture-sampled positions

**Data Flow**:
```
Loaded Trajectories (CPU)
    ↓
Pack into RGBA32F Texture (C++)
    ↓
Upload to GPU as UTexture2D
    ↓
Pass as User Parameter to Niagara
    ↓
Sample in Custom HLSL Module
    ↓
Ribbon Renderer draws lines
```

### Rendering Methods

#### Method 1: Static Trajectory Lines (Best Performance)

**Use Case**: Display all trajectories as static lines

**Setup**:
1. Spawn burst: N particles (one per trajectory)
2. Each particle represents one trajectory line
3. Use Ribbon Renderer with ribbon ID per trajectory
4. Particles sample texture sequentially along ribbon

**Particle Count**: `NumTrajectories × SamplesPerTrajectory`

**Pros**: All trajectories visible at once, simple setup
**Cons**: High particle count for many samples

#### Method 2: Animated Trajectory Growth

**Use Case**: Animate trajectories drawing over time

**Setup**:
1. Spawn burst: N particles per trajectory
2. Control visible samples via AnimationTime parameter (0-1)
3. Particles fade in/out based on time
4. Ribbon grows as animation progresses

**Particle Count**: `NumTrajectories × VisibleSamples`

**Pros**: Cinematic effect, lower particle count
**Cons**: Requires animation control logic

#### Method 3: Time-Slice Visualization

**Use Case**: Show trajectory state at specific time step

**Setup**:
1. Spawn burst: N particles (one per trajectory)
2. Sample texture at specific time index
3. Render as points or short line segments
4. Update sample index to scrub through time

**Particle Count**: `NumTrajectories`

**Pros**: Minimal particles, fast updates
**Cons**: Only shows one time slice

## Detailed Niagara System Setup Guide

### Step 1: Create Texture Provider Component (C++)

First, implement the C++ component that converts trajectory data to textures.

**In your level/actor Blueprint**:

```cpp
// Blueprint pseudo-code
Event BeginPlay
  ↓
Create TrajectoryTextureProvider Component
  ↓
Set DatasetIndex = 0 (or desired dataset)
  ↓
Call UpdateFromDataset()
  ↓
Get PositionTexture
  ↓
Pass to Niagara System as User Parameter
```

### Step 2: Create Niagara System

1. **Create New Niagara System**
   - Content Browser → Right Click → Niagara System
   - Choose "Empty" template
   - Name: `NS_TrajectoryVisualization`

2. **Add Required User Parameters**
   - Click "+" next to User Parameters
   - Add parameters:
     ```
     PositionTexture (Texture2D)
     PositionTextureSampler (Texture Sampler)
     NumTrajectories (int32)
     MaxSamplesPerTrajectory (int32)
     AnimationTime (float) - range 0 to 1
     ```

3. **Create Emitter**
   - Add new emitter: `TrajectoryLines`
   - Emitter Properties:
     - Simulation Target: CPU Simulation
     - Calculate Bounds Mode: Dynamic

### Step 3: Configure Emitter Spawn

**Add Module: Spawn Burst Instantaneous**
```
Spawn Count = NumTrajectories * MaxSamplesPerTrajectory
Spawn Time = 0.0
```

**Alternative for Ribbon-per-Trajectory**:
```
Spawn Count = NumTrajectories
Spawn Time = 0.0
```

### Step 4: Configure Particle Initialization

**Add Module: Initialize Particle**
- Lifetime Mode: Infinite
- Mass: 1.0

**Add Custom Module: Initialize Trajectory Data**

Create new Custom HLSL script and paste:

```hlsl
// Calculate trajectory index and sample index from UniqueID
int TotalSamplesPerTrajectory = MaxSamplesPerTrajectory;
int TrajectoryIndex = Particles.UniqueID / TotalSamplesPerTrajectory;
int SampleIndex = Particles.UniqueID % TotalSamplesPerTrajectory;

// Sample position from texture
float U = (float(SampleIndex) + 0.5) / float(MaxSamplesPerTrajectory);
float V = (float(TrajectoryIndex) + 0.5) / float(NumTrajectories);
float4 TexelData = Texture2DSample(PositionTexture, PositionTextureSampler, float2(U, V));

// Set particle attributes
Particles.Position = TexelData.rgb;
Particles.RibbonID = TrajectoryIndex;
Particles.RibbonLinkOrder = SampleIndex;

// Optional: Color by trajectory using HSV
float Hue = float(TrajectoryIndex) / float(NumTrajectories);
Particles.Color = float4(HSVtoRGB(float3(Hue, 0.8, 1.0)), 1.0);
```

### Step 5: Add Ribbon Renderer

**Add Renderer: Ribbon Renderer**

**Settings**:
- Ribbon Link Order: `Particles.RibbonLinkOrder`
- Ribbon ID: `Particles.RibbonID`
- Ribbon Width: 2.0 (adjust as needed)
- Ribbon Facing: Screen Aligned
- UV0 Mode: Normalized Age
- UV1 Mode: Normalized Link Order

**Material**:
- Use default ribbon material or create custom
- For custom: Use Particle Color for trajectory colors

### Step 6: Connect in Blueprint

**In Actor/Level Blueprint**:
```
Event BeginPlay
  ↓
Load Trajectory Data (using TrajectoryDataLoader)
  ↓
Create TrajectoryTextureProvider Component
  ↓
Call UpdateFromDataset(DatasetIndex)
  ↓
Get Niagara Component Reference
  ↓
Set User Parameters:
  - SetTextureParameter("PositionTexture", Provider->GetPositionTexture())
  - SetIntParameter("NumTrajectories", Metadata.NumTrajectories)
  - SetIntParameter("MaxSamplesPerTrajectory", Metadata.MaxSamplesPerTrajectory)
  - SetFloatParameter("AnimationTime", 1.0)  // Full trajectory
  ↓
Activate Niagara System
```

### Step 7: Optional Animation

**For animated trajectory growth**:

Add Timeline or Event Tick:
```
Every Frame
  ↓
Calculate AnimationTime (0 to 1 over desired duration)
  ↓
NiagaraComponent->SetFloatParameter("AnimationTime", AnimationTime)
  ↓
Update visible portion of trajectories
```

## C++ Implementation Details

### TrajectoryTextureProvider Component

**Header File**: `TrajectoryTextureProvider.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Texture2D.h"
#include "TrajectoryDataStructures.h"
#include "TrajectoryTextureProvider.generated.h"

/**
 * Metadata for trajectory texture
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryTextureMetadata
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    int32 NumTrajectories = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    int32 MaxSamplesPerTrajectory = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    FVector BoundsMin = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    FVector BoundsMax = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    int32 FirstTimeStep = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    int32 LastTimeStep = 0;
};

/**
 * Component that converts trajectory data into GPU textures for Niagara
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TRAJECTORYDATA_API UTrajectoryTextureProvider : public UActorComponent
{
    GENERATED_BODY()

public:
    UTrajectoryTextureProvider();

    /**
     * Update texture from a loaded dataset
     * @param DatasetIndex Index into LoadedDatasets array
     * @return True if successful
     */
    UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
    bool UpdateFromDataset(int32 DatasetIndex);

    /**
     * Get the position texture
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
    UTexture2D* GetPositionTexture() const { return PositionTexture; }

    /**
     * Get texture metadata
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
    FTrajectoryTextureMetadata GetMetadata() const { return Metadata; }

protected:
    /** Position texture (RGBA32F) */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    UTexture2D* PositionTexture;

    /** Metadata for current dataset */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
    FTrajectoryTextureMetadata Metadata;

private:
    /** Pack trajectory data into texture buffer */
    void PackTrajectories(const FLoadedDataset& Dataset, TArray<FFloat16Color>& OutTextureData);
    
    /** Create or update texture resource */
    void UpdateTextureResource(const TArray<FFloat16Color>& TextureData, int32 Width, int32 Height);
};
```

**Implementation File**: `TrajectoryTextureProvider.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryTextureProvider.h"
#include "TrajectoryDataLoader.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

UTrajectoryTextureProvider::UTrajectoryTextureProvider()
{
    PrimaryComponentTick.bCanEverTick = false;
    PositionTexture = nullptr;
}

bool UTrajectoryTextureProvider::UpdateFromDataset(int32 DatasetIndex)
{
    UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
    if (!Loader)
    {
        UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: Failed to get loader"));
        return false;
    }

    const TArray<FLoadedDataset>& Datasets = Loader->GetLoadedDatasets();
    if (!Datasets.IsValidIndex(DatasetIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: Invalid dataset index %d"), DatasetIndex);
        return false;
    }

    const FLoadedDataset& Dataset = Datasets[DatasetIndex];
    
    // Find maximum samples across all trajectories
    int32 MaxSamples = 0;
    for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
    {
        MaxSamples = FMath::Max(MaxSamples, Traj.Samples.Num());
    }

    if (MaxSamples == 0 || Dataset.Trajectories.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: No trajectory data"));
        return false;
    }

    // Update metadata
    Metadata.NumTrajectories = Dataset.Trajectories.Num();
    Metadata.MaxSamplesPerTrajectory = MaxSamples;
    Metadata.BoundsMin = Dataset.DatasetInfo.Metadata.BoundingBoxMin;
    Metadata.BoundsMax = Dataset.DatasetInfo.Metadata.BoundingBoxMax;
    Metadata.FirstTimeStep = Dataset.DatasetInfo.Metadata.FirstTimeStep;
    Metadata.LastTimeStep = Dataset.DatasetInfo.Metadata.LastTimeStep;

    // Pack data into texture buffer
    TArray<FFloat16Color> TextureData;
    PackTrajectories(Dataset, TextureData);

    // Update texture resource
    UpdateTextureResource(TextureData, MaxSamples, Dataset.Trajectories.Num());

    UE_LOG(LogTemp, Log, TEXT("TrajectoryTextureProvider: Updated texture %dx%d for %d trajectories"),
        MaxSamples, Dataset.Trajectories.Num(), Dataset.Trajectories.Num());

    return true;
}

void UTrajectoryTextureProvider::PackTrajectories(const FLoadedDataset& Dataset, TArray<FFloat16Color>& OutTextureData)
{
    int32 Width = Metadata.MaxSamplesPerTrajectory;
    int32 Height = Metadata.NumTrajectories;
    
    // Initialize texture data (RGBA = XYZ + TimeStep)
    OutTextureData.SetNum(Width * Height);

    for (int32 TrajIdx = 0; TrajIdx < Dataset.Trajectories.Num(); ++TrajIdx)
    {
        const FLoadedTrajectory& Traj = Dataset.Trajectories[TrajIdx];
        
        for (int32 SampleIdx = 0; SampleIdx < Width; ++SampleIdx)
        {
            int32 TexelIndex = TrajIdx * Width + SampleIdx;
            
            if (SampleIdx < Traj.Samples.Num())
            {
                const FTrajectoryPositionSample& Sample = Traj.Samples[SampleIdx];
                FVector Pos = Sample.Position;
                float TimeStep = static_cast<float>(Traj.StartTimeStep + SampleIdx);
                
                // Pack into Float16 RGBA
                OutTextureData[TexelIndex] = FFloat16Color(
                    FFloat16(Pos.X),
                    FFloat16(Pos.Y),
                    FFloat16(Pos.Z),
                    FFloat16(TimeStep)
                );
            }
            else
            {
                // Pad with zeros for trajectories with fewer samples
                OutTextureData[TexelIndex] = FFloat16Color(
                    FFloat16(0.0f),
                    FFloat16(0.0f),
                    FFloat16(0.0f),
                    FFloat16(0.0f)
                );
            }
        }
    }
}

void UTrajectoryTextureProvider::UpdateTextureResource(const TArray<FFloat16Color>& TextureData, int32 Width, int32 Height)
{
    if (!PositionTexture || PositionTexture->GetSizeX() != Width || PositionTexture->GetSizeY() != Height)
    {
        // Create new texture
        PositionTexture = UTexture2D::CreateTransient(Width, Height, PF_FloatRGBA);
        PositionTexture->CompressionSettings = TC_HDR;
        PositionTexture->SRGB = 0;
        PositionTexture->Filter = TF_Nearest;  // No filtering for exact data
        PositionTexture->AddressX = TA_Clamp;
        PositionTexture->AddressY = TA_Clamp;
    }

    // Update texture data
    FTexture2DMipMap& Mip = PositionTexture->GetPlatformData()->Mips[0];
    void* TextureDataPtr = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureDataPtr, TextureData.GetData(), TextureData.Num() * sizeof(FFloat16Color));
    Mip.BulkData.Unlock();
    
    PositionTexture->UpdateResource();
}
```

## Data Flow Diagram

```
[UTrajectoryDataLoader]
         ↓
  [LoadedDatasets Array]
         ↓
[UTrajectoryTextureProvider] ← UpdateFromDataset(DatasetIndex)
         ↓
  [Pack to RGBA32F Texture]
         ↓
  [UTexture2D: PositionTexture]
         ↓
  [Niagara User Parameter]
         ↓
  [Custom HLSL Module]
    (Texture2DSample)
         ↓
  [Particle Position]
         ↓
  [Ribbon Renderer]
         ↓
  [Trajectory Lines on Screen]
```

## Performance Considerations

### Memory Usage

**Texture Memory**:
```
Bytes per texel = 4 channels × 2 bytes (Float16) = 8 bytes
Total memory = Width × Height × 8 bytes

Examples:
- 1000 trajectories × 500 samples = 4 MB
- 2000 trajectories × 1024 samples = 16 MB
- 10000 trajectories × 2048 samples = 160 MB
```

**Particle Memory**:
```
Per particle: ~128 bytes (varies by attributes)
Total = NumTrajectories × SamplesPerTrajectory × 128 bytes

Examples:
- 1000 × 500 = 500K particles = ~64 MB
- 2000 × 1024 = 2M particles = ~256 MB
```

### Scalability Guidelines

| Trajectory Count | Samples/Traj | Texture Size | Particle Count | Performance |
|-----------------|--------------|--------------|----------------|-------------|
| 100-500         | 100-500      | 512x512      | 50K-250K       | Excellent   |
| 500-2000        | 500-1000     | 1024x2048    | 250K-2M        | Good        |
| 2000-5000       | 1000-2048    | 2048x5000    | 2M-10M         | Medium      |
| 5000+           | 2048+        | 4096x5000+   | 10M+           | Use LOD     |

### Optimization Strategies

1. **Texture Format**:
   - Use `PF_FloatRGBA` (Float16) for position data (8 bytes/texel)
   - Alternative: `PF_A32B32G32R32F` (Float32) for higher precision (16 bytes/texel)
   - Consider lossy compression for very large datasets

2. **Particle Reduction**:
   - Use sample rate when loading data (load every Nth sample)
   - Implement LOD: Fewer particles for distant trajectories
   - Frustum culling: Only spawn particles for visible trajectories

3. **GPU Optimization**:
   - Use nearest-neighbor filtering (no interpolation overhead)
   - Clamp texture addressing (avoid wrap artifacts)
   - Batch texture updates (don't update every frame)

4. **Streaming**:
   - Load subsets of trajectories based on view frustum
   - Update texture only when dataset changes
   - Use texture streaming for very large datasets

### CPU/GPU Performance

**Texture Sampling (GPU)**:
- Texture reads are extremely fast (cached)
- No CPU-GPU sync required after initial upload
- Parallel access from all particles simultaneously

**Update Performance (CPU)**:
- Pack operation: ~1-5ms for 1000 trajectories
- Texture upload: ~5-20ms depending on size
- One-time cost when dataset changes

## Future Extensibility

### Phase 2: Additional Attributes

The texture-based approach easily supports multiple attributes:

**Additional Textures**:
1. **Velocity Texture** (RGBA32F)
   - RGB: Velocity vector (X, Y, Z)
   - A: Speed magnitude

2. **Metadata Texture** (RGBA32F)
   - R: Object extent
   - G: Trajectory ID
   - B: Custom attribute 1
   - A: Custom attribute 2

**HLSL Sampling**:
```hlsl
// Sample velocity
float4 VelocityData = Texture2DSample(VelocityTexture, VelocityTextureSampler, UV);
float3 Velocity = VelocityData.rgb;
float Speed = VelocityData.a;

// Use for motion blur, flow lines, etc.
Particles.Velocity = Velocity;
Particles.Size = Speed * 0.1;  // Size based on speed
```

### Phase 3: Advanced Features

1. **Time-based Visibility**:
   - Add time range parameters
   - Show/hide trajectories based on time window
   - Implement temporal filtering

2. **Spatial Queries**:
   - Bounding box filtering (pass as parameters)
   - Camera-based culling
   - Region of interest selection

3. **Visual Effects**:
   - Trail fade-out effects
   - Particle emission along trajectory
   - Collision effects at specific points

## Alternative Approaches Considered

### Why NOT Niagara Data Interface (NDI)?

**User Requirement**: Avoid version-specific API dependencies

**NDI Drawbacks**:
- API changes between UE versions (4.26, 4.27, 5.0, 5.1+)
- Requires recompilation for each UE version
- Complex VM function binding
- Difficult to debug

**Texture Approach Advantages**:
- Standard texture sampling (stable API)
- Works across all UE5 versions
- Simple HLSL code (copy-paste ready)
- Easy to debug (inspect textures in editor)
- Better GPU performance for large datasets

### Why NOT Actor-Based Spawning?

**Drawbacks**:
- Poor performance (thousands of actors)
- No particle effects (ribbons, colors, etc.)
- High overhead for updates
- Limited visual flexibility

### Why NOT Blueprint-Only?

**Drawbacks**:
- Cannot efficiently pass arrays to Niagara
- Poor performance for large datasets
- No direct GPU access
- Complex and slow

## Build Configuration

### Update TrajectoryData.Build.cs

Add minimal dependencies for texture types:

```csharp
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "Json",
        "JsonUtilities",
        "RenderCore",      // For texture types
        "RHI"              // For texture formats
    }
);
```

**Note**: We do NOT need to add "Niagara" or "NiagaraCore" modules since we're not using NDI!

## Implementation Phases

### Phase 1: Core Texture Provider (This Iteration)
- Implement `UTrajectoryTextureProvider` component
- Pack position data into RGBA32F texture
- Provide complete copy-paste HLSL code
- Create detailed setup guide

### Phase 2: Enhanced Features (Future)
- Add velocity, acceleration textures
- Implement LOD system
- Advanced rendering options
- Performance profiling tools

### Phase 3: Artist Tools (Future)
- Blueprint helper functions
- UI for dataset selection
- Visualization presets
- Material library for trajectories

## Required Files to Implement

### New C++ Files
1. `TrajectoryTextureProvider.h` - Component for texture packing
2. `TrajectoryTextureProvider.cpp` - Implementation

### Modified Files
1. `TrajectoryData.Build.cs` - Add RenderCore, RHI dependencies
2. `README.md` - Add Niagara integration documentation

### New Documentation
1. `NIAGARA_HLSL_EXAMPLES.md` - Ready-to-use HLSL code snippets
2. `examples/NIAGARA_USAGE_EXAMPLE.md` - Complete Blueprint examples

### Optional Content Files
1. `Content/Niagara/NS_TrajectoryVisualization.uasset` - Example Niagara System
2. `Content/Materials/M_TrajectoryLine.uasset` - Example line material

## Summary

**Approved Approach**: Texture-Based Data Transfer with Direct HLSL

**Key Benefits**:
1. ✅ **Version Independent** - No NDI API dependencies
2. ✅ **Maximum Performance** - Direct GPU texture sampling
3. ✅ **Copy-Paste Ready** - Complete HLSL code provided
4. ✅ **Extensible** - Easy to add more attribute textures
5. ✅ **Debuggable** - Inspect textures in UE editor

**Data Flow**:
```
CPU: LoadedTrajectories → Pack → UTexture2D
GPU: Niagara HLSL → Texture2DSample → Particle Position → Ribbon Lines
```

**Next Steps**: Proceed with C++ implementation of `UTrajectoryTextureProvider`

---

**Ready to implement upon confirmation.**
