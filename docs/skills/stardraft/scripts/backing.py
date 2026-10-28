# What is behind each base's mineral line: from the town hall through each field and on, the distance from the field to
# the first ground that is not walkable (a cliff, water, a structure: pieces of more than 8 tiles) or to the map's edge.
#   python backing.py <map> [<map> ...]   (an open map: its working copy)
# Prints for each base the median of its fields' distances; and for the map the share of bases backed (median <= 96 px).
import json, math, os, sys
import numpy as np
from scipy import ndimage
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sdcli import cli


def backing(path, verbose=False):
    opened = not os.path.isdir(path + '.work')
    if opened: cli('open', path)
    try:
        g = cli('get', path, 'units', 'walk')
    finally:
        if opened: cli('close', path, '--discard', ok=False)
    a = np.array([list(r) for r in g['walk']['rows']])
    passable, elev, _ = analyze.grid_arrays(g['walk'])
    walk = np.isin(a, ['0', '1', '2', 'x'])
    lab, n = ndimage.label(~walk)
    sz = np.bincount(lab.ravel()); sz[0] = 0
    big = (sz[lab] > 128) & ~walk
    h, w = a.shape
    starts = [u for u in g['units'] if u['type'] == 214 and u['player'] <= 8]
    out = []
    for grp in analyze.resource_groups(g['units']):
        m = [u for u in grp if u['type'] in analyze.MINERAL]
        if len(m) < 4: continue
        tx, ty, _ = analyze.town_hall_spot(grp, passable, elev, starts)
        ds = []
        for u in m:
            dx, dy = u['x'] - tx, u['y'] - ty
            l = math.hypot(dx, dy) or 1
            dx, dy = dx / l, dy / l
            # from the field's far side outward, 8 px a step, up to 640 px
            d = None
            for k in range(0, 81):
                px, py = u['x'] + dx * (24 + 8 * k), u['y'] + dy * (12 + 8 * k)
                cx, cy = int(px // 8), int(py // 8)
                if cx < 0 or cy < 0 or cx >= w or cy >= h: d = 8 * k; break
                if big[cy, cx]: d = 8 * k; break
            ds.append(d if d is not None else 999)
        out.append((tx, ty, float(np.median(ds))))
    backed = sum(1 for _, _, d in out if d <= 96)
    if verbose:
        for tx, ty, d in out: print(f'   base ({tx}, {ty}): {d:.0f} px')
    return dict(file=os.path.basename(path), bases=len(out), backed=round(backed / max(1, len(out)), 2),
                median=float(np.median([d for _, _, d in out])) if out else None)


if __name__ == '__main__':
    v = '-v' in sys.argv
    for p in [x for x in sys.argv[1:] if x != '-v']:
        try:
            r = backing(p, v)
            print(f"{r['file']:24} bases {r['bases']:3}  backed (<= 96 px) {r['backed']:.2f}  median {r['median']}")
        except (SystemExit, Exception) as e:
            print(p, e)
