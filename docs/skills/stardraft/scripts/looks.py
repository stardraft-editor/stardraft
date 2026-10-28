# Measure the looks of maps: the terrain's mix and patches, the way cliffs and shores run, the doodads and sprites.
#   python looks.py <map> [<map> ...]       (an open map: its working copy; else the file is opened and closed again)
#   python looks.py --json out.json <maps>  (also the numbers as JSON)
import json, math, os, sys
import numpy as np
from scipy import ndimage
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sdcli import cli


def edge_angles(mask):
    """Where a 0/1 mask changes: the share of its edge along the axes, along the tiles' 2:1 diagonals, and other ways."""
    m = ndimage.gaussian_filter(mask.astype(float), 2.0)
    gy, gx = np.gradient(m)
    mag = np.hypot(gx, gy)
    sel = mag > 0.08
    if sel.sum() == 0: return dict(axis=0.0, diagonal=0.0, other=0.0, length=0)
    a = (np.degrees(np.arctan2(gy[sel], gx[sel])) + 90) % 180  # the edge's way (0: left-right)
    d = np.minimum(a, 180 - a)  # 0..90 from horizontal
    axis = ((d < 12) | (d > 78)).mean()
    diag = ((np.abs(d - 26.57) < 10)).mean()
    return dict(axis=round(float(axis), 3), diagonal=round(float(diag), 3), other=round(float(1 - axis - diag), 3), length=int(sel.sum()))


def edge_runs(mask):
    """How straight the edges of a 0/1 mask run: the edge cells (where the mask changes), each with its way (left-right,
    up-down, the two 2:1 diagonals, or other); a run is a connected piece of one way. Gives the edge's length (px), the
    length-weighted mean run (px), and the turns (runs) for each 1000 px of edge."""
    b = np.zeros(mask.shape, bool)
    b[:, :-1] |= mask[:, :-1] != mask[:, 1:]
    b[:-1, :] |= mask[:-1, :] != mask[1:, :]
    if b.sum() == 0: return dict(length=0, mean_run=0.0, turns_per_1000=0.0)
    m = ndimage.gaussian_filter(mask.astype(float), 2.0)
    gy, gx = np.gradient(m)
    a = (np.degrees(np.arctan2(gy, gx)) + 90) % 180
    cls = np.full(mask.shape, 4)
    cls[(a < 12) | (a > 168)] = 0
    cls[np.abs(a - 90) < 12] = 1
    cls[np.abs(a - 26.57) < 10] = 2
    cls[np.abs(a - 153.43) < 10] = 3
    runs = []
    for c in range(5):
        lab, n = ndimage.label(b & (cls == c), structure=np.ones((3, 3)))
        if n: runs += list(np.bincount(lab.ravel())[1:])
    runs = np.array(runs, float) * 8
    total = runs.sum()
    return dict(length=int(total), mean_run=round(float((runs * runs).sum() / total), 1), turns_per_1000=round(len(runs) * 1000 / total, 2))


def looks(path):
    opened = not os.path.isdir(path + '.work')   # an open map: its working copy as it is now
    if opened: cli('open', path)
    try:
        g = cli('get', path, 'summary', 'terrain', 'terrains', 'doodads', 'walk', '--step', '32')
    finally:
        if opened: cli('close', path, '--discard', ok=False)
    s = g['summary']
    names = {t['id']: t['name'] for t in g['terrains']}
    T = np.array(g['terrain']['rows'])
    tiles = s['width'] * s['height']   # (T: one value per tile, --step 32)
    share = {}
    for k in np.unique(T):
        share[names.get(int(k), str(k))] = round(float((T == k).sum() / tiles), 3)
    diff = (np.count_nonzero(T[:, 1:] != T[:, :-1]) + np.count_nonzero(T[1:, :] != T[:-1, :])) / (T[:, 1:].size + T[1:, :].size)
    patches, sizes = 0, []
    for k in np.unique(T):
        lab, n = ndimage.label(T == k)
        if n: sizes += list(np.bincount(lab.ravel())[1:])
        patches += n
    a = np.array([list(r) for r in g['walk']['rows']])
    walk = np.isin(a, ['0', '1', '2', 'x'])
    lvl = np.full(a.shape, -1)
    for k in '012': lvl[a == k] = int(k)
    # high ground: fill the cells that are not walkable from the nearest walkable cell, so that a cliff is one edge
    idx = ndimage.distance_transform_edt(lvl < 0, return_distances=False, return_indices=True)
    filled = lvl[idx[0], idx[1]]
    cliffs = edge_angles(filled >= 1)
    shores = edge_angles(walk)
    cliffs.update({'run_' + k: v for k, v in edge_runs(filled >= 1).items()})
    shores.update({'run_' + k: v for k, v in edge_runs(walk).items()})
    doodads = g['doodads']
    dnames = {}
    for d in doodads: dnames[d['name']] = dnames.get(d['name'], 0) + 1
    return dict(file=os.path.basename(path), name=s['name'], tileset=s['tileset'], w=s['width'], h=s['height'],
                terrain_share=dict(sorted(share.items(), key=lambda x: -x[1])), terrains_used=sum(1 for v in share.values() if v >= 0.01),
                biggest_share=max(share.values()), changes=round(float(diff), 3), patches_per_1000_tiles=round(patches * 1000 / tiles, 1),
                median_patch=float(np.median(sizes)) if sizes else 0, cliffs=cliffs, walk_edges=shores,
                doodads_per_1000_tiles=round(len(doodads) * 1000 / tiles, 1), sprites_per_1000_tiles=round(s['sprites'] * 1000 / tiles, 1),
                doodad_names=dict(sorted(dnames.items(), key=lambda x: -x[1])[:15]))


if __name__ == '__main__':
    args = sys.argv[1:]
    out = None
    if args and args[0] == '--json': out, args = args[1], args[2:]
    res = []
    for p in args:
        try:
            r = looks(p)
        except (SystemExit, Exception) as e:
            print(f'{os.path.basename(p)}: {e}'); continue
        res.append(r)
        c, w = r['cliffs'], r['walk_edges']
        print(f"{r['file'][:28]:28} {r['tileset']:9} terrains {r['terrains_used']:2}  biggest {r['biggest_share']:.2f}  changes {r['changes']:.3f}  "
              f"patches/1000 {r['patches_per_1000_tiles']:5}  cliffs axis {c['axis']:.2f} diag {c['diagonal']:.2f}  "
              f"edges axis {w['axis']:.2f} diag {w['diagonal']:.2f}  doodads/1000 {r['doodads_per_1000_tiles']}  sprites/1000 {r['sprites_per_1000_tiles']}  "
              f"cliff run {c['run_mean_run']} turns/1000px {c['run_turns_per_1000']}  edge run {w['run_mean_run']} turns/1000px {w['run_turns_per_1000']}  "
              f"cliff px/tile {c['run_length'] / (r['w'] * r['h']):.2f}")
    if out: json.dump(res, open(out, 'w', encoding='utf-8'), indent=1)
