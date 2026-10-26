---
name: stardraft-maps
description: Make or improve a StarCraft melee map (player against player) with stardraft-cli, to the measures and the looks of the ladder maps, and measure a map against them. Use when asked to make a melee map, make a map fair or good to play, make it look like a real map, or check a map's layout (bases, distances, ramps, chokes).
---

# Melee maps with stardraft-cli

Read `stardraft-cli skill` first: it has the open, apply, check, render, save loop and the ops. This skill adds what
makes a melee map good, measured on the ladder maps (2017-2024), and scripts that measure a map against them. It is
for melee maps only; UMS maps have other rules.

## Files

| File | What it is |
| --- | --- |
| `reference/ladder-measures.md` | The ladder maps' numbers: layout, resources, looks, walking. Read it before you plan. |
| `scripts/measure.py <map>` | The layout against the ladder ranges. `OUT` lines are problems. |
| `scripts/looks.py <map>` | Terrain mix and patches, cliffs (way, straight runs, amount), doodads, sprites. |
| `scripts/backing.py <map>` | The share of bases with a cliff, water or the map's edge behind the mineral line. |
| `scripts/clutter.py <map>` | Small obstacles, tight ground, where the sprites are. |
| `scripts/shapes.py` | Terrain shapes and helpers: `blob`, `worm`, `rect`, `fill`, `roughen`, `back`, `narrow_rects`, `open_doodads`. |
| `examples/lagoon/` | A 1v1 Jungle map, 128x112: `terrain.py`, `build.py`, `bases.json`, pictures, `measure.txt`. |
| `examples/dusk/` | A 1v1 Twilight map, 112x128: the mains for a tileset whose ramps fit only lower edges. |

The scripts find stardraft-cli in this checkout's `build/Release`, else `STARDRAFT_CLI`, else the PATH. They need
Python with numpy and scipy. A script given an open map reads its working copy.

## Workflow

Write a build script that makes the map from nothing (copy an example), so that each change is a full rebuild.

1. **Plan.** Size (2 players: 128x112 or 112x128), symmetry (a half turn), one player's bases (main, natural, third,
   more), ramps and chokes, from the numbers in `ladder-measures.md`.
2. **Terrain**, with `"options": {"turns": 2}`. Draw plateaus, lakes and mesas as wavy outlines
   (`fill(blob(...))`, `fill(worm(...))` for rivers); keep `paint_area` rects and diamonds for edges that must be
   straight (a ramp's edge, a base's core). Put each painted area on the diamond grid (x a multiple of 128, y of 64).
   Then `roughen()` the edges away from bases, ramps and chokes. Pass the ops to `apply` in a file.
3. **Backing.** Behind each mineral line not at the map's edge, paint a cliff or water (`fill(back(base))`) before
   the bases go down. Bases at the map's edge go 288 px from it.
4. **Texture.** Patches of the other terrains of the same level (`paint_terrain`, circle brush), far enough from
   cliffs and shores that no cliff moves (`texture()` in the examples).
5. **Ramps**, one by one with no symmetry: `place_ramp` with the doodad for that edge (see Tilesets).
6. **Bases** with `place_base` and the half turn, then `fix_pockets` (`"closed_passages": true` on a map you made).
7. **Doodads.** `decorate` with the small doodads of the ground's groups (`level` 3) and a few bigger ones, with
   `keep_clear` on bases, ramps and `narrow_rects()`. Then `delete_doodads` on `open_doodads()`.
8. **Sprites.** About 3.5 per 1000 tiles, each with `place_sprite` on unwalkable ground at a cliff or shore edge, one
   or two together.
9. **Check.** `stardraft-cli check`, `get <map> pathing` (only pockets and small regions allowed), `measure.py`,
   `looks.py`, `backing.py`, and a `render`, including each ramp close up. Rebuild until nothing is `OUT`.

## Rules

Symmetry and terrain:
- A painted area off the diamond grid comes out different on the two sides of a turn.
- A cliff that faces up is lower than one that faces down, so the two sides differ where a shore or cliff meets a
  cliff. `fix_pockets` removes the unreachable strips this leaves.
- Shapes that must join should overlap by a tile or more: a half-turn image that falls a few pixels short leaves a
  thin strip, a closed passage on a route.
- Keep 6 tiles or more between water areas, and keep diamond tips apart or overlapping: tip to tip leaves 16 px gaps
  or cuts the map in two.

Ramps:
- Put the ramp point on the rendered cliff edge; the cliff lies outside the painted area.
- `place_ramp` can fit a cliff piece that joins the ground but is not a ramp. Name the doodad for the edge and look at
  every ramp close up.
- Jungle ramps need High Dirt beside Dirt: paint Dirt first, then High Dirt on it.

Walking:
- The narrowest choke in the middle should be 128-320 px (ladder median 229). Measure after every shape change: a
  lake that grows a little can close a route or double the rush distance.
- Doodads in open ground or in narrow passages are obstacles. Keep passages clear and delete the ones in the open.
- Run `fix_pockets` after the bases: with `closed_passages` it refuses a fix that lengthens a route from a start
  location, and it needs the start locations for that. On maps made by others the emulated regions can be wrong;
  leave `closed_passages` off there.
- Place bases from a ground-distance map, not by eye (`analyze.py` has the distance functions).

Looks:
- Mineral fields need terrain behind them: 80 % of ladder bases have a cliff, water or the map's edge within 96 px.
- Sprites go at edges, on unwalkable ground. In the open they look like obstacles.
- `get <map> terrain` samples every 128 px by default; use `--step 32` for one value per tile.

## Tilesets

For a new tileset, find on a scratch map and on its ladder maps: its terrains and their ladder shares (`looks.py
--json`), which edges take a ramp (paint a High Dirt rect and diamond, try `place_ramp` as a dry run on each edge,
render), the sprite ids its ladder maps use, and its small ground doodads (`get <map> doodad-types`).

| Tileset | Ramps fit | Sprites | Terrain shares on its ladder maps |
| --- | --- | --- | --- |
| Jungle | Straight edges (293 bottom, 284 top, 302 left, 281 right); lower diagonals (12, 13). No upper diagonal. | 16-30 | Dirt 30 %, Jungle 24 %, High Dirt 14 %, High Jungle 11 %, Water 5 %, Mud, Rocky Ground |
| Twilight | Lower diagonals only (0 south-west, 1 south-east). | 390, 386, 395-401, 98, 99 | Dirt 30 %, High Dirt 21 %, High Crushed Rock 12 %, Crushed Rock 7 %, Water 5 %, Mud 4 %, High Flagstones 3 %, Crevices 2 % |

Under a half turn, a ramp on a lower edge has its image on an upper edge. In Jungle, use straight edges (flatten a
diamond's top and bottom points with small rects). In Twilight there is no such edge, so the mains cannot be exact
images; the ladder maps give each main a ramp on a lower edge of its own. `examples/dusk` paints P1's main and its
image, then cuts a small notch of low ground into P2's main beside the image of P1's ramp; the notch's upper edge
faces south-west and takes a ramp. Give back the notch's area elsewhere, compare the two mains' areas, and keep the
rest a half turn.

## Report

Show the render, a table of the measures against the ladder ranges, and what is still out of range and why.
