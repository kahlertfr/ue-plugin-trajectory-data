// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TrajectoryDataTypes.h"
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
	 * Scan the configured datasets directory and gather all available datasets
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
	 * Get the configured datasets directory path
	 * @return Path to the datasets directory
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Get Datasets Directory"))
	static FString GetDatasetsDirectory();

	/**
	 * Set the datasets directory path
	 * @param NewPath New path to the datasets directory
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data", meta = (DisplayName = "Set Datasets Directory"))
	static void SetDatasetsDirectory(const FString& NewPath);

	/**
	 * Calculate the maximum displayable sample points for a dataset
	 * This is num_trajectories * num_samples
	 * @param DatasetInfo The dataset to calculate for
	 * @return Total number of sample points
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Calculate Max Display Points"))
	static int32 CalculateMaxDisplayPoints(const FTrajectoryDatasetInfo& DatasetInfo);

	/**
	 * Calculate the maximum displayable sample points for a specific shard
	 * This is num_trajectories * num_samples for the shard
	 * @param ShardMetadata The shard to calculate for
	 * @return Total number of sample points
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data", meta = (DisplayName = "Calculate Shard Display Points"))
	static int32 CalculateShardDisplayPoints(const FTrajectoryShardMetadata& ShardMetadata);
};
