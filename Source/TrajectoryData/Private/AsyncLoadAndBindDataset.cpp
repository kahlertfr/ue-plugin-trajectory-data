// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncLoadAndBindDataset.h"
#include "DatasetVisualizationActor.h"

UAsyncLoadAndBindDataset* UAsyncLoadAndBindDataset::LoadAndBindDataset(
	ADatasetVisualizationActor* InVisualizationActor,
	int32 InDatasetIndex)
{
	UAsyncLoadAndBindDataset* Action = NewObject<UAsyncLoadAndBindDataset>();
	Action->VisualizationActor = InVisualizationActor;
	Action->DatasetIndex = InDatasetIndex;
	// Prevent the action from being garbage-collected before it completes
	Action->RegisterWithGameInstance(InVisualizationActor);
	return Action;
}

void UAsyncLoadAndBindDataset::Activate()
{
	TWeakObjectPtr<ADatasetVisualizationActor> WeakActor(VisualizationActor.Get());
	if (!WeakActor.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncLoadAndBindDataset: VisualizationActor is null or invalid"));
		OnFailure.Broadcast();
		return;
	}

	TWeakObjectPtr<UAsyncLoadAndBindDataset> WeakThis(this);

	// Delegate to the actor's internal async method which manages background packing
	// and game-thread Niagara binding.
	WeakActor->LoadAndBindDatasetAsync(DatasetIndex,
		[WeakThis](bool bSuccess)
		{
			// Runs on the game thread
			if (!WeakThis.IsValid())
			{
				return;
			}

			if (bSuccess)
			{
				WeakThis->OnSuccess.Broadcast();
			}
			else
			{
				WeakThis->OnFailure.Broadcast();
			}
		});
}
