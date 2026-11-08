#!/usr/bin/env python3
"""A short video of a feature, for the release notes: the real program in a real window, used with a real mouse and keys.

The screen is recorded and cropped to the window, so that menus, context menus and popups show as they do for a user.
AutoHotkey moves the mouse and types, so this uses the desktop while it runs. It stops at once when a person touches the
mouse or the keyboard, or when another program's window comes to the front. It starts the program it is given, on a copy
of the map with a settings file of its own, and ends only that program.

A demo is a JSON file. Places are in the window's client area:

    {"map": "<map file>", "size": [1600, 1000], "steps": [
        {"text": "Pick a Marine", "click": [1386, 429]},
        {"rclick": [700, 650]}, {"altclick": [120, 668]}, {"shiftclick": [700, 650]}, {"ctrlclick": [700, 650]}, {"move": [700, 650]},
        {"drag": [640, 560, 820, 700]}, {"wheel": [145, 491, 3]}, {"key": "^a"}, {"wait": 500}]}

A step's "text" is the caption from that step on; "hold" is how long the step stays, in ms (default 900).

    python -I tools/demo_video.py <exe> <demo.json> <out.mp4>
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

AHK = r"C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe"

HEADER = r'''#Requires AutoHotkey v2
#SingleInstance Off
InstallKeybdHook
InstallMouseHook
SetTitleMatchMode 2
CoordMode "Mouse", "Client"
SendMode "Event"
SetDefaultMouseSpeed 18
SetKeyDelay 40, 40
pid := {pid}
win := "ahk_pid " pid
if !WinWait(win, , 30)
    ExitApp 2
Sleep 4000
WinActivate win
WinMove 40, 40, , , win
WinMove 40, 40, {w} + 16, {h} + 39 + 16, win
Sleep 800
WinGetClientPos &cx, &cy, &cw, &ch, win
FileAppend cx " " cy " " cw " " ch, "{rect}"
while !FileExist("{go}")
    Sleep 50
MouseGetPos &homeX, &homeY
caption := Gui("+AlwaysOnTop -Caption +ToolWindow +E0x20")
caption.BackColor := "101010"
caption.SetFont("s16 cWhite", "Segoe UI")
label := caption.AddText("Center w500 h30", "")
Guard() {{
    if A_TimeIdlePhysical < 200
        ExitApp 3
    if WinGetPID("A") != pid
        ExitApp 4
}}
Say(text) {{
    label.Value := text
    if text = ""
        caption.Hide()
    else
        caption.Show("x" (cx + (cw - 520) // 2) " y" (cy + ch - 90) " w520 h40 NoActivate")
}}
Sleep 600
'''


def quoted(text):
    return '"' + text.replace('`', '``').replace('"', '`"') + '"'


def step_code(step):
    lines = ["Guard()"]
    if "text" in step:
        lines.append(f"Say({quoted(step['text'])})")
    if "move" in step:
        lines.append("MouseMove {}, {}".format(*step["move"]))
    for name, button, mods in (("click", "Left", ""), ("rclick", "Right", ""), ("altclick", "Left", "Alt"), ("shiftclick", "Left", "Shift"), ("ctrlclick", "Left", "Ctrl")):
        if name in step:
            x, y = step[name]
            lines += [f"MouseMove {x}, {y}", "Sleep 250", "Guard()"]
            if mods:
                lines.append(f'Send "{{{mods} down}}"')
            lines.append(f'Click "{button}"')
            if mods:
                lines.append(f'Send "{{{mods} up}}"')
    if "drag" in step:
        x0, y0, x1, y1 = step["drag"]
        lines += [f"MouseMove {x0}, {y0}", "Sleep 250", "Guard()", 'Click "Down"', f"MouseMove {x1}, {y1}, 70", "Sleep 150", 'Click "Up"']
    if "wheel" in step:
        x, y, n = step["wheel"]
        lines += [f"MouseMove {x}, {y}", "Sleep 500", "Guard()"]
        lines += [f'Click "{"WheelUp" if n > 0 else "WheelDown"}"', "Sleep 120"] * abs(n)
    if "key" in step:
        lines.append(f"Send {quoted(step['key'])}")
    lines.append(f"Sleep {step.get('hold', step.get('wait', 900))}")
    return "\n".join(lines) + "\n"


def main():
    exe, demo_path, out = (os.path.abspath(p) for p in sys.argv[1:4])
    with open(demo_path, encoding="utf-8") as f:
        demo = json.load(f)
    w, h = demo.get("size", [1600, 1000])
    code = 1
    with tempfile.TemporaryDirectory() as folder:
        map_copy = os.path.join(folder, "demo" + os.path.splitext(demo["map"])[1])
        shutil.copy(demo["map"], map_copy)
        settings = os.path.join(folder, "settings.json")
        with open(settings, "w", encoding="utf-8") as f:
            json.dump(demo.get("settings", {}), f)
        rect, go = os.path.join(folder, "rect.txt"), os.path.join(folder, "go")
        program = subprocess.Popen([exe, map_copy, "--settings", settings, "--silent"], cwd=folder)
        script = os.path.join(folder, "demo.ahk")
        with open(script, "w", encoding="utf-8") as f:
            f.write(HEADER.format(pid=program.pid, w=w, h=h, rect=rect, go=go))
            for step in demo["steps"]:
                f.write(step_code(step))
            f.write('Say("")\nSleep 500\nMouseMove homeX, homeY, 0\nExitApp 0\n')
        driver = subprocess.Popen([AHK, script])
        recorder = None
        try:
            while not os.path.exists(rect):
                if driver.poll() is not None:
                    sys.exit(f"no window (AutoHotkey ended with {driver.returncode})")
                time.sleep(0.1)
            time.sleep(0.1)
            with open(rect, encoding="utf-8") as f:
                x, y, cw, ch = (int(v) for v in f.read().split())
            cw, ch = cw & ~1, ch & ~1
            recorder = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-draw_mouse", "1", "-offset_x", str(x), "-offset_y", str(y),
                                         "-video_size", f"{cw}x{ch}", "-i", "desktop", "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", "-crf", "20", "-movflags", "+faststart", out],
                                        stdin=subprocess.PIPE)
            time.sleep(1.0)
            open(go, "w").close()
            code = driver.wait()
        finally:
            if recorder:
                recorder.communicate(b"q", timeout=20)
            if driver.poll() is None:
                driver.kill()
            program.kill()
            program.wait()
    reasons = {3: "stopped: someone used the mouse or the keyboard", 4: "stopped: another program's window came to the front"}
    if code:
        sys.exit(reasons.get(code, f"AutoHotkey ended with {code}"))
    print(out)


if __name__ == "__main__":
    main()
