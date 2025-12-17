// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataBlueprintLibrary.h"
#include "TrajectoryDataManager.h"
#include "TrajectoryDataSettings.h"
#include "TrajectoryDataMemoryEstimator.h"

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

FString UTrajectoryDataBlueprintLibrary::GetScenariosDirectory()
{
	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
	if (Settings)
	{
		return Settings->ScenariosDirectory;
	}
	return FString();
}

void UTrajectoryDataBlueprintLibrary::SetScenariosDirectory(const FString& NewPath)
{
	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
	if (Settings)
	{
		Settings->ScenariosDirectory = NewPath;
		// Note: SaveConfig is called explicitly here. Consider calling SaveConfig manually
		// after multiple setting changes if you're updating multiple properties at once.
		Settings->SaveConfig();
	}
}

int32 UTrajectoryDataBlueprintLibrary::CalculateMaxDisplayPoints(const FTrajectoryDatasetInfo& DatasetInfo)
{
	int64 TotalPoints = 0;
	for (const FTrajectoryShardMetadata& Shard : DatasetInfo.Shards)
	{
		// Each trajectory has TimeStepIntervalSize samples
		TotalPoints += Shard.TrajectoryCount * Shard.TimeStepIntervalSize;
	}
	// Clamp to int32 range for Blueprint compatibility
	return (int32)FMath::Min(TotalPoints, (int64)MAX_int32);
}

int32 UTrajectoryDataBlueprintLibrary::CalculateShardDisplayPoints(const FTrajectoryShardMetadata& ShardMetadata)
{
	int64 TotalPoints = ShardMetadata.TrajectoryCount * ShardMetadata.TimeStepIntervalSize;
	// Clamp to int32 range for Blueprint compatibility
	return (int32)FMath::Min(TotalPoints, (int64)MAX_int32);
}

// Memory Monitoring Functions

int64 UTrajectoryDataBlueprintLibrary::GetTotalPhysicalMemory()
{
	return UTrajectoryDataMemoryEstimator::GetTotalPhysicalMemory();
}

int64 UTrajectoryDataBlueprintLibrary::GetMaxTrajectoryDataMemory()
{
	return UTrajectoryDataMemoryEstimator::GetMaxTrajectoryDataMemory();
}

int64 UTrajectoryDataBlueprintLibrary::CalculateShardMemoryRequirement(const FTrajectoryShardMetadata& ShardMetadata)
{
	return UTrajectoryDataMemoryEstimator::CalculateShardMemoryRequirement(ShardMetadata);
}

int64 UTrajectoryDataBlueprintLibrary::CalculateDatasetMemoryRequirement(const FTrajectoryDatasetInfo& DatasetInfo)
{
	return UTrajectoryDataMemoryEstimator::CalculateDatasetMemoryRequirement(DatasetInfo);
}

FTrajectoryDataMemoryInfo UTrajectoryDataBlueprintLibrary::GetMemoryInfo()
{
	UTrajectoryDataMemoryEstimator* Estimator = UTrajectoryDataMemoryEstimator::Get();
	if (Estimator)
	{
		return Estimator->GetMemoryInfo();
	}
	return FTrajectoryDataMemoryInfo();
}

void UTrajectoryDataBlueprintLibrary::AddEstimatedUsage(int64 MemoryBytes)
{
	UTrajectoryDataMemoryEstimator* Estimator = UTrajectoryDataMemoryEstimator::Get();
	if (Estimator)
	{
		Estimator->AddEstimatedUsage(MemoryBytes);
	}
}

void UTrajectoryDataBlueprintLibrary::RemoveEstimatedUsage(int64 MemoryBytes)
{
	UTrajectoryDataMemoryEstimator* Estimator = UTrajectoryDataMemoryEstimator::Get();
	if (Estimator)
	{
		Estimator->RemoveEstimatedUsage(MemoryBytes);
	}
}

void UTrajectoryDataBlueprintLibrary::ResetEstimatedUsage()
{
	UTrajectoryDataMemoryEstimator* Estimator = UTrajectoryDataMemoryEstimator::Get();
	if (Estimator)
	{
		Estimator->ResetEstimatedUsage();
	}
}

bool UTrajectoryDataBlueprintLibrary::CanLoadShard(const FTrajectoryShardMetadata& ShardMetadata)
{
	UTrajectoryDataMemoryEstimator* Estimator = UTrajectoryDataMemoryEstimator::Get();
	if (Estimator)
	{
		return Estimator->CanLoadShard(ShardMetadata);
	}
	return false;
}

bool UTrajectoryDataBlueprintLibrary::CanLoadDataset(const FTrajectoryDatasetInfo& DatasetInfo)
{
	UTrajectoryDataMemoryEstimator* Estimator = UTrajectoryDataMemoryEstimator::Get();
	if (Estimator)
	{
		return Estimator->CanLoadDataset(DatasetInfo);
	}
	return false;
}

FString UTrajectoryDataBlueprintLibrary::FormatMemorySize(int64 Bytes)
{
	const double KB = 1024.0;
	const double MB = KB * 1024.0;
	const double GB = MB * 1024.0;
	const double TB = GB * 1024.0;

	if (Bytes >= TB)
	{
		return FString::Printf(TEXT("%.2f TB"), Bytes / TB);
	}
	else if (Bytes >= GB)
	{
		return FString::Printf(TEXT("%.2f GB"), Bytes / GB);
	}
	else if (Bytes >= MB)
	{
		return FString::Printf(TEXT("%.2f MB"), Bytes / MB);
	}
	else if (Bytes >= KB)
	{
		return FString::Printf(TEXT("%.2f KB"), Bytes / KB);
	}
	else
	{
		return FString::Printf(TEXT("%lld Bytes"), Bytes);
	}
}
