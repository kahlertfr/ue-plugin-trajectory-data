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

	/** Physical time unit (e.g., seconds, milliseconds, minutes) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString PhysicalTimeUnit;

	/** Physical start time in physical_time_unit */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	double PhysicalStartTime;

	/** Physical end time in physical_time_unit */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	double PhysicalEndTime;

	/** Coordinate units (e.g., millimeters, meters) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString CoordinateUnits;

	// Read-only dataset meta info (from binary file or JSON mirror)
	
	/** Format version */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 FormatVersion;

	/** Endianness (little or big) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString Endianness;

	/** Floating point precision (e.g., float32) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString FloatPrecision;

	/** First time step in the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 FirstTimeStep;

	/** Last time step in the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 LastTimeStep;

	/** Time step interval size */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TimeStepIntervalSize;

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
		, PhysicalTimeUnit(TEXT("seconds"))
		, PhysicalStartTime(0.0)
		, PhysicalEndTime(0.0)
		, CoordinateUnits(TEXT(""))
		, FormatVersion(1)
		, Endianness(TEXT("little"))
		, FloatPrecision(TEXT("float32"))
		, FirstTimeStep(0)
		, LastTimeStep(0)
		, TimeStepIntervalSize(0)
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

	/** Name of the scenario this dataset belongs to */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString UniqueDSName;

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
		: UniqueDSName(TEXT(""))
		, DatasetName(TEXT(""))
		, DatasetPath(TEXT(""))
		, ScenarioName(TEXT(""))
		, TotalTrajectories(0)
	{
	}
};
