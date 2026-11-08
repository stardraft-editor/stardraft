# Engine region overlay

The SDL editor's existing Regions command uses the region constructor shipped
with Mining Lab/Rush. This contribution depends on that engine integration.
No OpenBW or BWAPI code is linked or needed. No simulation world is created.

`region_overlay_data.h` consumes an owned snapshot of MTXM and the active
tileset's parsed CV5/VF4 fields. It builds native `Terrain`/`Regions`, retains
walkable region IDs at 8-pixel resolution, and compresses fills and boundaries
into runs. Absent tile groups follow the engine scenario loader's group-zero
fallback. Raw game assets are neither generated nor redistributed.

`sdl_region_overlay.inc` runs this work on one background task. The cache key
includes editor identity, monotonic terrain revision, dimensions and era. A
result for a superseded key is discarded; no stale raster is drawn. Inputs own
all their data, so switching or closing documents cannot leave editor references
on the worker. Errors are displayed in the options window and retried after a
terrain change. Hidden overlays do not start jobs.

The portable core exposes optional scene/hover callbacks. Other hosts retain
their existing implementation. Connected areas and Check pathing keep their
existing analysis and problem markers. The SDL hook changes the region display
and hover data only. Region display state and opacity are ordinary local editor
preferences, with no CHK metadata or undo actions.

Build `region_overlay_test`, `stardraft_editor` and `editor_core_check`.
The geometry test reconstructs every raster cell and boundary segment and
exercises partial/blocked/elevated terrain, rectangular and maximum maps,
terrain edits/restoration, malformed input and absent groups.
`--check region-overlay` checks the live cache, stale-result rejection, zoom,
bottom/right clipping, zero opacity, slider keys, toggling and unchanged map
bytes/revision. Snapshot scripts can use `regions` for the cache/raster summary.
The check also covers worker errors without retry loops and frame requests
while a job is pending. It compares raster IDs and ordered graph metadata with
a separate native construction from the original CV5/VF4 byte streams, to check
the parsed-tileset adapter without calling that adapter again.
`tools/tests/region_overlay_settings.py` checks loading
saved display preferences and clamping invalid opacity values in fresh processes;
snapshots keep those files read-only, following the editor's test policy.
Run the region workflow cases in `tools/sdl_snapshots.py`, plus the existing
pathing-area/check/fix cases and Mining Lab/Rush cases for integration.
