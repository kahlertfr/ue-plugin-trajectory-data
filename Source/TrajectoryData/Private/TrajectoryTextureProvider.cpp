// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryTextureProvider.h"
#include "TrajectoryDataLoader.h"
#include "Engine/Texture2DArray.h"
#include "TextureResource.h"

UTrajectoryTextureProvider::UTrajectoryTextureProvider()
{
	PrimaryComponentTick.bCanEverTick = false;
	PositionTextureArray = nullptr;
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

	// Calculate number of texture slices needed (1024 trajectories per slice)
	const int32 MaxTrajPerTexture = 1024;
	int32 NumSlices = FMath::DivideAndRoundUp(Dataset.Trajectories.Num(), MaxTrajPerTexture);

	// Update metadata
	Metadata.NumTrajectories = Dataset.Trajectories.Num();
	Metadata.MaxSamplesPerTrajectory = MaxSamples;
	Metadata.MaxTrajectoriesPerTexture = MaxTrajPerTexture;
	Metadata.NumTextureSlices = NumSlices;
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

	// Pack data into texture buffers (one per slice)
	TArray<TArray<FFloat16Color>> TextureDataArray;
	PackTrajectories(Dataset, TextureDataArray);

	// Update texture array resource
	UpdateTextureArrayResource(TextureDataArray, MaxSamples);

	UE_LOG(LogTemp, Log, TEXT("TrajectoryTextureProvider: Created Texture2DArray with %d slices (%dx%d each) for %d trajectories"),
		NumSlices, MaxSamples, MaxTrajPerTexture, Dataset.Trajectories.Num());

	return true;
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
	const int32 NumSlices = Metadata.NumTextureSlices;

	// Initialize texture data arrays (one per slice)
	OutTextureDataArray.SetNum(NumSlices);
	
	for (int32 SliceIdx = 0; SliceIdx < NumSlices; ++SliceIdx)
	{
		// Calculate height for this slice (last slice might have fewer trajectories)
		int32 StartTraj = SliceIdx * MaxTrajPerTexture;
		int32 EndTraj = FMath::Min(StartTraj + MaxTrajPerTexture, Dataset.Trajectories.Num());
		int32 Height = EndTraj - StartTraj;
		
		// Note: For Texture2DArray, all slices must have same dimensions
		// So we always use MaxTrajPerTexture height, padding with invalid data if needed
		OutTextureDataArray[SliceIdx].SetNum(Width * MaxTrajPerTexture);
		
		// Initialize all with invalid data first
		const float InvalidValue = FTrajectoryTextureMetadata::InvalidPositionValue;
		for (int32 i = 0; i < OutTextureDataArray[SliceIdx].Num(); ++i)
		{
			FFloat16Color& Texel = OutTextureDataArray[SliceIdx][i];
			Texel.R = FFloat16(InvalidValue);
			Texel.G = FFloat16(InvalidValue);
			Texel.B = FFloat16(InvalidValue);
			Texel.A = FFloat16(InvalidValue);
		}
		
		// Pack actual trajectories for this slice
		for (int32 LocalTrajIdx = 0; LocalTrajIdx < Height; ++LocalTrajIdx)
		{
			int32 GlobalTrajIdx = StartTraj + LocalTrajIdx;
			const FLoadedTrajectory& Traj = Dataset.Trajectories[GlobalTrajIdx];
			
			for (int32 SampleIdx = 0; SampleIdx < Width; ++SampleIdx)
			{
				int32 TexelIndex = LocalTrajIdx * Width + SampleIdx;
				
				if (SampleIdx < Traj.Samples.Num())
				{
					const FVector3f& Pos = Traj.Samples[SampleIdx];
					float TimeStep = static_cast<float>(Traj.StartTimeStep + SampleIdx);
					
					// Pack into Float16 RGBA: XYZ + TimeStep
					// Float32 positions are automatically converted to Float16
					// This provides ~3 decimal digit precision with range ±65504
					FFloat16Color& Texel = OutTextureDataArray[SliceIdx][TexelIndex];
					Texel.R = FFloat16(Pos.X);
					Texel.G = FFloat16(Pos.Y);
					Texel.B = FFloat16(Pos.Z);
					Texel.A = FFloat16(TimeStep);
				}
				// else: Already initialized with invalid data above
			}
		}
	}
}

void UTrajectoryTextureProvider::UpdateTextureArrayResource(const TArray<TArray<FFloat16Color>>& TextureDataArray, int32 Width)
{
	const int32 Height = Metadata.MaxTrajectoriesPerTexture;  // Always 1024 for Texture2DArray
	const int32 NumSlices = TextureDataArray.Num();

	// Create new texture array if needed or if dimensions changed
	if (!PositionTextureArray || 
		PositionTextureArray->GetSizeX() != Width || 
		PositionTextureArray->GetSizeY() != Height ||
		PositionTextureArray->GetArraySize() != NumSlices)
	{
		// Create new Texture2DArray using CreateTransient
		PositionTextureArray = UTexture2DArray::CreateTransient(Width, Height, NumSlices, PF_FloatRGBA);
		
		if (!PositionTextureArray)
		{
			UE_LOG(LogTemp, Error, TEXT("TrajectoryTextureProvider: Failed to create Texture2DArray"));
			return;
		}
		
		// Configure texture settings
		PositionTextureArray->CompressionSettings = TC_HDR;
		PositionTextureArray->SRGB = 0;
		PositionTextureArray->Filter = TF_Nearest;  // No filtering for exact data
		PositionTextureArray->AddressX = TA_Clamp;
		PositionTextureArray->AddressY = TA_Clamp;
		PositionTextureArray->AddressZ = TA_Clamp;
		
		// Update with source data
		if (PositionTextureArray->GetPlatformData())
		{
			auto& Mip = PositionTextureArray->GetPlatformData()->Mips[0];
			void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
			
			if (TextureData)
			{
				// Copy all slices into the bulk data
				const int32 SliceSize = Width * Height * sizeof(FFloat16Color);
				for (int32 SliceIdx = 0; SliceIdx < NumSlices; ++SliceIdx)
				{
					if (TextureDataArray.IsValidIndex(SliceIdx))
					{
						const FFloat16Color* SourceData = TextureDataArray[SliceIdx].GetData();
						uint8* DestData = static_cast<uint8*>(TextureData) + (SliceIdx * SliceSize);
						FMemory::Memcpy(DestData, SourceData, SliceSize);
					}
				}
				
				Mip.BulkData.Unlock();
			}
		}
		
		// Update the resource
		PositionTextureArray->UpdateResource();
	}
	else
	{
		// Texture exists with correct dimensions, just update the data
		if (PositionTextureArray->GetPlatformData())
		{
			auto& Mip = PositionTextureArray->GetPlatformData()->Mips[0];
			void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
			
			if (TextureData)
			{
				// Copy all slices into the bulk data
				const int32 SliceSize = Width * Height * sizeof(FFloat16Color);
				for (int32 SliceIdx = 0; SliceIdx < NumSlices; ++SliceIdx)
				{
					if (TextureDataArray.IsValidIndex(SliceIdx))
					{
						const FFloat16Color* SourceData = TextureDataArray[SliceIdx].GetData();
						uint8* DestData = static_cast<uint8*>(TextureData) + (SliceIdx * SliceSize);
						FMemory::Memcpy(DestData, SourceData, SliceSize);
					}
				}
				
				Mip.BulkData.Unlock();
			}
			
			// Update the resource
			PositionTextureArray->UpdateResource();
		}
	}
}
