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
- Put the operations in a file (`ops.json`), not in a command line: the shell can change backslashes and quotes.
- For many requests, `stardraft-cli serve` keeps the game data open: one JSON request for each line on stdin.

## A first map

```
stardraft-cli new duel.scx --size 96,96 --tileset jungle --terrain Jungle
stardraft-cli open duel.scx
```

ops.json:

```json
[{"op": "set_scenario", "name": "Duel", "description": "A 1v1 map"},
 {"op": "paint_area", "terrain": "Water", "area": [1216, 1216, 1856, 1856], "shape": "ellipse"},
 {"op": "place_unit", "type": "Start Location", "player": 1, "x": 400, "y": 400},
 {"op": "place_unit", "type": 176, "player": 12, "x": 140, "y": 400},
 {"op": "place_unit", "type": "Vespene Geyser", "player": 12, "x": 460, "y": 120},
 {"op": "place_unit", "type": "Start Location", "player": 2, "x": 2672, "y": 2672},
 {"op": "decorate", "level": "some"}]
```

```
stardraft-cli apply duel.scx ops.json --label "Layout"
stardraft-cli check duel.scx
stardraft-cli render duel.scx -o duel.png --grid 512
stardraft-cli save duel.scx
```

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
