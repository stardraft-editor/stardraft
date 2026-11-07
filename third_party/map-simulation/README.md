# Map Simulation

An independent C++20 simulation library, contributed under the MIT license in
LICENSE. It has no Qt, SDL, BWAPI or OpenBW build or runtime dependency. It reads
game data supplied by its caller; no game data is included here.

This is a source extraction from Map Editor revision
`cdc6bd99ab80f836c1d3fded991b013ec66d0fae`. SOURCE.json fingerprints the extracted
engine files. The build change in the initial extraction is a standalone CMake
project, optional tests, and removal of a test that used a script outside this
library. Subsequent performance changes are recorded separately in
SOURCE.json's `localChanges`, retaining the initial extraction fingerprints:
bounded immutable navigation-space reuse, indexed obstacle queries, stable
incremental spatial ordering and allocation-free overlap checks. Navigation
geometry is warmed before parallel mining searches. These changes retain the
simulation rules, search candidates and validation horizons.

Cargo/cycle search performance also uses bounded memoization of complete pure
route queries, conservative walkable-tile checks, immutable visibility footprints,
and decoded image-only animation blocks. Other animation blocks use the full VM;
waits, script cursors, image order and random draws remain observable state.
Creep changes invalidate navigation geometry when they change blocked centers.
Spatial queries retain the existing axis, distance and stable sweep tie ordering.
Search tasks are consumed in their original order, with batches of up to 128
independent candidates. The executor uses at most 15 workers and reserves one
logical CPU when available. It waits briefly for pending results rather than
busy-polling. Cancellation and pause still run at each 128-frame chunk.

Fingerprints use LF-normalized text. Run `python -I
third_party/map-simulation/verify_source.py` to check them, including after
`git am` on Windows (which can materialize new files with CRLF before applying
the patch's attributes). The source manifest does not fingerprint this README,
the verifier, or itself.

StarDraft's integration is in `tools/map_lab/`. Its FileReader callback replaces
the original editor's CASC wrapper. Its map import uses this library's Scenario
reader in place of the original editor's editable document. The mining and Rush
algorithms are the same ones used by the original editor.

The original development used controlled behavioral comparisons with OpenBW as
an optional reference, including targeted source-informed behavior discovery.
Replacement routines were written independently; this extraction includes no
OpenBW source, BWAPI source, comparison executable, game assets or captured
reference traces. The library is not a complete Brood War engine. Read
COMPATIBILITY.md before extending its use beyond these measurements.

To build the library and its synthetic tests alone:

```
cmake -S third_party/map-simulation -B build/engine
cmake --build build/engine --config Release
ctest --test-dir build/engine -C Release --output-on-failure
```

Tests use synthetic data and need no game installation. Real-map comparisons
need locally supplied maps and game data; those inputs must not be added to the
repository.
