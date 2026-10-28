# Keyboard shortcuts

The keys of StarDraft 2. On macOS, Ctrl is Command (⌘) and Alt is Option (⌥). A key with no Ctrl or Alt (a letter, a
digit, Space) does its work only when no text field has the keyboard: in a text field it types.

## Files and tabs

| Key | What it does |
|---|---|
| Ctrl+N | New: the start page (a classic map or an EUD project) |
| Ctrl+Shift+N | New EUD project |
| Ctrl+O | Open a map or a project |
| Ctrl+Shift+O | Quick open: an open tab, a recent map or project |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+Shift+E | Export map image |
| Ctrl+W | Close the tab |
| Ctrl+Shift+W | Close all tabs |
| Ctrl+Tab, Ctrl+Shift+Tab | The next tab, the tab before (macOS: also Command with the key left of 1, and with Shift) |
| Ctrl+B | Build the map of the EUD project |
| Ctrl+, | Program options |

## Edit

| Key | What it does |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| Ctrl+Shift+H | Undo history |
| Ctrl+X, Ctrl+C, Ctrl+V | Cut, copy, paste |
| Ctrl+D | Duplicate |
| Del | Delete the selection (macOS: also ⌘⌫) |
| Ctrl+A | Select all |
| F2 | Rename: the selected location, or the selected trigger in the Triggers tree (macOS: also Return in the tree) |
| Q | Quick picker: the mode's units, terrains, doodads, sprites or locations; in Data, any entry; in the trigger editors, Quick add; in Help, a search of the help |
| Ctrl+Shift+F | Find in triggers (the Triggers workspace and Classic triggers) |
| Ctrl+P | Find a command: each item of the menus in a searchable list, with its menu, its keys and whether it is on; Enter runs it |

## Workspaces and pages

| Key | What it does |
|---|---|
| Ctrl+1 to Ctrl+9 | The side bar's items in their order: Map, Data, Triggers, Briefing, Assets, Properties (a project: then Project), and last Help. View > Workspaces lists them with their keys |
| Ctrl+M | Show or hide the Log (not on macOS: there Cmd+M is Window > Minimize) |
| F1 | Help on what has the keyboard: a Data field's page, a trigger check's part, else the contents |

## The map

