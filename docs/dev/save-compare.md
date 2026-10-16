# The save comparison (2026-10-16)

This check compares the maps that the SDL program (port/kit at b10fefb) saves with the maps that the last Win32
release, v0.5.0.8 (tag at b47a3b6), saves. The two must save a map the same way, or each difference must be an intended
change. Result: **the two save the same bytes for all 333 maps, unchanged and after a fixed set of edits.** No CHK
section and no other file of the archive is different. No difference needs an explanation, and no bug was found.

## How the maps were saved

- **The new program**: `stardraft.exe --snapshot <png> --install <StarCraft> --saveas <out> <map>` (a snapshot run, with
  `APPDATA` and `LOCALAPPDATA` in a temp folder). `--saveas` is File > Save as (`saveMapFile` in
  `apps/editor/core/map_commands.inc`, which calls `Editor::save`). Its round-trip line said "the saved map is the same
  (yes)" for all 333 maps.
- **The old program**: the Win32 program of v0.5.0.8 has no test option that saves a map to a given file (its
  `--datashots` specs and `--selftest` save only to their own files), and it cannot run without a window. Its File >
  Save is the same `saveMapFile` (`git show a5ea9a6^:apps/editor/core/map_commands.inc`), which calls `Editor::save` of
  the map library. Thus the old saves come from the map library at the tag v0.5.0.8 (`src/`, `include/`, built from
  `git archive v0.5.0.8`), with a small command that opens each map with `Editor`, saves it, and opens the copy again.
- **The library at port/kit**: the same small command, built against port/kit's map library, for a third set.
- **The edits**: the same command with a fixed set of edits through `Editor`, at both library versions:
  1. Terrain: a brush stroke of the second terrain across the middle of the map (`beginTerrainStroke`, `paintBrush`,
     `endTerrainStroke`; 326 maps, the 7 others have no terrain that StarDraft can paint).
  2. A unit: a Marine of player 1 at the middle (`placeUnit`).
  3. A location: the first used location (not Anywhere) moves 32 px (`setLocationRect`). A map with no used location
     first gets a new one (`createLocation`).
  4. A trigger: the first trigger is turned off or on (`setClassicTrigger`); a map without triggers gets a new trigger.

`git diff v0.5.0.8 b10fefb -- src include` changes only the renderer (`renderer.h`, `scene.h`, `renderer.cpp`), a
key name in `commands.cpp`, the Linux data folder in `euddraft.cpp` and two undo counters in `editor.h`. Nothing that
reads or writes a map changed, so the same bytes are what the code tells.

## The maps

333 maps: every `.scm` and `.scx` of the StarCraft install (its `Maps` folder with Blizzard's melee maps, Brood War,
the campaigns, the ladder maps of 2017 to 2019 and the user's own maps) and `bneck.scm`.

| Tileset | Maps | | Size | Maps |
|---|---:|---|---|---:|
| Jungle | 108 | | 128 x 128 | 168 |
| Space platform | 59 | | 192 x 192 | 36 |
| Badlands | 51 | | 96 x 96 | 26 |
| Twilight | 42 | | 128 x 96, 96 x 128 | 33 |
| Desert | 28 | | 256 x 256 | 11 |
| Ash world | 24 | | a side of 64 (64 x 64, 64 x 96, 128 x 64, 64 x 192) | 13 |
| Ice | 18 | | other sizes (112 x 128, 256 x 128, 192 x 256 ...) | 46 |
| Installation | 3 | | | |

332 maps have triggers, 69 have mission briefings. About 30 are protected ladder maps (no ISOM, sections with junk
names, a truncated last section, a section with a negative size).

## The result, section by section

"Maps" is the number of saved maps that have the section (at least: in 7 protected maps a junk section name with a zero byte cut the count). Old and new are the same in each section for each map, both
unchanged and after the edits (`chkcmp`: the archive's files, the section order, each section's effective data, and
the whole scenario.chk byte by byte).

| Section | Maps | Unchanged save: old vs new | After the edits: old vs new | Changed by the edits |
|---|---:|---|---|---|
| VER, VCOD, IVER, IVE2, TYPE | 318, 316, 172, 201, 137 | same | same | no |
| OWNR, IOWN, SIDE, FORC, COLR, CRGB | 316, 280, 326, 316, 155, 2 | same | same | no |
| ERA, DIM | 326 | same | same | no |
| MTXM, TILE, ISOM, DD2 | 318, 316, 316, 314 | same | same | yes (terrain) |
| MASK | 318 | same | same | no |
| UNIT | 326 | same | same | yes (the unit; terrain removes units it rules out) |
| THG2 | 316 | same | same | yes (sprites of doodads under the stroke) |
| MRGN | 316 | same | same | yes (the location) |
| TRIG | 316 | same | same | yes (the trigger) |
| MBRF | 312 | same | same | no |
| STR, STRx | 291, 27 | same | same | yes (the new location's name) |
| SPRP, WAV, SWNM | 316, 298, 203 | same | same | no |
| PUNI, UPGR, UPGS, PTEC, TECS, UNIS, UPUS | 198 to 316 | same | same | no |
| PUPx, PTEx, UNIx, UPGx, TECx | 219 to 231 | same | same | no |
| Protector sections (CMP, TlLE, SlDE, PUNl, FORc, junk names) | 7 to 87 | same | same | no |
| Other files of the archive (sounds) | | same | same | no |
| The whole scenario.chk, byte by byte | 333 | same | same | |

## What a save changes in a map (old and new the same)

These are not differences between the two programs: both make them. They are the map library's behavior since 0.5.

| What changes against the original file | Maps | Why |
|---|---:|---|
| ISOM is added at the end | 29 | The map has no ISOM (protected ladder maps). StarDraft makes it from the tiles at open (`isom_.rebuildFromTiles` in `Editor::open`), so isometric painting works. The game does not read ISOM. |
| The size of a truncated last section ("CMP ", size "Debu") is written as the bytes that it has | 7 | `Chk::parse` keeps a truncated section's bytes; `Chk::serialize` writes its real size. The game reads the same bytes. |
| A section with a negative size at the end (asd.scx's ISOM) is not written | 1 | `Chk::parse` follows the jump as StarCraft does; the sections StarCraft reads are written in their order. |
| Each other section | 333 | The same bytes as the original. |

## Open the saved maps again

- **The new program**: each saved map opened again in the program's second editor (`mapSaveRoundTrip`: size, tileset,
  terrain, units, doodads, sprites, locations and strings the same, "(yes)" for 333 of 333), and each edited save
  opened in a new snapshot run ("Opened ..." for 333 of 333).
- **StarCraft's rules** (`Editor::playabilityProblems`, the core's `playabilityText`): no problem in any unchanged
  save (333 of 333). After the edits, 6 maps have "Player n owns units but has no start location": the campaign maps
  Premonition 3 to 5 and `out.scx` have no start location for player 1, which gets the new Marine; in (2)Pro Bowl and
  (4)Steal the Bacon the terrain stroke removed start locations that the new terrain rules out (`pruneUnits`, as
  Options > Allow illegal units off does). The old library gives the same 6 lines. These come from the edits, not from
  the save.

## Findings

None. Each section of each map is the same in the old and the new save, so there is nothing to explain and nothing to fix.
