// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataBlueprintLibrary.h"
#include "TrajectoryDataManager.h"
#include "TrajectoryDataSettings.h"

bool UTrajectoryDataBlueprintLibrary::ScanTrajectoryDatasets()
{
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	if (Manager)
	{
		return Manager->ScanDatasets();
	}
	return false;
}

TArray<FTrajectoryDatasetInfo> UTrajectoryDataBlueprintLibrary::GetAvailableDatasets()
{
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	if (Manager)
	{
		return Manager->GetAvailableDatasets();
	}
	return TArray<FTrajectoryDatasetInfo>();
}

bool UTrajectoryDataBlueprintLibrary::GetDatasetInfo(const FString& DatasetName, FTrajectoryDatasetInfo& OutDatasetInfo)
{
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	if (Manager)
	{
		return Manager->GetDatasetInfo(DatasetName, OutDatasetInfo);
	}
	return false;
}

int32 UTrajectoryDataBlueprintLibrary::GetNumDatasets()
{
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	if (Manager)
	{
		return Manager->GetNumDatasets();
	}
	return 0;
}

void UTrajectoryDataBlueprintLibrary::ClearDatasets()
{
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	if (Manager)
	{
		Manager->ClearDatasets();
	}
}

FString UTrajectoryDataBlueprintLibrary::GetDatasetsDirectory()
{
	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
	if (Settings)
	{
		return Settings->DatasetsDirectory;
	}
	return FString();
}

void UTrajectoryDataBlueprintLibrary::SetDatasetsDirectory(const FString& NewPath)
{
	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
	if (Settings)
	{
		Settings->DatasetsDirectory = NewPath;
		// Note: SaveConfig is called explicitly here. Consider calling SaveConfig manually
		// after multiple setting changes if you're updating multiple properties at once.
		Settings->SaveConfig();
	}
}

int32 UTrajectoryDataBlueprintLibrary::CalculateMaxDisplayPoints(const FTrajectoryDatasetInfo& DatasetInfo)
{
	int32 TotalPoints = 0;
	for (const FTrajectoryShardMetadata& Shard : DatasetInfo.Shards)
	{
		TotalPoints += Shard.NumTrajectories * Shard.NumSamples;
	}
	return TotalPoints;
}

int32 UTrajectoryDataBlueprintLibrary::CalculateShardDisplayPoints(const FTrajectoryShardMetadata& ShardMetadata)
{
	return ShardMetadata.NumTrajectories * ShardMetadata.NumSamples;
}
