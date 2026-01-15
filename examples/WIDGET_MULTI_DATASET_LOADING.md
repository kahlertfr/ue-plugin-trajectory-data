# Example: Loading Multiple Datasets from UMG Widget with One Button

This example demonstrates how to load two datasets (specified by unique names) with a single button click in a UMG widget. The example handles async loading with separate completion handlers for each dataset.

## Blueprint Widget Setup

### Widget Hierarchy
```
- Canvas Panel
  - Vertical Box
    - Text Block (Title)
    - Horizontal Box
      - Text Box (Dataset1Name)
      - Text Box (Dataset2Name)
    - Button (LoadBothButton)
    - Text Block (StatusText)
    - Progress Bar (LoadingProgress)
```

### Variables
- `Dataset1Name` (Text) - Name of first dataset
- `Dataset2Name` (Text) - Name of second dataset
- `StatusText` (Text Block Reference)
- `LoadingProgress` (Progress Bar Reference)
- `LoadedCount` (Integer) - Tracks how many datasets have loaded
- `TotalToLoad` (Integer) - Total datasets to load (always 2)

## Blueprint Graph - Button Click Event

### On Load Both Button Clicked

```blueprint
Event OnClicked (LoadBothButton)
|
├─> Set LoadedCount = 0
├─> Set TotalToLoad = 2
├─> Set Text (StatusText) = "Starting to load datasets..."
├─> Set Percent (LoadingProgress) = 0.0
|
├─> Get Trajectory Loader
|   |
|   ├─> Bind Event to OnLoadComplete (Custom Event: OnDataset1Complete)
|   |
|   └─> Load Dataset 1
|       |
|       ├─> Get Available Datasets
|       |   |
|       |   └─> Find Dataset by Name (Dataset1Name)
|       |       |
|       |       └─> If Found:
|       |           |
|       |           ├─> Make TrajectoryLoadParams
|       |           |   ├─> StartTimeStep = -1
|       |           |   ├─> EndTimeStep = -1
|       |           |   ├─> SampleRate = 1
|       |           |   ├─> SelectionStrategy = FirstN
|       |           |   └─> NumTrajectories = 100
|       |           |
|       |           └─> LoadTrajectoriesAsync (DatasetInfo, Params)
|       |
|       └─> If Not Found:
|           └─> Set Text (StatusText) = "Dataset 1 not found!"
|
└─> Load Dataset 2 (Same pattern as Dataset 1)
    |
    ├─> Bind Event to OnLoadComplete (Custom Event: OnDataset2Complete)
    |
    └─> [Same loading logic as Dataset 1]
```

### Custom Event: OnDataset1Complete

```blueprint
Event OnDataset1Complete (bSuccess, Result)
|
├─> Branch (bSuccess)
|   |
|   ├─> True:
|   |   |
|   |   ├─> Increment LoadedCount
|   |   |
|   |   ├─> Print String: "Dataset 1 loaded: {Trajectories.Num} trajectories"
|   |   |
|   |   └─> Update Progress
|   |       |
|   |       └─> Set Percent (LoadingProgress) = LoadedCount / TotalToLoad
|   |
|   └─> False:
|       |
|       └─> Set Text (StatusText) = "Failed to load Dataset 1: {ErrorMessage}"
|
└─> Check If All Loaded
    |
    └─> Branch (LoadedCount == TotalToLoad)
        |
        ├─> True:
        |   |
        |   ├─> Unbind Event from OnLoadComplete
        |   |
        |   ├─> Get Loaded Datasets
        |   |   |
        |   |   └─> Set Text (StatusText) = "All datasets loaded! Total: {LoadedDatasets.Num}"
        |   |
        |   └─> Set Percent (LoadingProgress) = 1.0
        |
        └─> False:
            |
            └─> Set Text (StatusText) = "Loading... {LoadedCount}/{TotalToLoad} complete"
```

### Custom Event: OnDataset2Complete

```blueprint
Event OnDataset2Complete (bSuccess, Result)
|
└─> [Same logic as OnDataset1Complete]
```

## C++ Widget Implementation

For a C++ implementation, here's the equivalent code:

