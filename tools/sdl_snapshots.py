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

To add a case: add a line to CASES. Pass only options that main_sdl.cpp reads (its argument loop in main()).
"""

import argparse
import hashlib
import os
import struct
import subprocess
import sys
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
    ("map-rclick-search", ["--mode", "units", "--rclick", "1420,280"], "map"),
    # The Triggers code pane's menu (Copy all, Select all), and Report feedback in a dialog (Program options, a block's
    # help page): a right click that no view takes there.
    ("project-rclick-code", ["--workspace", "triggers", "--trig", "1:0", "--trigcode", "--rclick", "1300,700"], "project"),
    ("dialog-options-rclick", ["--dialog", "options", "--size", "720x820", "--rclick", "600,30"], ""),
    ("dialog-blockhelp-rclick", ["--dialog", "blockhelp:act.createunit", "--size", "460x600", "--rclick", "200,300"], "data"),
    # A project open.
    # A trigger whose blocks name units: the units' pictures in the values (the second trigger of the second group).
    ("project-trig-icons", ["--workspace", "triggers", "--trig", "1:1"], "project"),
]
# The Map data page's unit Rename: the game text pop-out.
CASES += [("map-textpop-unitname", ["--workspace", "classic", "--textpop", "unitname"], "map")]
# The cursors (the system's cursor is not in a picture): --cursors writes the map view's and a text field's cursors at
# some points to the case's .txt (lines "cursor: ...", each with "(yes)" or "(WRONG)").
CASES += [("map-cursors", ["--mode", "units", "--cursors"], "map")]
# Not in the program yet (add them to CASES when main_sdl.cpp reads them): --dialog about, message, tip, valuepop.

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
        args = [x.format(map=a.map or "", project=a.project or "", root=ROOT) for x in args]
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
