// The editor's portable core: the parts of apps/editor that compile without Win32 (research/ports/MAC-BUILD-PLAN.md).
// main_sdl.cpp includes this file; core_check.cpp compiles it on every system without Win32
// headers, so a Win32 call that comes into the core breaks the build at once (the ratchet). A file moves in here when it
// has no Win32 call left; it never moves back.

#include "strings.inc"
#include "ids.inc"
#include "prefs.inc"
#include "install_search.inc"
#include "releases.inc"
#include "recent.inc"
#include "dock_prefs.inc"
#include "modes.inc"
#include "colors.inc"
#include "app_state.inc"
#include "palette_tools.inc"
#include "kit_paint.inc"
#include "documents.inc"
#include "layout.inc"
#include "menu_model.inc"
#include "commands_core.inc"
#include "views_status.inc"
#include "views_stub.inc"
#include "map_edit.inc"
#include "tiles_edit.inc"
#include "path_overlay.inc"
#include "map_input.inc"
#include "views_rail.inc"
#include "views_minimap.inc"
#include "views_tabs.inc"
#include "map_commands.inc"
#include "views_modebar.inc"
#include "views_toolstrip.inc"
#include "views_panel_tabs.inc"
#include "map_edit_commands.inc"  // the Map's Edit and Options commands, the player and tool keys, Close (commandMapEdit, kProgramKeys)
#include "app_settings.inc"  // the menus' options in the settings file (appSettings, saveSettingsTable, loadSettingsTable)
#include "startup.inc"
#include "names.inc"
#include "palette_items.inc"
#include "palette_view.inc"  // the palette's views: list or grid, the View menu, the pins, the rows' places, the pie's terrains
#include "data_list.inc"
#include "views_data.inc"
#include "views_data_requirements.inc"
#include "views_data_requirements_paint.inc"
#include "views_data_display.inc"
#include "views_data_players.inc"  // the Data form's Players section (the map's settings for each player)
#include "data_edits.inc"
#include "views_data_form.inc"
#include "views_data_strings.inc"
#include "views_data_buttons.inc"
#include "data_model.inc"
#include "data_nav.inc"  // the Data workspace's pins, back and forward, clipboard, menus and entry pictures
#include "data_pictures.inc"  // the Data pictures' model: the game's images, the image player, the timeline, the portraits
#include "map_scene.inc"  // the map's scene: the grid, the sprites, the units and their animations, the locations, the selection, the ghosts (buildScene)
#include "map_image.inc"  // File > Export map image and the New page's larger preview: the options, the dialog, the terrain pictures
#include "views_data_pictures.inc"  // the Data pictures on the Canvas: the image player, the timeline, the portrait (the SDL form's)
#include "views_data_diagrams.inc"  // the Data diagrams on the Canvas: footprint, ranges, damage, speed, sprite, sound player, script code
#include "game_text_model.inc"
#include "views_data_button_pop.inc"
#include "views_game_text.inc"
#include "views_search.inc"
#include "views_tiles.inc"
#include "views_data_entry.inc"
#include "sound_lists.inc"
#include "views_files.inc"
#include "views_data_refs.inc"
#include "views_data_list.inc"
#include "views_data_workspace.inc"
#include "views_project.inc"
#include "views_map_pages.inc"
#include "views_map_props.inc"
#include "views_players.inc"
#include "views_classic_settings.inc"
#include "views_classic_triggers.inc"
#include "views_classic_batch.inc"
#include "views_classic_value.inc"
#include "views_list_pick.inc"
#include "views_palette.inc"  // the palette's list and grid (PaletteGridView) and the pie palette (PieView): the SDL program's
#include "views_palette_float.inc"  // the floating terrain palette's window and its note in the side bar
#include "views_classic_page.inc"
#include "views_triggers_tree.inc"
#include "views_triggers_pane.inc"
#include "views_triggers_pick.inc"
#include "triggers_model.inc"  // the Triggers workspace's program parts (the set, the tree's commands, the menus' models)
#include "undo_history.inc"  // Edit > Undo history: the steps of the workspace's undo, with their names
#include "views_triggers_help.inc"  // the block help page, the code pane, the build errors' line map
#include "views_value_pop.inc"
#include "triggers_values.inc"  // the Triggers pane's values: which pop-out each one opens, Tab, the value block's menu
#include "views_unit_props.inc"
#include "views_location_props.inc"
#include "views_text_prompt.inc"
#include "views_map_menu.inc"
#include "views_quick_pick.inc"
#include "views_briefing.inc"
#include "views_briefing_page.inc"  // the Briefing workspace's page (BriefingPageView; the SDL host is sdl_briefing.inc)
#include "views_start.inc"
#include "views_new_page.inc"
#include "views_new_page_paint.inc"
#include "views_options.inc"
#include "views_feedback.inc"
#include "feedback_send.inc"  // the feedback's files and sending (both programs' Send)
#include "views_new_project.inc"
#include "views_message_box.inc"
#include "views_about.inc"
#include "views_changelog.inc"  // the release notes window (the top bar's Update, Help > What is new): the HTML view (html_view.h)
#include "views_help.inc"  // the help window (Help > Documentation, F1): the pages of docs/help in the HTML view
#include "views_tip.inc"
#include "kit_surface.inc"
#include "views_kit_gallery.inc"
#include "title.inc"
#include "project_page.inc"  // the Project page's program parts, createProject, newMap, addProjectScript (the host shows the page)
#include "project_build.inc"  // Build: the files it writes, euddraft on a thread, the log of the build
#include "views_topbar.inc"  // the top tool bar: the icons, Build, Debug mode, Report feedback (TopBarView)
#include "views_log.inc"  // the Log at the bottom of the window: its lines, its bar and its height (the SDL program's; Windows keeps its own panel)
