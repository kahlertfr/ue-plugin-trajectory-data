// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "TrajectoryBufferProvider.h"
#include "DatasetVisualizationActor.generated.h"

/**
 * Actor for visualizing trajectory datasets in Niagara using built-in Position Array NDI
 * Provides complete GPU buffer access for rendering trajectories as ribbons
 * 
 * This actor uses UE5's built-in UNiagaraDataInterfaceArrayFloat3 to expose
 * trajectory position data directly to Niagara HLSL, enabling full GPU-based
 * trajectory visualization without custom NDI registration issues.
 * 
 * Features:
 * - Blueprint-spawnable and extensible
 * - Built-in Position Array NDI for direct GPU buffer access in HLSL
 * - Automatic NDI configuration and data population
 * - Automatic metadata parameter passing
 * - Support for ribbon rendering
 * - Real-time dataset switching
 * - Works across all UE5+ versions
 * 
 * Usage in Blueprint:
 * 1. Add this actor to level or spawn in Blueprint
 * 2. Set NiagaraSystem template (must include PositionArray User Parameter of type Float3 Array)
 * 3. Call LoadAndBindDataset(DatasetIndex) in BeginPlay
 * 4. Niagara HLSL can use: PositionArray.Get(Index), PositionArray.Length(), etc.
 * 
 * HLSL Functions Available (built-in Float3 Array NDI):
 * - PositionArray.Get(int Index) → float3
 * - PositionArray.Length() → int
 * - Use metadata parameters for trajectory-specific information
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (TrajectoryData))
class TRAJECTORYDATA_API ADatasetVisualizationActor : public AActor
{
	GENERATED_BODY()

public:
	ADatasetVisualizationActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/**
	 * Load trajectory dataset and bind to Niagara system
	 * This is the main Blueprint-callable function that does everything
	 * 
	 * @param DatasetIndex Index of dataset to load
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Visualization")
	bool LoadAndBindDataset(int32 DatasetIndex);

	/**
	 * Update visualization to a different dataset
	 * Can be called at runtime to switch datasets
	 * 
	 * @param DatasetIndex New dataset index
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Visualization")
	bool SwitchToDataset(int32 DatasetIndex);

	/**
	 * Check if visualization is ready
	 * 
	 * @return True if buffers are loaded and bound
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Visualization")
	bool IsVisualizationReady() const;

	/**
	 * Get current dataset metadata
	 * 
	 * @return Buffer metadata structure
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Visualization")
	FTrajectoryBufferMetadata GetDatasetMetadata() const;

	/**
	 * Get trajectory information array
	 * 
	 * @return Array of trajectory info structures
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Visualization")
	TArray<FTrajectoryBufferInfo> GetTrajectoryInfoArray() const;

	/**
	 * Set Niagara system activation state
	 * 
	 * @param bActivate True to activate, false to deactivate
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Visualization")
	void SetVisualizationActive(bool bActivate);

	/**
	 * Get the Niagara component (for Blueprint customization)
	 * 
	 * @return Niagara component reference
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Visualization")
	UNiagaraComponent* GetNiagaraComponent() const { return NiagaraComponent; }

protected:
	/**
	 * Populate Position Array NDI with trajectory data
	 * This is the core C++ functionality that enables Blueprint workflows
	 * 
	 * @return True if successful
	 */
	bool PopulatePositionArrayNDI();

	/**
	 * Populate TrajectoryInfo arrays to Niagara
	 * Transfers trajectory metadata as separate int arrays for HLSL access
	 * 
	 * @return True if successful
	 */
	bool PopulateTrajectoryInfoArrays();

	/**
	 * Populate SampleTimeSteps array to Niagara
	 * Transfers time step for each sample point (aligned with position data)
	 * 
	 * @return True if successful
	 */
	bool PopulateSampleTimeStepsArray();

	/**
	 * Pass metadata parameters to Niagara
	 * 
	 * @return True if successful
	 */
	bool PassMetadataToNiagara();

	/**
	 * Initialize the Niagara component
	 */
	void InitializeNiagaraComponent();

public:
	/** Niagara system template (set in Blueprint or editor) - must have PositionArray User Parameter (Float3 Array type) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization")
	TObjectPtr<UNiagaraSystem> NiagaraSystemTemplate;

	/** Name of Position Array NDI parameter in Niagara (User Parameter of type Niagara Float3 Array) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization")
	FName PositionArrayParameterName = TEXT("PositionArray");

	/** Enable TrajectoryInfo array transfer to Niagara (requires Int Array User Parameters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization|TrajectoryInfo")
	bool bTransferTrajectoryInfo = true;

	/** Name prefix for TrajectoryInfo array parameters in Niagara (creates: <Prefix>StartIndex, <Prefix>SampleCount, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization|TrajectoryInfo")
	FName TrajectoryInfoParameterPrefix = TEXT("TrajInfo");

	/** Auto-activate Niagara system after loading dataset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization")
	bool bAutoActivate = true;

	/** Auto-load dataset on BeginPlay (set DatasetIndex in Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization")
	bool bAutoLoadOnBeginPlay = false;

	/** Dataset index to auto-load (if bAutoLoadOnBeginPlay is true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization", meta = (EditCondition = "bAutoLoadOnBeginPlay"))
	int32 AutoLoadDatasetIndex = 0;

protected:
	/** Niagara component for visualization */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> NiagaraComponent;

	/** Buffer provider component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTrajectoryBufferProvider> BufferProvider;

	/** Track if buffers are currently bound */
	bool bBuffersBound = false;

	/** Current dataset index */
	int32 CurrentDatasetIndex = -1;
};
