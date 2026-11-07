# Mining Lab and Rush distance

Tools > Mining Lab and Measure rush distance use the independent MIT
library in `third_party/map-simulation`, through `tools/map_lab`. They do not use
the experimental simulator in `tools/sim`. Combat and Run the map keep their
existing simulator.

## Use

Open a standard Brood War map and open one of the two tools. Select the race and
a start location, or select the start directly on the map. Rush also needs an end
point. Mining accepts a worker count or one worker per nearby mineral field.
Select Start. The map and its undo history are unchanged. The tool uses the map's
current CHK, including unsaved edits, and data from the user's game installation.

The results window has speed, pause, stop and copy controls. Mining has position,
cargo and cycle searches, with a one-second search budget per click. Rush sends
successive workers in separate worlds: they do not block one another. Its table
shows actual traveled pixels, elapsed game seconds, and whether the worker reached
the endpoint. A failed or blocked route is not reported as a successful arrival.
The overlay shows workers and route trails on the map. Editing the map, changing
tabs, closing the tool or starting the old simulator stops the measurement.

The mining table reports income from the start of the run, including travel to
the minerals. It reports the last completed delivery-to-delivery cycle per
mineral field. Workers are assigned to fields in the same order as the source
editor; fields within 12 tiles participate. The diagnostic uses 50,000 mineral
units per assigned field to avoid depletion. It measures mineral mining only,
with a completed depot and no worker production. It does not measure the
existing experimental simulator's warm-up, gas or multi-base scenario.

At Fastest, one game frame is 42 ms. Playback speed changes only how quickly the
results appear. Mining stops at 10,000 frames; Rush at 24,000. Search controls can
restart mining from a saved search state, so its clock can move back. Copy keeps
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
