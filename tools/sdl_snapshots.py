#!/usr/bin/env python3
"""Snapshots of the SDL program (apps/editor/main_sdl.cpp): one run for each workspace and dialog, with no window.

usage: python3 -I tools/sdl_snapshots.py --exe <stardraft> --out <folder> [--install <folder>] [--map <map>]
                                          [--project <project>] [--only <name>,...] [--scale <s>] [--jobs <n>] [--list]

Each run is `<exe> [map] --snapshot <out>/<name>.png <args>`, with SDL_VIDEODRIVER=offscreen when it is not set
(not on macOS: there the offscreen driver gives no GPU device).
A run passes when it exits with 0 and writes a PNG that is not empty and not one flat color. The PNGs, each run's
output (<name>.txt) and a summary (summary.txt) go into the --out folder. The script exits with 1 when a run fails.
--jobs <n>: n runs at the same time (default 1). Each run has its own files in --out (its PNG, its output, its new
folders); the files that runs share ({wav}, {release}, the settings files) are written once, before the runs start.
Each run prints its output when it ends; summary.txt has the cases in their order.

A case that needs the game data runs only with --install; one that needs a map, only with --map (and --install);
one that needs a project, only with --project. The others run everywhere (CI has no game data).

To add a case: add a line to CASES. Pass only options that main_sdl.cpp reads (its argument loop in main()). In a
case's options, {map}, {project}, {install} (the --install folder), {root} (the repository), {out} (the --out folder),
{newdir} (a new empty folder in --out), {kodir} (a new empty folder in --out with Korean letters and spaces in its
name), {wav} (a short sound that the script writes in --out), {release} (GitHub's answer for the newest release, with a
download for each system: RELEASE_JSON), {oldsettings} (the settings file of an older release), {nosnapsettings} (a settings file with
Options > Units > Snap buildings to tile edges off)
and {legacyhive} (Windows: a registry file with the settings of the releases before the settings file) are paths, and
{script:<name>} is the path of the input script SCRIPTS[name] (main_sdl.cpp's --script), written into --out with the
same paths in it. A run also fails when its output has "(WRONG)", or when the
case's function in CHECKS finds a problem in its output.
"""

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import threading
import zlib
from concurrent.futures import ThreadPoolExecutor

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
    # The banner's Browse is a Tab stop: Tab goes through the cards and Browse, Shift+Tab back; Left and Right from
    # Browse go to a card (each --key line says where the page's keys are).
    ("start-noinstall-keys", ["--noinstall"] + [a for k in ["Tab"] * 5 + ["Shift+Tab"] * 3 + ["Right", "Shift+Tab", "Left"]
                                                for a in ("--key", k)], ""),
    # Scale 2 (a Retina display of macOS, or 200 % on Windows): the small text (the rail's labels, captions, table
    # headers) is two thirds of the UI text, as at scale 1.
    ("start-scale2", ["--scale", "2", "--size", "3200x2000"], ""),
    # A trackpad's scroll as macOS gives it (--wheel ...:precise:mac: in tenths of points, on any system): the start page
    # moves by its exact distance (3 tenths of 10 points: 30 px), not by notches; the momentum after the fingers lift
    # comes as more such events.
    ("start-trackpad-scroll", ["--noinstall", "--size", "1000x500", "--wheel", "500,300,0,-3:precise:mac"], ""),
]
CASES += [("ws-" + w, ["--workspace", w], "") for w in WORKSPACES]
# The main window as the system's document window (sdl_window.inc; a native window and a sheet are not in a picture):
# --check window prints what the window has. macOS: a hidden window with the picture as its file, with changes (the
# proxy icon, the edited dot), full screen and Split View, the system's appearance, accent color and sidebar color. Each
# system: the accent that each of macOS's accent colors gives, and the sheet of the save question (its buttons in the
# system's order, with their keys). -system: the theme and the accent color are the system's (macOS's Automatic).
CASES += [
    ("check-window", ["--check", "window", "--lang", "en"], ""),
    ("check-window-system", ["--check", "window", "--lang", "en", "--theme", "system", "--accent", "system"], ""),
    ("dialog-options-system", ["--dialog", "options", "--size", "720x820", "--theme", "system", "--accent", "system"], ""),
]
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
    # In the Assets workspace a dropped sound imports into the map (as Import sounds...); another file says so.
    ("drop-sound-assets", ["--workspace", "files", "--drop", "{wav}"], "map"),
    ("drop-image-assets", ["--workspace", "files", "--drop", "{root}/apps/editor/res/atlas-16.png"], ""),
    # macOS: what the Dock and Finder parts set up (the Dock menu, Build's bar and badge, the Settings toolbar), as text;
    # elsewhere one line says that they are macOS only. A second --check (another part's name) adds nothing to the
    # Dock's lines. Program options' tabs as macOS's Settings window shows them.
    ("check-dock", ["--check", "dock", "--check", "bundle"], ""),
    ("dialog-options-tab-general", ["--dialog", "options:0", "--size", "720x520"], ""),
    ("dialog-options-tab-map", ["--dialog", "options:1", "--size", "720x520"], ""),
    ("dialog-options-tab-start", ["--dialog", "options:2", "--size", "720x520"], ""),
    ("dialog-options-tab-folders", ["--dialog", "options:3", "--size", "720x520", "--theme", "light"], ""),
    # What the program sets up for the system (--check, sdl_dist.inc). bundle: on macOS the app's document types,
    # exported types, Quick Look extensions and signatures (elsewhere "not macOS"). preview: the map's Quick Look
    # picture (every system), its terrain the same as the minimap's. spotlight: on macOS the map's Spotlight entry,
    # indexed and removed again.
    ("check-bundle", ["--check", "bundle"], ""),
    ("map-check-preview", ["--check", "preview:{newdir}"], "map"),
    ("map-check-spotlight", ["--check", "spotlight"], "map"),
    # The free desktops (Linux, sdl_xdg.inc): the app id (the .desktop file's name, also SDL's), the XDG folders, what
    # the desktop has (the session bus, the portal, the notification server) and the recently used files' entry of the
    # map, written two times into {newdir} (one entry, its count 2). Elsewhere one line says that it is Linux only.
    ("map-check-desktop", ["--check", "desktop:{newdir}"], "map"),
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
    # The window loses the focus during a stroke (--lostdrag: Alt+Tab before the release, which does not come): the
    # Shift stroke with the second terrain (Ctrl+click on Solar Array) ends at once, as the Windows program's
    # WM_CAPTURECHANGED: the mouse that moves after it paints nothing, the first terrain is the pick again, and one Undo
    # takes the stroke back.
    ("map-terrain-lostdrag", ["--mode", "terrain", "--click", "202,759:Ctrl", "--key", "B", "--lostdrag", "520,650-640,690:Shift",
                              "--move", "700,650", "--key", "Ctrl+Z"], "map"),
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
    # In Terrain mode the keys pick the palette's terrains: 1 the first, 3 the third, 0 the tenth (the player stays 1).
    ("map-terrain-keys", ["--mode", "terrain", "--key", "1", "--key", "3", "--key", "0"], "map"),
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
# a wheel's notch zooms at the mouse (1.2 times). Only macOS tells a trackpad's two-finger scroll (precise) from a
# wheel's: there it moves the map with the fingers, and Cmd and a trackpad's scroll zoom (1.2 times each 4 of its
# notches). On Windows and Linux a scroll with fractions is a smooth wheel's (a Logitech MX: 0.25 a step), and it
# zooms as the wheel (1.2 times a notch); Ctrl too. A pinch zooms around the mouse on each system. Each line says the
# point in the map view and the camera before and after: CHECKS reads them.
CASES += [
    ("map-script-session", ["--script", "{script:session}:3"], "map"),
    ("map-script-dialogs", ["--mode", "locations", "--drag", "520,640-600,700", "--script", "{script:dialogs}:300"], "map"),
    ("project-script-reopen", ["--script", "{script:reopen}:6"], "project"),
    ("script-nodata", ["--noinstall", "--script", "{script:nodata}:3"], ""),
]
CASES += [
    ("map-wheel-zoom", ["--wheel", "700,500,0,1"], "map"),
    ("map-wheel-smooth", ["--wheel", "700,500,0,0.25:precise"], "map"),
    ("map-trackpad-pan", ["--wheel", "700,500,1,-1.5:precise"], "map"),
    ("map-trackpad-ctrl-zoom", ["--wheel", "700,500,0,2:precise:Ctrl"], "map"),
    ("map-pinch", ["--pinch", "700,500,1.5"], "map"),
    # Alt + pinch with a brush (the terrain brush, B): the brush's size up (fingers apart), then down; the zoom stays.
    ("map-brush-pinch", ["--mode", "terrain", "--key", "B", "--pinch", "700,500,1.5:Alt", "--pinch", "700,500,0.6:Alt"], "map"),
    # macOS's smart zoom (a double tap with two fingers; --smartzoom feeds the event that mac_input.mm gives): in two
    # times at the mouse, then back to the zoom before.
    ("map-smartzoom", ["--smartzoom", "700,500", "--smartzoom", "700,500"], "map"),
    # Swipes between the tabs (a new project gives the second tab): three fingers to the right (the tab before: the
    # map's) and to the left (the next: the project's), as macOS's monitor gives them (--swipe); then two fingers to
    # the right on the tabs' strip (a scroll as macOS gives it), the map's tab again.
    ("map-swipe", ["--newproject", "{newdir}", "--swipe", "800,500,right", "--swipe", "800,500,left",
                   "--wheel", "100,83,-8,0:precise:mac"], "map"),
]
# The workspaces' edits with the mouse and the keys (the Windows self-test's steps off the map). --type types text into
# the view with the focus; --wait ms lets the program's timers run. A dialog or pop-out that an input opened (a list
# picker, the block picker, a value's pop-out, a text prompt) takes the next keys and text, and closes when there are no
# more. The mouse's points are the main window's, as on the screen: a pop-out takes a click on it, and a click outside
# it closes it; a dialog takes the clicks in its own pixels. A modal window's inputs' lines come before the line of the
# input that opened it. After each input's line, a line "  state: ..." says what the workspace holds (sdl_shot_state.inc).
CASES += [
    # The New page: the tileset Ice, then Create makes the map (128 x 128).
    ("newpage-create", ["--newpage", "classic", "--click", "489,517", "--click", "1126,895"], "data"),
    # Generate (a window wide enough for its settings to be beside the preview), then 8 quick clicks (Repeat8: SDL counts
    # them, so all but the first are double clicks) on Randomize seed, Randomize values and Randomize all, a wait after
    # each: each click randomizes, and one map is made, for the last values shown.
    ("newpage-randomize-quick", ["--size", "2400x1300", "--newpage", "classic", "--click", "894,394", "--wait", "600",
                                 "--click", "1870,463:Repeat8", "--wait", "600", "--click", "1566,529:Repeat8", "--wait", "600",
                                 "--click", "1679,529:Repeat8", "--wait", "600"], "data"),
    # Generate, then the Symmetry dropdown: a click opens its list, Down twice and Enter pick Mirror top and bottom, and
    # the map is made again with it. Then Blank (the dropdown goes from view) and Down twice: the symmetry stays.
    ("newpage-symmetry-dropdown", ["--size", "2400x1300", "--newpage", "classic", "--click", "894,394", "--wait", "600",
                                   "--click", "1822,496", "--key", "Down", "--key", "Down", "--key", "Enter", "--wait", "600",
                                   "--click", "894,362", "--key", "Down", "--key", "Down"], "data"),
    # The Data form of the Marine in a new project: Hit points typed (50, Enter), Shield enable clicked, Unit size picked
    # (Medium in the list picker), then three Undo steps.
    ("data-form-edits", ["--newproject", "{newdir}", "--workspace", "data", "--data", "units:0", "--click", "671,742", "--type", "50",
                         "--key", "Enter", "--click", "624,768", "--click", "776,872", "--type", "Medium", "--key", "Enter",
                         "--key", "Ctrl+Z", "--key", "Ctrl+Z", "--key", "Ctrl+Z"], "data"),
    # Quick clicks (--click x,y:Repeat8: eight clicks, as SDL counts them: the second and each next one is a double
    # click to the views): each one acts, on each kind of control. The damage table's More armor (a diagram's button:
    # armor 8), a check box of the Data form (Shield enable: eight changes, eight undo steps, off again) and the brush
    # height's up arrow (Terrain: 2 to 10).
    ("data-quick-armor", ["--newproject", "{newdir}", "--workspace", "data", "--data", "weapons:0", "--click", "779,404:Repeat8"], "data"),
    ("data-quick-toggle", ["--newproject", "{newdir}", "--workspace", "data", "--data", "units:0", "--click", "624,768:Repeat8"], "data"),
    ("map-quick-arrow", ["--mode", "terrain", "--click", "296,485:Repeat8"], "map"),
    # A double click needs both clicks on the same row (--click x,y:Quick: a click that comes quickly after the one
    # before, which SDL counts as a double click): two quick clicks on two rows select the second one and play nothing;
    # a double click on one row plays its sound. The Assets page's game sounds and the SFX data list.
    ("files-double-click", ["--workspace", "files", "--click", "300,375", "--click", "300,401:Quick", "--click", "300,453:Double"], "map"),
    ("data-sfx-double-click", ["--newproject", "{newdir}", "--workspace", "data", "--data", "sfxdata:1", "--click", "200,239",
                               "--click", "200,265:Quick", "--click", "200,317:Double"], "data"),
    # The caret is where the drawn letters end, after a long text (51 letters, no spaces) is typed (--check caret: each
    # focus line has the caret's x and the x where the letters of its line end): Map properties' Title and Description,
    # the Strings tab's text (a game text field) and the Data list's search box.
    ("map-props-caret", ["--check", "caret", "--workspace", "props", "--click", "420,214", "--key", "Ctrl+A", "--type", "ThequickbrownfoxjumpsoverthelazydogThequickbrownfox",
                         "--click", "420,300", "--key", "Ctrl+A", "--type", "ThequickbrownfoxjumpsoverthelazydogThequickbrownfox"], "map"),
    ("data-text-caret", ["--check", "caret", "--newproject", "{newdir}", "--workspace", "data", "--data", "strings:5", "--click", "800,360",
                         "--key", "Ctrl+A", "--type", "ThequickbrownfoxjumpsoverthelazydogThequickbrownfox", "--click", "190,152", "--type", "ThequickbrownfoxjumpsoverthelazydogThequickbrownfox"], "data"),
    # The diagrams' handles (the small squares) are only on parts that the mouse drags: the flingy's turn path has none
    # (its Turn Radius is typed, as on Windows); the weapon's ranges have their handles.
    ("data-turn-nohandle", ["--newproject", "{newdir}", "--workspace", "data", "--data", "flingy:0", "--move", "10,10"], "data"),
    ("data-range-handles", ["--newproject", "{newdir}", "--workspace", "data", "--data", "weapons:0", "--move", "10,10"], "data"),
    # Location properties, both ways: the edges typed freely set the Type list (0,0,32,32: Normal; 64 in Left: inverted
    # left and right), and a type picked in the list (Up, Down: the list has the keyboard) swaps the edges to give it,
    # with the same numbers (Normal: 32,0,64,32; inverted left and right: 64,0,32,32; inverted top and bottom:
    # 32,32,64,0). OK puts the edges in as they are; Undo takes them back. The dialog's inputs' lines come before
    # Enter's (the key that opened it).
    ("map-location-inverted", ["--mode", "locations", "--drag", "520,640-600,700", "--key", "Enter"] +
     [a for v in ["0", "0", "32", "32"] for a in ("--key", "Tab", "--key", "Ctrl+A", "--type", v)] +
     ["--key", "Shift+Tab"] * 3 + ["--key", "Ctrl+A", "--type", "64"] + ["--key", "Tab"] * 4 +
     ["--key", "Up", "--key", "Down", "--key", "Down", "--key", "Shift+Tab", "--key", "Enter", "--key", "Ctrl+Z"], "map"),
    # A list picker's search box takes typing (Unit size of the Marine's Data form), with the keys and the text as SDL
    # gives them on Windows (--keysfromopener: from the main window under the pop-out): the typed letters filter the
    # rows, Korean through the input method (its composition, then the text), Backspace takes a letter back, Esc closes it.
    ("data-picker-typing", ["--keysfromopener", "--newproject", "{newdir}", "--workspace", "data", "--data", "units:0", "--click", "776,872",
                            "--type", "Med", "--ime", "\ud55c", "--type", "\ud55c", "--key", "Backspace", "--key", "Esc"], "data"),
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
# Typed text as SDL gives it (--type: SDL_EVENT_TEXT_INPUT; --ime: an input method's composition, SDL_EVENT_TEXT_EDITING)
# in English, Korean, Russian, Chinese and emoji. After each input a "focus:" line says the view with the keyboard, a
# field's text, selection and composition, and whether the input method is on with its area at the caret (the system's
# composition and candidate windows show there). Clipboard: an offscreen run has SDL's own (not the system's).
CASES += [
    # The Files search box: a composition (its caret at its end, then after its first syllable), the commit, more text,
    # Backspace over an emoji, select, copy, paste, select all, cut, paste, then Undo twice.
    ("map-text-input", ["--workspace", "files", "--click", "240,289", "--type", "abc", "--ime", "\u314e", "--ime", "\ud55c",
                        "--ime", "\ud55c\u3131@1", "--type", "\ud55c\uad6d", "--type", " \u041f\u0440\u0438\u0432\u0435\u0442 \u4f60\u597d \U0001f600",
                        "--key", "Backspace", "--key", "Shift+Home", "--key", "Ctrl+C", "--key", "End", "--key", "Ctrl+V",
                        "--key", "Ctrl+A", "--key", "Ctrl+X", "--key", "Ctrl+V", "--key", "Ctrl+Z", "--key", "Ctrl+Z"], "map"),
    # The map's title (Properties) and the Marine's name (Map data > Units: the game text pop-out) in Korean, File > Save
    # as into a folder with a Korean name ({kodir}; the save dialog's answer, --pick: a name without .scx), the tab closed,
    # File > Open of that file: --check names says the title and the name that the map has again.
    ("map-text-korean-save", ["--workspace", "props", "--click", "420,214", "--key", "Ctrl+A", "--ime", "\ubcd1",
                              "--type", "\ubcd1\ubaa9 \uc9c0\ub3c4 \U0001f600", "--click", "30,190", "--click", "474,224",
                              "--ime", "\ub9c8\ub9b0", "--type", "\ub9c8\ub9b0 \ud574\ubcd1", "--key", "Tab", "--key", "Enter",
                              "--key", "Ctrl+Shift+S", "--pick", "{kodir}/\ubcd1\ubaa9 \uc9c0\ub3c4", "--key", "Ctrl+W",
                              "--key", "Ctrl+O", "--pick", "{kodir}/\ubcd1\ubaa9 \uc9c0\ub3c4.scx", "--check", "names"], "map"),
    # Unit properties (a double click on the unit): Tab into a number selects it, so that 50 takes its place; Tab and
    # Shift+Tab go through the fields in order.
    ("map-dialog-tab", ["--mode", "units", "--click", "825,128:Double", "--key", "Tab", "--type", "50", "--key", "Tab",
                        "--key", "Shift+Tab", "--key", "Escape"], "map"),
    # The Triggers tree takes typed letters (type-ahead) after a text field of the window had the keyboard: New trigger
    # (its name's field), Enter, then "o" goes to On every trigger cycle.
    ("data-tree-typeahead", ["--newproject", "{newdir}", "--workspace", "triggers", "--trig", "1", "--click", "433,169",
                             "--key", "Enter", "--wait", "1200", "--type", "o"], "data"),
    # The file and folder dialogs as the program asks for them (--pick: none shows, each is canceled): Add a script (a
    # save dialog in the project's scripts folder, new.eps) and Open project (in the projects' folder, its path ending
    # with a separator: else the dialogs of Windows and macOS take the folder's name as the file's).
    ("data-file-dialogs", ["--newproject", "{newdir}", "--command", "project.addscript", "--command", "project.open"], "data"),
]


# With no game data and no map (as on GitHub's machines): the Files workspace's search box takes Korean and Japanese
# compositions (each shows in the field, not in its text, until the commit), and emoji (one Backspace removes one).
CASES += [
    ("text-input-cjk", ["--noinstall", "--workspace", "files", "--click", "240,289", "--type", "abc", "--ime", "\u314e", "--ime", "\ud55c",
                        "--ime", "\ud55c\u3131@1", "--type", "\ud55c\uad6d", "--ime", "\u306b", "--ime", "\u306b\u307b\u3093",
                        "--ime", "\u65e5\u672c@1", "--type", "\u65e5\u672c\u8a9e", "--type", " \U0001f600\U0001f3ae", "--key", "Backspace"], ""),
]


# The release-candidate fixes: what is kept when StarDraft 2 ends, what a Save writes, Undo, the pop-outs.
CASES += [
    # Quit with a changed map in a tab that is not the active one (a new project's tab is active): the map's tab asks
    # (sdl_menus.inc: confirmQuit), and the program does not end (the box closes with no answer: Cancel).
    ("map-quit-tabs", ["--newproject", "{newdir}", "--script", "{script:quittabs}"], "map"),
    # Save as with the Properties page's title still being typed (no wait for its timer): the file has the title (the
    # tab closes with no question, and the map that opens again has it).
    ("map-save-typing", ["--workspace", "props", "--click", "420,214", "--key", "Ctrl+A", "--type", "Typed title", "--key", "Ctrl+Shift+S",
                         "--pick", "{out}/typed.scx", "--key", "Ctrl+W", "--key", "Ctrl+O", "--pick", "{out}/typed.scx", "--check", "names"], "map"),
    # The Briefing's text being typed in a saved map, then quit: the map has a change, so the quit asks.
    ("map-briefing-quit", ["--pick", "{out}/brief.scx", "--script", "{script:briefquit}"], "map"),
    # File > Save of a map in a Program Files folder asks for another place (Save as), as the Windows program did.
    ("map-program-files", ["--mode", "units", "--key", "Ctrl+Shift+S", "--pick", "{pfdir}/pf.scx", "--key", "Ctrl+A", "--key", "Del",
                           "--key", "Ctrl+S", "--pick", "cancel"], "map"),
    # Save as answers while the program waits (--pick defer:): a tab switch in that time does nothing, and the answer
    # saves the map that asked (not the other tab's map).
    ("map-saveas-tab", ["--newproject", "{newdir}", "--pick", "defer:{out}/asked.scx", "--script", "{script:saveastab}"], "map"),
    # Ctrl+Z while the mouse button still paints terrain does nothing; one Undo after the stroke takes all of it back.
    ("map-undo-midstroke", ["--mode", "terrain", "--click", "202,759", "--key", "B", "--drag", "520,650-640,690:Undo", "--key", "Ctrl+Z"], "map"),
    # Undo back to the map as it was opened: no change (no "*"); Redo: a change again.
    ("map-undo-saved", ["--mode", "units", "--click", "1386,429", "--click", "700,650", "--key", "Ctrl+Z", "--key", "Ctrl+Y"], "map"),
    # A unit deleted, another selected, Undo: the selection is still that other unit (not the unit that came back).
    ("map-undo-selection", ["--mode", "units", "--click", "825,128", "--key", "Del", "--click", "1240,580", "--key", "Ctrl+Z"], "map"),
    # A unit placed, then the Properties page's title typed and Edit > Undo at once: Undo takes the title back (the page
    # does not put it in again after the Undo), and the unit stays.
    ("map-props-typing-undo", ["--script", "{script:propsundo}", "--check", "names"], "map"),
    # Data: a Players setting (Can build), then a unit placed in Map, then Ctrl+Z in Data: the unit stays (Data's Undo
    # does not take back a Map edit that it cannot show).
    ("data-map-undo", ["--newproject", "{newdir}", "--workspace", "data", "--data", "units:0", "--script", "{script:datamapundo}"], "data"),
    # The Triggers' value pop-out: Insert value's picker, a click on its row: the value pop-out stays open and takes it.
    ("data-value-insert", ["--newproject", "{newdir}", "--workspace", "triggers", "--trig", "1", "--click", "433,169", "--click", "460,347",
                           "--type", "Display text", "--key", "Enter", "--click", "498,353", "--click", "568,632", "--type", "player's name",
                           "--click", "556,749", "--click", "996,635"], "data"),
    # Check for updates: a release published before this build's commit is not newer (a CI build of a later commit is
    # never offered an older release); a token that GitHub refuses (expired) gets the release without it.
    ("update-release-older", ["--check", "release:{releaseold}"], ""),
    ("update-release-token", ["--check", "release-token:{release}"], ""),
]


INPUT_START = re.compile(r"^(key|type|ime|click|wait) (.*?): (?:\d+ units|no map)")
FOCUS_LINE = re.compile(r'^  focus: (.+?) at (-?\d+),(-?\d+) "(.*?)"(?: selected (\d+)-(\d+) composing "(.*)"; input method (on|off) at '
                        r'.* (\(yes\)|\(WRONG\)))?$')


def focus_lines(output):
    """The focus line after each input: [(input, kind, text, (selection start, end) or None, composition)]."""
    out, last = [], None
    for line in output.splitlines():
        m = INPUT_START.match(line)
        if m:
            last = m.group(1) + " " + m.group(2)
        m = FOCUS_LINE.match(line)
        if m and last:
            sel = (int(m.group(5)), int(m.group(6))) if m.group(5) else None
            out.append((last, m.group(1), m.group(4), sel, m.group(7)))
    return out


def expect_focus(output, want):
    """None when the focus lines are `want`: [(input, text, selection or None, composition or None)] in order."""
    got = focus_lines(output)
    if len(got) != len(want):
        return f"{len(got)} focus lines, not {len(want)}: {got}"
    for (inp, kind, text, sel, comp), (w_inp, w_text, w_sel, w_comp) in zip(got, want):
        if inp != w_inp or text != w_text or (w_sel is not None and sel != w_sel) or (w_comp is not None and comp != w_comp):
            return f"{inp}: {kind} \"{text}\" selected {sel} composing \"{comp}\" (not \"{w_text}\" {w_sel} \"{w_comp}\")"
    return None


def check_text_input(output):
    """The composition shows in the field (not in its text) with the input method's area at the caret; the commit, the
    keys, the clipboard and Undo give the texts in order."""
    first = "abc\ud55c\uad6d \u041f\u0440\u0438\u0432\u0435\u0442 \u4f60\u597d "
    end = 18 if sys.platform == "win32" else 17  # (the places count wchar_t: UTF-16 on Windows, there the emoji is two)
    return expect_focus(output, [
        ("type abc", "abc", (3, 3), ""),
        ("ime \u314e", "abc", (3, 3), "\u314e"),
        ("ime \ud55c", "abc", (3, 3), "\ud55c"),
        ("ime \ud55c\u3131@1", "abc", (3, 3), "\ud55c\u3131"),
        ("type \ud55c\uad6d", "abc\ud55c\uad6d", (5, 5), ""),
        ("type  \u041f\u0440\u0438\u0432\u0435\u0442 \u4f60\u597d \U0001f600", first + "\U0001f600", (end, end), ""),
        ("key Backspace", first, (16, 16), ""),  # (the emoji: one Backspace)
        ("key Shift+Home", first, (0, 16), ""),
        ("key Ctrl+C", first, (0, 16), ""),
        ("key End", first, (16, 16), ""),
        ("key Ctrl+V", first + first, (32, 32), ""),
        ("key Ctrl+A", first + first, (0, 32), ""),
        ("key Ctrl+X", "", (0, 0), ""),
        ("key Ctrl+V", first + first, (32, 32), ""),
        ("key Ctrl+Z", "", (0, 0), ""),
        ("key Ctrl+Z", first + first, None, ""),
    ])


def check_text_cjk(output):
    """Korean and Japanese compositions show in the field until the commit; one Backspace removes one emoji. (The
    caret's place counts the text's wchar_t: UTF-16 units on Windows, where an emoji is two; elsewhere code points.)"""
    ko, ja = "abc\ud55c\uad6d", "abc\ud55c\uad6d\u65e5\u672c\u8a9e"
    emoji = 2 if sys.platform == "win32" else 1
    return expect_focus(output, [
        ("type abc", "abc", (3, 3), ""),
        ("ime \u314e", "abc", (3, 3), "\u314e"),
        ("ime \ud55c", "abc", (3, 3), "\ud55c"),
        ("ime \ud55c\u3131@1", "abc", (3, 3), "\ud55c\u3131"),
        ("type \ud55c\uad6d", ko, (5, 5), ""),
        ("ime \u306b", ko, (5, 5), "\u306b"),
        ("ime \u306b\u307b\u3093", ko, (5, 5), "\u306b\u307b\u3093"),
        ("ime \u65e5\u672c@1", ko, (5, 5), "\u65e5\u672c"),
        ("type \u65e5\u672c\u8a9e", ja, (8, 8), ""),
        ("type  \U0001f600\U0001f3ae", ja + " \U0001f600\U0001f3ae", (9 + 2 * emoji, 9 + 2 * emoji), ""),
        ("key Backspace", ja + " \U0001f600", (9 + emoji, 9 + emoji), ""),
    ])


def check_release(output):
    """None when Check for updates found the release of the answer (RELEASE_JSON): its title, commit and page, this
    system's download, and what it does with it: a release build installs it (Windows) or opens the disk image (macOS),
    an AppImage opens the new AppImage, a Flatpak tells that the package manager updates it, and a build made here (not
    in a package) opens the release page."""
    if not re.search(r'^release: "StarDraft 2 - v9\.9\.9\.9 \(build 0123abc\)", commit 0123abc, page https://github\.com/.*/releases/tag/v9\.9\.9\.9\r?$',
                     output, re.M):
        return "no release line with the title, the commit and the page"
    want = RELEASE_DOWNLOADS.get(sys.platform if sys.platform in ("win32", "darwin") else "linux")
    m = re.search(r"^release: this system's download \((\S+)\) (\S+)\r?$", output, re.M)  # (Windows: the lines end in \r\n)
    if not m or not m.group(2).endswith("/" + want):
        return f"this system's download is not {want}: {m.group(0) if m else 'no line'}"
    m = re.search(r"^release: newer yes, a release build (yes|no), packaged (none|AppImage|Flatpak), Check for updates: (.*?)\r?$", output, re.M)
    if not m:
        return "no line of what Check for updates does (or the release is not newer)"
    release, packaged, does = m.group(1) == "yes", m.group(2), m.group(3)
    page = "opens https://github.com/stardraft-editor/stardraft/releases/tag/v9.9.9.9"
    if packaged == "AppImage":
        expect = "opens " + RELEASE_DOWNLOAD + want
    elif packaged == "Flatpak":
        expect = "says that the package manager updates it"
    elif not release:
        expect = page
    elif sys.platform == "darwin":
        expect = "opens " + RELEASE_DOWNLOAD + want
    else:
        expect = None  # (a release build on Windows: it installs it, or opens the page when the file has another name)
    if expect and does != expect:
        return f"packaged {packaged}, a release build {m.group(1)}: Check for updates {does}, not {expect}"
    if not expect and does not in ("installs it", page):
        return f"a release build: Check for updates {does}"
    return None


def check_soak_nodata(output):
    """None when each time over of the session with no map printed the memory (each system says it) and the bitmaps
    with a texture stayed as many."""
    stats = re.findall(r"^stats: loop (\d+), .*?working set ([-\d.]+) MB .*?, (\d+) bitmaps ", output, re.M)
    if len(stats) < 2:
        return f"{len(stats)} stats lines, not 2 or more"
    if any(float(ws) <= 0 for _, ws, _ in stats):
        return "a stats line has no memory (the system did not say)"
    if int(stats[-1][2]) > int(stats[0][2]):
        return f"the bitmaps went up from {stats[0][2]} to {stats[-1][2]}"
    return None


def check_korean_save(output):
    """The title and the unit's name typed in Korean (with an emoji) are the map's again after Save as into a folder with
    a Korean name and File > Open of that file; the save dialog asked for a StarCraft map."""
    title, unit = "\ubcd1\ubaa9 \uc9c0\ub3c4 \U0001f600", "\ub9c8\ub9b0 \ud574\ubcd1"
    if not re.search(r"^dialog: save, title \"\", filters StarCraft maps \(scx;scm\)", output, re.M):
        return "no save dialog for a StarCraft map"
    m = re.search(r'^names: map "([^"]*)"', output, re.M)
    if not m or not m.group(1).endswith("\ubcd1\ubaa9 \uc9c0\ub3c4.scx") or "\ud55c\uad6d" not in m.group(1):
        return f"the map that opened: {m.group(1) if m else 'none'}"
    if f'names: title "{title}" (utf-8)' not in output:
        return "the title is not the one typed"
    if f'names: unit 0 "{unit}" (utf-8)' not in output:
        return "the Marine's name is not the one typed"
    return None


def check_dialog_tab(output):
    """Tab into a number selects it; the typed number takes its place; Tab and Shift+Tab select the next and the one before."""
    got = focus_lines(output)
    if len(got) < 4:
        return f"{len(got)} focus lines: {got}"
    (_, k1, t1, s1, _), (_, k2, t2, s2, _), (_, k3, t3, s3, _), (_, k4, t4, s4, _) = got[:4]
    if k1 != "text field" or s1 != (0, len(t1)) or not t1:
        return f"Tab: {k1} \"{t1}\" selected {s1} (not all of it)"
    if t2 != "50":
        return f"50 typed: \"{t2}\""
    if k3 != "text field" or s3 != (0, len(t3)) or t3 == t2:
        return f"Tab: {k3} \"{t3}\" selected {s3}"
    if t4 != "50" or s4 != (0, 2):
        return f"Shift+Tab: \"{t4}\" selected {s4}"
    return None


def check_tree_typeahead(output):
    """After the name's field, the tree took the typed "o"."""
    lines = states(output)
    typed = [st for k, st in lines if k == "type o"]
    if not typed or not typed[0].startswith('triggers: "On every trigger cycle"'):
        return f"type o: {typed}"
    return None


def check_file_dialogs(output):
    """Add a script: a save dialog with its title and filters, starting at scripts/new.eps; Open project: an open dialog
    for project files, starting in a folder (its path ends with a separator)."""
    dialogs = re.findall(r'^dialog: (\w+), title "(.*?)", filters (.*?), starts at "(.*)": (.*)$', output, re.M)
    if len(dialogs) != 2:
        return f"{len(dialogs)} dialogs, not 2: {dialogs}"
    (k1, t1, f1, at1, _), (k2, _, f2, at2, _) = dialogs
    if k1 != "save" or not t1.startswith("Add a script") or not f1.startswith("epScript (*.eps) (eps)"):
        return f"Add a script: {dialogs[0]}"
    if not re.search(r"[\\/]scripts[\\/]new\.eps$", at1):
        return f"Add a script starts at {at1}"
    if k2 != "open" or "(stardraft;eds)" not in f2 or not at2 or at2[-1] not in "\\/":
        return f"Open project: {dialogs[1]}"
    return None

# An update from an older release keeps the user's settings (--check settings: the values this run read). --settings
# gives the settings file of the last Win32 release, 0.5.0.8 ({oldsettings}: as its code wrote it); on Windows,
# --legacyhive gives the registry of the releases before the settings file, 0.5.0.7 and older ({legacyhive}: a registry
# file of its own, never the user's registry). --check update: the old program's file and the list of files to open
# again are gone; crashes and filetypes: the crash reports' state and the file types (read only).
CASES += [("update-settings", ["--settings", "{oldsettings}", "--check", "settings", "--check", "update"], ""),
          ("update-checks", ["--check", "crashes", "--check", "filetypes", "--check", "update"], ""),
          # Check for updates with a release of GitHub as the answer ({release}: RELEASE_JSON, no network): it finds the
          # release's commit and this system's download (Windows: the zip; macOS: the disk image; Linux: the AppImage),
          # and says what it does: a release build installs it (Windows) or opens the download (macOS), an AppImage opens
          # the new AppImage, a Flatpak tells that its package manager updates it, a build made here opens the release
          # page.
          ("update-release", ["--check", "release:{release}"], "")]
if sys.platform == "win32":
    CASES += [("update-registry", ["--settings", "{out}/no-settings.json", "--legacyhive", "{legacyhive}", "--check", "settings"], "")]


# A long session (--script <file>[:<times>], apps/editor/sdl_soak.inc): the inputs of a file, the times over. The edits of
# each mode on (4)Bottleneck (the points above), the zoom, then "undocheck" (the undo steps undone and redone: each redo
# gives the map again as it was) and the save round trip ({n}: the time over); "stats" says the memory and the undo
# steps. reopen: the project's Data workspace, the project closes and opens again: the pictures of the closed project go.
# dialogs: F2's prompt renames the location, 300 times (each prompt is a window of the snapshot: more than 255 of them).
SCRIPTS = {
    "dialogs": """key F2
type Spot {n}
key Enter
""",
    "session": """mode units
click 1386,429
click 700,650
key Esc
drag 780,110-880,150
key Ctrl+C
key Ctrl+V
move 700,650
click 700,650
key Esc
click 825,128
drag 825,128-825,168
key Ctrl+Z
key Ctrl+Y
mode doodads
click 115,600
click 559,590
key Esc
click 559,590
key Del
key Ctrl+Z
mode locations
drag 520,640-600,700
mode terrain
click 202,759
key B
drag 520,650-640,690
key V
drag 520,640-600,700
key Ctrl+C
key Ctrl+V
move 900,800
click 900,800
key Esc
key Ctrl+Z
key Esc
rect
click 160,660
key B
click 560,690
rect off
mode units
wheel 800,500,0,1
wheel 800,500,0,-1
undocheck
roundtrip {out}/session{n}.scx
stats
""",
    # nodata: no game data and no map (as on GitHub's machines): the start page's scroll and keys, the workspaces, the
    # Files workspace's search box, the Log. tools/ci/mac_release_test.py runs it for 10 minutes (<file>:10m).
    "nodata": """workspace map
wheel 500,300,0,-3
wheel 500,300,0,3
wheel 500,300,0,-3:precise
key Tab
key Tab
key Shift+Tab
workspace files
click 240,289
type marine {n}
key Ctrl+A
key Backspace
wait 200
workspace data
workspace triggers
workspace project
workspace briefing
workspace props
workspace classic
key Ctrl+L
key Ctrl+L
workspace map
stats
""",
    "quittabs": """key Ctrl+Shift+Tab
mode units
key Ctrl+A
key Del
key Ctrl+Tab
command file.exit
""",
    "briefquit": """workspace briefing
click 543,166
type Text message
key Enter
key Ctrl+Shift+S
click 510,840
type Hello there
command file.exit
key Esc
""",
    "saveastab": """key Ctrl+Shift+Tab
mode units
key Ctrl+A
key Del
key Ctrl+Shift+S
key Ctrl+Tab
""",
    "propsundo": """mode units
click 1386,429
click 700,650
workspace props
click 420,214
key Ctrl+A
type Undo title
command edit.undo
wait 700
""",
    "datamapundo": """wheel 800,500,0,-300
click 714,697
workspace map
mode units
click 1386,429
click 700,650
workspace data
click 900,170
key Ctrl+Z
""",
    "reopen": """workspace data
wait 0
workspace map
key Ctrl+W
key Esc
open {project}
stats
""",
}


def check_session(output):
    """None when each time over of the session undid and redid its steps to the same map and saved a map that opens
    the same, and the undo steps went up; else the reason."""
    undo = re.findall(r"^undocheck: (\d+) steps undone and redone, (\d+) undo steps: the same map \(yes\)", output, re.M)
    saved = re.findall(r"^save round-trip: session\d\.scx: .* the saved map is the same \(yes\)", output, re.M)
    steps = [int(s) for s in re.findall(r"^stats: .*; 128x128, \d+ units, \d+ doodads, undo (\d+),", output, re.M)]
    if len(undo) != 3 or any(int(n) == 0 for n, _ in undo):
        return f"{len(undo)} good undo checks, not 3"
    if len(saved) != 3:
        return f"{len(saved)} good round trips, not 3"
    if len(steps) != 3 or not steps[0] < steps[1] < steps[2]:
        return f"the undo steps of the stats lines: {steps}"
    return None


def check_dialogs(output):
    """None when each of the 300 prompts renamed the location (the last name is Spot 300); else the reason."""
    names = re.findall(r'^key F2: .*\n  state: .* name="([^"]*)"', output, re.M)
    if len(names) != 300 or names[-1] != "Spot 300":
        return f"{len(names)} renames, the last name {names[-1] if names else None}, not 300 and Spot 300"
    return None


def check_reopen(output):
    """None when the project opened again each time and the bitmaps with a texture stayed as many (the closed project's
    Data pictures went); else the reason."""
    # (the map's images load on threads, and the loaded ones stay for the next open: so the counts are compared only
    # between the opens whose first frame had the same images still loading, 24 or 0)
    found = re.findall(r"^first frame: [\d.]+ ms \((\d+) images still loading\)\s*\nstats: .*, (\d+) bitmaps [\d.]+ MB; 1 tabs; ", output, re.M)
    if len(found) != 6:
        return f"{len(found)} stats lines with one tab after an open, not 6"
    by_pending = {}
    for pending, count in found[1:]:
        by_pending.setdefault(int(pending), []).append(int(count))
    for pending, counts in by_pending.items():
        if counts[-1] > counts[0]:
            return f"the bitmaps went up from {counts[0]} to {counts[-1]} (the closed project's pictures stay; {pending} images loading)"
    return None


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


INPUT_LINE = re.compile(r"^(key|type|click|drag|lostdrag|move|wait) (.*?): (\d+) units, (\d+) doodads, (\d+) selected(?: \(the first at (\d+),(\d+)\))?:")


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


def check_terrain_lostdrag(output):
    """A Shift stroke with the second terrain ends when the window loses the focus: one undo step, the first pick again."""
    return expect(output, 5, [
        ("Ctrl+click picked no second terrain", lambda s: "Shift+drag paints" in output),
        ("the stroke did not paint", lambda s: s[2]["terrain"] != s[1]["terrain"]),
        ("the first terrain is not the pick after the stroke", lambda s: s[2]["palette"] == s[1]["palette"]),
        ("the stroke did not end: the mouse paints after the focus went", lambda s: s[3]["terrain"] == s[2]["terrain"]),
        ("one Undo did not take the stroke back", lambda s: s[4]["terrain"] == s[1]["terrain"]),
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


def check_terrain_keys(output):
    """None when 1, 3 and 0 in Terrain mode each picked another terrain (Painting ...) and the player stayed 1."""
    s = states(output)
    if [i for i, _ in s] != ["key 1", "key 3", "key 0"]:
        return f"input lines {[i for i, _ in s]}, not keys 1, 3, 0"
    picks = []
    for i, st in s:
        m = re.search(r" palette=(-?\d+)", st)
        if "mode=Terrain" not in st or " player=1 " not in st or not m or int(m.group(1)) < 0:
            return f"{i}: {st or 'no state'} (not Terrain mode, player 1 and a terrain picked)"
        picks.append(int(m.group(1)))
    if len(set(picks)) != 3:
        return f"the keys picked {picks} (not three terrains)"
    names = re.findall(r"^key [130]: .*: Painting (.+?)\r?$", output, re.M)
    if len(names) != 3:
        return f"{len(names)} Painting messages, not 3"
    return None


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
    if not re.search(r"^click 489,517: no map:", output, re.M):
        return "a map was open before Create"
    return None


NEWPAGE_STATE = re.compile(r"^new page: (.*); (\d+) randomize presses, (\d+) maps made, the last for (the same choices|other choices .*); "
                           r"the symmetry list (open|closed)$")


def newpage_states(output):
    """The New page's state after each input: [(input, choices, presses, maps, same, list open)]."""
    out = []
    for what, st in states(output):
        m = NEWPAGE_STATE.match(st)
        if not m:
            return None
        out.append((what, m.group(1), int(m.group(2)), int(m.group(3)), m.group(4) == "the same choices", m.group(5) == "open"))
    return out


def check_newpage_randomize(output):
    """None when each of the 8 quick clicks on each Randomize button randomized, the page shows new values, and the
    wait after them made one map, for the values shown."""
    s = newpage_states(output)
    if not s or len(s) != 8:
        return f"{len(s) if s else 0} New page state lines, not 8"
    if s[1][3] < 1 or not s[1][4]:
        return f"after Generate: {s[1]} (no map for the choices)"
    for k in range(3):
        before, click, wait = s[1 + 2 * k], s[2 + 2 * k], s[3 + 2 * k]
        if click[2] - before[2] != 8:
            return f"{click[0]}: {click[2] - before[2]} of the 8 clicks randomized"
        if click[1] == before[1]:
            return f"{click[0]}: the values did not change ({click[1]})"
        if wait[1] != click[1]:
            return f"the wait after {click[0]}: the values changed ({wait[1]}, not {click[1]})"
        if wait[3] - before[3] != 1 or not wait[4]:
            return f"the wait after {click[0]}: {wait[3] - before[3]} maps made (not 1), for {'the same' if wait[4] else 'other'} choices"
    if " era 0 " not in s[4][1] or " shape 30 75 180 35" not in s[3][1]:
        return "Randomize seed changed more than the seed, or the tileset changed before Randomize all"
    return None


def check_newpage_symmetry(output):
    """None when a click on Symmetry opened its list, Down, Down, Enter picked Mirror top and bottom (2) and closed it,
    the wait made the map with it, and the keys after Blank did not change the dropdown that is not shown."""
    s = newpage_states(output)
    if not s or len(s) != 10:
        return f"{len(s) if s else 0} New page state lines, not 10"
    if not s[2][5] or " symmetry 0 " not in s[2][1]:
        return f"after the click on Symmetry: the list is {'open' if s[2][5] else 'closed'} ({s[2][1]})"
    if s[5][5] or " symmetry 2 " not in s[5][1]:
        return f"after Enter: the list is {'open' if s[5][5] else 'closed'} ({s[5][1]}; not symmetry 2)"
    if s[6][3] - s[5][3] != 1 or not s[6][4]:
        return f"after the wait: {s[6][3] - s[5][3]} maps made (not 1) for {'the same' if s[6][4] else 'other'} choices"
    for st in s[7:]:
        if " blank " not in st[1] or " symmetry 2 " not in st[1]:
            return f"{st[0]}: {st[1]} (not blank with symmetry 2)"
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


def quick_check(want, what):
    def check(output):
        s = states(output)
        if len(s) != 1:
            return f"{len(s)} input lines, not 1"
        if want not in s[0][1]:
            return f"{what}: {s[0][1]} (no {want})"
        return None
    return check


def check_double_click_rows(sel, plays):
    """None when the two quick clicks on two rows selected the second (no play) and the double click played one sound."""
    def check(output):
        s = states(output)
        if len(s) != 3:
            return f"{len(s)} input lines, not 3"
        if sel[0] not in s[1][1] or "0 plays" not in s[1][1]:
            return f"two quick clicks on two rows: {s[1][1]} (not {sel[0]} with no play)"
        if sel[1] not in s[2][1] or plays not in s[2][1]:
            return f"a double click on one row: {s[2][1]} (not {sel[1]}, {plays})"
        return None
    return check


def check_caret_lines(want):
    """None when each field's caret is at the end of its drawn letters after the long text, in `want` fields."""
    def check(output):
        lines = re.findall(r'focus: (?:text|rich) field at [^"]*"(\w*)" \[caret at (\d+), the letters end at (\d+) \((yes|WRONG)\)\]', output)
        long_ = [m for m in lines if len(m[0]) >= 40]
        if len(long_) != want:
            return f"{len(long_)} fields with the long text, not {want}"
        for text, caret, end, ok in long_:
            if ok != "yes":
                return f"the caret at {caret}, the letters end at {end} (after {len(text)} letters)"
        return None
    return check


def check_no_still_handles(output):
    """None when no diagram handle is on a part that the mouse cannot drag."""
    m = re.search(r"; (\d+) handles, (\d+) not dragged", output)
    if m and m.group(2) != "0":
        return f"{m.group(2)} of {m.group(1)} handles cannot be dragged"
    if not re.search(r"^move 10,10: ", output, re.M):
        return "no input line"
    return None


def check_range_handles(output):
    """None when the weapon's diagrams have handles and each one drags."""
    m = re.search(r"; (\d+) handles, (\d+) not dragged", output)
    if not m or m.group(1) == "0":
        return "no handles (the state line does not count them)"
    if m.group(2) != "0":
        return f"{m.group(2)} of {m.group(1)} handles cannot be dragged"
    return None


def check_location_inverted(output):
    """None when the Type list followed the typed edges, a picked type swapped the edges (the same numbers), OK put
    them in as they were and Undo took them back."""
    s = steps(output)
    by = {st["input"]: st for st in s}
    typed = [(st.get("locedges"), st.get("locprops")) for st in s if st["input"].startswith("type ")]
    want = [("0,2592,928,2880", "Normal"), ("0,0,928,2880", "Normal"), ("0,0,32,2880", "Normal"), ("0,0,32,32", "Normal"),
            ("64,0,32,32", "Inverted left and right")]
    if typed != want:
        return f"the edges and the Type list after each typed edge: {typed}"
    picked = [(st.get("locedges"), st.get("locprops")) for st in s if st["input"] in ("key Up", "key Down")]
    want = [("32,0,64,32", "Normal"), ("64,0,32,32", "Inverted left and right"), ("32,32,64,0", "Inverted top and bottom")]
    if picked != want:
        return f"the edges after each type picked in the list: {picked}"
    enter = [st for st in s if st["input"] == "key Enter"]
    if len(enter) != 2 or enter[1].get("edges") != "32,32,64,0" or "locprops" in enter[1]:
        return f"after OK: edges {enter[-1].get('edges') if enter else None} (not 32,32,64,0 as in the dialog)"
    undo = by.get("key Ctrl+Z")
    if not undo or undo.get("edges") != s[0].get("edges"):
        return f"Undo: edges {undo.get('edges') if undo else None}, not {s[0].get('edges')}"
    return None


def check_picker_typing(output):
    """None when the picker's search took the typed text, the composition, Korean and Backspace, and filtered its rows."""
    seen = []
    cur = None
    for line in output.splitlines():
        m = re.match(r"^(key|type|ime|click) (.*?): ", line)
        if m:
            cur = {"input": f"{m.group(1)} {m.group(2)}"}
            seen.append(cur)
        elif cur is not None and line.startswith("  picker: "):
            pm = re.match(r'  picker: search "(.*)", (\d+) rows: ?(.*)$', line)
            if pm:
                cur["search"], cur["rows"], cur["names"] = pm.group(1), int(pm.group(2)), pm.group(3)
        elif cur is not None and line.startswith("  focus: "):
            cm = re.search(r'composing "(.*?)"', line)
            cur["composing"] = cm.group(1) if cm else None
    want = [("type Med", "Med", 1, "Medium"), ("ime \ud55c", "Med", 1, "Medium"), ("type \ud55c", "Med\ud55c", 0, ""),
            ("key Backspace", "Med", 1, "Medium")]
    for i, (inp, search, rows, names) in enumerate(want):
        if i >= len(seen) or seen[i]["input"] != inp:
            return f"input {i + 1}: {seen[i]['input'] if i < len(seen) else 'none'}, not {inp}"
        got = seen[i]
        if got.get("search") != search or got.get("rows") != rows or got.get("names") != names:
            return f"after {inp}: search {got.get('search')!r}, {got.get('rows')} rows {got.get('names')!r} (not {search!r}, {rows} {names!r})"
    if seen[1].get("composing") != "\ud55c":
        return f"the input method's text is not in the search box: {seen[1].get('composing')!r}"
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


def check_start_keys(output):
    """None when the start page's keys went as the rule says (from each card or Browse: Tab, Shift+Tab, Left, Right),
    and Browse was reached with Tab and with Shift+Tab."""
    steps = re.findall(r"^key (\S+): start page: keys on (.+?)\r?$", output, re.M)
    if len(steps) != 11:
        return f"{len(steps)} key lines, not 11"
    rule = {("card 0", "Tab"): "card 1", ("card 1", "Tab"): "Browse", ("Browse", "Tab"): "card 0",
            ("card 0", "Shift+Tab"): "Browse", ("card 1", "Shift+Tab"): "card 0", ("Browse", "Shift+Tab"): "card 1",
            ("card 0", "Right"): "card 1", ("card 1", "Right"): "card 1", ("Browse", "Right"): "card 1",
            ("card 0", "Left"): "card 0", ("card 1", "Left"): "card 0", ("Browse", "Left"): "card 0"}
    was, reached = "none", set()
    for key, now in steps:
        want = rule.get((was, key))
        if want and now != want:
            return f"{key} on {was} went to {now}, not {want}"
        if want == "Browse":
            reached.add(key)
        was = now
    if reached != {"Tab", "Shift+Tab"}:
        return f"Browse was reached only with {sorted(reached)}"
    return None


def check_scale2(output):
    """None when at scale 2 the UI text is 30 px and the small text (the rail's labels) two thirds of it (20 px), and
    the icons are sharp: a 16 px icon from the 32 px atlas, the Start page's card icons from the 64 px atlas."""
    m = re.search(r"text (\d+) px, small text (\d+) px, titles (\d+) px", output)
    if not m:
        return "no text sizes line"
    ui, small, title = map(int, m.groups())
    if (ui, small, title) != (30, 20, 36):
        return f"text {ui} px, small {small} px, titles {title} px; not 30, 20 and 36"
    return check_icons(32, 32, 64, 64)(output)


def check_icons(icon, icon_cell, card, card_cell):
    """A check of the line of the icons' sizes: a 16 px icon and the Start page's 32 px card icon (each at the scale)
    from atlas cells of their own size (sharp, not scaled)."""
    def check(output):
        m = re.search(r"icons (\d+) px from the (\d+) px atlas, card icons (\d+) px from the (\d+) px atlas", output)
        if not m:
            return "no icons line"
        got = tuple(map(int, m.groups()))
        if got != (icon, icon_cell, card, card_cell):
            return f"icons {got}, not {(icon, icon_cell, card, card_cell)}"
        return None
    return check


def check_fonts(output):
    """On macOS the UI text is SF Pro (SFNS.ttf), drawn as macOS draws text; on Windows Segoe UI (the system's), at the
    system's message font size; elsewhere Noto Sans, as before."""
    m = re.search(r"^stardraft_editor: fonts (.*), scale", output, re.M)
    if not m:
        return "no fonts line"
    fonts = m.group(1)
    if sys.platform == "darwin":
        if not fonts.startswith("SFNS") or "rasterizer: macOS" not in fonts:
            return f"the UI font is not SF Pro with macOS's rasterizer settings: {fonts}"
    elif sys.platform == "win32" and not fonts.startswith("segoeui.ttf"):
        return f"the UI font is not Segoe UI: {fonts}"
    elif "SFNS" in fonts or "rasterizer" in fonts:
        return f"SF Pro or macOS's rasterizer on another system: {fonts}"
    return None


def check_start(output):
    """The start page at scale 1: Noto Sans or SF Pro (check_fonts), and the icons at their sizes."""
    return check_fonts(output) or check_icons(16, 16, 32, 32)(output)


def check_trackpad_scroll(output):
    """None when macOS's trackpad scroll moved the start page by its exact distance (30 px), not by notches."""
    m = re.search(r"^wheel 500,300,0,-3:precise:mac: scroll (\S+) -> (\S+)\r?$", output, re.M)
    if not m:
        return "no scroll line"
    if m.groups() != ("0", "30"):
        return f"the start page scrolled {m.group(1)} -> {m.group(2)}, not 0 -> 30"
    return None


def check_smartzoom(output):
    """None when the first smart zoom zoomed in two times at the mouse, and the second gave the zoom before."""
    lines = re.findall(r"^smartzoom 700,500: at map .*\r?$", output, re.M)
    if len(lines) != 2:
        return f"{len(lines)} smart zoom lines, not 2"
    first = check_zoom_at("smartzoom 700,500", 2)(lines[0])
    if first:
        return "the first: " + first
    (_, (cx0, cy0, z0), _), (_, _, (cx1, cy1, z1)) = camera(lines[0], "smartzoom 700,500"), camera(lines[1], "smartzoom 700,500")
    if abs(z1 - z0) > 1e-4 or abs(cx1 - cx0) > 0.5 or abs(cy1 - cy0) > 0.5:
        return f"the second did not give the camera before ({cx0},{cy0} zoom {z0} -> {cx1},{cy1} zoom {z1})"
    return None


def check_swipe(output):
    """None when the swipes went to the map's tab, to the project's, then (two fingers on the strip) to the map's."""
    got = re.findall(r"^(?:swipe|wheel) [^:]*(?::precise:mac)?: .*: Switched to .* \((map|project)\)\r?$", output, re.M)
    if got != ["map", "project", "map"]:
        return f"the tabs after the swipes: {got}, not map, project, map"
    return None


def camera(output, what):
    """The point in the map view and the camera before and after (x, y, zoom) of the --wheel or --pinch line."""
    f = r"(-?[\d.]+)"
    m = re.search(rf"^{re.escape(what)}: at map {f},{f}; camera {f},{f} zoom {f} -> {f},{f} zoom {f}\r?$", output, re.M)
    if not m:
        return None
    v = list(map(float, m.groups()))
    return (v[0], v[1]), (v[2], v[3], v[4]), (v[5], v[6], v[7])


def check_brush_pinch(output):
    """None when Alt + pinch made the brush larger, then smaller, and the zoom stayed."""
    sizes = [float(m) for m in re.findall(r"^pinch 700,500,[\d.]+:Alt: .*Brush size ([\d.]+)\r?$", output, re.M)]
    if len(sizes) != 2:
        return f"{len(sizes)} brush sizes after the pinches, not 2"
    if not sizes[0] > 2 or not sizes[1] < sizes[0]:
        return f"brush sizes {sizes} (not larger than 2, then smaller)"
    for what in ("pinch 700,500,1.5:Alt", "pinch 700,500,0.6:Alt"):
        c = camera(output, what)
        if not c or abs(c[2][2] - c[1][2]) > 1e-4:
            return f"{what}: the zoom changed"
    return None


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


def check_window(system):
    """--check window: the lines that sdl_window.inc's sdlWindowCheck prints."""
    def check(output):
        lines = [x for x in output.splitlines() if x.startswith("window: ")]
        text = "\n".join(lines)
        if not lines:
            return "no window lines"
        sheet = next((x for x in lines if x.startswith("window: sheet ")), "")
        if not re.search(r'"Save changes to the map\?": Save \[Return\] Cancel \[Escape\] Do not save \[Cmd\+n\]', sheet):
            return f"the save question's sheet is not Save, Cancel, Do not save: {sheet!r}"
        for color, accent in (("blue", "Blue"), ("red", "Red"), ("green", "Green"), ("graphite", "Blue")):
            if f"window: system accent {color} -> {accent}" not in text:
                return f"the system's {color} does not give {accent}"
        if "window: system accent purple -> hue " not in text:
            return "the system's purple does not keep its own hue"
        mac = "window: system look yes" in text and sys.platform == "darwin"
        if not mac:
            if "window: not macOS" not in text:
                return "no 'not macOS' line"
            if "window: system look yes (the desktop's)" in text:  # (Linux: the desktop portal's look)
                if not re.search(r"^window: system appearance (dark|light), desktop portal ", text, re.M):
                    return "no system appearance line"
                if system and "(the system's)" not in next((x for x in lines if x.startswith("window: theme ")), ""):
                    return "the theme is not the system's"
            return None
        win = next((x for x in lines if x.startswith("window: represented file ")), "")
        if not win or win.startswith("window: represented file none"):
            return f"the window has no file: {win!r}"
        for part in ("edited yes", "full screen yes", "split view yes", "resizable yes"):
            if part not in win:
                return f"the window does not have '{part}': {win!r}"
        if not re.search(r"window: rail color \d+,\d+,\d+", text):
            return "no rail color"
        if system and "(the system's)" not in next((x for x in lines if x.startswith("window: theme ")), ""):
            return "the theme is not the system's"
        return None
    return check


def check_pan(what, rx, ry):
    """A check that a trackpad's scroll moved the map, not the zoom: the camera moved in screen pixels as (rx, ry)
    times one unit (macOS's: its tenths of points at the display's density), the map the other way."""
    def check(output):
        c = camera(output, what)
        if not c:
            return f"no camera line for {what}"
        _, (cx0, cy0, z0), (cx1, cy1, z1) = c
        if abs(z1 - z0) > 1e-4:
            return f"the zoom changed ({z0} -> {z1})"
        dx, dy = (cx1 - cx0) * z0, (cy1 - cy0) * z0  # (the camera's move in screen pixels: the map moves the other way)
        unit = max(abs(dx), abs(dy)) / max(abs(rx), abs(ry))
        if unit < 1 or abs(dx - rx * unit) > 1 or abs(dy - ry * unit) > 1:
            return f"the camera moved {dx:.1f},{dy:.1f} screen px, not as {rx},{ry}"
        return None
    return check


def per_system(mac, other):
    """macOS's check (a precise scroll is a trackpad's), else the one of Windows and Linux (it is a smooth wheel's)."""
    return mac if sys.platform == "darwin" else other


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
    snap = [l for l in lines if l.startswith("Options > Units > Snap buildings to tile edges")]
    if not snap or "(checked)" not in snap[0]:
        return f"no checked item Options > Units > Snap buildings to tile edges: {snap}"
    if tips[0] != tip:
        return f"the tooltip's keys are {tips[0]!r}, not {tip!r}"
    return None


def check_drop(want):
    """A check that the drop's line ("drop: <the status bar>") has `want`."""
    def check(output):
        m = re.search(r"^drop: (.*?)\r?$", output, re.M)
        if not m:
            return "no drop line"
        return None if want in m.group(1) else f"drop: {m.group(1)!r}, not {want!r}"
    return check


def check_dock(output):
    """None when on macOS the Dock menu, Build's tile and the Settings toolbar are set up (elsewhere: macOS only)."""
    if "(WRONG)" in output:
        return "a line says WRONG"
    if "dock: dialog waits: no; the menu's items and the drops open" not in output:
        return "no dialog waits line"
    if sys.platform != "darwin":
        return None if "dock: not on this system (macOS only)" in output else "no macOS only line"
    wants = ["\"(2)Bottleneck.scm\" in the system's recent documents: not in the menu again; 3 items",
             "dock: while a dialog waits: 0 of 3 items can be clicked", "application:openFile: yes",
             "applicationDockMenu: yes; 4 items: \"(2)Bottleneck.scm\" \"(4)Lost Temple.scx\" | "
             "\"AshenCrown\"", "dock: build running: tile shows the bar, timer on",
             "dock: build ended with 3 errors: badge \"3\", tile is the icon, timer off",
             "settings: toolbar style preference; 4 items: \"General\" \"Map\" \"Start\" \"Folders\"; selected"]
    for w in wants:
        if w not in output:
            return f"no {w!r}"
    return None


def check_options_tab(output):
    """None when the tab's content fits in the picture (520 px): macOS's Settings window is as high as it."""
    m = re.search(r"^options tab (\d+): content (\d+) px high\r?$", output, re.M)
    if not m:
        return "no options tab line"
    return None if 100 < int(m.group(2)) <= 520 else f"the tab is {m.group(2)} px high"
def check_bundle(output):
    """None when the app bundle (macOS) has the map types, the Quick Look extensions in their sandbox; or not macOS."""
    if "bundle: not macOS" in output:
        return None
    lines = [x for x in output.splitlines() if x.startswith("bundle: ")]
    for ext in ("scm", "scx"):
        uti = "io.github.stardraft-editor.map-" + ext
        if not any(x.startswith(f"bundle: exported type {uti} ({ext})") and "icon text " + ext in x for x in lines):
            return f"no exported type {uti} with its icon"
        if not any(x.startswith("bundle: document type ") and f"({uti})" in x and "role Editor" in x for x in lines):
            return f"no document type for {uti}"
    for name, point in (("StarDraftPreview.appex", "com.apple.quicklook.preview"),
                        ("StarDraftThumbnail.appex", "com.apple.quicklook.thumbnail")):
        if not any(x.startswith(f"bundle: plug-in {name} {point} types ") and "map-scx" in x for x in lines):
            return f"no plug-in {name} ({point})"
        if not any(x.startswith(f"bundle: {name} entitlements: ") and "com.apple.security.app-sandbox" in x for x in lines):
            return f"{name} is not signed with its sandbox"
    return None


def check_desktop(output):
    """None when on Linux the app id, the XDG folders and the recently used files' entry are right; elsewhere: Linux only."""
    if "desktop: not on this system (Linux and the BSDs only)" in output:
        return None if sys.platform in ("win32", "darwin") else "Linux says that it is not Linux"
    app = "io.github.stardraft_editor.StarDraft"
    if f"desktop: app id {app}; SDL app id {app}" not in output:
        return "the app id is not the .desktop file's name"
    for line in ("desktop: settings ", "desktop: data ", "desktop: tile colors "):
        m = re.search("^" + line + r"(\S.*?)\r?$", output, re.M)
        if not m or not m.group(1).startswith("/"):
            return f"no absolute path for {line.strip()}"
    if not re.search(r"^desktop: recently used written, 1 entry, type application/x-starcraft-sc[mx] \(yes\)", output, re.M):
        return "no recently used entry"
    return None


def check_preview(output):
    """None when the map's Quick Look picture was drawn with its tile color table and its terrain is the minimap's."""
    if not re.search(r"^preview: \d+ x \d+ tiles, .* table yes", output, re.M):
        return "no picture with the table"
    if not re.search(r"^preview: the terrain is the minimap's: .*\(yes\)\s*$", output, re.M):
        return "the terrain is not the minimap's"
    return None


def check_spotlight(output):
    """None when the map's Spotlight entry has its title and picture and was indexed and removed; or not macOS."""
    if "spotlight: not macOS" in output:
        return None
    if not re.search(r"^spotlight: title .+", output, re.M) or not re.search(r"^spotlight: picture [1-9]\d* bytes", output, re.M):
        return "no entry with a title and a picture"
    if "spotlight: indexing not available" in output:
        return None  # (a Mac whose Spotlight is off)
    if "spotlight: indexed yes" not in output or "spotlight: removed yes" not in output:
        return "not indexed and removed"
    return None


def check_settings_read(expected):
    """None when each expected "settings: ..." line is in the output (--check settings)."""
    def check(output):
        lines = set(line.rstrip() for line in output.splitlines())
        missing = [e for e in expected if e not in lines]
        return "not read: " + "; ".join(missing) if missing else None
    return check


# The settings file of 0.5.0.8 that write_old_settings writes: the lines of --check settings (and the working folder).
OLD_SETTINGS_LINES = ["settings: language es", "settings: theme light, accent Red", "settings: Quality 1", "settings: Carbot 1",
                      "settings: ShowGrid 1", "settings: AllowStacking 1", "settings: UnitGrid 16", "settings: BrushShape 1",
                      "settings: LayerDoodads 0", "settings: PaletteViews 12345",
                      "settings: layout minimap right hidden; Terrain right, Units left, Doodads right, Locations right, Sprites right",
                      "settings: layout widths 411 248 333, log 294, grid 48x16+3+5, trigger zoom 90, volume 40, unit names 1, code pane yes, "
                      "script preview yes",
                      "settings: recent map C:\\Maps\\(2)Bottleneck.scm", "settings: recent map /home/user/maps/Ice \uc5bc\uc74c.scx",
                      "settings: recent project C:\\StarDraft\\AshenCrown", "settings: unit limits 2000 1500",
                      "settings: open the last project yes, look for updates no, crash reports 2, middle button drags yes, asked about maps yes",
                      "settings: data pins units 0,7,37"]


def check_update_settings(output):
    """None when the settings of 0.5.0.8 were read, and the update left nothing behind."""
    problem = check_settings_read(OLD_SETTINGS_LINES)(output)
    if problem:
        return problem
    if not re.search(r"^settings: working folder .+[\\/]projects\s*$", output, re.M):  # (the --out folder's projects)
        return "not read: the working folder"
    if not re.search(r"^update: .*gone \(yes\), .*gone \(yes\)", output, re.M):
        return "no update line"
    return None


def check_update_checks(output):
    """None when the crash reports, the file types and the update each printed their lines."""
    for prefix in ("crashes: ", "filetypes: ", "update: "):
        if not re.search("^" + re.escape(prefix), output, re.M):
            return "no " + prefix.strip() + " lines"
    return None


# The registry of 0.5.0.7 that write_legacy_hive writes: the lines of --check settings.
LEGACY_LINES = ["settings: language ko", "settings: theme light, accent Green", "settings: Quality 0", "settings: ShowGrid 1",
                "settings: BrushShape 1", "settings: PaletteViews 777", "settings: layout minimap right; Terrain left, Units left, Doodads left, "
                "Locations left, Sprites left",
                "settings: layout widths 350 280 300, log 220, grid 64x32+0+0, trigger zoom 150, volume 25, unit names 2, code pane no, "
                "script preview no",
                "settings: recent map C:\\Maps\\(2)Bottleneck.scm", "settings: recent map C:\\Maps\\Ice \uc5bc\uc74c.scx",
                "settings: recent project C:\\StarDraft\\AshenCrown", "settings: working folder C:\\StarDraft Projects",
                "settings: unit limits 2500 1200",
                "settings: open the last project yes, look for updates no, crash reports 1, middle button drags yes, asked about maps yes",
                "settings: data pins units 5,6", "settings: data pins weapons 1"]


# Checks of a case's output (after the PNG's): a function that returns None, or what is wrong.
def check_quit_tabs(output):
    """None when the quit asked about the changed map in the other tab, and StarDraft 2 did not end (no answer: Cancel)."""
    asked = re.findall(r"^question: Save changes to the map\? \(tab (.*)\)\r?$", output, re.M)
    if len(asked) != 1 or not asked[0].startswith("* ") or asked[0].endswith("Sample"):
        return f"the quit asked {len(asked)} times about {asked} (not once about the changed map's tab)"
    if re.search(r"^quit: ", output, re.M):
        return "StarDraft 2 ended without an answer (the changes of the other tab are lost)"
    return None


def check_save_typing(output):
    """None when the title being typed went into the saved file: the tab closed with no question and the map opened again
    has the title."""
    if "question:" in output:
        return "the tab asked to be saved after Save as (the typed title was not in the saved map)"
    if not re.search(r"^dialog: open", output, re.M):
        return "Ctrl+O did not show the open dialog"
    if not re.search(r'^names: title "Typed title"', output, re.M):
        return "the map opened again does not have the typed title"
    return None


def check_briefing_quit(output):
    """None when the quit after the Briefing text was typed in a saved map asked to save it (and did not end)."""
    if not re.search(r"^question: Save changes to the map\?", output, re.M):
        return "the quit did not ask: the typed Briefing text was not a change of the map"
    if re.search(r"^quit: ", output, re.M):
        return "StarDraft 2 ended after Cancel"
    return None


def check_program_files(output):
    """None when File > Save of a map in a Program Files folder showed the save dialog again (and said why)."""
    saves = re.findall(r"^dialog: save, .* starts at \"(.*)\": (.*)\r?$", output, re.M)
    if len(saves) != 2:
        return f"{len(saves)} save dialogs, not 2 (Save as, then Save of the map in Program Files)"
    if "Program Files" not in saves[1][0]:
        return f"the second save dialog starts at {saves[1][0]}"
    return None


def check_saveas_tab(output):
    """None when the tab switch during the Save as dialog did nothing and its answer saved the map that asked."""
    s = {x["input"]: x for x in steps(output)}
    try:
        switched, answered = s["key Ctrl+Tab"], s["wait dialog answer"]
    except KeyError:
        return "no line for Ctrl+Tab or for the dialog's answer"
    if switched.get("tabs") != "1/2":
        return f"Ctrl+Tab switched the tab while the save dialog was open: {switched.get('tabs')}"
    if answered.get("tabs") != "1/2" or answered.get("tab") != "asked.scx":
        return f"the answer saved another map: tab {answered.get('tab')} {answered.get('tabs')}"
    return None


def check_undo_midstroke(output):
    """None when the stroke painted (the Ctrl+Z during it did nothing) and one Undo after it took the stroke back."""
    return expect(output, 4, [
        ("the stroke painted nothing", lambda s: s[2]["terrain"] != s[1]["terrain"]),
        ("Undo after the stroke did not take it back", lambda s: s[3]["terrain"] == s[1]["terrain"]),
    ])


def check_undo_saved(output):
    """None when Undo back to the opened map shows no change, and Redo shows one."""
    return expect(output, 4, [
        ("the placed unit is not a change", lambda s: s[1]["tab"].startswith("* ")),
        ("Undo back to the map as it was opened still shows a change", lambda s: not s[2]["tab"].startswith("* ")),
        ("Redo is not a change", lambda s: s[3]["tab"].startswith("* ")),
    ])


def check_undo_selection(output):
    """None when the unit that was selected before Undo is still the selected one after it (the same place)."""
    first = dict(re.findall(r"^(click 1240,580|key Ctrl\+Z): \d+ units, \d+ doodads, 1 selected \(the first at (\d+,\d+)\)", output, re.M))
    if len(first) != 2:
        return f"one selected unit before and after Undo: {first}"
    if first["click 1240,580"] != first["key Ctrl+Z"]:
        return f"after Undo the selection is another unit: at {first['key Ctrl+Z']}, not {first['click 1240,580']}"
    return None


def check_props_typing_undo(output):
    """None when Undo took the typed title back (the map's title is not the typed one) and the placed unit stayed."""
    m = re.search(r'^names: title "(.*)"', output, re.M)
    if not m:
        return "no title line"
    if m.group(1) == "Undo title":
        return "the map has the typed title after Undo"
    placed, later = re.search(r"^click 700,650: (\d+) units", output, re.M), re.search(r"^wait 700: (\d+) units", output, re.M)
    if not placed or not later or placed.group(1) != later.group(1):
        return "Undo took back the placed unit, not the typed title"
    return None


def check_data_map_undo(output):
    """None when Ctrl+Z in Data did not take back the unit placed in Map after the Players setting."""
    placed = re.search(r"^click 700,650: (\d+) units", output, re.M)
    undone = re.search(r"^key Ctrl\+Z: (\d+) units", output, re.M)
    if not placed or not undone:
        return "no line for the placed unit or for Ctrl+Z"
    if placed.group(1) != undone.group(1):
        return f"Ctrl+Z in Data took back the unit placed in Map ({placed.group(1)} units, then {undone.group(1)})"
    return None


def check_value_insert(output):
    """None when the value picked with a click in Insert value's picker went into the text (the value pop-out stayed)."""
    m = re.search(r"^click 498,353: .*\n  state: .*act\.showtext\(([^,]*),", output, re.M)
    if not m or "{name(current player):s}" not in m.group(1):
        return f"the text is {m.group(1) if m else 'not there'}, without the inserted value"
    return None


def check_release_older(output):
    """None when a release published before this build's commit is not newer (nothing to install)."""
    if not re.search(r"^release: newer no, .*Check for updates: nothing \(up to date\)", output, re.M):
        return "an older release is offered as newer"
    return None


def check_release_token(output):
    """None when the request that GitHub refused with the token was made again without it, and found the release."""
    if not re.search(r'^release-token: 1 request with the token \(refused\), 1 without it: "StarDraft 2 - v9\.9\.9\.9 \(build 0123abc\)" \(yes\)',
                     output, re.M):
        return "the release was not found when GitHub refused the token"
    return None


# The New page, the start page, the side bars and the building snap (the window's own parts: the "  ui: ..." line after
# each input, sdl_shot_state.inc: sdlShotUi). Bug reports of 0.6.0-preview.1:
# The New page has no Back button (Cancel and Esc leave it): a click where Back was does nothing, Esc closes the page.
# A classic project can start from the terrain of a map (as an EUD project can import a map): Create makes a new map
# (untitled) with the map's tileset, size, terrain and doodads, and no units; the map file is the case's map ({map}: it is
# not opened before).
# File > New (Ctrl+N) and the tabs' + show the start page with a project open (Esc goes back to the map); its EUD card and
# Create make a new project in a tab of its own (the open project stays).
# Each side bar is resized by a drag of its edge next to the map (the width cursor over it), from 200 to 640 px.
# Buildings snap to tile edges while Options > Units > Snap buildings to tile edges is on (the default; the unit snap is
# for the other units): a placed or dragged Command Center has its box on the tile grid (x on a tile edge, y 16 px off);
# with the option off ({nosnapsettings}) it stays where the mouse put it.
# Every New entry works after a project was opened (File > Open, the project given to the program, another workspace
# first, its tab closed): File > New project (Ctrl+N) and the tabs' + show the start page, its cards and Project > New
# EUD project (Ctrl+Shift+N) the New page, and Create makes the project in the folder typed ({newdir}), in a tab of its
# own. While the New page (or the start page over a map) shows, the side bar's other workspaces and Ctrl+2 do nothing.
# The last tab closed from any workspace (Data, Triggers, Briefing) goes to the start page in the Map workspace; a classic
# map's tab after a project's in Data shows its map.
CASES += [
    ("newpage-noback", ["--newpage", "eud", "--click", "382,218", "--key", "Esc"], ""),
    ("map-newpage-classic-terrain", ["--newpage", "classic", "--click", "489,517", "--click", "815,314", "--click", "960,353", "--type", "{map}",
                                     "--click", "1126,897"], "map"),
    ("project-newproject", ["--key", "Ctrl+N", "--click", "711,430", "--click", "437,406", "--key", "Ctrl+A", "--type", "{newdir}",
                            "--click", "1162,815"], "project"),
    # The map's commands do nothing while the start page shows over the map (Del after Select all deletes no unit).
    ("map-start-over-map", ["--mode", "units", "--key", "Ctrl+A", "--key", "Ctrl+N", "--key", "Del", "--key", "Esc"], "map"),
    ("project-newpage-esc", ["--key", "Ctrl+N", "--key", "Esc", "--click", "176,84", "--key", "Esc", "--key", "Ctrl+Shift+N", "--key", "Esc"], "project"),
    ("project-open-new", ["--key", "Ctrl+O", "--pick", "{project}", "--key", "Ctrl+N", "--click", "711,430", "--click", "437,406", "--key", "Ctrl+A", "--type", "{newdir}", "--click", "1162,815"], "project"),
    ("project-triggers-neweud", ["--workspace", "triggers", "--key", "Ctrl+Shift+N", "--click", "437,406", "--key", "Ctrl+A", "--type", "{newdir}", "--click", "1162,815"], "project"),
    ("project-close-new", ["--workspace", "data", "--key", "Ctrl+W", "--click", "711,430", "--click", "30,190", "--key", "Ctrl+2",
                           "--click", "437,406", "--key", "Ctrl+A", "--type", "{newdir}", "--click", "1162,815"], "project"),
    ("project-newpage-locked", ["--key", "Ctrl+Shift+N", "--click", "30,190", "--key", "Ctrl+2", "--key", "Ctrl+3", "--key", "Esc",
                                "--key", "Ctrl+N", "--click", "471,430", "--click", "30,190", "--key", "Esc"], "project"),
    ("project-close-data", ["--workspace", "data", "--key", "Ctrl+W"], "project"),
    ("project-close-triggers", ["--workspace", "triggers", "--key", "Ctrl+W"], "project"),
    ("project-close-briefing", ["--workspace", "briefing", "--key", "Ctrl+W"], "project"),
    ("map-close-project-data", ["--newproject", "{newdir}", "--key", "Ctrl+2", "--key", "Ctrl+W"], "map"),
    # The map's Edit commands, the quick picker (Q) and Rename location (F2) do nothing to the map in Data and Triggers
    # (the units, doodads, locations and the selection stay); the workspace's own Edit commands are enabled, the others
    # gray (the menu items and the top bar's buttons): Data copies and pastes entries, Triggers cuts, copies, pastes and
    # duplicates; Q in Data opens the quick picker of its entries (Go to a .dat entry), not the map's.
    ("project-edit-data", ["--mode", "units", "--key", "Ctrl+A", "--key", "Ctrl+2", "--key", "Ctrl+A", "--key", "Ctrl+C", "--key", "Ctrl+X",
                           "--key", "Ctrl+D", "--key", "F2", "--key", "Del", "--key", "Q"], "project"),
    ("project-edit-triggers", ["--mode", "units", "--key", "Ctrl+A", "--key", "Ctrl+3", "--key", "Ctrl+A", "--key", "Del", "--key", "F2",
                               "--key", "Q", "--key", "1"], "project"),
    ("map-sidebar-resize", ["--move", "341,500", "--drag", "341,500-401,500", "--move", "1323,500", "--drag", "1323,500-1263,500",
                            "--drag", "401,500-1001,500", "--drag", "701,500-101,500", "--move", "900,500"], "map"),
    ("map-building-nosnap", ["--settings", "{nosnapsettings}", "--mode", "units", "--key", "Q", "--type", "Command Center", "--key", "Enter", "--click", "701,651", "--key", "Esc",
                             "--drag", "701,651-713,659"], "map"),
    ("map-building-snap", ["--mode", "units", "--key", "Q", "--type", "Command Center", "--key", "Enter",
                           "--click", "701,651", "--key", "Esc", "--drag", "701,651-713,659"], "map"),
]


def ui_lines(output):
    """The input lines and the ui line after each: [(input, {name: value})] (the last value of a name that comes twice)."""
    out, last = [], None
    for line in output.splitlines():
        m = STATE_LINE.match(line)
        if m:
            last = m.group(1) + " " + m.group(2)
            out.append((last, {}))
        elif line.startswith("  ui: ") and out and out[-1][0] == last:
            out[-1] = (last, dict(kv.split("=", 1) for kv in line[6:].split() if "=" in kv))
    return out


def expect_ui(count, rules):
    """A check: `count` inputs with a ui line, and each rule (what, function of the [(input, ui)] list) holds."""
    def check(output):
        u = ui_lines(output)
        if len(u) != count or any(not d for _, d in u):
            return f"{len(u)} inputs ({sum(1 for _, d in u if d)} with a ui line), not {count}"
        for what, ok in rules:
            try:
                good = ok(u)
            except (KeyError, ValueError, IndexError):
                good = False
            if not good:
                return f"{what}: {u}"
        return None
    return check


def check_building_snap(snapped):
    """None when the Command Center that was placed, then dragged, is on the tile grid (`snapped`) or off it each time."""
    def on_grid(st):
        x, y = (int(v) for v in re.search(r" lastat=(\d+),(\d+)", st).group(1, 2))
        return x % 32 == 0 and y % 32 == 16  # (its box, 128 x 96, on tile edges)

    def check(output):
        s = states(output)
        if len(s) != 6:
            return f"{len(s)} input lines, not 6"
        for i, what in ((3, "placed"), (5, "dragged")):
            if not re.search(r" lastat=\d+,\d+", s[i][1]):
                return f"no place of the {what} unit: {s[i][1]}"
            if on_grid(s[i][1]) != snapped:
                return f"the {what} Command Center is {'not ' if snapped else ''}on the tile grid: {s[i][1]}"
        if "59 units" not in output.split("click 701,651:")[1].split("\n")[0]:
            return "the Command Center was not placed"
        return None
    return check


def check_new_project_in(case, count, before):
    """A check: `count` inputs; the ui line of each input in `before` ({index: (what, function of the ui)}) holds; the last
    one is the new project in a tab of its own, made in the case's {newdir} (not in the projects folder)."""
    def check(output):
        u = ui_lines(output)
        if len(u) != count or any(not d for _, d in u):
            return f"{len(u)} inputs ({sum(1 for _, d in u if d)} with a ui line), not {count}"
        for i, (what, ok) in before.items():
            try:
                good = ok(u[i][1])
            except (KeyError, ValueError):
                good = False
            if not good:
                return f"{what}: {u[i]}"
        last = u[-1][1]
        if last.get("page") != "map" or last.get("project") != "yes":
            return f"Create did not make the project: {last}"
        if not re.search(r"Created the project \S+.* in .*" + re.escape(case) + "-", output):
            return "the project is not in the folder typed in Location"
        return None
    return check


def check_edit_elsewhere(ws, enabled, last_quick="no"):
    """A check: after Select all on the map, the inputs in workspace `ws` leave the map's units, doodads, locations and
    selection as they were, the quick picker closed, and the Edit commands in `enabled` on, the others gray."""
    names = ["cut", "copy", "paste", "dup", "del", "all", "rename", "quick"]

    def check(output):
        u = ui_lines(output)
        if len(u) < 3 or any(not d for _, d in u):
            return f"{len(u)} inputs with a ui line"
        first = u[0][1]
        if first.get("ws") != "map" or first.get("sel", "0").startswith("0,"):
            return f"Select all on the map selected nothing: {first}"
        for inp, d in u[1:]:
            if d.get("ws") != ws:
                return f"{inp}: not in {ws}: {d}"
            for k in ("units", "doodads", "locations", "sel"):
                if d.get(k) != first.get(k):
                    return f"{inp} changed the map's {k}: {first.get(k)} -> {d.get(k)}"
            want = last_quick if inp == u[-1][0] else "no"
            if d.get("quickopen") != want:
                return f"{inp}: the quick picker is {d.get('quickopen')}, not {want}, in {ws}"
            flags = dict(kv.split(":") for kv in d.get("edit", "").split(",") if ":" in kv)
            for n in names:
                if flags.get(n) != ("1" if n in enabled else "0"):
                    return f"{inp}: Edit's {n} is {'gray' if flags.get(n) == '0' else 'enabled'} in {ws}: {d.get('edit')}"
        return None
    return check


def check_classic_terrain(output):
    """The classic New page offers the terrain of a map; Create makes an untitled map with the map's tileset, size and
    doodads (the --map: Space Platform, 128 x 128, 50 doodads) and no units."""
    u, s = ui_lines(output), states(output)
    if len(u) != 5 or len(s) != 5:
        return f"{len(u)} inputs, not 5"
    if "terrain" not in u[0][1].get("offered", "").split(","):
        return f"the classic New page does not offer the terrain of a map: {u[0][1]}"
    if u[1][1].get("start") != "terrain":
        return f"The terrain of a map is not selected: {u[1][1]}"
    if u[4][1].get("page") != "map" or u[4][1].get("tabs") != "1/1":
        return f"after Create: {u[4][1]} (not the new map in a tab)"
    if not s[4][1].startswith("map: Space Platform 128 x 128;") or 'tab="* Untitled"' not in s[4][1]:
        return f"the new map is not an untitled map with the map's tileset and size: {s[4][1]}"
    if not re.search(r"^click 1126,897: 0 units, 50 doodads", output, re.M):
        return "the new map does not have the map's doodads and no units"
    return None


CHECKS = {"map-terrain-lostdrag": check_terrain_lostdrag, "map-key-undo": check_undo, "map-save-edited": check_edited, "map-click-place": check_place,
          "map-drag-select": check_box_select, "map-drag-unit": check_drag_unit, "map-doodad-place": check_doodad_place,
          "map-doodad-ghost": check_doodad_ghost, "map-sprite-place": check_sprite_place,
          "map-location-make": check_location_make, "map-terrain-brush": check_terrain_brush,
          "map-terrain-rect-paint": check_rect_paint, "map-terrain-paste": check_terrain_paste,
          "map-terrain-paste-ghost": check_terrain_paste_ghost, "map-paste-ghost": check_paste_ghost,
          "map-paste-ghost-red": check_paste_ghost_red, "map-paste-placed": check_paste_placed,
          "map-player-keys": check_player_keys, "map-terrain-keys": check_terrain_keys, "map-tabs": check_tabs, "map-quick-click": check_quick_click,
          "newpage-create": check_newpage_create, "data-form-edits": check_data_edits,
          "newpage-randomize-quick": check_newpage_randomize, "newpage-symmetry-dropdown": check_newpage_symmetry,
          "data-triggers-edits": check_trigger_edits, "map-classictrig-edits": check_classic_edits,
          "map-files-search": check_files_search, "map-briefing-add": check_briefing_add,
          "start-noinstall": check_banner(True, "none", False),
          "start-noinstall-nodata": check_banner(True, "That folder has no StarCraft game data.", False),
          "start-noinstall-pick": check_banner(False, "none", True), "start-noinstall-keys": check_start_keys, "start-scale2": check_scale2,
          "map-wheel-zoom": check_zoom_at("wheel 700,500,0,1", 1.2),
          "map-wheel-smooth": per_system(check_pan("wheel 700,500,0,0.25:precise", 0, -0.25),
                                         check_zoom_at("wheel 700,500,0,0.25:precise", 1.2 ** 0.25)),
          "map-trackpad-pan": per_system(check_pan("wheel 700,500,1,-1.5:precise", 1, 1.5),
                                         check_zoom_at("wheel 700,500,1,-1.5:precise", 1.2 ** -1.5)),
          "map-trackpad-ctrl-zoom": per_system(check_zoom_at("wheel 700,500,0,2:precise:Ctrl", 1.2 ** 0.5),
                                               check_zoom_at("wheel 700,500,0,2:precise:Ctrl", 1.44)),
          "map-pinch": check_zoom_at("pinch 700,500,1.5", 1.5), "map-brush-pinch": check_brush_pinch, "start-menubar": check_menubar,
          "check-window": check_window(False), "check-window-system": check_window(True), "start": check_start,
          "start-trackpad-scroll": check_trackpad_scroll, "map-smartzoom": check_smartzoom, "map-swipe": check_swipe,
          "drop-sound-assets": check_drop("Imported 1 sounds"), "drop-image-assets": check_drop("Did not import atlas-16.png"),
          "check-dock": check_dock, "map-check-desktop": check_desktop, "dialog-options-tab-general": check_options_tab, "dialog-options-tab-map": check_options_tab,
          "dialog-options-tab-start": check_options_tab, "dialog-options-tab-folders": check_options_tab,
          "update-settings": check_update_settings, "update-checks": check_update_checks, "update-release": check_release,
          "update-registry": check_settings_read(LEGACY_LINES),
          "map-text-input": check_text_input, "map-text-korean-save": check_korean_save, "map-dialog-tab": check_dialog_tab,
          "data-tree-typeahead": check_tree_typeahead, "data-file-dialogs": check_file_dialogs,
          "map-script-session": check_session, "map-script-dialogs": check_dialogs, "project-script-reopen": check_reopen,
          "text-input-cjk": check_text_cjk, "script-nodata": check_soak_nodata,
          "map-quit-tabs": check_quit_tabs, "map-save-typing": check_save_typing, "map-briefing-quit": check_briefing_quit,
          "map-program-files": check_program_files, "map-saveas-tab": check_saveas_tab, "map-undo-midstroke": check_undo_midstroke,
          "map-undo-saved": check_undo_saved, "map-undo-selection": check_undo_selection, "map-props-typing-undo": check_props_typing_undo,
          "data-map-undo": check_data_map_undo, "data-value-insert": check_value_insert, "update-release-older": check_release_older,
          "update-release-token": check_release_token,
          "newpage-noback": expect_ui(2, [
              ("a click where Back was closed the page (Back is there)", lambda u: u[0][1]["page"] == "new"),
              ("Esc did not close the New page", lambda u: u[1][1]["page"] == "start")]),
          "map-newpage-classic-terrain": check_classic_terrain,
          "project-newproject": lambda output: expect_ui(6, [
              ("File > New (Ctrl+N) did not show the start page with a project open", lambda u: u[0][1]["page"] == "start"),
              ("the EUD project card did not show the New page", lambda u: u[1][1]["page"] == "new" and u[1][1]["new"] == "eud"),
              ("Create did not make the project in a tab of its own", lambda u: u[5][1]["page"] == "map" and u[5][1]["tabs"] == "2/2"
               and u[5][1]["project"] == "yes")])(output) or (
              None if re.search(r"Created the project .* in .*project-newproject-", output) else "the project is not in the folder typed"),
          "project-newpage-esc": expect_ui(6, [
              ("Ctrl+N did not show the start page", lambda u: u[0][1]["page"] == "start"),
              ("Esc did not close the start page", lambda u: u[1][1]["page"] == "map"),
              ("the tabs' + did not show the start page", lambda u: u[2][1]["page"] == "start"),
              ("Esc did not close the start page", lambda u: u[3][1]["page"] == "map"),
              ("Ctrl+Shift+N did not show the New page", lambda u: u[4][1]["page"] == "new" and u[4][1]["new"] == "eud"),
              ("Esc did not close the New page", lambda u: u[5][1]["page"] == "map" and u[5][1]["tabs"] == "1/1")]),
          "map-sidebar-resize": expect_ui(7, [
              ("no width cursor over the left side bar's edge", lambda u: u[0][1]["cursor"] == "width"),
              ("the drag did not make the left side bar 60 px wider", lambda u: u[1][1]["left"] == "340" and u[1][1]["right"] == "280"),
              ("no width cursor over the right side bar's edge", lambda u: u[2][1]["cursor"] == "width"),
              ("the drag did not make the right side bar 60 px wider", lambda u: u[3][1]["right"] == "340" and u[3][1]["left"] == "340"),
              ("the left side bar is not at most 640 px", lambda u: u[4][1]["left"] == "640"),
              ("the left side bar is not at least 200 px", lambda u: u[5][1]["left"] == "200"),
              ("the width cursor over the map", lambda u: u[6][1]["cursor"] != "width")]),
          "map-start-over-map": lambda output: expect_ui(4, [
              ("Ctrl+N did not show the start page over the map", lambda u: u[1][1]["page"] == "start"),
              ("Esc did not close the start page", lambda u: u[3][1]["page"] == "map")])(output) or (
              None if re.search(r"^key Del: 58 units", output, re.M) else "Del deleted units of the map under the start page"),
          "project-open-new": check_new_project_in("project-open-new", 7, {
              0: ("File > Open did not open the project", lambda d: d["project"] == "yes" and d["tabs"] == "1/1"),
              1: ("File > New project (Ctrl+N) did not show the start page", lambda d: d["page"] == "start"),
              2: ("the EUD project card did not show the New page", lambda d: d["page"] == "new" and d["new"] == "eud"),
              6: ("the new project is not in a second tab", lambda d: d["tabs"] == "2/2")}),
          "project-triggers-neweud": check_new_project_in("project-triggers-neweud", 5, {
              0: ("Project > New EUD project (Ctrl+Shift+N) in Triggers did not show the New page", lambda d: d["page"] == "new" and d["ws"] == "map"),
              4: ("the new project is not in a second tab", lambda d: d["tabs"] == "2/2")}),
          "project-close-new": check_new_project_in("project-close-new", 8, {
              0: ("the last tab closed in Data did not show the start page in Map", lambda d: d["page"] == "start" and d["ws"] == "map"),
              1: ("the EUD project card did not show the New page", lambda d: d["page"] == "new"),
              2: ("the side bar's Data left the New page", lambda d: d["page"] == "new" and d["ws"] == "map"),
              3: ("Ctrl+2 left the New page", lambda d: d["page"] == "new" and d["ws"] == "map"),
              7: ("the new project is not in the only tab", lambda d: d["tabs"] == "1/1")}),
          "project-newpage-locked": expect_ui(9, [
              ("Ctrl+Shift+N did not show the New page", lambda u: u[0][1]["page"] == "new"),
              ("the side bar's Data left the New page", lambda u: u[1][1]["page"] == "new" and u[1][1]["ws"] == "map"),
              ("Ctrl+2 left the New page", lambda u: u[2][1]["page"] == "new" and u[2][1]["ws"] == "map"),
              ("Ctrl+3 left the New page", lambda u: u[3][1]["page"] == "new" and u[3][1]["ws"] == "map"),
              ("Esc did not close the New page", lambda u: u[4][1]["page"] == "map"),
              ("Ctrl+N did not show the start page", lambda u: u[5][1]["page"] == "start"),
              ("the classic project card did not show the New page", lambda u: u[6][1]["page"] == "new" and u[6][1]["new"] == "classic"),
              ("the side bar's Data left the classic New page", lambda u: u[7][1]["page"] == "new" and u[7][1]["ws"] == "map"),
              ("Esc did not close the classic New page", lambda u: u[8][1]["page"] == "map" and u[8][1]["tabs"] == "1/1")]),
          "project-close-data": expect_ui(1, [("closing the last tab in Data did not show the start page in Map",
                                               lambda u: u[0][1]["page"] == "start" and u[0][1]["ws"] == "map")]),
          "project-close-triggers": expect_ui(1, [("closing the last tab in Triggers did not show the start page in Map",
                                                   lambda u: u[0][1]["page"] == "start" and u[0][1]["ws"] == "map")]),
          "project-close-briefing": expect_ui(1, [("closing the last tab in Briefing did not show the start page in Map",
                                                   lambda u: u[0][1]["page"] == "start" and u[0][1]["ws"] == "map")]),
          "map-close-project-data": expect_ui(2, [
              ("Ctrl+2 did not show the project's Data", lambda u: u[0][1]["ws"] == "data" and u[0][1]["tabs"] == "2/2"),
              ("the classic map's tab after the project's (closed in Data) does not show its map",
               lambda u: u[1][1]["ws"] == "map" and u[1][1]["page"] == "map" and u[1][1]["tabs"] == "1/1")]),
          "project-edit-data": check_edit_elsewhere("data", {"copy", "paste", "quick"}, "data"),
          "project-edit-triggers": check_edit_elsewhere("triggers", {"cut", "copy", "paste", "dup"}),
          "map-building-nosnap": check_building_snap(False), "map-building-snap": check_building_snap(True),
          "data-quick-armor": quick_check("damage table armor 8;", "eight clicks on More armor"),
          "data-quick-toggle": quick_check("no changed values; 8 undo steps", "eight clicks on Shield enable"),
          "map-quick-arrow": quick_check(" brush=2x10", "eight clicks on the height's up arrow"),
          "data-turn-nohandle": check_no_still_handles, "data-range-handles": check_range_handles,
          "map-location-inverted": check_location_inverted,
          "map-props-caret": check_caret_lines(2), "data-text-caret": check_caret_lines(2),
          "files-double-click": check_double_click_rows(('selected "sound\\bullet\\dragbull.wav"', 'selected "sound\\bullet\\laserb.wav"'),
                                                        '1 plays, last "sound\\bullet\\laserb.wav"'),
          "data-sfx-double-click": check_double_click_rows(("list entry 3;", "list entry 5;"), '1 plays, last "sfx 5"'), "data-picker-typing": check_picker_typing}

# GitHub's answer for the newest release ({release}; /releases/latest, shortened): a release with a download for each
# system, as tools/release.py and the macos-release job make it, indented as GitHub gives it to a request without a
# token (core/releases.inc: compactJson).
RELEASE_DOWNLOAD = "https://github.com/stardraft-editor/stardraft/releases/download/v9.9.9.9/"
RELEASE_DOWNLOADS = {"win32": "StarDraft-9.9.9.9-win64.zip", "darwin": "StarDraft-9.9.9.9-macos-arm64.dmg",
                     "linux": "StarDraft-9.9.9.9-x86_64.AppImage"}
RELEASE_JSON = json.dumps({
    "url": "https://api.github.com/repos/stardraft-editor/stardraft/releases/1",
    "html_url": "https://github.com/stardraft-editor/stardraft/releases/tag/v9.9.9.9", "id": 1,
    "author": {"login": "stardraft-editor", "url": "https://api.github.com/users/stardraft-editor",
               "html_url": "https://github.com/stardraft-editor"},
    "tag_name": "v9.9.9.9", "name": "StarDraft 2 - v9.9.9.9 (build 0123abc)", "draft": False, "prerelease": False,
    "assets": [{"url": f"https://api.github.com/repos/stardraft-editor/stardraft/releases/assets/{11 + k}", "name": name,
                "browser_download_url": RELEASE_DOWNLOAD + name} for k, name in enumerate(RELEASE_DOWNLOADS.values())],
    "body": "What is new"}, indent=2) + "\n"

# A release published in 2020 ({releaseold}), before any commit of this build: never newer than this build.
RELEASE_OLD_JSON = json.dumps({
    "url": "https://api.github.com/repos/stardraft-editor/stardraft/releases/0",
    "html_url": "https://github.com/stardraft-editor/stardraft/releases/tag/v0.0.0.1", "id": 0,
    "tag_name": "v0.0.0.1", "name": "StarDraft 2 - v0.0.0.1 (build 0000abc)", "published_at": "2020-01-01T00:00:00Z",
    "assets": [{"url": "https://api.github.com/repos/stardraft-editor/stardraft/releases/assets/1", "name": name,
                "browser_download_url": RELEASE_DOWNLOAD + name} for name in RELEASE_DOWNLOADS.values()],
    "body": "An old release"}, indent=2) + "\n"

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def write_wav(path):
    """A short sound (0.1 s of a 440 Hz tone, 16-bit mono at 22050 Hz) for the drop cases ({wav})."""
    import math
    import wave
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(22050)
        w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 22050))) for i in range(2205)))


