// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasetVisualizationActor.h"
#include "TrajectoryDataLoader.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

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

	// Populate the Position Array NDI with position data
	if (!PopulatePositionArrayNDI())
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Failed to populate Position Array NDI"));
		return false;
	}

	// Populate TrajectoryInfo arrays if enabled
	if (bTransferTrajectoryInfo)
	{
		if (!PopulateTrajectoryInfoArrays())
		{
			UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Failed to populate TrajectoryInfo arrays (non-critical)"));
		}
	}

	// Populate SampleTimeSteps array
	if (!PopulateSampleTimeStepsArray())
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Failed to populate SampleTimeSteps array (non-critical)"));
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

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully loaded and bound dataset %d using Position Array NDI"), DatasetIndex);
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
		// Return a copy for Blueprint use (Blueprint requires value return)
		// The underlying getter now returns const reference, so this is a single copy
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

bool ADatasetVisualizationActor::PopulatePositionArrayNDI()
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

	// Get all positions from buffer provider as a flat array (const reference - no copy!)
	FTrajectoryBufferMetadata Metadata = BufferProvider->GetMetadata();
	const TArray<FVector3f>& AllPositions3f = BufferProvider->GetAllPositions();
	
	if (AllPositions3f.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: No positions available"));
		return false;
	}

	// Convert FVector3f to FVector for Niagara API
	// Note: Niagara's SetNiagaraArrayPosition expects TArray<FVector> (double precision)
	// even though internally it may use float precision
	TArray<FVector> AllPositions;
	AllPositions.Reserve(AllPositions3f.Num());
	for (const FVector3f& Pos3f : AllPositions3f)
	{
		AllPositions.Add(FVector(Pos3f));
	}

	// Set the position array using UE5's built-in array NDI function
	// This automatically finds or creates the Float3 Array NDI and populates it
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
		NiagaraComponent, 
		PositionArrayParameterName, 
		AllPositions
	);

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully populated Position Array NDI with %d positions"), 
	       AllPositions.Num());

	// Release CPU position data from BufferProvider to save memory
	// The data has been transferred to Niagara, so we don't need the CPU copy anymore
	if (BufferProvider)
	{
		BufferProvider->ReleaseCPUPositionData();
	}

	return true;
}

bool ADatasetVisualizationActor::PopulateTrajectoryInfoArrays()
{
	if (!BufferProvider || !NiagaraComponent)
	{
		return false;
	}

	// Get trajectory info array from buffer provider (const reference - no copy!)
	const TArray<FTrajectoryBufferInfo>& TrajectoryInfo = BufferProvider->GetTrajectoryInfo();
	
	if (TrajectoryInfo.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: No trajectory info available"));
		return false;
	}

	// Prepare arrays for each field (removed SampleCount and StartTimeStep as they're no longer needed)
	TArray<int32> TrajectoryId;
	TArray<int32> StartIndex;
	TArray<FVector> Extent;  // Using FVector for Niagara compatibility

	// Reserve space
	const int32 NumTrajectories = TrajectoryInfo.Num();
	TrajectoryId.Reserve(NumTrajectories);
	StartIndex.Reserve(NumTrajectories);
	Extent.Reserve(NumTrajectories);

	// Pack data into arrays
	for (const FTrajectoryBufferInfo& Info : TrajectoryInfo)
	{
		TrajectoryId.Add(Info.TrajectoryId);
		StartIndex.Add(Info.StartIndex);
		Extent.Add(FVector(Info.Extent));  // Convert FVector3f to FVector
	}

	// Transfer arrays to Niagara using the parameter prefix
	FString Prefix = TrajectoryInfoParameterPrefix.ToString();
	
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
		NiagaraComponent, 
		FName(*(Prefix + "StartIndex")), 
		StartIndex
	);
	
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
		NiagaraComponent, 
		FName(*(Prefix + "TrajectoryId")), 
		TrajectoryId
	);
	
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
		NiagaraComponent, 
		FName(*(Prefix + "Extent")), 
		Extent
	);

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully populated TrajectoryInfo arrays with %d trajectories (SampleCount and StartTimeStep removed)"), 
	       NumTrajectories);

	return true;
}

bool ADatasetVisualizationActor::PopulateSampleTimeStepsArray()
{
	if (!BufferProvider || !NiagaraComponent)
	{
		return false;
	}

	// Get sample time steps array from buffer provider (const reference - no copy!)
	const TArray<int32>& SampleTimeSteps = BufferProvider->GetSampleTimeSteps();
	
	if (SampleTimeSteps.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: No sample time steps available"));
		return false;
	}

	// Transfer SampleTimeSteps array to Niagara
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
		NiagaraComponent, 
		TEXT("SampleTimeSteps"), 
		SampleTimeSteps
	);

	// Compute global first and last time steps across all samples
	int32 GlobalFirstTimeStep = TNumericLimits<int32>::Max();
	int32 GlobalLastTimeStep = TNumericLimits<int32>::Min();
	
	for (int32 TimeStep : SampleTimeSteps)
	{
		GlobalFirstTimeStep = FMath::Min(GlobalFirstTimeStep, TimeStep);
		GlobalLastTimeStep = FMath::Max(GlobalLastTimeStep, TimeStep);
	}

	// Transfer global time step range to Niagara as int parameters
	NiagaraComponent->SetIntParameter(TEXT("GlobalFirstTimeStep"), GlobalFirstTimeStep);
	NiagaraComponent->SetIntParameter(TEXT("GlobalLastTimeStep"), GlobalLastTimeStep);

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully populated SampleTimeSteps array with %d entries (time range: %d to %d)"), 
	       SampleTimeSteps.Num(), GlobalFirstTimeStep, GlobalLastTimeStep);

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
		UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Set Niagara system template: %s. Make sure it has a '%s' User Parameter (Niagara Float3 Array type)."), 
		       *NiagaraSystemTemplate->GetName(), *PositionArrayParameterName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: No Niagara system template set. Assign NiagaraSystemTemplate in Blueprint or editor."));
	}
}
