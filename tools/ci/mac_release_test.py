#!/usr/bin/env python3
"""The release test of the macOS app (the job release-test-macos in .github/workflows/build.yml): the disk image as a
user gets it, on GitHub's macOS machine (it has a desktop session but no StarCraft data).

usage: python3 -I tools/ci/mac_release_test.py --dmg <StarDraft-...-macos-arm64.dmg> --out <folder> [--minutes 10]

The parts, each with its lines in the output and in <out>/<part>.txt, and a summary at the end (summary.txt):
  dmg       the disk image mounts (hdiutil attach -nobrowse), StarDraft.app goes into a temporary Applications folder and
            starts from there (--snapshot); its signature is good (codesign --verify; ad hoc when the repository has no
            Developer ID), the Info.plist has the map types (.scm, .scx: document types and exported types), the Quick
            Look extensions are in the bundle and registered (pluginkit -m), and Quick Look makes a thumbnail of a map
            (qlmanage -t; a small map that this script writes: no Blizzard files)
  snapshots each case of tools/sdl_snapshots.py that runs without game data, with the app from the disk image
  soak      a session of --minutes with no map (the Files workspace's search box, each workspace, the Log: SCRIPTS
            "nodata" of tools/sdl_snapshots.py), the memory flat (the resident memory and the footprint at the end are
            not much more than after the first minute)
  window    the real window (--noactivate: it never takes the focus): presents on Metal, the display and its Retina
            scale, a resize, 50 % more scale, minimize and restore, full screen on and off (tools/sdl_window_test.py);
            then the --check window, menus and dock lines
  text      Korean and Japanese input method compositions and emoji (text-input-cjk); the UI font SF Pro
  crash     --crash-test and --crash-test stack: each writes a report (a test run: it is not sent), the program ends with
            the signal, and --check crashes lists the reports and says that the next start sends nothing
  update    Check for updates with a release as GitHub's answer: it finds the release and opens its .dmg (a release
            build) or the release page (a build made here)
Every run of the app has its own HOME, settings and XDG folders in <out> (the runner's user settings stay the same).
Exits with 1 when a part failed.
"""

import argparse
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import sdl_snapshots  # noqa: E402  (its SCRIPTS and RELEASE_JSON)

LSREGISTER = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"


