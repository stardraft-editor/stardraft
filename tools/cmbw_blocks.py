# Blocks of a brush set from cmbw_brushes' printout: each block (first group, rows) with its variants' subtiles, in
# the brushes' order. usage: python blocks.py <printout file>
import re, sys
text = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
brushes = []  # (name, [(group, sub) per row of the left column])
rows = []
for line in text:
    m = re.match(r'\s*\((Cosmonarchy_[^)]*)\)', line)
    if m:
        if rows: brushes.append((m.group(1), rows))
        rows = []
        continue
    toks = re.findall(r'(\d+)\.(\d+)', line)
    if toks:
        g, s = toks[0]
        rows.append((int(g), int(s)))
blocks = []  # [key(groups tuple), name, [subs]]
for name, rs in brushes:
    key = tuple(g for g, s in rs)
    sub = rs[0][1]
    for b in blocks:
        if b[0] == key and all(r[1] == sub for r in rs) and (sub - b[2][-1] == 2):
            b[2].append(sub)
            break
    else:
        blocks.append([key, name.split('_', 2)[-1], [sub]])
for key, name, subs in blocks:
    print('%-18s rows %-18s subs %s' % (name[:18], ','.join(map(str, key)), ','.join(map(str, subs))))