def write_old_settings(path, out):
    """The settings file of StarDraft 0.5.0.8, the last Win32 release, as its code wrote it ({oldsettings}): values that
    are not the defaults, so that each one read shows."""
    import json
    values = {
        "Accent": 3, "AskedMapsDefault": 1, "CheckUpdates": 0, "CrashReports": 2, "DataPins/units": "0,7,37", "Language": "es",
        "Layout/DataListWidth": 333, "Layout/DoodadsRight": 1, "Layout/GridHeight": 16, "Layout/GridWidth": 48, "Layout/GridX": 3,
        "Layout/GridY": 5, "Layout/LeftWidth": 411, "Layout/LocationsRight": 1, "Layout/LogHeight": 294, "Layout/MinimapHidden": 1,
        "Layout/MinimapRight": 1, "Layout/RightWidth": 248, "Layout/ScriptPreview": 1, "Layout/SpritesRight": 1, "Layout/TerrainRight": 1,
        "Layout/TriggerCode": 1, "Layout/TriggerZoom": 90, "Layout/UnitNames": 1, "Layout/UnitsRight": 0, "Layout/Volume": 40,
        "LightTheme": 1, "MiddleDrag": 1, "OpenLastProject": 1, "ProjectsFolder": os.path.join(out, "projects"),
        "RecentMaps": ["C:\\Maps\\(2)Bottleneck.scm", "/home/user/maps/Ice \uc5bc\uc74c.scx"], "RecentProjects": ["C:\\StarDraft\\AshenCrown"],
        "Settings/AllowStacking": 1, "Settings/BrushShape": 1, "Settings/Carbot": 1, "Settings/LayerDoodads": 0,
        "Settings/PaletteViews": 12345, "Settings/Quality": 1, "Settings/ShowGrid": 1, "Settings/UnitGrid": 16,
        "UnitLimitClassic": 2000, "UnitLimitEud": 1500}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(values, f, ensure_ascii=False, indent=2)


