#!/usr/bin/env python3
"""StarDraft's quality numbers (KPIs): measured the same way each time, compared with their targets.

usage: python -I tools/kpi.py measure [--areas perf,ux,code,docs] [--quick] [--out FILE]
       python -I tools/kpi.py record <version>      measure everything, keep it as docs/dev/kpi/<version>.json
       python -I tools/kpi.py gate <version>        the release check: that version's numbers meet the targets
       python -I tools/kpi.py show                  the targets and the numbers of each release (as KPI.md has them)

Options (measure, record): --exe <program> (default build/Release/stardraft-testing.exe), --install <StarCraft folder>
(default: the usual install folder), --runs <n> (timings: the median of n runs, default 3).

The targets are in docs/dev/kpi/targets.json: for each KPI its area, unit, target and direction ("max": the number may
be at most the target; "min": at least). A KPI with no target yet ("target": null) is only recorded. A release may
accept a missed target: targets.json's "accepted" lists the KPIs for that version, with the reason (docs/dev/KPI.md).

What each area measures (the program with no window, SDL_VIDEODRIVER=offscreen, --snapshot --kpi --script):
  perf  the start (no map: the first frame from the process's start), each reference map's first frame, the slowest
        5% of the steps (an input and the frame after it) of a paint stroke (with and without map layers), a palette
        scroll and a zoom, the memory after 30 map opens and closes (per graphics driver), the video memory with no map
  ux    the snapshots (tools/sdl_snapshots.py: every workspace, mode and dialog), the UI text rules
        (tools/ui_text_lint.py), UI strings missing in a language, symbols from a font in UI text, bare "Search" cues
  code  the test programs that need no game data, unused static functions, files of more than 3,000 lines, and with
        --rebuild the compiler warnings in our own code (a clean build of stardraft_editor: slow)
  docs  the help pages' check (tools/help_build.py --check) and links in docs/ to files that are not there
--quick: no snapshots, no tests, one run of each timing (a quick look while optimizing: not for a release).
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parent.parent
KPI_DIR = ROOT / "docs" / "dev" / "kpi"
TARGETS = KPI_DIR / "targets.json"
DEFAULT_INSTALL = "C:/Program Files (x86)/Starcraft Remastered"
# The reference maps (the game's own, so that every computer with the game has them): 96 x 96, 128 x 128, 256 x 256.
MAPS = {"small": "(2)Boxer.scm", "medium": "(2)Bottleneck.scm", "large": "(8)Killing Fields.scm"}
# The test programs that need no game data (as build.yml runs them on Linux), with their arguments ({tmp}: a new folder).
TESTS = [("trigger_test", ["{tmp}/trigger_test"]), ("render_smoke_test", ["{tmp}/render_smoke.png"]), ("settings_test", ["{tmp}"]),
         ("commands_test", []), ("help_test", []), ("platform_test", ["{tmp}"]), ("kit_test", []), ("pathing_regions_test", []),
         ("map_preview_test", ["{tmp}/map_preview"])]
# Symbols that come from a font, not from the icon set (arrows other than the text's "->" one, shapes, dingbats, the
# private-use glyph fonts), as \xNNNN in a wide string literal (UI text; a character constant in the code is not one).
FONT_SYMBOL = re.compile(r"\\x(?:219[013-9A-Fa-f]|21[A-F][0-9A-Fa-f]|(?!2318|2325|232[BbF6]|238[Bb])23[0-9A-Fa-f]{2}|25[A-F][0-9A-Fa-f]|"
                         r"2[67][0-9A-Fa-f]{2}|[Ee][0-9A-Fa-f]{3}|[Ff][0-8][0-9A-Fa-f]{2})(?![0-9A-Fa-f])")
WIDE_LITERAL = re.compile(r'L"((?:[^"\\\n]|\\.)*)"')
BARE_SEARCH = re.compile(r'setPlaceholder\((?:tr\()?L"Search(?:\\x2026|\.\.\.)?"')


# --- Running the program ---------------------------------------------------------------------------------------------

def run_program(exe, args, script=None, timeout=600):
    """The program with no window (its output); `script`: the lines of a --script, written to a file of its own."""
    with tempfile.TemporaryDirectory() as tmp:
        cmd = [str(exe)] + list(args) + ["--kpi", "--snapshot", str(Path(tmp) / "shot.png")]
        if script is not None:
            sp = Path(tmp) / "script.txt"
            sp.write_text("\n".join(script) + "\n", encoding="utf-8")
            cmd += ["--script", f"{sp}:0"]
        env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "offscreen"))
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, timeout=timeout)
        return r.stdout + r.stderr


def steps(output, kinds):
    """The times of the --kpi steps of these input kinds (each its input and its frame, ms)."""
    out = []
    for m in re.finditer(r"^kpi: (\S) input ([\d.-]+) frame ([\d.]+)", output, re.M):
        if m.group(1) in kinds:
            out.append(max(0.0, float(m.group(2))) + float(m.group(3)))
    return out


def p95(values):
    if not values:
        return None
    v = sorted(values)
    return round(v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))], 1)


def stats_lines(output):
    """Each "stats:" line's memory: working set, private, GPU and the heap in use (MB)."""
    out = []
    num = lambda s: None if float(s) < 0 else float(s)  # (-1: the system did not say)
    for m in re.finditer(r"stats: .*?working set ([-\d.]+) MB.*?private ([-\d.]+) MB, GPU ([-\d.]+) MB.*?heap ([-\d.]+) MB", output):
        out.append({"working": num(m.group(1)), "private": num(m.group(2)), "gpu": num(m.group(3)), "heap": num(m.group(4))})
    return out


