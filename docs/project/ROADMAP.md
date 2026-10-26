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
- [x] **R39 Tiles in the map's graphics.** *Tag:* Terrain.
  The Tiles list shows each tile in the graphics of the map: HD, SD or Carbot.

  The Tiles list shows the tiles in SD, also when the map shows HD or Carbot. Mappers cannot find a tile by the look
  that the map shows.

  Done (2026-10-18): the Tiles grid draws each tile in the map's art, SD, HD or Carbot (tilesPixels in views_tiles.inc).
- [x] **R8 Preplaced unit graphics.** *Tag:* EUD.
  Preplaced units show the graphics from the map's data edits.

  The game makes preplaced units before the map's data edits run. These units keep the game's graphics. Make them
  again at map start.

  Done (2026-10-24): released in 0.7. Units of a type above 227 and units on or past the map's edge also stay in the
  map with the old graphics (Create Unit cannot make them as the map does).

  Status (2026-10-23): on main. The Project page option "New graphics for preplaced units" shows the units with the new
  graphics on the map. Build takes these units out of the map that euddraft gets (the project's map does not change),
  and the map's code makes them at the start, in their order, from the map's data: owner, place, hit points, shields,
  energy, resources, hangar, and the states cloaked, burrowed, lifted, hallucination and invincible (one unit property
  for each different set of values). Units in a transport and units linked to an add-on or a Nydus Canal stay in the
  map with the old graphics. Open questions:
  - Check in the game that the units are made with the new graphics, on their places, with their values (also cloaked,
    burrowed, lifted, hallucinations and hangars).
  - Units that the game cannot put on their place (on other units) move near it. Is that acceptable?
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
  Take it out of this item? The other tasks have no block yet (2026-10-18).

## Next

Make StarDraft 2 a more powerful creation suite.

- [x] **R55 Stacked units.** *Tag:* Capability.
  Place units on top of each other, as in SCMDraft 2, and see a list of all the units under the cursor.

  Mappers use stacked units for dense mineral fields and for buildings that attack very fast. StarDraft 2 does not
  let units overlap, and a click selects only the top unit.

  Done: Options > Units > Can be on top of others; the status bar counts the units under the mouse, and Alt+click
  (Option+click on macOS) lists them to pick one.
- [x] **R37 Terrain decoration tools.** *Tag:* Terrain.
  Tools that put doodads and decoration on the terrain, such as a doodad spray that puts doodads only where they fit.

  The generator makes only terrain. Mappers must place doodads and decoration by hand.

  Done (2026-10-18): Edit > Decorate the terrain puts a few, some or many doodads in the terrain selection (else
  the whole map), each where it fits its tiles and on no other doodad; the doodads chosen in the Doodads palette, else
  all of the tileset's. One undo step. The New page's generator has Decorate with doodads.
- [x] **R40 Floating palettes.** *Tag:* UI.
  Open each palette (terrain, tiles, units, doodads, sprites) as a floating window. Open several at once, each with
  its own filter.

  The palettes are fixed beside the map, and only one of each shows. Mappers with a large screen or two screens
  cannot put them where they want.

  Done (2026-10-18): a mode's whole panel (its tools, its options and its list) pops out into a floating panel, and
  more panels of a palette open, each with its own type, filter, search and sort. Panels belong to their map's tab and
  are kept per map.
- [x] **R38 Checker documentation.** *Tag:* EUD.
  Help for each EUD and desync check: the rule, an example, and what the check does not find. Each mark in the
  Triggers workspace links to its help.

  The Triggers workspace marks EUD errors and desync risks, but mappers cannot see what the checkers look at and what
  they cannot find.

  Done (2026-10-18): the help page Trigger checks has each check's rule, an example and what it does not find. A click
  on a mark in the Triggers workspace, or F1 there, opens the check's part of the page.
- [x] **R54 Built-in help articles.** *Tag:* UI.
  Help articles in a sidebar of StarDraft 2, which are also on the website. Each article has a feedback button and
  names its sources. The first articles explain the fields of the EUD Book.

  Mappers must find help on many websites, and the program does not tell what a value does in the game (for example,
  how build time changes to seconds). Much good help exists in the community (SCRMapDocs, the EUD Book, the euddraft
  wiki, Staredit.net): link to it, with the permission of the authors.

  Done: Help > Documentation (F1) has the pages in a side bar with a search, also on the web: Getting started, Values
  and units, and a page for each .dat file's fields, with sources and Send feedback about this page. F1 in the Data
  workspace opens the field's page. More articles (the checkers: R38) can follow.
- [x] **R33 Unit range overlay.** *Tag:* Capability.
  Show the weapon ranges, sight, detection and psi fields of the selected units on the map.

  The map does not show the ranges of units. Mappers must find the values in the data.

  Done (2026-10-18): View > Overlays > Show ranges of the selected units shows sight, target acquisition, the ground
  and air weapon ranges, the detection range of detectors and the psi field of a Pylon.
- [x] **R34 Terrain flag overlays.** *Tag:* Terrain.
  Show on the map which terrain is walkable, how high it is, and which terrain gives cover or blocks the line of
  sight.

  The map shows terrain only as pictures. The overlays come from the tileset flags (CV5, VF4).

  Done (2026-10-18): View > Overlays has Walkable ground, Ground height (low, middle, high), Ground that blocks the line
  of sight and Buildable ground.
- [x] **R32 Fog of war preview.** *Tag:* Capability.
  Select a player, and see the fog of war of that player at the start of the game.

  Mappers cannot see what each player sees at the start of the game.

  Done (2026-10-18): View > Overlays > Fog of war of the selected player darkens the map where that player sees
  nothing when the game starts: the sight of its units, and of its force's units when the force has Shared vision.
  Ground units do not see higher ground.
- [x] **R53 Camera controls.** *Tag:* EUD.
  Trigger blocks that move a player's camera: center the view on a location or a unit, follow a unit, pan, and shake
  the screen.

  Mappers must make camera effects by hand. The blocks use the game's Center View action: they move a location, then
  center the view on it.

  Done (2026-10-18): Center the camera, Center the camera on a unit, Pan the camera (from one location to another in a
  given time) and Shake the camera (around a location). Still to check in the game: how smooth the pan and the shake
  look.
- [x] **R58 EUD terrain info.** *Tag:* EUD.
  Trigger blocks that read the terrain tile at a location or at a point.

  No trigger reads the terrain. Mappers must find the tile in the game's memory by hand, for example to make units
  that act on the terrain under them.

  Done (2026-10-18): the values The tile at a location and The tile at a point give the tile's number or its tile
  group. Still to check in the game: the memory addresses.
- [x] **R14 Exact timers.** *Tag:* EUD.
  "After N seconds" and "Every N seconds" blocks that stay exact over a long game.

  Wait stops all the triggers of a player. The timer blocks drift over a long game.

  Done (2026-10-18): Every so often counts each next time from the last due time, so it does not drift; after more
  than one interval missed (the trigger was off), it counts from then. Still to check in the game.
- [ ] **R23 Batch trigger generation.** *Tag:* UMS.
  In Classic triggers, blocks of actions that repeat for each player, location or unit type ("For each unit in ..."),
  and that can nest.

  Mappers make hundreds of triggers that differ in one value. They must make and change each trigger by hand.

  Status (2026-10-18): the blocks are made, but the action picker does not offer them for now (kClassicBatchesOffered
  is off again); a map's blocks still show and can be edited. Done with them: the Classic triggers page zooms with Ctrl+wheel, puts
  quotes around locations and switches without a name, shows help for the selected condition or action, and selects
  several conditions or actions (Ctrl+click, Shift+click) to delete them. Still to check in the game: a trigger with
  more than 64 actions becomes several triggers, linked by a switch; does it work for a trigger of several players?
- [x] **R59 Classic trigger organization.** *Tag:* UMS.
  Tools that keep Classic triggers in order: quick find, search, and comments that put the triggers into groups.

  A map can have hundreds of Classic triggers in one long list. Mappers cannot find a trigger fast, and they cannot
  put notes or headings between the triggers to show their structure.

  Done (2026-10-18): a heading (a trigger that has only a comment) folds the triggers under it, up to the next
  heading; Add a heading is in the list's right-click menu. A heading moves with its triggers.
- [x] **R60 Trigger search.** *Tag:* Capability.
  A search pop-up in the Triggers workspace and on the Classic triggers page, as Quick open: type words, and it lists
  the triggers, conditions, actions and blocks whose text or comments contain them, each with the trigger it is in.
  Select one to go to it: the editor shows and selects it. The list of triggers stays as it is (no filter).

  Large maps have many triggers. Mappers cannot find the trigger that changes a value or uses a unit fast. Classic
  triggers can be searched by their text since 0.5.0.8, but not by comments, and the Triggers workspace has no search.

  Done (2026-10-18): Edit > Find in triggers (Ctrl+Shift+F) in the Triggers workspace and on the Classic triggers
  page. A folded heading opens to show what was found.
- [x] **R61 Quick add.** *Tag:* Capability.
  Press Q in the Triggers workspace or on the Classic triggers page: a search box opens, and the block, condition or
  action you select goes in after the selected one. On the map, Q opens the quick search already.

  To add a block, mappers now go through the Add menu and its groups. Mappers who know the name of the block they need
  want to type it and keep their hands on the keyboard.

  Done (2026-10-18): Q in the Triggers workspace and on the Classic triggers page.
- [x] **R62 Trigger comments.** *Tag:* Capability.
  Comments are fast to write in the Triggers workspace and on the Classic triggers page. Each trigger has a comment in
  its header: click it and type. In the Triggers workspace, a comment can also go between actions, as a row of its
  own. Search (R60) finds the comments.

  Comments tell other map makers, and the author months later, what a trigger does. The Triggers workspace has a
  Comment action, but it is one more block to find in the Add menu, and it cannot go among the conditions.

  Done (2026-10-18): a trigger's comment in its header (Classic triggers keep it in the game's Comment action, and the
  list shows it as the trigger's title); comment rows among the actions of the Triggers workspace.
- [x] **R25 Campaign projects.** *Tag:* UMS.
  Keep all the maps of a campaign in one project, in their order.

  Each map of a campaign is a separate file. The settings drift between the maps, and mappers cannot see the
  campaign in one place.

  Done (2026-10-18), a first version: File > Campaign makes a campaign (a folder with campaign.json) or opens one. The
  Campaign window lists the maps in their order; it adds the open map or a map file, moves a map up or down, takes it
  out, opens it, and copies a map's unit, upgrade and technology settings or its terrain into the open map. The start
  page's Campaign card makes a new campaign.
- [x] **R35 Pathfinder region overlay.** *Tag:* Terrain.
  Show the regions that units use to find their paths, to see why units take a path or get stuck.

  The game makes these regions when it loads a map, and mappers cannot see them. Find how the game makes the regions
  (OpenBW may have the code).

  Done: View > Pathing shows the game's pathing regions or the connected areas, and Scenario > Check pathing lists the
  nooks and crannies where units get stuck.
- [x] **R44 Unit replace.** *Tag:* Capability.
  Replace all units of one type with a different type, in the map or in the selection.

  Mappers must delete each unit and place the new unit by hand.
- [x] **R49 Revamped terrain generation.** *Tag:* Terrain.
  Generate maps with a constraint-based method, such as wave function collapse: set rules for the terrain, and get
  maps that obey them.

  The generator makes terrain from amounts and a shape. Mappers cannot give it rules, such as where bases, paths and
  high ground must be.

  Done (2026-10-18), a first version: the New page's generator has Rules. Bases (up to 8, round the map, as the
  symmetry keeps them) are open ground with no water, cliffs or patches, each with a Start Location; Paths between
  the bases are ground that units can walk; Bases on high ground puts the bases and the paths on high ground. The
  preview marks the bases. Not yet: ramps from high ground to low ground, and rules for other places.
