#!/usr/bin/env python3
"""Snapshots of the SDL program (apps/editor/main_sdl.cpp): one run for each workspace and dialog, with no window.

usage: python3 -I tools/sdl_snapshots.py --exe <stardraft> --out <folder> [--install <folder>] [--map <map>]
                                          [--project <project>] [--only <name>,...] [--scale <s>] [--list]

Each run is `<exe> [map] --snapshot <out>/<name>.png <args>`, with SDL_VIDEODRIVER=offscreen when it is not set
(not on macOS: there the offscreen driver gives no GPU device).
A run passes when it exits with 0 and writes a PNG that is not empty and not one flat color. The PNGs, each run's
output (<name>.txt) and a summary (summary.txt) go into the --out folder. The script exits with 1 when a run fails.

A case that needs the game data runs only with --install; one that needs a map, only with --map (and --install);
one that needs a project, only with --project. The others run everywhere (CI has no game data).

To add a case: add a line to CASES. Pass only options that main_sdl.cpp reads (its argument loop in main()). In a
case's options, {map}, {project}, {install} (the --install folder), {root} (the repository), {out} (the --out folder)
and {newdir} (a new empty folder in --out) are paths. A run also fails when its output has "(WRONG)", or when the
case's function in CHECKS finds a problem in its output.
"""

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (name, arguments, needs): needs is "" (nothing), "data" (--install), "map" (--map and --install) or "project".
# Dialog sizes are those of the dialogs on Windows at scale 1 (a map's dialog sets its own size).
WORKSPACES = ["map", "data", "triggers", "assets", "project", "briefing", "files", "props", "players",
              "classictriggers", "classic"]  # main_sdl.cpp, kWorkspaceNames
MODES = ["units", "terrain", "doodads", "sprites", "locations"]  # main_sdl.cpp, --mode

