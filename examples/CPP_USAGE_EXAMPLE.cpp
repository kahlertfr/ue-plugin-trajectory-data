// Example C++ usage of the Trajectory Data plugin
// This is a reference implementation showing how to use the plugin from C++

#include "TrajectoryDataManager.h"
#include "TrajectoryDataLoader.h"
#include "TrajectoryDataBlueprintLibrary.h"
#include "TrajectoryDataStructures.h"

// Example 1: Scan and list available datasets
void ExampleScanDatasets()
{
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	
	// Scan for datasets
	if (Manager->ScanDatasets())
	{
		TArray<FTrajectoryDatasetInfo> Datasets = Manager->GetAvailableDatasets();
		
		UE_LOG(LogTemp, Log, TEXT("Found %d datasets"), Datasets.Num());
		
		for (const FTrajectoryDatasetInfo& Dataset : Datasets)
		{
			UE_LOG(LogTemp, Log, TEXT("Dataset: %s (Scenario: %s)"), 
				*Dataset.DatasetName, *Dataset.ScenarioName);
			UE_LOG(LogTemp, Log, TEXT("  Path: %s"), *Dataset.DatasetPath);
			UE_LOG(LogTemp, Log, TEXT("  Trajectories: %lld"), Dataset.TotalTrajectories);
			UE_LOG(LogTemp, Log, TEXT("  Time steps: %d"), Dataset.Metadata.TimeStepIntervalSize);
			UE_LOG(LogTemp, Log, TEXT("  Time interval: %.3f seconds"), Dataset.Metadata.TimeIntervalSeconds);
		}
	}
}

// Example 2: Load first 100 trajectories from a dataset
void ExampleLoadFirstN()
{
	// Get dataset info
	UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();
	FTrajectoryDatasetInfo DatasetInfo;
	if (!Manager->GetDatasetInfo(TEXT("sample_dataset"), DatasetInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("Dataset not found"));
		return;
	}
	
	// Create load parameters
	FTrajectoryLoadParams Params;
	Params.DatasetPath = DatasetInfo.DatasetPath;
	Params.StartTimeStep = -1;  // Use dataset start
	Params.EndTimeStep = -1;    // Use dataset end
	Params.SampleRate = 1;      // Load every sample
	Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
	Params.NumTrajectories = 100;
	
	// Get loader
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	
	// Validate parameters
	FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
	if (!Validation.bCanLoad)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot load: %s"), *Validation.Message);
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("Will load %d trajectories with %d samples each"),
		Validation.NumTrajectoriesToLoad, Validation.NumSamplesPerTrajectory);
	UE_LOG(LogTemp, Log, TEXT("Estimated memory: %s"),
		*UTrajectoryDataBlueprintLibrary::FormatMemorySize(Validation.EstimatedMemoryBytes));
	
	// Load synchronously
	FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
	
	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully loaded %d trajectories"), Result.Trajectories.Num());
		UE_LOG(LogTemp, Log, TEXT("Memory used: %s"),
			*UTrajectoryDataBlueprintLibrary::FormatMemorySize(Result.MemoryUsedBytes));
		
		// Process trajectories
		for (const FLoadedTrajectory& Traj : Result.Trajectories)
		{
			UE_LOG(LogTemp, Log, TEXT("Trajectory %lld: %d samples (time steps %d to %d)"),
				Traj.TrajectoryId, Traj.Samples.Num(), Traj.StartTimeStep, Traj.EndTimeStep);
			
			// Access individual samples
			for (const FTrajectoryPositionSample& Sample : Traj.Samples)
			{
				if (Sample.bIsValid)
				{
					// Use sample position
					FVector Position = Sample.Position;
					int32 TimeStep = Sample.TimeStep;
					
					// Example: Create debug point
					// DrawDebugPoint(GetWorld(), Position, 5.0f, FColor::Red, false, 1.0f);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
	}
}

// Example 3: Load every 10th trajectory (distributed)
void ExampleLoadDistributed()
{
	FTrajectoryDatasetInfo DatasetInfo;
	if (!UTrajectoryDataManager::Get()->GetDatasetInfo(TEXT("sample_dataset"), DatasetInfo))
	{
		return;
	}
	
	FTrajectoryLoadParams Params;
	Params.DatasetPath = DatasetInfo.DatasetPath;
	Params.StartTimeStep = 0;
	Params.EndTimeStep = 500;
	Params.SampleRate = 2;  // Load every 2nd sample
	Params.SelectionStrategy = ETrajectorySelectionStrategy::Distributed;
	Params.NumTrajectories = 50;  // Load 50 trajectories distributed across dataset
	
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
	
	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectories from time steps %d to %d"),
			Result.Trajectories.Num(), Result.LoadedStartTimeStep, Result.LoadedEndTimeStep);
	}
}

