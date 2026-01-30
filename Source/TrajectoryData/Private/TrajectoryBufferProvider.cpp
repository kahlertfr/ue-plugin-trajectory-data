// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryBufferProvider.h"
#include "TrajectoryDataLoader.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "NiagaraComponent.h"

// ============================================================================
// FTrajectoryPositionBufferResource Implementation
// ============================================================================

void FTrajectoryPositionBufferResource::Initialize(const TArray<FVector>& PositionData)
{
	CPUPositionData = PositionData;
	NumElements = PositionData.Num();

	// Update resource on render thread
	ENQUEUE_RENDER_COMMAND(UpdateTrajectoryPositionBuffer)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			if (IsInitialized())
			{
				ReleaseResource();
			}
			InitResource(RHICmdList);
		});
}

void FTrajectoryPositionBufferResource::InitResource(FRHICommandListBase& RHICmdList)
{
	FRenderResource::InitResource(RHICmdList);

	if (NumElements == 0)
	{
		return;
	}

	// Create structured buffer
	FRHIResourceCreateInfo CreateInfo(TEXT("TrajectoryPositionBuffer"));
	
	const uint32 ElementSize = sizeof(FVector);
	const uint32 BufferSize = NumElements * ElementSize;

	StructuredBuffer = RHICmdList.CreateStructuredBuffer(
		ElementSize,
		BufferSize,
		BUF_ShaderResource | BUF_Static,
		CreateInfo
	);

	// Create shader resource view
	BufferSRV = RHICmdList.CreateShaderResourceView(StructuredBuffer);

	// Upload data to GPU
	if (CPUPositionData.Num() > 0)
	{
		void* BufferData = RHICmdList.LockBuffer(StructuredBuffer, 0, BufferSize, RLM_WriteOnly);
		FMemory::Memcpy(BufferData, CPUPositionData.GetData(), BufferSize);
		RHICmdList.UnlockBuffer(StructuredBuffer);
	}
}

void FTrajectoryPositionBufferResource::ReleaseResource()
{
	BufferSRV.SafeRelease();
	StructuredBuffer.SafeRelease();
	FRenderResource::ReleaseResource();
}

// ============================================================================
// UTrajectoryBufferProvider Implementation
// ============================================================================

UTrajectoryBufferProvider::UTrajectoryBufferProvider()
{
	PrimaryComponentTick.bCanEverTick = false;
	PositionBufferResource = new FTrajectoryPositionBufferResource();
}

UTrajectoryBufferProvider::~UTrajectoryBufferProvider()
{
	if (PositionBufferResource)
	{
		// Release on render thread
		FTrajectoryPositionBufferResource* ResourceToDelete = PositionBufferResource;
		ENQUEUE_RENDER_COMMAND(ReleaseTrajectoryPositionBuffer)(
			[ResourceToDelete](FRHICommandListImmediate& RHICmdList)
			{
				ResourceToDelete->ReleaseResource();
				delete ResourceToDelete;
			});
		PositionBufferResource = nullptr;
	}
}

void UTrajectoryBufferProvider::BeginDestroy()
{
	Super::BeginDestroy();

	if (PositionBufferResource)
	{
		// Release on render thread
		FTrajectoryPositionBufferResource* ResourceToDelete = PositionBufferResource;
		ENQUEUE_RENDER_COMMAND(ReleaseTrajectoryPositionBuffer)(
			[ResourceToDelete](FRHICommandListImmediate& RHICmdList)
			{
				ResourceToDelete->ReleaseResource();
				delete ResourceToDelete;
			});
		PositionBufferResource = nullptr;
	}
}

bool UTrajectoryBufferProvider::UpdateFromDataset(int32 DatasetIndex)
{
	// Get dataset manager
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	if (!Loader)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryBufferProvider: TrajectoryLoader not found"));
		return false;
	}

	// Get loaded datasets
	const TArray<FLoadedDataset>& LoadedDatasets = Loader->GetLoadedDatasets();
	if (!LoadedDatasets.IsValidIndex(DatasetIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryBufferProvider: Invalid dataset index %d"), DatasetIndex);
		return false;
	}

	const FLoadedDataset& Dataset = LoadedDatasets[DatasetIndex];
	if (Dataset.Trajectories.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider: Dataset has no trajectories"));
		return false;
	}

	// Update metadata
	Metadata.NumTrajectories = Dataset.Trajectories.Num();
	Metadata.FirstTimeStep = Dataset.DatasetInfo.Metadata.FirstTimeStep;
	Metadata.LastTimeStep = Dataset.DatasetInfo.Metadata.LastTimeStep;
	Metadata.BoundsMin = FVector(Dataset.DatasetInfo.Metadata.BoundingBoxMin[0],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMin[1],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMin[2]);
	Metadata.BoundsMax = FVector(Dataset.DatasetInfo.Metadata.BoundingBoxMax[0],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMax[1],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMax[2]);

	// Calculate max samples per trajectory
	Metadata.MaxSamplesPerTrajectory = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		Metadata.MaxSamplesPerTrajectory = FMath::Max(Metadata.MaxSamplesPerTrajectory, Traj.Samples.Num());
	}

	// Pack trajectory data into flat position array
	TArray<FVector> PositionData;
	PackTrajectories(Dataset, PositionData);

	Metadata.TotalSampleCount = PositionData.Num();

	// Initialize GPU buffer with position data
	if (PositionBufferResource)
	{
		PositionBufferResource->InitializeResource();
		PositionBufferResource->Initialize(PositionData);
	}

	UE_LOG(LogTemp, Log, TEXT("TrajectoryBufferProvider: Updated with %d trajectories, %d total samples, %.2f MB"),
		Metadata.NumTrajectories, Metadata.TotalSampleCount, 
		(Metadata.TotalSampleCount * sizeof(FVector)) / (1024.0f * 1024.0f));

	return true;
}

