// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TrajectoryDataSettings.generated.h"

/**
 * Settings for the Trajectory Data plugin
 * These settings are read from Config/DefaultTrajectoryData.ini
 */
UCLASS(config=TrajectoryData, defaultconfig)
class TRAJECTORYDATA_API UTrajectoryDataSettings : public UObject
{
	GENERATED_BODY()

public:
	UTrajectoryDataSettings();

	/** Root directory containing scenario folders */
	UPROPERTY(config, EditAnywhere, Category = "Trajectory Data", meta = (DisplayName = "Scenarios Directory"))
	FString ScenariosDirectory;

	/** Enable automatic scanning of datasets on startup */
	UPROPERTY(config, EditAnywhere, Category = "Trajectory Data", meta = (DisplayName = "Auto Scan On Startup"))
	bool bAutoScanOnStartup;

	/** Enable debug logging */
	UPROPERTY(config, EditAnywhere, Category = "Trajectory Data", meta = (DisplayName = "Debug Logging"))
	bool bDebugLogging;

	/** Get the singleton instance of the settings */
	static UTrajectoryDataSettings* Get();
};
