---
name: stardraft-cli
description: Make and edit StarCraft maps (.scm, .scx) and StarDraft EUD projects with stardraft-cli, with no window. Use when asked to make a map, change units, terrain, locations, players, triggers or briefings, edit .dat values of a project, build an EUD map, or check that StarCraft can start a map.
---

# stardraft-cli

`stardraft-cli` edits maps with the same rules as the StarDraft 2 editor (placement, terrain correction, doodads).
Each command writes one JSON object: `{"ok": true, ...}`, or `{"ok": false, "error": "..."}` with exit code 1.
It needs a StarCraft: Remastered install: `--install <folder>` or `STARDRAFT_INSTALL`, if it does not find one.

## The loop

1. `stardraft-cli open <map>`: makes a working folder (`<map>.work`). The map file does not change until `save`.
2. `stardraft-cli get <map> summary` (also `units`, `doodads`, `locations`, `players`, `triggers`, `terrain`,
   `pathing`, ...): read before you change.
3. `stardraft-cli apply <map> ops.json --label "what this does"`: one undo step. All the operations work, or none.
4. `stardraft-cli check <map>`: the checks of a save. Read `problems` (they stop a save) and `warnings`.
5. `stardraft-cli render <map> -o view.png --grid 512`: a picture. Look at it. The JSON data does not show
   everything: closed areas, units in bad places, a layout that looks wrong.
6. Repeat 2-5. `undo` goes back one step.
7. `stardraft-cli save <map>`, then `stardraft-cli close <map>`.

Show the user the pictures of the result (`render`), not only the JSON.

## Rules

- Positions are map pixels (a tile is 32 px). An area is `[left, top, right, bottom]`.
- Players are 1-12 (12: neutral). Resources (mineral fields, geysers) belong to player 12.
- Names can repeat ("Mineral Field" is 176, 177 and 178). Look up ids: `get <map> unit-types`, `doodad-types`,
  `terrains`, and `stardraft-cli trigger-types`. `stardraft-cli ops` lists the operations and their parameters.
- Read each result of `apply`. A terrain edit removes the units and doodads that no longer fit: see `units_removed`
  and `doodads_removed`, then place again what you need.
- A melee map needs a "Start Location" unit for each player (`place_unit` with type "Start Location", or the op
  `place_start_locations`). `save` refuses without them; `--anyway` saves.
- High ground needs a ramp to be reachable. `check` warns when start locations cannot reach each other by ground.
- Use symmetry for fair maps: `{"options": {"turns": 2}, "ops": [...]}` (a half turn) or `"mirrors"`. It applies to
  the edits in that `apply` only.
- Put the operations in a file (`ops.json`), not in a command line: the shell can change backslashes and quotes.
- For many requests, `stardraft-cli serve` keeps the game data open: one JSON request for each line on stdin.

## A first map

```
stardraft-cli new duel.scx --size 112,128 --tileset jungle --terrain Jungle
stardraft-cli open duel.scx
```

