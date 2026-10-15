# The SDL program's gaps

The SDL program (`apps/editor/main_sdl.cpp` and the `sdl_*.inc` files) is to replace the Windows program
(`main.cpp` and its Win32 `.inc` files) on Windows, macOS and Linux. This list is what the SDL program does not do
yet, or does in a different way, as the gap sweep of 2026-10-16 found it. Each item names where it is and what the
Windows program does. "Done" items were done in the sweep; the others are open.

Other work streams have these items (they are not in this list): the Windows build of the SDL program and its CI,
the value pop-outs of the Triggers workspace, the game text colors, the message boxes, and macOS.

## "Not on this system yet" and stand-ins

| Where | What | State |
|---|---|---|
| `sdl_main_view.inc`: `NotHereView` | A workspace with no SDL view shows "Not on this system yet". All workspaces have a view now (Map, Data, Triggers, Assets/Files, Project, Briefing, the map pages), so it shows only for a workspace that a new mode adds. | Kept as the fallback. |
| `sdl_standins.inc`: `classicCommandInProject` | Was always false: a project whose map has classic triggers or classic settings (L.4) did not get Map data and Classic triggers. | Done: the Windows version (with `classicSettingsUsed`) is in the core (`core/map_edit_commands.inc`) for both programs; `sdl_standins.inc` is gone. |
| `sdl_triggers.inc`: `sdlTrigNotHere` | The value block's menu and a field's pop-out (fields 0 and 1, the per-player fields). | The value pop-out work stream. |
| `sdl_triggers.inc`: `entryIcon` | The entries' pictures in the Triggers tree and pane are empty. Windows draws the unit's icon (`datRefIcon`). | Open (small: `sdlDataImage(dataRefPicture(...))`, as the Data workspace does). |
| `sdl_data_extras.inc`: `pickBox` | The open button of a pick box (go to the entry) is not drawn. | Open. |
| `sdl_options.inc`: View > Control kit | The first add row's multi-pick and the entries' icons. | Done: the list picker and the Data pictures; the demo items are in the core (`kitGalleryPickItems`). |
| `sdl_options.inc`: Report feedback | The comment said that sending is not there. Send works (`core/feedback_send.inc`). | Done: the comment is gone. |

## Seams that do less than the Windows host (`shell_sdl.inc`)

