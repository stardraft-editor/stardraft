# Bridge and ramp tool: design

Status: approved for implementation planning. Date: 2026-11-01.

This folder holds everything that the implementation needs:

| Path | Contents |
|---|---|
| `2026-11-01-bridge-ramp-tool-design.md` | This spec |
| `data/bridge_kits.json` | The bridge kits: one per tileset and orientation |
| `data/ramp_kits.json` | The ramp kits: one per tileset and orientation |
| `data/reference_generator.js` | A small model of the generator. It reads only the two JSON files. Use it to make test data for the C++ code |
| `examples/examples_<tileset>.scm` and `.png` | The approved example maps, one for each tileset (see "Example maps"). The bridge kits were learned from the bridges in their top band |

## 1. Purpose

Map makers need bridges and ramps of any size. The game has only a few fixed bridge and ramp doodads. The tool
builds a bridge or a ramp of a chosen size from tile pieces, and places it as terrain tiles. The result must look as
if the game had a doodad of that size, and it must join ISOM terrain without hard edges where the data allows.

## 2. Terms

- **Orientation.** Bridges have 2 orientations: `d` (the deck runs from upper left to lower right) and `u` (from
  lower left to upper right). Ramps have 8 orientations: `N`, `NE`, `E`, `SE`, `S`, `SW`, `W`, `NW`. The letter is
  the cliff edge that the ramp is cut into (`S` = the south edge of a plateau, the ramp goes down to the south).
- **Size.** A size is a count of repeated middle pieces, not a count of tiles. Size `0.5` is the two ends with no
  middle piece. Size `1` has one middle piece, size `2` two, and so on. One middle piece of a bridge is one isometric
  step (2 tiles across, 1 tile down). One middle piece of a ramp is one slope doodad (4 tiles for a straight ramp,
  one diagonal step for a diagonal ramp).
- **Length** is along the direction of travel over a bridge. **Width** is across it. Ramps have a width only.
- **Kit.** The data that builds one orientation of one tileset at any size.
- **Null tile.** Tile value 0. The example maps use it as a background.

## 3. User interface

### 3.1 Where the tool is

The tool is a part of Terrain mode. A new panel, "Bridges and ramps", is beside the Custom terrain list
(`apps/editor/core/views_stamps.inc`). It shows:

- A choice: Bridge or Ramp. It is not available when the tileset has no kit (see section 7).
- Two number fields: Length and Width. Length is not available for ramps.
- The orientation, as text (for example "Bridge, down-right" or "Ramp, SW").

### 3.2 How the tool places

The tool makes a terrain stamp: tiles plus a mask, as a saved custom terrain stamp has
(`TerrainStamp`, `include/stardraft/terrain_stamp.h`). It gives the stamp to the Brush with the existing
`stampsUse` (`apps/editor/core/stamps.inc:67`). So the existing stamp preview, the click to place, the drag to
place again, Esc and right click to stop, and undo all work as they do for custom terrain. The tool does not need a
new placement path.

When the user changes a size, the choice or the orientation, the tool makes the stamp again and gives it to the
Brush again.

### 3.3 Keys

| Key | What it does |
|---|---|
| R | The next orientation (bridges: d, u; ramps: N, NE, E, SE, S, SW, W, NW, then again) |
| Shift+R | The orientation before |

R and Shift+R already turn terrain that is about to be pasted (`docs/help/keyboard-shortcuts.md`). The meaning is
the same ("turn what is about to be placed"), so there is no conflict. Add the new use to the keyboard shortcuts
page. The tool adds no other keys.

### 3.4 Sizes and limits

- Allowed sizes: 0.5, then the whole numbers 1, 2, 3, and up.
- There is no fixed maximum. The stamp must fit in the map; the number fields stop at the largest size that fits.
- Making a stamp is fast (a table lookup for each tile). If a test shows that the preview takes longer than one frame
  at size 32 x 32, then use 32 as the maximum for both fields.
- Badlands bridges are an exception for length. See section 5.6.

### 3.5 Random variants

Some kit positions have more than one allowed tile pair (section 5.4). The tool picks one at random each time it makes
the stamp. A "Reroll" button makes the stamp again with a new random seed. The seed changes only when the user asks,
so the preview does not change while the user moves the mouse.

### 3.6 Symmetry

