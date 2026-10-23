#include "TrajectoryDataLoader.h"
#include "TrajectoryDataBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "DatasetVisualizationActor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    template<class T> void AppendIdentityBytes(TArray<uint8>& Bytes, const T& Value)
    {
        Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T));
    }
    bool WriteIdentityFixture(const FString& Directory)
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
        FDatasetMetaBinary Meta{};
        FMemory::Memcpy(Meta.Magic, "TDSH", 4);
        Meta.FormatVersion = 1;
        Meta.LastTimeStep = 2;
        Meta.TimeStepIntervalSize = 3;
        Meta.TrajectoryCount = 3;
        Meta.FirstTrajectoryId = 7;
        Meta.LastTrajectoryId = 3000000001ULL;
        Meta.EntrySizeBytes = sizeof(FTrajectoryEntryHeaderBinary) + 3 * sizeof(FVector3f);
        TArray<uint8> Bytes;
        AppendIdentityBytes(Bytes, Meta);
        if (!FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(Directory, TEXT("dataset-meta.bin")))) return false;
        const TArray<uint64> Ids = {518, 7, 3000000001ULL};
        Bytes.Reset();
        for (const uint64 Id : Ids)
        {
            FTrajectoryMetaBinary Trajectory{};
            Trajectory.TrajectoryId = Id;
            Trajectory.EndTimeStep = 2;
            AppendIdentityBytes(Bytes, Trajectory);
        }
        if (!FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(Directory, TEXT("dataset-trajmeta.bin")))) return false;
        FDataBlockHeaderBinary Header{};
        FMemory::Memcpy(Header.Magic, "TDDB", 4);
        Header.FormatVersion = 1;
        Header.TimeStepIntervalSize = 3;
        Header.TrajectoryEntryCount = 3;
        Header.DataSectionOffset = sizeof(Header);
        Bytes.Reset();
        AppendIdentityBytes(Bytes, Header);
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
        {
            FTrajectoryEntryHeaderBinary Entry{};
            Entry.TrajectoryId = Ids[Index];
            Entry.ValidSampleCount = 3;
            AppendIdentityBytes(Bytes, Entry);
            for (int32 Time = 0; Time < 3; ++Time) AppendIdentityBytes(Bytes, FVector3f(Index, Time, 0));
        }
        return FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(Directory, TEXT("shard-0.bin")));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrajectoryLoadedIdentityTest, "TrajectoryData.Identity.LoadedIndexAndSourceId",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTrajectoryLoadedIdentityTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/TrajectoryIdentity"), FGuid::NewGuid().ToString());
    if (!WriteIdentityFixture(Directory)) { AddError(TEXT("Could not write identity fixture")); return false; }
    auto* Loader = NewObject<UTrajectoryDataLoader>();
    FTrajectoryDatasetInfo Dataset;
    Dataset.DatasetPath = Directory;
    FTrajectoryLoadParams Params;
    Params.SelectionStrategy = ETrajectorySelectionStrategy::ExplicitList;
    for (const int64 Id : {3000000001LL, 518LL, 3000000001LL})
    {
        FTrajectoryLoadSelection Selection;
        Selection.TrajectoryId = Id;
        Params.TrajectorySelections.Add(Selection);
    }
    const auto Result = Loader->LoadTrajectoriesSync(Dataset, Params);
    TestTrue(TEXT("Reordered subset loads"), Result.bSuccess);
    TestEqual(TEXT("Duplicate requested IDs are deduplicated"), Result.Trajectories.Num(), 2);
    if (Result.Trajectories.Num() == 2)
    {
        TestEqual(TEXT("Requested first source ID is retained"), Result.Trajectories[0].TrajectoryId, 3000000001LL);
        TestEqual(TEXT("Requested second source ID is retained"), Result.Trajectories[1].TrajectoryId, 518LL);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            TestEqual(TEXT("Result index matches loaded array position"), Result.Trajectories[Index].TrajectoryIndex, Index);
            TestEqual(TEXT("Stored dataset preserves index"), Loader->GetLoadedDatasets()[0].Trajectories[Index].TrajectoryIndex, Index);
        }
        TestTrue(TEXT("First subset selects the correct source samples"), Result.Trajectories[0].Samples.Num() > 0 && Result.Trajectories[0].Samples[0].X == 2);
        int64 Id = -1;
        TestTrue(TEXT("Index resolves source identity"), UTrajectoryDataBlueprintLibrary::TryGetTrajectoryIdByIndex(Result.Trajectories, 0, Id));
        TestEqual(TEXT("Lookup preserves 64-bit ID"), Id, 3000000001LL);
        TestEqual(TEXT("Slider maximum uses final valid index"), UTrajectoryDataBlueprintLibrary::GetTrajectorySliderMaximum(Result.Trajectories), 1.0f);
        TestEqual(TEXT("Slider rounds to nearest index"), UTrajectoryDataBlueprintLibrary::ResolveTrajectorySliderIndex(Result.Trajectories, 0.7f), 1);
        TestEqual(TEXT("Slider clamps high values"), UTrajectoryDataBlueprintLibrary::ResolveTrajectorySliderIndex(Result.Trajectories, 100), 1);
        TestEqual(TEXT("Slider clamps negative values"), UTrajectoryDataBlueprintLibrary::ResolveTrajectorySliderIndex(Result.Trajectories, -100), 0);
        TestFalse(TEXT("Index equal to count is invalid"), UTrajectoryDataBlueprintLibrary::TryGetTrajectoryIdByIndex(Result.Trajectories, 2, Id));
        TestEqual(TEXT("Invalid lookup resets ID"), Id, -1LL);
        TestTrue(TEXT("Label displays original ID without float conversion"), UTrajectoryDataBlueprintLibrary::GetTrajectorySelectionLabel(Result.Trajectories, 0).ToString().Contains(TEXT("3000000001")));
    }
    Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Params.NumTrajectories = 1;
    const auto Other = Loader->LoadTrajectoriesSync(Dataset, Params);
    TestTrue(TEXT("Second load succeeds"), Other.bSuccess);
    if (!Other.Trajectories.IsEmpty())
    {
        TestEqual(TEXT("Each loaded dataset starts at index zero"), Other.Trajectories[0].TrajectoryIndex, 0);
        TestEqual(TEXT("First-N preserves metadata selection order"), Other.Trajectories[0].TrajectoryId, 518LL);
    }
    const TArray<FLoadedTrajectory> Empty;
    TestFalse(TEXT("Empty dataset disables selector"), UTrajectoryDataBlueprintLibrary::HasLoadedTrajectories(Empty));
    TestEqual(TEXT("Empty selector has no valid index"), UTrajectoryDataBlueprintLibrary::ResolveTrajectorySliderIndex(Empty, 0), INDEX_NONE);
    Loader->UnloadAll();
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizedDatasetIdentityTest, "TrajectoryData.Identity.VisualizedDatasetSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVisualizedDatasetIdentityTest::RunTest(const FString& Parameters)
{
    auto* Loader = UTrajectoryDataLoader::Get();
    if (!Loader->GetLoadedDatasets().IsEmpty() || Loader->IsLoadingAsync())
    { AddError(TEXT("Run dataset snapshot test in an isolated editor with no loaded datasets.")); return false; }
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),
        TEXT("codex/Automation/VisualizationIdentity"), FGuid::NewGuid().ToString()));
    ON_SCOPE_EXIT { Loader->UnloadAll(); IFileManager::Get().DeleteDirectory(*Directory, false, true); };
    if (!WriteIdentityFixture(Directory)) { AddError(TEXT("Could not write local fixture")); return false; }
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!World) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Actor = World->SpawnActor<ADatasetVisualizationActor>();
    if (!Actor) return false;
    Actor->bAutoActivate = false;
    FTrajectoryDatasetInfo Snapshot;
    TestFalse(TEXT("No identity before first bind"), Actor->GetVisualizedDatasetInfo(Snapshot));
    FTrajectoryDatasetInfo Dataset;
    Dataset.UniqueDSName = TEXT("fixture/scenario/overview");
    Dataset.DatasetPath = Directory;
    Dataset.Metadata.LastTimeStep = 2;
    FTrajectoryLoadParams Params;
    Params.SelectionStrategy = ETrajectorySelectionStrategy::FirstN;
    Params.NumTrajectories = 2;
    const auto Result = Loader->LoadTrajectoriesSync(Dataset, Params);
    if (!TestTrue(TEXT("Load fixture"), Result.bSuccess)) return false;
    if (!TestTrue(TEXT("Bind loaded dataset"), Actor->LoadAndBindDatasetWithTrajectoryCap(0, 1))) return false;
    TestTrue(TEXT("Bound identity exposed"), Actor->GetVisualizedDatasetInfo(Snapshot));
    TestEqual(TEXT("Original stable ID"), Snapshot.UniqueDSName, Dataset.UniqueDSName);
    TestEqual(TEXT("Original source path"), Snapshot.DatasetPath, Directory);
    TestFalse(TEXT("Invalid index fails"), Actor->LoadAndBindDatasetWithTrajectoryCap(123456, 1));
    Loader->UnloadAll();
    TestTrue(TEXT("Identity survives cache unload and failed bind"), Actor->GetVisualizedDatasetInfo(Snapshot));
    TestEqual(TEXT("Snapshot retains original identity"), Snapshot.UniqueDSName, Dataset.UniqueDSName);
    return true;
}
#endif
