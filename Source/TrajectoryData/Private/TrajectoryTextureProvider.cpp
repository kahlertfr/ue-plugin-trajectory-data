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
					const FTrajectoryPositionSample& Sample = Traj.Samples[SampleIdx];
					FVector Pos = Sample.Position;
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
		// Create new Texture2DArray
		PositionTextureArray = NewObject<UTexture2DArray>(this);
		
		// Initialize with correct settings
		PositionTextureArray->Init(Width, Height, NumSlices, PF_FloatRGBA);
		PositionTextureArray->CompressionSettings = TC_HDR;
		PositionTextureArray->SRGB = 0;
		PositionTextureArray->Filter = TF_Nearest;  // No filtering for exact data
		PositionTextureArray->AddressX = TA_Clamp;
		PositionTextureArray->AddressY = TA_Clamp;
		PositionTextureArray->AddressZ = TA_Clamp;
	}

	// Update texture data for each slice
	for (int32 SliceIdx = 0; SliceIdx < NumSlices; ++SliceIdx)
	{
		if (TextureDataArray.IsValidIndex(SliceIdx))
		{
			FTexture2DArrayResource* Resource = (FTexture2DArrayResource*)PositionTextureArray->GetResource();
			if (Resource)
			{
				// Update mip 0 for this slice
				FUpdateTextureRegion2D UpdateRegion(0, 0, 0, 0, Width, Height);
				const FFloat16Color* SourceData = TextureDataArray[SliceIdx].GetData();
				uint32 SourcePitch = Width * sizeof(FFloat16Color);
				
				// Queue texture update on render thread
				ENQUEUE_RENDER_COMMAND(UpdateTrajectoryTextureArraySlice)(
					[Resource, SliceIdx, UpdateRegion, SourceData, SourcePitch, Width, Height](FRHICommandListImmediate& RHICmdList)
					{
						// Copy data to the slice
						FUpdateTextureRegion3D Region3D(
							UpdateRegion.DestX, UpdateRegion.DestY, SliceIdx,
							UpdateRegion.SrcX, UpdateRegion.SrcY, 0,
							UpdateRegion.Width, UpdateRegion.Height, 1
						);
						
						RHICmdList.UpdateTexture3D(
							Resource->GetTexture2DArrayRHI(),
							0,  // Mip level
							Region3D,
							SourcePitch,
							SourcePitch * Height,
							(uint8*)SourceData
						);
					});
			}
		}
	}

	PositionTextureArray->UpdateResource();
}
