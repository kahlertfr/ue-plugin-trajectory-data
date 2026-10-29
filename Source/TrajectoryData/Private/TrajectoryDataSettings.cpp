// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataSettings.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UTrajectoryDataSettings::UTrajectoryDataSettings()
	: bAutoScanOnStartup(true)
	, bDebugLogging(false)
{
}

UTrajectoryDataSettings* UTrajectoryDataSettings::Get()
{
	auto* Settings = GetMutableDefault<UTrajectoryDataSettings>();
#if !WITH_EDITOR
	// Apply once, before any caller can scan. Do not replace later runtime/UI edits.
	static bool bExternalConfigurationChecked = false;
	if (!bExternalConfigurationChecked && !IsRunningCommandlet())
	{
		bExternalConfigurationChecked = true;
		// In a packaged build RootDir is the distribution root, not Binaries/Win64
		// or the process working directory. This also works when launching the inner EXE.
		const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::RootDir() / TEXT("TrajectoryData.ini"));
		if (IFileManager::Get().FileExists(*Filename))
		{
			FString Error;
			if (Settings->LoadExternalConfiguration(Filename, Error))
			{
				UE_LOG(LogTemp, Display, TEXT("TrajectoryData: External config '%s'; dataset root '%s'."), *Filename, *Settings->ScenariosDirectory);
			}
			else
			{
				// A broken explicit override must not silently select a packaged/default dataset.
				Settings->ScenariosDirectory.Reset();
				UE_LOG(LogTemp, Error, TEXT("TrajectoryData: %s Dataset discovery is disabled until the config is corrected and the application restarted."), *Error);
			}
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("TrajectoryData: No external config at '%s'; using Unreal config dataset root '%s'."), *Filename, *Settings->ScenariosDirectory);
		}
	}
#endif
	return Settings;
}

bool UTrajectoryDataSettings::LoadExternalConfiguration(const FString& Filename, FString& Error)
{
	Error.Reset();
	const FString FullFilename = FPaths::ConvertRelativePathToFull(Filename);
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *FullFilename))
	{
		Error = FString::Printf(TEXT("Cannot read external dataset config '%s'."), *FullFilename);
		return false;
	}
	FConfigFile Config;
	Config.ProcessInputFileContents(Contents, FullFilename);
	FString Directory;
	if (!Config.GetString(TEXT("/Script/TrajectoryData.TrajectoryDataSettings"), TEXT("ScenariosDirectory"), Directory) || Directory.TrimStartAndEnd().IsEmpty())
	{
		Error = FString::Printf(TEXT("External dataset config '%s' requires a nonempty ScenariosDirectory in [/Script/TrajectoryData.TrajectoryDataSettings]."), *FullFilename);
		return false;
	}
	Directory.TrimStartAndEndInline();
	FPaths::NormalizeDirectoryName(Directory);
	Directory = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FullFilename), Directory);
	FPaths::CollapseRelativeDirectories(Directory);
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		Error = FString::Printf(TEXT("External dataset config '%s' selects a directory that does not exist or cannot be accessed: '%s'."), *FullFilename, *Directory);
		return false;
	}
	ScenariosDirectory = MoveTemp(Directory);
	return true;
}
