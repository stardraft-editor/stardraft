# Plan: IsomTerrain as the isometric terrain core

State: done for what StarEdit has (2026-10-17), branch `isom-terrain`. See "Progress" at the end.

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
| Tables | 6.8 MB learned data (gone since; see Progress) | built from CV5 in 2 ms + hand tables |

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
  (`IsomCore::untileable`, `Editor::applyCore`). Always on: StarEdit's null tiles there are glitches, and exactness
  with StarEdit is the reference tests' job.
- Measured (paint_stress, 189 Blizzard maps outside `ladder`, 40 strokes each, through the Editor as the UI paints):

  | | Old engine | Core + repair |
  |---|---|---|
  | Time per brush call | 0.76 ms | 0.25 ms |
  | Misfit tiles, total (most on one map) | 80 (4) | 11 (1) |
  | Invalid diamonds | 0 | 0 |

  Without the repair the core had 48 misfits, 22 of them null tiles on (4)Zoo Keeper.
- Done: each brush size keeps its shape wherever it is (`starEditBrushAt`): an odd size is a block centered on the
  nearest diamond, an even size a block centered on the nearest corner (so it moves in half-diamond steps), both
  StarEdit's blocks through the core; size 0.5 (back in the size list, Alt + wheel and a typed value) is one corner,
  half a diamond, with the old engine.
- Done: resize with the same tileset through the core: ScmDraft's own resize (`IsomCore::resize`: the terrain moved,
  the new ground filled, the edges made, the tiles that stay keep theirs); `isomterrain_test` runs IsomTerrain's 10
  resize reference tests: 10 of 10 perfect. A change of tileset keeps StarDraft's resize (IsomTerrain has none).
- Done: new maps (File > New, the New page's Create) get their tiles from the core: StarEdit's new map (`IsomCore::fill`).
- Decided: the tile variants stay the core's: StarEdit's own rule (a common or a rare variant at random, the right
  tile of a rect as the left one, a cliff stack repeating the variant above). StarDraft's learned choice imitated it.
- Done: the old engine's tiles come from the core too (`regenerateTiles` with the tileset: `IsomCore::retile`, the
  learned rules only for the rects no tile group fits; `IsomGrid::setTileset`). isom_test (290 maps, every tile made
  again from the ISOM): 0.9496 overall against the learned rules' 0.9493; Blizzard's maps 1.0000 on every tileset but
  jungle (0.9999).
- paint_stress (189 Blizzard maps, 40 strokes each):

  | | Time per brush call | Misfit tiles (most on one map) | Invalid diamonds |
  |---|---|---|---|
  | Old engine, learned tiles (before) | 0.76 ms | 80 (4) | 0 |
  | Old engine, the core's tiles | 0.46 ms | 0 | 0 |
  | Core + repair (the editor) | 0.22 ms | 22 (2) | 0 |

- Done: the learned tables are gone (`src/data/*_tile_rules.inc`, `tile_edges.inc`: 6.5 MB). Every question they
  answered comes from the tileset's CV5 through the core's tables (`IsomTiles`: the groups StarEdit picks for a rect
  of two diamond shapes, the rects of a group, the stack connections, which rects can sit side by side):
  - the tiles: `regenerateTiles` is the core's (a rect no tile group fits keeps its tiles);
  - which rects can be tiled while the old engine corrects corners (`rectTileable`, `stacksWithAbove`), the cliff
    stacks and side-by-side checks after it (`stackBreaks`), the misfit count (`countTileMisfits`);
  - whether a tile was placed by hand (`tileFromIsom`), the Tiles palette's terrains (`tileGroupTerrains`), the solid
    tile of a terrain (`solidTileGroup`), maps without an ISOM (`rebuildFromTiles`).
  Every `IsomGrid` that stores has the tileset (`IsomGrid::setTileset`, `create`'s new argument); `isom_test` takes the
  StarCraft folder.
- Measured without them: isom_test (290 maps, every tile made again) 0.9495 (the learned rules 0.9493), Blizzard's
  maps 1.0000 on every tileset but jungle (0.9999); maps without an ISOM, corners right 0.9619 (the learned votes
  0.9341); Blizzard's maps break none of the CV5 rules (2.2 million rect pairs side by side, 4.5 million stacked tiles);
  paint_stress (189 maps): the old engine 0 misfit tiles at 0.44 ms a call, core + repair 0 at 0.22 ms; the old
  engine matches StarEdit on 2 of the 10 references.
- The old engine's corners stay: the repair, the 0.5 brush, paste, Rectangle mode, the terrain generator, maps
  without an ISOM and a resize to another tileset use them (StarDraft's own features, with no StarEdit equivalent).
  Its terrain tables (`IsomTerrainTable`: the terrains and their pairs) are hand-written, not learned.