Version 1 places the stamp as the Brush places custom terrain today: the symmetry setting does not mirror tiles.
A later version can place the mirrored orientation at each mirrored place (d and u are exact mirrors; see 5.5).

## 4. Data files

Both files are JSON. Tile values are MTXM values (`group << 4 | index`). A tile pair is written `"a,b"`: the tile at
an even column x and the tile at x + 1. Bridges are always placed in tile pairs, with the first tile of a pair on an
even map column.

`data/reference_generator.js` is the exact algorithm of sections 5 and 6 in about 100 lines. The C++ code must give
the same tiles as this model for the same seed rule (the random choice may use another random generator).

## 5. Bridges

### 5.1 Coordinates

A kit has two step vectors in pair cells (one pair cell = 2 tiles across, 1 tile down):

- `step.length` = `L`, `step.width` = `W`, for example `L = (1, 1)`, `W = (1, -1)`.
- A pair cell `(P, y)` has the axis coordinates `wl = P*L.x + y*L.y` and `ww = P*W.x + y*W.y`.
- One isometric step along an axis changes that coordinate by 2.

### 5.2 Extent for a size

For lengthSize and widthSize, with `pieces(s) = 0 if s = 0.5, else s`:

```
extentLength = caps.start + caps.far   + middleAtSizeHalf.length + 2 * pieces(lengthSize)
extentWidth  = caps.side0 + caps.side1 + middleAtSizeHalf.width  + 2 * pieces(widthSize)
```

`middleAtSizeHalf` can be 0 or negative. A negative value means that the two caps share cells at size 0.5.

### 5.3 The class of each cell

For `a` in `0 .. extentLength-1` and `c` in `0 .. extentWidth-1`:

1. `wl = a + start.length`, `ww = c + start.width`. Solve for `(P, y)`. If `P` or `y` is not a whole number, there
   is no cell.
2. The class along one axis, for position `w` in an extent `n`, with caps `s` and `f` and period `p`:
   - if `w < s`: `S<w>` (the start cap). Test the start cap first.
   - else if `n - 1 - w < f`: `F<n-1-w>` (the far cap)
   - else `M<(w - s) mod p>` (the middle, with its phase)
3. The key is `<length class>|<width class>`, for example `S2|M1`.
4. `table[key]` gives the allowed tile pairs, with how often each one is in the approved example bridges. The value `none`
   means "no tile here" (the bridge outline). A key that is not in the table means "no tile".

### 5.4 Variants and walkability

- A key with more than one pair is a random variant. Pick by weight (the counts in the table).
- These variants came from the approved example bridges: the user's hand fixes and random choices (for example Badlands
  Asphalt in the deck, the Jungle deck tile pairs `16:6`/`16:7`).
- No key mixes `none` with tiles. So the outline of a bridge never depends on the random choice.
- The deck must be fully walkable. In the keys `M<i>|M<j>` (the deck interior), the data keeps only fully walkable
  pairs when such a pair exists. This is already done in `bridge_kits.json`. Badlands Asphalt pairs (groups 16/17)
  count as walkable.
- Test (do it in the C++ tests): for every size from 0.5 to 6 in both fields, no deck tile `M|M` has a blocked
  minitile.

### 5.5 The two orientations

`d` and `u` are separate kits. Each one is learned from its own approved example bridges. The `u` bridges are exact pixel
mirrors of the `d` bridges in Badlands, Desert, Ice and Twilight (each tile has a mirror twin with the same image flipped;
Badlands Asphalt mirrors as a 16/17 pair with the same index).

### 5.6 Size numbers for each tileset

The example maps show sizes 0.5, 1, 2 and 3 in both fields, with one exception:

| Tileset | Length sizes in the example columns | Width sizes in the example rows | The game bridge is |
|---|---|---|---|
| Jungle, Space Platform, Desert, Ice, Twilight | 0.5, 1, 2, 3 | 0.5, 1, 2, 3 | length 1, width 1 |
| Badlands | 1, 2, 3, 4 | 0.5, 1, 2, 3 | length 2, width 1 |

Badlands length 0.5 is in the data (it is 2 tiles shorter than length 1), but no approved example shows it. The map
maker can decide later whether to keep it.

### 5.7 ISOM: how a bridge joins a river

