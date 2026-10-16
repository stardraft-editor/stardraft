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
| The pie palette (hold Space in Terrain mode) | `pieOpen`, `pieRelease` (main.cpp): a ring of the tileset's terrains. | Done: the ring is the core's `PieView` (`core/views_palette.inc`) over the middle of the map while Space is held, also with Shift (`MainView::pieOpen`, `pieRelease`); letting go of Space paints with any keys held, and when the program loses the keyboard (Alt+Tab) the ring closes with no change, as on Windows; the terrains, the slice under the mouse and the sizes are the core's (`pieTerrains`, `pieSliceAt`, `pieRadius`), and the Windows ring uses them too. Snapshot map-pie (`--pie`: the output says the slices, the slice under the mouse and that letting go paints with it). |

### The menus and the palette (were open)
### Also done in this sweep

| What | Windows | SDL |
|---|---|---|
| The text fields' context menu | The system's menu of an edit box (Cut, Copy, Paste, Select all); the Triggers code pane: Copy (Copy all) and Select all (`trigCodeKeys`). | Done: a right click on a kit text field opens its menu through the kit menu (`TextField::editMenu`), in both programs where the kit field is used: Undo, Cut, Copy, Paste, Delete, Select all; a read-only field (the code pane, the script preview) only Copy (Copy all with nothing selected) and Select all. kit_test `text_field_context_menu`; snapshots map-rclick-search, project-rclick-code. |
| Report feedback in the other windows | Each window (dialogs, pop-outs, the block help) gets the feedback menu on a right click. | Done: a right click that no view takes in a dialog or a pop-out (`SdlHost::setRightClickHook`) gets the menu with only "Report feedback on ..." (`sdlWindowRightClick`, `sdl_menus.inc`): the view there, else the window's title; not on a text field, not in the feedback dialog. The feedback dialog opens over that window, and it stays open (with its text) when another program takes the focus: the host keeps the pop-outs that an open dialog is opened from (`SdlHost::dispatch`, focus lost). Snapshots dialog-options-rclick, dialog-blockhelp-rclick. |
| The feedback item's icon | In the feedback color (kFeedback). | Done: `kit::MenuItem::iconColor`. |
| The menus in the snapshots | | Done: `--rclick x,y` with no item number leaves the menu open, and the snapshot shows it over the window (`SdlHost` paints the menus that views with no window open). Every kit menu (a text field's too) writes its items to the output (`kit::Menu::setTestHook`). |
## The switch on Windows (2026-10-16)
`stardraft.exe` is the SDL program now (CMake target `stardraft_editor` on every system); the Win32 program builds
as `stardraft-win32.exe` (target `stardraft_win32`), a fallback that is not in a release. A user who updates keeps
what the Win32 program kept; this table is what was checked and what changed for that.
| What | Win32 program | SDL program now |
|---|---|---|
| The release and the update | `StarDraft-<version>-win64.zip` with `stardraft.exe` only; the update unpacks it and puts `stardraft.exe` in place of the running one (`core/releases.inc`, `apps/common/update_win32.cpp`). | The same zip and asset name. The fonts, the icon atlases and the app icon are RCDATA resources of `stardraft.exe` (generated `stardraft_editor_files.rc`, `embeddedFile` in `shell_sdl_win.inc`, `kit_sdl::setEmbeddedFiles`), so the one file runs alone: an installed Win32 `stardraft.exe` updates to it with no change, and a StarDraft-latest copy runs too. Files beside the program (`res/`, `fonts/`) come first. CI takes a snapshot of `stardraft.exe` alone in a folder (windows-sdl). |
| The manifest and the version resource | `stardraft.manifest`: common controls 6, per monitor DPI (v2), UTF-8 code page. No version resource. | `stardraft_sdl.manifest`: per monitor DPI (v2), UTF-8 code page, long paths (no common controls). Both programs have a version resource now (`res/version.rc.in`: StarDraft 2, the version, `stardraft.exe` or `stardraft-win32.exe`). |
| The settings file | `%APPDATA%\StarDraft\settings.json` (`shellSettingsFile`) | The same file. |
| The settings of the builds before the file | The registry `HKCU\Software\StarDraft` for a value the file does not have (`shellLegacyValue`). | Was nothing on SDL: an update from an old build lost those settings. Done: the same registry reads (`shell_sdl_win.inc`). |
| The recent maps and projects, the working folder, the unit limits | Read at start (`loadRecent`). | Were never read on SDL: the lists showed empty, and the first map opened wrote a list of one over the user's lists. Done: `loadRecentLists` (`core/recent.inc`) and `loadUnitLimits` (`core/map_edit.inc`), for both programs. |
| Options > Open the last project on start | The last project opens at start. | Was saved only. Done: it opens at start (not with a file on the command line, not in a test run). |
| The unsaved projects' folder | `%LOCALAPPDATA%\StarDraft\Unsaved` | Was `$XDG_DATA_HOME` or `~/.local/share` on Windows too (no folder, as HOME is not set). Done: `%LOCALAPPDATA%\StarDraft\Unsaved`. |
| The StarCraft folder | `%APPDATA%\stardraft\install.txt` | Read and written too (`sdlWindowsInstallFile`), and the setting StarCraftFolder. |
| The dock layout, the options of the menus, the theme, the language, crash reports, updates on start | The settings file | The same names in the same file. |
| One program for all maps (the pipe) | None: each start was a new program. | `\.\pipe\StarDraft-<file name>-<session>`: `stardraft.exe` and `stardraft-win32.exe` do not share it. The StarDraft-latest copies (`stardraft-latest-<n>.exe`) share one name now (before, each copy had its own). |
| The file types (maps, project.stardraft) | Registered at start by each program file. | Done: `stardraft.exe` registers them at start (before, only a file named `stardraft.exe`; now each name, as the Win32 program did); `stardraft-win32.exe` leaves them to it (a name with `-win32`). |
| StarDraft-latest | Starts a copy of `stardraft-testing.exe`, else `stardraft.exe`, of the build folder. | The same files: now the SDL program. |
| Self-tests and release screenshots | `--selftest`, `--datashots` | Stay on `stardraft-win32.exe` (`tools/release.py shots` runs `stardraft-testing-win32.exe`). |
| Options > Middle mouse click and hold (MiddleDrag) | Pan map (the default): the map moves toward the mouse while the button is held, faster the farther the mouse is from where it went down; Drag map: the map follows the mouse. | Was saved, but the middle button always dragged. Done: both, as on Windows; the pan's step is the core's (`middlePanStep`, `core/map_input.inc`), the Win32 program's timer uses it too; on SDL the map view steps each frame (`MapView::panStep`). A right click during it opens no menu, and a left click starts no drag. Snapshot map-midpan (`--midpan`: the output says that Pan map moves toward the mouse while held and stops on release, that a left click during it starts no drag, and that Drag map follows the mouse). |
| The new unsaved project, the unsaved projects' folders | `newUntitledProject`: a 128 x 128 badlands project in the unsaved folder (the screenshot runs and the self-tests start with it; a normal start shows the start page, as on SDL). `cleanUnsavedProjects`: the folders of programs that ended go. | Was nothing: the folder of a Win32 program that crashed stayed. Done: both are in the core (`core/documents.inc`; `shellProcessId`, `shellProcessRunning`), for both programs; each program removes the folders of programs that ended at its start (not in a test run). Snapshot untitled-project (`--untitled`: the new project in the Project workspace; its folder goes after the picture). |
| The restart after Language (`--restart <pid>`, the setting Reopen) | Writes the open maps and projects to Reopen, starts itself with `--restart <pid>`; the new program waits for the old one to end, then opens them. | Was its own: the active map or project on the command line, and the new program could send it to the old one (the pipe) before that one ended. Done: the same setting and argument (`restartWrite`, `restartWaitFor`, `restartReopen` in `core/documents.inc`), for both programs; the SDL program waits before the pipe. Checked in WSL with a settings file of its own (`STARDRAFT_SETTINGS`): Reopen with a map, `--restart 999999`: the map opens and Reopen goes. |
Open: none.
Open:
| What | Win32 program | SDL program |
|---|---|---|
| The start with a new unsaved project | Starts with a 128 x 128 badlands project in the unsaved folder (`newUntitledProject`), and removes the folders of programs that ended (`cleanUnsavedProjects`). | Starts with the start page; the unsaved folder of a Win32 program that crashed stays. |
## The final check (2026-10-16)
Each command id of `core/ids.inc`, each key of `kProgramKeys` (`core/map_edit_commands.inc`), each workspace, dialog
and pane was compared with the Win32 fallback, by reading the code (the SDL menus run what `sdlRuns` lists, the keys
go through `shortcutRun`, the same table as the Win32 accelerators) and with the snapshots.
| What | State |
|---|---|
| The commands of the menus | Each item of the core's menu model runs on SDL, except Options > Maps (Windows only, as on the Win32 program). The ids that no menu has (`ID_EDIT_CORRECTION`, `ID_EDIT_DOODADCHECK`, `ID_EDIT_SNAPLOC`, `ID_EDIT_SNAPUNITS`, `ID_SCEN_REPLAY`, `ID_OPT_CRASHREPORTS`) are not in the menus of either program; the `ID_FIXED_*` items are always gray in both. `ID_ACCENT` is the accent swatches of Program options in both. The palette's view ids (`ID_PAL_*`) are the palette's View menu (`paletteMenuModel`). |
| View > Show town areas | Was gray on SDL: `sdlRuns` did not list it (nor the other pathing overlay ids). Done: they run (`commandMapEdit`, `core/path_overlay.inc`). Snapshot map-view-towns (`--command view.towns`: the output says that the item is not gray, and the town areas show). |
| The keys of `kProgramKeys` | Each one runs on SDL (`shortcutRun`: the program's keys, then the menus' keys). Snapshots: map-key-copy, map-key-rail, map-key-undo (Ctrl+A, Del, Ctrl+Z, Ctrl+Y). |
| The workspaces | Map, Data, Triggers, Assets, Project, Briefing, Files, and the map pages (Map properties, Players, Classic triggers, Map data): each has its SDL view (snapshots ws-* and map-ws-*). |
| The dialogs | Program options, About, Report feedback, New EUD project, the text prompt, the list picker, the block picker, a block's help, the unit and location properties, the quick picker, the value pop-out, the message boxes, the tooltip: each has a snapshot. Project settings is the Project page in both. The button pop-out (Replace a button) is `ButtonPopView` on SDL. |
| The panes | The menu bar, the top tool bar, the tabs, the rail, the status bar, the Log, the minimap, the palettes, the tool strip, the modes' bar, the code pane: on SDL (snapshots). |
| Open | Options > Middle mouse click and hold, the start with a new unsaved project and the restart's Reopen list (see "The switch on Windows"). The working folder row of Program options is below the end of the dialog-options pictures (the dialog scrolls). |
New snapshot options (`main_sdl.cpp`): `--command <name>` runs a menu command by its name; `--saveas <file>` saves
the open map as File > Save as does, opens the file again and compares it (`mapSaveRoundTrip`,
`core/map_commands.inc`); `--newproject <folder>` makes the project Sample with a new map, as File > Project > New EUD
project; `--build` in a snapshot waits for the end of the build and says its result (`gBuildResult`,
`core/project_build.inc`), and is skipped when the computer has no euddraft (a test run never downloads it). Each key
of `--key` writes the units, doodads and selection after it.
## Tests
The Win32 fallback's self-tests (`stardraft-win32 <map> --selftest <dir>`, `runSelfTest` in main.cpp, and the
`--datashots <dir> <spec>...` specs) drive the Win32 windows; they stay on `stardraft-win32.exe`. This is what of them
has an SDL snapshot case (`tools/sdl_snapshots.py`) or a test, and what has none.
### Has an SDL case now
| Win32 test | SDL case |
|---|---|
| The start page, the New page, the workspaces of a map and of a project (`pages:`) | start, newpage-classic, newpage-eud, ws-*, map-ws-* (the dark theme only; dialog-options-light is the light theme) |
| undo_twice (Undo of an edit) | map-key-undo (Delete, Undo, Redo, Undo: CHECKS reads the counts) |
| place_marines (a unit placed with the mouse), box_select, drag_move | map-click-place (a palette pick, a click on the map, Undo), map-drag-select (a box, Shift and a second box), map-drag-unit (a click selects, a drag moves the unit, Undo puts it back): `--click` and `--drag` go the host's way for a window's mouse events; CHECKS reads the counts and the unit's place |
| A save of the map (the steps that save, `openagain:`) | map-save-roundtrip and map-save-edited (`--saveas` into the --out folder: the saved file opens again and is the same; after Delete it has no units) |
| project created, project compiled (`project:`, a Build) | data-newproject-build (a new project Sample in a new folder in --out, then Build when euddraft is there) |
| sd_quality, carbot_graphics | map-view-sd, map-view-carbot |
| The pathing overlays (`--pathshots`, the town areas step) | map-view-towns |
| palette_list, palette_icons, palette_sprites, palette_large_neutral | map-palette-grid, map-palette-sprites, map-palette-menu, project-palette-pin |
| grid_snap_ghost, selection_circles | map-scene-grid, map-scene-select |
| quick picker (`quick:`) | map-dialog-quick |
| unit properties, location properties (the double-clicks) | map-dialog-unitprops, map-dialog-locprops (the dialogs only) |
| `blockpick`, `blockhelp:`, `listpick:`, `pickstyle:`, `projectdlg:`, `msgbox:`, `tooltip:` | dialog-blockpick, dialog-blockhelp-rclick, dialog-listpick, dialog-newproject, dialog-message*, dialog-error, dialog-tip |
| `textpop:`, `trigshow:`, `trigtext:`, `trigobj:` | project-textpop-*, project-trigpop-*, project-varpop-* |
| `mapmenu:`, `ctxmenu:`, `feedbacknames:` (some of it) | map-rclick-map, map-rclick-*, project-rclick-code, dialog-options-rclick |
| `dat:entry[:section]` (the Data workspace) | data-units, data-images, data-strings, project-data-open |
| The Classic triggers page (`bowner:`, `bsel:`) | map-classictrig, map-classictrig-value (the page and a value's pop-out; not the owners, the selection or the Edit commands) |
| The briefing (`brec:`) | project-briefing (one picture, not the video) |
| The cursors | map-cursors |
| doodad_ghost, doodad, doodad_selected (a doodad placed, selected, dragged, deleted) | map-doodad-ghost (the ghost: red, then green where it fits), map-doodad-place (a palette pick, a click places it, Esc, a click selects it, a drag moves it, Undo, Delete, Undo) |
| Sprites placed with the mouse | map-sprite-place (a palette pick, a click places the sprite, Esc, a click selects it, Undo removes it and its selection) |
| location_create, location_resize, location_rename | map-location-make (a drag makes a location, a drag of its right edge makes it wider, F2 and the prompt rename it: `--prompt` types the name and Enter; three Undos) |
| terrain_paint, rect_paint | map-terrain-brush (a terrain of the palette, B, a drag paints; Undo), map-terrain-rect-paint (Rectangle: a tile of the Tiles list, B, a click; Undo) |
| terrain_select, terrain_paste_ghost, terrain_pasted, `terraincut:` | map-terrain-paste (V, a drag selects, Copy, Paste follows the mouse, a click pastes, Undo, Esc, Cut, Undo), map-terrain-paste-ghost (the copy at the mouse) |
| paste_ghost, paste_ghost_on_water_red, paste_placed | map-paste-ghost (the copy of the mineral fields at the mouse, green on ground), map-paste-ghost-red (red over space), map-paste-placed (a click places it; Undo): `--move` moves the mouse only |
| The player keys (1 to 0) | map-player-keys (3 picks the player; 5 and 0 give a selected Marine to players 5 and 10; 7 while placing picks the player and leaves the Marine) |
| tab_previous, tab_closed | map-tabs (a new project in a second tab, Ctrl+Shift+Tab, Ctrl+Tab, Ctrl+W) |

After each `--key`, `--click`, `--drag` and `--move`, a second output line (`  state: name=value ...`, sdl_menus.inc:
`sdlStateLine`) says the mode, the player, the owners, the sprites, the selected doodad, the locations, the paste (and
how many of its units are red), the terrain (a hash of its tiles, the tile under the mouse, the terrain selection) and
the tabs; CHECKS reads it. On SDL File > New shows the start page in the map's tab (no page tabs), so map-tabs makes
its second tab with `--newproject`.

These cases found a bug that both programs had: after Undo of a placed sprite or doodad that was selected, the
selection kept it (the status bar said "1 selected" for a sprite that was not there). Now Undo and Redo drop the
selected doodads and sprites that are not on the map (`dropGoneItems`, core/map_edit.inc).
### No SDL case yet
| Win32 test | What it checks |
|---|---|
| place_cc_on_water_corrected, place_marines_corrected, drag_place, unit_spray, unit_replace_marines_with_zealots | Units placed with the mouse: the correction to a free place, a row, the spray, Replace (`--click` and `--drag` can do these now). |
| drag_snap_back, drag_over_water_red, drag_dropped_corrected | A dragged selection that does not fit (snap back, red where it does not fit). |
| doodad_spray, sprite_details_hd, sprite_details_sd, `spritetools:` | The doodads' spray; the units' sprite details in HD and SD; the sprite tools. |
| location_move, `locwrap:` | A location moved with the mouse; a long name in lines. |
| brush_circle_*, fill_lake_with_grass, spray_mud, replace_mud_with_rocky, symmetry_*, `selshape:`, `pipette:` | The other terrain tools on the map (the brush's shapes, Fill, Spray, Replace, the mirrors, the selection's shapes, Ctrl+click). |
| zoom_25, hd4x_zoom_200 | The zoom to a percent and HD 4x (the + key zooms in map-view-sd). |
| new_page_map, new_map_ice, generated_map, generated_again | A map made on the New page (Blank, a tileset, Generate). |
| `cardagain:` | The start page's cards. |
| The recent menus at the start, `menutree:`, `barmenu:`, `menutest:` | The menu tree, the File menu by the keys, the context menus driven by messages. |
| The Data workspace's steps (tabs, Tab, back and forward, a weapons.dat save, `portrait:`, `mapplayers:`, `classicdefault:`, `unitnames:`) | The Data form's edits and their save. |
| The Triggers editor's steps (copy and paste, the keyboard, values, variables, properties, an external script, `trigreplace:`, `emptylists:`, `bcmd:`) and `trigger_selftest.inc` (help, fixes, a picked unit deleted, a build error) | The Triggers workspace's edits. |
| `eptrace:` | Trace build and its report. |
| `files:`, `fref:` | The Files workspace's search and a sound the map names. |
| `spray:`, `emptynone:`, `rightpan:`, `wheelpick:`, `dpi:` | Doodads > Spray, an empty search, the right button on the map, the wheel on a dropdown, a DPI change. |
| `opentimes:`, `leaks:` | The time to open each workspace; GDI and USER objects (Win32 only). |
The kit's own tests (`tools/tests/kit_test.cpp`) check the portable views without a program (a text field's menu,
the text codec).
