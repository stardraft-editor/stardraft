# Symmetry rotation: Isometric/Tile, Stable/Precise, Rotate brush, 2:1 mirrors

Date: 2026-11-23. Status: approved in the design talk.

## Problem

The symmetry turns (the Brushes number next to the symmetry button) have these problems:

1. A turn uses the map's proportions. On a square map, a quarter turn moves an isom diamond (64 x 32 px) off the
   diamond grid.
2. Most tools turn the exact pointer position. The StarEdit brush snaps to its lattice point first. The user cannot
   select the behavior.
3. The Tile, Custom and Rectangle (Solid) terrain tools turn each tile's middle and paint the tile there
   (`tilesMirrored`, `solidMirrored` in `apps/editor/core/tiles_edit.inc`). With 8 turns, a 12 x 12 rectangle gets
   holes in the 45-degree copies and stripes in the 90-degree copies (Solid: a 2-tile pair turned 90 degrees is put
   as one flat pair).

## Controls

Three controls go to the right of the symmetry button and the Brushes number in the tool strip, in the same row.
When the row has no space, they go in the next row.

| Control | Values | Default | Scope | Shows for |
|---|---|---|---|---|
| Isometric / Tile rotation | Isometric, Tile | Tile | Panel (as the mirrors) | All tools with symmetry |
| Stable / Precise rotation | Stable, Precise | Precise | Panel (as the mirrors) | All tools with symmetry |
| Rotate brush | On, Off | On | Each tool: Tile, Custom, Rectangle | Tile, Custom, Rectangle tools only |

- Each control is an icon button. A click changes the value. The tooltip gives the name and the value.
- The values are saved in the panel's state (`PanelState`, `panelStateText`, `panelStateRead` in
  `apps/editor/core/panel_state.inc`) as new fields at the end. An older file without them gives the defaults.
- The two new 2:1 mirrors go in the symmetry pop-out after the four mirrors (Alt+5, Alt+6).

## Isometric / Tile rotation

The turn of a point `p` round the center `c` by the angle `a = 2*pi*k/n`:

- Tile rotation (the current turn): `u = (px - cx) / W`, `v = (py - cy) / H`; turn `(u, v)` by `a`; multiply by `W`, `H`
  again. `W`, `H` are the map's size in pixels.
- Isometric rotation: `u = px - cx`, `v = 2 * (py - cy)`; turn `(u, v)` by `a`; the result is `(cx + u', cy + v' / 2)`.
  A quarter turn moves an offset of `(64 i, 32 j)` to `(-64 j, 32 i)`, so a diamond goes to a diamond.

The toggle changes only the turns. The current diagonal mirrors do not change.

Copies that are off the map are not painted (as now).

## Stable / Precise rotation

- Precise: the editor turns the exact point that the tool gives.
- Stable: the editor first moves the point to the middle of its cell. Then it turns that point.
- With both values, each copy then uses the cell under its turned point (as now).
- The cell of each caller:
  - Isometric terrain tool (brush, spray, fill, StarEdit brush, terrain paste): the isom diamond, the nearest point
    `(64 x, 32 y)` with `x + y` odd.
  - Tile, Custom: the tile (32 x 32).
  - Rectangle (Solid): the 2-tile pair (64 x 32).
  - Units, doodads, sprites, locations: no cell. They already snap to their grid before the call. Stable is the
    same as Precise for them.
- The StarEdit brush follows the toggle. It no longer always snaps first (`terrainSymmetricPoints`).
- The first place (the pointer's own place) is never moved by Stable.

## Rotate brush (Tile, Custom, Rectangle tools)

The anchor of a set of cells is the middle of their bounding box (in pixels). With Stable, the anchor goes to the
middle of its cell. Each copy `k` has a place `P_k` (the anchor turned and mirrored) and a 2 x 2 matrix `M_k` (the
turns and mirrors of that copy, in pixels).

- On (the reverse method): the editor turns the 4 corners of the cells' bounding box with `P_k + M_k (q - anchor)`.
  For each cell in that box on the map, it calculates `anchor + M_k^-1 (cell middle - P_k)`. When that point is in a
  source cell, the copy cell gets the source cell's value. Each target cell is read one time, so a copy has no holes.
- Off: the editor moves the cells by `P_k - anchor` and does not turn them.
- Solid: the same, with 2-tile pairs as cells. A pair keeps its two tiles in their order.
- The preview outlines (`apps/editor/core/map_scene.inc`) use the same functions, so they show what the tool paints.
- A cell that a stroke's own tiles hold is not changed by a copy (as now).

The Isometric terrain tool has no Rotate brush. Its brush and its outlines do not turn. Its filled shapes turn their
corners and fill correctly. Terrain paste has no Rotate brush and keeps its current rules.

## 2:1 mirrors

Two new mirror flags, `Mirror::IsoDiagonal = 16` and `Mirror::IsoAntiDiagonal = 32` (`Mirror::All = 63`):

- IsoDiagonal: across the line through the center that goes down to the right with a slope of 1/2:
  `(dx, dy) -> (2 dy, dx / 2)`.
- IsoAntiDiagonal: across the line that goes up to the right with a slope of 1/2: `(dx, dy) -> (-2 dy, -dx / 2)`.
- They combine with the other mirrors and with the turns, as the other mirrors do. A copy is not flipped (as the
  diagonal mirrors).
- The icon shows a line with a 2:1 slope in the square.

## Tests (`tools/tests/editor_test.cpp`)

- Isometric rotation: with 4 turns, every copy of a corner lattice point is a corner lattice point. A 128 x 128 map
  painted with 4 isometric turns is symmetric under the isometric quarter turn.
- Stable: two points in the same diamond give the same copies. Precise: they can give different copies.
- The 2:1 mirrors: the copy of a corner lattice point is a corner lattice point, and a painted map is symmetric.
- Rotate brush: the app-level functions are in `.inc` files that the test does not build. The reverse method is a
  function in the library (`Editor::turnCells`), and the test calls it: 8 turns of a 12 x 12 block of cells give 8
  copies with no holes (each copy has no empty cell inside its outline), and a 90-degree copy of a block of 2-tile
  pairs has no empty rows.

## Out of scope

- Terrain paste with Rotate brush.
- The diagonal mirrors with the Isometric/Tile toggle.
- Scaling copies to fit the map.
