# Loaded trajectory index and source ID

## Overview dataset identity

`ADatasetVisualizationActor::GetVisualizedDatasetInfo` returns the dataset identity
snapshot taken when its last synchronous/asynchronous bind succeeded. It refers
to `UTrajectoryDataLoader::GetLoadedDatasets()[DatasetIndex].DatasetInfo`, not to
the discovery manager's dataset array. The two indices are unrelated. The snapshot
survives discovery rescans, source dataset cache unloading and CPU position-buffer
release. The getter returns false with a reset output before a successful bind
and after EndPlay. Failed binds do not replace the last successful snapshot.
Use this Blueprint getter for saving presets rather than reconstructing folder IDs.

`FLoadedTrajectory` exposes two Blueprint-readable fields:

| Field | Meaning |
| --- | --- |
| `TrajectoryIndex` (`int32`) | Zero-based position in this loaded dataset's `Trajectories` array |
| `TrajectoryId` (`int64`) | Original source dataset identifier |

For example, loading IDs `[518, 7, 3000000001]` yields indices `[0, 1, 2]`.
Loading the explicit subset `[3000000001, 518]` yields indices `[0, 1]`, while the
IDs remain unchanged. Repeated requested IDs are included once. The loader
preserves selection order rather than depending on map iteration. Both sync
and async loading use this assembly path. A default/unassigned trajectory has
index `INDEX_NONE`.

Indices are local to a loaded array and can change after reloading or changing
the subset. If application code reorders a copied array, it must update its
indices too. Source identity across datasets is `(dataset, TrajectoryId)`.

Use an index for array access. Use the resolved ID for selection lookup, query
exclusion, and display. `TryGetTrajectoryIdByIndex` validates an index without
copying position buffers, and resets its output ID to `-1` on failure.
`ResolveTrajectorySliderIndex` rounds/clamps a slider value and returns
`INDEX_NONE` when no trajectory can be selected. `GetTrajectorySliderMaximum`
returns `max(0, count - 1)`; `HasLoadedTrajectories` controls the empty state.

`GetTrajectorySelectionLabel` displays a human-facing ordinal and the original
integer ID, for example `Trajectory 2 of 3 · ID 7`. IDs never pass through the
slider's floating-point value. Loaded samples and binary file layouts are
unchanged by the new index field.

The overview's existing GPU buffers still use the 32-bit binary ID representation.
Their packed particle indices are separate from loaded-array indices, especially
when visualization sampling selects a subset. No GPU buffer layout is changed.

Run `TrajectoryData.Identity` in Unreal automation for loaded subset order,
index assignment, duplicate IDs, 64-bit identity, selection lookup, slider
bounds, and empty-state regression tests.
