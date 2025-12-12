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
		Instance = NewObject<UTrajectoryDataManager>(GetTransientPackage());
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

	// JSON field name constants
	static const FString JsonExtension = TEXT(".json");
	static const FString DataExtension = TEXT(".tds");
	static const FString FieldShardId = TEXT("shard_id");
	static const FString FieldNumTrajectories = TEXT("num_trajectories");
	static const FString FieldNumSamples = TEXT("num_samples");
	static const FString FieldTimeStepStart = TEXT("time_step_start");
	static const FString FieldTimeStepEnd = TEXT("time_step_end");
	static const FString FieldDataType = TEXT("data_type");
	static const FString FieldVersion = TEXT("version");
	static const FString FieldOrigin = TEXT("origin");

	// Extract fields
	OutShardMetadata.MetadataFilePath = MetadataFilePath;
	
	// Construct data file path by replacing .json with .tds
	if (MetadataFilePath.EndsWith(JsonExtension))
	{
		OutShardMetadata.DataFilePath = MetadataFilePath.LeftChop(JsonExtension.Len()) + DataExtension;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Metadata file doesn't end with .json: %s"), *MetadataFilePath);
		OutShardMetadata.DataFilePath = MetadataFilePath + DataExtension;
	}

	JsonObject->TryGetNumberField(FieldShardId, OutShardMetadata.ShardId);
	JsonObject->TryGetNumberField(FieldNumTrajectories, OutShardMetadata.NumTrajectories);
	JsonObject->TryGetNumberField(FieldNumSamples, OutShardMetadata.NumSamples);
	JsonObject->TryGetNumberField(FieldTimeStepStart, OutShardMetadata.TimeStepStart);
	JsonObject->TryGetNumberField(FieldTimeStepEnd, OutShardMetadata.TimeStepEnd);
	JsonObject->TryGetStringField(FieldDataType, OutShardMetadata.DataType);
	JsonObject->TryGetStringField(FieldVersion, OutShardMetadata.Version);

	// Parse origin array
	const TArray<TSharedPtr<FJsonValue>>* OriginArray;
	if (JsonObject->TryGetArrayField(FieldOrigin, OriginArray))
	{
		if (OriginArray->Num() == 3)
		{
			double X = (*OriginArray)[0]->AsNumber();
			double Y = (*OriginArray)[1]->AsNumber();
			double Z = (*OriginArray)[2]->AsNumber();
			OutShardMetadata.Origin = FVector(X, Y, Z);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Origin array has %d elements (expected 3) in file: %s"), 
				OriginArray->Num(), *MetadataFilePath);
		}
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
