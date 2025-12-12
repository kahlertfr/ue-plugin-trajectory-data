// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TrajectoryDataTypes.generated.h"

/**
 * Structure representing metadata for a single trajectory data shard
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryShardMetadata
{
	GENERATED_BODY()

	/** Unique identifier for the shard within the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 ShardId;

	/** Number of trajectories in this shard */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 NumTrajectories;

	/** Number of time samples per trajectory */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 NumSamples;

	/** Starting time step index */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TimeStepStart;

	/** Ending time step index (inclusive) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TimeStepEnd;

	/** Spatial origin coordinates */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector Origin;

	/** Type of data (e.g., "particle", "bubble") */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString DataType;

	/** Format version */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString Version;

	/** Full path to the metadata file */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString MetadataFilePath;

	/** Full path to the binary data file */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString DataFilePath;

	FTrajectoryShardMetadata()
		: ShardId(0)
		, NumTrajectories(0)
		, NumSamples(0)
		, TimeStepStart(0)
		, TimeStepEnd(0)
		, Origin(FVector::ZeroVector)
		, DataType(TEXT(""))
		, Version(TEXT("1.0"))
		, MetadataFilePath(TEXT(""))
		, DataFilePath(TEXT(""))
	{
	}
};

/**
 * Structure representing a complete trajectory dataset with all its shards
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

	/** Array of all shards in this dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<FTrajectoryShardMetadata> Shards;

	/** Total number of trajectories across all shards */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TotalTrajectories;

	/** Total number of samples across all shards */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TotalSamples;

	FTrajectoryDatasetInfo()
		: DatasetName(TEXT(""))
		, DatasetPath(TEXT(""))
		, TotalTrajectories(0)
		, TotalSamples(0)
	{
	}
};
