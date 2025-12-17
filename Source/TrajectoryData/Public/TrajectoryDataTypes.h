// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TrajectoryDataTypes.generated.h"

/**
 * Structure representing metadata for a single trajectory dataset
 * Based on the Trajectory Dataset specification (dataset-manifest.json format)
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryDatasetMetadata
{
	GENERATED_BODY()

	/** Name of the scenario this dataset belongs to */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString ScenarioName;

	/** Name of the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString DatasetName;

	/** Format version */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 FormatVersion;

	/** Endianness (little or big) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString Endianness;

	/** Coordinate units (e.g., millimeters, meters) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString CoordinateUnits;

	/** Floating point precision (e.g., float32) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString FloatPrecision;

	/** Time units (e.g., seconds) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString TimeUnits;

	/** Time step interval size */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TimeStepIntervalSize;

	/** Time interval in seconds */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	float TimeIntervalSeconds;

	/** Entry size in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 EntrySizeBytes;

	/** Bounding box minimum */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector BoundingBoxMin;

	/** Bounding box maximum */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector BoundingBoxMax;

	/** Total number of trajectories in this dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 TrajectoryCount;

	/** First trajectory ID */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 FirstTrajectoryId;

	/** Last trajectory ID */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 LastTrajectoryId;

	/** Creation timestamp */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString CreatedAt;

	/** Converter version (git commit hash) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString ConverterVersion;

	/** Full path to the manifest JSON file */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString ManifestFilePath;

	/** Directory path containing all dataset files */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString DatasetDirectory;

	FTrajectoryDatasetMetadata()
		: ScenarioName(TEXT(""))
		, DatasetName(TEXT(""))
		, FormatVersion(1)
		, Endianness(TEXT("little"))
		, CoordinateUnits(TEXT(""))
		, FloatPrecision(TEXT("float32"))
		, TimeUnits(TEXT("seconds"))
		, TimeStepIntervalSize(0)
		, TimeIntervalSeconds(0.0f)
		, EntrySizeBytes(0)
		, BoundingBoxMin(FVector::ZeroVector)
		, BoundingBoxMax(FVector::ZeroVector)
		, TrajectoryCount(0)
		, FirstTrajectoryId(0)
		, LastTrajectoryId(0)
		, CreatedAt(TEXT(""))
		, ConverterVersion(TEXT(""))
		, ManifestFilePath(TEXT(""))
		, DatasetDirectory(TEXT(""))
	{
	}
};

/**
 * Structure representing a complete trajectory dataset with all its shards.
 * Multiple datasets within the same scenario are spatially and temporally related to each other.
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryDatasetInfo
{
	GENERATED_BODY()

	/** Name of the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString DatasetName;

	/** Directory path containing the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString DatasetPath;

	/** Name of the scenario this dataset belongs to */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString ScenarioName;

	/** Metadata for this dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FTrajectoryDatasetMetadata Metadata;

	/** Total number of trajectories in this dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 TotalTrajectories;

	FTrajectoryDatasetInfo()
		: DatasetName(TEXT(""))
		, DatasetPath(TEXT(""))
		, ScenarioName(TEXT(""))
		, TotalTrajectories(0)
	{
	}
};
