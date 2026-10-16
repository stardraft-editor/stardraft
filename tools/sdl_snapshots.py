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
case's options, {map}, {project}, {root} (the repository), {out} (the --out folder) and {newdir} (a new empty folder in
--out) are paths. A run also fails when its output has "(WRONG)", or when the
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
# The workspaces' edits with the mouse and the keys (the Windows self-test's steps off the map). --type types text into
# the view with the focus; --wait ms lets the program's timers run. An input goes to the window that takes input now: a
# dialog or pop-out that an input opened (a list picker, the block picker, a value's pop-out; in its own pixels) takes
# the next inputs, and closes when there are no more. Its inputs' lines come before the line of the input that opened
# it. After each input's line, a line "  state: ..." says what the workspace holds (sdl_shot_state.inc).
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


INPUT_LINE = re.compile(r"^(key|click|drag) (\S+): (\d+) units, (\d+) doodads, (\d+) selected(?: \(the first at (\d+),(\d+)\))?:")


def input_lines(output):
    """The lines of --key, --click and --drag: (option, value, units, selected, (x, y) of the first selected unit or None)."""
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


STATE_LINE = re.compile(r"^(key|type|click|drag|wait) (.*?): (?:no map|\d+ units)")


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
    if "click 1126,895" != s[1][0] or s[1][1] != "map: Ice 128 x 128":
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


# Checks of a case's output (after the PNG's): a function that returns None, or what is wrong.
CHECKS = {"map-key-undo": check_undo, "map-save-edited": check_edited, "map-click-place": check_place,
          "map-drag-select": check_box_select, "map-drag-unit": check_drag_unit, "newpage-create": check_newpage_create,
          "data-form-edits": check_data_edits, "data-triggers-edits": check_trigger_edits,
          "map-classictrig-edits": check_classic_edits, "map-files-search": check_files_search,
          "map-briefing-add": check_briefing_add}

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
        args = [x.format(map=a.map or "", project=a.project or "", root=ROOT, out=a.out, newdir=newdir) for x in args]
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
