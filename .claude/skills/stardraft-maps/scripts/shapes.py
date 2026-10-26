# Make terrain less geometric: roughen the edges of cliffs and shores with small blobs, as the ladder maps' edges turn
# every 60-85 px (looks.py: "cliff run"). Used by a build script after the layout and before the texture and ramps:
#
#   import shapes
#   ops = shapes.roughen(cli, M, keep=[[l, t, r, b], ...], count=120, seed=3, half=lambda x, y: x < W // 2)
#   apply(ops, "Roughen", turns=2)
#
# Each blob sits on an edge between two terrains of the map (from `get terrain --step 32`): half of them push the
# inner terrain out (a bulge), half the outer one in (a bite). No blob comes near the `keep` areas (bases, ramps,
# chokes) or into a narrow passage (the walkable ground must stay wider than `min_gap` px each way).
import random
import numpy as np
from scipy import ndimage


def roughen(cli, M, keep=(), count=120, seed=3, half=None, min_gap=320, sizes=(1, 1, 2)):
    t = cli('get', M, 'terrain', 'terrains', 'walk', '--step', '32')
    T = np.array(t['terrain']['rows'])           # terrain id per tile
    names = {x['id']: x['name'] for x in t['terrains']}
    a = np.array([list(r) for r in t['walk']['rows']])
    walk = np.isin(a, ['0', '1', '2'])
    room = ndimage.distance_transform_edt(walk) * 8   # px from walkable ground to the nearest ground that is not
    h, w = T.shape
    # edge tiles: a neighbor (4 tiles away at most) has another terrain
    edge = np.zeros(T.shape, bool)
    for dy, dx in ((0, 1), (1, 0)):
        d = T[:h - dy, :w - dx] != T[dy:, dx:]
        edge[:h - dy, :w - dx] |= d
        edge[dy:, dx:] |= d
    ys, xs = np.nonzero(edge)
    rng = random.Random(seed)
    ops, taken = [], []
    tries = 0
    while len(ops) < count and tries < 40 * count:
        tries += 1
        k = rng.randrange(len(xs))
        tx, ty = int(xs[k]), int(ys[k])
        x, y = tx * 32 + 16, ty * 32 + 16
        if half and not half(x, y): continue
        size = rng.choice(sizes)
        r = 64 * size + 64
        if any(l - r <= x <= rr + r and tp - r <= y <= b + r for l, tp, rr, b in keep): continue
        if any(abs(x - px) < 160 and abs(y - py) < 96 for px, py in taken): continue
        # the two terrains of the edge: here and the most common other one round it
        here = T[ty, tx]
        around = T[max(0, ty - 3):ty + 4, max(0, tx - 3):tx + 4].ravel()
        others = [v for v in around if v != here]
        if not others: continue
        there = max(set(others), key=others.count)
        terrain = here if rng.random() < 0.5 else there
        # not into a narrow passage: the walkable ground near it stays wide
        cy, cx = y // 8, x // 8
        near = room[max(0, cy - 24):cy + 25, max(0, cx - 24):cx + 25]
        if near.size and 0 < near.max() < min_gap / 2: continue
        taken.append((x, y))
        ops.append({"op": "paint_terrain", "terrain": names.get(int(terrain), int(terrain)), "points": [[x, y]],
                    "brush": {"shape": "circle", "size": size}})
    return ops


# --- Organic shapes ------------------------------------------------------------------------------------------------
# blob(): an outline round (cx, cy) whose radius waves (a few sine waves with random phases); worm(): a band along a
# path whose width waves. fill(): `paint_terrain` strokes, one per row of tiles, that paint the inside of an outline
# (the brush makes the edge the terrain's own iso edge). Paint a straight `rect` where a ramp goes (ramps need a
# straight edge).
import math


def blob(cx, cy, rx, ry, amp=0.22, seed=1, waves=(2, 3, 5, 7), n=96):
    rng = random.Random(seed)
    ph = [rng.uniform(0, 2 * math.pi) for _ in waves]
    am = [rng.uniform(0.4, 1.0) / (1 + i * 0.6) for i, _ in enumerate(waves)]
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        r = 1 + amp * sum(a * math.sin(w * t + p) for a, w, p in zip(am, waves, ph)) / sum(am)
        pts.append((cx + rx * r * math.cos(t), cy + ry * r * math.sin(t)))
    return pts


def worm(path, width, amp=0.35, seed=1, step=48):
    """A band along `path` ([(x, y), ...]), `width` px wide (it waves by `amp`)."""
    rng = random.Random(seed)
    ph = [rng.uniform(0, 2 * math.pi) for _ in range(3)]
    pts = []
    for (x0, y0), (x1, y1) in zip(path, path[1:]):
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) // step))
        pts += [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n)]
    pts.append(path[-1])
    left, right = [], []
    total = 0.0
    for i, (x, y) in enumerate(pts):
        a, b = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy) or 1
        nx, ny = -dy / l, dx / l
        if i: total += math.hypot(x - pts[i - 1][0], y - pts[i - 1][1])
        hw = width / 2 * (1 + amp * (math.sin(total / 180 + ph[0]) * 0.6 + math.sin(total / 77 + ph[1]) * 0.4))
        left.append((x + nx * hw, y + ny * hw))
        right.append((x - nx * hw, y - ny * hw))
    return left + right[::-1]


def rect(l, t, r, b):
    return [(l, t), (r, t), (r, b), (l, b)]


