# stardraft-cli

`stardraft-cli` edits maps with no window and no GPU, for scripts and AI agents. It uses the same library as the
editor (`include/stardraft/editor.h`), so an edit has the same checks: placement, terrain correction, doodad rules.
It is the first part of [the interface for agents](agent-interface.md).

Build: `cmake --build build --config Release --target stardraft_cli` (it builds also with `-DSTARDRAFT_RENDERER=OFF`).

## The working folder

A map is edited in a working folder next to it, `<map>.work`. The map file does not change until `save`.

```
stardraft-cli open maps/test.scx            # makes maps/test.scx.work
stardraft-cli get maps/test.scx units       # JSON
stardraft-cli apply maps/test.scx ops.json  # one undo step
stardraft-cli render maps/test.scx -o view.png --center 1600,1200 --zoom 1 --grid 128 --locations
stardraft-cli undo maps/test.scx
stardraft-cli save maps/test.scx            # or --to other.scx
stardraft-cli close maps/test.scx
```

The folder keeps the scenario after each step (`steps/NNNN.chk`) and `work.json`: the source map, the step the map
is at, and a change number for each step. `apply --expect <change>` refuses the edit when the map is at another
change (another script edited it, or an undo was done). After an undo, a new `apply` removes the redo steps.

`open` also keeps a copy of the map file in the folder. `save` writes the scenario of the current step and the map's
other files (sounds and the like) from that copy. When another program changed the map file after `open`, `save`
refuses: `save --force` writes over it, `save --to` writes another file.

## Commands

| Command | What it does |
| --- | --- |
| `open <map> [--reopen]` | Makes the working folder. `--reopen` starts again from the map file. |
| `new <map> [--size w,h] [--tileset name] [--terrain name]` | Writes a new map file (default 128x128 badlands). |
| `status <map>` | The change number, the undo and redo steps, the history. |
| `get <map> [part...]` | `summary` (default), `units`, `doodads`, `sprites`, `locations`, `players`, `forces`, `terrain [--step px]`, `terrains`, `unit-types`, `doodad-types`, `triggers`, `briefings`, `files`, `unit-settings`, `upgrade-settings`, `tech-settings` (`--all`: also the entries that use the game's values). |
| `apply <map> <ops> [--expect n] [--dry-run] [--label text]` | The operations from a file, from stdin (`-`) or as JSON text. All of them, or none. |
| `undo` / `redo <map> [--steps n]` | Go back or forward. |
| `render <map> -o <png>` | See below. |
| `add-file <map> <name> <file> [--remove]` | Puts a file (a sound, `sound\x.wav`) into the map, or takes one out. Not an undo step: it stays until `close`. |
| `save <map> [--to path] [--force]` | Writes .scm, .scx or .chk. |
| `close <map> [--discard]` | Removes the working folder. Refused when changes are not saved. |
| `ops` | The operations with their parameters. |
| `trigger-types` | The trigger conditions and actions with their parameters and values. |

`<map>` is the map file or its working folder. Positions are map pixels (a tile is 32 px). Players are 1-12 (12:
neutral), forces 1-4, locations by their number. Units are named by their serial (`"units": [...]`, from `get
units`); on a map whose units share a serial, by their index (`"unit_indices": [...]`). Doodads and sprites are named
by their index. Unit types and terrains can be given by id or by name.

The StarCraft install: `--install <folder>`, else `STARDRAFT_INSTALL`, else the editor's setting (StarCraftFolder),
else the usual folders. `--timing` writes the time of each step on stderr.

Each command writes one JSON object on stdout: `{"ok": true, ...}`, or `{"ok": false, "error": "..."}` with exit
code 1.

## Operations

```json
{"options": {"units_anywhere": false, "stacking": false},
 "ops": [
  {"op": "place_unit", "type": "Terran Command Center", "player": 1, "x": 1600, "y": 1300},
  {"op": "paint_area", "terrain": "High Dirt", "area": [1800, 1000, 2300, 1400]},
  {"op": "create_location", "name": "Base", "area": [1400, 1100, 1800, 1450]},
  {"op": "set_player", "player": 2, "controller": "computer", "race": "zerg"}
 ]}
```

A plain list of operations is also accepted. `stardraft-cli ops` lists them all: units (place, move, delete, set,
replace), doodads, sprites, terrain (`paint_terrain` with brush points, `paint_area`, `fill_terrain`,
`replace_terrain`, `place_tiles`), locations, players, forces, the scenario's name and description, and the map's
size and tileset. `paint_area` paints the area with the smallest square brush, so its edges follow the diamond grid.

## Triggers

`add_trigger`, `add_triggers` (many in one op), `set_trigger`, `delete_trigger` and `move_trigger` edit the map's
classic triggers. A trigger is the groups that run it, its conditions and its actions:

```json
{"op": "add_trigger", "trigger": {
  "players": ["Force 1"],
  "conditions": [{"type": "Bring", "player": "Current player", "cmp": "at least", "n": 1, "unit": "men", "location": "Shop: Heal"},
                 {"type": "Accumulate", "player": "Current player", "cmp": "at least", "n": 50, "resource": "minerals"}],
  "actions": [{"type": "Set resources", "player": "Current player", "modifier": "subtract", "n": 50, "resource": "minerals"},
              {"type": "Display text message", "text": "Your units are healed."},
              {"type": "Preserve trigger"}]}}
```

`stardraft-cli trigger-types` lists each condition and action with its parameters and their values. A player is 1-12
or a group ("Current player", "All players", "Force 1"); a unit type is an id or a name, or "men", "buildings", "any
unit"; a location is a number or a name; a switch is 1-256; a text is the text (it becomes a map string). A parameter
that is not given has StarEdit's default value. No conditions: Always. Play WAV and Transmission also take
`wav_time`, the sound's length in ms. `flags` (the game's bits) is given only where it differs from StarEdit's
default. EUD conditions and actions show `"eud": true` and cannot be edited yet.

