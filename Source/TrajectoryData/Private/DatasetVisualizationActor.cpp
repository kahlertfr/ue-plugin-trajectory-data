// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasetVisualizationActor.h"
#include "TrajectoryDataLoader.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Async/ParallelFor.h"
#include "Components/BoxComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	// Requirement: visualization load-and-bind should use at most 250 trajectories.
	constexpr int32 DefaultMaxVisualizationTrajectories = 250;

	FBox DatasetDisplayBounds(const FBox& Samples, const FBox& MetadataBounds, const FMatrix& Matrix)
	{
		if (!Samples.IsValid) return FBox(ForceInit);
		const double Scale = Matrix.M[0][0];
		const FVector Center = MetadataBounds.GetCenter();
		const FVector Translation(Matrix.M[0][3], Matrix.M[1][3],
			Matrix.M[2][3] + Scale * MetadataBounds.GetSize().Y * 0.5);
		FBox Display(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector P = FVector(Corner & 1 ? Samples.Max.X : Samples.Min.X,
				Corner & 2 ? Samples.Max.Y : Samples.Min.Y, Corner & 4 ? Samples.Max.Z : Samples.Min.Z) - Center;
			Display += FVector(P.X, -P.Z, P.Y) * Scale + Translation;
		}
		return Display;
	}

	FBox CenterDatasetCollisionXY(const FBox& Display, const FVector& VisualizationOrigin)
	{
		FVector Center = Display.GetCenter();
		// The matrix translation includes dataset-centering terms; those are not
		// the visualization's world origin. Keep horizontal placement at the actor.
		Center.X = VisualizationOrigin.X;
		Center.Y = VisualizationOrigin.Y;
		return FBox(Center - Display.GetExtent(), Center + Display.GetExtent());
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDatasetDisplayBoundsTest, "TrajectoryBLab.Dataset.DisplayBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDatasetDisplayBoundsTest::RunTest(const FString& Parameters)
{
	FMatrix Matrix = FMatrix::Identity;
	Matrix.M[0][0] = 2;
	Matrix.M[0][3] = 100;
	Matrix.M[1][3] = 200;
	Matrix.M[2][3] = 300;
	const FBox Metadata(FVector(10,20,30), FVector(20,60,50));
	const FBox Samples(FVector(12,30,35), FVector(18,50,45));
	const FBox Display = DatasetDisplayBounds(Samples, Metadata, Matrix);
	TestTrue(TEXT("Extent follows sample bounds with Y/Z conversion"), Display.GetExtent().Equals(FVector(6,10,20)));
	TestTrue(TEXT("Nonzero dataset origin and placement match Niagara"), Display.GetCenter().Equals(FVector(100,200,340)));
	const FBox Flat = DatasetDisplayBounds(FBox(FVector(12,30,35), FVector(18,30,45)), Metadata, Matrix);
	TestEqual(TEXT("Planar data has finite zero height"), Flat.GetExtent().Z, 0.0);
	TestFalse(TEXT("Empty data does not produce a box"), DatasetDisplayBounds(FBox(ForceInit), Metadata, Matrix).IsValid != 0);
	const FVector Origin(500, -120, 40);
	const FBox Collision = CenterDatasetCollisionXY(Display, Origin);
	TestTrue(TEXT("Collision is centered on visualization origin in X and Y"),
		Collision.GetCenter().Equals(FVector(500, -120, 340)));
	TestTrue(TEXT("Centering preserves the fitted data extent"), Collision.GetExtent().Equals(Display.GetExtent()));
	TestEqual(TEXT("Centering preserves vertical placement"), Collision.Min.Z, Display.Min.Z);
	TestTrue(TEXT("Axes corner follows the centered collision box"), Collision.Min.Equals(FVector(494, -130, 320)));
	return true;
}
#endif

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
	return LoadAndBindDatasetWithTrajectoryCap(DatasetIndex, DefaultMaxVisualizationTrajectories);
}

bool ADatasetVisualizationActor::LoadAndBindDatasetWithTrajectoryCap(int32 DatasetIndex, int32 MaxTrajectories)
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

	// Load data into buffer (with optional trajectory cap)
	if (!BufferProvider->UpdateFromDatasetWithTrajectoryCap(DatasetIndex, MaxTrajectories))
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

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully loaded and bound dataset %d using Position Array NDI (cap: %d)"),
		DatasetIndex, MaxTrajectories);
	return true;
}