ops.json (a half turn: each edit is made twice, and the second base's start location goes to player 2):

```json
{"options": {"turns": 2},
 "ops": [{"op": "set_scenario", "name": "Duel", "description": "A 1v1 map"},
         {"op": "paint_area", "terrain": "Water", "area": [1408, 1792, 2176, 2304], "shape": "ellipse"},
         {"op": "place_base", "x": 288, "y": 400, "side": "left", "minerals": 9, "player": 1},
         {"op": "place_base", "x": 320, "y": 1328, "side": "left", "minerals": 7},
         {"op": "decorate", "level": "some"}]}
```

```
stardraft-cli apply duel.scx ops.json --label "Layout"
stardraft-cli check duel.scx
stardraft-cli render duel.scx -o duel.png --grid 512
stardraft-cli save duel.scx
```

## Ramps

The tilesets have no "ramp" name: a ramp is a cliff doodad that fits one side of a cliff. Use `place_ramp`, not
`place_doodad`: `{"op": "place_ramp", "x": 860, "y": 1088}`. Put the point on the cliff edge between the high and the
low ground. It finds the doodad that fits there and joins the two sides, and places it. Use it in an `apply` with no
symmetry: the other side of a symmetric map needs a different doodad, so place each ramp on its own.

- A rectangle of high ground (`paint_area`, shape `rect`) takes ramps on all four sides. A diamond's lower sides take
  them; its upper sides take none: `place_ramp` can put a cliff piece there that joins the ground but looks broken.
  Render each ramp close and look at it.
- With a half turn, the image of a ramp on a lower side is on an upper side. So put the ramps of a turned map on
  straight edges: flatten a diamond's top and bottom points with a small `rect`, and give `place_ramp` the ramp of that
  edge (`doodads`; Jungle: 293 at a bottom edge, 284 at a top edge).
- A terrain edit near a ramp can remove it (`doodads_removed`). Paint first, then place the ramps.
- Then `check`, or `get <map> pathing`, and look at a close `render` of the ramp.

## A good melee map

A melee map is player against player: it must be fair, so most are symmetric. The numbers are from the 66 ladder maps
of 2017-2024 (the 10th to the 90th percentile; 2-player maps where they are different), and the staredit.net Melee
Mapping Guide (staredit.net/topic/378).

| Measure | Ladder maps |
| --- | --- |
| Size | 4 players: 128x128. 2 players: 96-128 x 112-128, not 128x128 |
| Symmetry | a half turn (37 of 66), a mirror (20) |
| Bases for each player | 2 players: 5-6. 4 players: 3-5 |
| Main | 9 fields (8-10), 1 geyser, 1500 a field, 5000 a geyser; 680-1150 tiles of high ground, 1 ramp |
| Start location to the map's edge | 240-320 px; the fields at the side away from the map's center |
| Start location to its ramp (ground) | 590-1000 px; the ramp 64-136 px wide |
| Natural | 916-1134 px (ground) from the start location; 7 fields, 1 geyser; lower than the main |
| Natural's choke | 128-320 px wide |
| Third | 1630-2490 px (ground) |
| Start location to start location | ground: 2 players 4550-5950 px, 4 players 4100-4740 px; air: 3360-4650 px |
| Other bases | mostly 7 fields (then 8, 6), 1 geyser; 1 in 5 has no gas |
| Fairness | each player's natural and third at the same ground distance (difference less than 150 px) |

- Use `place_base`: `{"op": "place_base", "x": 288, "y": 400, "side": "left", "minerals": 9, "player": 1}`. (x, y)
  is the town hall's center. At the left or the right, the fields make the arc of the ladder maps' mains, and the
  geyser goes 176 px above the town hall. With symmetry, each mirror image is a base of its own, the same way up (as
  on the ladder maps), and its start location goes to the next player.
- Fields at the left or the right of the town hall. Above it, the first trips take 1-2 s longer; below it, new workers
  get stuck on the miners. `place_base` warns about both. A geyser above the town hall mines faster than one below it:
  "balanced gas > symmetrical gas".
- `get <map> distances`: the ground distance from each start location to each other start location and each base.
  `get <map> walk`: the ground, one character per minitile, to measure areas and chokes.
- Room for a town hall at each base (`decorate` keeps the bases clear; with `clear_bases: false` it does not). Keep
  the ramps and the chokes clear too (`keep_clear`).
- Resources and placed sprites belong to player 12. Each start location to its own player.

### Looks

The ladder maps (Jungle, the median): 7 terrains, the largest 34 % of the map; about 6 patches of terrain for each
1000 tiles; cliffs half along the axes and half along the tiles' 2:1 diagonals; 14 doodads and 3.5 sprites for each
1000 tiles.

- Put patches of the other terrains of the same level on the ground (`paint_terrain`, a circle brush): grass, mud and
  rocks on the dirt, grass on the high ground. Keep them away from cliffs and shores, or the cliffs move. Do it before
  the ramps.
- Use `paint_area` with shape `diamond` for some cliffs and water: its edges are the 2:1 diagonals.
- Doodads: mostly small ground details. `decorate` with `doodads` (the 2x1 and 2x2 doodads of the ground's groups,
  from `get <map> doodad-types`) and `level` 3 (the tiles between them), then a few bigger ones.
- Sprites: few, delicately. On the ladder maps most sprites are on ground that is not walkable, at the edge of a
  cliff or a shore, one by one; a sprite in the open looks like an obstacle. Use `place_sprite`.
- `get <map> terrain --step 32` gives the terrain of each tile.

### Terrain with symmetry

- Put each painted area on the grid of the terrain's diamonds: x a multiple of 128, y a multiple of 64. Else the two
  sides of a turned map do not come out the same.
- A cliff that faces up is lower than one that faces down. So the two sides differ where a shore or a cliff meets a
  cliff: ground that no unit can get to stays there. `{"op": "fix_pockets"}` makes such ground not walkable;
  `"closed_passages": true` also small closed passages, where it is safe. Then `get <map> pathing` must show no
  pockets. Fix the other problems by hand.
- `decorate` can make a pocket at a cliff: if `get <map> pathing` shows a problem after it, `undo` and use the next
  `seed`.
- Jungle: a ramp needs High Dirt beside Dirt. Paint Dirt, then High Dirt on it, then `place_ramp`.

## EUD projects

`project-new <parent> <name> <map>` makes a project. `project`, `project-apply` (scripts, flags), `project-triggers`
and `project-triggers-apply` (the EUD triggers), `project-data` and `project-data-apply` (the .dat values, in the
units the editor shows), `check <project>`, then `project-build <project>`. A failed build gives `script_error`
(the .eps file and line) or `trigger_problems`.

## Feedback

When stardraft-cli does not do what the user needs, or does it wrong, tell the user. With their consent, send it:
`stardraft-cli feedback "<what you did, what happened, what you expected>"`. It becomes a GitHub issue for the
developers. Do not put personal data, maps or file contents in it. A text that starts with "-" goes after `--`
(`stardraft-cli feedback -- "--anyway ..."`), or in a file (`--file feedback.txt`).