def fill(poly, terrain, brush=1):
    """`paint_terrain` strokes that paint the inside of the outline `poly`, a row of tiles at a time."""
    ops = []
    ys = [p[1] for p in poly]
    y = (int(min(ys)) // 32) * 32 + 16
    while y < max(ys):
        xs = []
        for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
            if (y0 <= y < y1) or (y1 <= y < y0):
                xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            a, b = int(a + 32) // 32 * 32 + 16, int(b - 32) // 32 * 32 + 16
            if b >= a: ops.append({"op": "paint_terrain", "terrain": terrain, "points": [[a, y], [b, y]], "brush": {"shape": "diamond", "size": brush}})
        y += 32
    return ops


def open_doodads(cli, M, max_px=192, corridor=400, wall_px=40, skip=('Cliff', 'Coastal Cliff', 'Bridges')):
    """The indexes of the doodads in the way: their center more than `max_px` from a cliff, water or other large
    ground that is not walkable (the open: the ladder maps keep their small doodads near such edges), or in a passage
    narrower than `corridor` px (by the large ground only) and not against its wall (`wall_px`): there they make a
    choke narrower. Give them to `delete_doodads`."""
    g = cli('get', M, 'walk', 'doodads')
    a = np.array([list(r) for r in g['walk']['rows']])
    walk = np.isin(a, ['0', '1', '2', 'x'])
    lab, n = ndimage.label(~walk)
    sz = np.bincount(lab.ravel()); sz[0] = 0
    big = (sz[lab] > 128) & ~walk
    lvl = np.full(a.shape, -1)
    for k in '012': lvl[a == k] = int(k)
    chg = np.zeros(a.shape, bool)
    chg[:, :-1] |= (lvl[:, :-1] >= 0) & (lvl[:, 1:] >= 0) & (lvl[:, :-1] != lvl[:, 1:])
    chg[:-1, :] |= (lvl[:-1, :] >= 0) & (lvl[1:, :] >= 0) & (lvl[:-1, :] != lvl[1:, :])
    dist = ndimage.distance_transform_edt(~(big | chg)) * 8
    # narrow passages: on the passages' middle lines (local maxima of the distance to the large ground), the parts
    # narrower than `corridor`; their cross-section is the disk of that half width round each such point
    clear = ndimage.distance_transform_edt(~big)
    ridge = (~big) & (clear >= ndimage.maximum_filter(clear, size=3)) & (clear > 1) & (2 * clear * 8 < corridor)
    narrow = np.zeros(a.shape, bool)
    for y, x in zip(*np.nonzero(ridge)):
        h = int(clear[y, x]) + 1
        y0, y1, x0, x1 = max(0, y - h), min(a.shape[0], y + h + 1), max(0, x - h), min(a.shape[1], x + h + 1)
        yy, xx = np.ogrid[y0:y1, x0:x1]
        narrow[y0:y1, x0:x1] |= (yy - y) ** 2 + (xx - x) ** 2 <= h * h
    out = []
    for d in g['doodads']:
        if d['name'] in skip: continue
        y, x = min(a.shape[0] - 1, d['y'] // 8), min(a.shape[1] - 1, d['x'] // 8)
        if dist[y, x] > max_px or (narrow[y, x] and dist[y, x] > wall_px): out.append(d['index'])
    return out


def narrow_rects(cli, M, corridor=400, block=128):
    """`keep_clear` areas for `decorate`: the narrow passages (narrower than `corridor` px) of the ground as it is now
    (call it before the doodads), as squares of `block` px. A doodad there would make a choke narrower."""
    a = np.array([list(r) for r in cli('get', M, 'walk')['walk']['rows']])
    walk = np.isin(a, ['0', '1', '2', 'x'])
    lab, n = ndimage.label(~walk)
    sz = np.bincount(lab.ravel()); sz[0] = 0
    big = (sz[lab] > 128) & ~walk
    clear = ndimage.distance_transform_edt(~big)
    ridge = (~big) & (clear >= ndimage.maximum_filter(clear, size=3)) & (clear > 1) & (2 * clear * 8 < corridor)
    narrow = np.zeros(a.shape, bool)
    for y, x in zip(*np.nonzero(ridge)):
        h = int(clear[y, x]) + 1
        y0, y1, x0, x1 = max(0, y - h), min(a.shape[0], y + h + 1), max(0, x - h), min(a.shape[1], x + h + 1)
        yy, xx = np.ogrid[y0:y1, x0:x1]
        narrow[y0:y1, x0:x1] |= (yy - y) ** 2 + (xx - x) ** 2 <= h * h
    narrow &= ~big
    c = block // 8
    out = []
    for by in range(0, a.shape[0], c):
        for bx in range(0, a.shape[1], c):
            if narrow[by:by + c, bx:bx + c].any(): out.append([bx * 8, by * 8, bx * 8 + block, by * 8 + block])
    return out


def back(base, terrain='High Dirt', gap=40, depth=360, amp=0.18, seed=1):
    """The outline of terrain behind a base's mineral line (`base`: a place_base op, side left or right), as on the
    ladder maps (80 % of their bases have a cliff, water, a structure or the map's edge within 96 px behind the
    fields; backing.py): a wavy blob that starts `gap` px behind the outer fields. Paint it before the bases with
    fill(back(...), terrain): a higher terrain makes a cliff (its cliff stands outside the painted shape, so `gap`
    leaves room for it), Water makes a shore."""
    x, y, side = base['x'], base['y'], base.get('side', 'left')
    sx = -1 if side == 'left' else 1
    inner = x + sx * (256 + gap)                 # the outer fields' far edge is 256 px from the town hall's center
    cx = inner + sx * depth / 2
    return blob(cx, y + 32, depth / 2, 300, amp=amp, seed=seed)
