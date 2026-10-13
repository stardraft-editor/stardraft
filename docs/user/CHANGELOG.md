# StarDraft changelog

StarDraft is a combined map editor and EUD editor for StarCraft: Remastered, for Windows (64-bit).

## 0.5.0.4

### Ranges on the map

View > Show ranges of the selected units: around each selected unit, its sight range (yellow), its ground weapon's
range (red) and its air weapon's range (blue). A tank's or goliath's turret counts too. EUD projects use their own
units.dat and weapons.dat.

![The ranges of a goliath and a siege tank](images/0.5.0.4/ranges.png)

### Tiles (Rectangle mode)

- The selection box is whole tiles while you drag it.
- Fill in the selection fills all of it. With several tiles selected in the Tiles list, each tile is one of them at
  random. The map's right-click menu also has Fill selection.
- Drag a box over tiles in the Tiles list to select them all. Ctrl+drag adds to the selection.
- Favorites: right-click a tile (or the selected tiles) in the Tiles list and select Add to favorites. The
  Favorites filter shows them. StarDraft keeps them for each tileset.

![A box dragged over the Tiles list](images/0.5.0.4/tile_box.png) ![The Favorites filter](images/0.5.0.4/tile_favorites.png)

### Triggers

- Increase or decrease a unit's hit points, shields or energy. An increase stops at the unit's maximum. A decrease
  stops at 0, and a unit with no hit points dies.
- Order one unit: with no target (stop, hold position, burrow, siege mode …), to a location, to a point (x, y), or
  on another unit (attack, follow, repair, load, spells).
- The generated code is shorter: StarDraft's helper functions are in a file of their own (generated/sdlib.eps), and
  triggers.eps uses only the ones it needs.

### Other changes

- Enter on a selected unit opens Unit properties.
- New releases are also posted in the Discord's #general channel.

## 0.5.0.3

### Korean and Spanish

StarDraft is now in Korean (한국어) and Spanish (Español). The language follows Windows. To change it, use
Options > Program options > Language (it applies at the next start). Game names (units, weapons) stay as the game
files give them.

![StarDraft in Korean: the map with a new ramp](images/0.5.0.3/korean_map.png)

![The start page in Korean](images/0.5.0.3/korean_start.png)

![Program options in Korean, with the language and the map unit limit](images/0.5.0.3/korean_options.png)

### Doodads

- Doodads go only on the terrain they are made for: StarDraft now checks every tile under a doodad, not only its
  top row. Doodads no longer go over the wrong terrain (for example, over rocky ground at a corner).
- Ramps and other cliff doodads go on cliffs that you paint in StarDraft, on all tilesets.
- Space Platform: Solar Array, Low Platform and Dark Platform have their correct names.

![A ramp on a cliff painted in StarDraft](images/0.5.0.3/ramp.png)

### Map pages

- Classic triggers, Map data and Players set each change in the map at once. There is no Apply button. Each change
  is one undo step.
- Map data: each weapon shows what it attacks (ground, air, or ground and air).
- Map data: to rename a unit, click the pencil button next to its name. The name shows as Program options > Unit
  names sets it.

![Map data: weapons with their targets, and a renamed unit](images/0.5.0.3/map_data.png)

![Classic triggers without Apply](images/0.5.0.3/classic_triggers.png)

### Other changes

- Keys for the tools of each mode: V select, B brush or place, A spray, F fill.
- Program options > Map unit limit: the most units a map can have (3400 for standard maps, 1700 for EUD projects).
  Placing, spraying and pasting units stop at the limit.
- Triggers: a new condition, "the current player is (less than, more than, exactly, …) Player N", for If blocks
  in For each player.
- New map: the buttons are Randomize seed, Randomize values and Randomize all.
- Right-click "Report feedback on …" works in the windows over the main window too (Unit properties, a location,
  Program options).

## 0.5.0.2

- The middle mouse button drags the map: the map follows the mouse, with no speed-up.
- With Spray, the map shows only the spray circle: no single unit, doodad or sprite in the middle (Spray places
  random picks around the mouse).
- The top bar's feedback button says "Report feedback".
- The start page asks for your feedback: right-click anything and select "Report feedback on …".
- The log no longer lists slow frames.

## 0.5.0.1

- Each new version is now a release of its own (0.5.0.1, 0.5.1, ...), with the version in its name.
- Help > Check for updates finds this version: a test of the update from 0.5 Alpha.

## 0.5 Alpha

The first public version. This is an alpha: please save often, keep copies of your maps, and report issues.

### Start

1. Unzip the release's zip file into a folder and run `stardraft.exe`. StarCraft: Remastered must be
   installed. StarDraft reads the game's files from it (if it does not find them, use Options > Program options >
   StarCraft folder).
2. StarDraft starts with a new project. File > Save asks where to keep it. A project is a folder with
   `project.stardraft`, the map, its scripts and its data changes. Double-click `project.stardraft` to open it.
3. Build (Ctrl+B) makes the map to play (`<name>_EUD.scx`). The first build downloads
   [euddraft](https://github.com/armoha/euddraft). Copy the map into StarCraft's `Maps` folder to play it.

StarDraft also opens and saves regular maps (.scm, .scx) without a project.

### Maps

- Terrain brushes in different shapes and sizes, spray, fill, replace, and copy and paste with symmetry.
- Select terrain in the shape of the brush: diamond, square or circle.
- Units, doodads, sprites and locations, with snap and placement rules (Options menu).
- HD, SD and Carbot graphics.
- A new map with generated terrain (with symmetry), or with the terrain of a map that you have.
- Scenario > Map properties: the title, description, tileset and size. Scenario > Players and forces: the players.
- Show town areas (View menu): the buildable area and the resources around each base.
- Press Q to find a unit, doodad, sprite or tile by its name.

### Classic maps

- A classic trigger editor.
- The Data page: the unit, upgrade and technology settings for each player, as in the game.
- Classic maps keep up to 3,400 units, save and load, and replays.

### EUD projects

- The Data workspace edits every .dat file, the strings, requirements, button sets and the unit display. Previews
  show what each value does: ranges, the image player with its iscript, portraits and the command card.
- Triggers are blocks in plain sentences that become epScript: values from the game, loops, keys and the mouse,
  text with colors, and the game's countdown timer. A checker marks EUD errors and desyncs, and the code shows beside
  the blocks.
- Link your own .eps files. StarDraft builds them as they are, and a build error names the file and the line.
- The bug button beside Build turns on debug mode: the triggers check their indexes in the game, and Build writes a
  trace file. epTrace (from euddraft) reads it while the map plays and shows which script lines use the most time.
  The Project page and the log show its report.

### Briefings and sounds

- A mission briefing editor with a preview that plays the briefing as the game plays it.
- The map's sounds play as in the game.

### The program

- A light and a dark theme, with eight accent colors.
- Options > Program options (Ctrl+,): the theme, the volume, the unit names, the start options and the StarCraft
  folder.

### Feedback and crashes

- Right-click buttons, labels, or panes and select "Report feedback on …", or use Help > Report feedback. Feedback is
  anonymous. Do not include private information.
- If StarDraft crashes, it sends a crash report by itself (the version, Windows, the video card, where it crashed,
  the last log lines; no maps). Can be turned off in Program options.
- Help > Check for updates gets the newest build. StarDraft also looks for one on start (Options > Program options
  to turn it off).