def growth(a, b):
    return None if a is None or b is None else round(b - a, 1)


def median(values):
    values = [v for v in values if v is not None]
    return round(statistics.median(values), 1) if values else None


# --- perf ------------------------------------------------------------------------------------------------------------

def map_path(install, name):
    return str(Path(install) / "StarCraft" / "Maps" / name).replace("\\", "/")


TERRAIN_TAB, TERRAIN_ROW = "click 95,392", "click 196,693"  # (the left panel's Terrain tab, its second terrain)


def stroke(n=40):
    """A paint stroke as n short drags across the map's middle (each one step: an input and its frame)."""
    lines, x, y = [], 520, 300
    for i in range(n):
        nx, ny = x + 14, y + 9
        lines.append(f"drag {x},{y}-{nx},{ny}")
        x, y = (nx, ny) if nx < 1150 else (520, ny)
    return lines


def measure_perf(exe, install, maps, runs, log):
    r = {}
    data = ["--install", install] if install else []
    # The start, and the memory with no map
    starts, gpu_nomap = [], []
    for _ in range(runs):
        out = run_program(exe, data, ["wait 10", "stats"])
        if m := re.search(r"^kpi: startup ([\d.]+)", out, re.M):
            starts.append(float(m.group(1)))
        if s := stats_lines(out):
            gpu_nomap.append(s[-1]["gpu"])
    r["startup_ms"] = median(starts)
    r["gpu_nomap_mb"] = median(gpu_nomap)
    log(f"start {r['startup_ms']} ms, video memory with no map {r['gpu_nomap_mb']} MB")
    # Each map's first frame (opened from the start page, as a user opens it)
    for size in ("small", "large"):
        firsts = []
        for _ in range(runs):
            out = run_program(exe, data, [f"open {map_path(install, maps[size])}", "wait 200"])
            if m := re.search(r"^first frame: ([\d.]+) ms", out, re.M):
                firsts.append(float(m.group(1)))
        r[f"first_frame_{size}_ms"] = median(firsts)
        log(f"first frame, {size} map: {r[f'first_frame_{size}_ms']} ms")
    # Painting, with and without map layers; a palette's scroll; a zoom
    medium, large = [map_path(install, maps["medium"])], [map_path(install, maps["large"])]
    scenarios = {
        "paint_p95_ms": (medium, [TERRAIN_TAB, "wait 200", TERRAIN_ROW, "wait 200"] + stroke(), "d"),
        "paint_layers_p95_ms": (medium, ["key Ctrl+Alt+N", "wait 300", TERRAIN_TAB, "wait 200", TERRAIN_ROW, "wait 200"] + stroke(), "d"),
        "palette_scroll_p95_ms": (medium, ["wait 1500"] + ["wheel 1450,600,0,-3"] * 15 + ["wheel 1450,600,0,3"] * 15, "h"),
        "zoom_p95_ms": (large, ["wait 1500"] + ["wheel 830,500,0,1", "wheel 830,500,0,1", "wheel 830,500,0,-1", "wheel 830,500,0,-1"] * 5, "h"),
    }
    for kpi, (args, script, kinds) in scenarios.items():
        values = []
        for _ in range(runs):
            values += steps(run_program(exe, data + args, script), kinds)
        r[kpi] = p95(values)
        log(f"{kpi}: {r[kpi]} ms ({len(values)} steps)")
    # The memory: the working set with the large map; 30 opens and closes of a map per graphics driver (the growth
    # from the 5th to the 30th: the first ones fill the caches)
    out = run_program(exe, data + large, ["wait 1500", "stats"])
    if s := stats_lines(out):
        r["working_set_large_mb"] = s[-1]["working"]
    cycle = []
    for i in range(30):
        cycle += [f"open {map_path(install, maps['medium'])}", "wait 100", "key Ctrl+W", "wait 100"]
        if i in (4, 29):
            cycle.append("stats")
    for driver in ("direct3d12", "vulkan"):
        s = stats_lines(run_program(exe, data + ["--driver", driver], cycle, timeout=1200))
        if len(s) >= 2:
            r[f"cycles_heap_growth_mb_{driver}"] = growth(s[0]["heap"], s[-1]["heap"])
            r[f"cycles_gpu_growth_mb_{driver}"] = growth(s[0]["gpu"], s[-1]["gpu"])
        log(f"30 opens and closes, {driver}: heap +{r.get(f'cycles_heap_growth_mb_{driver}')} MB, "
            f"video +{r.get(f'cycles_gpu_growth_mb_{driver}')} MB")
    return r