`get triggers` gives each trigger in the same form, with a `sentence` for each condition and action: an edited
trigger can be given back to `set_trigger` as it is.

## Briefings

`set_briefings` replaces the mission briefing, `add_briefing` adds a briefing trigger. The action types are `wait`,
`play_wav`, `text`, `objectives`, `show_portrait`, `hide_portrait`, `speaking_portrait`, `transmission` and
`skip_tutorial`, with `text`, `time` (ms), `wav` and `wav_time`, `slot` (1-4), `unit`, `modifier` and `disabled`.
`add-file` puts a sound file into the map. `get briefings` gives the same form.

## Unit, upgrade and tech settings

`set_unit_settings` (hit points, shields, armor, build time, cost, the unit's name in the map, who can build it),
`set_upgrade_settings` (costs, the max and start level) and `set_tech_settings` (costs, energy, available and
researched). Per player: `"players": {"1": ..., "4": ...}`; `null` gives the player the default again. A value that is
given makes the entry stop using the game's values; `"use_default": true` uses them again.

## Render

A picture of the map or of an area: the terrain, doodads, sprites and units (SD graphics).

- `--area l,t,r,b`: the area in map pixels. Default: the whole map.
- `--center x,y` with `--size w,h` (default 1024,768) and `--zoom z`: a view as in the editor.
- `--zoom z` (to 8), or `--size w,h` to fit the area. Default: the whole map in at most 1024 px.
- `--grid px`: grid lines with their coordinates. `--locations`: the locations' boxes and numbers.
- `--no-units`, `--no-sprites`.

The output gives the area and the zoom: picture pixel (px, py) is map pixel (left + px / zoom, top + py / zoom).

## Not yet

The Data workspace, projects and EUD triggers; switch names. See [agent-interface.md](agent-interface.md).