int64 UTrajectoryBufferProvider::GetTrajectoryId(int32 TrajectoryIndex) const
{
	if (TrajectoryInfo.IsValidIndex(TrajectoryIndex))
	{
		return TrajectoryInfo[TrajectoryIndex].TrajectoryId;
	}
	return -1;
}

TArray<FVector> UTrajectoryBufferProvider::GetAllPositions() const
{
	if (PositionBufferResource)
	{
		return PositionBufferResource->GetCPUPositionData();
	}
	return TArray<FVector>();
}

bool UTrajectoryBufferProvider::BindToNiagaraSystem(UNiagaraComponent* NiagaraComponent, FName BufferParameterName)
{
	// Validate inputs
	if (!NiagaraComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider::BindToNiagaraSystem - NiagaraComponent is null"));
		return false;
	}

	if (!PositionBufferResource)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider::BindToNiagaraSystem - PositionBufferResource is null. Call UpdateFromDataset() first."));
		return false;
	}

	if (!PositionBufferResource->GetBufferSRV().IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider::BindToNiagaraSystem - BufferSRV is not valid. Ensure data is loaded."));
		return false;
	}

	// Set the buffer as a user parameter
	// Note: Niagara structured buffers require custom Niagara Data Interface for HLSL access
	// This function passes metadata to Niagara parameters that can be used in HLSL
	
	UE_LOG(LogTemp, Display, TEXT("TrajectoryBufferProvider::BindToNiagaraSystem - Binding buffer '%s' to Niagara component"), *BufferParameterName.ToString());
	UE_LOG(LogTemp, Display, TEXT("  Buffer has %d elements"), PositionBufferResource->GetNumElements());
	
	// Pass metadata as Niagara parameters (accessible in HLSL as int/float/vector parameters)
	NiagaraComponent->SetIntParameter(FName(*(BufferParameterName.ToString() + TEXT("_NumElements"))), PositionBufferResource->GetNumElements());
	NiagaraComponent->SetIntParameter(TEXT("NumTrajectories"), Metadata.NumTrajectories);
	NiagaraComponent->SetIntParameter(TEXT("MaxSamplesPerTrajectory"), Metadata.MaxSamplesPerTrajectory);
	NiagaraComponent->SetIntParameter(TEXT("TotalSampleCount"), Metadata.TotalSampleCount);
	NiagaraComponent->SetVectorParameter(TEXT("BoundsMin"), Metadata.BoundsMin);
	NiagaraComponent->SetVectorParameter(TEXT("BoundsMax"), Metadata.BoundsMax);
	
	UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider::BindToNiagaraSystem - Note: Direct buffer binding to HLSL requires a custom Niagara Data Interface."));
	UE_LOG(LogTemp, Warning, TEXT("  Metadata has been passed as parameters. For full buffer access in HLSL, implement a custom NDI."));
	UE_LOG(LogTemp, Warning, TEXT("  Alternatively, use UTrajectoryTextureProvider for Blueprint-compatible workflow."));
	
	return true;
}

void UTrajectoryBufferProvider::PackTrajectories(const FLoadedDataset& Dataset, TArray<FVector>& OutPositionData)
{
	// Calculate total samples needed
	int32 TotalSamples = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		TotalSamples += Traj.Samples.Num();
	}

	// Pre-allocate arrays
	OutPositionData.Reserve(TotalSamples);
	TrajectoryInfo.Reset();
	TrajectoryInfo.Reserve(Dataset.Trajectories.Num());

	// Pack all trajectories sequentially
	int32 CurrentIndex = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		// Store trajectory info
		FTrajectoryBufferInfo Info;
		Info.TrajectoryId = Traj.TrajectoryId;
		Info.StartIndex = CurrentIndex;
		Info.SampleCount = Traj.Samples.Num();
		Info.StartTimeStep = Traj.StartTimeStep;
		Info.EndTimeStep = Traj.EndTimeStep;
		Info.Extent = Traj.Extent;
		TrajectoryInfo.Add(Info);

		// Copy positions directly - NO iteration needed!
		// This is the key performance improvement: direct memory operations
		for (const FTrajectoryPositionSample& Sample : Traj.Samples)
		{
			OutPositionData.Add(Sample.Position);
		}

		CurrentIndex += Traj.Samples.Num();
	}

	check(OutPositionData.Num() == TotalSamples);
	check(TrajectoryInfo.Num() == Dataset.Trajectories.Num());
}

void UTrajectoryBufferProvider::ReleaseCPUPositionData()
{
	if (PositionBufferResource)
	{
		PositionBufferResource->ReleaseCPUData();
		UE_LOG(LogTemp, Log, TEXT("TrajectoryBufferProvider: Released CPU position data to save memory"));
	}
}

// Implementiere die Methode:
void FTrajectoryPositionBufferResource::InitializeResource()
{
	// Standardm��ig: Initialisiere das Resource-Objekt auf dem Render-Thread
	ENQUEUE_RENDER_COMMAND(InitTrajectoryPositionBuffer)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			if (IsInitialized())
			{
				ReleaseResource();
			}
			InitResource(RHICmdList);
		});
}
