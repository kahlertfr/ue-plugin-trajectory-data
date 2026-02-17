// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TrajectoryDataTypes.h"
#include "TrajectoryDataStructures.generated.h"

/**
 * Binary structure for Dataset Meta (dataset-meta.bin)
 * Total size: 92 bytes
 */
#pragma pack(push, 1)
struct FDatasetMetaBinary
{
	char Magic[4];                      // "TDSH"
	uint8 FormatVersion;                // = 1
	uint8 EndiannessFlag;               // 0 = little, 1 = big
	uint8 FloatPrecision;               // 0 = float32, 1 = float64
	uint8 Reserved;
	int32 FirstTimeStep;
	int32 LastTimeStep;
	int32 TimeStepIntervalSize;
	int32 EntrySizeBytes;
	float BBoxMin[3];
	float BBoxMax[3];
	uint64 TrajectoryCount;
	uint64 FirstTrajectoryId;
	uint64 LastTrajectoryId;
	int64 CreatedAtUnix;
	char ConverterVersion[8];
	uint32 Reserved2;
};
#pragma pack(pop)

/**
 * Binary structure for Trajectory Meta (dataset-trajmeta.bin)
 * Total size: 40 bytes per trajectory
 */
#pragma pack(push, 1)
struct FTrajectoryMetaBinary
{
	uint64 TrajectoryId;
	int32 StartTimeStep;
	int32 EndTimeStep;
	float Extent[3];                    // object half-extent in meters
	uint32 DataFileIndex;               // which shard file
	uint64 EntryOffsetIndex;            // index within shard file
};
#pragma pack(pop)

/**
 * Binary structure for Data Block Header (shard-*.bin)
 * Total size: 32 bytes
 * Note: This is a USTRUCT to allow use in Blueprint-exposed structs.
 * WARNING: This struct is used for binary file I/O with FMemory::Memcpy.
 * The field layout must match the specification exactly (no padding).
 * In Unreal Engine, USTRUCT maintains field order without adding padding,
 * so this is safe for binary I/O operations.
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FDataBlockHeaderBinary
{
	GENERATED_BODY()

	char Magic[4];                      // "TDDB" - 4 bytes at offset 0
	uint8 FormatVersion;                // 1 byte at offset 4
	uint8 EndiannessFlag;               // 1 byte at offset 5 (0 = little, 1 = big)
	uint16 Reserved;                    // 2 bytes at offset 6
	
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 GlobalIntervalIndex;          // 4 bytes at offset 8 - which interval this file represents
	
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TimeStepIntervalSize;         // 4 bytes at offset 12
	
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 TrajectoryEntryCount;         // 4 bytes at offset 16
	
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 DataSectionOffset;            // 8 bytes at offset 20 - byte offset where entries begin
	
	uint32 Reserved2;                   // 4 bytes at offset 28
	// Total: 32 bytes

	FDataBlockHeaderBinary()
		: FormatVersion(0)
		, EndiannessFlag(0)
		, Reserved(0)
		, GlobalIntervalIndex(0)
		, TimeStepIntervalSize(0)
		, TrajectoryEntryCount(0)
		, DataSectionOffset(0)
		, Reserved2(0)
	{
		FMemory::Memzero(Magic, sizeof(Magic));
	}
};

/**
 * Binary structure for raw position data as stored in shard files
 * Matches the exact layout in shard-*.bin files for blob copy optimization
 */
#pragma pack(push, 1)
struct FPositionSampleBinary
{
	float X;
	float Y;
	float Z;
};
#pragma pack(pop)

/**
 * Binary header structure for trajectory entry as stored in shard files
 * This is the fixed-size header that precedes the positions array
 */
#pragma pack(push, 1)
struct FTrajectoryEntryHeaderBinary
{
	uint64 TrajectoryId;                    // 8 bytes
	int32 StartTimeStepInInterval;          // 4 bytes
	int32 ValidSampleCount;                 // 4 bytes
	// Total: 16 bytes
	// Positions array follows immediately after this header
};
#pragma pack(pop)

/**
 * Structure representing a single trajectory entry from a shard file
 * Uses efficient bulk memory copy from binary format
 * The positions array is stored contiguously in memory matching the binary layout
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FShardTrajectoryEntry
{
	GENERATED_BODY()

	/** Unique trajectory ID (uint64 from binary) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 TrajectoryId;

	/** Start time step within this interval (int32 from binary, -1 if no valid samples) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 StartTimeStepInInterval;

	/** Number of valid samples in this interval (int32 from binary) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 ValidSampleCount;

	/** Position samples for this trajectory in this interval (array of FVector3f) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<FVector3f> Positions;

	FShardTrajectoryEntry()
		: TrajectoryId(0)
		, StartTimeStepInInterval(-1)
		, ValidSampleCount(0)
	{
	}
};

/**
 * Structure to hold complete shard file data in memory
 * Contains parsed header and trajectory entries with structured access
 * This allows other components to easily access trajectory data
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FShardFileData
{
	GENERATED_BODY()

	/** Shard file header containing metadata */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FDataBlockHeaderBinary Header;

	/** Parsed trajectory entries from the shard file */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<FShardTrajectoryEntry> Entries;

	/** Path to the shard file that was loaded */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString FilePath;

	/** Whether the load was successful */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	bool bSuccess;

	/** Error message if load failed */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString ErrorMessage;

	FShardFileData()
		: bSuccess(false)
	{
		FMemory::Memzero(&Header, sizeof(FDataBlockHeaderBinary));
	}
};