A bridge joins ISOM terrain when it crosses a river of the right width at the right place. The tool does not paint
the river. It snaps the stamp so that its ends meet the banks of a river that ISOM painting made.

- The river runs across the bridge. For `d` the river runs from lower left to upper right (`dir: "ur"`): its brush
  points are at `D + k*(4, -2)` tiles. Each extra row of river width is at `+ m*(2, 1)`. Twilight `d` is the other
  way round (`dir: "dr"`: points at `D + k*(4, 2)`, rows at `+ m*(2, -1)`).
- `D` is a diamond centre: tile `x` even and `x/2 + y` even.
- River rows (1-diamond brush) for a length size: `rows = rowsAtSize1 + pieces(length) - 1`. If `rows` is 0, the
  river is one row of the 0.5 brush, painted at `D` + (24, -4) pixels.
- The bridge origin: `O = D + river.offset`, plus (2, 1) when the river is a 0.5-brush row. The origin is the cell
  `(P, y) = (0, 0)` of the axis coordinates in 5.1, so the tile of cell `(P, y)` is at `(O.x + 2P, O.y + y)`.
- The water must continue at least one diamond past each rail along the river. Else the end of the river cuts the bank
  tiles.
- `u` is the mirror of `d`: mirror the river points and the bridge about a vertical axis at an even tile column.

| Tileset | Ground | Water | rowsAtSize1 | offset |
|---|---|---|---|---|
| Jungle | Dirt | Water | 1 | (-2, -5) |
| Space Platform | Platform | Space | 1 | (0, -7) |
| Desert | Dirt | Tar | 2 | (-2, -7) |
| Ice | Dirt | Water | 3 | (0, -6) |
| Twilight | Dirt | Water | 2 | (-2, -3) |
| Badlands | road | Water | not used | not used |

- **Badlands** is a road bridge. Its ends are square Asphalt and join an Asphalt road, not river banks. The tool
  places it where the user clicks, on tile pairs, with no river snap.
- **Ice** bridges join Dirt banks, not Snow.
- Snapping rule for the tool: when the Brush is over a tile, find the nearest `D` (a diamond centre) and place the
  stamp at `O = D + offset`. Show the snapped preview. Hold Alt to place without the snap (tile pairs only).

## 6. Ramps

### 6.1 The kit

Each orientation has three pieces from the game's SC:R cliff doodads: wall A, slope, wall B, and two vectors:
`start` and `step` (tiles).

```
wall A          at p
slope k         at p + start + k*step      for k = 0 .. n-1
wall B          at p + start + n*step
n = pieces(widthSize)   (size 0.5: walls only)
```

The pieces never overlap. Each piece is written as tiles (the doodad's tiles from the tileset), not as a doodad.
`ramp_kits.json` has the tiles of each piece.

### 6.2 Kits for each tileset

The pieces are the same in each tileset, in the same order, from a different first doodad id. The JSON has the
final ids. Jungle ids:

| Orientation | Wall A | Slope | Wall B | start | step |
|---|---|---|---|---|---|
| N | 285 | 284 | 283 | (2, 0) | (4, 0) |
| S | 292 | 293 | 294 | (2, 0) | (4, 0) |
| E | 280 | 281 | 282 | (0, 2) | (0, 4) |
| W | 301 | 302 | 303 | (0, 2) | (0, 4) |
| NW | 289 | 290 | 291 | (0, -1) | (2, -1) |
| NE | 286 | 287 | 288 | (-2, -1) | (-2, -1) |
| SW | 298 | 299 | 300 | (0, 1) | (2, 1) |
| SE | 295 | 296 | 297 | (-2, 1) | (-2, 1) |

| Tileset | id = Jungle id − 272 + | Low ground | High ground | Exception |
|---|---|---|---|---|
| Jungle | 272 | Dirt | High Dirt | |
| Badlands | 363 | Dirt | High Dirt | |
| Ashworld | 143 | Dirt | High Dirt | |
| Ice | 268 | Snow | High Snow | The S slope is doodad 272 |
| Space Platform | 264 | Platform | Elevated Catwalk | |

Desert, Twilight and Installation have no SC:R ramp pieces. The tool has no ramps there.

### 6.3 Width steps

Ramps grow in steps of one slope piece (4 tiles for N, S, E, W). A 2-tile step was tested and dropped: the ISOM cliff
pattern repeats every 4 tiles, so a wall moved by 2 tiles meets the cliff out of step.

### 6.4 ISOM: snapping to a cliff

- A ramp must be cut into a cliff edge of its orientation. The anchor `p` (wall A) snaps to the nearest place on that
  edge where wall A fits. "Fits" is the game's own doodad terrain rule (`dddata.bin`: `DoodadType::required`, and
  the CLI's `doodadFits`).
- **Trap:** the game's terrain rule refuses all NE pieces and the NW wall B, even at places where they are correct (the
  user built the diagonal ramps of the examples by hand, and they pass review, but the rule refuses them). So:
  - NW: snap by wall A only (wall A fits; wall B does not).
  - NE: wall A never fits. Snap NE to its lattice instead: the anchor must have the same position on the ISOM grid as
    in the user's example. On a plateau painted with the 12-diamond brush, the example anchors are, from the brush
    centre: NW (-16, -9), NE (11, -9), SW (-16, 4), SE (12, 4). Other valid anchors are these moved along the edge by
    whole multiples of 2 × `step`.
