---
name: stardraft-maps
description: Make or improve a StarCraft melee map (player against player) with stardraft-cli, to the measures and the looks of the ladder maps, and measure a map against them. Use when asked to make a melee map, make a map fair or good to play, make it look like a real map, or check a map's layout (bases, distances, ramps, chokes).
---

# Melee maps with stardraft-cli

First read `stardraft-cli skill` (the CLI's own skill: the open, apply, check, render, save loop and the ops). This
skill adds what makes a melee map good, measured on the ladder maps, and scripts to measure a map.

- `reference/ladder-measures.md`: the numbers (layout, resources, looks). Read it before you plan a map.
- `scripts/measure.py <map>`: the layout of an open map against the ladder ranges (`OUT` lines are the problems).
- `scripts/looks.py <map>`: the looks (terrain mix and patches, cliffs: their ways, how long they run straight and how
  much of them there is, doodads, sprites) to compare with the table.
- `scripts/backing.py <map>`: what is behind each base's mineral line (the share of bases with a cliff, water, a
  structure or the map's edge within 96 px behind the fields).
- `scripts/clutter.py <map>`: how easy the ground is to walk (small obstacles, tight ground) and where the sprites
  are (on walkable ground or at an edge).
- `scripts/shapes.py`: organic shapes. `blob()` (a wavy outline), `worm()` (a band along a path: a river, a ridge),
  `rect()`, `fill()` (the `paint_terrain` strokes that paint an outline's inside), `roughen()` (bulges and bites on
  the edges that are there), `back()` (terrain behind a mineral line), `narrow_rects()` (the narrow passages, as
  `keep_clear` for `decorate`) and `open_doodads()` (the doodads in the open or in a narrow passage, for
  `delete_doodads`).
- `examples/dusk/`: a 1v1 Twilight map, 112x128 (`build.py`, `bases.json`, `dusk.png`, `ramps.png`): the mains
  for a tileset whose ramps fit only lower edges.
- `examples/lagoon/`: a 1v1 map made this way: `terrain.py` (the shapes), `build.py` (the rest), `bases.json`,
  `lagoon.png`, `ramps.png` (each ramp close), `measure.txt`. Copy them: they are the loop below in code.
- The scripts find stardraft-cli in this checkout's `build/Release`, else `STARDRAFT_CLI`, else the PATH. They need
  Python with numpy and scipy.

UMS maps (Use Map Settings) have other rules: this skill is for melee maps only.

## The way to make a map

Write a build script (as `examples/lagoon/build.py`) that makes the map from nothing, so that each change is a new
build. Then:

1. **Plan** with the table: size (2 players: 128x112 or 112x128, not 128x128), symmetry (a half turn), the bases of
   one player (main, natural, third, more), where the ramps and chokes go.
2. **Terrain** with symmetry (`"options": {"turns": 2}`), from organic shapes: `shapes.fill(blob(...))` for plateaus,
   lakes and mesas, `worm()` for rivers. Ladder terrain is fluid: its cliffs turn every 60-140 px, and there is about
   2 px of cliff for each tile. Rects, ellipses and diamonds (`paint_area`) give long straight sides: use them only
   where an edge must be straight (a ramp, a base's core), on the diamond grid (x a multiple of 128, y of 64). Give
   `apply` the ops in a file: the strokes are many.
   Then `roughen()` the edges (not near the bases, the ramps and the chokes).
   Behind each mineral line that is not at the map's edge, paint terrain: `fill(back(base, ...))` with a higher
   terrain (a cliff) or Water, before the bases. On the ladder maps 80 % of the bases have it within 96 px; fields
   out in the open look wrong. Put bases at the map's edge 288 px from it (the fields then touch the edge).
3. **Texture** before the ramps: patches of the other terrains of the same level (`paint_terrain` with a circle
   brush), far enough from the cliffs and shores that no cliff moves. The example's `texture()` does it from
   `get <map> walk`.
4. **Ramps** one by one, with no symmetry (`place_ramp`). For a main ramp, give the wide doodads (Jungle: 293 at a
   bottom edge, 284 at a top edge).
5. **Bases** with `place_base` and the half turn, then `fix_pockets` (with `"closed_passages": true` on a map made
   from nothing). After the bases: with closed passages it checks the ways from the start locations, so they must be
   there. If it refuses, a closed passage is on a way: change the terrain there (or fix the pockets only).
6. **Doodads and sprites**: `decorate` with the small doodads of the ground's groups (`level` 3) and a few bigger ones
   (`few`), with `keep_clear` on the bases, the ramps and the narrow passages (`narrow_rects()`); then delete the
   doodads in the open (`open_doodads()`): in the open and in passages they are obstacles in the way.
   Sprites delicately: few (about 3.5 for each 1000 tiles), each with `place_sprite` on ground that is NOT walkable,
   at its edge (a cliff, a shore), one by one or two together, never in the open (see the examples).
7. **Check**: `stardraft-cli check`, `get <map> pathing` (after decoration only pockets and small regions are
   allowed), `scripts/measure.py`, `scripts/looks.py`, and look at `render`. Change the plan and build again until no
   line is `OUT` and the looks are in the table's ranges.

## Another tileset

Find out these four things first, on a scratch map and on the ladder maps of that tileset (`ladder-looks.txt` names
their tilesets; `scripts/ladder_extract.py` makes the data):

1. **Terrains**: `get <map> terrains`, and the ladder share of each (`looks.py --json` on its ladder maps).
2. **Ramps**: paint a High Dirt rect and a High Dirt diamond on a scratch map, `place_ramp` (dry run) at each edge,
   and render what fits. The edges that take a ramp differ by tileset (below).
3. **Sprites**: the ids that its ladder maps use (`get <map> sprites`, not the doodads' overlays).
4. **Small doodads**: the 2x1 and 2x2 doodads of its ground groups (`get <map> doodad-types`).

| Tileset | Ramps fit | Sprites on its ladder maps | Texture (ladder shares) |
| --- | --- | --- | --- |
| Jungle | straight edges (293 bottom, 284 top, 302 left, 281 right); lower diagonal edges (12, 13); no upper diagonal edge | 16-30 (plants) | Dirt 30 %, Jungle 24 %, High Dirt 14 %, High Jungle 11 %, Water 5 %, Mud, Rocky Ground |
| Twilight | ONLY lower diagonal edges (0 south-west, 1 south-east): no straight edge, no upper edge | 390, 386, 395-401, 98, 99 | Dirt 30 %, High Dirt 21 %, High Crushed Rock 12 %, Crushed Rock 7 %, Water 5 %, Mud 4 %, High Flagstones 3 %, Crevices 2 % |

With a half turn, a ramp on a lower edge has its image on an upper edge. In Twilight no upper edge takes a ramp, so
the ladder maps (Neo Dark Origin, Polaris Rhapsody, Overwatch) make each main's ramp on a lower edge of its own: the
top player's main leaves on its south-east edge, the bottom player's on its south-west edge. The two main plateaus
can then not be half-turn images. What works (`examples/dusk`): paint P1's main and its half-turn image, then cut a
notch of low ground (a small Dirt diamond) into P2's main beside the place of P1's ramp's image: the notch's upper
edge faces south-west and takes the ramp. Give P2's main the notch's area back elsewhere (a rect on its far side),
measure both mains' areas, and keep everything else a half turn. Use mesas with no ramp for other high ground.

## What went wrong before (do not do it again)

- A terrain area off the diamond grid: the two sides of a turned map came out different.
- A cliff that faces up is lower than one that faces down: where a shore meets a cliff, the two sides differ and a
  strip of ground that no unit can reach stays. `fix_pockets` makes it not walkable.
- A chain of water areas tip to tip from edge to edge: the map was cut in two. Leave gaps of 6 tiles or more between
  water areas; `check` says when the start locations cannot reach each other.
- Two diamond tips that nearly touch make a gap of 16 px: only small units pass. Keep tips apart, or overlap them.
- Jungle ramps need High Dirt beside Dirt: paint Dirt, then High Dirt on it. High Jungle next to grass takes no ramp.
- Ramps on a diamond's lower edges (Jungle 12, 13) have their half-turn images on upper edges, where no ramp fits:
  `place_ramp` put cliff pieces there (Jungle 287, 290) that join the ground but look broken (the user saw it). Use
  straight edges: flatten the top and bottom points with small rects and give the doodad (Jungle: 293 bottom, 284
  top, 302 left, 281 right). Put the ramp point on the real cliff edge: the cliff is outside the painted area.
  Render each ramp close and look at it before you report.
- Sprites sprayed in groups on open ground: too many, and they look like obstacles in the way (the user saw it). On
  the ladder maps 56-90 % of the sprites are on ground that is not walkable, at a cliff or a shore.
- A wavy lake and a plateau left a 58 px choke on the only way between the mains: armies cannot pass (the user saw
  that units would have trouble). The ladder maps' narrowest choke in the middle is 86-283 px (median 229);
  `measure.py` shows it ("narrowest mid-map choke").
- `fix_pockets` with closed passages on a map made by others cut a real walkway (the emulated regions can be wrong):
  use `closed_passages` only on your own map, and look at `units_removed` and the distances after it.
- Doodads near a lake's shore or a mesa joined the wall and made a 45-80 px choke in a 300 px passage: keep
  narrow passages clear (`narrow_rects()` as `keep_clear`) and delete the doodads in the way (`open_doodads()`).
- `fix_pockets` before the bases had no start locations to check, and closed a gap that was the way through the
  middle. Run it after the bases.
- A shape's half-turn image did not reach the shape it should join (a river and a mesa): a thin strip stayed, a
  closed passage on a way. Make shapes that must join overlap by a tile or more.
- Mineral fields out in the open, with nothing behind them (the user saw it): 17 % of the bases backed, against 80 %
  on the ladder maps. Paint a cliff or water behind each mineral line (`back()`), and check with `backing.py`.
- Rectangles, ellipses and diamonds looked made by a program. Wavy outlines (`blob`, `worm`) filled with strokes look
  like the ladder maps; check with `looks.py` (cliff run, cliff px per tile).
- A wavy lake that grew too big closed the middle: the rush distance went from 5600 to 6900 px. Measure after each
  change of the shapes.
- Moving shapes by guess did not converge. Print the ground distance from a start location (see `measure.py`'s
  `analyze` module) and place the bases where the distances are right.
- A `decorate` on a crowded map can close a passage: try the next `seed`; pockets in a doodad are fine.
- `stardraft-cli get <map> terrain` reads one point every 128 px by default; give `--step 32` for one per tile.
- A main is measured from the start location: a doodad with low ground in it (a crater) on the main is not an exit.

## Report to the user

Give the picture (`render`), the table of the measures with the ladder ranges, and what is still out of range and
why.