CASES = [("start", [], "")]
# No StarCraft install found (--noinstall: no search): the start page's banner asks for the folder. --pickinstall gives
# Browse's folder dialog an answer: a folder with no game data (the banner says so and stays), or the install (the game
# data opens and the banner goes; a snapshot saves no setting). "found": the folder that the search finds.
CASES += [
    ("start-noinstall", ["--noinstall"], ""),
    ("start-noinstall-nodata", ["--noinstall", "--pickinstall", "{root}"], ""),
    ("start-noinstall-pick", ["--noinstall", "--pickinstall", "found"], "data"),
    # Scale 2 (a Retina display of macOS, or 200 % on Windows): the small text (the rail's labels, captions, table
    # headers) is two thirds of the UI text, as at scale 1.
    ("start-scale2", ["--scale", "2", "--size", "3200x2000"], ""),
]
CASES += [("ws-" + w, ["--workspace", w], "") for w in WORKSPACES]
CASES += [
    ("newpage-classic", ["--newpage", "classic"], ""),
    ("newpage-eud", ["--newpage", "eud"], ""),
    ("dialog-options", ["--dialog", "options", "--size", "720x820"], ""),
    ("dialog-feedback", ["--dialog", "feedback", "--size", "592x370"], ""),
    ("dialog-newproject", ["--dialog", "newproject", "--size", "688x394"], ""),
    ("dialog-prompt", ["--dialog", "prompt", "--size", "385x105"], ""),
    ("dialog-listpick", ["--dialog", "listpick", "--size", "378x494"], ""),
    ("dialog-message", ["--dialog", "message"], ""),  # the message box (msgbox::ask: questions, updates, crash reports)
    ("dialog-error", ["--dialog", "error"], ""),
    # The game data, no map.
    ("data-units", ["--workspace", "data", "--data", "units:0"], "data"),
    ("data-images", ["--workspace", "data", "--data", "images:239"], "data"),
    ("data-strings", ["--workspace", "data", "--data", "strings:5"], "data"),
]
# A map open.
CASES += [("map-ws-" + w, ["--workspace", w], "map") for w in WORKSPACES]
CASES += [("map-mode-" + m, ["--mode", m], "map") for m in MODES]
CASES += [
    ("map-terrain-rect", ["--rect"], "map"),
    ("map-classictrig", ["--workspace", "classictriggers", "--classictrig", "1:1:0"], "map"),
    ("map-dialog-unitprops", ["--dialog", "unitprops"], "map"),
    ("map-dialog-locprops", ["--dialog", "locprops"], "map"),
    ("map-dialog-prompt", ["--dialog", "prompt"], "map"),
    ("map-dialog-quick", ["--dialog", "quick", "--mode", "units"], "map"),
    ("map-newpage-classic", ["--newpage", "classic"], "map"),
    ("map-dialog-message-build", ["--dialog", "message:build"], "map"),  # Build's question (shellAskYesNo)
    ("map-dialog-message-delete", ["--dialog", "message:delete"], "map"),  # the Triggers' question before Delete
    # A project open.
    ("project-scriptpreview", ["--workspace", "project", "--scriptpreview"], "project"),
    # The top tool bar of a project's map: the icons, the modes, Build and Debug mode, Report feedback.
    ("project-topbar", ["--workspace", "map"], "project"),
    # The game's text codes (colors) in a block's text: drawn without boxes (the first trigger of the second category).
    ("project-trig-gametext", ["--workspace", "triggers", "--trig", "1:0"], "project"),
    ("project-briefing", ["--workspace", "briefing", "--briefing", "0"], "project"),
    # A file dropped on the window (on macOS also a file that Finder opens): a file that is no map says so in the status
    # bar; a map and a project open. {map}, {project} and {root} (the repository) are the paths; the case's map or
    # project is then not also given before the options.
    ("drop-other", ["--drop", "{root}/README.md"], ""),
    ("drop-map", ["--drop", "{map}"], "map"),
    ("drop-project", ["--drop", "{project}"], "project"),
    # The classic page with the pop-out of the selected action's value (trigger:kind:entry:value): the first action of
    # the first trigger, its value 3 (a map whose first trigger has an action, as Set deaths: the number pop-out).
    ("map-classictrig-value", ["--workspace", "classictriggers", "--classictrig", "1:1:0:3"], "map"),
    # The Triggers workspace with a value's pop-out open (--trigpop n: value n of the block --trigsel selects; text: its text's words). The
    # trigger and block numbers are the first trigger of "On map start" (a text with values, the objectives' colors,
    # Set resources) and the third one (Create a unit: the unit types with their icons).
    ("project-trigpop-text", ["--workspace", "triggers", "--trig", "1:0", "--trigsel", "0", "--trigpop", "text"], "project"),
    ("project-trigpop-colors", ["--workspace", "triggers", "--trig", "1:0", "--trigsel", "1", "--trigpop", "0"], "project"),
    ("project-trigpop-number", ["--workspace", "triggers", "--trig", "1:0", "--trigsel", "2", "--trigpop", "1"], "project"),
    ("project-trigpop-unit", ["--workspace", "triggers", "--trig", "1:2", "--trigsel", "0", "--trigpop", "0"], "project"),
    # The variable form's pop-outs (--trigpop var:<field>: 0 kind, 4 holds, 10 player 1's default value) of the first
    # variables of Definitions (a unit, then a value for each player).
    ("project-varpop-kind", ["--workspace", "triggers", "--trig", "0:0", "--trigpop", "var:0"], "project"),
    ("project-varpop-holds", ["--workspace", "triggers", "--trig", "0:1", "--trigpop", "var:4"], "project"),
    ("project-varpop-player", ["--workspace", "triggers", "--trig", "0:1", "--trigpop", "var:10"], "project"),
    # The game text pop-out (its colors, Preview and Raw) of the Data form's Rename and of a button's tooltip.
    ("project-textpop-rename", ["--workspace", "data", "--data", "units:0", "--textpop", "rename"], "project"),
    ("project-textpop-tooltip", ["--workspace", "data", "--data", "buttons:0", "--textpop", "tooltip"], "project"),
    # A pick box's open button (go to the value's entry): the requirements' values of Stim Packs (the form's end).
    ("project-data-open", ["--workspace", "data", "--data", "techdata:0:99999"], "project"),
    # Keys pressed in the window (--key): Select all and Copy of the doodads (the status bar says how many); Ctrl+2,
    # the rail's second item (Map data in a classic map, as on Windows).
    ("map-key-copy", ["--mode", "doodads", "--key", "Ctrl+A", "--key", "Ctrl+C"], "map"),
    ("map-key-rail", ["--key", "Ctrl+2"], "map"),
    # The map's scene (core/map_scene.inc): the grid (G), and the selection circles of all units zoomed in (+).
    ("map-scene-grid", ["--key", "G"], "map"),
    ("map-scene-select", ["--mode", "units", "--key", "Ctrl+A", "--key", "+", "--key", "+", "--key", "+", "--key", "+"], "map"),
    # The map's own keys (core/map_input.inc): Esc clears the selection; the arrows move the view (zoomed in).
    ("map-key-esc", ["--mode", "units", "--key", "Ctrl+A", "--key", "Esc"], "map"),
    ("map-key-arrows", ["--key", "+", "--key", "+", "--key", "Right", "--key", "Right", "--key", "Down"], "map"),
    # Right clicks (--rclick x,y[:n]: the menu's items go to the output, item n runs; with no n the menu shows in the
    # picture): the minimap's and the tool strip's dock menus (the minimap to the right, the Units toolbar to the left),
    # the map's menu, a right click on empty space (only Report feedback) and the palette's search box (the text
    # field's edit menu).
    ("map-rclick-minimap", ["--rclick", "200,200:3"], "map"),
    ("map-rclick-tools", ["--rclick", "1450,148:2"], "map"),
    ("map-rclick-map", ["--mode", "units", "--rclick", "800,400"], "map"),
    ("map-rclick-feedback", ["--rclick", "30,600"], "map"),
    # The palette's views (core/palette_view.inc, --palview): the Units palette as a grid of icons, and as a large grid of
    # sprites; a right click on it (the View menu: List, Grid, Normal, Large, the sections; item 1 is Grid: icons).
    ("map-palette-grid", ["--mode", "units", "--palview", "icons"], "map"),
    ("map-palette-sprites", ["--mode", "units", "--palview", "sprites,large"], "map"),
    ("map-palette-menu", ["--mode", "units", "--rclick", "1500,400:1"], "map"),
    # The pie palette (hold Space in Terrain mode): the ring of the tileset's terrains, the mouse at a slice; the output
    # says the slices, the hot one and whether letting go paints with it.
    ("map-pie", ["--pie"], "map"),
    ("map-rclick-search", ["--mode", "units", "--rclick", "1420,280"], "map"),
    # The Triggers code pane's menu (Copy all, Select all), and Report feedback in a dialog (Program options, a block's
    # help page): a right click that no view takes there.
    ("project-rclick-code", ["--workspace", "triggers", "--trig", "1:0", "--trigcode", "--rclick", "1300,700"], "project"),
    ("dialog-options-rclick", ["--dialog", "options", "--size", "720x820", "--rclick", "600,30"], ""),
    ("dialog-blockhelp-rclick", ["--dialog", "blockhelp:act.createunit", "--size", "460x600", "--rclick", "200,300"], "data"),
    # A project open.
    # A trigger whose blocks name units: the units' pictures in the values (the second trigger of the second group).
    ("project-trig-icons", ["--workspace", "triggers", "--trig", "1:1"], "project"),
    # The Doodads palette's menu (on the left) on a doodad of a project: Pin to the top first (the menu's items go to the output).
    ("project-palette-pin", ["--mode", "doodads", "--rclick", "200,600"], "project"),
]
# The Map data page's unit Rename: the game text pop-out.
CASES += [("map-textpop-unitname", ["--workspace", "classic", "--textpop", "unitname"], "map")]
# The cursors (the system's cursor is not in a picture): --cursors writes the map view's and a text field's cursors at
# some points to the case's .txt (lines "cursor: ...", each with "(yes)" or "(WRONG)").
CASES += [("map-cursors", ["--mode", "units", "--cursors"], "map")]
# Options > Middle mouse click and hold: --midpan writes to the .txt whether Pan map moves the map toward the mouse while
# the button is held and Drag map makes it follow the mouse ("(WRONG)" when not).
CASES += [("map-midpan", ["--midpan"], "map")]
# The menu bar (a system menu is not in a picture): --check menus writes its items and keys to the .txt (lines
# "menubar: File > Save [cmd+S]"). On macOS it is the system's menu bar (the application menu, the program's menus,
# the Window menu; Cmd keys), and the window has no menu bar of its own (the picture); elsewhere the window's.
CASES += [("start-menubar", ["--check", "menus"], "")]
# A new project that is not saved yet (core/documents.inc: newUntitledProject), as the Win32 program's screenshot run
# starts; its folder goes after the picture.
CASES += [("untitled-project", ["--untitled", "--workspace", "project"], "data")]
# The dialogs that take their own sizes (About, a tooltip, the value pop-out) and Add an action's block picker.
CASES += [
    ("dialog-about", ["--dialog", "about"], ""),
    ("dialog-tip", ["--dialog", "tip"], ""),
    ("dialog-valuepop", ["--dialog", "valuepop"], ""),
    ("dialog-valuepop-number", ["--dialog", "valuepop:number"], ""),
    ("dialog-blockpick", ["--dialog", "blockpick", "--size", "1266x861"], "data"),
    ("dialog-options-light", ["--dialog", "options", "--size", "720x820", "--theme", "light"], ""),
    ("map-log", ["--log"], "map"),
]
# Edits, Undo and the save (the Windows self-test's steps): each key's line in the output says the units, the doodads,
# the selection and the status bar (CHECKS reads them). --saveas saves the map into the --out folder ({out}) as File >
# Save as does, opens that file again and compares it with the open map ("(WRONG)" when a part is different).
CASES += [
    ("map-key-undo", ["--mode", "units", "--key", "Ctrl+A", "--key", "Del", "--key", "Ctrl+Z", "--key", "Ctrl+Y", "--key", "Ctrl+Z"], "map"),
    ("map-save-roundtrip", ["--saveas", "{out}/roundtrip.scx"], "map"),
    ("map-save-edited", ["--mode", "units", "--key", "Ctrl+A", "--key", "Del", "--saveas", "{out}/edited.scx"], "map"),
    # A menu command by its name (--command): View > Show town areas (the item is not gray, the areas show).
    ("map-view-towns", ["--command", "view.towns"], "map"),
    # The graphics (View > Standard definition, Carbot), zoomed in on the units.
    ("map-view-sd", ["--command", "view.sd", "--key", "+", "--key", "+", "--key", "+"], "map"),
    ("map-view-carbot", ["--command", "view.carbot", "--key", "+", "--key", "+", "--key", "+"], "map"),
    # File > Project > New EUD project (--newproject: the project Sample with a new map, in a new folder {newdir}), then
    # Build (Ctrl+B) when this computer has euddraft (else the output says that the build is skipped); the Log shows
    # euddraft's lines.
    ("data-newproject-build", ["--newproject", "{newdir}", "--workspace", "project", "--build", "--log"], "data"),
]
# The left mouse button (--click x,y[:mods], --drag x0,y0-x1,y1[:mods], in the window's pixels at scale 1, in their order
# with the --key options): each goes the host's way for a window's events (down, moves, up) and writes the same line as
# a key (with the first selected unit's place). The points are those of the map (4)Bottleneck at the fitted zoom: the
# Units palette's Marine at 1386,429, open ground at 700,650, the mineral fields at the top at 780..880,110..150 (the
# first unit at 825,128) and at the right at 1225..1260,560..600.
CASES += [
    # Units mode: a palette pick (Marine), a click on the map places it, Undo removes it.
    ("map-click-place", ["--mode", "units", "--click", "1386,429", "--click", "700,650", "--key", "Ctrl+Z"], "map"),
    # A box selects the units in it; Shift and a box adds the units of the second box; Undo changes no unit.
    ("map-drag-select", ["--mode", "units", "--drag", "780,110-880,150", "--drag", "1225,560-1260,600:Shift", "--key", "Ctrl+Z"], "map"),
    # A click selects the unit; a drag moves it 40 px down (192 map pixels); Undo puts it back.
    ("map-drag-unit", ["--mode", "units", "--click", "825,128", "--drag", "825,128-825,168", "--key", "Ctrl+Z"], "map"),
]
# The map steps of the old Windows self-test with the mouse and the keys. After each input line a second
# line ("  state: map: <tileset> <size>; name=value ...") says the mode, the player, the sprites, the selected doodad,
# the locations, the paste, the terrain (a hash of its tiles, the tile under the mouse, the terrain selection) and the
# tabs: CHECKS reads them. --move x,y moves the mouse only (a paste's copy follows it). More points of the map at the fitted zoom: open ground at 520..640,640..720 and 900,800, space (no
# ground) at 450,450, a place where the first doodad (Dark Platform #0) fits at 559,590 and at 535,590; the Doodads
# and Sprites palettes' first item at 115,600; the Terrain palette's Solar Array at
# 202,759; the Tiles list's first tile at 160,660 (Rectangle).
CASES += [
    # Doodads: a palette pick, a click places the doodad, Esc lets go of the pick, a click selects the doodad, a drag
    # moves it, Undo puts it back, Delete removes it, Undo gives it back.
    ("map-doodad-place", ["--mode", "doodads", "--click", "115,600", "--click", "559,590", "--key", "Esc", "--click", "559,590",
                          "--drag", "559,590-535,590", "--key", "Ctrl+Z", "--key", "Del", "--key", "Ctrl+Z"], "map"),
    # The doodad's ghost at the mouse before the click, zoomed in (+ twice, about the middle of the map view: 559,590 of
    # the fitted zoom is at 405,627): where it does not fit (red), then where it fits (green).
    ("map-doodad-ghost", ["--mode", "doodads", "--click", "115,600", "--key", "+", "--key", "+", "--move", "830,520",
                          "--move", "405,627"], "map"),
    # Sprites: a palette pick, a click places the sprite, Esc, a click selects it, Undo removes it.
    ("map-sprite-place", ["--mode", "sprites", "--click", "115,600", "--click", "560,690", "--key", "Esc", "--click", "560,690",
                          "--key", "Ctrl+Z"], "map"),
    # Locations: a drag makes one, a drag of its right edge makes it wider, F2 opens the prompt (Rename location): the
    # name typed over the old one (selected when the prompt opens) and Enter rename it; three Undos take the name, the
    # size and the location back. The prompt's inputs' lines come before F2's line.
    ("map-location-make", ["--mode", "locations", "--drag", "520,640-600,700", "--drag", "600,670-640,670", "--key", "F2",
                           "--type", "Selftest Spot", "--key", "Enter", "--key", "Ctrl+Z", "--key", "Ctrl+Z", "--key", "Ctrl+Z"], "map"),
    # Terrain: a palette pick (Solar Array), the brush (B) and a drag paints; Undo takes the paint back.
    ("map-terrain-brush", ["--mode", "terrain", "--click", "202,759", "--key", "B", "--drag", "520,650-640,690", "--key", "Ctrl+Z"], "map"),
    # Rectangle: a tile of the Tiles list, the brush and a click puts the tile there; Undo takes it back.
    ("map-terrain-rect-paint", ["--rect", "--click", "160,660", "--key", "B", "--click", "560,690", "--key", "Ctrl+Z"], "map"),
    # Terrain select (V): a drag selects, Copy, Paste: the copy follows the mouse; a click puts it; Undo; Cut; Undo.
    ("map-terrain-paste", ["--mode", "terrain", "--key", "V", "--drag", "520,640-600,700", "--key", "Ctrl+C", "--key", "Ctrl+V",
                           "--move", "900,800", "--click", "900,800", "--key", "Ctrl+Z", "--key", "Esc", "--key", "V",
                           "--drag", "520,640-600,700", "--key", "Ctrl+X", "--key", "Ctrl+Z"], "map"),
    # The terrain paste's ghost at the mouse.
    ("map-terrain-paste-ghost", ["--mode", "terrain", "--key", "V", "--drag", "520,640-600,700", "--key", "Ctrl+C", "--key", "Ctrl+V",
                                 "--move", "900,800"], "map"),
    # The units' paste: a box selects the mineral fields at the top, Copy, Paste: the copy follows the mouse (on ground:
    # green; over space: red); a click on ground places it; Undo.
    ("map-paste-ghost", ["--mode", "units", "--drag", "780,110-880,150", "--key", "Ctrl+C", "--key", "Ctrl+V", "--move", "700,650"], "map"),
    ("map-paste-ghost-red", ["--mode", "units", "--drag", "780,110-880,150", "--key", "Ctrl+C", "--key", "Ctrl+V", "--move", "450,450"], "map"),
    ("map-paste-placed", ["--mode", "units", "--drag", "780,110-880,150", "--key", "Ctrl+C", "--key", "Ctrl+V", "--move", "700,650",
                          "--click", "700,650", "--key", "Ctrl+Z"], "map"),
    # The player keys: 3 picks the placement's player; a Marine placed is player 3's; Esc and a click select it; 5 gives
    # it to player 5 and 0 to player 10 (the placement's player stays 3); with the Marine picked again, 7 is the
    # placement's player and the selected Marine stays player 10's.
    ("map-player-keys", ["--mode", "units", "--key", "3", "--click", "1386,429", "--click", "700,650", "--key", "Esc",
                         "--click", "700,650", "--key", "5", "--key", "0", "--click", "1386,429", "--key", "7"], "map"),
    # Tabs: a new project (--newproject, its map in a second tab), Ctrl+Shift+Tab goes back to the map, Ctrl+Tab to the
    # project, Ctrl+W closes it (the map's tab is active).
    ("map-tabs", ["--newproject", "{newdir}", "--key", "Ctrl+Shift+Tab", "--key", "Ctrl+Tab", "--key", "Ctrl+W"], "map"),
    # The quick picker (Q), a pop-out over the map that is not modal (at 522,182-1142,782): "marine" typed, a click on
    # its first row picks the Marine and closes it; Q opens it again, and a click on the map outside it closes it and
    # places a Marine there, as the press of a user's mouse does.
    ("map-quick-click", ["--mode", "units", "--key", "Q", "--type", "marine", "--click", "832,280", "--key", "Q",
                         "--click", "1200,850"], "map"),
]
# The wheel and a trackpad on the map (--wheel x,y,dx,dy[:precise][:mods], --pinch x,y,scale; in the window's pixels):
# a wheel's notch zooms at the mouse (1.2 times); a trackpad's two-finger scroll (precise) moves the map with the
# fingers (a notch of SDL's: 48 px at scale 1); Ctrl (Cmd on macOS) and a trackpad's scroll zoom (1.2 times each 4 of
# its notches); a pinch zooms around the mouse. Each line says the point in the map view and the camera before and
# after: CHECKS reads them.
CASES += [
    ("map-wheel-zoom", ["--wheel", "700,500,0,1"], "map"),
    ("map-trackpad-pan", ["--wheel", "700,500,1,-1.5:precise"], "map"),
    ("map-trackpad-ctrl-zoom", ["--wheel", "700,500,0,8:precise:Ctrl"], "map"),
    ("map-pinch", ["--pinch", "700,500,1.5"], "map"),
]
# The workspaces' edits with the mouse and the keys (the Windows self-test's steps off the map). --type types text into
# the view with the focus; --wait ms lets the program's timers run. A dialog or pop-out that an input opened (a list
# picker, the block picker, a value's pop-out, a text prompt) takes the next keys and text, and closes when there are no
# more. The mouse's points are the main window's, as on the screen: a pop-out takes a click on it, and a click outside
# it closes it; a dialog takes the clicks in its own pixels. A modal window's inputs' lines come before the line of the
# input that opened it. After each input's line, a line "  state: ..." says what the workspace holds (sdl_shot_state.inc).
CASES += [
    # The New page: the tileset Ice, then Create makes the map (128 x 128).
    ("newpage-create", ["--newpage", "classic", "--click", "489,545", "--click", "1126,895"], "data"),
    # The Data form of the Marine in a new project: Hit points typed (50, Enter), Shield enable clicked, Unit size picked
    # (Medium in the list picker), then three Undo steps.
    ("data-form-edits", ["--newproject", "{newdir}", "--workspace", "data", "--data", "units:0", "--click", "671,742", "--type", "50",
                         "--key", "Enter", "--click", "624,768", "--click", "776,872", "--type", "Medium", "--key", "Enter",
                         "--key", "Ctrl+Z", "--key", "Ctrl+Z", "--key", "Ctrl+Z"], "data"),
    # The Triggers workspace of a new project: On map start > New trigger, Add an action (the block picker: Set
    # resources), its amount typed in the value's pop-out (800), Delete, then Undo twice (the block, then its amount).
    ("data-triggers-edits", ["--newproject", "{newdir}", "--workspace", "triggers", "--trig", "1", "--click", "433,169",
                             "--click", "460,347", "--type", "Set resources", "--key", "Enter", "--click", "827,352",
                             "--type", "800", "--key", "Enter", "--key", "Del", "--key", "Ctrl+Z", "--key", "Ctrl+Z"], "data"),
    # The Classic triggers page: Add a trigger, Player 3, Undo (the page reads the map again after a moment).
    ("map-classictrig-edits", ["--workspace", "classictriggers", "--click", "310,250", "--click", "819,221", "--key", "Ctrl+Z",
                               "--wait", "400"], "map"),
    # The Files workspace: the search box of the game's sounds, "marine" typed.
    ("map-files-search", ["--workspace", "files", "--click", "240,289", "--type", "marine"], "map"),
    # The Briefing: Add an action (the list picker: Text message), then Undo.
    ("map-briefing-add", ["--workspace", "briefing", "--click", "543,166", "--type", "Text message", "--key", "Enter",
                          "--key", "Ctrl+Z"], "map"),
]