| Seam | SDL | Windows |
|---|---|---|
| `shellSettingsChanged` | Nothing: the menus' options (graphics, grid, layers, snaps, place rules, the terrain copy, the palette's views) are not saved. | `saveAppSettings` (main.cpp: the `AppSetting` table in the settings file). Open: move the table and `loadAppSettings` into the core. |
| `shellSaveChanges(true)` | False: Save in the save question of a new project stops the close, and the project is not saved. | `saveUnsavedProject` (asks where the project goes, then moves it there). Open. |
| `shellModeWindows` | Nothing (the SDL layout shows the mode's panes itself). | Shows the filter, the list, the section box and the palette of the mode. No gap. |
| `shellShowRail` | Nothing: the rail always shows. | Shows or hides the rail. Open (small). |
| `shellForgetAnims` | Was nothing (the SDL map had no unit animations). | Clears the animations. Done: both clear the core's animations (`core/map_scene.inc`). |
| `shellCodePageText` | UTF-8. | The system's ANSI code page (a map's legacy strings). Open: a Korean or European map's old text shows wrong. |
| `shellRunPopout` | A click outside does not close the pop-out (Esc and its buttons do). | A click outside closes it. Open. |
| `shellUiFontLfHeight`, `shellTrackMouseLeave`, `shellUiIconMask`, `shellLegacy*`, `shellMapFileOpened` | Fixed values or nothing. | Win32-only things (the LOGFONT height, the registry of old builds, the file association). No gap. |
| `shellRailUpdateNow`, `shellViewMouse` | New in the sweep: nothing (the rail paints each frame, so its items are the last frame's) and the state's mouse. | `RedrawWindow` of the rail; `GetCursorPos` in the map view. |
## Menu commands and keys

The SDL menus are the core's menu model (`core/menu_model.inc`); an item that `sdlRuns` (`sdl_menus.inc`) does not
list is gray.

| Command | State |
|---|---|
| Edit > Cut, Copy, Paste, Duplicate, Delete, Select all | Done: `core/map_edit_commands.inc` (`commandMapEdit`), for both programs. Paste is gray with no copy (`mapMenuGrayed`). |
| Options > Units, Doodads, Sprites, Locations (snaps, place rules, on top of others, drag back, names in lines) | Done: the same file, with the checks (`mapMenuCheck`). |
| Options > Terrain copy includes units, doodads | Done. |
| View > Grid size (the presets and Custom) | Done (Custom asks with the text prompt). The grid does not show: see "The map view". |
| File > Close, Close all; Ctrl+Tab, Ctrl+Shift+Tab | Done. |
| File > Quick open | Done (the core ran it; the item was gray). |
| Help > Report an issue on GitHub | Done (`issueNewUrl`, `core/feedback_send.inc`). |
| The keys 1 to 0 (the player), V, B, A, F (the tools), Ctrl+1 to Ctrl+9 (the rail's items), Ctrl+Shift+Z | Done: `kProgramKeys` in `core/map_edit_commands.inc` is the key table of both programs (the Windows accelerators are made from it). Ctrl+2 is now the rail's second item on SDL too (Map data in a classic map), as on Windows. |
| View > Show town areas (and the walk and path overlays) | Done: the core's scene draws them (see "The map view"). |
| Help > Check for updates; Options > Look for updates, Crash reports | The Windows SDL work stream. |
| Options > Maps (.scm and .scx open with StarDraft 2) | Windows only. |
| Edit > Undo, Redo: gray when there is nothing to undo | Open (small): the Windows rule uses `dataCanUndo`, which is in main.cpp. |

## The map view

Before the sweep, `sdl_main_view.inc`'s `buildMapScene` drew the terrain and the units only. The Windows
`buildScene` (main.cpp) also draws, in this order: the grid (View > Show grid, Grid size), the pathing overlays, the
doodads' overlays and the sprites (View > Layers), the units with their overlays, shadows, add-on connectors, turrets
and idle animations, a moved selection in red where it does not fit, the locations (View > Show locations; their
names, inverted ones in red, the selected one's handles), the selection circles, the terrain selection, the ranges
(View > Show ranges), the symmetry axes, the paste ghosts, the unit, sprite and doodad to place (with the doodad's
tiles), the Rectangle mode's tiles, the brush and the spray area. The layers (View > Layers: no terrain, or no doodad
tiles) also change the terrain that is sent to the renderer (`renderNow`).

Done: `buildScene` and its helpers (`addImage`, `addUnitSprites`, the animations, `rangesScene`, `tilesScene`,
`diamondOutline`, `wrapLocationName`, `playerRgba`, the map's unit graphics of a project, the layers' terrain, and the
title and status bar after a frame: `mapFrameDone`) are in `core/map_scene.inc`; both programs draw it. The SDL
program's snapshots wait until the map's sprites are loaded (`imagesPending`). Snapshots: map-scene-grid,
map-scene-select.

Open: the map view's cursor (the move and resize cursors: main.cpp `mapCursorAt`) is the arrow on SDL.

## Other surfaces

| Surface | State |
|---|---|
| The top tool bar (main.cpp `TopBarView`: New, Open, Save, Undo, Redo, Cut, Copy, Paste, Delete, Quick search, Build, Report feedback) | Open: the SDL top bar has the menus and the modes only. Build and Report feedback are in the menus. |
| Map data (a classic map's Data page): the button after the entry's name | Shows a missing glyph (a box with a question mark) on Linux. Open. |
| The snapshots (`main_sdl.cpp`) | `--key <key>` presses a key in the window after the first frame (new in the sweep; `tools/sdl_snapshots.py`: map-key-copy, map-key-rail, map-scene-grid, map-scene-select). |
| The status bar, the Log, the tabs, the rail, the minimap, the palettes, the tool strip, the map pages, Data, Triggers, Briefing, Files, Project, the New page, the start page, Program options, About, the feedback dialog | On SDL. |
