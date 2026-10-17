# Plan: IsomTerrain as the isometric terrain core

State: in progress (2026-10-17), branch `isom-terrain`. See "Progress" at the end.

## The goal

StarEdit-exact isometric painting and 4-8 times the speed, with no lost features. IsomTerrain
(https://github.com/TheNitesWhoSay/IsomTerrain, MIT, by the author of Chkdraft) is the core, used without changes.
StarDraft's own terrain features stay on top of it.

## Why (measured 2026-10-17, same 296 maps, same reader)

| | Ours | IsomTerrain |
|---|---|---|
| Brush: StarEdit/ScmDraft reference tests (10 tests, 8 tilesets, sizes 1-3) | 0 of 10 perfect | 10 of 10 perfect |
| Brush: time per operation | 0.70 ms | 0.09 ms |
| Tiles from ISOM (regen), overall match | 0.9498 | 0.9499 |
| Regen, all 296 maps | 557 ms | 148 ms |
| New maps vs StarEdit's | 50 of 86 | 50 of 86 (same) |
| Tables | 6.8 MB learned data | built from CV5 in 2 ms + hand tables |

Most of our brush difference is one bug: for even sizes, `IsomGrid::paint` grows the brush one diamond up and right,
StarEdit grows it down and left. The rest: where painted areas meet, or near an existing edge (StarEdit leaves a
1-diamond neck), and at map borders.

## The setup

1. **Their code, unchanged:** `third_party/IsomTerrain/` at one pinned commit, with its LICENSE. No edits inside it,
   so a newer version is a drop-in. A bug in it: a fix in the adapter, or a small documented patch offered upstream.
2. **An adapter** (ours): converts our map's ISOM section, tiles and the tileset's CV5 to its API and back; connects
   its undo hook to our undo steps.
3. **Our features on top, all kept:**
   - Brush shapes (square, circle), 0.5 size steps and the separate height: the adapter turns them into the set of
     diamonds to place; the plain diamond brush is then exactly StarEdit's.
   - Flood fill and selection by rectangle: diamond sets for the core.
   - Rectangle mode: hand-placed tiles stay as they are (`tileFromIsom`).
   - Maps without an ISOM: `rebuildFromTiles` makes it first, then the core takes over.
   - Cliff-stack and misfit repair: a step after the core, only where no tile fits. On by default; a setting turns it
     off for exact StarEdit results.
   - Tile variants: our learned variant choice picks the look inside the tile group that the core chooses.
   - Doodad checks (a TODO in IsomTerrain), resize with a tileset change, undo: ours.
4. **The learned tables:** replaced by the CV5-derived tables, except the guesses for combinations the rules lack.

## Tests (the old engine goes only when all pass)

- IsomTerrain's 10 StarEdit reference brush tests: perfect.
- The 296-map regen (`isom_test`, the linux-data job): at least 0.9498 overall and per tileset no worse.
- The random brush test (204 Blizzard maps x 40 strokes): no new invalid diamonds; misfits no more than today with
  repair on.
- All `tools/sdl_snapshots.py` cases: brush tools, Rectangle mode, fill, undo, Generate terrain, resize.
- Both engines side by side during the switch.

## License and notices

- MIT (Copyright (c) 2023 Justin F). Its copyright and permission notice go in the repo, NOTICE.md and the About box.
- Its bundled libraries (CascLib, StormLib, ICU, RareCpp) are not needed.
- Origin: the README credits SI's ScmDraft ISOM code, "originally decompiled from staredit". The license is clean; the
  owner decides whether that origin is acceptable.

## The copy to use

IsomTerrain is a one-day 2023 snapshot (7 commits, no work since). The maintained version is in Chkdraft
(`src/mapping_core`, MIT, active), inside a larger library. Start from IsomTerrain as it is (it compiles unchanged);
compare with Chkdraft's newer code for fixes worth taking.

## Effort and risks

- About 1-2 days of agent work.
- Risks: square and circle brushes as diamond sets must look as they do now; the undo steps; repair must not undo
  what the core placed on purpose.

## Progress

- Done: `third_party/IsomTerrain/` (commit 4839e25, `IsomApi.h` unchanged, two stand-in headers,
  `STARDRAFT.md`); the adapter `stardraft/isom_core.h`; `isomterrain_test` runs the 10 reference tests through
  StarDraft's map reader: 10 of 10 perfect, about 0.014 ms a stroke (the old engine: 1 of 10, after the even-size
  fix).
- Done: the even-size offset (`IsomGrid::paint`, `starEditBrushDiamonds`, `Brush::starEdit`): both engines and the
  brush preview cover StarEdit's diamonds.
- Done: StarEdit's brush (a diamond of whole diamonds) paints through the core in the editor (`Editor::applyCoreBrush`);
  `STARDRAFT_TERRAIN_CORE=0` keeps the old engine, to compare. editor_test and the snapshots: the same results with
  both engines.
- Done: square and circle brushes, other heights, fill and replace paint through the core too, whenever the corners
  they set are whole diamonds of one terrain (`IsomCore::paintDiamonds`: StarEdit's own loop for any set of diamonds;
  `isomterrain_test` checks it gives the same 10 maps). A corner alone (the half-size brush) and paste keep the old
  engine.
- Done: the repair step. Where the core leaves a rect that no tile group fits (StarEdit's rule writes the null tile; it
  happens on maps whose ISOM StarEdit would not make, e.g. (4)Zoo Keeper), the old engine makes that edit instead
  (`IsomCore::untileable`, `Editor::applyCore`; `STARDRAFT_TERRAIN_REPAIR=0` keeps the core's result).
- Measured (paint_stress, 189 Blizzard maps outside `ladder`, 40 strokes each, through the Editor as the UI paints):

  | | Old engine | Core + repair |
  |---|---|---|
  | Time per brush call | 0.76 ms | 0.25 ms |
  | Misfit tiles, total (most on one map) | 80 (4) | 11 (1) |
  | Invalid diamonds | 0 | 0 |

  Without the repair the core had 48 misfits, 22 of them null tiles on (4)Zoo Keeper.
- Done: StarEdit's brush moves in half-diamond steps, as StarDraft's did (`starEditBrushAt`): an odd size is centered
  on the nearest diamond, an even size on the nearest corner (both StarEdit's blocks, through the core); an odd size
  nearer a corner (between diamonds) paints the brush's own footprint there (size 1: that corner, half a diamond),
  with the old engine. paint_stress: 0.27 ms a call, 16 misfits (old engine 80), no invalid diamonds.
- The old engine stays for now: the repair, the half-size brush, paste and Rectangle mode use it, so its learned tables
  stay too (point 4 of the setup waits for that).
- Next: tiles from ISOM (regen: new maps, maps without an ISOM, resize) through the core; the tile variant choice on
  top (the core picks a random common or rare subtile, as StarEdit); the repair as a setting in Options; paste as ISOM
  copies (the core's `copyIsomFrom`).
