"""Makes src/mods/cmbw_tail_rules.inc: tiles for the NULL rects of the repaired rows that the mod team has not done yet, from
the mod's unused groups right after each set (the mod team lays a set's missing rects out there: their terrains.isom of
2026-11-01 filled 568 NULL rects, mostly from that run). The layouts are the mod team's:
  - 8 rects in a row (k, k+2 .. k+14, as 260-274 or 608-622): one group each, in order, subtiles 0 and 1;
  - the wall block 76-106 (16 rects): two rects a group (subtiles 8-9, then 0-1) for groups 0-4, then 96: 6.0, 98: 6.8,
    100: 5.0, 102: 5.8, 104: 7.0, 106: 7.8;
  - 198-212: 198: 2, 200: 0, 202: 3, 204: 1, 210: 1, 212: 3 (206 and 208 stay NULL).
Other NULL rects stay NULL (also a wall's second row, 560-574). Variants follow their parent. A terrain the mod team's file has (cmbw_creator_rules.inc) is left out: theirs wins.
usage: python -I tools/plugins/cmbw/cmbw_tail_rules.py <cmbw_unused_groups output .txt>  (in the repository folder;
the probe's output made with this file empty, so that the groups it fills count as unused)"""
import re, struct, sys
from collections import defaultdict


def inc(path):
    t = open(path, encoding='ascii').read()
    return bytes(int(x) for x in re.findall(r'\d+', '\n'.join(l for l in t.splitlines() if not l.lstrip().startswith('//'))))


def parse(d):
    assert d[:4] == b'SDTR'
    n, = struct.unpack_from('<H', d, 6)
    p, out = 8, {}
    for _ in range(n):
        era, groups, fp = struct.unpack_from('<BIQ', d, p); p += 13
        nt, = struct.unpack_from('<H', d, p); p += 2
        ts = []
        for _ in range(nt):
            head = struct.unpack_from('<HBHHHHIII', d, p); p += 23
            sl, = struct.unpack_from('<H', d, p); p += 2
            name = d[p:p + sl]; p += sl
            pc, = struct.unpack_from('<I', d, p); p += 4
            pieces = [list(struct.unpack_from('<HHH', d, p + 6 * i)) for i in range(pc)]; p += 6 * pc
            ts.append((head, name, pieces))
        out[era] = (groups, fp, ts)
    return out


NUL = {(0, 0), (0, 1)}
WALL = {76: (0, 8), 78: (0, 0), 80: (1, 8), 82: (1, 0), 84: (2, 8), 86: (2, 0), 88: (3, 8), 90: (3, 0), 92: (4, 8),
        94: (4, 0), 96: (6, 0), 98: (6, 8), 100: (5, 0), 102: (5, 8), 104: (7, 0), 106: (7, 8)}
PLATING = {198: (2, 0), 200: (0, 0), 202: (3, 0), 204: (1, 0), 210: (1, 0), 212: (3, 0)}


def layout(nullg):
    """slot -> (group index in the run, first subtile) for the NULL slots the layouts cover."""
    out, s = {}, set(nullg)
    if all(g in s for g in range(76, 107, 2)) and 74 not in s and 108 not in s:
        # (a wall's second row, 560-574, stays NULL: the mod team's files give no layout for it, and the run's 8
        # groups are the wall block's)
        return dict(WALL)
    if all(g in s for g in range(198, 213, 2)) and 196 not in s and 214 not in s:
        out.update(PLATING)
    for k in sorted(s):
        if k in out or k - 2 in s and k - 2 not in out and all(g in s for g in range(k - 2, k + 13, 2)):
            continue
        run = list(range(k, k + 15, 2))
        if all(g in s and g not in out for g in run) and (k - 2 not in s or k - 2 in out) and (k + 16 not in s or k + 16 in out):
            out.update({g: (i, 0) for i, g in enumerate(run)})
    return out


unused, era = defaultdict(set), None
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if m := re.match(r'== tileset (\d+)', line):
        era = int(m.group(1))
    elif (m := re.match(r'\s+groups (\d+)-(\d+)', line)) and era is not None:
        unused[era].update(range(int(m.group(1)), int(m.group(2)) + 1))

repaired = parse(inc('src/mods/cmbw_repaired_rules_prerelease.inc'))
team = parse(inc('src/mods/cmbw_creator_rules.inc'))
out, count = {}, 0
for era, (groups, fp, ts) in sorted(repaired.items()):
    theirs = {h[0] for h, _, _ in team.get(era, (0, 0, []))[2]}
    for head, name, pieces in ts:
        if head[0] in theirs or head[5]:  # (a variant takes its parent's pieces: terrainrules::apply skips its own)
            continue
        nullg = sorted({g for g, l, r in pieces if (l, r) in NUL})
        own = [v >> 4 for g, l, r in pieces for v in (l, r) if v > 1]
        if not nullg or not own:
            continue
        lay = layout(nullg)
        tail, k = [], max(own) + 1
        while k in unused[era]:
            tail.append(k); k += 1
        if not lay or max(i for i, _ in lay.values()) >= len(tail):
            continue
        for p in pieces:
            if (p[1], p[2]) in NUL and p[0] in lay:
                i, sub = lay[p[0]]
                p[1], p[2] = tail[i] * 16 + sub, tail[i] * 16 + sub + 1
                count += 1
        out.setdefault(era, (groups, fp, []))[2].append((head, name, pieces))

b = bytearray(b'SDTR') + struct.pack('<HH', 2, len(out))
for era, (groups, fp, ts) in sorted(out.items()):
    b += struct.pack('<BIQ', era, groups, fp) + struct.pack('<H', len(ts))
    for head, name, pieces in ts:
        b += struct.pack('<HBHHHHIII', *head) + struct.pack('<H', len(name)) + name + struct.pack('<I', len(pieces))
        for p in pieces:
            b += struct.pack('<HHH', *p)
ref = open('src/mods/cmbw_creator_rules.inc', 'rb').read()
nl = '\r\n' if b'\r\n' in ref else '\n'
lines = [
    "// Tiles for the NULL rects of rows the mod team has not done yet, from the mod's unused groups right after each set,",
    "// in the mod team's layouts (tools/plugins/cmbw/cmbw_tail_rules.py makes it; do not edit by hand). A terrain rules",
    "// file (version 2) the program applies over the repaired rows.",
    "",
]
for i in range(0, len(b), 32):
    lines.append(','.join(str(x) for x in b[i:i + 32]) + ',')
open('src/mods/cmbw_tail_rules.inc', 'wb').write((nl.join(lines) + nl).encode('ascii'))
print(sum(len(v[2]) for v in out.values()), 'terrains,', count, 'rects,', len(b), 'bytes')
for era, (_, _, ts) in sorted(out.items()):
    print(' ', era, ', '.join(n.decode('utf-8') for _, n, _ in ts))