def check_undo(output):
    """None when Delete removed the units and Undo gave them back (and Redo removed them again); else the reason."""
    units = {}
    for line in output.splitlines():
        if line.startswith("key ") and " units," in line:
            key, rest = line[4:].split(":", 1)
            units.setdefault(key, []).append(int(rest.split()[0]))
    try:
        before, deleted, undone, redone, again = units["Ctrl+A"][0], units["Del"][0], units["Ctrl+Z"][0], units["Ctrl+Y"][0], units["Ctrl+Z"][1]
    except (KeyError, IndexError):
        return "the output has no key lines"
    if not before or deleted != 0 or undone != before or redone != 0 or again != before:
        return f"units: {before}, after Delete {deleted}, Undo {undone}, Redo {redone}, Undo {again}"
    return None


def check_edited(output):
    """None when the saved map has no units after Delete (the round-trip's line says 0 units)."""
    return None if "save round-trip: edited.scx: 0 units," in output else "the saved map has units after Delete"


INPUT_LINE = re.compile(r"^(key|type|click|drag|move|wait) (.*?): (\d+) units, (\d+) doodads, (\d+) selected(?: \(the first at (\d+),(\d+)\))?:")


def input_lines(output):
    """The lines of --key, --click, --drag and --move: (option, value, units, selected, (x, y) of the first selected unit or None)."""
    lines = []
    for line in output.splitlines():
        m = INPUT_LINE.match(line)
        if m:
            at = (int(m.group(6)), int(m.group(7))) if m.group(6) else None
            lines.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(5)), at))
    return lines


