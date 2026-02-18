// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDataCppApi.h"
#include "TrajectoryDataStructures.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/RunnableThread.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Async/Async.h"

FTrajectoryDataCppApi* FTrajectoryDataCppApi::Instance = nullptr;
FCriticalSection FTrajectoryDataCppApi::InstanceMutex;

FTrajectoryDataCppApi::FTrajectoryDataCppApi()
{
}

FTrajectoryDataCppApi::~FTrajectoryDataCppApi()
{
	// Wait for all tasks to complete
	FScopeLock Lock(&TaskMutex);
	for (TSharedPtr<FTrajectoryQueryTask>& Task : ActiveTasks)
	{
		if (Task.IsValid())
		{
			Task->Stop();
		}
	}
	ActiveTasks.Empty();
}

FTrajectoryDataCppApi* FTrajectoryDataCppApi::Get()
{
	// Double-checked locking pattern for thread-safe singleton initialization
	if (!Instance)
	{
		FScopeLock Lock(&InstanceMutex);
		if (!Instance)
		{
			Instance = new FTrajectoryDataCppApi();
		}
	}
	return Instance;
}

void FTrajectoryDataCppApi::CleanupCompletedTasks()
{
	FScopeLock Lock(&TaskMutex);
	
	// Remove completed tasks
	ActiveTasks.RemoveAll([](const TSharedPtr<FTrajectoryQueryTask>& Task) {
		return Task.IsValid() && Task->IsComplete();
	});
}

bool FTrajectoryDataCppApi::QuerySingleTimeStepAsync(
	const FString& DatasetPath,
	const TArray<int64>& TrajectoryIds,
	int32 TimeStep,
	FOnTrajectoryQueryComplete OnComplete)
{
	if (DatasetPath.IsEmpty() || TrajectoryIds.Num() == 0)
	{
		return false;
	}
	
	// Clean up old tasks
	CleanupCompletedTasks();
	
	// Create new query task
	TSharedPtr<FTrajectoryQueryTask> Task = MakeShared<FTrajectoryQueryTask>(
		FTrajectoryQueryTask::EQueryType::SingleTimeStep,
		DatasetPath,
		TrajectoryIds,
		TimeStep,
		TimeStep,
		OnComplete,
		FOnTrajectoryTimeRangeComplete()
	);
	
	// Add to active tasks
	{
		FScopeLock Lock(&TaskMutex);
		ActiveTasks.Add(Task);
	}
	
	return true;
}

bool FTrajectoryDataCppApi::QueryTimeRangeAsync(
	const FString& DatasetPath,
	const TArray<int64>& TrajectoryIds,
	int32 StartTimeStep,
	int32 EndTimeStep,
	FOnTrajectoryTimeRangeComplete OnComplete)
{
	if (DatasetPath.IsEmpty() || TrajectoryIds.Num() == 0 || StartTimeStep > EndTimeStep)
	{
		return false;
	}
	
	// Clean up old tasks
	CleanupCompletedTasks();
	
	// Create new query task
	TSharedPtr<FTrajectoryQueryTask> Task = MakeShared<FTrajectoryQueryTask>(
		FTrajectoryQueryTask::EQueryType::TimeRange,
		DatasetPath,
		TrajectoryIds,
		StartTimeStep,
		EndTimeStep,
		FOnTrajectoryQueryComplete(),
		OnComplete
	);
	
	// Add to active tasks
	{
		FScopeLock Lock(&TaskMutex);
		ActiveTasks.Add(Task);
	}
	
	return true;
}

// ============================================================================
// FTrajectoryQueryTask Implementation
// ============================================================================

FTrajectoryQueryTask::FTrajectoryQueryTask(
	EQueryType InQueryType,
	const FString& InDatasetPath,
	const TArray<int64>& InTrajectoryIds,
	int32 InStartTimeStep,
	int32 InEndTimeStep,
	FOnTrajectoryQueryComplete InSingleCallback,
	FOnTrajectoryTimeRangeComplete InRangeCallback)
	: QueryType(InQueryType)
	, DatasetPath(InDatasetPath)
	, TrajectoryIds(InTrajectoryIds)
	, StartTimeStep(InStartTimeStep)
	, EndTimeStep(InEndTimeStep)
	, SingleTimeStepCallback(InSingleCallback)
	, TimeRangeCallback(InRangeCallback)
	, Thread(nullptr)
	, bShouldStop(false)
	, bIsComplete(false)
{
	// Create and start thread
	Thread = FRunnableThread::Create(this, TEXT("TrajectoryQueryTask"));
}

