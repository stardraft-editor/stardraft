---
name: stardraft
description: Make or improve a 1v1 StarCraft melee map (two players) with stardraft-cli, to the measures and the looks of the 2-player ladder maps, and measure a map against them. Use when asked to make a 1v1 melee map, make one fair or good to play, make it look like a real map, or check its layout (bases, distances, ramps, chokes).
---

# 1v1 melee maps with stardraft-cli

This skill is for **1v1 melee maps**: two start locations, a half turn. Read `stardraft-cli skill` first (the open,
apply, check, render, save loop and the ops). This skill adds the targets of a good 1v1 map and scripts that measure
a map against them. Maps for more players and UMS maps have other rules.

## Files

| File | What it is |
| --- | --- |
| `reference/targets.md` | The targets: layout, bases and chokes, looks. Read it before you plan. |
| `examples/fernway/` | A complete 1v1 Jungle map, 128x112: `build.py` (the whole map from nothing), `bases.json`, `measure.txt`. Copy it. |
| `scripts/measure.py <map>` | The layout and fairness against the targets. `OUT` lines are problems. |
| `scripts/looks.py`, `backing.py`, `clutter.py` | Terrain mix and cliffs; what is behind each mineral line; small obstacles and sprites. |
| `scripts/shapes.py` | `blob`, `worm`, `fill` (shapes), `extreme`, `ramp_pair` (ramps), `back`, `roughen`, `narrow_rects`, `open_doodads`. |

Paths are relative to this skill's folder, `${CLAUDE_SKILL_DIR}`: run a script as
`python "${CLAUDE_SKILL_DIR}/scripts/measure.py" <map>`. The scripts find stardraft-cli in `STARDRAFT_CLI`, else in
the build folder of a StarDraft checkout that holds this skill, else on the PATH (it comes with each StarDraft
release). They need Python with numpy and scipy.

## Workflow

A build script makes the map from nothing, so each change is a full rebuild (seconds). Copy
`examples/fernway/` to the user's work folder and change it; run the copy with the
environment variable `SKILL_SCRIPTS` set to `${CLAUDE_SKILL_DIR}/scripts`.

1. **Plan** P1's side (top-left) from `targets.md`: the main in a corner, the natural, the third, 2-3 more bases, the
   chokes, overlord spots, the middle. The half turn makes P2's side.
2. **Terrain** with `"options": {"turns": 2}`: every shape is `fill(blob(...))` or `fill(worm(...))`. Then
   `roughen()`, away from bases, ramps and chokes.
3. **Texture**: patches of the other terrains of the same level, away from cliffs and shores.
4. **Ramps**: `ramp_pair()` at `extreme(shape, side)` of each shape that takes a ramp.
5. **Bases** with `place_base` and the half turn, then `fix_pockets`.
6. **Doodads** (`decorate`, `keep_clear` on bases, ramps, chokes and `narrow_rects()`), then delete `open_doodads()`.
7. **Sprites**, one `apply` each (a sprite whose image does not fit is left out).
8. **Check**: `stardraft-cli check`, `get <map> pathing`, `measure.py`, `looks.py`, `backing.py`, and a `render` of the
   map and close-ups of each ramp and choke. Rebuild until nothing is `OUT`.

## Rules

Shapes:
- No `paint_area` rectangles or diamonds for terrain: their edges come out wrong more often than right. Use blobs and
  worms.
- Water and cliffs come out about a tile wider on each side than the outline: a river or stream is a worm of 100-120 px.
- Keep 6 tiles or more between separate water areas and between mesas, or they join or leave thin strips.
- Shapes that must join should overlap by a tile or more.

Ramps:
- Jungle ramp doodads: 293 on a bottom edge, 284 top, 281 right, 302 left. The half turn's image of a bottom edge is
  a top edge, of a right edge a left edge.
- A ramp needs a straight run of edge: put it at `extreme()` of a shape with only long waves (`waves=(2, 3)`), and not
  where the edge meets water.
- The image side has other cliff tiles, so the same doodad often fits at another place there. `ramp_pair()` scans
  both sides and takes the pair that lands as images; if it finds none, change the shape.

Fairness:
- A cliff that faces one way is wider than its image, so a choke between cliffs comes out narrower on one side.
  Make chokes between water shores (they come out the same), or measure both sides and change the shape.
- Bases: `place_base` puts the geyser 176 px above the town hall. Leave room on both sides; a geyser that does not
  fit is moved, sometimes far. Check `base pair spread` in `measure.py`.
- Doodads in open ground and in passages are obstacles: keep the chokes and the ground in front of them clear.

Measuring:
- `measure.py`'s natural and third choose a town hall spot from the fields; for a base with no geyser the guess can
  be wrong. `base pair spread` uses the editor's own distances.
- `get <map> walk` and a row scan give a choke's real width; `measure.py`'s widths are along the rush path.

## Tilesets

Jungle is the default. For another tileset, find its terrains, which edges take a ramp (try `place_ramp` in `apply --dry-run`
on a scratch map with a blob), its sprites and its small doodads (`get <map> doodad-types`). Twilight: ramps only on
lower diagonal edges (doodads 0 south-west, 1 south-east), so P2's ramp cannot be an exact image.

## Report

Show the render, the `measure.py` table, and what is still out of range and why.