def check_place(output):
    """None when the palette pick and the click on the map placed one unit and Undo removed it."""
    lines = input_lines(output)
    if len(lines) != 3:
        return f"{len(lines)} input lines, not 3"
    picked, placed, undone = (n for _, _, n, _, _ in lines)
    if placed != picked + 1 or undone != picked:
        return f"units: {picked} after the pick, {placed} after the click (not one more), {undone} after Undo"
    if "Placed " not in output:
        return "the status bar does not say Placed"
    return None


def check_box_select(output):
    """None when the box selected units, Shift and a second box added more, and no unit count changed."""
    lines = input_lines(output)
    if len(lines) != 3:
        return f"{len(lines)} input lines, not 3"
    (_, _, u1, s1, _), (_, _, u2, s2, _), (_, _, u3, _, _) = lines
    if not 0 < s1 < u1 or s2 <= s1:
        return f"selected: {s1} after the box, {s2} after Shift and the second box (of {u1} units)"
    if not u1 == u2 == u3:
        return f"units: {u1}, {u2}, {u3} (a box changed the units)"
    return None


def check_drag_unit(output):
    """None when the click selected one unit, the drag moved it down, and Undo put it back."""
    lines = input_lines(output)
    if len(lines) != 3:
        return f"{len(lines)} input lines, not 3"
    (_, _, _, s1, a1), (_, _, _, s2, a2), (_, _, _, _, a3) = lines
    if s1 != 1 or not a1:
        return f"{s1} units selected after the click, not 1"
    if not a2 or a2[0] != a1[0] or a2[1] <= a1[1]:
        return f"the unit did not move down: {a1} -> {a2}"
    if a3 != a1:
        return f"Undo did not put the unit back: {a1} -> {a2} -> {a3}"
    return None