class Part:
    """One part's lines (printed and kept) and its problems."""

    def __init__(self, name, out):
        self.name, self.out, self.lines, self.problems = name, out, [], []
        print(f"::group::{name}", flush=True)

    def say(self, text):
        for line in str(text).rstrip("\n").split("\n"):
            print(line, flush=True)
            self.lines.append(line)

    def check(self, ok, text):
        self.say(("ok    " if ok else "WRONG ") + text)
        if not ok:
            self.problems.append(text)
        return ok

    def end(self):
        print("::endgroup::", flush=True)
        with open(os.path.join(self.out, self.name + ".txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(self.lines) + "\n")
        state = "passed" if not self.problems else "FAILED: " + "; ".join(self.problems)
        print(f"{self.name}: {state}", flush=True)
        return f"{self.name}: {state}"


def run(cmd, env=None, timeout=300):
    """(exit code, output) of a command; the code is None when it did not end in time."""
    try:
        r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return r.returncode, r.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        return None, (e.stdout or b"").decode("utf-8", "replace") + f"\n(timed out after {timeout} s)"
    except OSError as e:
        return None, f"could not start: {e}"


def app_env(out, name):
    """The environment of a run of the app: its own HOME (macOS's Application Support), settings and XDG folders."""
    base = os.path.join(out, "home-" + name)
    env = dict(os.environ)
    env.pop("SDL_VIDEODRIVER", None)  # (macOS: SDL's own driver; offscreen has no Metal device)
    for var, sub in (("HOME", "home"), ("STARDRAFT_SETTINGS", "settings"), ("XDG_CONFIG_HOME", "config"),
                     ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache")):
        env[var] = os.path.join(base, sub)
        os.makedirs(env[var], exist_ok=True)
    return env


# --- A small map (no Blizzard files): an MPQ archive with staredit\scenario.chk, not compressed -----------------------

def _crypt_table():
    seed, table = 0x00100001, [0] * 0x500
    for i in range(0x100):
        index = i
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            hi = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[index] = hi | (seed & 0xFFFF)
            index += 0x100
    return table


CRYPT = _crypt_table()


def _hash(text, kind):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in text.upper().encode("ascii"):
        s1 = (CRYPT[kind * 0x100 + ch] ^ (s1 + s2)) & 0xFFFFFFFF
        s2 = (ch + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1


def _encrypt(words, key):
    s2, out = 0xEEEEEEEE, []
    for v in words:
        s2 = (s2 + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        out.append((v ^ (key + s2)) & 0xFFFFFFFF)
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        s2 = (v + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return out


def write_test_map(path):
    """A 64 x 32 jungle map (the left half tile group 1, the right half 2) with a name, in an MPQ of format 0."""
    w, h = 64, 32

    def section(name, data):
        return name.encode("ascii") + struct.pack("<I", len(data)) + data
    strings = [b"\x03Release\x01 Test", b"A map of the release test"]
    offsets, body, at = [], b"", 2 + 2 * len(strings)
    for s in strings:
        offsets.append(at + len(body))
        body += s + b"\0"
    chk = b"".join([
        section("VER ", struct.pack("<H", 205)),
        section("DIM ", struct.pack("<HH", w, h)),
        section("ERA ", struct.pack("<H", 4)),
        section("OWNR", bytes([6, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0])),
        section("MTXM", b"".join(struct.pack("<H", 0x10 if x < w // 2 else 0x20) for _ in range(h) for x in range(w))),
        section("STR ", struct.pack("<H", len(strings)) + b"".join(struct.pack("<H", o) for o in offsets) + body),
        section("SPRP", struct.pack("<HH", 1, 2)),
    ])
    name = "staredit\\scenario.chk"
    hash_size, header = 16, 32
    file_pos = header
    hash_pos = file_pos + len(chk)
    block_pos = hash_pos + hash_size * 16
    hashes = [0xFFFFFFFF] * (hash_size * 4)
    slot = _hash(name, 0) & (hash_size - 1)
    hashes[slot * 4:slot * 4 + 4] = [_hash(name, 1), _hash(name, 2), 0, 0]  # (locale 0, platform 0; block 0)
    blocks = [file_pos, len(chk), len(chk), 0x80000000]  # (exists, not compressed, not encrypted)
    archive = (b"MPQ\x1a" + struct.pack("<IIHHIIII", header, block_pos + 16, 0, 3, hash_pos, block_pos, hash_size, 1) + chk
               + struct.pack("<%dI" % len(hashes), *_encrypt(hashes, _hash("(hash table)", 3)))
               + struct.pack("<4I", *_encrypt(blocks, _hash("(block table)", 3))))
    with open(path, "wb") as f:
        f.write(archive)


# --- The parts ---------------------------------------------------------------------------------------------------------

def part_dmg(a, out, apps):
    p = Part("dmg", out)
    mount = os.path.join(out, "mnt")
    os.makedirs(mount, exist_ok=True)
    code, text = run(["hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", mount, a.dmg])
    p.say(text)
    if not p.check(code == 0, "the disk image mounts"):
        return p.end(), None
    try:
        p.say("in the disk image: " + ", ".join(sorted(os.listdir(mount))))
        src = os.path.join(mount, "StarDraft.app")
        p.check(os.path.isdir(src), "StarDraft.app is in the disk image")
        p.check(os.path.islink(os.path.join(mount, "Applications")), "the link to Applications is in the disk image")
        app = os.path.join(apps, "StarDraft.app")
        code, text = run(["ditto", src, app])
        p.check(code == 0 and os.path.isdir(app), f"copied to {app} {text.strip()}")
    finally:
        code, text = run(["hdiutil", "detach", mount])
        p.check(code == 0, "the disk image detaches " + text.strip())
    exe = os.path.join(app, "Contents", "MacOS", "stardraft")
    # The app starts from the Applications folder.
    code, text = run([exe, "--snapshot", os.path.join(out, "dmg-start.png")], env=app_env(out, "dmg"), timeout=120)
    p.say(text)
    p.check(code == 0 and "wrote " in text, f"the app starts from {apps} (--snapshot, exit code {code})")
    # The signature.
    code, text = run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", app])
    p.say(text)
    p.check(code == 0, "codesign --verify --deep --strict")
    code, text = run(["codesign", "-dv", "--verbose=2", app])
    p.say(text)
    sig = "ad hoc" if "Signature=adhoc" in text else "Developer ID" if "Developer ID Application" in text else "?"
    p.check(sig != "?", f"signed: {sig}")
    p.check(re.search(r"flags=0x[0-9a-f]+\([^)]*\bruntime\b", text) is not None, "the hardened runtime is on")
    # The Info.plist: the document types and the exported types.
    plist = os.path.join(app, "Contents", "Info.plist")
    code, text = run(["plutil", "-lint", plist])
    p.check(code == 0, "Info.plist: " + text.strip())
    code, text = run(["plutil", "-convert", "json", "-o", "-", plist])
    info = json.loads(text) if code == 0 else {}
    exts = sorted({e for d in info.get("CFBundleDocumentTypes", []) for e in d.get("CFBundleTypeExtensions", [])})
    types = sorted({t for d in info.get("CFBundleDocumentTypes", []) for t in d.get("LSItemContentTypes", [])})
    exported = {t.get("UTTypeIdentifier"): t.get("UTTypeTagSpecification", {}).get("public.filename-extension")
                for t in info.get("UTExportedTypeDeclarations", [])}
    p.say(f"Info.plist: version {info.get('CFBundleShortVersionString')} ({info.get('CFBundleVersion')}), bundle id "
          f"{info.get('CFBundleIdentifier')}, minimum macOS {info.get('LSMinimumSystemVersion')}")
    p.say(f"Info.plist: document types: extensions {exts}, content types {types}")
    p.say(f"Info.plist: exported types: {exported}")
    p.check(any("scm" in (v or []) for v in exported.values()) and any("scx" in (v or []) for v in exported.values()),
            "the map types .scm and .scx are exported types")
    p.check(any(t in types for t in exported), "the document types open the exported map types")
    # The Quick Look extensions: in the bundle, registered.
    run([LSREGISTER, "-f", "-R", app])
    appexes = sorted(glob.glob(os.path.join(app, "Contents", "PlugIns", "*.appex")))
    p.check(len(appexes) >= 2, "Quick Look extensions in the bundle: " + ", ".join(os.path.basename(x) for x in appexes))
    for x in appexes:
        code, ident = run(["plutil", "-extract", "CFBundleIdentifier", "raw", os.path.join(x, "Contents", "Info.plist")])
        ident = ident.strip()
        run(["pluginkit", "-a", x])
        found = ""
        for _ in range(10):  # (pluginkit learns of it in a moment)
            code, text = run(["pluginkit", "-m", "-v", "-i", ident])
            if ident in text:
                found = text.strip()
                break
            time.sleep(1)
        p.check(bool(found), f"{os.path.basename(x)} ({ident}) is registered: {found or 'pluginkit -m does not list it'}")
    # Quick Look's thumbnail of a map.
    scx = os.path.join(out, "Release Test.scx")
    write_test_map(scx)
    thumbs = os.path.join(out, "quicklook")
    os.makedirs(thumbs, exist_ok=True)
    run(["qlmanage", "-r"])
    run(["qlmanage", "-r", "cache"])
    code, text = run(["qlmanage", "-t", "-s", "256", "-o", thumbs, scx], timeout=60)
    p.say(text)
    made = glob.glob(os.path.join(thumbs, "*.png"))
    if made:
        p.check(os.path.getsize(made[0]) > 1000, f"Quick Look's thumbnail of the map: {made[0]} ({os.path.getsize(made[0])} bytes)")
    else:
        p.say("note: qlmanage made no thumbnail (on GitHub's machine Quick Look may not load a new extension at once); "
              "the extensions are in the bundle and registered")
    code, text = run(["mdls", "-name", "kMDItemContentType", "-name", "kMDItemContentTypeTree", scx])
    p.say("mdls: " + " ".join(text.split()))
    return p.end(), exe


def part_snapshots(a, out, exe):
    p = Part("snapshots", out)
    env = app_env(out, "snapshots")
    code, text = run([sys.executable, "-I", os.path.join(ROOT, "tools", "sdl_snapshots.py"), "--exe", exe, "--out",
                      os.path.join(out, "snapshots")], env=env, timeout=3600)
    summary = text[text.rfind("\n\n", 0, len(text) - 2):] if "\n\n" in text else text
    p.say(summary.strip())
    m = re.search(r"(\d+) passed, (\d+) failed, (\d+) skipped", text)
    p.check(code == 0 and m is not None and m.group(2) == "0",
            f"tools/sdl_snapshots.py: {m.group(0) if m else 'no summary'} (exit code {code})")
    return p.end()


def stats_lines(text):
    return [(int(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4)), int(m.group(5)), float(m.group(6)), float(m.group(7)))
            for m in re.finditer(r"^stats: loop (\d+), step \d+: working set ([-\d.]+) MB \(peak [-\d.]+\), private ([-\d.]+) MB, GPU ([-\d.]+) MB "
                                 r"\(shared [-\d.]+\), (\d+) bitmaps .*frames \d+, ([\d.]+) ms on average, ([\d.]+) ms at most", text, re.M)]


def part_soak(a, out, exe):
    p = Part("soak", out)
    script = os.path.join(out, "soak-nodata.txt")
    with open(script, "w", encoding="utf-8") as f:
        f.write(sdl_snapshots.SCRIPTS["nodata"])
    t0 = time.time()
    code, text = run([exe, "--snapshot", os.path.join(out, "soak.png"), "--noinstall", "--scale", "1", "--script", f"{script}:{a.minutes}m"],
                     env=app_env(out, "soak"), timeout=a.minutes * 60 + 600)
    took = time.time() - t0
    with open(os.path.join(out, "soak-output.txt"), "w", encoding="utf-8") as f:
        f.write(text)
    stats = stats_lines(text)
    p.check(code == 0 and "wrote " in text, f"the session ran for {took:.0f} s (exit code {code})")
    m = re.search(r"^script: (\d+) times over in (\d+) s", text, re.M)
    p.check(m is not None and int(m.group(2)) >= a.minutes * 60, f"the script: {m.group(0) if m else 'no end line'} (at least {a.minutes} minutes)")
    p.check("(WRONG)" not in text, "no (WRONG) line")
    if not p.check(len(stats) >= 3, f"{len(stats)} stats lines"):
        return p.end()
    for s in stats[:2] + stats[len(stats) // 2:len(stats) // 2 + 1] + stats[-2:]:
        p.say(f"  loop {s[0]}: resident {s[1]:.1f} MB, footprint {s[2]:.1f} MB, Metal {s[3]:.1f} MB, {s[4]} bitmaps, frames {s[5]:.1f} ms on average, {s[6]:.1f} ms at most")
    # After the first minute (the fonts and the pictures are made by then), the memory stays.
    loops_per_minute = max(1, len(stats) // max(1, a.minutes))
    base, last = stats[min(len(stats) - 1, loops_per_minute)], stats[-1]
    p.check(base[1] > 0 and base[2] > 0, "the memory is known (Mach's task info)")
    p.check(last[1] <= base[1] * 1.10 + 20, f"resident memory flat: {base[1]:.1f} MB after the first minute, {last[1]:.1f} MB at the end")
    p.check(last[2] <= base[2] * 1.10 + 20, f"footprint flat: {base[2]:.1f} MB after the first minute, {last[2]:.1f} MB at the end")
    p.check(last[3] <= base[3] * 1.10 + 8, f"Metal's memory flat: {base[3]:.1f} MB after the first minute, {last[3]:.1f} MB at the end")
    p.check(last[4] <= base[4], f"bitmaps with a texture: {base[4]} after the first minute, {last[4]} at the end")
    return p.end()


def part_window(a, out, exe):
    p = Part("window", out)
    env = app_env(out, "window")
    code, text = run([sys.executable, "-I", os.path.join(ROOT, "tools", "sdl_window_test.py"), "--exe", exe, "--fullscreen", "--timeout", "180"],
                     env=env, timeout=240)
    p.say(text)
    p.check(code == 0 and "window test: passed" in text, f"tools/sdl_window_test.py (exit code {code})")
    for what in ("swapchain:", "display:", "keyboard focus:", "minimized:", "restored:", "full screen on:", "full screen off:"):
        p.check(re.search(r"^wintest: " + re.escape(what), text, re.M) is not None, f"a line \"{what}\"")
    m = re.search(r"GPU (\w+)", text)
    p.check(m is not None and m.group(1) == "metal", f"the GPU driver: {m.group(1) if m else '?'}")
    code, text = run([exe, "--snapshot", os.path.join(out, "window-checks.png"), "--lang", "en", "--check", "window", "--check", "menus",
                      "--check", "dock"], env=app_env(out, "checks"), timeout=120)
    p.say(text)
    p.check(code == 0 and "(WRONG)" not in text, f"--check window, menus, dock (exit code {code})")
    for what in ("window: ", "menubar: ", "dock: "):
        p.check(re.search("^" + what, text, re.M) is not None, f"lines \"{what.strip()}\"")
    return p.end()


def part_text(a, out, exe):
    p = Part("text", out)
    env = app_env(out, "text")
    code, text = run([sys.executable, "-I", os.path.join(ROOT, "tools", "sdl_snapshots.py"), "--exe", exe, "--out",
                      os.path.join(out, "text"), "--only", "text-input-cjk,start"], env=env, timeout=600)
    p.say("\n".join(x for x in text.splitlines() if re.match(r"^(  focus:|stardraft_editor: fonts|text-input-cjk:|start:)", x)))
    p.check(code == 0, f"text-input-cjk and start (exit code {code})")
    m = re.search(r"^stardraft_editor: fonts (\S+)", text, re.M)
    p.check(m is not None and m.group(1).startswith("SFNS"), f"the UI font: {m.group(1) if m else '?'} (SF Pro: SFNS)")
    return p.end()


def part_crash(a, out, exe):
    p = Part("crash", out)
    env = app_env(out, "crash")
    folder = os.path.join(env["HOME"], "Library", "Application Support", "StarDraft", "crashes")
    for args in (["--crash-test"], ["--crash-test", "stack"]):
        before = set(os.listdir(folder)) if os.path.isdir(folder) else set()
        code, text = run([exe] + args, env=env, timeout=120)
        p.say(text)
        p.check(code is not None and code < 0, f"{' '.join(args)}: the program ends with a signal (exit code {code})")
        new = sorted(set(os.listdir(folder)) - before) if os.path.isdir(folder) else []
        if not p.check(len(new) == 1, f"{' '.join(args)}: one new report in {folder}: {new}"):
            continue
        d = os.path.join(folder, new[0])
        with open(os.path.join(d, "report.txt"), encoding="utf-8", errors="replace") as f:
            report = f.read()
        p.say("report.txt:\n" + "\n".join("  " + x for x in report.splitlines()[:14]))
        p.check(report.startswith("StarDraft ") and re.search(r"^macOS \d+", report, re.M) is not None, "its header: the program and the macOS version")
        p.check(re.search(r"^Signal \d+ \(", report, re.M) is not None and "\nStack:\n" in report and "stardraft+0x" in report,
                "the signal and the stack (in stardraft)")
        sent = open(os.path.join(d, "sent"), encoding="utf-8").read().strip() if os.path.exists(os.path.join(d, "sent")) else ""
        p.check(sent == "a test run: not sent", f"not sent: \"{sent}\"")
        time.sleep(1.1)  # (a report's folder has the second's name)
    code, text = run([exe, "--snapshot", os.path.join(out, "crash-check.png"), "--check", "crashes"], env=env, timeout=120)
    p.say(text)
    p.check(len(re.findall(r"^crashes: report \S+, report\.txt \d+ bytes, crash\.dmp no, a test run: not sent", text, re.M)) == 2,
            "--check crashes lists the two reports, not sent")
    p.check("crashes: the next start: nothing (no report waits)" in text, "the next start sends nothing")
    return p.end()


def part_update(a, out, exe):
    p = Part("update", out)
    env = app_env(out, "update")
    release = os.path.join(out, "release-latest.json")
    with open(release, "w", encoding="utf-8") as f:
        f.write(sdl_snapshots.RELEASE_JSON)
    code, text = run([exe, "--snapshot", os.path.join(out, "update.png"), "--check", "release:" + release], env=env, timeout=120)
    p.say(text)
    problem = sdl_snapshots.check_release(text)
    p.check(code == 0 and problem is None, f"Check for updates with the release: {problem or 'finds it'} (exit code {code})")
    m = re.search(r"Check for updates: opens (\S+)", text)
    p.say(f"Check for updates opens: {m.group(1) if m else '?'}")
    return p.end()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dmg", required=True, help="the disk image (the macos job's)")
    ap.add_argument("--out", required=True, help="a folder for the outputs")
    ap.add_argument("--minutes", type=int, default=10, help="the session's minutes (default 10)")
    ap.add_argument("--only", help="only these parts (comma-separated): dmg is always first")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    a.dmg = os.path.abspath(a.dmg)
    os.makedirs(a.out, exist_ok=True)
    apps = os.path.join(a.out, "Applications")
    if os.path.isdir(os.path.join(apps, "StarDraft.app")):
        shutil.rmtree(os.path.join(apps, "StarDraft.app"))  # (the --out folder's own copy from an earlier run)
    os.makedirs(apps, exist_ok=True)
    print(f"macOS {run(['sw_vers', '-productVersion'])[1].strip()}, {run(['uname', '-m'])[1].strip()}", flush=True)
    code, text = run(["system_profiler", "SPDisplaysDataType"])
    print(text, flush=True)
    results = []
    line, exe = part_dmg(a, a.out, apps)
    results.append(line)
    only = set(a.only.split(",")) if a.only else None
    if exe and os.path.exists(exe):
        for name, part in (("update", part_update), ("crash", part_crash), ("text", part_text), ("window", part_window),
                           ("snapshots", part_snapshots), ("soak", part_soak)):
            if only is None or name in only:
                results.append(part(a, a.out, exe))
    summary = "\n".join(results) + "\n"
    with open(os.path.join(a.out, "summary.txt"), "w", encoding="utf-8") as f:
        f.write(summary)
    print("\n" + summary, end="")
    return 0 if all(": passed" in r for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
