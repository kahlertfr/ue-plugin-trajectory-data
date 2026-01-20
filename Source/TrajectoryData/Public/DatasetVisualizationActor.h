// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "TrajectoryBufferProvider.h"
#include "NiagaraDataInterfaceTrajectoryBuffer.h"
#include "DatasetVisualizationActor.generated.h"

/**
 * Actor for visualizing trajectory datasets in Niagara with custom NDI
 * Provides complete GPU buffer binding for rendering trajectories as ribbons
 * 
 * This actor uses a custom lightweight Niagara Data Interface (NDI) to expose
 * trajectory position data directly to Niagara HLSL, enabling full GPU-based
 * trajectory visualization without manual buffer binding.
 * 
 * Features:
 * - Blueprint-spawnable and extensible
 * - Custom NDI for direct GPU buffer access in HLSL
 * - Automatic NDI configuration and binding
 * - Automatic metadata parameter passing
 * - Support for ribbon rendering
 * - Real-time dataset switching
 * 
 * Usage in Blueprint:
 * 1. Add this actor to level or spawn in Blueprint
 * 2. Set NiagaraSystem template (must include TrajectoryBuffer User Parameter)
 * 3. Call LoadAndBindDataset(DatasetIndex) in BeginPlay
 * 4. Niagara HLSL can use: TrajectoryBuffer.GetPositionAtIndex(), etc.
 * 
 * HLSL Functions Available:
 * - TrajectoryBuffer.GetPositionAtIndex(int Index) → float3
 * - TrajectoryBuffer.GetNumPositions() → int
 * - TrajectoryBuffer.GetTrajectoryStartIndex(int TrajIndex) → int
 * - TrajectoryBuffer.GetTrajectorySampleCount(int TrajIndex) → int
 * - TrajectoryBuffer.GetNumTrajectories() → int
 * - TrajectoryBuffer.GetMaxSamplesPerTrajectory() → int
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
	 * Bind buffers to Niagara system using custom NDI
	 * This is the core C++ functionality that enables Blueprint workflows
	 * 
	 * @return True if successful
	 */
	bool BindBuffersToNiagara();

	/**
	 * Pass metadata parameters to Niagara
	 * 
	 * @return True if successful
	 */
	bool PassMetadataToNiagara();

	/**
	 * Initialize the Niagara component and NDI
	 */
	void InitializeNiagaraComponent();

	/**
	 * Configure the trajectory buffer NDI with our buffer provider
	 * 
	 * @return True if successful
	 */
	bool ConfigureTrajectoryBufferNDI();

public:
	/** Niagara system template (set in Blueprint or editor) - must have TrajectoryBuffer User Parameter (NDI type) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization")
	TObjectPtr<UNiagaraSystem> NiagaraSystemTemplate;

	/** Name of Trajectory Buffer NDI parameter in Niagara (User Parameter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Visualization")
	FName TrajectoryBufferNDIParameterName = TEXT("TrajectoryBuffer");

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
