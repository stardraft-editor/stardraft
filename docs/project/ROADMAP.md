# Roadmap

The features that StarDraft 2 does not have yet. The simplest items are first in each section.
The *Tag:* of each item names its area: EUD, Terrain, UMS (triggers without EUD), UI (the user
interface and experience), Capability (a new thing StarDraft 2 can do), Mods or Platform.

Each item has a title that names the feature, then one or two short sentences that tell what the feature is. The
website (https://stardraft-editor.github.io, made by build.py in stardraft-editor/stardraft-editor.github.io) shows
only these, and the sentence under each section heading. The next paragraph tells the problem that the feature solves,
and other notes. A push of this file updates the website (the Roadmap workflow).

## Now

Accelerated improvements, and the missing features based on early user feedback.

- [ ] **R36 Clickable build errors.** *Tag:* EUD.
  Click a build error in the Log to open its trigger and block in the Triggers workspace.

  euddraft shows an error in the Log with a line of the triggers' code. Mappers must find the trigger by hand.

  Status (2026-10-14): most of it exists. A failed build selects the block of the error (trigShowBuildError), and the
  epTrace rows open their lines. Only a click on a Log line is missing.
- [ ] **R39 Tiles in the map's graphics.** *Tag:* Terrain.
  The Tiles list shows each tile in the graphics of the map: HD, SD or Carbot.

  The Tiles list shows the tiles in SD, also when the map shows HD or Carbot. Mappers cannot find a tile by the look
  that the map shows.

  Status (2026-10-14): the Tiles list draws each tile in SD (tilesPixels in tiles_view.inc). The HD and Carbot tiles
  load already for the terrain palette (loadHdTiles); use them here.
- [ ] **R8 Preplaced unit graphics.** *Tag:* EUD.
  Preplaced units show the graphics from the map's data edits.

  The game makes preplaced units before the map's data edits run. These units keep the game's graphics. Make them
  again at map start.

  Status (2026-10-14): built, not pushed. The Project page option "New graphics for preplaced units" makes the units
  again at map start and shows them with the new graphics on the map. Open questions:
  - Check in the game that the units are made again with the new graphics, on their places, with their values.
  - Units that a new unit cannot copy keep the old graphics: cloaked, burrowed, lifted, loaded, hallucinations, units
    linked to an add-on or a Nydus Canal, and units with interceptors or scarabs. Is that acceptable?
- [ ] **R2 Terrain rendering accuracy.** *Tag:* Terrain.
  All tilesets look the same in StarDraft 2 as in the game, in HD, SD and Carbot.

  Some terrain looks different in StarDraft 2 and in the game. Not all tilesets are compared in HD, SD and Carbot.
  Compare each tileset with the game, and fix what differs.

  Status (2026-10-14): the automatic checks pass (terrain_check, terrain_render_test), but no check compares with the
  game. Open questions:
  - Water and lava do not move: the game animates them with palette cycling, and StarDraft 2 has none.
  - In Carbot, Twilight shows a large magenta area where SD and HD2 show a black void.
  - Ashworld has no seam test, and terrain_render_test uses 2 maps only.
- [ ] **R50 More EUD trigger blocks.** *Tag:* EUD.
  Blocks for common mapping tasks: center the screen, queue buildings, play animations, place sprites, and sprite
  effects.

  Mappers must write epScript for these tasks, because no block does them yet.

  Open question (2026-10-14): "center the screen" exists (act.centerview), and R53 Camera controls covers the camera.
  Take it out of this item?

## Next

Make StarDraft 2 a more powerful creation suite.

- [ ] **R56 Signed program.** *Tag:* Platform.
  StarDraft 2 has a code-signing certificate, so that Windows and antivirus programs trust it.

  Windows shows a warning when users start StarDraft 2 for the first time, and some antivirus programs stop it. Users
  cannot see that the program comes from the StarDraft 2 team.
- [ ] **R55 Stacked units.** *Tag:* Capability.
  Place units on top of each other, as in SCMDraft 2, and see a list of all the units under the cursor.

  Mappers use stacked units for dense mineral fields and for buildings that attack very fast. StarDraft 2 does not
  let units overlap, and a click selects only the top unit.
- [ ] **R37 Terrain decoration tools.** *Tag:* Terrain.
  Tools that put doodads and decoration on the terrain, such as a doodad spray that puts doodads only where they fit.

  The generator makes only terrain. Mappers must place doodads and decoration by hand.
- [ ] **R40 Floating palettes.** *Tag:* UI.
  Open each palette (terrain, tiles, units, doodads, sprites) as a floating window. Open several at once, each with
  its own filter.

  The palettes are fixed beside the map, and only one of each shows. Mappers with a large screen or two screens
  cannot put them where they want.
- [ ] **R38 Checker documentation.** *Tag:* EUD.
  Help for each EUD and desync check: the rule, an example, and what the check does not find. Each mark in the
  Triggers workspace links to its help.

  The Triggers workspace marks EUD errors and desync risks, but mappers cannot see what the checkers look at and what
  they cannot find.
- [ ] **R54 Built-in help articles.** *Tag:* UI.
  Help articles in a sidebar of StarDraft 2, which are also on the website. Each article has a feedback button and
  names its sources. The first articles explain the fields of the EUD Book.

  Mappers must find help on many websites, and the program does not tell what a value does in the game (for example,
  how build time changes to seconds). Much good help exists in the community (SCRMapDocs, the EUD Book, the euddraft
  wiki, Staredit.net): link to it, with the permission of the authors.
- [ ] **R33 Unit range overlay.** *Tag:* Capability.
  Show the weapon ranges, sight, detection and psi fields of the selected units on the map.

  The map does not show the ranges of units. Mappers must find the values in the data.
- [ ] **R34 Terrain flag overlays.** *Tag:* Terrain.
  Show on the map which terrain is walkable, how high it is, and which terrain gives cover or blocks the line of
  sight.

  The map shows terrain only as pictures. The overlays come from the tileset flags (CV5, VF4).
- [ ] **R32 Fog of war preview.** *Tag:* Capability.
  Select a player, and see the fog of war of that player at the start of the game.

  Mappers cannot see what each player sees at the start of the game.
- [ ] **R53 Camera controls.** *Tag:* EUD.
  Trigger blocks that move a player's camera: center the view on a location or a unit, follow a unit, pan, and shake
  the screen.

  Mappers must make camera effects by hand. The blocks use the game's Center View action: they move a location, then
  center the view on it.
- [ ] **R58 EUD terrain info.** *Tag:* EUD.
  Trigger blocks that read the terrain tile at a location or at a point.

  No trigger reads the terrain. Mappers must find the tile in the game's memory by hand, for example to make units
  that act on the terrain under them.
- [ ] **R14 Exact timers.** *Tag:* EUD.
  "After N seconds" and "Every N seconds" blocks that stay exact over a long game.

  Wait stops all the triggers of a player. The timer blocks drift over a long game. Paused.
- [ ] **R23 Batch trigger generation.** *Tag:* UMS.
  In Classic triggers, make many triggers from one pattern: one trigger for each player, location or unit type.

  Mappers make hundreds of triggers that differ in one value. They must make and change each trigger by hand.

  Status (2026-10-14): built, not pushed. Batches are blocks of actions in a trigger ("For each unit in ..."), which
  can nest; the Classic triggers page has a new layout (search, trigger header, Turn off for triggers, blocks in the
  action list, pop-outs, right-click menus). Open questions:
  - The text above says one trigger for each value. Change it to batch blocks (and the Korean text on the website)?
  - Check in the game: a trigger with more than 64 actions becomes several triggers, linked by a switch. Does it work
    for a trigger of several players?
  - The rail button says "Triggers". Keep it, or another name?
  - Ctrl+wheel zoom on the page, as in the Triggers workspace?
  - Players as check boxes (now) or as chips?
  - The value pop-out's button says "Set". Keep it, or use the action's name?
  - Compare the sentences ("Set Switch 2", the quotes around locations) with the game's words.
  - Help for the selected action, as the info pane of the Triggers workspace.
  - Select several conditions and actions (now only triggers).
- [ ] **R59 Classic trigger organization.** *Tag:* UMS.
  Tools that keep Classic triggers in order: quick find, search, and comments that put the triggers into groups.

  A map can have hundreds of Classic triggers in one long list. Mappers cannot find a trigger fast, and they cannot
  put notes or headings between the triggers to show their structure.
- [ ] **R60 Trigger search.** *Tag:* Capability.
  A search pop-up in the Triggers workspace and on the Classic triggers page, as Quick open: type words, and it lists
  the triggers, conditions, actions and blocks whose text or comments contain them, each with the trigger it is in.
  Select one to go to it: the editor shows and selects it. The list of triggers stays as it is (no filter).

  Large maps have many triggers. Mappers cannot find the trigger that changes a value or uses a unit fast. Classic
  triggers can be searched by their text since 0.5.0.8, but not by comments, and the Triggers workspace has no search.
- [ ] **R61 Quick add.** *Tag:* Capability.
  Press Q in the Triggers workspace or on the Classic triggers page: a search box opens, and the block, condition or
  action you select goes in after the selected one. On the map, Q opens the quick search already.

  To add a block, mappers now go through the Add menu and its groups. Mappers who know the name of the block they need
  want to type it and keep their hands on the keyboard.
- [ ] **R62 Trigger comments.** *Tag:* Capability.
  Comments are fast to write in the Triggers workspace and on the Classic triggers page. Each trigger has a comment in
  its header: click it and type. A comment can also go between any conditions or actions, as a row of its own.
  Search (R60) finds the comments.

  Comments tell other map makers, and the author months later, what a trigger does. The Triggers workspace has a
  Comment action, but it is one more block to find in the Add menu, and it cannot go among the conditions.
  Open question: Classic triggers keep a trigger's comment in the game's Comment action; where do comments between
  conditions go (the project, or the map)?
- [ ] **R25 Campaign projects.** *Tag:* UMS.
  Keep all the maps of a campaign in one project, in their order.

  Each map of a campaign is a separate file. The settings drift between the maps, and mappers cannot see the
  campaign in one place.
- [ ] **R35 Pathfinder region overlay.** *Tag:* Terrain.
  Show the regions that units use to find their paths, to see why units take a path or get stuck.

  The game makes these regions when it loads a map, and mappers cannot see them. Find how the game makes the regions
  (OpenBW may have the code).
- [ ] **R31 Replay preview.** *Tag:* Capability.
  Make a replay of the map with computer players, and watch it in StarCraft.

  To see a map in the game, a mapper must start a game and play it.
- [x] **R44 Unit replace.** *Tag:* Capability.
  Replace all units of one type with a different type, in the map or in the selection.

  Mappers must delete each unit and place the new unit by hand.
- [ ] **R42 Map import from scmscx.com.** *Tag:* Capability.
  Find a map on [scmscx.com](https://scmscx.com), and open it in StarDraft 2.

  To edit a map from scmscx.com, mappers must download it and then open the file.
- [ ] **R49 Revamped terrain generation.** *Tag:* Terrain.
  Generate maps with a constraint-based method, such as wave function collapse: set rules for the terrain, and get
  maps that obey them.

  The generator makes terrain from amounts and a shape. Mappers cannot give it rules, such as where bases, paths and
  high ground must be.
- [ ] **R52 Compatibility import layer.** *Tag:* Capability.
  Import the projects of other map and EUD editors, such as EUD Editor 2, EUD Editor 3 and euddraft projects, as
  StarDraft 2 projects.

  Mappers who use other programs must make their work again to move to StarDraft 2. The import reads their data
  edits, triggers and scripts, and tells what it could not import.
- [ ] **R51 Advanced EUD capabilities.** *Tag:* EUD.
  Blocks for queues and deques (with their methods and loops), byte readers and writers, text compare and search for
  chat commands, text to number, drawing, and background music.

  These need epScript today: EUDQueue and EUDDeque with their methods and loops, EUDByteReader and EUDByteWriter,
  strcmp, strnstr and memcmp (mainly to parse chat commands), f_parse (text to a number), drawing, and BGM.

## Later

Exploring how to enable the next evolution of StarCraft creations.

- [ ] **R57 Custom keyboard shortcuts.** *Tag:* UI.
  A menu where users change the keyboard shortcuts of StarDraft 2.

  The keyboard shortcuts are fixed. Users who come from other editors cannot use the keys that they know.
- [ ] **R26 Remastered mod support.** *Tag:* Mods.
  Load a StarCraft: Remastered mod (MPQ or folder) on top of the game's files, and make maps with it.

  StarDraft 2 reads only the game's own files. Mappers cannot use the files of a mod. Modders are not a group in
  README.md yet.
- [ ] **R41 Game simulation.** *Tag:* Capability.
  Simulate the game in StarDraft 2, without StarCraft. Move units and tell them to stop or attack, and see who wins a
  fight. See the resources, the supply and the unit counts change over time, with a log of each event and its time.

  To balance custom units and economies, mappers must play test games in StarCraft. It needs a simulation of the game
  itself, so it waits for that.
- [ ] **R45 Map publishing.** *Tag:* Capability.
  Upload a map to [scmscx.com](https://scmscx.com) from StarDraft 2.

  To share a map, mappers must save it and upload it on the website.
- [ ] **R46 1.16.1 mod support.** *Tag:* Mods.
  Load a StarCraft 1.16.1 mod, such as [Cosmonarchy](https://cosmonarchy.com/), and make maps with it.

  StarDraft 2 reads only the files of StarCraft: Remastered. The largest mod, Cosmonarchy, has needs of its own (R27
  was its own item). These needs are not known yet.
- [ ] **R47 Version history.** *Tag:* Capability.
  Compare two versions of a map or project, and merge the changes.

  Mappers cannot see what changed between two versions of a map.
- [ ] **R48 Map templates.** *Tag:* UMS.
  Start a map from a template, such as a defense map or an RPG, with its layout and triggers.

  Each new map starts empty. Mappers make the same layout and triggers again for each map.
- [ ] **R43 Collaborative editing.** *Tag:* Capability.
  Two or more people edit the same map or project at the same time.

  Only one person can edit a map at a time. Teams send files to each other.
- [ ] **R28 macOS port.** *Tag:* Platform.
  StarDraft 2 on macOS.

  StarDraft 2 runs only on Windows.
- [ ] **R29 Linux port.** *Tag:* Platform.
  StarDraft 2 on Linux.

  StarDraft 2 runs only on Windows.
- [ ] **R30 Web build.** *Tag:* Platform.
  StarDraft 2 in the browser, with no installation.

  Users must install StarDraft 2 to use it.
