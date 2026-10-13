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
		UE_LOG(LogTemp, Warning, TEXT("TrajectoryDataManager: Dataset root directory is not configured"));
		return false;
	}

	// Check if directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ScenariosDir))
	{
		UE_LOG(LogTemp, Error, TEXT("TrajectoryDataManager: Dataset root directory does not exist: %s"), *ScenariosDir);
		return false;
	}

	if (Settings->bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Scanning dataset root directory: %s"), *ScenariosDir);
	}

	// Scan domain/scenario/dataset beneath the configured root.
	PlatformFile.IterateDirectory(*ScenariosDir, [this, &PlatformFile](const TCHAR* DomainPath, bool bIsDomain) -> bool
	{
		if (bIsDomain)
		{
			const FString DomainName = FPaths::GetCleanFilename(DomainPath);
			PlatformFile.IterateDirectory(DomainPath, [this, &DomainName](const TCHAR* ScenarioPath, bool bIsScenario) -> bool
			{
				if (bIsScenario)
				{
					ScanScenarioDirectory(ScenarioPath, DomainName, Datasets);
				}
				return true;
			});
		}
		return true;
	});
	UE_LOG(LogTemp, Log, TEXT("TrajectoryDataManager: Scan complete. Found %d datasets across all domains"), Datasets.Num());
	return true;
}
int32 UTrajectoryDataManager::ScanScenarioDirectory(const FString& ScenarioDirectory, const FString& DomainName, TArray<FTrajectoryDatasetInfo>& OutDatasets)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
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
	for (const FString& DatasetDir : DatasetDirectories)
	{
		FTrajectoryDatasetInfo DatasetInfo;
		if (ScanDatasetDirectory(DatasetDir, DomainName, ScenarioName, DatasetInfo))
		{
			OutDatasets.Add(DatasetInfo);
			
		}
	}

	return OutDatasets.Num() - InitialCount;
}

bool UTrajectoryDataManager::ScanDatasetDirectory(const FString& DatasetDirectory, const FString& DomainName, const FString& ScenarioName, FTrajectoryDatasetInfo& OutDatasetInfo)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// Extract dataset name from directory path
	FString DatasetName = FPaths::GetCleanFilename(DatasetDirectory);
	OutDatasetInfo.DomainName = DomainName;
	OutDatasetInfo.DatasetName = DatasetName;
	OutDatasetInfo.DatasetPath = DatasetDirectory;
	OutDatasetInfo.ScenarioName = ScenarioName;
	OutDatasetInfo.TotalTrajectories = 0;
	OutDatasetInfo.UniqueDSName = DomainName + TEXT("/") + ScenarioName + TEXT("/") + DatasetName;

	// Look for dataset-manifest.json directly in the dataset directory
	FString ManifestPath = FPaths::Combine(DatasetDirectory, TEXT("dataset-manifest.json"));
	if (!PlatformFile.FileExists(*ManifestPath))
	{
		return false; // No manifest file found
	}

	UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();

	// Parse the manifest file
	FTrajectoryDatasetMetadata DatasetMetadata;
	if (ParseMetadataFile(ManifestPath, DatasetMetadata))
	{
		OutDatasetInfo.Metadata = DatasetMetadata;
		OutDatasetInfo.TotalTrajectories = DatasetMetadata.TrajectoryCount;

		if (Settings && Settings->bDebugLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("  Dataset '%s' in domain '%s', scenario '%s': %lld trajectories"),
				*DatasetName, *DomainName, *ScenarioName, DatasetMetadata.TrajectoryCount);
		}
		return true;
	}

	return false;
}

