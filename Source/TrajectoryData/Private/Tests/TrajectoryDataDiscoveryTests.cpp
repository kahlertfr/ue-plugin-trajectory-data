#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "TrajectoryDataManager.h"
#include "TrajectoryDataSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrajectoryDomainDiscoveryTest,
    "TrajectoryData.Discovery.Domains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrajectoryDomainDiscoveryTest::RunTest(const FString& Parameters)
{
    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FGuid::NewGuid().ToString());
    const TArray<FString> Ids = {
        TEXT("flotation/my_simulation/particles"),
        TEXT("flotation/my_simulation/bubbles"),
        TEXT("flotation/other_simulation/particles"),
        TEXT("other_domain/my_simulation/particles")
    };
    for (const FString& Id : Ids)
    {
        const FString Directory = FPaths::Combine(Root, Id);
        TestTrue(TEXT("Create fixture directory"), IFileManager::Get().MakeDirectory(*Directory, true));
        TestTrue(TEXT("Write discovery manifest"), FFileHelper::SaveStringToFile(TEXT("{}"),
            *FPaths::Combine(Directory, TEXT("dataset-manifest.json"))));
    }
    // A dataset without a manifest must not be discovered.
    IFileManager::Get().MakeDirectory(*FPaths::Combine(Root, TEXT("flotation/my_simulation/empty")), true);

    UTrajectoryDataSettings* Settings = UTrajectoryDataSettings::Get();
    const FString PreviousRoot = Settings->ScenariosDirectory;
    Settings->ScenariosDirectory = Root;
    // Use an isolated manager so the test does not replace the application's cached datasets.
    UTrajectoryDataManager* Manager = NewObject<UTrajectoryDataManager>();
    TestTrue(TEXT("Scan shared root"), Manager->ScanDatasets());
    TestEqual(TEXT("All domains, scenarios and datasets discovered"), Manager->GetNumDatasets(), Ids.Num());
    for (const FString& Id : Ids)
    {
        FTrajectoryDatasetInfo Info;
        if (TestTrue(TEXT("Lookup qualified ID"), Manager->GetDatasetInfo(Id, Info)))
        {
            TArray<FString> Parts;
            Id.ParseIntoArray(Parts, TEXT("/"));
            TestEqual(TEXT("Domain name"), Info.DomainName, Parts[0]);
            TestEqual(TEXT("Scenario name"), Info.ScenarioName, Parts[1]);
            TestEqual(TEXT("Dataset name"), Info.DatasetName, Parts[2]);
            TestEqual(TEXT("Dataset path points to leaf"), Info.DatasetPath, FPaths::Combine(Root, Id));
            TestEqual(TEXT("Manifest directory points to leaf"), Info.Metadata.DatasetDirectory, Info.DatasetPath);
        }
    }
    FTrajectoryDatasetInfo Unqualified;
    TestFalse(TEXT("Unqualified legacy ID cannot select an arbitrary domain"),
        Manager->GetDatasetInfo(TEXT("particles-my_simulation"), Unqualified));
    Settings->ScenariosDirectory = PreviousRoot;
    IFileManager::Get().DeleteDirectory(*Root, false, true);
    return true;
}
#endif
