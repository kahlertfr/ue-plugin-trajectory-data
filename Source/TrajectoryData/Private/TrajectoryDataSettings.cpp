// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataSettings.h"

UTrajectoryDataSettings::UTrajectoryDataSettings()
	: bAutoScanOnStartup(true)
	, bDebugLogging(false)
{
}

UTrajectoryDataSettings* UTrajectoryDataSettings::Get()
{
	return GetMutableDefault<UTrajectoryDataSettings>();
}
