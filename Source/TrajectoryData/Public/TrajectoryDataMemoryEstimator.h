// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TrajectoryDataTypes.h"
#include "TrajectoryDataMemoryEstimator.generated.h"

/**
 * Structure representing memory usage information for trajectory data
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryDataMemoryInfo
{
	GENERATED_BODY()

	/** Total physical memory available on the system in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	int64 TotalPhysicalMemory;

	/** Maximum memory available for trajectory data (75% of total) in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	int64 MaxTrajectoryDataMemory;

	/** Currently estimated memory usage for loaded trajectory data in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	int64 CurrentEstimatedUsage;

	/** Remaining capacity for loading additional trajectory data in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	int64 RemainingCapacity;

	/** Percentage of available trajectory data memory currently used (0-100) */
	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	float UsagePercentage;

	FTrajectoryDataMemoryInfo()
		: TotalPhysicalMemory(0)
		, MaxTrajectoryDataMemory(0)
		, CurrentEstimatedUsage(0)
		, RemainingCapacity(0)
		, UsagePercentage(0.0f)
	{
	}
};

/**
 * Memory estimator for trajectory data
 * Calculates memory requirements based on the Trajectory Data Shard specification
 * and monitors available system capacity
 */
UCLASS()
class TRAJECTORYDATA_API UTrajectoryDataMemoryEstimator : public UObject
{
	GENERATED_BODY()

public:
	UTrajectoryDataMemoryEstimator();

	/**
	 * Get the total physical memory available on the system
	 * @return Total physical memory in bytes
	 */
	UFUNCTION(BlueprintCallable, Category = "Memory")
	static int64 GetTotalPhysicalMemory();

	/**
	 * Get the maximum memory that can be used for trajectory data (75% of total)
	 * @return Maximum trajectory data memory in bytes
	 */
	UFUNCTION(BlueprintCallable, Category = "Memory")
	static int64 GetMaxTrajectoryDataMemory();

	/**
	 * Calculate memory required for a single shard
	 * Based on the specification:
	 * - Shard Meta: 76 bytes
	 * - Trajectory Meta: 40 bytes per trajectory
	 * - Data entries: entry_size_bytes per trajectory
	 * @param ShardMetadata The shard to calculate memory for
	 * @return Estimated memory required in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory")
	static int64 CalculateShardMemoryRequirement(const FTrajectoryShardMetadata& ShardMetadata);

	/**
	 * Calculate memory required for an entire dataset
	 * @param DatasetInfo The dataset to calculate memory for
	 * @return Estimated memory required in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory")
	static int64 CalculateDatasetMemoryRequirement(const FTrajectoryDatasetInfo& DatasetInfo);

	/**
	 * Get current memory usage information
	 * @return Structure containing memory usage details
	 */
	UFUNCTION(BlueprintCallable, Category = "Memory")
	FTrajectoryDataMemoryInfo GetMemoryInfo() const;

	/**
	 * Add estimated memory usage for loaded data
	 * Call this when planning to load data to update capacity estimates
	 * @param MemoryBytes Amount of memory to add to the estimate
	 */
	UFUNCTION(BlueprintCallable, Category = "Memory")
	void AddEstimatedUsage(int64 MemoryBytes);

	/**
	 * Remove estimated memory usage for unloaded data
	 * Call this when planning to unload data to update capacity estimates
	 * @param MemoryBytes Amount of memory to remove from the estimate
	 */
	UFUNCTION(BlueprintCallable, Category = "Memory")
	void RemoveEstimatedUsage(int64 MemoryBytes);

	/**
	 * Reset all estimated memory usage to zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Memory")
	void ResetEstimatedUsage();

	/**
	 * Check if loading a shard would exceed available capacity
	 * @param ShardMetadata The shard to check
	 * @return True if the shard can fit in remaining capacity
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory")
	bool CanLoadShard(const FTrajectoryShardMetadata& ShardMetadata) const;

	/**
	 * Check if loading a dataset would exceed available capacity
	 * @param DatasetInfo The dataset to check
	 * @return True if the dataset can fit in remaining capacity
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory")
	bool CanLoadDataset(const FTrajectoryDatasetInfo& DatasetInfo) const;

	/**
	 * Get the singleton instance of the memory estimator
	 */
	static UTrajectoryDataMemoryEstimator* Get();

private:
	/** Current estimated memory usage for trajectory data */
	UPROPERTY()
	int64 EstimatedMemoryUsage;

	/** Singleton instance */
	static UTrajectoryDataMemoryEstimator* Instance;
};
