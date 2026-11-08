# Mining Lab and Rush distance

Tools > Mining Lab and Measure rush distance use the independent MIT
library in `third_party/map-simulation`, through `tools/map_lab`. They do not use
the experimental simulator in `tools/sim`. Combat and Run the map keep their
existing simulator.

## Use

Mining Lab is a compact floating tool window; Tests and Boosts switch its view. It includes
start locations and placed depots and can run independent tests at several bases.
The Rush widget is a compact floating tool with two-point picking, per-run timing,
launch interval and retained-result controls. The map and its undo history are unchanged. The tool uses the map's
current CHK, including unsaved edits, and data from the user's game installation.

The results window has speed, pause, stop and copy controls. Mining has position,
cargo and cycle searches, each with a 1-600 second budget (default 15), progress,
verification and continuation. Ghosts, cargo-return markers, numbered cycle arrows
and field-cycle labels follow the source editor's display contract. Rush sends
successive workers in separate worlds: they do not block one another. Its table
shows actual traveled pixels, elapsed game seconds, and whether the worker reached
the endpoint. A failed or blocked route is not reported as a successful arrival.
The overlay shows workers and route trails on the map. Hiding either widget keeps
its sessions running. Editing the map, changing tabs or starting the old simulator
stops the measurement.

The mining table reports income from the start of the run, including travel to
the minerals. It reports the last completed delivery-to-delivery cycle per
mineral field. Workers are assigned to fields in the same order as the source
editor; fields within 12 tiles participate. The diagnostic uses 50,000 mineral
units per assigned field to avoid depletion. It measures mineral mining only,
with a completed depot and no worker production. It does not measure the
existing experimental simulator's warm-up, gas or multi-base scenario.

At Fastest, one game frame is 42 ms. Playback speed changes only how quickly the
results appear. Both sessions run until stopped; each Rush runner still has its
own failure/stall limit. Search controls can
restart mining from a saved search state, so its clock can move back. Continue
extends the existing frontier, and changing an earlier plan invalidates its dependent
later plans as in the source adapter. Copy keeps
table columns separated by tabs.

## Boundaries

The engine supports the standard data formats. Cosmonarchy and other games with
a custom unit loader are refused. StarDraft's map/project DAT overrides go through
its existing `simDataFiles` reader. This does not add EUD execution, triggers,
technology research or full mod compatibility.

The imported world follows the source editor's melee diagnostic setup: resources,
neutral units, critters and neutral sprite-units are imported; player armies and
buildings are not part of the measurement. Start points need space for the race's
depot and workers. Rush points must be open. Decorative sprites remain visible
through StarDraft's normal renderer; simulated worker images are an overlay.

The source engine's recorded regression scope was 276 mining configurations and
234 of 240 Rush configurations. Six Circuit Breakers random-route cases still
differed from the behavioral reference. Do not describe this as exact pathfinding
for every map, complete gameplay, or replay support. The contribution's validation
also compares the extracted adapters with the source editor; that is a separate
check from agreement with the game.

## Implementation

`native_world.cpp` imports read-only CHK data. `native_mining.cpp` and
`native_rush.cpp` implement the diagnostic contracts. `session.cpp` owns those
objects on a worker thread. The UI receives value snapshots under a mutex and
sends search/speed commands; it never touches the worker's world. Stop joins the
worker and cancels its owned searches. A dedicated GameData object keeps input
storage alive independently of editor tabs. No game process is launched.

Navigation geometry is warmed before search snapshots. Immutable navigation
spaces are reused through a bounded, synchronized cache keyed by complete body
geometry and departure state. Clearance uses a spatial index; sweep boundary
updates preserve stable tie order. These optimizations retain the search
candidates, simulation rules and full-lineup validation horizons. Engine-local
changes include exact route/visibility memoization, short image-only animation
blocks and smaller stable spatial updates. The cargo/cycle executor admits up to
128 independent candidates per batch, consumes results in the original order,
and uses at most 15 workers (one fewer than the logical CPU count). Its pending
result wait is bounded to one millisecond; pause/cancellation checks remain at
128-frame chunk boundaries. Cache hits copy value results, leaving mutable
worlds and controller state private. These changes and their fingerprints are recorded in `third_party/map-simulation/SOURCE.json`.

Synthetic engine tests and the session lifecycle test can be built without
StarDraft's renderer or third-party downloads:

```
cmake -S tools/map_lab -B build/lab
cmake --build build/lab --config Release
ctest --test-dir build/lab -C Release --output-on-failure
```

The normal StarDraft build also includes those tests. Run `python -I
tools/ui_text_lint.py` after UI changes. For real-map validation, retain local
input hashes, race/spawn/count settings, frame horizons and comparison output;
do not commit maps, game assets or traces.


## Measurement UI and playback

The floating widgets share an accessible +/- stepper. Mining Lab's 0 worker
setting still means one per nearby mineral patch; the first step resolves that
count before changing it, and clicking the value restores automatic assignment.
Its base preview uses the current terrain and the map's actual player colors.

Mining guide labels set TextDraw::smooth. The SDL map frame separates those
labels from bitmap map text and paints them through the UI canvas at screen size,
using measured rounded backplates and the same camera as the rendered map frame.
Other scene consumers retain their bitmap-text fallback. Guide coordinates and
simulation data are unchanged.

Session advances and publishes individual simulation frames, at steady deadlines
42/speed milliseconds apart, rather than publishing speed-frame bursts every
42 ms. Pause still polls searches without advancing game time. The game script
waits and animation frames are unchanged; a private Hatchery body/shadow trace
matched the optional OpenBW reference at 96 frame endpoints. The snapshot helper
includes map-mining-controls for the steppers and automatic worker-count reset.