bool UTrajectoryDataManager::ParseMetadataFile(const FString& MetadataFilePath, FTrajectoryDatasetMetadata& OutDatasetMetadata)
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
	OutDatasetMetadata.ManifestFilePath = MetadataFilePath;
	OutDatasetMetadata.DatasetDirectory = FPaths::GetPath(MetadataFilePath);

	// Parse all fields from dataset-manifest.json according to specification
	// Editable real-world semantics
	JsonObject->TryGetStringField(TEXT("scenario_name"), OutDatasetMetadata.ScenarioName);
	JsonObject->TryGetStringField(TEXT("dataset_name"), OutDatasetMetadata.DatasetName);
	JsonObject->TryGetStringField(TEXT("physical_time_unit"), OutDatasetMetadata.PhysicalTimeUnit);
	JsonObject->TryGetNumberField(TEXT("physical_start_time"), OutDatasetMetadata.PhysicalStartTime);
	JsonObject->TryGetNumberField(TEXT("physical_end_time"), OutDatasetMetadata.PhysicalEndTime);
	JsonObject->TryGetStringField(TEXT("coordinate_units"), OutDatasetMetadata.CoordinateUnits);
	
	// Dataset meta info (read-only) - for human readability, may be present in manifest
	const TSharedPtr<FJsonObject>* DatasetMetaInfo;
	if (JsonObject->TryGetObjectField(TEXT("dataset_meta_info"), DatasetMetaInfo))
	{
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("format_version"), OutDatasetMetadata.FormatVersion);
		(*DatasetMetaInfo)->TryGetStringField(TEXT("endianness"), OutDatasetMetadata.Endianness);
		(*DatasetMetaInfo)->TryGetStringField(TEXT("float_precision"), OutDatasetMetadata.FloatPrecision);
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("first_time_step"), OutDatasetMetadata.FirstTimeStep);
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("last_time_step"), OutDatasetMetadata.LastTimeStep);
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("time_step_interval_size"), OutDatasetMetadata.TimeStepIntervalSize);
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("entry_size_bytes"), OutDatasetMetadata.EntrySizeBytes);
		
		// Parse bounding_box object
		const TSharedPtr<FJsonObject>* BBoxObject;
		if ((*DatasetMetaInfo)->TryGetObjectField(TEXT("bounding_box"), BBoxObject))
		{
			// Parse min array
			const TArray<TSharedPtr<FJsonValue>>* MinArray;
			if ((*BBoxObject)->TryGetArrayField(TEXT("min"), MinArray) && MinArray->Num() == 3)
			{
				double X = (*MinArray)[0]->AsNumber();
				double Y = (*MinArray)[1]->AsNumber();
				double Z = (*MinArray)[2]->AsNumber();
				OutDatasetMetadata.BoundingBoxMin = FVector(X, Y, Z);
			}
			
			// Parse max array
			const TArray<TSharedPtr<FJsonValue>>* MaxArray;
			if ((*BBoxObject)->TryGetArrayField(TEXT("max"), MaxArray) && MaxArray->Num() == 3)
			{
				double X = (*MaxArray)[0]->AsNumber();
				double Y = (*MaxArray)[1]->AsNumber();
				double Z = (*MaxArray)[2]->AsNumber();
				OutDatasetMetadata.BoundingBoxMax = FVector(X, Y, Z);
			}
		}
		
		// Parse trajectory counts as 64-bit integers
		int64 TempTrajectoryCount = 0;
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("trajectory_count"), TempTrajectoryCount);
		OutDatasetMetadata.TrajectoryCount = TempTrajectoryCount;
		
		int64 TempFirstId = 0;
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("first_trajectory_id"), TempFirstId);
		OutDatasetMetadata.FirstTrajectoryId = TempFirstId;
		
		int64 TempLastId = 0;
		(*DatasetMetaInfo)->TryGetNumberField(TEXT("last_trajectory_id"), TempLastId);
		OutDatasetMetadata.LastTrajectoryId = TempLastId;
		
		(*DatasetMetaInfo)->TryGetStringField(TEXT("created_at"), OutDatasetMetadata.CreatedAt);
		(*DatasetMetaInfo)->TryGetStringField(TEXT("converter_version"), OutDatasetMetadata.ConverterVersion);
	}

	return true;
}

bool UTrajectoryDataManager::GetDatasetInfo(const FString& DatasetName, FTrajectoryDatasetInfo& OutDatasetInfo) const
{
	for (const FTrajectoryDatasetInfo& Dataset : Datasets)
	{
		if (Dataset.UniqueDSName.Equals(DatasetName, ESearchCase::IgnoreCase))
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
