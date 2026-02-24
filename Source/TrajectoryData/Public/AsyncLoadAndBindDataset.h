// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncLoadAndBindDataset.generated.h"

class ADatasetVisualizationActor;

/** Delegate type used for the OnSuccess and OnFailure output execution pins */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadAndBindDatasetComplete);

/**
 * Async Blueprint action that loads a trajectory dataset and binds it to a
 * DatasetVisualizationActor's Niagara system without blocking the game thread.
 *
 * CPU-heavy data packing runs on a background thread pool thread; Niagara
 * binding and the output execution pins fire back on the game thread.
 *
 * Usage in Blueprint:
 * 1. Call the "Load And Bind Dataset" async node, supplying the actor and dataset index.
 * 2. Connect game logic to the OnSuccess pin – it fires once the dataset is fully bound.
 * 3. Connect error handling to the OnFailure pin.
 */
UCLASS()
class TRAJECTORYDATA_API UAsyncLoadAndBindDataset : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Fires on the game thread when the dataset has been successfully loaded and bound */
	UPROPERTY(BlueprintAssignable)
	FOnLoadAndBindDatasetComplete OnSuccess;

	/** Fires on the game thread when loading or binding failed */
	UPROPERTY(BlueprintAssignable)
	FOnLoadAndBindDatasetComplete OnFailure;

	/**
	 * Load a trajectory dataset and bind it to a DatasetVisualizationActor asynchronously.
	 *
	 * The CPU-heavy data packing runs on a background thread so the game thread is not
	 * blocked.  Use the OnSuccess execution pin to know when the dataset is ready and
	 * visualization can begin.
	 *
	 * @param VisualizationActor  The actor that owns the Niagara system to bind the data to
	 * @param DatasetIndex        Index of the dataset to load (from the loaded datasets list)
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Visualization",
		meta = (BlueprintInternalUseOnly = "true", DisplayName = "Load And Bind Dataset"))
	static UAsyncLoadAndBindDataset* LoadAndBindDataset(
		ADatasetVisualizationActor* VisualizationActor,
		int32 DatasetIndex);

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;

private:
	UPROPERTY()
	TObjectPtr<ADatasetVisualizationActor> VisualizationActor;

	int32 DatasetIndex = 0;
};
