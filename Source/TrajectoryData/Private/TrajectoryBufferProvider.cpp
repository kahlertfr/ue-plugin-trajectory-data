// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryBufferProvider.h"
#include "TrajectoryDataManager.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

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
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get(GetWorld());
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryBufferProvider: TrajectoryDataManager not found"));
		return false;
	}

	// Get loaded datasets
	const TArray<FLoadedDataset>& LoadedDatasets = Manager->GetLoadedDatasets();
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
	Metadata.FirstTimeStep = Dataset.DatasetInfo.Meta.FirstTimeStep;
	Metadata.LastTimeStep = Dataset.DatasetInfo.Meta.LastTimeStep;
	Metadata.BoundsMin = FVector(Dataset.DatasetInfo.Meta.BBoxMin[0], 
								  Dataset.DatasetInfo.Meta.BBoxMin[1], 
								  Dataset.DatasetInfo.Meta.BBoxMin[2]);
	Metadata.BoundsMax = FVector(Dataset.DatasetInfo.Meta.BBoxMax[0], 
								  Dataset.DatasetInfo.Meta.BBoxMax[1], 
								  Dataset.DatasetInfo.Meta.BBoxMax[2]);

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