| Key | What it does |
|---|---|
| T, U, D, S, L | Terrain, Units, Doodads, Sprites and Locations mode |
| Ctrl+T, Ctrl+U, Ctrl+G, Ctrl+R, Ctrl+L | Terrain, Units, Doodads, Sprites and Locations mode (as SCMDraft's layer keys, but Doodads on Ctrl+G; in the Map workspace) |
| Ctrl+F | Show or hide the fog of war of the selected player |
| V, B, A, F | The mode's tools: Select, Brush (place), Spray and Fill |
| Ctrl+Alt+T | Terrain mode: Isometric, Rectangle and Custom, each in turn |
| Shift+click in the palette | Isometric terrain: the second terrain (Ctrl+click too) |
| Shift+click, Shift+drag | Isometric terrain: paint with the second terrain (Brush, Spray, Replace) |
| Shift+click with Fill | Fill every place of the clicked terrain (in the selection, if there is one) |
| 1 to 9, 0 | Player 1 to 10 for what you place (in Terrain mode, the palette's terrain) |
| G | Show or hide the grid |
| + and - | Zoom in and out |
| Ctrl+0 | Actual size |
| Ctrl+H | Fit the map in the window |
| Arrows | Move the selected units by one pixel, else scroll the map |
| Shift+arrows | Move the selected units by 8 pixels, or by a grid cell when units snap |
| Enter | Properties of the selected location or unit |
| Esc | Cancel what is under way: a paste, a drag, a selection |
| R, Shift+R | Turn the terrain that is about to be pasted, the other way |
| Space (held) | Terrain mode: the pie palette; let go over a slice to paint with its terrain (View > Terrain pie palette: a click on a slice) |
| Alt+wheel | The brush one size up or down; Doodads mode with a doodad picked (no spray): the next or previous doodad |

The mouse: the wheel zooms the map at the mouse; a trackpad scrolls it with two fingers and zooms with a pinch; the
middle button held pans it (Options > Middle mouse click and hold).

## Data workspace

| Key | What it does |
|---|---|
| Ctrl+E | The Table view of the open .dat file, or the entry's form again (View > Data > Entry view, Table view) |
| Ctrl+click, Shift+click | In the entry list: select more entries. The Table view then shows only them |
| Shift+Up, Shift+Down, Ctrl+A | In the entry list: select more entries, or all of them |

In the Table view:

| Key | What it does |
|---|---|
| Arrows, Tab, Home, End, Page Up, Page Down | Move the active cell; with Shift, select the cells to it. Ctrl+arrow: the end of the row or column |
| A letter or a digit, Enter, F2 | Type a value. Enter puts it into every selected cell |
| +10, -10, \*1.5, /2, then Enter | Change each selected number by this step (hit points in hit points) |
| Space | Toggle the selected check boxes. On a flag field's cell: the list of its flags. Ctrl+Space: select the column; Shift+Space: select the row |
| Del | Reset the selected cells to the game's values |
| Ctrl+C, Ctrl+V | Copy and paste cells, as tab-separated text (spreadsheets use it too). A paste with a row of column names puts each column into the column it names |
| Ctrl+D | Copy the top cell of the selection into the cells under it |
| Ctrl+A | Select all cells |
| Alt+Down, a letter | Open the list of a dropdown's cell; a letter searches it. Enter puts the value into each selected cell. On a flag field's cell: the list of its flags |
| Right button | Reset the cells, copy or paste. On a column's header: hide the column, sort, or Columns… to select the columns that show |
| Double click on a header | Sort by the column: ascending, then descending, then the default order |
| Drag a selected column's header | Move the column. The project keeps the columns' order, widths, hidden columns and sort |
| Double click on a header's right edge | Fit the column's width to its text (all selected columns) |

The box at the top filters the columns by name. **Changed** shows only the columns that have a value that is not the game's. The right button's **Copy with headers** copies the ids and the column names (the field names, such as hit_points) too; **Copy as CSV** copies the same, separated by commas. A paste of these puts each value into its entry and its column, also when the columns show in another order. A flag field is one wide column: its value in hex, then a check box for each flag, with the flag's name above it. Type a hex value (0x is not necessary) or the names of the flags (Building, Flyer) to change the value, and the check boxes follow. A click on a check box changes that flag in each selected cell of the column, and the hex value follows. In Columns…, each click shows or hides a column at once; a column that you show goes to the right end.

## Triggers workspace

| Key | What it does |
|---|---|
| Up, Down | The block before or after |
| Alt+Up, Alt+Down | Move the selected block up or down |
| Tab, Shift+Tab | The next or previous value of the block |
| Enter | Edit the selected block's first value |
| A letter or a digit | Add a block: the block picker opens with what you type |
| Del | Delete the selected block |
| Esc | Stop editing the value |
| F1 | The selected block's problem, in Trigger checks |

## Classic triggers

| Key | What it does |
|---|---|
| Up, Down | The trigger, condition or action before or after |
| Ctrl+click, Shift+click | Choose several conditions or actions |
| Del | Delete the chosen conditions or actions, or the trigger |
| Ctrl+C, Ctrl+X, Ctrl+V | Copy, cut and paste triggers, conditions or actions |
| Ctrl+D | Duplicate the trigger |
| Q | Quick add: a condition or an action after the selected one |
| Ctrl+wheel | Zoom the conditions and actions |

## Briefing

| Key | What it does |
|---|---|
| Space | Play or pause the briefing |
| Up, Down | The action before or after |
| Alt+Up, Alt+Down | Move the selected action up or down |
| Del | Delete the selected action |

## Lists, dropdowns and the help

| Key | What it does |
|---|---|
| Space, Enter, Alt+Down | Open a dropdown's list |
| A letter | In an open list: the next item that starts with it |
| Up, Down, Enter, Esc | In a list or the quick picker: move, pick, close |
| Alt+Left, Alt+Right | Help: back and forward |
| Ctrl+F | Help: the search |

## Sources and credits

The keys are those of the program's menus and its key table (apps/editor/core/map_edit_commands.inc and
apps/editor/core/map_commands.inc). Written for StarDraft 2.


## Two-key shortcuts

Each menu command that has no key of its own has two keys: press the first key, then the second key. After the
first key, the status bar shows that StarDraft 2 waits for the second key. Esc cancels. The menus show these keys
too, for example "Ctrl+K, G". The second key is always one letter or digit, with no Shift. (Tools > Cosmonarchy: only in
Cosmonarchy BW mode.)

### Ctrl+K: the View menu

| Keys | Menu command |
|---|---|
| Ctrl+K, E | View > Graphics > High definition (2x, default) |
| Ctrl+K, K | View > Graphics > High definition (4x) |
| Ctrl+K, S | View > Graphics > Standard definition |
| Ctrl+K, C | View > Graphics > Carbot (cartooned) |
| Ctrl+K, 1 | View > Grid > Fine (8 pixels) |
| Ctrl+K, 2 | View > Grid > Small (16 pixels) |
| Ctrl+K, 3 | View > Grid > Normal (32 pixels) |
| Ctrl+K, 4 | View > Grid > Large (64 pixels) |
| Ctrl+K, 5 | View > Grid > Huge (128 pixels) |
| Ctrl+K, 6 | View > Grid > Custom |
| Ctrl+K, T | View > Overlays > Town areas |
| Ctrl+K, P | View > Overlays > Pathing > Regions |
| Ctrl+K, A | View > Overlays > Pathing > Connected areas |
| Ctrl+K, W | View > Overlays > Terrain flags > Walkable ground |
| Ctrl+K, H | View > Overlays > Terrain flags > Ground height |
| Ctrl+K, B | View > Overlays > Terrain flags > Ground that blocks the line of sight |
| Ctrl+K, G | View > Overlays > Terrain flags > Buildable ground |
| Ctrl+K, R | View > Overlays > Unit vision and ranges > All ranges |
| Ctrl+K, V | View > Overlays > Unit vision and ranges > Sight |
| Ctrl+K, Q | View > Overlays > Unit vision and ranges > Target acquisition |
| Ctrl+K, Y | View > Overlays > Unit vision and ranges > Ground weapon |
| Ctrl+K, J | View > Overlays > Unit vision and ranges > Air weapon |
| Ctrl+K, D | View > Overlays > Unit vision and ranges > Detection |
| Ctrl+K, I | View > Overlays > Unit vision and ranges > Psi field |
| Ctrl+K, O | View > Overlays > Turn off all overlays |
| Ctrl+K, N | View > Animate units |
| Ctrl+K, M | View > Show minimap |
| Ctrl+K, U | View > Units list |
| Ctrl+K, L | View > Data > Entry view |
| Ctrl+K, X | View > Triggers > Trigger code pane |

### Ctrl+Shift+L: View > Layers and View > Floating palettes

| Keys | Menu command |
|---|---|
| Ctrl+Shift+L, T | View > Layers > Terrain |
| Ctrl+Shift+L, D | View > Layers > Doodads |
| Ctrl+Shift+L, S | View > Layers > Sprites |
| Ctrl+Shift+L, U | View > Layers > Units |
| Ctrl+Shift+L, R | View > Layers > Map revealers |
| Ctrl+Shift+L, C | View > Layers > Creep |
| Ctrl+Shift+L, L | View > Layers > Locations |
| Ctrl+Shift+L, 1 | View > Floating palettes > Terrain palette |
| Ctrl+Shift+L, 2 | View > Floating palettes > Unit palette |
| Ctrl+Shift+L, 3 | View > Floating palettes > Doodad palette |
| Ctrl+Shift+L, 4 | View > Floating palettes > Sprite palette |
| Ctrl+Shift+L, 5 | View > Floating palettes > Location list |

### Ctrl+J: the Options menu

| Keys | Menu command |
|---|---|
| Ctrl+J, 1 | Options > Units > Do not snap |
| Ctrl+J, 2 | Options > Units > Snap to tiles (32 px) |
| Ctrl+J, 3 | Options > Units > Snap to half tiles (16 px) |
| Ctrl+J, 4 | Options > Units > Snap to quarter tiles (8 px) |
| Ctrl+J, B | Options > Units > Snap buildings to tile edges |
| Ctrl+J, F | Options > Units > Place only where it fits |
| Ctrl+J, N | Options > Units > Move to the nearest place where it fits |
| Ctrl+J, A | Options > Units > Place anywhere |
| Ctrl+J, T | Options > Units > Can be on top of others |
| Ctrl+J, R | Options > Units > Keep resources 3 tiles from town halls |
| Ctrl+J, D | Options > Units > Randomize unit direction when placed |
| Ctrl+J, G | Options > Units > Dragged units go back to the last place where they fit |
| Ctrl+J, P | Options > Doodads > Place only where it fits |
| Ctrl+J, M | Options > Doodads > Move to the nearest place where it fits |
| Ctrl+J, Y | Options > Doodads > Place anywhere |
| Ctrl+J, 5 | Options > Sprites > Do not snap |
| Ctrl+J, 6 | Options > Sprites > Snap to tiles (32 px) |
| Ctrl+J, 7 | Options > Sprites > Snap to half tiles (16 px) |
| Ctrl+J, 8 | Options > Sprites > Snap to quarter tiles (8 px) |
| Ctrl+J, Z | Options > Locations > Do not snap |
| Ctrl+J, X | Options > Locations > Snap to tiles (32 px) |
| Ctrl+J, C | Options > Locations > Snap to half tiles (16 px) |
| Ctrl+J, V | Options > Locations > Snap to quarter tiles (8 px) |
| Ctrl+J, L | Options > Locations > Names in lines that fit in the location |
| Ctrl+J, U | Options > Terrain copy includes units |
| Ctrl+J, O | Options > Terrain copy includes doodads |

### Ctrl+Shift+K: the File, Tools and Help menus

| Keys | Menu command |
|---|---|
| Ctrl+Shift+K, O | File > Project > Open project |
| Ctrl+Shift+K, M | File > Project > Make this map a project |
| Ctrl+Shift+K, I | File > Project > Import a project (experimental) |
| Ctrl+Shift+K, S | File > Project > Add script |
| Ctrl+Shift+K, F | File > Project > Open project folder |
| Ctrl+Shift+K, P | File > Project > Project settings |
| Ctrl+Shift+K, X | File > Exit |
| Ctrl+Shift+K, C | Tools > Check that StarCraft can start the map |
| Ctrl+Shift+K, H | Tools > Check pathing (nooks and crannies) |
| Ctrl+Shift+K, 1 | Tools > Decorate the terrain > A few doodads |
| Ctrl+Shift+K, 2 | Tools > Decorate the terrain > Some doodads |
| Ctrl+Shift+K, 3 | Tools > Decorate the terrain > Many doodads |
| Ctrl+Shift+K, R | Tools > Place map revealers |
| Ctrl+Shift+K, E | Tools > Remove all map revealers |
| Ctrl+Shift+K, Y | Tools > Cosmonarchy > Try to repair the ISOM |
| Ctrl+Shift+K, K | Tools > Cosmonarchy > Check the CRIG commands |
| Ctrl+Shift+K, B | Help > Report feedback |
| Ctrl+Shift+K, G | Help > Report an issue on GitHub |
| Ctrl+Shift+K, D | Help > Join Discord |
| Ctrl+Shift+K, L | Help > StarDraft 2 on GitHub |
| Ctrl+Shift+K, W | Help > What is new |
| Ctrl+Shift+K, U | Help > Check for updates |
| Ctrl+Shift+K, A | Help > About StarDraft 2 |