def write_legacy_hive(path):
    """A registry file of its own (Windows) with Software\\StarDraft as StarDraft 0.5.0.7 and older kept the settings
    ({legacyhive}); the user's registry is never opened."""
    import ctypes
    import winreg
    advapi = ctypes.WinDLL("advapi32")
    key = ctypes.c_void_p()
    if advapi.RegLoadAppKeyW(ctypes.c_wchar_p(path), ctypes.byref(key), winreg.KEY_ALL_ACCESS, 0, 0) != 0:
        raise OSError("RegLoadAppKey failed: " + path)
    values = {"": {"RecentMaps": ["C:\\Maps\\(2)Bottleneck.scm", "C:\\Maps\\Ice \uc5bc\uc74c.scx"], "RecentProjects": ["C:\\StarDraft\\AshenCrown"],
                   "ProjectsFolder": "C:\\StarDraft Projects", "OpenLastProject": 1, "CheckUpdates": 0, "CrashReports": 1, "MiddleDrag": 1,
                   "UnitLimitClassic": 2500, "UnitLimitEud": 1200, "Language": "ko", "LightTheme": 1, "Accent": 6, "AskedMapsDefault": 1},
              "\\Layout": {"MinimapRight": 1, "UnitsRight": 0, "LeftWidth": 350, "Volume": 25, "TriggerZoom": 150, "GridWidth": 64},
              "\\Settings": {"Quality": 0, "ShowGrid": 1, "PaletteViews": 777, "BrushShape": 1},
              "\\DataPins": {"units": "5,6", "weapons": "1"}}
    try:
        for sub, items in values.items():
            k = winreg.CreateKeyEx(key.value, "Software\\StarDraft" + sub, 0, winreg.KEY_ALL_ACCESS)
            for name, v in items.items():
                kind = winreg.REG_MULTI_SZ if isinstance(v, list) else winreg.REG_SZ if isinstance(v, str) else winreg.REG_DWORD
                winreg.SetValueEx(k, name, 0, kind, v)
            winreg.CloseKey(k)
    finally:
        advapi.RegCloseKey(key)


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
    # (the cases' text is not all ASCII: Korean, Chinese, emoji; a Windows console's code page cannot show it all)
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="the SDL program (on macOS: stardraft.app/Contents/MacOS/stardraft)")
    ap.add_argument("--out", help="the folder for the PNGs, the output of each run and summary.txt")
    ap.add_argument("--install", help="a StarCraft: Remastered install (the cases that need game data)")
    ap.add_argument("--map", help="a map to open (the cases that need a map; needs --install)")
    ap.add_argument("--project", help="a project to open (the cases that need a project)")
    ap.add_argument("--only", help="only these cases (names, comma-separated)")
    ap.add_argument("--scale", default="1", help="the UI scale (default 1, so that the sizes are the same everywhere)")
    ap.add_argument("--timeout", type=int, default=120, help="seconds for each run (default 120)")
    ap.add_argument("--jobs", type=int, default=1, help="runs at the same time (default 1)")
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
    # Linux: Noto Sans, not the desktop's UI font (canvas_imgui.cpp), so that the text and the places the cases click
    # are the same on each computer.
    env.setdefault("STARDRAFT_FONT", "noto")

    # First each case's command and files, one after the other (the files that runs share are written once); then
    # the runs, --jobs at the same time.
    planned, written = [], set()

    def write_once(path, write):
        if path not in written:
            write()
            written.add(path)

    for name, args, needs in CASES:
        if only and name not in only:
            continue
        if not have[needs]:
            planned.append((name, None))
            continue
        png = os.path.join(a.out, name + ".png")
        if os.path.exists(png):
            os.replace(png, png + ".old")  # (a PNG from an earlier run does not count; never deleted)
        cmd = [a.exe]
        given = any("{map}" in x or "{project}" in x for x in args)  # (the path is in the options)
        # {newdir}: a new empty folder in --out for each run (a folder is never deleted)
        newdir = tempfile.mkdtemp(prefix=name + "-", dir=a.out) if any("{newdir}" in x for x in args) else ""
        # {kodir}: the same, its name in Korean with spaces (a path that is not ASCII: the save and the open)
        kodir = tempfile.mkdtemp(prefix=name + " \ud55c\uad6d \ud3f4\ub354 ", dir=a.out) if any("{kodir}" in x for x in args) else ""
        # {pfdir}: a new folder named "Program Files" in --out (a map there: File > Save asks for another place)
        pfdir = ""
        if any("{pfdir}" in x for x in args):
            pfdir = os.path.join(tempfile.mkdtemp(prefix=name + "-", dir=a.out), "Program Files")
            os.makedirs(pfdir)
        wav = os.path.join(a.out, "drop-sound.wav")
        if any("{wav}" in x for x in args) and not os.path.exists(wav):
            write_wav(wav)
        release = os.path.join(a.out, "release-latest.json")

        def write_text(path, text):
            def write():
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
            return write
        if any("{release}" in x for x in args):
            write_once(release, write_text(release, RELEASE_JSON))
        releaseold = os.path.join(a.out, "release-older.json")
        if any("{releaseold}" in x for x in args):
            write_once(releaseold, write_text(releaseold, RELEASE_OLD_JSON))
        oldsettings, legacyhive = os.path.join(a.out, "old-settings.json"), os.path.join(a.out, "legacy-settings.hive")
        nosnapsettings = os.path.join(a.out, "nosnap-settings.json")  # {nosnapsettings}: Snap buildings to tile edges off
        if any("{nosnapsettings}" in x for x in args):
            write_once(nosnapsettings, write_text(nosnapsettings, json.dumps({"Settings/SnapBuildings": 0})))
        if any("{oldsettings}" in x for x in args):
            write_once(oldsettings, lambda: write_old_settings(oldsettings, a.out))
        if any("{legacyhive}" in x for x in args):
            write_once(legacyhive, lambda: write_legacy_hive(legacyhive))
        paths = dict(map=a.map or "", project=a.project or "", install=a.install or "", root=ROOT, out=a.out, newdir=newdir,
                     kodir=kodir, wav=wav, release=release, oldsettings=oldsettings, legacyhive=legacyhive, pfdir=pfdir,
                     releaseold=releaseold, nosnapsettings=nosnapsettings)

        def script(m):  # {script:<name>}: SCRIPTS[name] with the paths, as a file in --out
            path = os.path.join(a.out, f"{name}-{m.group(1)}.txt")
            with open(path, "w", encoding="utf-8") as f:
                f.write(SCRIPTS[m.group(1)].replace("{n}", "\0").format(**paths).replace("\0", "{n}"))
            return path.replace("{", "{{").replace("}", "}}")
        args = [re.sub(r"\{script:(\w+)\}", script, x).format(**paths) for x in args]
        if needs == "map" and not given:
            cmd.append(a.map)
        elif needs == "project" and not given:
            cmd.append(a.project)
        cmd += ["--snapshot", png, "--scale", a.scale]
        if a.install:
            cmd += ["--install", a.install]
        cmd += args
        planned.append((name, (cmd, png)))

    lock = threading.Lock()  # (one run's output at a time)

    def run_case(name, cmd, png):
        if a.jobs <= 1:
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
        problem = f"exit code {code}" if code != 0 else png_check(png)
        if not problem and "(WRONG)" in output:  # (a check the run prints, as --cursors)
            problem = "a check printed (WRONG)"
        if not problem and name in CHECKS:
            problem = CHECKS[name](output)
        with lock:
            if a.jobs > 1:
                print(f"::group::{name}: {' '.join(cmd)}")
            print(output, end="" if output.endswith("\n") else "\n")
            print("::endgroup::", flush=True)
            print(f"{name}: FAILED ({problem})" if problem else f"{name}: passed", flush=True)
        return problem

    runs = [(name, plan) for name, plan in planned if plan]
    if a.jobs <= 1:
        problems = {name: run_case(name, *plan) for name, plan in runs}
    else:
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            futures = {name: pool.submit(run_case, name, *plan) for name, plan in runs}
            problems = {name: f.result() for name, f in futures.items()}

    # The results in the cases' order (the same picture as an earlier case: by that order too).
    results, failed, skipped, hashes = [], [], [], {}
    for name, plan in planned:
        if not plan:
            needs = next(n for c, _, n in CASES if c == name)
            skipped.append(name)
            results.append(f"{name}: skipped (needs {needs})")
            continue
        if problems[name]:
            failed.append(name)
            results.append(f"{name}: FAILED ({problems[name]})")
            continue
        with open(plan[1], "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        same = hashes.setdefault(digest, name)
        note = f" (the same picture as {same})" if same != name else ""
        results.append(f"{name}: passed{note}")

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
