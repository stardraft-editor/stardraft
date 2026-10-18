# Plan for the roadmap's Next items

The work that is left in the Next section of [ROADMAP.md](../project/ROADMAP.md), as it stood on 2026-10-18,
grouped by size. ROADMAP.md is the source for what each item is; this page tells where each one starts in the code,
what is missing, and what must be decided first. Update ROADMAP.md (and so the website) when an item is done.

## How to work through it

- The small and medium items are independent of each other: they can be done one by one, or by several agents at the
  same time (one item each), then reviewed, merged and tested together.
- Each item ends with its snapshot cases (tools/sdl_snapshots.py) or unit tests, the texts in the four translations
  (apps/editor/res/lang/*.tsv, then `python3 -I tools/ui_lang_gen.py`), and its line in ROADMAP.md marked done.
- Trigger blocks (src/data/trigger_blocks.inc, src/triggers.cpp) are checked by build/trigger_test with the game data:
  euddraft builds a map with every block. What a block does in the game needs a test in StarCraft.

## Small and medium: can start now

| Item | What exists | What is missing |
|---|---|---|
| R33 Unit range overlay | View > Show ranges of the selected units (view.ranges, map_commands.inc; rangesScene in map_scene.inc): sight, target acquisition, ground and air weapons | Detection range (detectors) and the psi field (Pylon); then mark done |
| R34 Terrain flag overlays | View > Pathing (core/pathing_layer.inc) draws an overlay on the map; the tileset's minitile flags are read (src/tileset.cpp, src/pathing.cpp) | Layers for walkable, height (low, middle, high) and cover or line of sight, from the VF4 minitile flags |
| R53 Camera controls | "Center the camera" (act.centerview) and "Center the camera on a unit" (act.camerafollow) | Pan (move the view over time) and shake; Center View moves a location, then centers on it |
| R58 EUD terrain info | Nothing | Blocks that read the tile at a location or a point: the game's tile map in memory (find and check its address), as a condition and as a value |
| R61 Quick add | Q on the map opens the quick search | Q in the Triggers workspace and on the Classic triggers page: a search box that adds the selected block, condition or action after the selected one |
| R60 Trigger search | Classic triggers have a text search (since 0.5.0.8) | A pop-up, as Quick open, in both trigger editors: matching triggers, conditions, actions, blocks and comments, each with its trigger; selecting one goes to it; the list is not filtered |
| R38 Checker documentation | Help > Documentation (docs/help/*.md, built in and on the web); the checkers mark EUD errors and desync risks | A help page per check (the rule, an example, what it does not find), and a link from each mark in the Triggers workspace |
| R32 Fog of war preview | Nothing | Pick a player; the map dims what that player does not see at the start (the units' sight at their places, as the game reveals it) |

## Medium: decide first

- **R62 Trigger comments.** A comment in each trigger's header, and comment rows among the conditions and actions.
  Decide where a comment between conditions is kept: Classic triggers keep a trigger's comment in the game's Comment
  action, but conditions have no comment. The project file, or the map (an unused trigger field)?
- **R23 Batch trigger generation.** On main; the action picker does not offer new blocks yet (kClassicBatchesOffered
  in core/views_classic_page.inc). The open questions are in ROADMAP.md (the item's text, more than 64 actions for
  several players, the rail button's name, Ctrl+wheel zoom, players as check boxes or chips, the pop-out's Set
  button, the sentences, help for the selected action, several selected conditions and actions).
- **R59 Classic trigger organization.** Quick find, search (R60) and comments that make groups (R62): do R60 and R62
  first; then decide how a group shows (a heading row that folds its triggers?).

## Large

- **R37 Terrain decoration tools.** Doodads mode has a spray already (map_input.inc). Missing: decoration that follows
  the terrain (only where a doodad fits its tiles, edges, density rules), and decoration after terrain generation.
- **R51 Advanced EUD capabilities.** Blocks for EUDQueue and EUDDeque with their methods and loops, EUDByteReader and
  EUDByteWriter, strcmp, strnstr and memcmp for chat commands, f_parse, drawing and background music.
- **R42 Map import from scmscx.com.** Search and open a map from scmscx.com. Find out what the site offers (an API or
  pages to read) and ask its owner.
- **R25 Campaign projects.** The New page lists Campaign project already, turned off (views_start.inc). A project with
  the campaign's maps in order, and copying data and terrain between them.
- **R52 Compatibility import layer.** Read EUD Editor 2 and EUD Editor 3 projects and euddraft projects (.eds and its
  files) into a StarDraft 2 project, with a list of what could not be imported.
- **R49 Revamped terrain generation.** Constraint-based generation (for example wave function collapse) with rules for
  bases, paths and high ground.
- **R31 Replay preview.** Make a replay of the map with computer players. Needs a way to run the game without a person
  (or a simulation), so it is the largest.

## Not code

- **R56 Signed program.** Needs a Windows code-signing certificate (bought, or a signing service such as Azure Trusted
  Signing); then sign in the release workflow. The macOS build has its own signing (tools/ci/mac_dist.sh).
- **R14 Exact timers.** Paused in ROADMAP.md. Note for later: "Every N seconds" (act.every in src/triggers.cpp) sets
  its next time from the current tick, so it drifts by the cycles it waits; adding the period to the last due time
  would keep it exact.

## The website

The website (stardraft-editor/stardraft-editor.github.io) is made from ROADMAP.md, so it follows the items. Its
privacy page (PRIVACY in that repository's build.py) still describes only Windows: the settings in the registry and
"the Windows version" in feedback and crash reports. Check what the macOS and Linux builds keep and send, then change
the page and its date.
