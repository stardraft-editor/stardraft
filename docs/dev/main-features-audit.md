# The main features audit (2026-10-16)

While branch port/kit moved the editor to the SDL program (`apps/editor/main_sdl.cpp`, the `sdl_*.inc` files and the
portable core `apps/editor/core`), other work went on in the Win32 program on main, and main was merged into port/kit
several times. Commit a5ea9a6 removed the Win32 program. This audit checks that each main feature of that time is in
the SDL program.

The range: the port started at c2f06c6 (main, 2026-10-14 21:56, the parent of port/kit's first commit f46e754). The
main commits after it, to 35cf48b (main's last commit), are 42 commits (`git log --first-parent c2f06c6..origin/main`).
No branch was merged into main in that range. port/kit has all of them (main is an ancestor of port/kit).

How each commit was checked:

- (a) Only docs, the roadmap, the changelog, CI or translations of texts that are still in the program: no action.
- (b) The core library (`src/`, `include/`, `tools/tests/`): `git diff origin/main port/kit -- src include` changes
  only the renderer's scene (`renderer.h`, `scene.h`, `renderer.cpp`) and one line of `commands.cpp`, so each main change
  there is in port/kit as it is.
- (c) Win32-only code (`main.cpp` and the `.inc` files that a5ea9a6 and the commits before it removed): the feature that
  the user sees was looked for in the SDL program and the core (its texts, ids, settings and functions).

Result: no feature is missing. The Windows-only datashot specs of these commits (`about:`, `openagain:`,
`portraits:`, `textvar:`, `dataname:`, `copyunit:`, `perplayer:`, `addon:`, `brushheight:`, `isomtwo:`,
`doodadpin:`, `project:`) were self-tests of the Win32 program; the SDL program has its own snapshot cases in
`tools/sdl_snapshots.py` (for example `dialog-about`, `dialog-feedback`, `project-textpop-rename`, `project-varpop-holds`,
`project-varpop-player`, `project-palette-pin`, `map-terrain-brush`).

## The commits

| Commit | What it does for the user | Kind | Where it is in the SDL program |
|---|---|---|---|
| f881e29 | Roadmap: R54 to R57 | a | (docs) |
| eb34d09 | Doodads overlap only where both have a tile; the next segment of a ramp goes in the empty corner | b | `src/editor.cpp` (`Editor::doodadOverlaps`), as on main |
| 7c4237b | A map or project that is open already goes to its tab (no second tab) | c | `core/documents.inc`: `openProject`, `openMap(path, switchToOpen)`, `findOpenProject`, `findOpenMap`, `samePath` (Quick open uses `samePath` too: `core/views_quick_pick.inc`) |
| 7c4237b | About: "StarDraft 2 is powered by euddraft and eudplib", two links | c | `core/views_about.inc`: `AboutView::paint` (the links, underlined under the mouse) |
| 7c4237b | The doodad preview is red where it overlaps, as a click checks | c | `core/map_scene.inc`: `buildScene` (`doodadFits` and `!doodadOverlaps`) |
| 4ccdaf1 | Rectangle terrain: the tile grid shows each tile in the map's art (SD, HD or Carbot) | c | `core/views_tiles.inc`: `tilesPixels` (`loadHdTiles`, `mapTilesPicture`) |
| c36b603 | Data: the animation timeline does not flash while it plays | c | `core/views_data_pictures.inc`: the timeline's blocks (`alt` starts each paint) |
| 0d478bf | A Terran add-on placed near its building goes on the add-on spot, and both are linked | b | `src/editor.cpp`, `src/units.cpp` (`placeUnit`, `addonSpot`), as on main |
| 300dab7 | Classic triggers: the action picker does not offer new blocks for now | c | `core/views_classic_page.inc`: `ctAddItems` (`kClassicBatchesOffered`, `include/stardraft/classic_triggers.h`) |
| 2c7beab | FEEDBACK.md: GitHub feedback issues also go to the #feedback forum | a | (docs) |
| 6206a2b | Feedback: Send keeps the box open with a spinner, then a thank-you and a link to the new issue; a link to the Discord server | c | `core/views_feedback.inc`: `FeedbackView` (stages, spinner, issue link, Discord link); `core/feedback_send.inc` (the send thread) |
| e4faf72 | Data: the Changed toggle is a filter icon with a tooltip | c | `core/views_data_workspace.inc`: `DataWorkspaceView` (`changed_`: `UiIcon::Filter`, its tip) |
| 70f9568 | Roadmap: R41 Game simulation | a | (docs) |
| 7e4b853 | Data > Portraits: switching is fast (decoded on a thread, kept, the next and previous ones loaded) | c | `core/data_pictures.inc`: `portraitClips`; `core/views_data_pictures.inc`: the portrait widget (loads e + 1 and e - 1) |
| 7dc73f6 | Map: a right-drag does not move the map; only the middle button pans | c | `sdl_main_view.inc`: `MapView::mouse` (the right button only opens the menu); `core/views_options.inc` ("The map moves with the mouse") |
| f6d4abe | Isometric terrain: a second terrain (Ctrl+click in the palette, a dashed frame) | c | `core/palette_tools.inc`: `pickUnit` (`palette2`); `core/views_palette.inc` (the dashed frame) |
| a6b853d | Doodads in a project: Pin to the top; pinned doodads first; the palette menu lists the categories without group headings | c | `core/palette_view.inc`: `doodadPinToggle`, `paletteRows`, `paletteMenuModel`, the section groups; `sdl_panels.inc` (`kPalPin`) |
| 77266ea | Triggers: the condition "a unit's build queue is empty" | b | `src/data/trigger_blocks.inc`, as on main |
| e4bb8e1 | Triggers: Center the camera on a unit, Scroll lock, Set the game speed | b | `src/data/trigger_blocks.inc`, `src/triggers.cpp`, as on main |
| 42a1e9f | Roadmap: R58 EUD terrain info | a | (docs) |
| f36ba5f | Roadmap: R59 Classic trigger organization | a | (docs) |
| 9892b0a | Triggers: a Text variable's default value has colors, Preview and Raw | c | `core/triggers_values.inc`: `trigEditVariableField` (`spec.colorsOnly`); `core/views_value_pop.inc` |
| 6c5f523 | Data: Rename and String N at the top of a unit, weapon, upgrade, technology or order; a changed unit name shows in the title, list and pickers | c | `core/views_data_form.inc` (the Rename and String links); `sdl_data.inc` (`entryNameString`); `core/data_model.inc`: `entryNameString`, `projectUnitName`; `core/names.inc` |
| 214e219 | Data > Units: right-click > Paste everything (values, display, button set) | c | `core/data_nav.inc`: `dataEntryMenuModel` (item 5), `dataPasteAll`, `extrasCopyUnit`, `extrasPasteUnit`; `sdl_data.inc` (the entry menu) |
| c400203 | Triggers: a Per player variable holds numbers, unit types or locations (Holds), a default value for each player; blocks can take the current player's value | c+b | `core/triggers_values.inc`: `trigPerPlayerItems`, `trigEditVariableField` (Holds, Player 1 to 8); `core/views_triggers_pane.inc` (the rows); `src/triggers.cpp` |
| 48f0ad4 | Review fixes: the ellipsis and pin glyphs; a portrait shows when its decode ends (8 kept); per-player renames and values | c+b | The glyphs were a Win32 text bug; `core/data_pictures.inc` (`portraitClips`, 8 kept), `core/views_data_pictures.inc` (repaints while it loads); `core/views_triggers_pane.inc` (`trigIsPerPlayer`); `src/triggers.cpp`. The end of a Shift stroke on WM_CAPTURECHANGED is Win32 only: on SDL the stroke ends at the button's release (`MapView::mouse`). |
| 7239259 | Units: a linked add-on is drawn with its connector and moves with its building; a link that is no longer true is cleared | c+b | `core/map_scene.inc`: `addUnitSprites` (the connector image); `src/editor.cpp` (`withLinkedAddons`, the link checks) |
| e57de21 | Terrain: the brush has a Height of its own (isometric, Rectangle, the spray) | c+b | `core/app_state.inc` (`brushHeight`), `core/map_edit.inc` and `core/palette_tools.inc` (`currentBrush`), `core/tiles_edit.inc` (the Rectangle footprint), `core/views_toolstrip.inc` (`kNumHeight`) |
| 3af8404 | Review fixes: a building's drag checks and shows its add-on; a flat brush paints the way at its smaller side | c+b | `core/map_edit.inc`: `selectionFitsAt` (`withLinkedAddons`); `core/map_scene.inc` (the moved selection); `core/map_input.inc` (`extent`: the smaller side) |
| ff26fb8 | Triggers: "number per player" is now "player array" | c+b | `core/views_value_pop.inc` (the tag), `core/triggers_values.inc` (the section "Player arrays"), `src/data/trigger_blocks.inc` |
| a66c54d | Terrain: the brush is one field: shape list, width and height with spins, wheel and typing, and a link | c | `core/views_toolstrip.inc`: the brush field in `ToolStripView::paint` (shape, `part`, the link) |
| 933c961 | Terrain: the mouse wheel on the brush field's shape changes the shape | c | `core/views_toolstrip.inc`: `stripNumberStep` (`kNumShape`) and the wheel |
| 74fade9 | Terrain: the brush field's numbers have the suffixes w and h, with a divider | c | `core/views_toolstrip.inc`: `part` (the suffixes), `divider` |
| 05c8b0e | Translations of the texts that had none; messages made of parts translate each part | a+c | `res/lang/*.tsv`; the `tr()` parts are in the core (`core/data_nav.inc`, `core/views_classic_triggers.inc`, `core/project_build.inc`, `core/triggers_values.inc`, `core/views_game_text.inc`, `core/views_data_requirements_paint.inc`, `core/releases.inc`, `core/views_new_page.inc`, `core/documents.inc`) and the SDL files (`sdl_map_pages.inc`, `sdl_start.inc`) |
| a9e2abf | Isometric terrain: Shift+click or Shift+drag paints the second terrain; the right button opens the map's menu | c | `sdl_main_view.inc`: `MapView::mouse` (`secondPaints`, `rightStroke`); `core/map_input.inc`: `secondPaints`; `core/palette_tools.inc` (the messages) |
| e8dcd1e | Terrain: the second terrain's row says Shift | c | `core/views_palette.inc` (the dashed frame and "Shift"). The `classictrig:set` and `project:` datashot changes were Win32 self-tests only. |
| 6f24316 | Texts: the feedback box says that feedback goes on the StarDraft Discord server; the camera and game speed texts are shorter | c+b | `core/views_feedback.inc` (the note); `src/data/trigger_blocks.inc` |
| b47a3b6 | Changelog 0.5.0.8 | a | (docs) |
| cdbd7e7 | About: not associated with Blizzard Entertainment; it needs an installation of StarCraft | c | `core/views_about.inc`: `AboutView::paint` |
| b3be28f | About: "It needs an installation of StarCraft to work" (shorter) | c | `core/views_about.inc`: `AboutView::paint` |
| f6a9228 | Roadmap: R60 Trigger search | a | (docs) |
| 0e2005d | Roadmap: R61 Quick add | a | (docs) |
| 60e24b9 | Roadmap: R60 is a pop-up | a | (docs) |
| 35cf48b | Roadmap: R62 Trigger comments | a | (docs) |