STATE_FIELD = re.compile(r'(\w+)=("[^"]*"|\S+)')


def steps(output):
    """Each input's state: its input line's counts (input, units, doodads, selected) and its state line's name=value."""
    out = []
    for line in output.splitlines():
        m = INPUT_LINE.match(line)
        if m:
            out.append({"input": f"{m.group(1)} {m.group(2)}", "units": int(m.group(3)), "doodads": int(m.group(4)),
                        "selected": int(m.group(5))})
        elif line.startswith("  state: ") and out:
            out[-1].update((k, v.strip('"')) for k, v in STATE_FIELD.findall(line))
    return out


def expect(output, count, rules):
    """None when the output has `count` inputs and each rule holds; else the first rule that does not. A rule is
    (what, function of the steps list)."""
    s = steps(output)
    if len(s) != count:
        return f"{len(s)} inputs with a state line, not {count}"
    for what, ok in rules:
        try:
            good = ok(s)
        except (KeyError, ValueError, IndexError):
            good = False
        if not good:
            return what
    return None


def check_doodad_place(output):
    """The Windows doodad, doodad_selected steps: placed, selected by a click, moved by a drag, Undo, Delete, Undo."""
    return expect(output, 8, [
        ("the palette click picked no doodad", lambda s: int(s[0]["palette"]) >= 0),
        ("the click placed no doodad", lambda s: s[1]["doodads"] == s[0]["doodads"] + 1),
        ("Esc did not let go of the pick", lambda s: s[2]["palette"] == "-1"),
        ("the click did not select the doodad", lambda s: s[3]["doodadsel"] == "1" and s[3]["doodad"] != "-"),
        ("the drag did not move the doodad", lambda s: s[4]["doodadsel"] == "1" and s[4]["doodad"] != s[3]["doodad"]),
        ("Undo did not put the doodad back", lambda s: s[5]["doodad"] == s[3]["doodad"] and s[5]["terrain"] == s[3]["terrain"]),
        ("Delete did not remove the doodad (and its tiles)", lambda s: s[6]["doodads"] == s[0]["doodads"] and s[6]["terrain"] == s[0]["terrain"]),
        ("Undo did not give the doodad back", lambda s: s[7]["doodads"] == s[1]["doodads"] and s[7]["terrain"] == s[1]["terrain"]),
    ])


def check_doodad_ghost(output):
    """The picked doodad's ghost: red where it does not fit, green (fits) where a click places it."""
    return expect(output, 5, [
        ("the ghost is not red where the doodad does not fit", lambda s: s[3]["ghost"] == "red"),
        ("the ghost does not fit where the doodad fits", lambda s: s[4]["ghost"] == "fits"),
    ])


def check_sprite_place(output):
    """A sprite placed with the mouse, selected by a click; Undo removes it and its selection."""
    return expect(output, 5, [
        ("the palette click picked no sprite", lambda s: int(s[0]["palette"]) >= 0),
        ("the click placed no sprite", lambda s: int(s[1]["sprites"]) == int(s[0]["sprites"]) + 1),
        ("the click did not select the sprite", lambda s: s[3]["spritesel"] == "1"),
        ("Undo did not remove the sprite and its selection", lambda s: s[4]["sprites"] == s[0]["sprites"] and s[4]["spritesel"] == "0"),
    ])


def location_box(step):
    """The selected location's edges (left, top, right, bottom), or None."""
    loc = step["location"]
    return tuple(int(v) for v in loc.split(":", 1)[1].split(",")) if ":" in loc else None


def check_location_make(output):
    """The Windows location_create, location_resize, location_rename steps (F2, the name typed in the prompt, Enter),
    then Undo of each. The prompt's two inputs come before F2's line."""
    return expect(output, 8, [
        ("the drag made no location", lambda s: int(s[0]["locations"]) == 2 and location_box(s[0])),
        ("the edge's drag did not make the location wider", lambda s: location_box(s[1])[2] > location_box(s[0])[2] and
         location_box(s[1])[:2] == location_box(s[0])[:2]),
        ("F2 did not open the prompt (its inputs did not come first)", lambda s: s[2]["input"] == "type Selftest Spot" and
         s[3]["input"] == "key Enter" and s[4]["input"] == "key F2"),
        ("the name changed before OK", lambda s: s[2]["name"] == s[1]["name"]),
        ("the prompt did not rename the location to Selftest Spot", lambda s: s[4]["name"] == "Selftest Spot"),
        ("Undo did not give the old name back", lambda s: s[5]["name"] == s[1]["name"]),
        ("Undo did not give the old size back", lambda s: location_box(s[6]) == location_box(s[0])),
        ("Undo did not remove the location", lambda s: s[7]["locations"] == "1" and s[7]["location"] == "-"),
    ])


def check_terrain_brush(output):
    """The Windows terrain_paint step: a palette pick, the brush and a drag change the terrain; Undo takes it back."""
    return expect(output, 4, [
        ("the palette click picked no terrain", lambda s: int(s[0]["palette"]) > 0),
        ("B did not pick the brush", lambda s: s[1]["tool"] == "0"),
        ("the drag did not paint", lambda s: s[2]["terrain"] != s[1]["terrain"]),
        ("Undo did not take the paint back", lambda s: s[3]["terrain"] == s[1]["terrain"]),
    ])


def check_rect_paint(output):
    """The Windows rect_paint step: a tile of the Tiles list, the brush and a click put tiles; Undo takes them back."""
    return expect(output, 4, [
        ("the click did not put tiles", lambda s: s[2]["terrain"] != s[1]["terrain"] and "Placed" in output),
        ("Undo did not take the tiles back", lambda s: s[3]["terrain"] == s[1]["terrain"]),
    ])


def check_terrain_paste(output):
    """The Windows terrain_select, terrain_paste_ghost, terrain_pasted and terraincut: steps, with Undo."""
    return expect(output, 12, [
        ("the drag did not select terrain", lambda s: s[1]["tsel"] != "-"),
        ("Ctrl+V did not start the paste", lambda s: s[3]["paste"] == "terrain" and s[4]["paste"] == "terrain"),
        ("the click did not paste", lambda s: s[5]["terrain"] != s[4]["terrain"]),
        ("Undo did not take the paste back", lambda s: s[6]["terrain"] == s[4]["terrain"]),
        ("Esc did not stop the paste", lambda s: s[7]["paste"] == "none"),
        ("Cut did not change the terrain", lambda s: s[10]["terrain"] != s[9]["terrain"]),
        ("Undo did not take the cut back", lambda s: s[11]["terrain"] == s[9]["terrain"]),
    ])


def check_terrain_paste_ghost(output):
    """The terrain paste follows the mouse (the picture shows its ghost)."""
    return expect(output, 5, [("the paste is not on", lambda s: s[4]["paste"] == "terrain" and s[4]["tsel"] != "-")])


def paste_ghost(step):
    """(units, red) of a units' paste ghost, or None."""
    p = step["paste"]
    return (int(p.split(":")[1]), int(p.split(":")[2][:-3])) if p.startswith("units:") else None


def check_paste_ghost(output):
    """The Windows paste_ghost step: the copy follows the mouse, green on ground."""
    return expect(output, 4, [
        ("the box selected no units", lambda s: s[0]["selected"] > 0),
        ("the paste is not on, or has red units on ground", lambda s: paste_ghost(s[3]) == (s[0]["selected"], 0)),
    ])


