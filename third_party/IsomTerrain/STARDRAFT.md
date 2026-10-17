# IsomTerrain in StarDraft

IsomTerrain (https://github.com/TheNitesWhoSay/IsomTerrain, MIT, Copyright (c) 2023 Justin F) at commit
`4839e251b4ecb45921a42a7c6346b03b7fb82773`: StarEdit's isometric terrain editing in one header. StarDraft uses it as
its terrain core (`include/stardraft/isom_core.h`, `src/isom_core.cpp`).

What is here:

- `IsomTerrain/IsomApi.h`, `LICENSE`, `README.md`: IsomTerrain's own, unchanged. A newer `IsomApi.h` is a drop-in.
- `Map Testing Pack/`: IsomTerrain's test maps that its edit and resize tests use (the source maps and the maps
  ScmDraft made from them), unchanged. `tools/tests/isomterrain_test.cpp` runs the edit tests
  (`tools/tests/isomterrain_cases.inc`, generated from IsomTerrain's `IsomTests.cpp`).
- `CrossCutLib/Logger.h`, `MappingCoreLib/MappingCore.h`: **StarDraft's** stand-ins for the two Chkdraft headers that
  `IsomApi.h` includes (a logger that writes nothing; `Span`, the integer names, `Sc::Terrain` and declarations for
  the archive loading that StarDraft does not call). They sit where `IsomApi.h`'s `#include "../..."` looks.

Not here: Chkdraft's MappingCoreLib, CrossCutLib, CascLib, StormLib, ICU and RareCpp (IsomTerrain bundles them; the
core does not need them).

IsomTerrain's README credits SI's ScmDraft ISOM code ("originally decompiled from staredit") among its sources.
