# Dataset naming and organization

External trajectory data uses this hierarchy:

```text
dataset-dir/
  domain-name/
    scenario-name/
      dataset-name/
        dataset-manifest.json
        dataset-meta.bin
        dataset-trajmeta.bin
        shard-<N>.bin
```

A domain groups a source or application domain. A scenario identifies a simulation
run or experiment. Datasets contain related trajectories, such as particles and
bubbles within the same scenario. Spatial hashing files remain inside each
dataset folder when present.

## Configuration and discovery

```ini
[/Script/TrajectoryData.TrajectoryDataSettings]
ScenariosDirectory=C:/Data/output
bAutoScanOnStartup=True
bDebugLogging=False
```

The legacy `ScenariosDirectory` key now points to the shared root containing
domains. `GetScenariosDirectory` and `SetScenariosDirectory` retain their C++ and
Blueprint function names. Discovery traverses domain, scenario, and dataset
folders and accepts datasets with readable `dataset-manifest.json` files.

## Identity and lookup

`FTrajectoryDatasetInfo` exposes `DomainName`, `ScenarioName`, `DatasetName`,
`DatasetPath`, and `UniqueDSName`. Names come from folders; `DatasetPath` is the
innermost dataset directory. Manifest metadata retains its existing semantics.

The unique ID is `domain/scenario/dataset`, for example
`flotation/my_simulation/particles`. Use the complete ID with `GetDatasetInfo`.
Lookup is case-insensitive, so names that differ only in case should be avoided.
Folder names cannot contain the `/` separator. Do not persist discovery indices,
which may change after rescanning.

## Migration

Add a domain folder above the existing scenario/dataset folders and configure the
shared parent as the root. Replace old `dataset-scenario` lookup IDs and saved
references with `domain/scenario/dataset`. Direct-path API callers should update
paths if files move. Metadata, shard formats, and spatial hashing layouts do not
change.