```cpp
// Header file
UCLASS()
class UMultiDatasetWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* StatusText;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UProgressBar* LoadingProgress;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UEditableTextBox* Dataset1NameBox;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UEditableTextBox* Dataset2NameBox;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UButton* LoadBothButton;

protected:
    virtual void NativeConstruct() override;

private:
    UFUNCTION()
    void OnLoadBothClicked();

    UFUNCTION()
    void OnDataset1Complete(bool bSuccess, const FTrajectoryLoadResult& Result);

    UFUNCTION()
    void OnDataset2Complete(bool bSuccess, const FTrajectoryLoadResult& Result);

    void UpdateProgress();

    int32 LoadedCount;
    int32 TotalToLoad;
    UTrajectoryDataLoader* Loader;
};

// Implementation file
void UMultiDatasetWidget::NativeConstruct()
{
    Super::NativeConstruct();

    LoadBothButton->OnClicked.AddDynamic(this, &UMultiDatasetWidget::OnLoadBothClicked);
    Loader = UTrajectoryDataLoader::Get();
}

void UMultiDatasetWidget::OnLoadBothClicked()
{
    LoadedCount = 0;
    TotalToLoad = 2;
    
    StatusText->SetText(FText::FromString(TEXT("Starting to load datasets...")));
    LoadingProgress->SetPercent(0.0f);

    UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();

    // Load Dataset 1
    FString Dataset1Name = Dataset1NameBox->GetText().ToString();
    FTrajectoryDatasetInfo Dataset1Info;
    if (Manager->GetDatasetInfo(Dataset1Name, Dataset1Info))
    {
        // Bind completion event for Dataset 1
        Loader->OnLoadComplete.AddDynamic(this, &UMultiDatasetWidget::OnDataset1Complete);

        FTrajectoryLoadParams Params1;
        Params1.StartTimeStep = -1;
        Params1.EndTimeStep = -1;
        Params1.SampleRate = 1;
        Params1.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
        Params1.NumTrajectories = 100;

        Loader->LoadTrajectoriesAsync(Dataset1Info, Params1);
    }
    else
    {
        StatusText->SetText(FText::FromString(TEXT("Dataset 1 not found!")));
        return;
    }

    // Load Dataset 2
    FString Dataset2Name = Dataset2NameBox->GetText().ToString();
    FTrajectoryDatasetInfo Dataset2Info;
    if (Manager->GetDatasetInfo(Dataset2Name, Dataset2Info))
    {
        // Bind completion event for Dataset 2
        // Note: We need a different approach here because OnLoadComplete will fire twice
        // Instead, we'll use a lambda or check in the same handler

        FTrajectoryLoadParams Params2;
        Params2.StartTimeStep = -1;
        Params2.EndTimeStep = -1;
        Params2.SampleRate = 1;
        Params2.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
        Params2.NumTrajectories = 100;

        Loader->LoadTrajectoriesAsync(Dataset2Info, Params2);
    }
    else
    {
        StatusText->SetText(FText::FromString(TEXT("Dataset 2 not found!")));
        return;
    }
}

void UMultiDatasetWidget::OnDataset1Complete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    // Unbind this handler
    Loader->OnLoadComplete.RemoveDynamic(this, &UMultiDatasetWidget::OnDataset1Complete);

    if (bSuccess)
    {
        LoadedCount++;
        UE_LOG(LogTemp, Log, TEXT("Dataset 1 loaded: %d trajectories"), Result.Trajectories.Num());
        UpdateProgress();

        // Now bind for Dataset 2
        if (LoadedCount < TotalToLoad)
        {
            Loader->OnLoadComplete.AddDynamic(this, &UMultiDatasetWidget::OnDataset2Complete);
        }
    }
    else
    {
        StatusText->SetText(FText::FromString(FString::Printf(TEXT("Failed to load Dataset 1: %s"), 
            *Result.ErrorMessage)));
    }
}

void UMultiDatasetWidget::OnDataset2Complete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    // Unbind this handler
    Loader->OnLoadComplete.RemoveDynamic(this, &UMultiDatasetWidget::OnDataset2Complete);

    if (bSuccess)
    {
        LoadedCount++;
        UE_LOG(LogTemp, Log, TEXT("Dataset 2 loaded: %d trajectories"), Result.Trajectories.Num());
        UpdateProgress();
    }
    else
    {
        StatusText->SetText(FText::FromString(FString::Printf(TEXT("Failed to load Dataset 2: %s"), 
            *Result.ErrorMessage)));
    }
}

void UMultiDatasetWidget::UpdateProgress()
{
    float Progress = (float)LoadedCount / (float)TotalToLoad;
    LoadingProgress->SetPercent(Progress);

    if (LoadedCount == TotalToLoad)
    {
        const TArray<FLoadedDataset>& LoadedDatasets = Loader->GetLoadedDatasets();
        StatusText->SetText(FText::FromString(FString::Printf(
            TEXT("All datasets loaded! Total datasets: %d"), LoadedDatasets.Num())));
        
        // Process loaded datasets
        for (const FLoadedDataset& Dataset : LoadedDatasets)
        {
            UE_LOG(LogTemp, Log, TEXT("Dataset: %s, Trajectories: %d, Memory: %lld bytes"),
                *Dataset.DatasetInfo.DatasetPath,
                Dataset.Trajectories.Num(),
                Dataset.MemoryUsedBytes);
        }
    }
    else
    {
        StatusText->SetText(FText::FromString(FString::Printf(
            TEXT("Loading... %d/%d complete"), LoadedCount, TotalToLoad)));
    }
}
```

