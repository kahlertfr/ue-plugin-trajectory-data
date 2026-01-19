// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Texture2D.h"
#include "TrajectoryDataStructures.h"
#include "TrajectoryTextureProvider.generated.h"

/**
 * Metadata for trajectory texture array
 */
USTRUCT(BlueprintType)
struct TRAJECTORYDATA_API FTrajectoryTextureMetadata
{
	GENERATED_BODY()

	/** Number of trajectories in the dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 NumTrajectories = 0;

	/** 
	 * Maximum samples per trajectory (actual texture width)
	 * This is calculated from the longest trajectory in the dataset,
	 * keeping texture size minimal based on actual data range.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 MaxSamplesPerTrajectory = 0;

	/** Maximum trajectories per texture (1024 for standard GPUs) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 MaxTrajectoriesPerTexture = 1024;

	/** Number of textures created */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 NumTextures = 0;

	/** Dataset bounding box minimum */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector BoundsMin = FVector::ZeroVector;

	/** Dataset bounding box maximum */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FVector BoundsMax = FVector::ZeroVector;

	/** First time step in dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 FirstTimeStep = 0;

	/** Last time step in dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	int32 LastTimeStep = 0;
	
	/**
	 * Invalid position marker value (NaN)
	 * Texels with this value indicate no position data is available.
	 * Use isnan() in HLSL to detect invalid positions.
	 */
	static constexpr float InvalidPositionValue = NAN;
};

/**
 * Component that converts trajectory data into GPU textures for Niagara
 * Supports multiple textures for datasets with more than 1024 trajectories
 * 
 * Texture Encoding Details:
 * - Format: PF_FloatRGBA (4 channels of 16-bit float, 8 bytes per texel)
 * - Width: Based on actual maximum samples in the dataset (not fixed)
 * - Height: Up to 1024 trajectories per texture
 * - Channels:
 *   - R: Position X (Float16 encoding of float position in world units)
 *   - G: Position Y (Float16 encoding of float position in world units)
 *   - B: Position Z (Float16 encoding of float position in world units)
 *   - A: Time Step (Float16 encoding of integer time step value)
 * 
 * Float16 Encoding:
 * - Float32 values are automatically converted to Float16 by FFloat16 constructor
 * - Range: ±65504 (max representable value)
 * - Precision: ~3 decimal digits
 * - Special values: NaN preserved, infinity clamped to max
 * - Invalid positions marked with NaN in all RGB channels
 * 
 * Invalid Position Handling:
 * - Texels where no trajectory data exists are set to NaN
 * - In HLSL, check with: isnan(Position.x) || isnan(Position.y) || isnan(Position.z)
 * - This occurs when trajectory has fewer samples than MaxSamplesPerTrajectory
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TRAJECTORYDATA_API UTrajectoryTextureProvider : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrajectoryTextureProvider();

	/**
	 * Update textures from a loaded dataset
	 * @param DatasetIndex Index into LoadedDatasets array
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Trajectory Data")
	bool UpdateFromDataset(int32 DatasetIndex);

	/**
	 * Get all position textures (array for multiple texture support)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
	TArray<UTexture2D*> GetPositionTextures() const { return PositionTextures; }

	/**
	 * Get a specific position texture by index
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
	UTexture2D* GetPositionTexture(int32 TextureIndex) const;

	/**
	 * Get texture metadata
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
	FTrajectoryTextureMetadata GetMetadata() const { return Metadata; }

	/**
	 * Get trajectory ID for a given trajectory index
	 * Used to map from texture index to original trajectory ID
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
	int64 GetTrajectoryId(int32 TrajectoryIndex) const;

	/**
	 * Get all trajectory IDs (for passing to Niagara)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trajectory Data")
	TArray<int32> GetTrajectoryIds() const { return TrajectoryIds; }

protected:
	/** Array of position textures (one per 1024 trajectories) */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<UTexture2D*> PositionTextures;

	/** Metadata for current dataset */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	FTrajectoryTextureMetadata Metadata;

	/** Mapping from trajectory index to original trajectory ID */
	UPROPERTY(BlueprintReadOnly, Category = "Trajectory Data")
	TArray<int32> TrajectoryIds;

private:
	/** Pack trajectory data into texture buffers */
	void PackTrajectories(const FLoadedDataset& Dataset, TArray<TArray<FFloat16Color>>& OutTextureDataArray);
	
	/** Create or update texture resources */
	void UpdateTextureResources(const TArray<TArray<FFloat16Color>>& TextureDataArray, int32 Width);
};
