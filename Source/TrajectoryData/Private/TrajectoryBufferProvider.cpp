// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryBufferProvider.h"
#include "TrajectoryDataLoader.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "NiagaraComponent.h"
#include "Async/Async.h"

// ============================================================================
// FTrajectoryPositionBufferResource Implementation
// ============================================================================

void FTrajectoryPositionBufferResource::Initialize(const TArray<FVector3f>& PositionData)
{
	// Store data on game thread before passing to render thread
	// This is safe because we're copying the data
	CPUPositionData = PositionData;
	NumElements = PositionData.Num();

	// Capture data size for render thread (CPUPositionData will be accessed on render thread)
	// The actual data copy to GPU happens on the render thread in InitResource()
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

void FTrajectoryPositionBufferResource::Initialize(TArray<FVector3f>&& PositionData)
{
	// Move data on game thread - this transfers ownership to CPUPositionData
	// Safe because the moved-from array is no longer used by the caller
	CPUPositionData = MoveTemp(PositionData);
	NumElements = CPUPositionData.Num();

	// Queue GPU upload on render thread
	// CPUPositionData is owned by this object and won't be modified on game thread after this point
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
	
	const uint32 ElementSize = sizeof(FVector3f);
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
	// NOTE: This function runs on the GAME THREAD
	// Array population (PackTrajectories) happens here on the game thread
	// Data is then transferred to the render thread via Initialize()
	
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

	// GAME THREAD: Pack trajectory data into flat position array
	// This happens on the game thread - all array building/population is done here
	TArray<FVector3f> PositionData;
	PackTrajectories(Dataset, PositionData);

	Metadata.TotalSampleCount = PositionData.Num();

	// THREAD HANDOFF: Transfer data to render thread via Initialize()
	// Initialize() stores the data and enqueues GPU upload to the render thread
	// After this call, we don't modify PositionData or the buffer resource data on game thread
	if (PositionBufferResource)
	{
		PositionBufferResource->InitializeResource();
		PositionBufferResource->Initialize(MoveTemp(PositionData));
	}

	UE_LOG(LogTemp, Log, TEXT("TrajectoryBufferProvider: Updated with %d trajectories, %d total samples, %.2f MB"),
		Metadata.NumTrajectories, Metadata.TotalSampleCount, 
		(Metadata.TotalSampleCount * sizeof(FVector3f)) / (1024.0f * 1024.0f));

	return true;
}

int32 UTrajectoryBufferProvider::GetTrajectoryId(int32 TrajectoryIndex) const
{
	if (TrajectoryInfo.IsValidIndex(TrajectoryIndex))
	{
		return TrajectoryInfo[TrajectoryIndex].TrajectoryId;
	}
	return -1;
}

TArray<FVector3f> UTrajectoryBufferProvider::GetAllPositions() const
{
	if (PositionBufferResource)
	{
		return PositionBufferResource->GetCPUPositionData();
	}
	return TArray<FVector3f>();
}

const TArray<FVector3f>& UTrajectoryBufferProvider::GetAllPositionsRef() const
{
	if (PositionBufferResource)
	{
		return PositionBufferResource->GetCPUPositionData();
	}
	// Return a reference to a thread-local static empty array for thread safety
	static thread_local TArray<FVector3f> EmptyArray;
	return EmptyArray;
}

void UTrajectoryBufferProvider::PackTrajectories(const FLoadedDataset& Dataset, TArray<FVector3f>& OutPositionData)
{
	PackTrajectoriesStatic(Dataset, OutPositionData, SampleTimeSteps, TrajectoryInfo);
}

void UTrajectoryBufferProvider::PackTrajectoriesStatic(
	const FLoadedDataset& Dataset,
	TArray<FVector3f>& OutPositionData,
	TArray<int32>& OutSampleTimeSteps,
	TArray<FTrajectoryBufferInfo>& OutTrajectoryInfo)
{
	// Calculate total samples needed
	int32 TotalSamples = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		TotalSamples += Traj.Samples.Num();
	}

	// Pre-allocate arrays
	OutPositionData.Reserve(TotalSamples);
	OutSampleTimeSteps.Reset();
	OutSampleTimeSteps.Reserve(TotalSamples);
	OutTrajectoryInfo.Reset();
	OutTrajectoryInfo.Reserve(Dataset.Trajectories.Num());

	// Pack all trajectories sequentially
	int32 CurrentIndex = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		// Store trajectory info
		FTrajectoryBufferInfo Info;
		Info.TrajectoryId = static_cast<int32>(Traj.TrajectoryId);  // Cast int64 to int32
		Info.StartIndex = CurrentIndex;
		Info.SampleCount = Traj.Samples.Num();
		Info.StartTimeStep = Traj.StartTimeStep;
		Info.EndTimeStep = Traj.EndTimeStep;
		Info.Extent = Traj.Extent;
		OutTrajectoryInfo.Add(Info);

		// Copy positions using efficient bulk operations
		// TArray::Append is optimized for bulk copying and handles all memory operations internally
		OutPositionData.Append(Traj.Samples);

		// Generate time steps for each sample in this trajectory
		int32 NumSamples = Traj.Samples.Num();
		if (NumSamples > 0)
		{
			if (NumSamples == 1)
			{
				// Single sample - use start time step
				OutSampleTimeSteps.Add(Traj.StartTimeStep);
			}
			else
			{
				// Multiple samples - distribute evenly between start and end time steps
				for (int32 i = 0; i < NumSamples; ++i)
				{
					// Linear interpolation: TimeStep = StartTime + (i / (NumSamples - 1)) * (EndTime - StartTime)
					float t = static_cast<float>(i) / static_cast<float>(NumSamples - 1);
					int32 TimeStep = Traj.StartTimeStep + FMath::RoundToInt(t * (Traj.EndTimeStep - Traj.StartTimeStep));
					OutSampleTimeSteps.Add(TimeStep);
				}
			}
		}

		CurrentIndex += Traj.Samples.Num();
	}

	check(OutPositionData.Num() == TotalSamples);
	check(OutSampleTimeSteps.Num() == TotalSamples);
	check(OutTrajectoryInfo.Num() == Dataset.Trajectories.Num());
}