FTrajectoryQueryTask::~FTrajectoryQueryTask()
{
	if (Thread)
	{
		// Request graceful stop
		Stop();
		
		// Wait for thread to complete
		Thread->WaitForCompletion();
		
		delete Thread;
		Thread = nullptr;
	}
}

bool FTrajectoryQueryTask::Init()
{
	return true;
}

uint32 FTrajectoryQueryTask::Run()
{
	// Execute the appropriate query type
	if (QueryType == EQueryType::SingleTimeStep)
	{
		ExecuteSingleTimeStepQuery();
	}
	else
	{
		ExecuteTimeRangeQuery();
	}
	
	// Invoke callback on game thread
	InvokeCallbackOnGameThread();
	
	bIsComplete = true;
	return 0;
}

void FTrajectoryQueryTask::Stop()
{
	bShouldStop = true;
}

void FTrajectoryQueryTask::Exit()
{
}

void FTrajectoryQueryTask::ExecuteSingleTimeStepQuery()
{
	SingleResult.bSuccess = false;
	
	// Validate dataset path
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DatasetPath))
	{
		SingleResult.ErrorMessage = FString::Printf(TEXT("Dataset directory does not exist: %s"), *DatasetPath);
		return;
	}
	
	// Read dataset metadata
	FString MetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-meta.bin"));
	if (!PlatformFile.FileExists(*MetaPath))
	{
		SingleResult.ErrorMessage = TEXT("dataset-meta.bin not found");
		return;
	}
	
	TArray<uint8> MetaData;
	if (!FFileHelper::LoadFileToArray(MetaData, *MetaPath))
	{
		SingleResult.ErrorMessage = TEXT("Failed to read dataset-meta.bin");
		return;
	}
	
	if (MetaData.Num() < sizeof(FDatasetMetaBinary))
	{
		SingleResult.ErrorMessage = TEXT("dataset-meta.bin is too small");
		return;
	}
	
	FDatasetMetaBinary DatasetMeta;
	FMemory::Memcpy(&DatasetMeta, MetaData.GetData(), sizeof(FDatasetMetaBinary));
	
	// Validate time step
	if (StartTimeStep < DatasetMeta.FirstTimeStep || StartTimeStep > DatasetMeta.LastTimeStep)
	{
		SingleResult.ErrorMessage = FString::Printf(
			TEXT("Time step %d is out of range [%d, %d]"),
			StartTimeStep, DatasetMeta.FirstTimeStep, DatasetMeta.LastTimeStep);
		return;
	}
	
	// Read trajectory metadata
	FString TrajMetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-trajmeta.bin"));
	if (!PlatformFile.FileExists(*TrajMetaPath))
	{
		SingleResult.ErrorMessage = TEXT("dataset-trajmeta.bin not found");
		return;
	}
	
	TArray<uint8> TrajMetaData;
	if (!FFileHelper::LoadFileToArray(TrajMetaData, *TrajMetaPath))
	{
		SingleResult.ErrorMessage = TEXT("Failed to read dataset-trajmeta.bin");
		return;
	}
	
	int32 NumTrajectories = TrajMetaData.Num() / sizeof(FTrajectoryMetaBinary);
	TArray<FTrajectoryMetaBinary> TrajMetas;
	TrajMetas.SetNum(NumTrajectories);
	FMemory::Memcpy(TrajMetas.GetData(), TrajMetaData.GetData(), TrajMetaData.Num());
	
	// Build map of trajectory ID to metadata
	TMap<int64, FTrajectoryMetaBinary> TrajMetaMap;
	for (const FTrajectoryMetaBinary& Meta : TrajMetas)
	{
		TrajMetaMap.Add(Meta.TrajectoryId, Meta);
	}
	
	// Calculate which shard file contains this time step
	int32 GlobalIntervalIndex = (StartTimeStep - DatasetMeta.FirstTimeStep) / DatasetMeta.TimeStepIntervalSize;
	FString ShardPath = FPaths::Combine(DatasetPath, FString::Printf(TEXT("shard-%d.bin"), GlobalIntervalIndex));
	
	if (!PlatformFile.FileExists(*ShardPath))
	{
		SingleResult.ErrorMessage = FString::Printf(TEXT("Shard file not found: %s"), *ShardPath);
		return;
	}
	
	// Load shard file
	TArray<uint8> ShardData;
	if (!FFileHelper::LoadFileToArray(ShardData, *ShardPath))
	{
		SingleResult.ErrorMessage = FString::Printf(TEXT("Failed to read shard file: %s"), *ShardPath);
		return;
	}
	
	if (ShardData.Num() < sizeof(FDataBlockHeaderBinary))
	{
		SingleResult.ErrorMessage = TEXT("Shard file is too small");
		return;
	}
	
	// Parse shard header
	FDataBlockHeaderBinary ShardHeader;
	FMemory::Memcpy(&ShardHeader, ShardData.GetData(), sizeof(FDataBlockHeaderBinary));
	
	// Calculate the time step index within this interval
	// Use the actual GlobalIntervalIndex from the shard header, not the calculated one
	int32 IntervalStartTimeStep = ShardHeader.GlobalIntervalIndex * DatasetMeta.TimeStepIntervalSize + DatasetMeta.FirstTimeStep;
	int32 TimeStepIndexInInterval = StartTimeStep - IntervalStartTimeStep;
	
	// Parse shard entries
	const uint8* DataPtr = ShardData.GetData() + ShardHeader.DataSectionOffset;
	int64 RemainingBytes = ShardData.Num() - ShardHeader.DataSectionOffset;
	
	for (int32 i = 0; i < ShardHeader.TrajectoryEntryCount && RemainingBytes > 0; ++i)
	{
		if (bShouldStop)
		{
			SingleResult.ErrorMessage = TEXT("Query was cancelled");
			return;
		}
		
		// Check if we have enough data for header
		if (RemainingBytes < sizeof(FTrajectoryEntryHeaderBinary))
		{
			break;
		}
		
		// Parse entry header
		FTrajectoryEntryHeaderBinary EntryHeader;
		FMemory::Memcpy(&EntryHeader, DataPtr, sizeof(FTrajectoryEntryHeaderBinary));
		DataPtr += sizeof(FTrajectoryEntryHeaderBinary);
		RemainingBytes -= sizeof(FTrajectoryEntryHeaderBinary);
		
		// Check if this trajectory is in our query list
		bool bIsRequested = TrajectoryIds.Contains(EntryHeader.TrajectoryId);
		
		// Calculate positions array size
		int32 PositionsArraySize = DatasetMeta.TimeStepIntervalSize * sizeof(FPositionSampleBinary);
		
		if (RemainingBytes < PositionsArraySize)
		{
			break;
		}
		
		// If this trajectory is requested, extract the sample
		if (bIsRequested)
		{
			// Check if this trajectory has valid data at the requested time step
			if (EntryHeader.StartTimeStepInInterval != -1 &&
				TimeStepIndexInInterval >= EntryHeader.StartTimeStepInInterval &&
				TimeStepIndexInInterval < EntryHeader.StartTimeStepInInterval + EntryHeader.ValidSampleCount)
			{
				// Calculate offset to the requested sample
				int32 SampleOffset = TimeStepIndexInInterval * sizeof(FPositionSampleBinary);
				const uint8* SamplePtr = DataPtr + SampleOffset;
				
				FPositionSampleBinary PosBinary;
				FMemory::Memcpy(&PosBinary, SamplePtr, sizeof(FPositionSampleBinary));
				
				// Check if sample is valid (not NaN)
				bool bIsValid = !FMath::IsNaN(PosBinary.X) && !FMath::IsNaN(PosBinary.Y) && !FMath::IsNaN(PosBinary.Z);
				
				FTrajectorySample Sample;
				Sample.TrajectoryId = EntryHeader.TrajectoryId;
				Sample.TimeStep = StartTimeStep;
				Sample.Position = FVector(PosBinary.X, PosBinary.Y, PosBinary.Z);
				Sample.bIsValid = bIsValid;
				
				SingleResult.Samples.Add(Sample);
			}
		}
		
		// Move to next entry
		DataPtr += PositionsArraySize;
		RemainingBytes -= PositionsArraySize;
	}
	
	SingleResult.bSuccess = true;
}

