# Solid: a terrain's inside tiles with Rectangle's tools

The Terrain tool strip has a fourth choice, Solid, after Isometric, Rectangle and Custom. The user selects a terrain in
the same list as Isometric's, and the tools put the tiles that the isometric brush puts inside that terrain (its solid
rects), not its edges. They go in ISOM rects (2 x 1 tiles), as tiles: the ISOM does not change (as SCMDraft's
rectangle terrain). It is SCMDraft's rectangular terrain brush; the request is from the user (2026-10-31).

## Model

- Solid is Rectangle's tools on the map with another source of tiles, as Custom is (`AppState::terrainRect` on,
  `TilesView::solid` on). The kinds: 0 Isometric, 1 Rectangle, 2 Custom, 3 Solid (`terrainKind`). Ctrl+Alt+T goes
  through the four.
- The panel shows the terrain palette (`paletteItems`, Mode::Terrain: the game's terrains, then each mod terrain under
  its ground, no variants), not the Tiles list. `tilesOn()` is false in Solid: the quick picker (Q) lists the
  terrains, as Isometric's does.
- The pick is the palette's (`AppState::palette`), shared with Isometric. No second terrain (Shift+click in the list):
  that is Isometric's.
- The brush size and height are Rectangle's (`otherBrushSize` while Isometric is on).

## The tiles of a rect

`solidRectTiles` (src/isom.cpp) gives the left and right tile of one rect (rx, ry: tiles 2rx and 2rx + 1 of row ry) of
a terrain, as the terrain core makes them for a rect whose two diamonds are that terrain:

- A game terrain: the groups that the CV5 gives for the solid rect (`IsomTiles::groups(parity, t, t)`); the one whose
  top stack connection is the bottom of the tile above (as StarEdit picks), else the first. Its variant by
  IsomTerrain's rule (`getRandomSubtile`): the common variants (the megatiles before the first empty one), and 1 time
  in 20 a rare one (the variants after that gap). Under the group above in its stack (stack connections that are not
  0): the variant of the tile above, as StarEdit gives a stack one variant. The right tile is the next group with the
  same variant. Rows under a dab keep their tiles (Rectangle's rule: no correction).
- A mod terrain (`Tileset::modTerrains`): its pieces for the stand-in's solid groups (its fill; not the gap pieces),
  picked as the ISOM brush picks them (`pickModPiece`, formerly `IsomGrid::pickPiece`: a piece that the mod's maps
  have beside the pieces above it, the first two variants 4 times as often); under a fill piece whose block has a next
  row (one group down, the same subtiles), that row, as the brush keeps a block's rows together. A mod terrain with no fill pieces (its
  inside is the stand-in's own tiles): the stand-in's tiles, as a game terrain.

Each rect gets its own random number (the spray seed). The rects of one dab are made from the top row down, so the
tile above a rect is the one the dab just put there.

## Brush and tools

- The brush is in rects: size n is n rects wide (2n tiles), the height (linked: the size) in rows. The shapes (square,
  diamond, circle) are `tilesFootprint`'s, on the rect grid; a rect is 2 to 1, so a linked Diamond keeps the map's
  diamond shape with no change to its height. Size 1: one rect, 2 x 1 tiles. The rect under the mouse is the middle.
- Brush: the footprint's rects, along a drag (one undo step for the stroke). Spray: random rects of the footprint
  (Density). Fill: the connected tiles (4 ways) of the clicked tile's terrain (the same tile, or a tile of a group with
  the same terrains: `tileGroupTerrains`), and Shift every such tile; in the selection: all of it. Each rect that has a
  filled tile gets new tiles; a half of a rect that is not in the fill keeps its tile. Select, copy and paste:
  Rectangle's, but Ctrl+C does not make the copy the Brush's stamp (the Brush places the terrain; Ctrl+V pastes), and
  Ctrl+click with Select does not pick a tile.
- No ISOM on the map (it could not load): the tools work with the list's terrain; Ctrl+click has no terrain to pick
  and says to select one in the list.
- A Brush stroke makes each rect one time: a drag over rects that the stroke gave tiles does not change them again, and
  a fast drag fills the rects between two mouse positions.
- Mirrors and turns: each rect is placed at the images of its middle too (the same two tiles, in their order, not
  turned), as Rectangle mirrors its tiles. On a map of an odd width the image's middle can be on the line between two
  rects: it goes to the rect at the image's middle (the right one).
- Ctrl+click and Alt+click on the map pick the terrain under the mouse (`Editor::paletteTerrainAt`), as Isometric's.

## ISOM

`Editor::placeTileValues` puts the tiles; the ISOM does not change. The rule of rect-placed tiles applies
(include/stardraft/isom.h, `tileFromIsom`): a later isometric edit keeps these tiles unless it changes the ISOM of their
own rects.

## UI texts

- Segment: "Solid" (the four segments are each their name's width and an equal part of the rest). Tip: "Solid: place
  the tiles inside a terrain, with no edges, in rects of 2 x 1 tiles. Ctrl+Alt+T selects the next choice."
- Status on the choice: "Solid: the tools place the tiles inside a terrain. Select a terrain in the list."
- Tools' tips (Brush, Spray, Fill) say "the tiles inside the terrain".
- No pick: "Select a terrain in the list first (or Ctrl+click the map)".

## Tests

Snapshot cases: `map-solid-brush` (Space Platform: Solar Array, one rect, then Undo; Isometric's list is the same),
`map-solid-size3` (size 3: 6 x 3 tiles), `map-solid-mirror` (a stroke with the left-right mirror puts twice the tiles),
`cmbw-solid-brush` (a Cosmonarchy mod terrain: its fill pieces). The state line's `solidfill` says whether the tile
under the mouse is one of the selected terrain's solid tiles.
