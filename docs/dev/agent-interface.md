# An interface for agents (parked idea)

State: an idea for later (2026-10-17). Nothing of it is made yet.

## The goal

An AI agent (or a script) can do everything that a user can do in StarDraft 2, on Windows, macOS and Linux, with the
same rules as the UI: the same checks, the same undo steps, the same saved files.

## The design

Use the pattern of the Google Docs API, not the UI:

- **Get**: the open map, project or trigger set as JSON: the units, doodads, sprites, locations, a summary of the
  terrain, the players and forces, the triggers, the briefings, the Data edits, the project's scripts.
- **Batch update**: a list of named operations that apply together as one undo step, for example
  `place_unit {type, player, x, y}`, `move_units {ids, dx, dy}`, `paint_terrain {terrain, points, brush}`,
  `set_location {id, left, top, right, bottom}`, `set_dat_field {table, entry, field, value}`,
  `add_trigger_block {trigger, block, args}`, `set_player {player, controller, race, force, color}`, `save {path}`,
  `build`. Each operation names its target by id and its positions in map units: no mouse and no screen pixels.
- **Concurrency**: an update can give the change number that it expects; when the document changed in between, the
  update is refused.
- **One layer for the UI and the agents**: the UI's edits go through the same named operations. A click and an agent's
  operation then have the same checks, undo and results.
- **The transport**: `stardraft --serve` reads one JSON request for each line on stdin and writes one JSON answer for
  each line on stdout. A small MCP server (Python, in tools/) starts it and gives its operations to agents as tools.
  No window and no GPU: the map and project library (src/, include/stardraft/) runs without them.

## What is there already

- The library has about 200 edit functions with no UI (`include/stardraft/editor.h`, `triggers.h`, `dat_edit.h`,
  `dat_extras.h`, `eud_project.h`, `euddraft.h`). Tests use them with no window (`tools/tests/editor_test.cpp`,
  `save_check.cpp`, `trigger_test.cpp`, `dat_test.cpp`).
- The command layer: about 130 command ids (`apps/editor/core/ids.inc`), 21 of them with names
  (`registerMapCommands` in `core/map_commands.inc`), and the gray rules (`sdlEnabled` in `sdl_menus.inc`).
- The snapshot test harness (`main_sdl.cpp`, `sdl_menus.inc`, `sdl_shot_state.inc`, `sdl_soak.inc`,
  `tools/sdl_snapshots.py`) drives the real UI with keys, clicks, commands and dialog answers, and prints the state
  after each input. It is a good test aid for the new layer, but it is one-shot, its state is text, and it needs a GPU.

## The work

1. The operation layer: the list of operations for each area (map objects, terrain, locations, players and forces,
   map properties, triggers, classic triggers, briefings, Data tables, strings, buttons, requirements, the project,
   Build), each a function on the library or the core with one undo step. Some edits are applied today only in page
   code of the SDL program (`sdl_map_pages.inc`, `sdl_briefing.inc`, `sdl_data.inc`): move them into the core first.
2. Get: the JSON form of each document, with stable ids for units, doodads, sprites, locations and triggers.
3. `--serve`: the request loop, the JSON answers, the errors, and the wait for work that runs on a thread (Build, the
   pathing analysis).
4. The MCP server and its tool list.
5. Move the UI's edits onto the operations, area by area, and test that the UI and an operation give the same map.

Size, rough: a first version (get, save, the map objects, locations, terrain, players, `--serve` and MCP) is about a
day of work; all the areas are a few days.

## Risks

- The program's state is one session with globals (`gState`, `gDocs`, `gActive`, `gWorkspace`, `gTE`, the Data
  state). The operation layer must work on a document by its id, not on what the UI shows.
- Some commands mean different things in each workspace and mode (Paste in the map waits for a click). The operations
  must be explicit.
- Game data: the library needs a StarCraft install (CASC) to open maps.
