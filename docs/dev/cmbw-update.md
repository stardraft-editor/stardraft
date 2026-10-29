# Cosmonarchy BW (CMBW)

`<P>` = install's `Prerelease`; `<maps>` = `CMBW-maps`. Probes: `-Dcmbw_probes=ON`, `tools/plugins/cmbw/probes/`
(usage in each header).

## Generated tables (`src/mods/`)

| File | Command |
|---|---|
| `cmbw_units.inc`, `cmbw_crigs.inc` | `python -I tools/plugins/cmbw/cmbw_update.py` |
| `cmbw_isom.inc`, `cmbw_isom_pieces.inc` | `cmbw_isom_learn <maps> <tileset dir> <out> <pieces out> <votes.txt>` |
| `cmbw_auto_terrains.inc` | `cmbw_auto_rows <P> <votes.txt> <out>` |
| `cmbw_neighbors.inc` | `cmbw_neighbors <maps> --inc src/mods/cmbw_neighbors.inc <P>` |

Hand-written: `kModTerrainSets` (`src/mods/cmbw.cpp`), `kLeftOut` (`cmbw_set_status.cpp`), `cmbw_crig_docs.inc`,
`tools/plugins/cmbw/tests/cmbw_pinned_ids.inc`.

## Engine

Each mod terrain: 15 ISOM values past the game's and a template game terrain (`IsomTerrainTable::withExtras`). Tiles =
template's, then `IsomGrid::putPieces`; `pickPiece` filters by `cmbw_neighbors.inc`. Rects with no piece stay the null
tile: no approximate fills.

## Mod update

1. `cmbw_update.py`
2. `cmbw_set_status <P> -1 <maps>`: `-` = new set → add a row or `kLeftOut`.
3. Rows changed → `cmbw_neighbors`.
4. `cmbw_test <P> <map> <new dir>` per tileset (~1 GB); `cmbw_stroke_bench <P> <map> --all`.
5. `cmbw_report.py --install <P> --bin build/Release --out <site>/cosmonarchy --raw <new dir>`, `cmbw_atlas.py
   <site>/cosmonarchy`, commit in the website repo.

## New row

1. `cmbw_brushes <P> <era> ["_<Set>_" x.png > b.txt]`, `cmbw_blocks.py b.txt`. Eras: 0 Badlands, 1 Platform,
   2 Installation, 3 Ash World, 4 Jungle, 5 Desert, 6 Ice, 7 Twilight. Blocks: W/E north, south, outer, inner. `sub`:
   two blocks in one group (0-4, 6-10).
2. Template: `cmbw_rect_shapes <P> <era> <a> <b>`. Known groups:
   - Flat: Ash Dirt/Lava 192-222, High 240-270; Jungle 228-258 (High Dirt), 276-298 (Dirt); Badlands Dirt/Grass
     188-218, High 236-266, Dirt/Asphalt 312-342.
   - Walls: Badlands Asphalt/Structure 360-398 (cliffs +316, pairs swapped).
   - Cliffs: Ash 120-158; Badlands 44-90; Jungle 36-74; Platform High 230-268; Ash Dirt/Magma 48-86.
3. Kind: game ground → `standIn` = template, `fill`; cliff foot → `ground` = high; third height → `standIn`/`ground`
   High Dirt, `groundTemplate` Dirt; on mod terrain → `groundMod`. No two terrains linked beside a third. Append after
   the tileset's last row (ids are ordinal).
4. `cmbw_paint_one <P> <map> "<name>" 4 out.png`. Fixes: swap south/inner blocks; `variants` 1; inside-out → reverse
   blocks (`cmbw_group_maps`, `cmbw_tile_at`); `fillSubtiles`. Read the maps first.
5. Missing rects: `cmbw_group_match <P> <map> <mod a-b> <game a-b>` (≈1 = copy).
6. `cmbw_neighbors`, `cmbw_test`; pin the printed id.