- [x] **R52 Compatibility import layer.** *Tag:* Capability.
  Import the projects of other map and EUD editors, such as EUD Editor 2, EUD Editor 3 and euddraft projects, as
  StarDraft 2 projects.

  Mappers who use other programs must make their work again to move to StarDraft 2. The import reads their data
  edits, triggers and scripts, and tells what it could not import.

  Done (2026-10-18), a first version: File > Project > Import a project reads an EUD Editor 2 project (.e2s: the
  map, the .dat edits, the stat_txt texts, the plugins, and TriggerEditor.eps with the modules it imports) or a
  euddraft project (.eds, .edd: the map, the scripts and the plugin sections; EUD Editor 3 builds through one). A list
  shows what did not come in: button sets, requirements and FireGraft's settings.
- [x] **R51 Advanced EUD capabilities.** *Tag:* EUD.
  Blocks for text compare and search for chat commands, text to number, and background music.

  These needed epScript: strcmp, strnstr and memcmp (mainly to parse chat commands), f_parse (text to a number), and
  BGM.

  Done (2026-10-18): the conditions Chat line starts with and Chat line contains, the value The number in the chat
  line after a text, and the actions Play music and Stop the music. Still to check in the game. Drawing, queues and
  byte readers moved to R63 (Later).

## Later

