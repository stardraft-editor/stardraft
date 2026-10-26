# How easy the ground is to walk, and where the sprites are: python clutter.py <map> [<map> ...] [--json out.json]
#   small obstacles: unwalkable pieces of 1-8 tiles with walkable ground all round (a doodad in the open), for each
#     1000 tiles; and how much of the walkable ground is within 48 px of one (units walk round them).
#   tight ground: the share of the walkable ground where a medium unit (32 px) does not fit.
#   sprites: for each 1000 tiles; the share on walkable ground; their distance to the nearest ground that is not
#     walkable (a cliff, water, a doodad), median; the share within 64 px of one; the share in groups (another sprite
#     within 96 px).
import json, os, sys
import numpy as np
from scipy import ndimage
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sdcli import cli


def clutter(path):
    opened = not os.path.isdir(path + '.work')
    if opened: cli('open', path)
    try:
        g = cli('get', path, 'summary', 'walk', 'sprites')
    finally:
        if opened: cli('close', path, '--discard', ok=False)
    s = g['summary']
    a = np.array([list(r) for r in g['walk']['rows']])
    walk = np.isin(a, ['0', '1', '2', 'x'])          # (under a resource: walkable ground)
    tiles = s['width'] * s['height']
    lab, n = ndimage.label(~walk)
    sizes = np.bincount(lab.ravel())
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]])))
    small = np.zeros(n + 1, bool)
    for k in range(1, n + 1):
        if 16 <= sizes[k] <= 8 * 16 and k not in border: small[k] = True   # 1-8 tiles (16 minitiles a tile)
    small_mask = small[lab]
    near_small = ndimage.binary_dilation(small_mask, iterations=6) & walk   # 48 px
    clear = ndimage.distance_transform_edt(walk)
    medium = ndimage.binary_dilation(clear >= 2.5, structure=np.ones((3, 3)), iterations=2) & walk
    sp = [x for x in g['sprites'] if not x.get('doodad_overlay')]
    dist = ndimage.distance_transform_edt(walk) * 8
    ds, on_walk, grouped = [], 0, 0
    for x in sp:
        cy, cx = min(a.shape[0] - 1, x['y'] // 8), min(a.shape[1] - 1, x['x'] // 8)
        on_walk += walk[cy, cx]
        ds.append(float(dist[cy, cx]))
        grouped += any(abs(o['x'] - x['x']) < 96 and abs(o['y'] - x['y']) < 96 for o in sp if o is not x)
    return dict(file=os.path.basename(path), name=s['name'], tileset=s['tileset'],
                small_obstacles_per_1000=round(int(small.sum()) * 1000 / tiles, 1),
                near_small_share=round(float(near_small.sum() / walk.sum()), 3),
                tight_share=round(float(1 - medium.sum() / walk.sum()), 3),
                sprites_per_1000=round(len(sp) * 1000 / tiles, 1),
                sprites_on_walkable=round(on_walk / len(sp), 2) if sp else None,
                sprite_edge_dist_med=round(float(np.median(ds)), 0) if ds else None,
                sprites_near_edge=round(sum(d <= 64 for d in ds) / len(ds), 2) if ds else None,
                sprites_grouped=round(grouped / len(sp), 2) if sp else None)


if __name__ == '__main__':
    args = sys.argv[1:]
    out = None
    if '--json' in args:
        i = args.index('--json'); out = args[i + 1]; args = args[:i] + args[i + 2:]
    res = []
    for p in args:
        try:
            r = clutter(p)
        except (SystemExit, Exception) as e:
            print(f'{os.path.basename(p)}: {e}'); continue
        res.append(r)
        print(' '.join(f'{k} {v}' for k, v in r.items() if k not in ('name',)))
    if out: json.dump(res, open(out, 'w', encoding='utf-8'), indent=1)
