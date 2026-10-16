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
| `sdl_triggers.inc`: `entryIcon` | The entries' pictures in the Triggers pane's values were empty. Windows draws the unit's icon (`datRefIcon`). | Done: the Data pictures (`sdlDataImage`), snapshot project-trig-icons. |
| `sdl_data_extras.inc`: `pickBox` | The open button of a pick box (go to the entry) was not drawn. | Done: the button after the box (a button's value, a requirement's value, a unit's button set), as on Windows; `openEntry` is in the core (`core/data_nav.inc`). Snapshot project-data-open (the requirements of Stim Packs). |
| `sdl_options.inc`: View > Control kit | The first add row's multi-pick and the entries' icons. | Done: the list picker and the Data pictures; the demo items are in the core (`kitGalleryPickItems`). |
| `sdl_options.inc`: Report feedback | The comment said that sending is not there. Send works (`core/feedback_send.inc`). | Done: the comment is gone. |

## Seams that do less than the Windows host (`shell_sdl.inc`)

| Seam | SDL | Windows |
|---|---|---|
| `shellSettingsChanged` | Was nothing: the menus' options (graphics, grid, layers, snaps, place rules, the terrain copy) were not saved. | `saveAppSettings` (the `AppSetting` table in the settings file). Done: the table is `core/app_settings.inc`; the SDL program reads it at its start and saves it after each command (not in snapshots and tests). Done: the palette's views of each mode (PaletteViews) are in the table (`core/palette_view.inc`), for both programs. |
| `shellSaveChanges(true)` | Was false: Save in the save question of a new project stopped the close, and the project was not saved. | `saveUnsavedProject` (asks where the project goes, then moves it there). Done: the move is the core's `saveUnsavedProjectTo` (`core/project_page.inc`) for both programs; each host asks where with its own save dialog (SDL: `sdlSaveUnsavedProject`, also for File > Save). |
| `shellModeWindows` | Nothing (the SDL layout shows the mode's panes itself). | Shows the filter, the list, the section box and the palette of the mode. No gap. |
| `shellShowRail` | Nothing: the rail always shows. | Shows or hides the rail; the core (`core/title.inc`) always shows it now. No gap. |
| `shellForgetAnims` | Was nothing (the SDL map had no unit animations). | Clears the animations. Done: both clear the core's animations (`core/map_scene.inc`). |
| `shellCodePageText` | Was UTF-8: a script in an old code page (a Korean mapper's CP949 `.eps`) showed wrong in the Project page's preview. | The system's ANSI code page (the RichEdit read the script). Done: `widenLegacy` (`apps/common/text_codec.h`): UTF-8, else CP949 when the bytes read as Korean, else Windows-1252. The CP949 table is `apps/common/cp949_table.inc` (made by `tools/gen_cp949.py`); kit_test `text_codec_legacy` checks it (on Windows, each code against code page 949). The code font has the Korean and Chinese fonts merged in (`apps/kit_sdl/canvas_imgui.cpp`), as the RichEdit's font linking gave them. Open: the map's strings (the Strings tab, the game texts) are UTF-8, else Windows-1252 on both programs (`widen`, `gameTextDecode`); a Korean map's old strings show wrong there. `widenLegacy` can decode them when both programs change together. |
| `shellRunPopout` | Was a modal dialog window (centered, with a title bar) that a click outside did not close. | A pop-out window at its place with a border; a press outside only closes it (`popoutEatsPress`). Done: on SDL it is a modal pop-out window at its place with a 1 px border (`PopoutFrameView`); a press outside only closes it, and the press and its release go to no view (`SdlHost::runModal` with a Popup spec); Esc closes it too. Checked by reading (a snapshot shows the main window only). |
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
| Edit > Undo, Redo: gray when there is nothing to undo | Done: `mapMenuGrayed` for both programs (`dataCanUndo` is in `core/data_model.inc`); the SDL menus are made again when Undo, Redo or Paste go gray or come back. |

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

Done: the map view's cursor (the move cursor while the camera moves and over a selected unit, doodad or sprite that a
press moves, the resize cursors on the selected location's edges and corners) is the core's `mapCursorAt`
(`core/map_input.inc`) for both programs; the SDL map view gives it to the host (`MapView::cursor`), which shows it
with `SDL_CreateSystemCursor` (`SdlHost::setCursor`; the diagonal cursors are new in `draw::Cursor`). The kit views
give their own cursors in the same way (a text field: the I-beam; a link, a button: the hand). The host also sets the
cursor after a press or a release, not only when the mouse moves. `--cursors` writes the cursors at some points as text
(snapshot map-cursors; a "(WRONG)" line fails the case).

## Other surfaces

| Surface | State |
|---|---|
| The top tool bar (main.cpp `TopBarView`: New, Open, Save, Undo, Redo, Cut, Copy, Paste, Delete, Quick search, Build, Report feedback) | Done: the bar is `core/views_topbar.inc` (`TopBarView`), for both programs. On SDL it is under the menu bar, as on Windows, with the modes after its icons; the icons are gray as the menu items are (`commandEnabled`). Snapshots: map-mode-units (a map), project-topbar (a project: Build and Debug mode). |
| The glyph icons (Segoe MDL2 Assets, which only Windows has): the game text editor's align buttons, the Rename button of Map data's Units page (the pencil U+270E), the start page's alpha note mark, the chevrons, + and -, the brush size's link, the pin of a pinned Data entry, Turn off | Done: the SDL canvas draws each glyph of the core's table (`kitpaint::glyphIcon`, `core/kit_paint.inc`) as its UI icon (`kit_sdl::setGlyphIcons`), on every system; the atlases have the five icons that were not there (align left, chevron right, info, pin, link). A private use glyph that is not in the table and not in the font is not drawn (no box). Snapshots start, map-ws-classic, project-textpop-tooltip, map-mode-terrain, project-trig-icons. |
| The snapshots (`main_sdl.cpp`) | `--key <key>` presses a key in the window after the first frame (new in the sweep; `tools/sdl_snapshots.py`: map-key-copy, map-key-rail, map-scene-grid, map-scene-select). |
| The status bar, the Log, the tabs, the rail, the minimap, the palettes, the tool strip, the map pages, Data, Triggers, Briefing, Files, Project, the New page, the start page, Program options, About, the feedback dialog | On SDL. |

## The second sweep (2026-10-16, after the second round)

This sweep compared each menu command, key, window, dialog, pane and context menu of the Windows program with the SDL
program. The menu model is the same for both programs, and each command of it runs on SDL (`sdlRuns`), except
Options > Maps (Windows only). The other work streams of this round have the top bar, the
glyph icons, the cursors, the clicks outside a pop-out, the save of a new project and the legacy text: they are not in
this list.

### Done in this sweep

| What | Windows | SDL now |
|---|---|---|
| The map's own keys: Esc, R and Shift+R, the arrows | `viewProc` (WM_KEYDOWN): Esc ends a stroke (one undo step), goes back to the mode's selection, then clears the selection; R turns the terrain to paste; the arrows move the selected units (1 px, Shift 8, with snap a grid cell) or the view. | The same functions for both programs: `mapCancelStep`, `mapTurnPaste`, `mapArrowKey` (`core/map_input.inc`); `MainView::mapKey` (`sdl_main_view.inc`) after the menus' keys, only when the map has the keyboard (or no view has it), as on Windows. Before, Esc only stopped a unit pick for a trigger, and R and the arrows did nothing. Snapshots: map-key-esc, map-key-arrows. |
| Alt + wheel on the map: the brush's size | `viewProc` (WM_MOUSEWHEEL) | `mapBrushWheel` (`core/map_input.inc`) for both; before, the wheel always zoomed on SDL. (`brushSizeIndex` is in `core/views_toolstrip.inc` now.) |
| The dock menu: a right click on the minimap, the tool strip or the modes' bar (Left, Right, Hide) | The main window's WM_CONTEXTMENU | `dockMenuModel` (`core/menu_model.inc`) for both; `MainView::mouse`. Before, SDL had no way to move them. Snapshots: map-rclick-minimap, map-rclick-tools. |
| "Report feedback on ..." at the end of each context menu | `popupMenu` (feedback.inc) | `sdlPopupMenu` and `sdlFeedbackOn` (`sdl_menus.inc`): the map's menu, the Triggers tree and pane, the Classic triggers page, the dock menu and the Data menus. The name is the view's `feedbackName` (its tip) at the mouse, else the pane, else the workspace (`feedbackWorkspaceName`, `core/feedback_send.inc`, for both programs: the Windows list had no names for the map's pages). Snapshot: map-rclick-map. |
| A right click where no menu opens: a menu with only "Report feedback on ..." | `feedbackAfterRightClick` | `MainView::mouse` (not on the map and the text fields). Snapshot: map-rclick-feedback. |
| The tool strip's brush shape list and the Tiles panel's favorites menu | Menus at the box and at the mouse (`shellPopupPick`, `shellContextMenu`) | Menus there too (`sdlMenuPick`, `shell_sdl.inc`); before, a list in a dialog. |
| The snapshots kept the user's settings | The self-test saves nothing | The dock layout and the recent lists are not saved in a test or screenshot run (`gSettingsOff`, `core/recent.inc`), and the SDL snapshots use the default layout. (A snapshot that moved the minimap had saved it.) |
| `--rclick x,y[:n]` (`main_sdl.cpp`, `sdlRightClickShot`) | | A right click in a snapshot: the menu that opens writes its items to the output (`menu: ...`), and item n runs. |
| The palette's views | The View button and a right click on the palette: List, Grid (units: icons or sprites), Normal or Large, the list's sections (go to one), and in a project Pin to the top / Unpin for a doodad (`unitPaletteMenu`, `setUnitPaletteView`, `doodadPinToggle`). The views of each mode are saved (PaletteViews). | Done: the views, the View menu (`paletteMenuModel`), the rows with the pinned doodads first (`paletteRows`), the places of the rows and cells (`paletteLayout`), Shift+click (`paletteShiftPick`) and the pins (`doodadPinToggle`) are in the core (`core/palette_view.inc`) for both programs; PaletteViews is in the settings table. On SDL the palette and the second palette are the core's `PaletteGridView` (`core/views_palette.inc`): a list or a grid with the sprites of units (`paletteBitmap`'s `sprite`), the grid's footer with the item's name, the pin of a pinned doodad. The View button and a right click on either palette open the menu. Snapshots: map-palette-grid, map-palette-sprites (`--palview`), map-palette-menu, project-palette-pin. |
| The pie palette (hold Space in Terrain mode) | `pieOpen`, `pieRelease` (main.cpp): a ring of the tileset's terrains. | Done: the ring is the core's `PieView` (`core/views_palette.inc`) over the middle of the map while Space is held (`MainView::pieOpen`, `pieRelease`); the terrains, the slice under the mouse and the sizes are the core's (`pieTerrains`, `pieSliceAt`, `pieRadius`), and the Windows ring uses them too. Snapshot map-pie (`--pie`: the output says the slices, the slice under the mouse and that letting go paints with it). |

### Open

| What | Windows | SDL |
|---|---|---|
| The text fields' context menu | The system's menu of an edit box (Cut, Copy, Paste, Select all); the Triggers code pane: Copy (Copy all) and Select all (`trigCodeKeys`). | The kit's text field has no menu (the keys work). |
| Report feedback in the other windows | Each window (dialogs, pop-outs, the block help) gets the feedback menu on a right click. | Only the main window (and the menus above). |
| The feedback item's icon | In the feedback color (kFeedback). | In the menu's text color. |
