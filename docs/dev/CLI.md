# stardraft-cli

`stardraft-cli` edits maps with no window and no GPU, for scripts and AI agents. It uses the same library as the
editor (`include/stardraft/editor.h`), so an edit has the same checks: placement, terrain correction, doodad rules.
It is the first part of [the interface for agents](agent-interface.md).

Build: `cmake --build build --config Release --target stardraft_cli` (it builds also with `-DSTARDRAFT_RENDERER=OFF`).
On Windows, a release's zip has `stardraft-cli.exe` beside `stardraft.exe`, and an update of the editor also
updates it (`placeReleaseCli` in `apps/editor/core/releases.inc`). The installed program does the update, so an
update from a release without `placeReleaseCli` does not put the CLI in place: get it from the zip that time.

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
| `get <map> [part...]` | `summary` (default), `units`, `doodads`, `sprites`, `locations`, `players`, `forces`, `terrain [--step px]`, `terrains`, `unit-types`, `doodad-types`, `triggers`, `briefings`, `files`, `unit-settings`, `upgrade-settings`, `tech-settings` (`--all`: also the entries that use the game's values), `switches`, `strings`, `sounds`. |
| `apply <map> <ops> [--expect n] [--dry-run] [--label text]` | The operations from a file, from stdin (`-`) or as JSON text. All of them, or none. |
| `undo` / `redo <map> [--steps n]` | Go back or forward. |
| `render <map> -o <png>` | See below. |
| `add-file <map> <name> <file> [--remove]` | Puts a file (a sound, `sound\x.wav`) into the map, or takes one out. Not an undo step: it stays until `close`. |
| `check <map \| project>` | The checks of a save and a build, with no change: the start locations that StarCraft needs and the playability; for a project also the scripts that are not there and the triggers' checker. `passed`, `problems` (each with its `kind`), `warnings` (also: start locations that cannot reach each other by ground). |
| `save <map> [--to path] [--force] [--anyway]` | Writes .scm, .scx or .chk. Refused while start locations that StarCraft needs are missing (`--anyway` saves). |
| `close <map> [--discard]` | Removes the working folder. Refused when changes are not saved. |
| `ops` | The operations with their parameters. |
| `trigger-types` | The trigger conditions and actions with their parameters and values. |
| `project-new <folder> <name> <map>` | A new EUD project; the map is copied in. See [EUD projects](#eud-projects). |
| `project <project> [--section name]` | The project's scripts, plugins and flags; or the lines of one section. |
| `project-apply <project> <ops>` | Operations on the project file: all of them, or none. |
| `project-data <project> [table \| strings] [--entry n] [--index n]` | The Data workspace: the tables, one table's fields, one entry's values, the strings. |
| `project-data-apply <project> <ops>` | Field and string edits of the Data workspace: all of them, or none. |
| `project-build <project>` | The Build: the generated files, the map, then euddraft. See [EUD projects](#eud-projects). |
| `project-triggers <project> [--path p]` / `project-triggers-apply <project> <ops>` | The EUD triggers as JSON, and their edits. See [EUD triggers](#eud-triggers). |
| `campaign-new <folder> <name>` / `campaign <folder>` / `campaign-apply <folder> <ops>` | A campaign: its maps, their order and its name. |
| `serve` | Requests on stdin, one JSON answer per line. See [serve](#serve). |

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
size and tileset. `paint_area` paints the area with the smallest square brush, so its edges follow the diamond grid. The operations for the
switch names, the strings, the sounds, the spray, the revealers and the start locations are in the sections below.

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

## Switch names, strings and sounds

`set_switch_name` (`{"switch": 1-256, "name": text}`; `""` takes the name away) names a switch. `set_string` (`string` id,
`text`) changes one text of the map string table; `get strings` lists the texts with their ids. `get sounds` lists the
game sounds that the map plays by path. `add_sound` and `remove_sound` (`path`) change that list; a file of the map
(`add-file`) is listed there already and is not added again.

## Spray, decorate and replace

Each spray takes its random seed (`seed`), so a script gives the same result each time.

- `spray_units` (`types`, `player`, `x`, `y`, `radius`, `count`): units at random points in the circle, on valid spots.
- `spray_doodads` (`doodads`, `x`, `y`, `radius`, `count`, `check_terrain`) and `spray_sprites` (`sprites`, `player`, `x`,
  `y`, `radius`, `count`, `grid`): the same for doodads and sprites. `grid` is the snap of each sprite in px (0: none).
- `spray_terrain` (`terrain`, `x`, `y`, `brush`, `dabs`) and `reroll_terrain` (`x`, `y`, `brush`): the terrain tools.
- `decorate` (`level`: `few`, `some` or `many`, or `spacing` in tiles; `doodads`, `area`): doodads where they fit the
  terrain. Without `doodads`, the palette's doodads that take tiles are used.
- `replace_doodads` (`doodads`: indices, `to`: doodad id), `replace_sprites` (`sprites`: indices, `to`: sprite id).

## Map revealers, start locations and links

`place_map_revealers` (`players`: 1-8, `spacing` in tiles, default 8) and `remove_map_revealers`.
`place_start_locations` places the start locations that StarCraft needs. `link_units` (`units`: two serials) links two
add-ons or a Nydus pair. `save` and `project-build` refuse a map that StarCraft will not start for want of start locations
(the editor asks Place them, Save anyway or Cancel): apply `place_start_locations` first, or give `--anyway`.

## Symmetry

The options of `apply` set the symmetry of the edits that follow in the same `apply`: `"mirrors": ["left_right",
"top_bottom", "diagonal", "anti_diagonal"]` and `"turns": n` (1: none, 2: a half turn, 4: quarter turns). Each apply
starts with no symmetry. `"resource_distance": false` takes away the rule that keeps town halls and resources 3 tiles
apart (default: kept).

## EUD projects

A project is a folder with `project.stardraft`, the scripts (`.eps`) and the Data edits (`dat/`). `project-new` makes one
and copies the map in. `project` gives its name, its map, its output, its scripts (`missing` when the file is not
there), its sections and its flags (`turbo`, `freeze`, `debug`, `trace`, `remake_units`). `project <p> --section
scripts/main.eps` gives the lines of a section.

`project-apply` takes these operations: `add_script` (`path`, relative to the folder), `link_script` (`path` of a file
anywhere on disk), `remove_section` (`section`), `move_script` (`section`, `delta`: -1 or 1), `set_section` (`section`,
`lines`), `set_plugin` (`section`, `on`), `set_flags` (`freeze`, `debug`, `trace`, `remake_units`: bools) and `set_output`
(`path`). They are saved with the project file when all of them work.

`project-build <project>` runs the Build: the generated files (the triggers' code, the .dat and extras code, the remake code),
the map that euddraft gets, then euddraft. The answer has `built`, `output`, `errors`, `warnings`, `remake` and the `log`
lines. A failed build also has `script_error`: the .eps file, its module, the line and euddraft's message (a script of the
project, a linked file or a file that a script imports; the triggers' code is `generated/triggers.eps`), as the editor
selects it. When the triggers' checker finds errors, the build stops before euddraft: `built` is false and
`trigger_problems` lists them. `save` and `project-build` read the game data (the start locations check). It can take a while. The EUD triggers: see [EUD triggers](#eud-triggers).

## EUD triggers

`project-triggers <p>` gives the categories (0 Definitions, 1 On map start, 2 On every trigger cycle) with their folders,
triggers and variables. A trigger has its `conditions` and `actions`, each a block: `{"block": "act.showtext", "args":
{"text": "Hello"}, "enabled": true, "lists": [...]}`. The parameters are named as the block library names them. `--path 2.1`
gives one node. The block ids are in the block library (`src/data/trigger_blocks.inc`).

`project-triggers-apply <p> <ops>`: `add_folder` and `add_trigger` (`parent`: a category name or number, or a node path;
`name`), `add_variable` (`kind`: number, flag, per player, array, unit, constant, text, unit group), `set_node` (`path` and
the fields to change), `delete_node`, `move_node` (`path`, `parent`, `index`), `add_block` (`path`, `list`: conditions or
actions, `index`, `block`, `in`), `set_block` and `delete_block` (`path`, `list`, `index`, `in`). `in` gives the way into a
container: `[[list, index], ...]`.

## The Data workspace

`project-data <p>` lists the tables (with the number of changed values). `project-data <p> units` gives a table's
fields (their size, their entries, their names when they are a choice, their reference). `project-data <p> units
--entry 0` gives an entry's values. `project-data <p> strings [--index n]` gives the stat_txt strings (the changed ones,
or one).

The buttons, requirements and unit looks (their project files: `dat/buttons.json`, `dat/requirements.json`,
`dat/unit_display.json`):

- `project-data <p> buttons [--set n]`: the sets that changed, or one set's buttons (`condition` and `action` by name, or
  by the game's address when the name is unknown). `set_buttons` (`set`, `buttons`: `[{pos, icon, condition, action,
  condition_arg, action_arg, enabled_string, disabled_string}]`) replaces a whole set; `reset_buttons` (`set`).
- `project-data <p> requirements --kind <kind> [--entry n]`: the entries that changed, or one entry's ops. `set_requirements`
  (`kind`, `entry`, `used`, `ops`: `[{op, arg}]`; an op is a name, a number (below 256: a unit) or `"end"`); `reset_requirements`.
- `project-data <p> unit-looks [--unit n]`: the units that changed, or one unit's panel functions and wireframes.
  `set_unit_look` (`unit`, `panel` (a kind's name, which sets both functions), `status`, `display`, `wireframe`,
  `group_wireframe`, `transport_wireframe`; -1 wireframe: the unit's own); `reset_unit_look`.

A value that the Data form shows in another unit has a `shown` text as well: hit points show in whole hit points (40 for
the Marine, with decimals), as the editor does (`datShownScale` in `include/stardraft/dat_units.h`). A number given to
`set_field` is in the shown unit; `"raw": true` takes the stored value (units.dat keeps 1/256 hit point).

`project-data-apply <p> <ops>` takes `set_field` (`table`, `entry`, `field`, `value`: a number, or a choice's name),
`reset_field` (`table`, `entry`, `field`), `set_string` (`index`, `text`) and `reset_string` (`index`). A value out of
range, a bad name or a bad entry stops the command: nothing is saved. The files are the ones the editor reads
(`dat/<table>.dat`, `dat/strings.json`), so a Build of the project in the editor uses them.

## serve

`serve` reads one request for each line on stdin and writes one JSON answer for each line on stdout. A request is
`{"argv": ["get", "<map>", "units"]}`, the words of a command line (the options are words too: `["apply", "<map>",
"--label", "text"]`). An `apply` takes its operations in the request: `{"argv": ["apply", "<map>"], "ops": [...]}`. The
game data stays open between the requests, so a later request must name the same `--install` (or none) as the first one.
A request cannot read its operations from stdin (`-`): it puts them in `"ops"`. A bad request gives `{"ok": false,
"error": ...}` and the next line is read.

## Parity with the editor

The list below compares the editor's capabilities (its menus `apps/editor/sdl_menus.inc` and `core/menu_model.inc`, the
commands `core/map_commands.inc`, the pages `sdl_map_pages.inc`, `sdl_briefing.inc`, `sdl_data.inc`) with the CLI.
"Yes": the CLI does it. "Part": some of it. "No": not yet, with the reason.

| Area | Editor | CLI |
| --- | --- | --- |
| Units | place, move, delete, set hit points, shields, energy, resources, hangar; player; spray; replace type; link add-ons | Yes (`place_unit`, `move_units`, `set_units`, `spray_units`, `replace_units`, `link_units`) |
| Units: copy, paste, duplicate, cut | Yes | Yes (`copy_units`, `cut_units`, `paste_units`, `duplicate_units`); the clipboard lasts for one `apply` |
| Doodads and sprites: copy, paste | Yes | Part: doodads with `copy_terrain` (`doodads: true`); a sprite clipboard is not in the library: place the sprites again |
| Doodads | place, spray, replace, move, delete, check terrain, decorate | Yes (`place_doodad`, `move_doodad`, `delete_doodads`, `spray_doodads`, `replace_doodads`, `decorate`) |
| Sprites | place, move, delete, spray, replace | Yes (`place_sprite`, `move_sprite`, `delete_sprites`, `spray_sprites`, `replace_sprites`) |
| Terrain | paint (brush), spray, fill, replace, tiles, reroll | Yes (`paint_terrain`, `paint_area`, `fill_terrain`, `replace_terrain`, `place_tiles`, `spray_terrain`, `reroll_terrain`) |
| Terrain copy: units and doodads in a copy | options | Yes (`copy_terrain` with `units` and `doodads`; `paste_terrain`) |
| Symmetry (mirrors, turns) | Symmetry tool | Yes (apply options) |
| Locations | create, edit, name, elevations, delete | Yes (`create_location`, `set_location`, `delete_location`) |
| Start locations | place, check before save | Yes (`place_start_locations`; `save` and `project-build` refuse without them, `--anyway` saves) |
| Players and forces | controller, race, force, color, names, flags; copy all | Yes (`set_player`, `set_force`, `copy_players` from another map) |
| Map properties | name, description, size, tileset, terrain map | Yes (`set_scenario`, `resize_map`) |
| Map revealers | place, remove, show or hide | Part: place and remove; the hide flag is a view setting |
| Classic triggers | add, edit, delete, move, find | Yes (add, set, delete, move; `get triggers --find text`) |
| Briefings | edit, sounds, portraits | Yes (`set_briefings`, `add_briefing`, `add-file`) |
| Unit, upgrade, tech settings | edit per player | Yes (`set_unit_settings`, `set_upgrade_settings`, `set_tech_settings`) |
| Switch names | (the editor shows them; it has no field to set them) | Yes (`set_switch_name`), a new library function |
| Map strings | (the editor sets them through the triggers) | Part: `get strings`, `set_string` |
| Sounds | add, remove the game sounds the map plays | Yes (`add_sound`, `remove_sound`, `get sounds`); files: `add-file` |
| Undo and redo | history, jump to a step | Yes (`undo --steps`, `redo --steps`); the history: `status` |
| Render, export image | picture of the map | Yes (`render`) |
| Check that StarCraft can start the map | problems list | Part: `summary` lists the problems |
| Pathing check (nooks and crannies) | report | Yes (`get pathing`: the problems, their boxes and the pocket fixes; the editor's list and its fix on the map are the editor's) |
| Simulate mining, combat (experimental) | tools | Part: `simulate-mining`, `simulate-combat` (when built with STARDRAFT_SIMULATION) |
| Campaign: new, open, show, add a map | File menu | Yes (`campaign-new`, `campaign`, `campaign-apply`: `add_map`, `set_name`); remove a map: No (the library has no remove) |
| EUD projects: new, open, import, make from map | File and Project menus | Yes (`project-new`, `project`, `import`); "make a project from a map": `project-new` copies the map |
| Project scripts: add, link, remove, move, plugins, flags | Project page | Yes (`project-apply`) |
| Project build | Build (Ctrl+B) | Yes (`project-build`: the same steps as the editor, then euddraft; the answer has the log). The editor's questions (start locations, the playability) are the editor's; the CLI reports the playability as warnings |
| EUD triggers (blocks) | Triggers workspace | Yes (`project-triggers`, `project-triggers-apply`); the checker's problems are not listed by the CLI (the build reports them) |
| Data: unit, weapon, flingy, sprite, image and other .dat fields | Data workspace | Yes (`project-data`, `project-data-apply`; hit points in whole hit points, as the editor shows them) |
| Data: entries' names and choices | Data workspace | Part: choices by name; reference fields and flag names take numbers |
| Data: strings (stat_txt) | Data strings | Yes (`project-data-apply`: `set_string`, `reset_string`) |
| Data: buttons, requirements, unit looks | Data pages | Yes (`project-data` and `project-data-apply`: `set_buttons`, `set_requirements`, `set_unit_look`) |
| Files workspace: maps, pictures | yes | Part: `add-file`, `files` |
| Options, themes, language, help, updates, log, palette, workspace layout, zoom, grid, layers | UI | Out of scope: they are not map or project data |
| Snap rules of the cursor (tiles, half and quarter tiles) | UI | Out of scope: the CLI takes exact positions |
| Quick picker, search, drag and drop | UI | Out of scope |

Moved from the editor into the library (the editor calls the library now, with the same steps):

- `Editor::spraySprites`: the sprite spray (`core/map_edit.inc`).
- `Editor::setSwitchName`, `Editor::setMapString`: new.
- The shown hit points: `datShownScale`, `datShownText`, `datRawFromShown` (`dat_units.h`, from `core/views_data.inc` and
  `views_data_entry.inc`).
- The Build of a project: `prepareBuild`, `compileBuild`, `triggerMapInfo`, `freeUnitProperties`,
  `projectRemakeUnitTypes` (`src/project_build.cpp`, `include/stardraft/project_build.h`; from `core/project_build.inc`
  and `core/triggers_model.inc`). The editor keeps its questions, its log, its workspaces' reloads and the trigger code map.

Not done, and why:

- Sprite clipboard: the library has no sprite clipboard; a sprite is placed again with `place_sprite`.
- Campaign: remove a map (the library has no remove).
- The editor's Build questions: `project-build` refuses without start locations (`--anyway`) and reports the playability.
- The editor's GUI build was built, not run: the same steps as before, checked by the CLI build on a test project.
- Triggers from the CLI: the checker's problems are not listed (`check` needs the map's data); the build reports them.
- The remake plan and the new graphics work the same in the CLI; the CLI build was run on a project without them.
