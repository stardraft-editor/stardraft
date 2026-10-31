"""Make the custom isom terrain editor's canvas of many mod terrains and tell what each shows.

Usage: python -I tools/isom_canvas_sweep.py --exe <stardraft-testing> --map <a Cosmonarchy map> --out <folder>
       [--terrain <era>/<name> ...] [--limit N]

With no --terrain, every terrain of every tileset (the editor's list: isomrules:<era>/*). For each terrain: one run of
the program (--cmbw --dialog isomrules:<era>/<name>), its picture (<out>/<n>.png) and its line in <out>/results.tsv:
the seconds, the snapshot line's numbers and the variants and rects that have no place. A summary at the end.
"""

import argparse
import os
import re
import subprocess
import sys
import time


def run(exe, mapfile, arg, picture):
    cmd = [exe, mapfile, "--cmbw", "--dialog", "isomrules:" + arg, "--size", "1600x1100", "--snapshot", picture]
    env = dict(os.environ, STARDRAFT_SHOW_STRAYS="1")  # (the pictures frame the strays' tiles red)
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900, env=env)
    return p.stdout + p.stderr


def terrains(exe, mapfile, out):
    names = []
    for era in range(8):
        text = run(exe, mapfile, "%d/*" % era, os.path.join(out, "list%d.png" % era))
        for line in text.splitlines():
            if line.startswith("isom terrain: "):
                names.append("%d/%s" % (era, line[len("isom terrain: "):].split(" | ")[0]))
    return names


def numbers(line):
    def num(pattern):
        m = re.search(pattern, line)
        return m.group(1) if m else ""
    shown = num(r"(\d+) rects shown")
    absent = num(r"(\d+) not in them")
    variants = re.search(r"variants with a place (\d+) of (\d+)", line)
    fills = re.search(r"fill pieces with a place (\d+) of (\d+)", line)
    strays = num(r"(\d+) strays")
    no_place = re.search(r"variants with no place:([^;]*)", line)
    missing = re.findall(r"\d+\(rect \d+, \w+\)", no_place.group(1)) if no_place else []
    not_in = re.search(r"rects not in them:([^;]*)", line)
    absent_real = [r for r in (not_in.group(1).split() if not_in else []) if "(null)" not in r]
    return {
        "shown": shown, "absent": absent,
        "variants": "%s/%s" % (variants.group(1), variants.group(2)) if variants else "",
        "fills": "%s/%s" % (fills.group(1), fills.group(2)) if fills else "",
        "strays": strays, "missing": len(missing), "absent_real": len(absent_real),
        "detail": " ".join(missing) + (" | absent: " + " ".join(absent_real) if absent_real else ""),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--terrain", action="append", default=[])
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    names = a.terrain or terrains(a.exe, a.map, a.out)
    if a.limit:
        names = names[:a.limit]
    rows = []
    with open(os.path.join(a.out, "results.tsv"), "w", encoding="utf-8") as f:
        f.write("n\tterrain\tseconds\trects shown\trects not shown\tvariants with a place\tfill with a place\tstrays\t"
                "variants with no place\tnon-NULL rects not shown\tdetail\n")
        for n, name in enumerate(names, 1):
            t0 = time.time()
            text = run(a.exe, a.map, name, os.path.join(a.out, "%d.png" % n))
            line = next((l for l in text.splitlines() if l.startswith("isom rules:")), "")
            r = numbers(line)
            secs = round(time.time() - t0)
            rows.append((name, secs, r))
            f.write("%d\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%s\n" % (n, name, secs, r["shown"], r["absent"], r["variants"], r["fills"],
                                                                     r["strays"], r["missing"], r["absent_real"], r["detail"]))
            f.flush()
            print("%3d %-45s %3ds  variants %-8s missing %2d  strays %-3s  non-NULL rects not shown %d" % (
                n, name, secs, r["variants"], r["missing"], r["strays"] or "-", r["absent_real"]))
            sys.stdout.flush()
    print("terrains %d; with variants with no place %d; with strays %d; with non-NULL rects not shown %d; slowest %ds" % (
        len(rows), sum(1 for _, _, r in rows if r["missing"]), sum(1 for _, _, r in rows if r["strays"] not in ("", "0")),
        sum(1 for _, _, r in rows if r["absent_real"]), max((s for _, s, _ in rows), default=0)))


if __name__ == "__main__":
    main()