void ADatasetVisualizationActor::LoadAndBindDatasetAsync(int32 DatasetIndex, TFunction<void(bool)> OnComplete)
{
	LoadAndBindDatasetWithTrajectoryCapAsync(DatasetIndex, DefaultMaxVisualizationTrajectories, MoveTemp(OnComplete));
}

void ADatasetVisualizationActor::LoadAndBindDatasetWithTrajectoryCapAsync(int32 DatasetIndex, int32 MaxTrajectories, TFunction<void(bool)> OnComplete)
{
	if (!BufferProvider)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: BufferProvider is null"));
		OnComplete(false);
		return;
	}

	if (!NiagaraComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: NiagaraComponent is null"));
		OnComplete(false);
		return;
	}

	TWeakObjectPtr<ADatasetVisualizationActor> WeakThis(this);

	// UpdateFromDatasetAsync packs data on a background thread and calls back on the game thread
	BufferProvider->UpdateFromDatasetWithTrajectoryCapAsync(DatasetIndex, MaxTrajectories,
		[WeakThis, DatasetIndex, MaxTrajectories, OnComplete](bool bUpdateSuccess)
		{
			// Runs on game thread
			if (!bUpdateSuccess)
			{
				UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Failed to load dataset %d"), DatasetIndex);
				OnComplete(false);
				return;
			}

			if (!WeakThis.IsValid())
			{
				OnComplete(false);
				return;
			}

			ADatasetVisualizationActor* This = WeakThis.Get();

			// Populate the Position Array NDI with position data
			if (!This->PopulatePositionArrayNDI())
			{
				UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: Failed to populate Position Array NDI"));
				OnComplete(false);
				return;
			}

			// Populate TrajectoryInfo arrays if enabled
			if (This->bTransferTrajectoryInfo)
			{
				if (!This->PopulateTrajectoryInfoArrays())
				{
					UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Failed to populate TrajectoryInfo arrays (non-critical)"));
				}
			}

			// Populate SampleTimeSteps array
			if (!This->PopulateSampleTimeStepsArray())
			{
				UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Failed to populate SampleTimeSteps array (non-critical)"));
			}

			// Pass metadata parameters
			if (!This->PassMetadataToNiagara())
			{
				UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: Failed to pass metadata to Niagara (non-critical)"));
			}

			// Activate Niagara if requested
			if (This->bAutoActivate && !This->NiagaraComponent->IsActive())
			{
				This->NiagaraComponent->Activate(true);
			}

			This->bBuffersBound = true;
			This->CurrentDatasetIndex = DatasetIndex;

			UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully loaded and bound dataset %d asynchronously (cap: %d)"),
				DatasetIndex, MaxTrajectories);

			OnComplete(true);
		});
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

