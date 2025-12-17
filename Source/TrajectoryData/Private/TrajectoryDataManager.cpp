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

	FString ScenariosDir = Settings->ScenariosDirectory;
	if (ScenariosDir.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Scenarios directory is not configured"));
		return false;
	}

	// Check if directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ScenariosDir))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataManager: Scenarios directory does not exist: %s"), *ScenariosDir);
		return false;
	}

	if (Settings->bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Scanning scenarios directory: %s"), *ScenariosDir);
	}

	// Get all scenario subdirectories
	TArray<FString> ScenarioDirectories;
	PlatformFile.IterateDirectory(*ScenariosDir, [&ScenarioDirectories](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (bIsDirectory)
		{
			ScenarioDirectories.Add(FilenameOrDirectory);
		}
		return true; // Continue iteration
	});

	// Scan each scenario directory for datasets
	for (const FString& ScenarioDir : ScenarioDirectories)
	{
		int32 DatasetsFound = ScanScenarioDirectory(ScenarioDir, Datasets);
		
		if (Settings->bDebugLogging && DatasetsFound > 0)
		{
			FString ScenarioName = FPaths::GetCleanFilename(ScenarioDir);
			UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Found %d dataset(s) in scenario '%s'"),
				DatasetsFound, *ScenarioName);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Scan complete. Found %d datasets across all scenarios"), Datasets.Num());
	return true;
}

int32 UTrajectoryDataManager::ScanScenarioDirectory(const FString& ScenarioDirectory, TArray<FTrajectoryDatasetInfo>& OutDatasets)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
	
	FString ScenarioName = FPaths::GetCleanFilename(ScenarioDirectory);
	int32 InitialCount = OutDatasets.Num();

	// Get all dataset subdirectories in this scenario
	TArray<FString> DatasetDirectories;
	PlatformFile.IterateDirectory(*ScenarioDirectory, [&DatasetDirectories](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (bIsDirectory)
		{
			DatasetDirectories.Add(FilenameOrDirectory);
		}
		return true; // Continue iteration
	});

	// Scan each dataset directory
	bool bDebugLogging = Settings && Settings->bDebugLogging;
	for (const FString& DatasetDir : DatasetDirectories)
	{
		FTrajectoryDatasetInfo DatasetInfo;
		if (ScanDatasetDirectory(DatasetDir, ScenarioName, DatasetInfo))
		{
			OutDatasets.Add(DatasetInfo);
			
			// Logging moved to ScanDatasetDirectory
		}
	}

	return OutDatasets.Num() - InitialCount;
}

bool UTrajectoryDataManager::ScanDatasetDirectory(const FString& DatasetDirectory, const FString& ScenarioName, FTrajectoryDatasetInfo& OutDatasetInfo)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Extract dataset name from directory path
	FString DatasetName = FPaths::GetCleanFilename(DatasetDirectory);
	OutDatasetInfo.DatasetName = DatasetName;
	OutDatasetInfo.DatasetPath = DatasetDirectory;
	OutDatasetInfo.ScenarioName = ScenarioName;
	OutDatasetInfo.TotalTrajectories = 0;

	// Look for dataset-manifest.json directly in the dataset directory
	FString ManifestPath = FPaths::Combine(DatasetDirectory, TEXT("dataset-manifest.json"));
	if (!PlatformFile.FileExists(*ManifestPath))
	{
		return false; // No manifest file found
	}

	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();

	// Parse the manifest file
	FTrajectoryShardMetadata DatasetMetadata;
	if (ParseMetadataFile(ManifestPath, DatasetMetadata))
	{
		OutDatasetInfo.Metadata = DatasetMetadata;
		OutDatasetInfo.TotalTrajectories = DatasetMetadata.TrajectoryCount;

		if (Settings && Settings->bDebugLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("  Dataset '%s' in scenario '%s': %lld trajectories"),
				*DatasetName, *ScenarioName, DatasetMetadata.TrajectoryCount);
		}
		return true;
	}

	return false;
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

	// Parse all fields from dataset-manifest.json according to specification
	JsonObject->TryGetStringField(TEXT("dataset_name"), OutShardMetadata.ShardName);
	JsonObject->TryGetNumberField(TEXT("format_version"), OutShardMetadata.FormatVersion);
	
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