// Example 4: Load specific trajectories with custom time ranges
void ExampleLoadExplicitList()
{
	FTrajectoryDatasetInfo DatasetInfo;
	if (!UTrajectoryDataManager::Get()->GetDatasetInfo(TEXT("sample_dataset"), DatasetInfo))
	{
		return;
	}
	
	FTrajectoryLoadParams Params;
	Params.DatasetPath = DatasetInfo.DatasetPath;
	Params.SampleRate = 1;
	Params.SelectionStrategy = ETrajectorySelectionStrategy::ExplicitList;
	
	// Add specific trajectories
	FTrajectoryLoadSelection Sel1;
	Sel1.TrajectoryId = 42;
	Sel1.StartTimeStep = 0;
	Sel1.EndTimeStep = 100;
	Params.TrajectorySelections.Add(Sel1);
	
	FTrajectoryLoadSelection Sel2;
	Sel2.TrajectoryId = 123;
	Sel2.StartTimeStep = 50;
	Sel2.EndTimeStep = 200;
	Params.TrajectorySelections.Add(Sel2);
	
	FTrajectoryLoadSelection Sel3;
	Sel3.TrajectoryId = 456;
	Sel3.StartTimeStep = -1;  // Use dataset start
	Sel3.EndTimeStep = -1;    // Use dataset end
	Params.TrajectorySelections.Add(Sel3);
	
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
	
	if (Result.bSuccess)
	{
		for (const FLoadedTrajectory& Traj : Result.Trajectories)
		{
			UE_LOG(LogTemp, Log, TEXT("Loaded trajectory %lld with %d samples"),
				Traj.TrajectoryId, Traj.Samples.Num());
		}
	}
}

// Example 5: Async loading with progress callbacks
class UMyTrajectoryLoadingComponent : public UActorComponent
{
public:
	void StartAsyncLoad()
	{
		FTrajectoryDatasetInfo DatasetInfo;
		if (!UTrajectoryDataManager::Get()->GetDatasetInfo(TEXT("sample_dataset"), DatasetInfo))
		{
			return;
		}
		
		// Get loader and bind delegates
		UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
		Loader->OnLoadProgress.AddDynamic(this, &UMyTrajectoryLoadingComponent::OnLoadProgress);
		Loader->OnLoadComplete.AddDynamic(this, &UMyTrajectoryLoadingComponent::OnLoadComplete);
		
		// Create load parameters for larger dataset
		FTrajectoryLoadParams Params;
		Params.DatasetPath = DatasetInfo.DatasetPath;
		Params.StartTimeStep = -1;
		Params.EndTimeStep = -1;
		Params.SampleRate = 1;
		Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
		Params.NumTrajectories = 1000;
		
		// Start async load
		if (Loader->LoadTrajectoriesAsync(Params))
		{
			UE_LOG(LogTemp, Log, TEXT("Async loading started"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to start async load"));
		}
	}
	
	UFUNCTION()
	void OnLoadProgress(int32 TrajectoriesLoaded, int32 TotalTrajectories, float ProgressPercent)
	{
		UE_LOG(LogTemp, Log, TEXT("Loading progress: %d/%d (%.1f%%)"),
			TrajectoriesLoaded, TotalTrajectories, ProgressPercent);
		
		// Update UI progress bar
		// if (ProgressBar)
		// {
		//     ProgressBar->SetPercent(ProgressPercent / 100.0f);
		// }
	}
	
	UFUNCTION()
	void OnLoadComplete(bool bSuccess, const FTrajectoryLoadResult& Result)
	{
		if (bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("Loading complete! Loaded %d trajectories"), 
				Result.Trajectories.Num());
			UE_LOG(LogTemp, Log, TEXT("Memory used: %s"),
				*UTrajectoryDataBlueprintLibrary::FormatMemorySize(Result.MemoryUsedBytes));
			
			// Process loaded trajectories
			ProcessTrajectories(Result.Trajectories);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Loading failed: %s"), *Result.ErrorMessage);
		}
		
		// Unbind delegates
		UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
		Loader->OnLoadProgress.RemoveDynamic(this, &UMyTrajectoryLoadingComponent::OnLoadProgress);
		Loader->OnLoadComplete.RemoveDynamic(this, &UMyTrajectoryLoadingComponent::OnLoadComplete);
	}
	
	void ProcessTrajectories(const TArray<FLoadedTrajectory>& Trajectories)
	{
		// Process loaded trajectories
		for (const FLoadedTrajectory& Traj : Trajectories)
		{
			// Example: Create visualization
			for (int32 i = 0; i < Traj.Samples.Num() - 1; ++i)
			{
				if (Traj.Samples[i].bIsValid && Traj.Samples[i + 1].bIsValid)
				{
					// Example: Draw line between consecutive samples
					// DrawDebugLine(GetWorld(), 
					//     Traj.Samples[i].Position, 
					//     Traj.Samples[i + 1].Position,
					//     FColor::Green, false, 10.0f, 0, 2.0f);
				}
			}
		}
	}
};

// Example 6: Data streaming with time window adjustment
void ExampleDataStreaming()
{
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	FTrajectoryDatasetInfo DatasetInfo;
	if (!UTrajectoryDataManager::Get()->GetDatasetInfo(TEXT("sample_dataset"), DatasetInfo))
	{
		return;
	}
	
	// Load initial time window
	FTrajectoryLoadParams Params;
	Params.DatasetPath = DatasetInfo.DatasetPath;
	Params.StartTimeStep = 0;
	Params.EndTimeStep = 100;
	Params.SampleRate = 1;
	Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
	Params.NumTrajectories = 50;
	
	FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Loaded time window 0-100"));
	}
	