void ADatasetVisualizationActor::FitDatasetCollisionBox(UBoxComponent* Box, USceneComponent* Axes, const FMatrix& TransformationMatrix)
{
	if (!Box || !VisualizedSampleBounds.IsValid) return;
	const auto Metadata = GetDatasetMetadata();
	const FBox Display = CenterDatasetCollisionXY(
		DatasetDisplayBounds(VisualizedSampleBounds, FBox(Metadata.BoundsMin, Metadata.BoundsMax), TransformationMatrix),
		GetActorLocation());
	const FVector WorldScale = Box->GetComponentScale().GetAbs();
	const FVector Extent = Display.GetExtent();
	const FVector LocalExtent(Extent.X / FMath::Max(WorldScale.X, UE_SMALL_NUMBER),
		Extent.Y / FMath::Max(WorldScale.Y, UE_SMALL_NUMBER), Extent.Z / FMath::Max(WorldScale.Z, UE_SMALL_NUMBER));
	Box->SetWorldRotation(FQuat::Identity);
	Box->SetWorldLocation(Display.GetCenter());
	Box->SetBoxExtent(LocalExtent, true);
	if (Axes) Axes->SetWorldLocation(Display.Min);
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
	// NOTE: This function runs on the GAME THREAD
	// All array population for Niagara happens here, never on the render thread
	
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
	const TArray<FVector3f>& AllPositions3f = BufferProvider->GetAllPositionsRef();
	
	if (AllPositions3f.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DatasetVisualizationActor: No positions available"));
		return false;
	}

	// GAME THREAD: Convert FVector3f to FVector for Niagara API
	// Using ParallelFor to avoid frame drop for large datasets
	// Note: Niagara's SetNiagaraArrayPosition expects TArray<FVector> (double precision)
	// even though internally it may use float precision
	TArray<FVector> AllPositions;
	AllPositions.SetNumUninitialized(AllPositions3f.Num());
	
	// Use parallel processing to convert millions of positions without stalling the frame
	// This distributes the work across multiple threads
	constexpr int32 ChunkSize = 4096;
	TArray<FBox> ChunkBounds;
	ChunkBounds.SetNum(FMath::DivideAndRoundUp(AllPositions3f.Num(), ChunkSize));
	ParallelFor(ChunkBounds.Num(), [&AllPositions, &AllPositions3f, &ChunkBounds](int32 Chunk)
	{
		FBox Bounds(ForceInit);
		const int32 End = FMath::Min((Chunk + 1) * ChunkSize, AllPositions3f.Num());
		for (int32 Index = Chunk * ChunkSize; Index < End; ++Index)
		{
			AllPositions[Index] = FVector(AllPositions3f[Index]);
			Bounds += AllPositions[Index];
		}
		ChunkBounds[Chunk] = Bounds;
	});
	VisualizedSampleBounds = FBox(ForceInit);
	for (const FBox& Bounds : ChunkBounds) VisualizedSampleBounds += Bounds;

	// GAME THREAD: Transfer to Niagara
	// SetNiagaraArrayPosition runs on the game thread and handles internal Niagara updates
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
	// NOTE: This function runs on the GAME THREAD
	// All array population for Niagara happens here, never on the render thread
	
	if (!BufferProvider || !NiagaraComponent)
	{
		return false;
	}

	// Get trajectory info array from buffer provider (const reference - no copy!)
	const TArray<FTrajectoryBufferInfo>& TrajectoryInfo = BufferProvider->GetTrajectoryInfoRef();
	
	if (TrajectoryInfo.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: No trajectory info available"));
		return false;
	}

	// GAME THREAD: Prepare arrays for each field
	// Using SetNumUninitialized and ParallelFor to avoid frame drops
	TArray<int32> TrajectoryId;
	TArray<int32> StartIndex;
	TArray<FVector> Extent;  // Using FVector for Niagara compatibility

	// Pre-allocate arrays
	const int32 NumTrajectories = TrajectoryInfo.Num();
	TrajectoryId.SetNumUninitialized(NumTrajectories);
	StartIndex.SetNumUninitialized(NumTrajectories);
	Extent.SetNumUninitialized(NumTrajectories);

	// Pack data into arrays using parallel processing
	ParallelFor(NumTrajectories, [&](int32 Index)
	{
		const FTrajectoryBufferInfo& Info = TrajectoryInfo[Index];
		TrajectoryId[Index] = Info.TrajectoryId;
		StartIndex[Index] = Info.StartIndex;
		Extent[Index] = FVector(Info.Extent);  // Convert FVector3f to FVector
	});

	// GAME THREAD: Transfer arrays to Niagara
	// SetNiagaraArray* functions run on the game thread
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

	UE_LOG(LogTemp, Log, TEXT("DatasetVisualizationActor: Successfully populated TrajectoryInfo arrays with %d trajectories"), 
	       NumTrajectories);

	return true;
}

bool ADatasetVisualizationActor::PopulateSampleTimeStepsArray()
{
	// NOTE: This function runs on the GAME THREAD
	// All array population for Niagara happens here, never on the render thread
	
	if (!BufferProvider || !NiagaraComponent)
	{
		return false;
	}

	// Get sample time steps array from buffer provider (const reference - no copy!)
	const TArray<int32>& SampleTimeSteps = BufferProvider->GetSampleTimeStepsRef();
	
	if (SampleTimeSteps.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DatasetVisualizationActor: No sample time steps available"));
		return false;
	}

	// GAME THREAD: Transfer SampleTimeSteps array to Niagara
	// SetNiagaraArrayInt32 runs on the game thread
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
		NiagaraComponent, 
		TEXT("SampleTimeSteps"), 
		SampleTimeSteps
	);

	// Get global time step range from metadata (already computed during dataset loading)
	// This avoids iterating through 20M time steps on the game thread
	FTrajectoryBufferMetadata Metadata = BufferProvider->GetMetadata();
	int32 GlobalFirstTimeStep = Metadata.FirstTimeStep;
	int32 GlobalLastTimeStep = Metadata.LastTimeStep;

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