/**
 * Blueprint-exposed structure for a single 3D position sample
 * Simplified for maximum memory efficiency - stores only position data
 * Time step is implicit from array index + trajectory StartTimeStep
 * Invalid samples (NaN) should be filtered during loading
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryPositionSample
{
	GENERATED_BODY()

	/** 3D position (x, y, z) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector Position;

	FTrajectoryPositionSample()
		: Position(FVector::ZeroVector)
	{
	}
	
	FTrajectoryPositionSample(const FVector& InPosition)
		: Position(InPosition)
	{
	}
};

/**
 * Blueprint-exposed structure for a loaded trajectory
 * Contains all position samples for a single trajectory
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FLoadedTrajectory
{
	GENERATED_BODY()

	/** Unique trajectory ID */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 TrajectoryId;

	/** Start time step (from metadata) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 StartTimeStep;

	/** End time step (from metadata) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 EndTimeStep;

	/** Object half-extent in meters */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector3f Extent;

	/** Array of position samples - stored as FVector3f (12 bytes) for Niagara compatibility */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<FVector3f> Samples;

	/** Default extent in meters (10 cm half-extent = 20 cm full size) */
	static constexpr float DefaultExtentMeters = 0.1f;

	FLoadedTrajectory()
		: TrajectoryId(0)
		, StartTimeStep(0)
		, EndTimeStep(0)
		, Extent(FVector3f(DefaultExtentMeters, DefaultExtentMeters, DefaultExtentMeters))
	{
	}
};

/**
 * Structure for specifying which trajectories to load
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryLoadSelection
{
	GENERATED_BODY()

	/** Trajectory ID */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int64 TrajectoryId;

	/** Start time step for this trajectory (optional, -1 for dataset start) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int32 StartTimeStep;

	/** End time step for this trajectory (optional, -1 for dataset end) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int32 EndTimeStep;

	FTrajectoryLoadSelection()
		: TrajectoryId(0)
		, StartTimeStep(-1)
		, EndTimeStep(-1)
	{
	}
};

/**
 * Enum for trajectory selection strategy
 */
UENUM(BlueprintType)
enum class ETrajectorySelectionStrategy : uint8
{
	/** Load first N trajectories */
	FirstN UMETA(DisplayName = "First N Trajectories"),
	
	/** Load every Ith trajectory to distribute N across dataset */
	Distributed UMETA(DisplayName = "Distributed N Trajectories"),
	
	/** Load trajectories by explicit ID list */
	ExplicitList UMETA(DisplayName = "Explicit Trajectory List")
};

/**
 * Parameters for loading trajectory data
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryLoadParams
{
	GENERATED_BODY()

	/** Start time step of interest (-1 for dataset start) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int32 StartTimeStep;

	/** End time step of interest (-1 for dataset end) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int32 EndTimeStep;

	/** Sample rate (1 = every sample, 2 = every 2nd, etc.) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int32 SampleRate;

	/** Selection strategy for trajectories */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	ETrajectorySelectionStrategy SelectionStrategy;

	/** Number of trajectories to load (when using FirstN or Distributed) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	int32 NumTrajectories;

	/** Explicit list of trajectory selections (when using ExplicitList) */
	UPROPERTY(BlueprintReadWrite, Category = "Trajectory Data")
	TArray<FTrajectoryLoadSelection> TrajectorySelections;

	FTrajectoryLoadParams()
		: StartTimeStep(-1)
		, EndTimeStep(-1)
		, SampleRate(1)
		, SelectionStrategy(ETrajectorySelectionStrategy::FirstN)
		, NumTrajectories(0)
	{
	}
};

/**
 * Structure representing a single loaded dataset
 * Contains the loading parameters, dataset info, and loaded trajectories
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FLoadedDataset
{
	GENERATED_BODY()

	/** Loading parameters used for this dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FTrajectoryLoadParams LoadParams;

	/** Dataset information including path, metadata, and scenario info */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FTrajectoryDatasetInfo DatasetInfo;

	/** Array of loaded trajectories for this dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<FLoadedTrajectory> Trajectories;

	/** Memory used by this dataset in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 MemoryUsedBytes;

	FLoadedDataset()
		: MemoryUsedBytes(0)
	{
	}
};

/**
 * Result structure for loaded trajectory data
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryLoadResult
{
	GENERATED_BODY()

	/** Whether loading succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	bool bSuccess;

	/** Error message if loading failed */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString ErrorMessage;

	/** Loaded trajectories */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<FLoadedTrajectory> Trajectories;

	/** Actual time step range loaded */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 LoadedStartTimeStep;

	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 LoadedEndTimeStep;

	/** Total memory used in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 MemoryUsedBytes;

	FTrajectoryLoadResult()
		: bSuccess(false)
		, ErrorMessage(TEXT(""))
		, LoadedStartTimeStep(0)
		, LoadedEndTimeStep(0)
		, MemoryUsedBytes(0)
	{
	}
};

/**
 * Validation result for load parameters
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryLoadValidation
{
	GENERATED_BODY()

	/** Whether the load configuration is valid */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	bool bCanLoad;

	/** Validation message (error or warning) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FString Message;

	/** Estimated memory required in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int64 EstimatedMemoryBytes;

	/** Number of trajectories that would be loaded */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 NumTrajectoriesToLoad;

	/** Number of samples per trajectory */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 NumSamplesPerTrajectory;

	FTrajectoryLoadValidation()
		: bCanLoad(false)
		, Message(TEXT(""))
		, EstimatedMemoryBytes(0)
		, NumTrajectoriesToLoad(0)
		, NumSamplesPerTrajectory(0)
	{
	}
};