	// User adjusts time slider to new range
	// Unload old data and load new window
	Loader->UnloadAll();
	
	Params.StartTimeStep = 100;
	Params.EndTimeStep = 200;
	
	Result = Loader->LoadTrajectoriesSync(Params);
	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Loaded time window 100-200"));
	}
}

// Example 7: Memory management
void ExampleMemoryManagement()
{
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	
	// Check current memory usage
	int64 MemoryUsed = Loader->GetLoadedDataMemoryUsage();
	UE_LOG(LogTemp, Log, TEXT("Current memory usage: %s"),
		*UTrajectoryDataBlueprintLibrary::FormatMemorySize(MemoryUsed));
	
	// Check system memory
	int64 TotalMemory = UTrajectoryDataBlueprintLibrary::GetTotalPhysicalMemory();
	int64 MaxTrajectoryMemory = UTrajectoryDataBlueprintLibrary::GetMaxTrajectoryDataMemory();
	
	UE_LOG(LogTemp, Log, TEXT("Total system memory: %s"),
		*UTrajectoryDataBlueprintLibrary::FormatMemorySize(TotalMemory));
	UE_LOG(LogTemp, Log, TEXT("Max trajectory memory (75%%): %s"),
		*UTrajectoryDataBlueprintLibrary::FormatMemorySize(MaxTrajectoryMemory));
	
	// Get detailed memory info
	FTrajectoryDataMemoryInfo MemInfo = UTrajectoryDataBlueprintLibrary::GetMemoryInfo();
	UE_LOG(LogTemp, Log, TEXT("Available for trajectories: %s"),
		*UTrajectoryDataBlueprintLibrary::FormatMemorySize(MemInfo.RemainingCapacity));
	
	// Unload all when done
	Loader->UnloadAll();
	
	UE_LOG(LogTemp, Log, TEXT("All trajectories unloaded"));
}

// Example 8: Error handling
void ExampleErrorHandling()
{
	UTrajectoryDataLoader* Loader = UTrajectoryDataLoader::Get();
	
	FTrajectoryLoadParams Params;
	Params.DatasetPath = TEXT("C:/NonExistent/Path");
	Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
	Params.NumTrajectories = 100;
	
	// Always validate first
	FTrajectoryLoadValidation Validation = Loader->ValidateLoadParams(Params);
	if (!Validation.bCanLoad)
	{
		UE_LOG(LogTemp, Warning, TEXT("Validation failed: %s"), *Validation.Message);
		
		// Handle specific errors
		if (Validation.Message.Contains(TEXT("does not exist")))
		{
			// Path issue - show file browser
		}
		else if (Validation.Message.Contains(TEXT("Insufficient memory")))
		{
			// Memory issue - suggest reducing parameters
			UE_LOG(LogTemp, Warning, TEXT("Try reducing number of trajectories or increasing sample rate"));
		}
		
		return;
	}
	
	// Load
	FTrajectoryLoadResult Result = Loader->LoadTrajectoriesSync(Params);
	if (!Result.bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("Load failed: %s"), *Result.ErrorMessage);
		return;
	}
	
	// Success
	UE_LOG(LogTemp, Log, TEXT("Loaded %d trajectories"), Result.Trajectories.Num());
}
