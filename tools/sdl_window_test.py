#!/usr/bin/env python3
"""The SDL program's real window (not a snapshot): the window test (--wintest, apps/editor/sdl_window_test.inc).

usage: python3 -I tools/sdl_window_test.py --exe <stardraft> [--map <map> --map2 <second map>] [--install <folder>]
                                            [--fullscreen]

The program opens <map> in a real window that never takes the focus or comes to the front (--noactivate), and
checks: the swapchain presents, the title, the icon, the title bar in the theme, a resize, a display of 150 %,
minimize and restore (nothing is painted while minimized), a dialog's title and title bar, and a second program that
gives <map2> to the first through the single-instance pipe (a test pipe of its own) and ends. Each check prints a line;
a "(WRONG)" line or an exit code other than 0 fails. The settings are in a new temporary folder (STARDRAFT_SETTINGS,
APPDATA, LOCALAPPDATA), so the user's settings, recent files and file types stay as they are. Needs a desktop (not
SDL_VIDEODRIVER=offscreen); on Windows only, the title bar, icon and pipe checks run; on Linux and the BSDs, the app id,
the focus and the single-instance socket checks (sdl_xdg.inc). Linux: give the program an X server of its own with a
window manager (Xvfb and openbox: minimize needs one), never the user's desktop; XDG_RUNTIME_DIR in a new folder too.
With no --map (no game data, as on GitHub's machines) the start page is in the window and the map's checks do not run;
with no --map2, no second program. --fullscreen: also full screen on and off (macOS: a space of its own; only on a
computer that no user works on, as GitHub's). The display (on macOS a Retina display's pixel density) is on the
"display:" line.
"""
import argparse
import os
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="the SDL program")
    ap.add_argument("--map", help="the map that the first program opens (none: the start page)")
    ap.add_argument("--map2", default="-", help="the map that the second program gives to the first (none: no second program)")
    ap.add_argument("--fullscreen", action="store_true", help="also full screen on and off")
    ap.add_argument("--install", help="a StarCraft: Remastered install (else the program's search)")
    ap.add_argument("--timeout", type=int, default=120, help="seconds (default 120)")
    ap.add_argument("args", nargs="*", help="more options for the program (--theme light, --scale 1.5)")
    a = ap.parse_args()
    sys.stdout.reconfigure(errors="replace")  # (a console in another code page: a Korean path prints as "?")
    env = dict(os.environ)
    env.pop("SDL_VIDEODRIVER", None)  # (a real window)
    folder = tempfile.mkdtemp(prefix="stardraft-wintest-")  # (a new folder each run)
    # (Linux and the BSDs: the XDG folders too: the data, the cache with the tile colors, the crash reports)
    for name, sub in (("STARDRAFT_SETTINGS", "settings"), ("APPDATA", "appdata"), ("LOCALAPPDATA", "localappdata"),
                      ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"), ("XDG_STATE_HOME", "state")):
        env[name] = os.path.join(folder, sub)
        os.makedirs(env[name])
    cmd = [os.path.abspath(a.exe)] + ([a.map] if a.map else []) + ["--wintest", a.map2] + (["--install", a.install] if a.install else [])
    cmd += (["--wintest-fullscreen"] if a.fullscreen else []) + a.args
    print(" ".join(cmd), flush=True)
    try:
        run = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=a.timeout)
        output, code = run.stdout.decode("utf-8", "replace"), run.returncode
    except subprocess.TimeoutExpired as e:
        output, code = (e.stdout or b"").decode("utf-8", "replace") + f"\n(timed out after {a.timeout} s)", None
    print(output, end="" if output.endswith("\n") else "\n")
    print(f"(settings folder: {folder})")
    if code != 0 or "(WRONG)" in output or " checks, 0 wrong" not in output:
        print(f"window test: FAILED (exit code {code})")
        return 1
    print("window test: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
