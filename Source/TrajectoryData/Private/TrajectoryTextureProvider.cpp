// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryTextureProvider.h"
#include "TrajectoryDataLoader.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

UTrajectoryTextureProvider::UTrajectoryTextureProvider()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UTrajectoryTextureProvider::UpdateFromDataset(int32 DatasetIndex)
{
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	if (!Loader)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: Failed to get loader"));
		return false;
	}

	const TArray<FLoadedDataset>& Datasets = Loader->GetLoadedDatasets();
	if (!Datasets.IsValidIndex(DatasetIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: Invalid dataset index %d"), DatasetIndex);
		return false;
	}

	const FLoadedDataset& Dataset = Datasets[DatasetIndex];
	
	if (Dataset.Trajectories.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: No trajectory data"));
		return false;
	}

	// Find maximum samples across all trajectories
	// This determines the actual texture width based on the dataset's time range
	int32 MaxSamples = 0;
	for (const FLoadedTrajectory& Traj : Dataset.Trajectories)
	{
		MaxSamples = FMath::Max(MaxSamples, Traj.Samples.Num());
	}

	if (MaxSamples == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: No samples in trajectories"));
		return false;
	}
	
	UE_LOG(LogTemp, Log, TEXT("TrajectoryTextureProvider: Texture width set to %d based on actual max samples"), MaxSamples);

	// Calculate number of textures needed (1024 trajectories per texture)
	const int32 MaxTrajPerTexture = 1024;
	int32 NumTextures = FMath::DivideAndRoundUp(Dataset.Trajectories.Num(), MaxTrajPerTexture);

	// Update metadata
	Metadata.NumTrajectories = Dataset.Trajectories.Num();
	Metadata.MaxSamplesPerTrajectory = MaxSamples;
	Metadata.MaxTrajectoriesPerTexture = MaxTrajPerTexture;
	Metadata.NumTextures = NumTextures;
	Metadata.BoundsMin = Dataset.DatasetInfo.Metadata.BoundingBoxMin;
	Metadata.BoundsMax = Dataset.DatasetInfo.Metadata.BoundingBoxMax;
	Metadata.FirstTimeStep = Dataset.DatasetInfo.Metadata.FirstTimeStep;
	Metadata.LastTimeStep = Dataset.DatasetInfo.Metadata.LastTimeStep;

	// Build trajectory ID mapping
	TrajectoryIds.SetNum(Dataset.Trajectories.Num());
	for (int32 i = 0; i < Dataset.Trajectories.Num(); ++i)
	{
		TrajectoryIds[i] = static_cast<int32>(Dataset.Trajectories[i].TrajectoryId);
	}

	// Pack data into texture buffers
	TArray<TArray<FFloat16Color>> TextureDataArray;
	PackTrajectories(Dataset, TextureDataArray);

	// Update texture resources
	UpdateTextureResources(TextureDataArray, MaxSamples);

	UE_LOG(LogTemp, Log, TEXT("TrajectoryTextureProvider: Created %d textures (%dx%d each) for %d trajectories"),
		NumTextures, MaxSamples, MaxTrajPerTexture, Dataset.Trajectories.Num());

	return true;
}

UTexture2D* UTrajectoryTextureProvider::GetPositionTexture(int32 TextureIndex) const
{
	if (PositionTextures.IsValidIndex(TextureIndex))
	{
		return PositionTextures[TextureIndex];
	}
	return nullptr;
}

int64 UTrajectoryTextureProvider::GetTrajectoryId(int32 TrajectoryIndex) const
{
	if (TrajectoryIds.IsValidIndex(TrajectoryIndex))
	{
		return TrajectoryIds[TrajectoryIndex];
	}
	return -1;
}