# --- ux --------------------------------------------------------------------------------------------------------------

def source_files():
    for base in ("apps", "src", "include"):
        for p in (ROOT / base).rglob("*"):
            if p.suffix in (".inc", ".cpp", ".h") and "res" not in p.parts:
                yield p


def measure_ux(exe, install, maps, quick, log):
    r = {}
    if not quick:
        with tempfile.TemporaryDirectory() as tmp:
            cmd = [sys.executable, "-I", str(ROOT / "tools" / "sdl_snapshots.py"), "--exe", str(exe), "--out", tmp, "--jobs", "8"]
            if install:
                cmd += ["--install", install, "--map", str(Path(install) / "StarCraft" / "Maps" / maps["medium"])]
            out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
            if m := re.search(r"(\d+) passed, (\d+) failed", out):
                r["snapshots_failed"] = int(m.group(2))
                log(f"snapshots: {m.group(1)} passed, {m.group(2)} failed")
    lint = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "ui_text_lint.py")], capture_output=True, text=True, encoding="utf-8")
    if m := re.search(r"(\d+) new violations", lint.stdout):
        r["ui_text_violations"] = int(m.group(1))
    # UI strings that a language does not have (the strings the lint finds, against each .tsv)
    with tempfile.TemporaryDirectory() as tmp:
        dump = Path(tmp) / "strings.json"
        subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "ui_text_lint.py"), "--dump", str(dump)], capture_output=True)
        try:
            recs = json.loads(dump.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            recs = []
    # (the trigger blocks' texts, their titles, docs and examples, apart: they are many and long)
    ui_strings = {x["text"] for x in recs if not str(x.get("surface", "")).startswith("block.")}
    block_strings = {x["text"] for x in recs if str(x.get("surface", "")).startswith("block.")}
    missing, blocks = {}, {}
    for tsv in sorted((ROOT / "apps" / "editor" / "res" / "lang").glob("*.tsv")):
        keys = {line.split("\t", 1)[0] for line in tsv.read_text(encoding="utf-8").splitlines() if "\t" in line}
        missing[tsv.stem] = len(ui_strings - keys) if ui_strings else None
        blocks[tsv.stem] = len(block_strings - keys) if block_strings else None
    r["strings_missing_max"] = max((v for v in missing.values() if v is not None), default=None)
    r["block_texts_missing_max"] = max((v for v in blocks.values() if v is not None), default=None)
    font, search, where = 0, 0, []
    for p in source_files():
        text = p.read_text(encoding="utf-8", errors="replace")
        for lit in WIDE_LITERAL.finditer(text):
            if n := len(FONT_SYMBOL.findall(lit.group(1))):
                font += n
                where.append(f"{p.relative_to(ROOT)}:{text.count(chr(10), 0, lit.start()) + 1}")
        search += len(BARE_SEARCH.findall(text))
    r["font_symbols"], r["bare_search_cues"] = font, search
    log(f"UI text: {r.get('ui_text_violations')} new violations; strings missing (most in one language): {r['strings_missing_max']} "
        f"{missing}; trigger block texts missing: {r['block_texts_missing_max']}; font symbols {font} {where[:8]}; "
        f"bare Search cues {search}")
    return r


# --- code ------------------------------------------------------------------------------------------------------------

STATIC_FN = re.compile(r"^static\s+[\w:<>,\s\*&]+?\b(\w+)\s*\(", re.M)


def measure_code(exe, quick, rebuild, log):
    r = {}
    unused, big = [], []
    texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in source_files()}
    words = {}  # (each word's count in all the files: a static function in a .inc is used by the file that includes it)
    for text in texts.values():
        for w in re.findall(r"\b[A-Za-z_]\w*\b", text):
            words[w] = words.get(w, 0) + 1
    for p, text in texts.items():
        lines = text.count("\n")
        if lines > 3000:
            big.append((lines, str(p.relative_to(ROOT))))
        if p.suffix == ".h":
            continue
        for m in STATIC_FN.finditer(text):
            name = m.group(1)
            if name not in ("inline", "const", "constexpr") and words.get(name, 0) <= 1:
                unused.append(f"{p.relative_to(ROOT)}: {name}")
    r["unused_static_functions"], r["files_over_3000_lines"] = len(unused), len(big)
    r["largest_file_lines"] = max((n for n, _ in big), default=0)
    log(f"unused static functions {len(unused)} {unused[:10]}; files over 3,000 lines {len(big)}: "
        + ", ".join(f"{f} ({n})" for n, f in sorted(big, reverse=True)[:8]))
    build = ROOT / "build"
    if not quick:
        names = [t for t, _ in TESTS]
        b = subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--target"] + names, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        failed = []
        with tempfile.TemporaryDirectory() as tmp:
            for name, args in TESTS:
                prog = build / "Release" / f"{name}.exe"
                if not prog.exists():
                    prog = build / name
                try:
                    t = subprocess.run([str(prog)] + [a.format(tmp=tmp) for a in args], capture_output=True, text=True, timeout=600,
                                       env=dict(os.environ, SDL_VIDEODRIVER="offscreen"))
                    ok = t.returncode == 0
                except (OSError, subprocess.TimeoutExpired):
                    ok = False
                if not ok:
                    failed.append(name)
        r["tests_failed"] = len(failed)
        log(f"tests: {len(TESTS) - len(failed)} of {len(TESTS)} pass" + (f" (failed: {', '.join(failed)})" if failed else "")
            + ("" if b.returncode == 0 else " (their build stopped)"))
    if rebuild:
        b = subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--target", "stardraft_editor", "--", "/t:Rebuild"],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        own = {l for l in b.stdout.splitlines() if "warning C" in l and "_deps" not in l}
        r["compiler_warnings"] = len(own)
        log(f"compiler warnings in our own code: {len(own)}")
    return r


