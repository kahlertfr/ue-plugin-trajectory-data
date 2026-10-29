#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "TrajectoryDataSettings.h"
#include "TrajectoryDataManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrajectoryExternalConfigTest, "TrajectoryData.Config.ExternalDatasetPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrajectoryExternalConfigTest::RunTest(const FString& Parameters)
{
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("codex/Automation/ExternalConfig") / FGuid::NewGuid().ToString());
    const FString Data = Root / TEXT("Shared Data");
    const FString ConfigDirectory = Root / TEXT("Package");
    const FString Filename = ConfigDirectory / TEXT("TrajectoryData.ini");
    const FString Dataset = Data / TEXT("domain/scenario/dataset");
    ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Root, false, true); };
    TestTrue(TEXT("Create synthetic dataset"), IFileManager::Get().MakeDirectory(*Dataset, true));
    TestTrue(TEXT("Create synthetic package"), IFileManager::Get().MakeDirectory(*ConfigDirectory, true));
    TestTrue(TEXT("Write discovery manifest"), FFileHelper::SaveStringToFile(TEXT("{}"), *(Dataset / TEXT("dataset-manifest.json"))));
    auto* Settings = NewObject<UTrajectoryDataSettings>();
    Settings->ScenariosDirectory = TEXT("previous root");
    const FString Section = TEXT("[/Script/TrajectoryData.TrajectoryDataSettings]\n");
    FString Error;
    const auto Write = [&](const FString& Contents) { return FFileHelper::SaveStringToFile(Contents, *Filename); };
    TestFalse(TEXT("Unreadable/missing config rejected"), Settings->LoadExternalConfiguration(Filename, Error));
    TestEqual(TEXT("Failure preserves settings"), Settings->ScenariosDirectory, FString(TEXT("previous root")));
    TestTrue(TEXT("Write relative path"), Write(Section + TEXT("ScenariosDirectory=\"../Shared Data\"\n")));
    TestTrue(TEXT("Relative path with spaces loads"), Settings->LoadExternalConfiguration(Filename, Error));
    TestTrue(TEXT("Path is relative to INI, not executable or working directory"), FPaths::IsSamePath(Settings->ScenariosDirectory, Data));
    FString SavedContents;
    FFileHelper::LoadFileToString(SavedContents, *Filename);
    TestEqual(TEXT("Reading never rewrites config"), SavedContents, Section + TEXT("ScenariosDirectory=\"../Shared Data\"\n"));
    TestTrue(TEXT("Write absolute path"), Write(Section + TEXT("ScenariosDirectory=") + Data + TEXT("\n")));
    TestTrue(TEXT("Absolute path loads"), Settings->LoadExternalConfiguration(Filename, Error));
    TestTrue(TEXT("Absolute path preserved"), FPaths::IsSamePath(Settings->ScenariosDirectory, Data));
    for (const FString& Invalid : { FString(TEXT("malformed")), Section, Section + TEXT("ScenariosDirectory=\n"),
        Section + TEXT("ScenariosDirectory=does-not-exist\n") })
    {
        TestTrue(TEXT("Write invalid override"), Write(Invalid));
        TestFalse(TEXT("Invalid override rejected"), Settings->LoadExternalConfiguration(Filename, Error));
        TestFalse(TEXT("Invalid override provides a diagnostic"), Error.IsEmpty());
        TestTrue(TEXT("Rejected override does not partly change settings"), FPaths::IsSamePath(Settings->ScenariosDirectory, Data));
    }
    auto* Defaults = UTrajectoryDataSettings::Get();
    const FString Previous = Defaults->ScenariosDirectory;
    ON_SCOPE_EXIT { Defaults->ScenariosDirectory = Previous; };
    TestTrue(TEXT("Rewrite valid override"), Write(Section + TEXT("ScenariosDirectory=../Shared Data\n")));
    TestTrue(TEXT("Apply override to real discovery settings"), Defaults->LoadExternalConfiguration(Filename, Error));
    auto* Manager = NewObject<UTrajectoryDataManager>();
    TestTrue(TEXT("Discover through externally configured root"), Manager->ScanDatasets());
    TestEqual(TEXT("Synthetic dataset discovered"), Manager->GetNumDatasets(), 1);
    return true;
}
#endif
