// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TrajectoryDataSettings.generated.h"

/**
 * Settings for the Trajectory Data plugin
 * These settings are read from Config/DefaultTrajectoryData.ini. Packaged runtimes
 * optionally override the dataset root from a loose TrajectoryData.ini at the package root.
 */
UCLASS(config=TrajectoryData, defaultconfig)
class TRAJECTORYDATA_API UTrajectoryDataSettings : public UObject
{
	GENERATED_BODY()

public:
	UTrajectoryDataSettings();

	/** Dataset root containing domain/scenario/dataset folders; legacy property name retained for config compatibility */
	UPROPERTY(config, EditAnywhere, Category = "Trajectory Data", meta = (DisplayName = "Dataset Root Directory"))
	FString ScenariosDirectory;

	/** Enable automatic scanning of datasets on startup */
	UPROPERTY(config, EditAnywhere, Category = "Trajectory Data", meta = (DisplayName = "Auto Scan On Startup"))
	bool bAutoScanOnStartup;

	/** Enable debug logging */
	UPROPERTY(config, EditAnywhere, Category = "Trajectory Data", meta = (DisplayName = "Debug Logging"))
	bool bDebugLogging;

	/** Get the singleton instance of the settings */
	static UTrajectoryDataSettings* Get();

	/** Read a loose runtime override. Relative dataset paths use the INI's directory.
	 * Returns false with a diagnostic for an unreadable file, missing/empty key or
	 * nonexistent directory. On failure the settings object is left unchanged.
	 */
	bool LoadExternalConfiguration(const FString& Filename, FString& Error);
};