# --- docs ------------------------------------------------------------------------------------------------------------

LINK = re.compile(r"\]\(([^)#\s]+)(?:#[^)]*)?\)")


def measure_docs(log):
    r = {}
    help_check = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "help_build.py"), "--check"], capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
    r["help_check_failed"] = 0 if help_check.returncode == 0 else 1
    broken = []
    for md in (ROOT / "docs").rglob("*.md"):
        for m in LINK.finditer(md.read_text(encoding="utf-8", errors="replace")):
            target = m.group(1)
            if re.match(r"^[a-z]+:", target) or target.startswith("/") or "<" in target:  # (a web address, a placeholder)
                continue
            if not (md.parent / target).exists():
                broken.append(f"{md.relative_to(ROOT)} -> {target}")
    r["docs_broken_links"] = len(broken)
    log(f"help check {'passes' if not r['help_check_failed'] else 'fails'}; broken links {len(broken)} {broken[:8]}")
    return r


# --- Targets and reports ---------------------------------------------------------------------------------------------

def load_targets():
    return json.loads(TARGETS.read_text(encoding="utf-8"))


def verdict(value, spec):
    """'ok', 'missed' or '-' (no target, or not measured)."""
    target = spec.get("target")
    if value is None or target is None:
        return "-"
    return "ok" if (value <= target if spec.get("dir", "max") == "max" else value >= target) else "missed"


