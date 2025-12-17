// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TrajectoryDataTypes.h"
#include "TrajectoryDataManager.generated.h"

/**
 * Manager class for scanning and managing trajectory datasets
 * This class handles reading metadata from trajectory data shards
 */
UCLASS()
class TRAJECTORYDATA_API UTrajectoryDataManager : public UObject
{
	GENERATED_BODY()

public:
	UTrajectoryDataManager();

	/**
	 * Scan the configured datasets directory and gather all available datasets
	 * @return True if scanning succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
	bool ScanDatasets();

	/**
	 * Get all available trajectory datasets
	 * @return Array of dataset information structures
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
	TArray<FTrajectoryDatasetInfo> GetAvailableDatasets() const;

	/**
	 * Get information about a specific dataset by name
	 * @param DatasetName Name of the dataset to retrieve
	 * @param OutDatasetInfo Output parameter containing the dataset information
	 * @return True if dataset was found, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
	bool GetDatasetInfo(const FString& DatasetName, FTrajectoryDatasetInfo& OutDatasetInfo) const;

	/**
	 * Get the number of available datasets
	 * @return Number of datasets found
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
	int32 GetNumDatasets() const;

	/**
	 * Clear all cached dataset information
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
	void ClearDatasets();

	/** Get the singleton instance of the manager */
	static UTrajectoryDataManager* Get();

private:
	/** Array of all scanned datasets */
	UPROPERTY()
	TArray<FTrajectoryDatasetInfo> Datasets;

	/**
	 * Scan a single scenario directory for datasets
	 * @param ScenarioDirectory Path to the scenario directory
	 * @param OutDatasets Output array to append discovered datasets to
	 * @return Number of datasets found in the scenario
	 */
	int32 ScanScenarioDirectory(const FString& ScenarioDirectory, TArray<FTrajectoryDatasetInfo>& OutDatasets);

	/**
	 * Scan a single dataset directory for trajectory data shards
	 * @param DatasetDirectory Path to the dataset directory
	 * @param ScenarioName Name of the parent scenario
	 * @param OutDatasetInfo Output parameter containing the scanned dataset info
	 * @return True if scanning succeeded and shards were found
	 */
	bool ScanDatasetDirectory(const FString& DatasetDirectory, const FString& ScenarioName, FTrajectoryDatasetInfo& OutDatasetInfo);

	/**
	 * Parse a metadata JSON file
	 * @param MetadataFilePath Path to the metadata file
	 * @param OutShardMetadata Output parameter containing the parsed metadata
	 * @return True if parsing succeeded
	 */
	bool ParseMetadataFile(const FString& MetadataFilePath, FTrajectoryShardMetadata& OutShardMetadata);

	/** Singleton instance */
	static UTrajectoryDataManager* Instance;
};