void UTrajectoryTextureProvider::PackTrajectories(const FLoadedDataset& Dataset, TArray<TArray<FFloat16Color>>& OutTextureDataArray)
{
	const int32 Width = Metadata.MaxSamplesPerTrajectory;
	const int32 MaxTrajPerTexture = Metadata.MaxTrajectoriesPerTexture;
	const int32 NumTextures = Metadata.NumTextures;

	// Initialize texture data arrays
	OutTextureDataArray.SetNum(NumTextures);
	
	for (int32 TexIdx = 0; TexIdx < NumTextures; ++TexIdx)
	{
		// Calculate height for this texture (last texture might have fewer trajectories)
		int32 StartTraj = TexIdx * MaxTrajPerTexture;
		int32 EndTraj = FMath::Min(StartTraj + MaxTrajPerTexture, Dataset.Trajectories.Num());
		int32 Height = EndTraj - StartTraj;
		
		OutTextureDataArray[TexIdx].SetNum(Width * Height);
		
		// Pack trajectories for this texture
		for (int32 LocalTrajIdx = 0; LocalTrajIdx < Height; ++LocalTrajIdx)
		{
			int32 GlobalTrajIdx = StartTraj + LocalTrajIdx;
			const FLoadedTrajectory& Traj = Dataset.Trajectories[GlobalTrajIdx];
			
			for (int32 SampleIdx = 0; SampleIdx < Width; ++SampleIdx)
			{
				int32 TexelIndex = LocalTrajIdx * Width + SampleIdx;
				
				if (SampleIdx < Traj.Samples.Num())
				{
					const FTrajectoryPositionSample& Sample = Traj.Samples[SampleIdx];
					FVector Pos = Sample.Position;
					float TimeStep = static_cast<float>(Traj.StartTimeStep + SampleIdx);
					
					// Pack into Float16 RGBA: XYZ + TimeStep
					// Float32 positions are automatically converted to Float16
					// This provides ~3 decimal digit precision with range ±65504
					FFloat16Color& Texel = OutTextureDataArray[TexIdx][TexelIndex];
					Texel.R = FFloat16(Pos.X);
					Texel.G = FFloat16(Pos.Y);
					Texel.B = FFloat16(Pos.Z);
					Texel.A = FFloat16(TimeStep);
				}
				else
				{
					// Mark invalid positions with NaN for trajectories with fewer samples
					// In HLSL, use isnan(Position.x) to detect invalid positions
					const float InvalidValue = FTrajectoryTextureMetadata::InvalidPositionValue;
					FFloat16Color& Texel = OutTextureDataArray[TexIdx][TexelIndex];
					Texel.R = FFloat16(InvalidValue);
					Texel.G = FFloat16(InvalidValue);
					Texel.B = FFloat16(InvalidValue);
					Texel.A = FFloat16(InvalidValue);
				}
			}
		}
	}
}

void UTrajectoryTextureProvider::UpdateTextureResources(const TArray<TArray<FFloat16Color>>& TextureDataArray, int32 Width)
{
	const int32 MaxTrajPerTexture = Metadata.MaxTrajectoriesPerTexture;

	// Resize texture array if needed
	if (PositionTextures.Num() != TextureDataArray.Num())
	{
		PositionTextures.SetNum(TextureDataArray.Num());
	}

	for (int32 TexIdx = 0; TexIdx < TextureDataArray.Num(); ++TexIdx)
	{
		// Calculate height for this texture
		int32 Height = TextureDataArray[TexIdx].Num() / Width;

		// Create new texture if needed
		if (!PositionTextures[TexIdx] || 
			PositionTextures[TexIdx]->GetSizeX() != Width || 
			PositionTextures[TexIdx]->GetSizeY() != Height)
		{
			PositionTextures[TexIdx] = UTexture2D::CreateTransient(Width, Height, PF_FloatRGBA);
			PositionTextures[TexIdx]->CompressionSettings = TC_HDR;
			PositionTextures[TexIdx]->SRGB = 0;
			PositionTextures[TexIdx]->Filter = TF_Nearest;  // No filtering for exact data
			PositionTextures[TexIdx]->AddressX = TA_Clamp;
			PositionTextures[TexIdx]->AddressY = TA_Clamp;
		}

		// Update texture data
		FTexture2DMipMap& Mip = PositionTextures[TexIdx]->GetPlatformData()->Mips[0];
		void* TextureDataPtr = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TextureDataPtr, TextureDataArray[TexIdx].GetData(), 
			TextureDataArray[TexIdx].Num() * sizeof(FFloat16Color));
		Mip.BulkData.Unlock();
		
		PositionTextures[TexIdx]->UpdateResource();
	}
}
