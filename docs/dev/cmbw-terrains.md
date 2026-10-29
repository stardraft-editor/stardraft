# Cosmonarchy BW (CMBW)

`<P>` = install's `Prerelease` folder; `<maps>` = `CMBW-maps` folder. Probes: `-Dcmbw_probes=ON`, sources in
`tools/plugins/cmbw/probes/` (usage in each file's header).

## Data

Read from the install at run time (no repo change on a mod update):

| Data | Source |
|---|---|
| units, weapons, flingy, sprites, images, orders | `mpq\arr\*.datex` |
| names | `rez\unitnames.tbl`, `rez\stat_txt.tbl` |
| iscripts | `scripts\iscript_*.bin` |
| graphics, tilesets (cv5/vx4ex) | loose `mpq\` over 1.16.1 `patch_rt`/`BrooDat`/`StarDat` |
| brush sets | `Starcraft\Maps\CMBW-Root\CMBW-editor\brush\*.scmdbrh` |

Generated, committed in `src/mods/`:

| File | Generator | Input |
|---|---|---|
| `cmbw_units.inc` | `cmbw_units.py` (via `cmbw_update.py`) | cosmonarchy.com export, patch notes |
| `cmbw_crigs.inc` | `cmbw_crigs.py` (via `cmbw_update.py`) | Cosmonarchy-GPTP `triggers.cpp` |
| `cmbw_isom.inc`, `cmbw_isom_pieces.inc` | `cmbw_isom_learn <maps> <tileset dir> <out> <pieces out> <votes.txt>` | maps |
| `cmbw_auto_terrains.inc` | `cmbw_auto_rows <P> <votes.txt> <out>` | brush sets, votes |
| `cmbw_neighbors.inc` | `cmbw_neighbors <maps> --inc src/mods/cmbw_neighbors.inc <P>` | maps |

Hand-written: `cmbw_crig_docs.inc`; `kModTerrainSets` in `src/mods/cmbw.cpp` (one row per terrain, notes in its
comment); `kLeftOut` in `cmbw_set_status.cpp` (reason at the row's place in `cmbw.cpp`);
`tools/plugins/cmbw/tests/cmbw_pinned_ids.inc`.

## Engine

Per-tab `IsomTerrainTable::withExtras` (from `Tileset::modTerrains`, built in `Editor::reload`). Each mod terrain: 15
ISOM values past the game's, plus a template game terrain. Tiles = template's tiles, then `IsomGrid::putPieces` puts the
set's pieces; `IsomGrid::pickPiece` filters by `cmbw_neighbors.inc`. Map files keep the mod's ISOM values. Rects with
no set piece = null tile (group 0); keep them null, no approximate fills. Classic tabs: unchanged.

## After a mod update

1. `python -I tools/plugins/cmbw/cmbw_update.py`: install at newest commit?, regenerates units/crigs, prints changes
   and undocumented commands.
2. `cmbw_set_status <P> -1 <maps>`: sets marked `-` are new. Add a row (below) or add to `kLeftOut`.
3. Rerun `cmbw_neighbors` if rows changed.
4. `cmbw_test <P> <map> <new empty dir>` per tileset (~1 GB, delete after; not in CI); `cmbw_stroke_bench <P> <map> --all`.
5. Report: `python tools/plugins/cmbw/cmbw_report.py --install <P> --bin build/Release --out <site>/cosmonarchy
   --raw <new empty dir>`, then `cmbw_atlas.py <site>/cosmonarchy`; commit in the website repo.

## Adding a terrain row

1. `cmbw_brushes <P> <era>` lists sets (era: 0 Badlands, 1 Platform, 2 Installation, 3 Ash World, 4 Jungle, 5 Desert,
   6 Ice, 7 Twilight; dirt1/2/3 = Dirt/High Dirt/third level).
2. `cmbw_brushes <P> <era> "_<Set>_" x.png > b.txt; python tools/plugins/cmbw/cmbw_blocks.py b.txt`: groups and
   variant subtiles per block. Named sets: W/E north (2 rows), south, outer (3), inner (2). A grid precedes its name
   line. Two blocks in one group: `sub` (W 0-4, E 6-10). Last brushes may be the fill.
3. Template layout: `cmbw_rect_shapes <P> <era> <first> <last>`. Known:
   - Flat: Ash World Dirt/Lava 192-222, High 240-270; jungle layout 228-258 (High Dirt), 276-298 (Dirt); Badlands
     Dirt/Grass 188-218, High 236-266, Dirt/Asphalt 312-342.
   - Walls: Badlands Asphalt/Structure 360-398 (= cliffs 44-82 +316, pairs swapped).
   - Cliffs: Ash World 120-158 (W/E north 120/124, W/E south 128-132/134-138, W/E outer 140-144/146-150, W/E inner
     152,154/156,158); Badlands 44-90 (46/44, 50/48, 58-62, 52-56, 64-68, 70-74, 80/82, 76/78); jungle 36-74 (38/36,
     42/40, 44-48, 50-54, 56-60, 62-66, E inner 68/70, W inner 72/74); Platform High Platform 230-268; Ash World
     Dirt/Magma 48-86.
4. Row kind:
   - on game ground: `standIn` = template terrain; `fill` = set's fill group or game terrain.
   - foot of cliff: `standIn` = low terrain, `ground` = high.
   - third height: `standIn` High Dirt, `ground` High Dirt, `groundTemplate` Dirt; `fill` = set's main block or 0 +
     `fillTerrain`.
   - on a mod terrain: `groundMod` = its name.
   Never two terrains linked beside a third (triangles break the engine). Append after the tileset's last row (ids are
   ordinal); pin the line `cmbw_test` prints.
5. `cmbw_paint_one <P> <map> "<name>" 4 out.png` (maps per tileset: `cmbw_map_eras <maps>`). Common fixes: swap south
   blocks or inner corners (stepped wall); `variants` 1 (misaligned); pair two floor groups (check `cmbw_sheet`);
   inside-out edges: reverse block order or swap terrains (`cmbw_group_maps <maps> <era> <a-b>`,
   `cmbw_tile_at <P> <map> <x> <y>`); pattern fill: `fillSubtiles`. Check the maps' combos before guessing.
6. Missing thin/single/concave rects: `cmbw_group_match <P> <map> <mod a-b> <game a-b>` (score near 1 = recolored copy).
7. Rerun `cmbw_neighbors`; `cmbw_test`. It fails on moved ids, empty blocks, or a stale neighbor table.

## SCMDraft checks

`tools/scmdraft/`: `open_cm.ahk` (open map, CM profile), `paint_save.ahk` (window messages, no input taken),
`steps.ahk`/`menus.ahk`/`save.ahk` (real input; only when the user is away). Terrain Palette = command 32771. Compare:
`cmbw_map_terrains <install> <map>`, `cmbw_map_diff <install> <a> <b>`. Verified 2026-10-25: round trip and strokes
beside mod terrain keep mod tiles.