def table(values, targets):
    rows = []
    for kpi, spec in targets["kpis"].items():
        v = values.get(kpi)
        t = spec.get("target")
        rows.append(f"  {verdict(v, spec):6} {kpi:36} {'' if v is None else v!s:>9} {spec.get('unit', ''):6} "
                    f"(target: {'none yet' if t is None else ('<= ' if spec.get('dir', 'max') == 'max' else '>= ') + str(t)})")
    return "\n".join(rows)


def measure_all(a):
    exe = Path(a.exe)
    if not exe.exists():
        raise SystemExit(f"no program {exe}: build stardraft_editor first")
    install = a.install if a.install and Path(a.install).exists() else None
    areas = set(a.areas.split(","))
    runs = 1 if a.quick else a.runs
    log = lambda s: print("  " + s, flush=True)
    values = {}
    t = time.time()
    if "perf" in areas:
        if install:
            print("perf:")
            values.update(measure_perf(exe, install, MAPS, runs, log))
        else:
            print("perf: no StarCraft folder (--install): not measured")
    if "ux" in areas:
        print("ux:")
        values.update(measure_ux(exe, install, MAPS, a.quick, log))
    if "code" in areas:
        print("code:")
        values.update(measure_code(exe, a.quick, a.rebuild, log))
    if "docs" in areas:
        print("docs:")
        values.update(measure_docs(log))
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return {"commit": commit, "date": time.strftime("%Y-%m-%d"), "quick": a.quick, "seconds": round(time.time() - t), "values": values}


def cmd_measure(a):
    result = measure_all(a)
    targets = load_targets()
    print(f"\nKPIs at {result['commit']} ({result['seconds']} s):\n" + table(result["values"], targets))
    if a.out:
        Path(a.out).write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    return 0


def cmd_record(a):
    a.quick = False
    if Path(a.exe).resolve() == (ROOT / "build" / "Release" / "stardraft-testing.exe").resolve():  # (the program of this commit)
        b = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--config", "Release", "--target", "stardraft_editor"],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        if b.returncode:
            raise SystemExit("the build stops: " + "\n".join(l for l in b.stdout.splitlines() if "error" in l)[:2000])
    if subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, capture_output=True, text=True).stdout.strip():
        print("note: changes that are not committed are in the measured program (the record names the commit)")
    result = measure_all(a)
    KPI_DIR.mkdir(parents=True, exist_ok=True)
    path = KPI_DIR / f"{a.version}.json"
    path.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    print(f"\nKPIs of {a.version} at {result['commit']}, kept in {path.relative_to(ROOT)}:\n" + table(result["values"], load_targets()))
    return 0


