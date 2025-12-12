// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataManager.h"
#include "TrajectoryDataSettings.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UTrajectoryDataManager* UTrajectoryDataManager::Instance = nullptr;

UTrajectoryDataManager::UTrajectoryDataManager()
{
}

UTrajectoryDataManager* UTrajectoryDataManager::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UTrajectoryDataManager>();
		Instance->AddToRoot(); // Prevent garbage collection
	}
	return Instance;
}

bool UTrajectoryDataManager::ScanDatasets()
{
	Datasets.Empty();

	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataManager: Failed to get settings"));
		return false;
	}

	FString DatasetsDir = Settings->DatasetsDirectory;
	if (DatasetsDir.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Datasets directory is not configured"));
		return false;
	}

	// Check if directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DatasetsDir))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataManager: Datasets directory does not exist: %s"), *DatasetsDir);
		return false;
	}

	if (Settings->bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Scanning datasets directory: %s"), *DatasetsDir);
	}

	// Get all subdirectories
	TArray<FString> SubDirectories;
	PlatformFile.IterateDirectory(*DatasetsDir, [&SubDirectories](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (bIsDirectory)
		{
			SubDirectories.Add(FilenameOrDirectory);
		}
		return true; // Continue iteration
	});

	// Scan each subdirectory
	for (const FString& SubDir : SubDirectories)
	{
		FTrajectoryDatasetInfo DatasetInfo;
		if (ScanDatasetDirectory(SubDir, DatasetInfo))
		{
			Datasets.Add(DatasetInfo);
			
			if (Settings->bDebugLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Found dataset '%s' with %d shards, %d trajectories, %d samples"),
					*DatasetInfo.DatasetName, DatasetInfo.Shards.Num(), DatasetInfo.TotalTrajectories, DatasetInfo.TotalSamples);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Scan complete. Found %d datasets"), Datasets.Num());
	return true;
}

bool UTrajectoryDataManager::ScanDatasetDirectory(const FString& DatasetDirectory, FTrajectoryDatasetInfo& OutDatasetInfo)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Extract dataset name from directory path
	FString DatasetName = FPaths::GetCleanFilename(DatasetDirectory);
	OutDatasetInfo.DatasetName = DatasetName;
	OutDatasetInfo.DatasetPath = DatasetDirectory;
	OutDatasetInfo.Shards.Empty();
	OutDatasetInfo.TotalTrajectories = 0;
	OutDatasetInfo.TotalSamples = 0;

	// Find all JSON metadata files in the directory
	TArray<FString> MetadataFiles;
	PlatformFile.IterateDirectory(*DatasetDirectory, [&MetadataFiles](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (!bIsDirectory)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.EndsWith(TEXT(".json")))
			{
				MetadataFiles.Add(Filename);
			}
		}
		return true; // Continue iteration
	});

	if (MetadataFiles.Num() == 0)
	{
		return false; // No metadata files found
	}

	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();

	// Parse each metadata file
	for (const FString& MetadataFile : MetadataFiles)
	{
		FTrajectoryShardMetadata ShardMetadata;
		if (ParseMetadataFile(MetadataFile, ShardMetadata))
		{
			OutDatasetInfo.Shards.Add(ShardMetadata);
			OutDatasetInfo.TotalTrajectories += ShardMetadata.NumTrajectories;
			OutDatasetInfo.TotalSamples += ShardMetadata.NumSamples;

			if (Settings && Settings->bDebugLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("  Shard %d: %d trajectories, %d samples"),
					ShardMetadata.ShardId, ShardMetadata.NumTrajectories, ShardMetadata.NumSamples);
			}
		}
	}

	// Sort shards by shard ID
	OutDatasetInfo.Shards.Sort([](const FTrajectoryShardMetadata& A, const FTrajectoryShardMetadata& B)
	{
		return A.ShardId < B.ShardId;
	});

	return OutDatasetInfo.Shards.Num() > 0;
}

bool UTrajectoryDataManager::ParseMetadataFile(const FString& MetadataFilePath, FTrajectoryShardMetadata& OutShardMetadata)
{
	// Read the JSON file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *MetadataFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Failed to read metadata file: %s"), *MetadataFilePath);
		return false;
	}

	// Parse JSON
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Failed to parse JSON from file: %s"), *MetadataFilePath);
		return false;
	}

	// Extract fields
	OutShardMetadata.MetadataFilePath = MetadataFilePath;
	
	// Construct data file path by replacing .json with .tds
	OutShardMetadata.DataFilePath = MetadataFilePath.LeftChop(5) + TEXT(".tds");

	JsonObject->TryGetNumberField(TEXT("shard_id"), OutShardMetadata.ShardId);
	JsonObject->TryGetNumberField(TEXT("num_trajectories"), OutShardMetadata.NumTrajectories);
	JsonObject->TryGetNumberField(TEXT("num_samples"), OutShardMetadata.NumSamples);
	JsonObject->TryGetNumberField(TEXT("time_step_start"), OutShardMetadata.TimeStepStart);
	JsonObject->TryGetNumberField(TEXT("time_step_end"), OutShardMetadata.TimeStepEnd);
	JsonObject->TryGetStringField(TEXT("data_type"), OutShardMetadata.DataType);
	JsonObject->TryGetStringField(TEXT("version"), OutShardMetadata.Version);

	// Parse origin array
	const TArray<TSharedPtr<FJsonValue>>* OriginArray;
	if (JsonObject->TryGetArrayField(TEXT("origin"), OriginArray) && OriginArray->Num() == 3)
	{
		double X = (*OriginArray)[0]->AsNumber();
		double Y = (*OriginArray)[1]->AsNumber();
		double Z = (*OriginArray)[2]->AsNumber();
		OutShardMetadata.Origin = FVector(X, Y, Z);
	}

	return true;
}

TArray<FTrajectoryDatasetInfo> UTrajectoryDataManager::GetAvailableDatasets() const
{
	return Datasets;
}

bool UTrajectoryDataManager::GetDatasetInfo(const FString& DatasetName, FTrajectoryDatasetInfo& OutDatasetInfo) const
{
	for (const FTrajectoryDatasetInfo& Dataset : Datasets)
	{
		if (Dataset.DatasetName.Equals(DatasetName, ESearchCase::IgnoreCase))
		{
			OutDatasetInfo = Dataset;
			return true;
		}
	}
	return false;
}

int32 UTrajectoryDataManager::GetNumDatasets() const
{
	return Datasets.Num();
}

void UTrajectoryDataManager::ClearDatasets()
{
	Datasets.Empty();
}