def check_paste_ghost_red(output):
    """The Windows paste_ghost_on_water_red step: over space (no ground) the copy is red."""
    return expect(output, 4, [
        ("the copy is not all red over space", lambda s: paste_ghost(s[3]) == (s[0]["selected"], s[0]["selected"])),
    ])


def check_paste_placed(output):
    """The Windows paste_placed step: a click places the copy; Undo takes it back."""
    return expect(output, 6, [
        ("the click did not place the copy", lambda s: s[4]["units"] == s[0]["units"] + s[0]["selected"]),
        ("Undo did not take the copy back", lambda s: s[5]["units"] == s[0]["units"]),
    ])


def check_player_keys(output):
    """The Windows player key steps: 3, then 5 and 0 on a selected unit, then 7 while placing."""
    return expect(output, 9, [
        ("3 did not pick player 3", lambda s: s[0]["player"] == "3"),
        ("the placed Marine is not player 3's", lambda s: s[2]["units"] == s[0]["units"] + 1 and s[2]["lastowner"] == "3"),
        ("the click did not select the Marine", lambda s: s[4]["selected"] == 1 and s[4]["owner"] == "3"),
        ("5 did not give the Marine to player 5", lambda s: s[5]["owner"] == "5" and s[5]["player"] == "3"),
        ("0 did not give the Marine to player 10 (or changed the placement's player)", lambda s: s[6]["owner"] == "10" and s[6]["player"] == "3"),
        ("7 while placing did not pick player 7 (or changed the Marine)", lambda s: s[8]["player"] == "7" and s[8]["lastowner"] == "10"),
    ])


def check_quick_click(output):
    """A click on the quick picker goes to it (its row picks), and a click outside it closes it (the main window takes
    the press): the mouse's points are the main window's, as on the screen."""
    return expect(output, 5, [
        ("Q did not open the quick picker", lambda s: s[0]["quick"] == "open" and s[1]["quick"] == "open"),
        ("the click on the picker's first row did not pick a unit and close it",
         lambda s: s[2]["quick"] == "-" and s[2]["palette"] != "-1" and s[1]["palette"] == "-1"),
        ("Q did not open the picker again", lambda s: s[3]["quick"] == "open"),
        ("the click outside the picker did not close it", lambda s: s[4]["quick"] == "-"),
        ("the click outside the picker did not go to the map (no unit placed)", lambda s: s[4]["units"] == s[3]["units"] + 1),
    ])


def check_tabs(output):
    """The Windows tab_previous and tab_closed steps: two tabs, Ctrl+Shift+Tab, Ctrl+Tab, Ctrl+W."""
    return expect(output, 3, [
        # (the map's tab is named after the --map file: any name but the project's)
        ("Ctrl+Shift+Tab did not go to the map's tab", lambda s: s[0]["tabs"] == "1/2" and s[0]["tab"] not in ("", "Sample")),
        ("Ctrl+Tab did not go to the project's tab", lambda s: s[1]["tabs"] == "2/2" and s[1]["tab"] == "Sample"),
        ("Ctrl+W did not close the project's tab", lambda s: s[2]["tabs"] == "1/1" and s[2]["tab"] == s[0]["tab"]),
    ])


STATE_LINE = re.compile(r"^(key|type|click|drag|move|wait) (.*?): (?:no map|\d+ units)")


def states(output):
    """The input lines and the state line after each, in the order of the output: [(input, state)]."""
    out, last = [], None
    for line in output.splitlines():
        m = STATE_LINE.match(line)
        if m:
            last = m.group(1) + " " + m.group(2)
            out.append((last, ""))
        elif line.startswith("  state: ") and out and out[-1][0] == last:
            out[-1] = (last, line[9:])
    return out


def check_newpage_create(output):
    """None when no map was open before Create, and Create made an Ice map of 128 x 128."""
    s = states(output)
    if len(s) != 2:
        return f"{len(s)} input lines, not 2"
    if "click 1126,895" != s[1][0] or not s[1][1].startswith("map: Ice 128 x 128;"):
        return f"after Create: {s[1][1] or 'no map'} (not an Ice map of 128 x 128)"
    if not re.search(r"^click 489,545: no map:", output, re.M):
        return "a map was open before Create"
    return None


def check_data_edits(output):
    """None when the number, the flag and the picked value went into the entry, and three Undo steps took them out."""
    s = [st for _, st in states(output)]
    if len(s) != 10:
        return f"{len(s)} input lines, not 10"
    want = [(2, "Hit Points=50 "), (3, "Shield Enable=1 "), (6, "Unit Size=2 ")]  # (the picker's inputs come before its click)
    for i, text in want:
        if text not in s[i]:
            return f"input {i + 1}: {s[i]} (no {text.strip()})"
    if "Unit Size=" in s[7] or "Shield Enable=" not in s[7]:
        return f"the first Undo: {s[7]}"
    if "Shield Enable=" in s[8] or "Hit Points=50 " not in s[8]:
        return f"the second Undo: {s[8]}"
    if "no changed values; 0 undo steps, 3 redo steps" not in s[9]:
        return f"the third Undo: {s[9]}"
    return None


def check_trigger_edits(output):
    """None when the block was added, its value changed, Delete removed it, Undo gave it back and then its old value."""
    lines = states(output)
    if len(lines) != 10:
        return f"{len(lines)} input lines, not 10"
    if '"New trigger"; 1 conditions: cond.always(); 0 actions' not in lines[0][1]:
        return f"New trigger: {lines[0][1]}"
    by = dict(lines)
    added, changed, deleted = by.get("click 460,347", ""), by.get("click 827,352", ""), by.get("key Del", "")
    undos = [st for k, st in lines if k == "key Ctrl+Z"]
    m = re.search(r"1 actions: (act\.\w+\([^)]*,)(\d+)\)$", added)  # (the amount is the block's last value)
    if not m:
        return f"Add an action: {added}"
    edited = m.group(1) + "800)"
    if not changed.endswith(edited):
        return f"the value: {changed} (not 800)"
    if not deleted.endswith("0 actions:"):
        return f"Delete: {deleted}"
    if len(undos) != 2 or not undos[0].endswith(edited) or not undos[1].endswith(m.group(1) + m.group(2) + ")"):
        return f"Undo: {undos} (not the block with 800, then with {m.group(2)})"
    return None


def check_classic_edits(output):
    """None when the trigger was added with Player 3 (on the page and in the map), and Undo took Player 3 out of both."""
    s = states(output)
    if len(s) != 4:
        return f"{len(s)} input lines, not 4"
    added, player, undone, waited = (st for _, st in s)
    m = re.search(r"the page has (\d+), selected (\d+)", added)
    if not m or m.group(1) != m.group(2):
        return f"Add a trigger: {added} (the new trigger is not selected)"
    n = int(m.group(1))
    if f"the map has {n}:" not in added:
        return f"Add a trigger: {added} (not in the map)"
    if "(players 3;" not in player or not player.endswith("[players 3]"):
        return f"Player 3: {player}"
    if not undone.endswith("[players none]"):
        return f"Undo: {undone} (the map's trigger still has Player 3)"
    if "players 3" in waited:
        return f"after Undo: {waited} (the page still has Player 3)"
    return None