def cmd_gate(a):
    path = KPI_DIR / f"{a.version}.json"
    if not path.exists():
        print(f"no KPIs for {a.version}: python -I tools/kpi.py record {a.version}")
        return 1
    result = json.loads(path.read_text(encoding="utf-8"))
    # (the numbers are of this code: their commit is HEAD's, or an older one with only the KPI files and the release's
    # changelog and pictures changed after it)
    rec = result.get("commit", "")
    git = lambda *args: subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if git("merge-base", "--is-ancestor", rec, "HEAD").returncode:
        print(f"the KPIs of {a.version} are of {rec}, which is not in this branch: record {a.version} again")
        return 1
    changed = [f for f in git("diff", "--name-only", rec, "HEAD").stdout.split()
               if not f.startswith(("docs/dev/kpi/", "docs/dev/KPI.md", "docs/user/", "tools/shots/"))]
    if changed:
        print(f"the KPIs of {a.version} are of {rec}; the code changed after it ({', '.join(changed[:5])}"
              f"{', ...' if len(changed) > 5 else ''}): record {a.version} again")
        return 1
    targets = load_targets()
    accepted = set(targets.get("accepted", {}).get(a.version, {}))
    missed = [k for k, spec in targets["kpis"].items() if verdict(result["values"].get(k), spec) == "missed" and k not in accepted]
    unmeasured = [k for k, spec in targets["kpis"].items() if spec.get("target") is not None and result["values"].get(k) is None]
    print(table(result["values"], targets))
    if accepted:
        print(f"accepted for {a.version}: {', '.join(sorted(accepted))}")
    if missed or unmeasured:
        print(f"{a.version}: KPIs missed: {', '.join(missed) or '-'}; not measured: {', '.join(unmeasured) or '-'}")
        return 1
    print(f"{a.version}: every KPI meets its target")
    return 0


def cmd_show(a):
    targets = load_targets()
    def order(v):  # (the baseline and other names first, then the versions in their order)
        parts = re.split(r"[.-]", v)
        return (1, [int(x) for x in parts]) if all(x.isdigit() for x in parts) else (0, [v])
    versions = sorted((p.stem for p in KPI_DIR.glob("*.json") if p.name != "targets.json"), key=order)
    results = {v: json.loads((KPI_DIR / f"{v}.json").read_text(encoding="utf-8"))["values"] for v in versions}
    print("| KPI | Target | " + " | ".join(versions) + " |")
    print("|---|---|" + "---|" * len(versions))
    for kpi, spec in targets["kpis"].items():
        t = spec.get("target")
        tt = "-" if t is None else ("≤ " if spec.get("dir", "max") == "max" else "≥ ") + f"{t} {spec.get('unit', '')}".strip()
        cells = [("" if results[v].get(kpi) is None else str(results[v][kpi])) for v in versions]
        print(f"| {spec.get('name', kpi)} | {tt} | " + " | ".join(cells) + " |")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("measure", "record"):
        p = sub.add_parser(name)
        if name == "record":
            p.add_argument("version")
        p.add_argument("--exe", default=str(ROOT / "build" / "Release" / "stardraft-testing.exe"))
        p.add_argument("--install", default=DEFAULT_INSTALL)
        p.add_argument("--areas", default="perf,ux,code,docs")
        p.add_argument("--runs", type=int, default=3)
        p.add_argument("--quick", action="store_true")
        p.add_argument("--rebuild", action="store_true")
        p.add_argument("--out")
    sub.add_parser("gate").add_argument("version")
    sub.add_parser("show")
    a = ap.parse_args()
    return {"measure": cmd_measure, "record": cmd_record, "gate": cmd_gate, "show": cmd_show}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