## Better Approach: Sequential Loading

The above approach has a timing issue where both async loads start simultaneously and the OnLoadComplete delegate fires twice. A better approach is to load sequentially:

```cpp
void UMultiDatasetWidget::OnLoadBothClicked()
{
    LoadedCount = 0;
    TotalToLoad = 2;
    
    StatusText->SetText(FText::FromString(TEXT("Starting to load datasets...")));
    LoadingProgress->SetPercent(0.0f);

    // Start by loading Dataset 1
    LoadNextDataset();
}

void UMultiDatasetWidget::LoadNextDataset()
{
    UTrajectoryDataManager* Manager = UTrajectoryDataManager::Get();

    if (LoadedCount == 0)
    {
        // Load Dataset 1
        FString Dataset1Name = Dataset1NameBox->GetText().ToString();
        FTrajectoryDatasetInfo Dataset1Info;
        if (Manager->GetDatasetInfo(Dataset1Name, Dataset1Info))
        {
            Loader->OnLoadComplete.AddDynamic(this, &UMultiDatasetWidget::OnAnyDatasetComplete);

            FTrajectoryLoadParams Params;
            Params.StartTimeStep = -1;
            Params.EndTimeStep = -1;
            Params.SampleRate = 1;
            Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
            Params.NumTrajectories = 100;

            Loader->LoadTrajectoriesAsync(Dataset1Info, Params);
        }
        else
        {
            StatusText->SetText(FText::FromString(TEXT("Dataset 1 not found!")));
        }
    }
    else if (LoadedCount == 1)
    {
        // Load Dataset 2
        FString Dataset2Name = Dataset2NameBox->GetText().ToString();
        FTrajectoryDatasetInfo Dataset2Info;
        if (Manager->GetDatasetInfo(Dataset2Name, Dataset2Info))
        {
            FTrajectoryLoadParams Params;
            Params.StartTimeStep = -1;
            Params.EndTimeStep = -1;
            Params.SampleRate = 1;
            Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
            Params.NumTrajectories = 100;

            Loader->LoadTrajectoriesAsync(Dataset2Info, Params);
        }
        else
        {
            StatusText->SetText(FText::FromString(TEXT("Dataset 2 not found!")));
        }
    }
}

void UMultiDatasetWidget::OnAnyDatasetComplete(bool bSuccess, const FTrajectoryLoadResult& Result)
{
    if (bSuccess)
    {
        LoadedCount++;
        UE_LOG(LogTemp, Log, TEXT("Dataset %d loaded: %d trajectories"), 
            LoadedCount, Result.Trajectories.Num());

        if (LoadedCount < TotalToLoad)
        {
            // Load next dataset
            LoadNextDataset();
        }
        else
        {
            // All done
            Loader->OnLoadComplete.RemoveDynamic(this, &UMultiDatasetWidget::OnAnyDatasetComplete);
            
            const TArray<FLoadedDataset>& LoadedDatasets = Loader->GetLoadedDatasets();
            StatusText->SetText(FText::FromString(FString::Printf(
                TEXT("All %d datasets loaded successfully!"), LoadedDatasets.Num())));
            LoadingProgress->SetPercent(1.0f);
        }
    }
    else
    {
        Loader->OnLoadComplete.RemoveDynamic(this, &UMultiDatasetWidget::OnAnyDatasetComplete);
        StatusText->SetText(FText::FromString(FString::Printf(
            TEXT("Failed to load dataset: %s"), *Result.ErrorMessage)));
    }

    UpdateProgress();
}

void UMultiDatasetWidget::UpdateProgress()
{
    float Progress = (float)LoadedCount / (float)TotalToLoad;
    LoadingProgress->SetPercent(Progress);

    if (LoadedCount < TotalToLoad)
    {
        StatusText->SetText(FText::FromString(FString::Printf(
            TEXT("Loading... %d/%d complete"), LoadedCount, TotalToLoad)));
    }
}
```

This sequential approach ensures that:
1. Datasets load one at a time
2. The OnLoadComplete delegate only fires once per load
3. Progress is tracked accurately
4. Both datasets end up in the LoadedDatasets array
