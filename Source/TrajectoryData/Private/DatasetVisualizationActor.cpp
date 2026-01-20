// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasetVisualizationActor.h"
#include "TrajectoryDataLoader.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceTrajectoryBuffer.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

ADatasetVisualizationActor::ADatasetVisualizationActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create root scene component
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Create Niagara component
	NiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NiagaraComponent"));
	NiagaraComponent->SetupAttachment(RootComponent);
	NiagaraComponent->bAutoActivate = false;

	// Create buffer provider component
	BufferProvider = CreateDefaultSubobject<UTrajectoryBufferProvider>(TEXT("BufferProvider"));
}

void ADatasetVisualizationActor::BeginPlay()
{
	Super::BeginPlay();

	// Initialize Niagara component with template
	InitializeNiagaraComponent();

	// Auto-load dataset if requested
	if (bAutoLoadOnBeginPlay)
	{
		LoadAndBindDataset(AutoLoadDatasetIndex);
	}
}

void ADatasetVisualizationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Deactivate Niagara system
	if (NiagaraComponent && NiagaraComponent->IsActive())
	{
		NiagaraComponent->Deactivate();
	}

	bBuffersBound = false;
	CurrentDatasetIndex = -1;

	Super::EndPlay(EndPlayReason);
}

bool ADatasetVisualizationActor::LoadAndBindDataset(int32 DatasetIndex)
{
	if (!BufferProvider)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: BufferProvider is null"));
		return false;
	}

	if (!NiagaraComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: NiagaraComponent is null"));
		return false;
	}

	// Load data into buffer
	if (!BufferProvider->UpdateFromDataset(DatasetIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Failed to load dataset %d"), DatasetIndex);
		return false;
	}

	// Configure the NDI with our buffer provider
	if (!ConfigureTrajectoryBufferNDI())
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Failed to configure Trajectory Buffer NDI"));
		return false;
	}

	// Pass metadata parameters
	if (!PassMetadataToNiagara())
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Failed to pass metadata to Niagara (non-critical)"));
	}

	// Activate Niagara if requested
	if (bAutoActivate && !NiagaraComponent->IsActive())
	{
		NiagaraComponent->Activate(true);
	}

	bBuffersBound = true;
	CurrentDatasetIndex = DatasetIndex;

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully loaded and bound dataset %d with NDI"), DatasetIndex);
	return true;
}

bool ADatasetVisualizationActor::SwitchToDataset(int32 DatasetIndex)
{
	// Deactivate current visualization
	if (NiagaraComponent && NiagaraComponent->IsActive())
	{
		NiagaraComponent->Deactivate();
	}

	// Load new dataset
	bool bSuccess = LoadAndBindDataset(DatasetIndex);

	// Reactivate if requested
	if (bSuccess && bAutoActivate)
	{
		NiagaraComponent->Activate(true);
	}

	return bSuccess;
}

bool ADatasetVisualizationActor::IsVisualizationReady() const
{
	return bBuffersBound && 
	       BufferProvider && 
	       BufferProvider->IsBufferValid() && 
	       NiagaraComponent != nullptr;
}

FTrajectoryBufferMetadata ADatasetVisualizationActor::GetDatasetMetadata() const
{
	if (BufferProvider)
	{
		return BufferProvider->GetMetadata();
	}
	return FTrajectoryBufferMetadata();
}

TArray<FTrajectoryBufferInfo> ADatasetVisualizationActor::GetTrajectoryInfoArray() const
{
	if (BufferProvider)
	{
		return BufferProvider->GetTrajectoryInfo();
	}
	return TArray<FTrajectoryBufferInfo>();
}

void ADatasetVisualizationActor::SetVisualizationActive(bool bActivate)
{
	if (!NiagaraComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Cannot set activation - NiagaraComponent is null"));
		return;
	}

	if (bActivate)
	{
		if (!bBuffersBound)
		{
			UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Cannot activate - buffers not bound. Call LoadAndBindDataset first."));
			return;
		}
		NiagaraComponent->Activate(true);
	}
	else
	{
		NiagaraComponent->Deactivate();
	}
}

bool ADatasetVisualizationActor::BindBuffersToNiagara()
{
	// This method is now deprecated in favor of ConfigureTrajectoryBufferNDI()
	// Kept for backwards compatibility
	return ConfigureTrajectoryBufferNDI();
}

bool ADatasetVisualizationActor::ConfigureTrajectoryBufferNDI()
{
	if (!BufferProvider || !NiagaraComponent)
	{
		return false;
	}

	// Validate buffer is ready
	if (!BufferProvider->IsBufferValid())
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Buffer is not valid"));
		return false;
	}

	// Get the Trajectory Buffer NDI from Niagara User Parameters
	UNiagaraDataInterfaceTrajectoryBuffer* TrajectoryBufferNDI = Cast<UNiagaraDataInterfaceTrajectoryBuffer>(
		NiagaraComponent->GetOverrideParameter(TrajectoryBufferNDIParameterName)
	);

	if (!TrajectoryBufferNDI)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Could not find TrajectoryBuffer NDI with name '%s' in Niagara system. Make sure to add a User Parameter of type 'Trajectory Position Buffer' with this name."), 
		       *TrajectoryBufferNDIParameterName.ToString());
		return false;
	}

	// Assign our buffer provider to the NDI
	TrajectoryBufferNDI->BufferProvider = BufferProvider;

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully configured Trajectory Buffer NDI with %d positions"), 
	       BufferProvider->GetMetadata().TotalSampleCount);

	return true;
}

bool ADatasetVisualizationActor::PassMetadataToNiagara()
{
	if (!BufferProvider || !NiagaraComponent)
	{
		return false;
	}

	// Get metadata
	FTrajectoryBufferMetadata Metadata = BufferProvider->GetMetadata();

	// Pass int parameters
	NiagaraComponent->SetIntParameter(TEXT("NumTrajectories"), Metadata.NumTrajectories);
	NiagaraComponent->SetIntParameter(TEXT("MaxSamplesPerTrajectory"), Metadata.MaxSamplesPerTrajectory);
	NiagaraComponent->SetIntParameter(TEXT("TotalSampleCount"), Metadata.TotalSampleCount);
	NiagaraComponent->SetIntParameter(TEXT("FirstTimeStep"), Metadata.FirstTimeStep);
	NiagaraComponent->SetIntParameter(TEXT("LastTimeStep"), Metadata.LastTimeStep);

	// Pass vector parameters
	NiagaraComponent->SetVectorParameter(TEXT("BoundsMin"), Metadata.BoundsMin);
	NiagaraComponent->SetVectorParameter(TEXT("BoundsMax"), Metadata.BoundsMax);

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Passed metadata to Niagara (%d trajectories, %d samples)"), 
	       Metadata.NumTrajectories, Metadata.TotalSampleCount);

	return true;
}

void ADatasetVisualizationActor::InitializeNiagaraComponent()
{
	if (!NiagaraComponent)
	{
		return;
	}

	// Set Niagara system template if provided
	if (NiagaraSystemTemplate)
	{
		NiagaraComponent->SetAsset(NiagaraSystemTemplate);
		UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Set Niagara system template: %s. Make sure it has a '%s' User Parameter (Trajectory Position Buffer type)."), 
		       *NiagaraSystemTemplate->GetName(), *TrajectoryBufferNDIParameterName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: No Niagara system template set. Assign NiagaraSystemTemplate in Blueprint or editor."));
	}
}
