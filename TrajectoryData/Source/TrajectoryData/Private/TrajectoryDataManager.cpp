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

	// Find all shard-manifest.json files in subdirectories
	TArray<FString> ManifestFiles;
	PlatformFile.IterateDirectory(*DatasetDirectory, [&ManifestFiles, &PlatformFile](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (bIsDirectory)
		{
			// Check if this subdirectory contains a shard-manifest.json
			FString SubDir(FilenameOrDirectory);
			FString ManifestPath = FPaths::Combine(SubDir, TEXT("shard-manifest.json"));
			if (PlatformFile.FileExists(*ManifestPath))
			{
				ManifestFiles.Add(ManifestPath);
			}
		}
		return true; // Continue iteration
	});

	if (ManifestFiles.Num() == 0)
	{
		return false; // No manifest files found
	}

	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();

	// Parse each manifest file
	for (const FString& ManifestFile : ManifestFiles)
	{
		FTrajectoryShardMetadata ShardMetadata;
		if (ParseMetadataFile(ManifestFile, ShardMetadata))
		{
			OutDatasetInfo.Shards.Add(ShardMetadata);
			OutDatasetInfo.TotalTrajectories += ShardMetadata.TrajectoryCount;

			if (Settings && Settings->bDebugLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("  Shard '%s': %lld trajectories"),
					*ShardMetadata.ShardName, ShardMetadata.TrajectoryCount);
			}
		}
	}

	// Sort shards by shard name
	OutDatasetInfo.Shards.Sort([](const FTrajectoryShardMetadata& A, const FTrajectoryShardMetadata& B)
	{
		return A.ShardName < B.ShardName;
	});

	return OutDatasetInfo.Shards.Num() > 0;
}

bool UTrajectoryDataManager::ParseMetadataFile(const FString& MetadataFilePath, FTrajectoryShardMetadata& OutShardMetadata)
{
	// Read the JSON file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *MetadataFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Failed to read manifest file: %s"), *MetadataFilePath);
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

	// Store paths
	OutShardMetadata.ManifestFilePath = MetadataFilePath;
	OutShardMetadata.ShardDirectory = FPaths::GetPath(MetadataFilePath);

	// Parse all fields from shard-manifest.json according to specification
	JsonObject->TryGetStringField(TEXT("shard_name"), OutShardMetadata.ShardName);
	
	int32 TempFormatVersion = 1;
	JsonObject->TryGetNumberField(TEXT("format_version"), TempFormatVersion);
	OutShardMetadata.FormatVersion = TempFormatVersion;
	
	JsonObject->TryGetStringField(TEXT("endianness"), OutShardMetadata.Endianness);
	JsonObject->TryGetStringField(TEXT("coordinate_units"), OutShardMetadata.CoordinateUnits);
	JsonObject->TryGetStringField(TEXT("float_precision"), OutShardMetadata.FloatPrecision);
	JsonObject->TryGetStringField(TEXT("time_units"), OutShardMetadata.TimeUnits);
	
	JsonObject->TryGetNumberField(TEXT("time_step_interval_size"), OutShardMetadata.TimeStepIntervalSize);
	
	double TempTimeInterval = 0.0;
	JsonObject->TryGetNumberField(TEXT("time_interval_seconds"), TempTimeInterval);
	OutShardMetadata.TimeIntervalSeconds = (float)TempTimeInterval;
	
	JsonObject->TryGetNumberField(TEXT("entry_size_bytes"), OutShardMetadata.EntrySizeBytes);
	
	// Parse bounding_box object
	const TSharedPtr<FJsonObject>* BBoxObject;
	if (JsonObject->TryGetObjectField(TEXT("bounding_box"), BBoxObject))
	{
		// Parse min array
		const TArray<TSharedPtr<FJsonValue>>* MinArray;
		if ((*BBoxObject)->TryGetArrayField(TEXT("min"), MinArray) && MinArray->Num() == 3)
		{
			double X = (*MinArray)[0]->AsNumber();
			double Y = (*MinArray)[1]->AsNumber();
			double Z = (*MinArray)[2]->AsNumber();
			OutShardMetadata.BoundingBoxMin = FVector(X, Y, Z);
		}
		
		// Parse max array
		const TArray<TSharedPtr<FJsonValue>>* MaxArray;
		if ((*BBoxObject)->TryGetArrayField(TEXT("max"), MaxArray) && MaxArray->Num() == 3)
		{
			double X = (*MaxArray)[0]->AsNumber();
			double Y = (*MaxArray)[1]->AsNumber();
			double Z = (*MaxArray)[2]->AsNumber();
			OutShardMetadata.BoundingBoxMax = FVector(X, Y, Z);
		}
	}
	
	// Parse trajectory counts as 64-bit integers
	int64 TempTrajectoryCount = 0;
	JsonObject->TryGetNumberField(TEXT("trajectory_count"), TempTrajectoryCount);
	OutShardMetadata.TrajectoryCount = TempTrajectoryCount;
	
	int64 TempFirstId = 0;
	JsonObject->TryGetNumberField(TEXT("first_trajectory_id"), TempFirstId);
	OutShardMetadata.FirstTrajectoryId = TempFirstId;
	
	int64 TempLastId = 0;
	JsonObject->TryGetNumberField(TEXT("last_trajectory_id"), TempLastId);
	OutShardMetadata.LastTrajectoryId = TempLastId;
	
	JsonObject->TryGetStringField(TEXT("created_at"), OutShardMetadata.CreatedAt);
	JsonObject->TryGetStringField(TEXT("converter_version"), OutShardMetadata.ConverterVersion);

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