Exploring how to enable the next evolution of StarCraft creations.

- [ ] **R56 Signed program.** *Tag:* Platform.
  StarDraft 2 has a code-signing certificate, so that Windows and antivirus programs trust it.

  Windows shows a warning when users start StarDraft 2 for the first time, and some antivirus programs stop it. Users
  cannot see that the program comes from the StarDraft 2 team.
- [ ] **R31 Replay preview.** *Tag:* Capability.
  Make a replay of the map with computer players, and watch it in StarCraft.

  To see a map in the game, a mapper must start a game and play it.
- [ ] **R42 Map import from scmscx.com.** *Tag:* Capability.
  Find a map on [scmscx.com](https://scmscx.com), and open it in StarDraft 2.

  To edit a map from scmscx.com, mappers must download it and then open the file.
- [ ] **R63 More EUD blocks.** *Tag:* EUD.
  Blocks for drawing, queues and deques (with their methods and loops), and byte readers and writers.

  These need epScript today: EUDQueue and EUDDeque with their methods and loops, EUDByteReader and EUDByteWriter, and
  drawing. What drawing should draw (shapes of units or sprites, as Korean EUD maps make them, or text on the screen)
  is being looked into.
- [ ] **R57 Custom keyboard shortcuts.** *Tag:* UI.
  A menu where users change the keyboard shortcuts of StarDraft 2.

  The keyboard shortcuts are fixed. Users who come from other editors cannot use the keys that they know.
- [ ] **R26 Remastered mod support.** *Tag:* Mods.
  Load a StarCraft: Remastered mod (MPQ or folder) on top of the game's files, and make maps with it.

  StarDraft 2 reads only the game's own files. Mappers cannot use the files of a mod. Modders are not a group in
  README.md yet.
- [x] **R41 Game simulation.** *Tag:* Capability.
  Simulate the game in StarDraft 2, without StarCraft. Move units and tell them to stop or attack, and see who wins a
  fight. See the resources, the supply and the unit counts change over time, with a log of each event and its time.

  To balance custom units and economies, mappers must play test games in StarCraft. It needs a simulation of the game
  itself, so it waits for that.

  Done (2026-10-25), the first version: released in 0.7 as experimental. Tools > Simulate melee mining (each base's
  minerals and gas a minute) and Simulate combat (the selected units fight; who wins, the losses and the trade), shown
  live on the map with speed controls, also with an EUD project's .dat edits. The rest is R64.
- [ ] **R64 Simulation orders and timeline.** *Tag:* Capability.
  Give the simulated units orders by hand (move, stop, attack), and see the resources, the supply and the unit counts
  change over time, with a log of each event and its time.

  The first simulation (R41) plays set situations only: melee mining, and a fight of the selected units. Mappers
  cannot try their own plan, and they see the result only at the end.
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
- [x] **R28 macOS port.** *Tag:* Platform.
  StarDraft 2 on macOS.

  StarDraft 2 runs only on Windows.

  Done: 0.6.0 has a macOS disk image (Apple silicon).
- [x] **R29 Linux port.** *Tag:* Platform.
  StarDraft 2 on Linux.

  StarDraft 2 runs only on Windows.

  Done: 0.6.0 has a Linux AppImage; CI also builds a Flatpak.
- [ ] **R30 Web build.** *Tag:* Platform.
  StarDraft 2 in the browser, with no installation.

  Users must install StarDraft 2 to use it.