- Straight anchors repeat along an edge every 4 tiles. Diagonal anchors repeat every 2 steps.

## 7. Tilesets

| Tileset | Bridges | Ramps |
|---|---|---|
| Jungle | yes | yes |
| Space Platform | yes | yes |
| Badlands | yes (road) | yes |
| Desert | yes | no |
| Ice | yes | yes |
| Twilight | yes | no |
| Ashworld | no | yes |
| Installation | no | no |

## 8. Example maps

`examples/examples_<tileset>.scm`, 256 x 256 tiles. The user reviewed all of them in the editor.

1. **Top band (null tiles).** The `d` bridges on the left, the `u` bridges on the right. Rows: width 0.5, 1, 2, 3.
   Columns: length 0.5, 1, 2, 3 (Badlands: 1, 2, 3, 4).
2. **Middle (ISOM terrain).**
   - Left: every `d` bridge over its own river (section 5.7), then every `u` bridge as the exact mirror.
   - Right: one block for each ramp size (0.5, 1, 2, 3). Each block has a diamond plateau with the 4 diagonal ramps and a 28 x 28 plateau with the 4 straight ramps.
3. **Bottom band (null tiles).** All 32 ramps alone: sizes 0.5 and 1 in the first row, 2 and 3 in the second.
   In each size: N, S, E, W, NW, NE, SW, SE.

Ashworld has ramps only. Desert and Twilight have bridges only.

The bridges in the top band are the approved bridges that the kits were learned from. The C++ generator, with the
same random choices, must give the same tiles. The reference generator gives the exact outline of all 192 of these
bridges (17,440 tiles, 0 outline differences). The tiles differ only where a key has random variants.

## 9. Tests

- **Outline test.** For each tileset, orientation and size in the top band of the example map, generate the bridge
  and compare its outline with the example: 0 differences.
- **Variant test.** Each generated tile pair is in `table[key]` for its key.
- **Walkability test.** Section 5.4.
- **Ramp test.** For each tileset, orientation and size 0.5 to 3, the generated tiles are the same as the bottom band of
  the example map.
- **Snap test.** For each straight orientation, and for SW and SE, a ramp snapped on a test plateau passes the game's
  doodad terrain rule for every piece. NE and NW: compare with the example maps.
- **Mirror test.** For Badlands, Desert, Ice and Twilight, `u` at a size is the pixel mirror of `d` at that size.
- Use `tools/tests/editor_test.cpp` for the editor parts, and the CLI (`stardraft-cli apply` with `place_tiles`) for
  test maps. To check a test map that the CLI wrote, save it with `stardraft-cli save <map> --to <file>`: after
  `apply`, the map file on disk does not change until a save.

## 10. Known gaps

- The tool does not paint rivers or plateaus. It snaps to the ones that are there.
- The tiles that the tool places are raw tiles. ISOM painting next to them later can change them, as it can change
  custom terrain stamps today.
- Badlands length 0.5 and the ISOM placement of Badlands bridges (on roads) are not shown in an approved example.
- Bridges cross only straight rivers. A bridge at a river bend is not handled.
- The tool does not change units, doodads or sprites under the stamp. It follows the Brush's existing rules for them.
