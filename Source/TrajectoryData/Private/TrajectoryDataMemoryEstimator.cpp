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

int64 UTrajectoryDataMemoryEstimator::CalculateShardMemoryRequirement(const FTrajectoryShardMetadata& ShardMetadata)
{
	// Based on the Trajectory Data Shard specification:
	
	// 1. Shard Meta: 76 bytes (fixed size)
	const int64 ShardMetaSize = 76;
	
	// 2. Trajectory Meta: 40 bytes per trajectory
	const int64 TrajectoryMetaSize = 40 * ShardMetadata.TrajectoryCount;
	
	// 3. Data Block Header: 32 bytes per data file
	// For simplicity, assume one data file per shard
	const int64 DataBlockHeaderSize = 32;
	
	// 4. Data entries: entry_size_bytes per trajectory
	// entry_size_bytes is stored in the shard metadata
	const int64 DataEntriesSize = static_cast<int64>(ShardMetadata.EntrySizeBytes) * ShardMetadata.TrajectoryCount;
	
	// Total memory required
	const int64 TotalMemory = ShardMetaSize + TrajectoryMetaSize + DataBlockHeaderSize + DataEntriesSize;
	
	return TotalMemory;
}

int64 UTrajectoryDataMemoryEstimator::CalculateDatasetMemoryRequirement(const FTrajectoryDatasetInfo& DatasetInfo)
{
	int64 TotalMemory = 0;
	
	// Sum up memory requirements for all shards
	for (const FTrajectoryShardMetadata& Shard : DatasetInfo.Shards)
	{
		TotalMemory += CalculateShardMemoryRequirement(Shard);
	}
	
	return TotalMemory;
}

FTrajectoryDataMemoryInfo UTrajectoryDataMemoryEstimator::GetMemoryInfo() const
{
	FTrajectoryDataMemoryInfo MemoryInfo;
	
	MemoryInfo.TotalPhysicalMemory = GetTotalPhysicalMemory();
	MemoryInfo.MaxTrajectoryDataMemory = GetMaxTrajectoryDataMemory();
	MemoryInfo.CurrentEstimatedUsage = EstimatedMemoryUsage;
	MemoryInfo.RemainingCapacity = MemoryInfo.MaxTrajectoryDataMemory - MemoryInfo.CurrentEstimatedUsage;
	
	// Calculate usage percentage
	if (MemoryInfo.MaxTrajectoryDataMemory > 0)
	{
		MemoryInfo.UsagePercentage = (static_cast<float>(MemoryInfo.CurrentEstimatedUsage) / 
		                              static_cast<float>(MemoryInfo.MaxTrajectoryDataMemory)) * 100.0f;
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

bool UTrajectoryDataMemoryEstimator::CanLoadShard(const FTrajectoryShardMetadata& ShardMetadata) const
{
	int64 RequiredMemory = CalculateShardMemoryRequirement(ShardMetadata);
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