void FTrajectoryQueryTask::ExecuteTimeRangeQuery()
{
	RangeResult.bSuccess = false;
	
	// Validate dataset path
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DatasetPath))
	{
		RangeResult.ErrorMessage = FString::Printf(TEXT("Dataset directory does not exist: %s"), *DatasetPath);
		return;
	}
	
	// Read dataset metadata
	FString MetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-meta.bin"));
	if (!PlatformFile.FileExists(*MetaPath))
	{
		RangeResult.ErrorMessage = TEXT("dataset-meta.bin not found");
		return;
	}
	
	TArray<uint8> MetaData;
	if (!FFileHelper::LoadFileToArray(MetaData, *MetaPath))
	{
		RangeResult.ErrorMessage = TEXT("Failed to read dataset-meta.bin");
		return;
	}
	
	if (MetaData.Num() < sizeof(FDatasetMetaBinary))
	{
		RangeResult.ErrorMessage = TEXT("dataset-meta.bin is too small");
		return;
	}
	
	FDatasetMetaBinary DatasetMeta;
	FMemory::Memcpy(&DatasetMeta, MetaData.GetData(), sizeof(FDatasetMetaBinary));
	
	// Validate time range
	if (StartTimeStep < DatasetMeta.FirstTimeStep || EndTimeStep > DatasetMeta.LastTimeStep)
	{
		RangeResult.ErrorMessage = FString::Printf(
			TEXT("Time range [%d, %d] is out of dataset range [%d, %d]"),
			StartTimeStep, EndTimeStep, DatasetMeta.FirstTimeStep, DatasetMeta.LastTimeStep);
		return;
	}
	
	// Read trajectory metadata
	FString TrajMetaPath = FPaths::Combine(DatasetPath, TEXT("dataset-trajmeta.bin"));
	if (!PlatformFile.FileExists(*TrajMetaPath))
	{
		RangeResult.ErrorMessage = TEXT("dataset-trajmeta.bin not found");
		return;
	}
	
	TArray<uint8> TrajMetaData;
	if (!FFileHelper::LoadFileToArray(TrajMetaData, *TrajMetaPath))
	{
		RangeResult.ErrorMessage = TEXT("Failed to read dataset-trajmeta.bin");
		return;
	}
	
	int32 NumTrajectories = TrajMetaData.Num() / sizeof(FTrajectoryMetaBinary);
	TArray<FTrajectoryMetaBinary> TrajMetas;
	TrajMetas.SetNum(NumTrajectories);
	FMemory::Memcpy(TrajMetas.GetData(), TrajMetaData.GetData(), TrajMetaData.Num());
	
	// Build map of trajectory ID to metadata
	TMap<int64, FTrajectoryMetaBinary> TrajMetaMap;
	for (const FTrajectoryMetaBinary& Meta : TrajMetas)
	{
		TrajMetaMap.Add(Meta.TrajectoryId, Meta);
	}
	
	// Initialize result time series for each requested trajectory
	TMap<int64, FTrajectoryTimeSeries> TimeSeriesMap;
	for (int64 TrajId : TrajectoryIds)
	{
		FTrajectoryTimeSeries Series;
		Series.TrajectoryId = TrajId;
		Series.StartTimeStep = StartTimeStep;
		Series.EndTimeStep = EndTimeStep;
		
		// Get extent from metadata if available
		if (const FTrajectoryMetaBinary* Meta = TrajMetaMap.Find(TrajId))
		{
			Series.Extent = FVector(Meta->Extent[0], Meta->Extent[1], Meta->Extent[2]);
		}
		
		// Preallocate samples array
		int32 NumSamples = EndTimeStep - StartTimeStep + 1;
		Series.Samples.SetNumZeroed(NumSamples);
		
		TimeSeriesMap.Add(TrajId, Series);
	}
	
	// Calculate which shards we need to load
	int32 StartIntervalIndex = (StartTimeStep - DatasetMeta.FirstTimeStep) / DatasetMeta.TimeStepIntervalSize;
	int32 EndIntervalIndex = (EndTimeStep - DatasetMeta.FirstTimeStep) / DatasetMeta.TimeStepIntervalSize;
	
	// Load each required shard
	for (int32 IntervalIndex = StartIntervalIndex; IntervalIndex <= EndIntervalIndex; ++IntervalIndex)
	{
		if (bShouldStop)
		{
			RangeResult.ErrorMessage = TEXT("Query was cancelled");
			return;
		}
		
		FString ShardPath = FPaths::Combine(DatasetPath, FString::Printf(TEXT("shard-%d.bin"), IntervalIndex));
		
		if (!PlatformFile.FileExists(*ShardPath))
		{
			continue; // Skip missing shards
		}
		
		// Load shard file
		TArray<uint8> ShardData;
		if (!FFileHelper::LoadFileToArray(ShardData, *ShardPath))
		{
			continue; // Skip failed loads
		}
		
		if (ShardData.Num() < sizeof(FDataBlockHeaderBinary))
		{
			continue;
		}
		
		// Parse shard header
		FDataBlockHeaderBinary ShardHeader;
		FMemory::Memcpy(&ShardHeader, ShardData.GetData(), sizeof(FDataBlockHeaderBinary));
		
		int32 IntervalStartTimeStep = IntervalIndex * DatasetMeta.TimeStepIntervalSize + DatasetMeta.FirstTimeStep;
		
		// Parse shard entries
		const uint8* DataPtr = ShardData.GetData() + ShardHeader.DataSectionOffset;
		int64 RemainingBytes = ShardData.Num() - ShardHeader.DataSectionOffset;
		
		for (int32 i = 0; i < ShardHeader.TrajectoryEntryCount && RemainingBytes > 0; ++i)
		{
			if (bShouldStop)
			{
				RangeResult.ErrorMessage = TEXT("Query was cancelled");
				return;
			}
			
			// Check if we have enough data for header
			if (RemainingBytes < sizeof(FTrajectoryEntryHeaderBinary))
			{
				break;
			}
			
			// Parse entry header
			FTrajectoryEntryHeaderBinary EntryHeader;
			FMemory::Memcpy(&EntryHeader, DataPtr, sizeof(FTrajectoryEntryHeaderBinary));
			DataPtr += sizeof(FTrajectoryEntryHeaderBinary);
			RemainingBytes -= sizeof(FTrajectoryEntryHeaderBinary);
			
			// Check if this trajectory is in our query list
			FTrajectoryTimeSeries* Series = TimeSeriesMap.Find(EntryHeader.TrajectoryId);
			
			// Calculate positions array size
			int32 PositionsArraySize = DatasetMeta.TimeStepIntervalSize * sizeof(FPositionSampleBinary);
			
			if (RemainingBytes < PositionsArraySize)
			{
				break;
			}
			
			// If this trajectory is requested, extract samples in our time range
			if (Series && EntryHeader.StartTimeStepInInterval != -1)
			{
				// Calculate which samples to extract
				int32 FirstSampleInInterval = EntryHeader.StartTimeStepInInterval;
				int32 LastSampleInInterval = FirstSampleInInterval + EntryHeader.ValidSampleCount - 1;
				
				for (int32 TimeStepInInterval = 0; TimeStepInInterval < DatasetMeta.TimeStepIntervalSize; ++TimeStepInInterval)
				{
					int32 AbsoluteTimeStep = IntervalStartTimeStep + TimeStepInInterval;
					
					// Check if this time step is in our query range
					if (AbsoluteTimeStep >= StartTimeStep && AbsoluteTimeStep <= EndTimeStep)
					{
						// Check if this time step has valid data
						if (TimeStepInInterval >= FirstSampleInInterval && TimeStepInInterval <= LastSampleInInterval)
						{
							// Extract sample
							int32 SampleOffset = TimeStepInInterval * sizeof(FPositionSampleBinary);
							const uint8* SamplePtr = DataPtr + SampleOffset;
							
							FPositionSampleBinary PosBinary;
							FMemory::Memcpy(&PosBinary, SamplePtr, sizeof(FPositionSampleBinary));
							
							// Check if sample is valid (not NaN)
							if (!FMath::IsNaN(PosBinary.X) && !FMath::IsNaN(PosBinary.Y) && !FMath::IsNaN(PosBinary.Z))
							{
								// Store in result
								int32 ResultIndex = AbsoluteTimeStep - StartTimeStep;
								Series->Samples[ResultIndex] = FVector(PosBinary.X, PosBinary.Y, PosBinary.Z);
							}
						}
					}
				}
			}
			
			// Move to next entry
			DataPtr += PositionsArraySize;
			RemainingBytes -= PositionsArraySize;
		}
	}
	
	// Convert map to array
	for (auto& Pair : TimeSeriesMap)
	{
		RangeResult.TimeSeries.Add(Pair.Value);
	}
	
	RangeResult.bSuccess = true;
}

void FTrajectoryQueryTask::InvokeCallbackOnGameThread()
{
	// Schedule callback on game thread
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		if (QueryType == EQueryType::SingleTimeStep && SingleTimeStepCallback.IsBound())
		{
			SingleTimeStepCallback.Execute(SingleResult);
		}
		else if (QueryType == EQueryType::TimeRange && TimeRangeCallback.IsBound())
		{
			TimeRangeCallback.Execute(RangeResult);
		}
	});
}
