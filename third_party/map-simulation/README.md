# Map Simulation

An independent C++20 simulation library, contributed under the MIT license in
LICENSE. It has no Qt, SDL, BWAPI or OpenBW build or runtime dependency. It reads
game data supplied by its caller; no game data is included here.

This is a source extraction from Map Editor revision
`cdc6bd99ab80f836c1d3fded991b013ec66d0fae`. SOURCE.json fingerprints the extracted
engine files. The only build change in the extraction is a standalone CMake
project, optional tests, and removal of a test that used a script outside this
library. The implementation files are unchanged.

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
