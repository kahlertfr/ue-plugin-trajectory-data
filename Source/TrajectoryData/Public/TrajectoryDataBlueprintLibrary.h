// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TrajectoryDataTypes.h"
#include "TrajectoryDataStructures.h"
#include "TrajectoryDataMemoryEstimator.h"
#include "TrajectoryDataBlueprintLibrary.generated.h"

/**
 * Blueprint Function Library for Trajectory Data
 * Provides easy access to trajectory data management functions from Blueprints
 */
UCLASS()
class TRAJECTORYDATA_API UTrajectoryDataBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Scan the configured scenarios directory and gather all available datasets from all scenarios
	 * Call this before accessing dataset information
	 * @return True if scanning succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data", meta = (DisplayName = "Scan Trajectory Datasets"))
	static bool ScanTrajectoryDatasets();

	/**
	 * Get all available trajectory datasets
	 * @return Array of dataset information structures
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Get Available Datasets"))
	static TArray<FTrajectoryDatasetInfo> GetAvailableDatasets();

	/**
	 * Get information about a specific dataset by name
	 * @param DatasetName Name of the dataset to retrieve
	 * @param OutDatasetInfo Output parameter containing the dataset information
	 * @return True if dataset was found, false otherwise
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Get Dataset Info"))
	static bool GetDatasetInfo(const FString& DatasetName, FTrajectoryDatasetInfo& OutDatasetInfo);

	/**
	 * Get the number of available datasets
	 * @return Number of datasets found
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Get Number of Datasets"))
	static int32 GetNumDatasets();

	/**
	 * Clear all cached dataset information
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data", meta = (DisplayName = "Clear Trajectory Datasets"))
	static void ClearDatasets();

	/**
	 * Get the configured scenarios directory path
	 * @return Path to the scenarios directory
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Get Scenarios Directory"))
	static FString GetScenariosDirectory();

	/**
	 * Set the scenarios directory path
	 * @param NewPath New path to the scenarios directory
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data", meta = (DisplayName = "Set Scenarios Directory"))
	static void SetScenariosDirectory(const FString& NewPath);

	/**
	 * Calculate the maximum displayable sample points for a dataset
	 * This is num_trajectories * num_samples
	 * @param DatasetInfo The dataset to calculate for
	 * @return Total number of sample points
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Calculate Max Display Points"))
	static int32 CalculateMaxDisplayPoints(const FTrajectoryDatasetInfo& DatasetInfo);

	/**
	 * Calculate the maximum displayable sample points for a specific dataset
	 * This is num_trajectories * num_samples for the dataset
	 * @param DatasetMetadata The dataset metadata to calculate for
	 * @return Total number of sample points
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Calculate Dataset Display Points"))
	static int32 CalculateDatasetDisplayPoints(const FTrajectoryDatasetMetadata& DatasetMetadata);

	// Memory Monitoring Functions

	/**
	 * Get the total physical memory available on the system
	 * @return Total physical memory in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Get Total Physical Memory"))
	static int64 GetTotalPhysicalMemory();

	/**
	 * Get the maximum memory that can be used for trajectory data (75% of total physical memory)
	 * @return Maximum trajectory data memory in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Get Max Trajectory Data Memory"))
	static int64 GetMaxTrajectoryDataMemory();

	/**
	 * Calculate memory required for a dataset from its metadata
	 * @param DatasetMetadata The dataset metadata to calculate memory for
	 * @return Estimated memory required in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Calculate Dataset Memory From Metadata"))
	static int64 CalculateDatasetMemoryFromMetadata(const FTrajectoryDatasetMetadata& DatasetMetadata);

	/**
	 * Calculate memory required for an entire dataset
	 * @param DatasetInfo The dataset to calculate memory for
	 * @return Estimated memory required in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Calculate Dataset Memory Requirement"))
	static int64 CalculateDatasetMemoryRequirement(const FTrajectoryDatasetInfo& DatasetInfo);

	/**
	 * Get current memory usage information
	 * @return Structure containing memory usage details
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Memory", meta = (DisplayName = "Get Memory Info"))
	static FTrajectoryDataMemoryInfo GetMemoryInfo();

	/**
	 * Add estimated memory usage for planned data loading
	 * Call this when user adjusts parameters to show immediate feedback
	 * @param MemoryBytes Amount of memory to add to the estimate
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Memory", meta = (DisplayName = "Add Estimated Usage"))
	static void AddEstimatedUsage(int64 MemoryBytes);

	/**
	 * Remove estimated memory usage for planned data unloading
	 * @param MemoryBytes Amount of memory to remove from the estimate
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Memory", meta = (DisplayName = "Remove Estimated Usage"))
	static void RemoveEstimatedUsage(int64 MemoryBytes);

	/**
	 * Reset all estimated memory usage to zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Memory", meta = (DisplayName = "Reset Estimated Usage"))
	static void ResetEstimatedUsage();

	/**
	 * Check if loading a dataset would exceed available capacity (using metadata)
	 * @param DatasetMetadata The dataset metadata to check
	 * @return True if the dataset can fit in remaining capacity
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Can Load Dataset From Metadata"))
	static bool CanLoadDatasetFromMetadata(const FTrajectoryDatasetMetadata& DatasetMetadata);

	/**
	 * Check if loading a dataset would exceed available capacity
	 * @param DatasetInfo The dataset to check
	 * @return True if the dataset can fit in remaining capacity
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Can Load Dataset"))
	static bool CanLoadDataset(const FTrajectoryDatasetInfo& DatasetInfo);

	/**
	 * Format memory size in bytes to a human-readable string (e.g., "1.5 GB", "256 MB")
	 * @param Bytes Memory size in bytes
	 * @return Formatted string
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Memory", meta = (DisplayName = "Format Memory Size"))
	static FString FormatMemorySize(int64 Bytes);

	// Trajectory Loading Functions

	/**
	 * Validate trajectory load parameters before loading
	 * @param Params Load parameters to validate
	 * @return Validation result with memory estimates
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading", meta = (DisplayName = "Validate Trajectory Load Params"))
	static FTrajectoryLoadValidation ValidateTrajectoryLoadParams(const FTrajectoryLoadParams& Params);

	/**
	 * Load trajectory data synchronously (blocking)
	 * @param Params Load parameters
	 * @return Load result with trajectory data
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading", meta = (DisplayName = "Load Trajectories Sync"))
	static FTrajectoryLoadResult LoadTrajectoriesSync(const FTrajectoryLoadParams& Params);

	/**
	 * Get the trajectory data loader singleton
	 * Use this to access async loading functions and delegates
	 * @return Trajectory data loader instance
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Loading", meta = (DisplayName = "Get Trajectory Loader"))
	static class UTrajectoryDataLoader* GetTrajectoryLoader();

	/**
	 * Unload all currently loaded trajectory data to free memory
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data|Loading", meta = (DisplayName = "Unload All Trajectories"))
	static void UnloadAllTrajectories();

	/**
	 * Get current memory usage for loaded trajectory data
	 * @return Memory usage in bytes
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Loading", meta = (DisplayName = "Get Loaded Data Memory Usage"))
	static int64 GetLoadedDataMemoryUsage();

	/**
	 * Get number of currently loaded trajectories
	 * @return Number of loaded trajectories
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data|Loading", meta = (DisplayName = "Get Num Loaded Trajectories"))
	static int32 GetNumLoadedTrajectories();
};