void UTrajectoryBufferProvider::ReleaseCPUPositionData()
{
	if (PositionBufferResource)
	{
		PositionBufferResource->ReleaseCPUData();
		UE_LOG(LogTemp, Log, TEXT("TrajectoryBufferProvider: Released CPU position data to save memory"));
	}
}

void UTrajectoryBufferProvider::UpdateFromDatasetAsync(int32 DatasetIndex, TFunction<void(bool)> OnComplete)
{
	// NOTE: Must be called on the GAME THREAD
	check(IsInGameThread());

	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	if (!Loader)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryBufferProvider: TrajectoryLoader not found"));
		OnComplete(false);
		return;
	}

	const TArray<FLoadedDataset>& LoadedDatasets = Loader->GetLoadedDatasets();
	if (!LoadedDatasets.IsValidIndex(DatasetIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryBufferProvider: Invalid dataset index %d"), DatasetIndex);
		OnComplete(false);
		return;
	}

	const FLoadedDataset& Dataset = LoadedDatasets[DatasetIndex];
	if (Dataset.Trajectories.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider: Dataset has no trajectories"));
		OnComplete(false);
		return;
	}

	// Update metadata on game thread (fast)
	Metadata.NumTrajectories = Dataset.Trajectories.Num();
	Metadata.FirstTimeStep = Dataset.DatasetInfo.Metadata.FirstTimeStep;
	Metadata.LastTimeStep = Dataset.DatasetInfo.Metadata.LastTimeStep;
	Metadata.BoundsMin = FVector(Dataset.DatasetInfo.Metadata.BoundingBoxMin[0],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMin[1],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMin[2]);
	Metadata.BoundsMax = FVector(Dataset.DatasetInfo.Metadata.BoundingBoxMax[0],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMax[1],
								  Dataset.DatasetInfo.Metadata.BoundingBoxMax[2]);

	Metadata.MaxSamplesPerTrajectory = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		Metadata.MaxSamplesPerTrajectory = FMath::Max(Metadata.MaxSamplesPerTrajectory, Traj.Samples.Num());
	}

	// Capture a const pointer to the dataset for the background thread.
	// CONTRACT: The caller must not call UnloadAll() or otherwise modify the loaded datasets
	// while this async operation is in flight.  The pointer is stable for the lifetime of the
	// UTrajectoryDataLoader singleton as long as no load/unload operations run concurrently.
	const FLoadedDataset* DatasetPtr = &Dataset;

	TWeakObjectPtr<UTrajectoryBufferProvider> WeakThis(this);
	TWeakObjectPtr<UTrajectoryDataLoader> WeakLoader(Loader);

	// Offload CPU-heavy data packing to a background thread
	Async(EAsyncExecution::ThreadPool, [WeakThis, WeakLoader, DatasetPtr, OnComplete]()
	{
		// Guard: if the loader has been GC'd the DatasetPtr is no longer safe to use
		if (!WeakLoader.IsValid())
		{
			Async(EAsyncExecution::TaskGraphMainThread, [OnComplete]()
			{
				UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider: DataLoader was destroyed during async packing"));
				OnComplete(false);
			});
			return;
		}

		// Background thread: read-only access to DatasetPtr – writes go to local arrays only
		TArray<FVector3f> PositionData;
		TArray<int32> NewSampleTimeSteps;
		TArray<FTrajectoryBufferInfo> NewTrajectoryInfo;

		PackTrajectoriesStatic(*DatasetPtr, PositionData, NewSampleTimeSteps, NewTrajectoryInfo);

		// Return to game thread to update class members and initialise GPU buffer
		Async(EAsyncExecution::TaskGraphMainThread,
			[WeakThis,
			 Positions = MoveTemp(PositionData),
			 TimeSteps = MoveTemp(NewSampleTimeSteps),
			 TrajInfo = MoveTemp(NewTrajectoryInfo),
			 OnComplete]() mutable
		{
			if (!WeakThis.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("TrajectoryBufferProvider: Provider was destroyed during async packing"));
				OnComplete(false);
				return;
			}

			// Move packed data into class members
			WeakThis->TrajectoryInfo = MoveTemp(TrajInfo);
			WeakThis->SampleTimeSteps = MoveTemp(TimeSteps);
			WeakThis->Metadata.TotalSampleCount = Positions.Num();

			// Initialise GPU buffer
			if (WeakThis->PositionBufferResource)
			{
				WeakThis->PositionBufferResource->InitializeResource();
				WeakThis->PositionBufferResource->Initialize(MoveTemp(Positions));
			}

			UE_LOG(LogTemp, Log, TEXT("TrajectoryBufferProvider: Async update complete – %d trajectories, %d total samples, %.2f MB"),
				WeakThis->Metadata.NumTrajectories, WeakThis->Metadata.TotalSampleCount,
				(WeakThis->Metadata.TotalSampleCount * sizeof(FVector3f)) / (1024.0f * 1024.0f));

			OnComplete(true);
		});
	});
}

// Initialize the resource
void FTrajectoryPositionBufferResource::InitializeResource()
{
	// Initialize the resource object on the render thread
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
