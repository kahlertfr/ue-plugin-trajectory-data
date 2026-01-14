// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataMemoryEstimator.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CoreDelegates.h"

// Initialize singleton instance
UTrajectoryDataMemoryEstimator* UTrajectoryDataMemoryEstimator::Instance = nullptr;

UTrajectoryDataMemoryEstimator::UTrajectoryDataMemoryEstimator()
	: EstimatedMemoryUsage(0)
{
}

UTrajectoryDataMemoryEstimator* UTrajectoryDataMemoryEstimator::Get()
{
	if (Instance == nullptr)
	{
		Instance = NewObject<UTrajectoryDataMemoryEstimator>();
		Instance->AddToRoot(); // Prevent garbage collection
	}
	return Instance;
}

int64 UTrajectoryDataMemoryEstimator::GetTotalPhysicalMemory()
{
	FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	return static_cast<int64>(MemoryStats.TotalPhysical);
}

int64 UTrajectoryDataMemoryEstimator::GetMaxTrajectoryDataMemory()
{
	// Allow 75% of total physical memory for trajectory data
	int64 TotalMemory = GetTotalPhysicalMemory();
	return static_cast<int64>(TotalMemory * 0.75);
}

int64 UTrajectoryDataMemoryEstimator::CalculateDatasetMemoryFromMetadata(const FTrajectoryDatasetMetadata& DatasetMetadata)
{
	// Based on the Trajectory Dataset specification:
	
	// 1. Dataset Meta: 76 bytes (fixed size)
	const int64 DatasetMetaSize = 76;
	
	// 2. Trajectory Meta: 40 bytes per trajectory
	const int64 TrajectoryMetaSize = 40 * DatasetMetadata.TrajectoryCount;
	
	// 3. Data Block Header: 32 bytes per data file
	// For simplicity, assume one data file per dataset
	const int64 DataBlockHeaderSize = 32;
	
	// 4. Data entries: entry_size_bytes per trajectory
	// entry_size_bytes is stored in the dataset metadata
	const int64 DataEntriesSize = static_cast<int64>(DatasetMetadata.EntrySizeBytes) * DatasetMetadata.TrajectoryCount;
	
	// Total memory required
	const int64 TotalMemory = DatasetMetaSize + TrajectoryMetaSize + DataBlockHeaderSize + DataEntriesSize;
	
	return TotalMemory;
}

int64 UTrajectoryDataMemoryEstimator::CalculateDatasetMemoryRequirement(const FTrajectoryDatasetInfo& DatasetInfo)
{
	// Calculate memory requirement for the single dataset
	return CalculateDatasetMemoryFromMetadata(DatasetInfo.Metadata);
}

FTrajectoryDataMemoryInfo UTrajectoryDataMemoryEstimator::GetMemoryInfo() const
{
	FTrajectoryDataMemoryInfo MemoryInfo;
	
	const int64 TotalPhysicalMemory = GetTotalPhysicalMemory();
	const int64 MaxTrajectoryDataMemory = GetMaxTrajectoryDataMemory();
	const int64 RemainingCapacity = MaxTrajectoryDataMemory - EstimatedMemoryUsage;
	
	// Convert bytes to gigabytes (GB)
	constexpr double BytesToGB = 1.0 / (1024.0 * 1024.0 * 1024.0);
	
	MemoryInfo.TotalPhysicalMemoryGB = static_cast<float>(TotalPhysicalMemory * BytesToGB);
	MemoryInfo.MaxTrajectoryDataMemoryGB = static_cast<float>(MaxTrajectoryDataMemory * BytesToGB);
	MemoryInfo.CurrentEstimatedUsageGB = static_cast<float>(EstimatedMemoryUsage * BytesToGB);
	MemoryInfo.RemainingCapacityGB = static_cast<float>(RemainingCapacity * BytesToGB);
	
	// Calculate usage percentage
	if (MaxTrajectoryDataMemory > 0)
	{
		MemoryInfo.UsagePercentage = (static_cast<float>(EstimatedMemoryUsage) / 
		                              static_cast<float>(MaxTrajectoryDataMemory)) * 100.0f;
	}
	else
	{
		MemoryInfo.UsagePercentage = 0.0f;
	}
	
	return MemoryInfo;
}

void UTrajectoryDataMemoryEstimator::AddEstimatedUsage(int64 MemoryBytes)
{
	EstimatedMemoryUsage += MemoryBytes;
	
	// Clamp to non-negative values
	if (EstimatedMemoryUsage < 0)
	{
		EstimatedMemoryUsage = 0;
	}
}

void UTrajectoryDataMemoryEstimator::RemoveEstimatedUsage(int64 MemoryBytes)
{
	EstimatedMemoryUsage -= MemoryBytes;
	
	// Clamp to non-negative values
	if (EstimatedMemoryUsage < 0)
	{
		EstimatedMemoryUsage = 0;
	}
}

void UTrajectoryDataMemoryEstimator::ResetEstimatedUsage()
{
	EstimatedMemoryUsage = 0;
}

bool UTrajectoryDataMemoryEstimator::CanLoadDatasetFromMetadata(const FTrajectoryDatasetMetadata& DatasetMetadata) const
{
	int64 RequiredMemory = CalculateDatasetMemoryFromMetadata(DatasetMetadata);
	int64 MaxMemory = GetMaxTrajectoryDataMemory();
	int64 RemainingCapacity = MaxMemory - EstimatedMemoryUsage;
	
	return RequiredMemory <= RemainingCapacity;
}

bool UTrajectoryDataMemoryEstimator::CanLoadDataset(const FTrajectoryDatasetInfo& DatasetInfo) const
{
	int64 RequiredMemory = CalculateDatasetMemoryRequirement(DatasetInfo);
	int64 MaxMemory = GetMaxTrajectoryDataMemory();
	int64 RemainingCapacity = MaxMemory - EstimatedMemoryUsage;
	
	return RequiredMemory <= RemainingCapacity;
}