def check_files_search(output):
    """None when the search shows fewer game sounds, and each is one with the text."""
    s = states(output)
    if len(s) != 2:
        return f"{len(s)} input lines, not 2"
    m0 = re.search(r'search "", (\d+) game sounds', s[0][1])
    m1 = re.search(r'search "marine", (\d+) game sounds', s[1][1])
    if not m0 or not m1 or not 0 < int(m1.group(1)) < int(m0.group(1)):
        return f"the search: {s[0][1]} -> {s[1][1]}"
    return None


def check_briefing_add(output):
    """None when Add an action made a briefing with a Text message (type 3), and Undo took it out."""
    s = states(output)
    if len(s) != 4:
        return f"{len(s)} input lines, not 4"
    by = {k: v for k, v in s}
    if not by.get("click 543,166", "").endswith("1 briefings; 1 actions: 3"):
        return f"Add an action: {by.get('click 543,166')}"
    if s[-1][1] != "briefing: 0 briefings":
        return f"Undo: {s[-1][1]}"
    return None


def check_banner(shown, note, data):
    """A check that the start page's line ("start page: banner shown; note: none; game data not open") is as given."""
    def check(output):
        m = re.search(r"^start page: banner (shown|not shown); note: (.*); game data (open|not open)\r?$", output, re.M)
        if not m:
            return "no start page line"
        want = ("shown" if shown else "not shown", note, "open" if data else "not open")
        if m.groups() != want:
            return f"start page: {m.groups()}, not {want}"
        return None
    return check


def check_scale2(output):
    """None when at scale 2 the UI text is 30 px and the small text (the rail's labels) two thirds of it (20 px)."""
    m = re.search(r"text (\d+) px, small text (\d+) px, titles (\d+) px", output)
    if not m:
        return "no text sizes line"
    ui, small, title = map(int, m.groups())
    if (ui, small, title) != (30, 20, 36):
        return f"text {ui} px, small {small} px, titles {title} px; not 30, 20 and 36"
    return None


def camera(output, what):
    """The point in the map view and the camera before and after (x, y, zoom) of the --wheel or --pinch line."""
    f = r"(-?[\d.]+)"
    m = re.search(rf"^{re.escape(what)}: at map {f},{f}; camera {f},{f} zoom {f} -> {f},{f} zoom {f}\r?$", output, re.M)
    if not m:
        return None
    v = list(map(float, m.groups()))
    return (v[0], v[1]), (v[2], v[3], v[4]), (v[5], v[6], v[7])


def check_zoom_at(what, factor):
    """A check that the zoom changed `factor` times and the map's point under the mouse stayed under it."""
    def check(output):
        c = camera(output, what)
        if not c:
            return f"no camera line for {what}"
        (x, y), (cx0, cy0, z0), (cx1, cy1, z1) = c
        if abs(z1 / z0 - factor) > 0.01:
            return f"the zoom changed {z1 / z0:.3f} times, not {factor}"
        for a, c0, c1 in ((x, cx0, cx1), (y, cy0, cy1)):
            if abs((c0 + a / z0) - (c1 + a / z1)) > 0.5:
                return f"the map's point under the mouse moved ({c0 + a / z0:.1f} -> {c1 + a / z1:.1f})"
        return None
    return check


def check_trackpad_pan(output):
    """None when a trackpad's scroll (1 right, 1.5 toward the user) moved the map 48 px left and 72 px up, not the zoom."""
    c = camera(output, "wheel 700,500,1,-1.5:precise")
    if not c:
        return "no camera line"
    _, (cx0, cy0, z0), (cx1, cy1, z1) = c
    if abs(z1 - z0) > 1e-4:
        return f"the zoom changed ({z0} -> {z1})"
    dx, dy = (cx1 - cx0) * z0, (cy1 - cy0) * z0  # (the camera's move in screen pixels: the map moves the other way)
    if abs(dx - 48) > 1 or abs(dy - 72) > 1:
        return f"the camera moved {dx:.1f},{dy:.1f} screen px, not 48,72"
    return None


def check_menubar(output):
    """None when --check menus printed the menu bar of this system: macOS the system's (the application menu, the
    Window menu, the Cmd keys, the Mac symbols in tooltips), elsewhere the window's (the Ctrl keys)."""
    lines = [l[len("menubar: "):] for l in output.splitlines() if l.startswith("menubar: ")]
    tips = [l[len("tip: "):] for l in output.splitlines() if l.startswith("tip: ")]
    if not lines or not tips:
        return "no menubar or tip lines"
    tops = ["File", "Edit", "View", "Mode", "Scenario", "Options", "Help"]
    if sys.platform == "darwin":
        if not lines[0].startswith("the system's"):
            return f"not the system's menu bar: {lines[0]}"
        tops = ["StarDraft 2"] + tops + ["Window"]
        need = ["StarDraft 2 > About StarDraft 2", "StarDraft 2 > Program options… [cmd+,]", "StarDraft 2 > Services",
                "StarDraft 2 > Hide StarDraft 2 [cmd+H]", "StarDraft 2 > Hide others [option+cmd+H]",
                "StarDraft 2 > Show all", "StarDraft 2 > Quit StarDraft 2 [cmd+Q]", "File > Open… [cmd+O]",
                "File > Save [cmd+S]", "File > Save as… [shift+cmd+S]", "File > Close [cmd+W]", "File > Recent maps > None",
                "Edit > Undo [cmd+Z]", "Edit > Redo [shift+cmd+Z]", "Edit > Delete [cmd+Backspace]",
                "Window > Minimize [cmd+M]", "Window > Zoom", "Window > Next tab [cmd+`]", "Window > Bring all to front"]
        never = ["File > Exit", "Options > Program options", "Help > About", "Scenario > Map properties (title, size, tileset) [cmd+M"]
        tip = "Zoom in (⌘+); ⇧⌘S; ⌘-click; ⌥-drag; Del"
    else:
        if not lines[0].startswith("the window's (shown)"):
            return f"not the window's menu bar: {lines[0]}"
        need = ["File > Save [Ctrl+S]", "File > Save as… [Ctrl+Shift+S]", "File > Exit", "Edit > Undo [Ctrl+Z]",
                "Edit > Redo [Ctrl+Y]", "Edit > Delete [Del]", "Scenario > Map properties (title, size, tileset) [Ctrl+M]"]
        never = ["Window > "]
        tip = "Zoom in (Ctrl+Plus); Ctrl+Shift+S; Ctrl+click; Alt+drag; Del"
    for t in tops:
        if t not in lines:
            return f"no menu {t}"
    for n in need:
        if not any(l == n or l.startswith(n + " (") for l in lines):
            return f"no item {n}"
    for n in never:
        if any(l.startswith(n) for l in lines):
            return f"an item {n}, which should not be there"
    if tips[0] != tip:
        return f"the tooltip's keys are {tips[0]!r}, not {tip!r}"
    return None


# Checks of a case's output (after the PNG's): a function that returns None, or what is wrong.
CHECKS = {"map-key-undo": check_undo, "map-save-edited": check_edited, "map-click-place": check_place,
          "map-drag-select": check_box_select, "map-drag-unit": check_drag_unit, "map-doodad-place": check_doodad_place,
          "map-doodad-ghost": check_doodad_ghost, "map-sprite-place": check_sprite_place,
          "map-location-make": check_location_make, "map-terrain-brush": check_terrain_brush,
          "map-terrain-rect-paint": check_rect_paint, "map-terrain-paste": check_terrain_paste,
          "map-terrain-paste-ghost": check_terrain_paste_ghost, "map-paste-ghost": check_paste_ghost,
          "map-paste-ghost-red": check_paste_ghost_red, "map-paste-placed": check_paste_placed,
          "map-player-keys": check_player_keys, "map-tabs": check_tabs, "map-quick-click": check_quick_click,
          "newpage-create": check_newpage_create, "data-form-edits": check_data_edits,
          "data-triggers-edits": check_trigger_edits, "map-classictrig-edits": check_classic_edits,
          "map-files-search": check_files_search, "map-briefing-add": check_briefing_add,
          "start-noinstall": check_banner(True, "none", False),
          "start-noinstall-nodata": check_banner(True, "That folder has no StarCraft game data.", False),
          "start-noinstall-pick": check_banner(False, "none", True), "start-scale2": check_scale2,
          "map-wheel-zoom": check_zoom_at("wheel 700,500,0,1", 1.2), "map-trackpad-pan": check_trackpad_pan,
          "map-trackpad-ctrl-zoom": check_zoom_at("wheel 700,500,0,8:precise:Ctrl", 1.44),
          "map-pinch": check_zoom_at("pinch 700,500,1.5", 1.5), "start-menubar": check_menubar}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_check(path):
    """None when the PNG is good; else the reason. Good: it decodes and its pixels are not all one color."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        return f"no PNG ({e.strerror})"
    if not data:
        return "the PNG is empty"
    if not data.startswith(PNG_SIGNATURE):
        return "not a PNG"
    pos, ihdr, idat = 8, None, []
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    if not ihdr or not idat:
        return "the PNG has no IHDR or no IDAT"
    width, height, depth, color, _, _, interlace = ihdr
    if width == 0 or height == 0:
        return "the PNG has no pixels"
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color)
    if depth != 8 or channels is None or interlace:
        return None  # (only 8-bit, not interlaced, no palette is read; the program writes RGBA 8-bit)
    try:
        raw = zlib.decompress(b"".join(idat))
    except zlib.error as e:
        return f"the PNG does not decode ({e})"
    bpp, stride = channels, width * channels
    if len(raw) < height * (stride + 1):
        return "the PNG's data is short"
    prev = bytearray(stride)
    first = None
    for y in range(height):
        start = y * (stride + 1)
        ftype, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:
            line = bytearray((a + b) & 0xFF for a, b in zip(line, prev))
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b, c = prev[i], prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif ftype != 0:
            return f"the PNG has a bad filter ({ftype})"
        if first is None:
            first = bytes(line[:bpp])
        if bytes(line) != first * width:
            return None  # two colors: good
        prev = line
    return f"the picture is one flat color ({first.hex()})"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="the SDL program (on macOS: stardraft.app/Contents/MacOS/stardraft)")
    ap.add_argument("--out", help="the folder for the PNGs, the output of each run and summary.txt")
    ap.add_argument("--install", help="a StarCraft: Remastered install (the cases that need game data)")
    ap.add_argument("--map", help="a map to open (the cases that need a map; needs --install)")
    ap.add_argument("--project", help="a project to open (the cases that need a project)")
    ap.add_argument("--only", help="only these cases (names, comma-separated)")
    ap.add_argument("--scale", default="1", help="the UI scale (default 1, so that the sizes are the same everywhere)")
    ap.add_argument("--timeout", type=int, default=120, help="seconds for each run (default 120)")
    ap.add_argument("--list", action="store_true", help="show the cases and exit")
    a = ap.parse_args()

    have = {"": True, "data": bool(a.install), "map": bool(a.map and a.install), "project": bool(a.project)}
    only = set(a.only.split(",")) if a.only else None
    if only:
        unknown = only - {name for name, _, _ in CASES}
        if unknown:
            ap.error("no such case: " + ", ".join(sorted(unknown)))
    if a.list:
        for name, args, needs in CASES:
            print(f"{name:24} {' '.join(args):50} {needs or '-'}")
        return 0
    if not a.exe or not a.out:
        ap.error("--exe and --out are necessary")
    os.makedirs(a.out, exist_ok=True)
    a.out = os.path.abspath(a.out)  # (the {out} of a case: the program may run in another folder)
    a.exe = os.path.abspath(a.exe)  # (Windows starts no program by a relative path with "/": build/Release/x.exe)
    env = dict(os.environ)
    if sys.platform != "darwin":  # (on macOS the offscreen driver has no Metal device: SDL's own driver, no window)
        env.setdefault("SDL_VIDEODRIVER", "offscreen")
    if a.install:
        env["STARDRAFT_INSTALL"] = a.install

    results, failed, skipped, hashes = [], [], [], {}
    for name, args, needs in CASES:
        if only and name not in only:
            continue
        if not have[needs]:
            skipped.append(name)
            results.append(f"{name}: skipped (needs {needs})")
            continue
        png = os.path.join(a.out, name + ".png")
        if os.path.exists(png):
            os.replace(png, png + ".old")  # (a PNG from an earlier run does not count; never deleted)
        cmd = [a.exe]
        given = any("{map}" in x or "{project}" in x for x in args)  # (the path is in the options)
        # {newdir}: a new empty folder in --out for each run (a folder is never deleted)
        newdir = tempfile.mkdtemp(prefix=name + "-", dir=a.out) if any("{newdir}" in x for x in args) else ""
        args = [x.format(map=a.map or "", project=a.project or "", install=a.install or "", root=ROOT, out=a.out, newdir=newdir)
                for x in args]
        if needs == "map" and not given:
            cmd.append(a.map)
        elif needs == "project" and not given:
            cmd.append(a.project)
        cmd += ["--snapshot", png, "--scale", a.scale]
        if a.install:
            cmd += ["--install", a.install]
        cmd += args
        print(f"::group::{name}: {' '.join(cmd)}", flush=True)
        try:
            run = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=a.timeout)
            output, code = run.stdout.decode("utf-8", "replace"), run.returncode
        except subprocess.TimeoutExpired as e:
            output, code = (e.stdout or b"").decode("utf-8", "replace") + f"\n(timed out after {a.timeout} s)", None
        except OSError as e:
            output, code = f"could not start: {e}", None
        with open(os.path.join(a.out, name + ".txt"), "w", encoding="utf-8") as f:
            f.write(" ".join(cmd) + "\n\n" + output)
        print(output, end="" if output.endswith("\n") else "\n")
        problem = f"exit code {code}" if code != 0 else png_check(png)
        if not problem and "(WRONG)" in output:  # (a check the run prints, as --cursors)
            problem = "a check printed (WRONG)"
        if not problem and name in CHECKS:
            problem = CHECKS[name](output)
        print("::endgroup::", flush=True)
        if problem:
            failed.append(name)
            results.append(f"{name}: FAILED ({problem})")
            print(f"{name}: FAILED ({problem})", flush=True)
            continue
        with open(png, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        same = hashes.setdefault(digest, name)
        note = f" (the same picture as {same})" if same != name else ""
        results.append(f"{name}: passed{note}")
        print(f"{name}: passed{note}", flush=True)

    summary = "\n".join(results) + f"\n\n{len(results) - len(failed) - len(skipped)} passed, {len(failed)} failed, " \
                                    f"{len(skipped)} skipped\n"
    with open(os.path.join(a.out, "summary.txt"), "w", encoding="utf-8") as f:
        f.write(summary)
    print(summary, end="")
    if failed:
        print("failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
