# Measure melee map layout from stardraft-cli JSON (ladder_extract.py): python analyze.py <out>/data/*.json  (writes analysis.json)
import glob, json, math, os, re, sys
import numpy as np
from scipy import ndimage
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra

CELL = 8
MINERAL = (176, 177, 178); GEYSER = 188; START = 214


def grid_arrays(walk):
    rows = walk['rows']
    a = np.array([list(r) for r in rows])
    passable = np.isin(a, ['0', '1', '2'])
    elev = np.where(passable, np.char.isdigit(a) * 0, -1)
    for k in '012': elev[a == k] = int(k)
    terrain = passable | (a == 'x')
    return passable, elev, terrain


def build_graph(passable, cost=None):
    h, w = passable.shape
    idx = np.arange(h * w).reshape(h, w)
    rows, cols, wts = [], [], []
    for dy, dx in ((0, 1), (1, 0), (1, 1), (1, -1)):
        y0, y1 = 0, h - dy
        x0, x1 = max(0, -dx), w - max(0, dx)
        a = passable[y0:y1, x0:x1] & passable[y0 + dy:y1 + dy, x0 + dx:x1 + dx]
        if dx and dy:  # no corner cutting
            a &= passable[y0 + dy:y1 + dy, x0:x1] & passable[y0:y1, x0 + dx:x1 + dx]
        s = idx[y0:y1, x0:x1][a]; t = idx[y0 + dy:y1 + dy, x0 + dx:x1 + dx][a]
        wt = 11.3137 if dx and dy else 8.0
        ww = np.full(len(s), wt) if cost is None else wt * (cost.flat[s] + cost.flat[t]) / 2
        rows += [s, t]; cols += [t, s]; wts += [ww, ww]
    return csr_matrix((np.concatenate(wts), (np.concatenate(rows), np.concatenate(cols))), shape=(h * w, h * w))


def nearest_cell(passable, x, y, r=12):
    cx, cy = int(x) // CELL, int(y) // CELL
    best = None
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            X, Y = cx + dx, cy + dy
            if 0 <= Y < passable.shape[0] and 0 <= X < passable.shape[1] and passable[Y, X]:
                d = dx * dx + dy * dy
                if best is None or d < best[0]: best = (d, Y * passable.shape[1] + X)
    return None if best is None else best[1]


def resource_groups(units):
    res = [u for u in units if u['type'] in MINERAL or u['type'] == GEYSER]
    group = [-1] * len(res); g = 0
    for i in range(len(res)):
        if group[i] >= 0: continue
        group[i] = g; todo = [i]
        while todo:
            k = todo.pop()
            for j in range(len(res)):
                if group[j] < 0 and math.hypot(res[j]['x'] - res[k]['x'], res[j]['y'] - res[k]['y']) < 320:
                    group[j] = g; todo.append(j)
        g += 1
    return [[r for r, gg in zip(res, group) if gg == k] for k in range(g)]


def footprint(u):  # resource footprint box in px
    if u['type'] == GEYSER: return (u['x'] - 64, u['y'] - 32, u['x'] + 64, u['y'] + 32)
    return (u['x'] - 32, u['y'] - 16, u['x'] + 32, u['y'] + 16)


def town_hall_spot(group, passable, elev, starts):
    """The town hall's center: the start location in the group's reach, else the legal 4x3 spot nearest the resources."""
    cx = sum(u['x'] for u in group) / len(group); cy = sum(u['y'] for u in group) / len(group)
    for s in starts:
        if math.hypot(s['x'] - cx, s['y'] - cy) < 420: return (s['x'], s['y'], True)
    boxes = [footprint(u) for u in group]
    H, W = passable.shape
    best = None
    for ty in range(int(cy // 32) - 12, int(cy // 32) + 12):
        for tx in range(int(cx // 32) - 12, int(cx // 32) + 12):
            l, t = tx * 32, ty * 32; r, b = l + 128, t + 96
            if l < 0 or t < 0 or r > W * CELL or b > H * CELL: continue
            sub = passable[t // CELL:b // CELL, l // CELL:r // CELL]
            if not sub.all(): continue
            e = elev[t // CELL:b // CELL, l // CELL:r // CELL]
            if e.min() != e.max(): continue
            if any(l - 96 < bx1 and bx0 < r + 96 and t - 96 < by1 and by0 < b + 96 for bx0, by0, bx1, by1 in boxes): continue
            x, y = l + 64, t + 48
            score = sum(math.hypot(u['x'] - x, u['y'] - y) for u in group)
            if best is None or score < best[0]: best = (score, x, y)
    return (best[1], best[2], False) if best else (cx, cy, False)


TRANSFORMS = {
    'half_turn': lambda x, y, W, H: (W - x, H - y),
    'quarter_turn': lambda x, y, W, H: (W - y, x),
    'mirror_left_right': lambda x, y, W, H: (W - x, y),
    'mirror_top_bottom': lambda x, y, W, H: (x, H - y),
    'mirror_diagonal': lambda x, y, W, H: (y, x),
    'mirror_antidiagonal': lambda x, y, W, H: (W - y, H - x),
}


def symmetry(res, W, H):
    out = {}
    for name, f in TRANSFORMS.items():
        if name in ('quarter_turn', 'mirror_diagonal', 'mirror_antidiagonal') and W != H: continue
        hit = 0
        for u in res:
            x, y = f(u['x'], u['y'], W, H)
            if any(v['type'] in MINERAL if u['type'] in MINERAL else v['type'] == u['type'] for v in res
                   if abs(v['x'] - x) <= 48 and abs(v['y'] - y) <= 48 and (v['type'] in MINERAL) == (u['type'] in MINERAL)):
                hit += 1
        out[name] = hit / max(1, len(res))
    return out


def direction(dx, dy):
    a = math.degrees(math.atan2(dy, dx)) % 360
    return ['right', 'bottom-right', 'bottom', 'bottom-left', 'left', 'top-left', 'top', 'top-right'][int(((a + 22.5) % 360) // 45)]


def path_cells(pred, target):
    out = []
    while target >= 0:
        out.append(target); target = pred[target]
    return out[::-1]


def analyze(path):
    d = json.load(open(path, encoding='utf-8'))
    s = d['summary']; units = d['units']
    W, H = s['width'] * 32, s['height'] * 32
    passable, elev, terrain = grid_arrays(d['walk'])
    gh, gw = passable.shape
    G = build_graph(passable)
    clear = ndimage.distance_transform_edt(passable) * CELL  # px to the nearest unpassable cell
    starts = sorted([u for u in units if u['type'] == START and u['player'] <= 8], key=lambda u: u['player'])
    res = [u for u in units if u['type'] in MINERAL or u['type'] == GEYSER]
    groups = [g for g in resource_groups(units) if sum(u['type'] in MINERAL for u in g) >= 4 or any(u['type'] == GEYSER for u in g)]
    bases = []
    for g in groups:
        x, y, is_start = town_hall_spot(g, passable, elev, starts)
        m = [u for u in g if u['type'] in MINERAL]; gz = [u for u in g if u['type'] == GEYSER]
        c = nearest_cell(passable, x, y)
        e = int(elev.flat[c]) if c is not None else -1
        mcx = sum(u['x'] for u in m) / len(m) if m else x; mcy = sum(u['y'] for u in m) / len(m) if m else y
        bases.append(dict(x=x, y=y, start=is_start, minerals=len(m), geysers=len(gz),
                          mineral_amount=sum(u['resources'] for u in m), gas_amount=sum(u['resources'] for u in gz),
                          elev=e, cell=c,
                          mineral_side=direction(mcx - x, mcy - y) if m else None,
                          mineral_dist=[round(math.hypot(u['x'] - x, u['y'] - y)) for u in m],
                          geyser_side=[direction(u['x'] - x, u['y'] - y) for u in gz],
                          geyser_offset=[(u['x'] - x, u['y'] - y) for u in gz],
                          mineral_offsets=[(u['x'] - x, u['y'] - y) for u in m]))
    elev_frac = {k: float((elev == k).sum() / max(1, passable.sum())) for k in (0, 1, 2)}
    out = dict(file=d['file'], name=s['name'], tileset=s['tileset'], w=s['width'], h=s['height'], players=len(starts),
               bases=len(bases), bases_per_player=len(bases) / max(1, len(starts)), symmetry=symmetry(res, W, H),
               passable=float(passable.sum() / passable.size), elev_frac=elev_frac, players_data=[])
    start_cells = [nearest_cell(passable, u['x'], u['y']) for u in starts]
    dists, preds = dijkstra(G, indices=start_cells, return_predecessors=True)
    # A path that keeps to the middle of passages: its narrowest points are the chokes (a shortest path hugs walls).
    GC = build_graph(passable, 1 + 160 / (clear + 8))
    cdists, cpreds = dijkstra(GC, indices=start_cells, return_predecessors=True)
    for i, su in enumerate(starts):
        dist = dists[i]; pred = preds[i]
        bd = [float(dist[b['cell']]) if b['cell'] is not None and np.isfinite(dist[b['cell']]) else None for b in bases]
        order = sorted([k for k in range(len(bases)) if bd[k] is not None], key=lambda k: bd[k])
        main = min(range(len(bases)), key=lambda k: math.hypot(bases[k]['x'] - su['x'], bases[k]['y'] - su['y']))
        exp = [k for k in order if k != main]
        # The main plateau: same elevation as the start location, 4-connected, from the start location.
        me = int(elev.flat[start_cells[i]])
        same = passable & (elev == me)
        lab, _ = ndimage.label(same)
        plateau = lab.flat[start_cells[i]]
        region = lab == plateau
        area_tiles = region.sum() / 16
        # Exits: passable cells of another elevation that touch the plateau (8-way), grouped.
        grown = ndimage.binary_dilation(region, structure=np.ones((3, 3)))
        edge = grown & passable & ~region
        # (an exit leads to a large area: low minitiles inside a doodad on the plateau, such as a crater, are no exit)
        out_lab, _ = ndimage.label(passable & ~region, structure=np.ones((3, 3)))
        big = np.bincount(out_lab.ravel())
        big[0] = 0
        edge &= big[out_lab] >= 1024
        # (only cells that touch the plateau: low ground behind a thin cliff is not an exit; near cells are one exit)
        el, en = ndimage.label(ndimage.binary_dilation(edge, structure=np.ones((3, 3)), iterations=2))
        exits = []
        for k in range(1, en + 1):
            cells = (el == k)
            n = int(cells.sum())
            if n < 6: continue
            ys, xs = np.nonzero(cells & edge)
            if len(xs) == 0: continue
            exits.append(dict(width_px=int(max(xs.max() - xs.min(), ys.max() - ys.min()) * CELL + CELL),
                              x=int(xs.mean() * CELL), y=int(ys.mean() * CELL)))
        # The rush path to each enemy, its narrowest points.
        rush = []
        for j, ou in enumerate(starts):
            if j == i: continue
            t = start_cells[j]
            if not np.isfinite(dist[t]): rush.append(None); continue
            pc = path_cells(cpreds[i], t)
            cl = np.array([clear.flat[c] for c in pc]) * 2  # width
            plen = dist[t]
            # chokes: local minima of width < 320 px (10 tiles), from the start, at least 160 px apart
            chokes = []
            dd = np.cumsum([0] + [math.hypot(pc[q] % gw - pc[q - 1] % gw, pc[q] // gw - pc[q - 1] // gw) * CELL for q in range(1, len(pc))])
            k = 0
            while k < len(pc):
                if cl[k] < 320:
                    j2 = k
                    while j2 < len(pc) and cl[j2] < 320: j2 += 1
                    seg = cl[k:j2]; m = k + int(np.argmin(seg))
                    chokes.append(dict(at=round(float(dd[m])), width=round(float(cl[m])), x=int(pc[m] % gw * CELL), y=int(pc[m] // gw * CELL),
                                       elev_before=int(elev.flat[pc[max(0, k - 2)]]), elev_after=int(elev.flat[pc[min(len(pc) - 1, j2 + 1)]])))
                    k = j2
                k += 1
            rush.append(dict(player=ou['player'], ground=round(float(plen)), air=round(math.hypot(ou['x'] - su['x'], ou['y'] - su['y'])),
                             chokes=chokes))
        # Routes: block the shortest path's middle (radius 5 tiles), again, while the next is at most 1.35x as long.
        routes = 1
        if len(starts) >= 2:
            j = 1 if i == 0 else 0
            t = start_cells[j]
            if np.isfinite(dist[t]):
                base_len = dist[t]
                block = passable.copy()
                pc = path_cells(pred, t)
                for _ in range(4):
                    n = len(pc); mid = pc[n // 4: 3 * n // 4]
                    mask = np.zeros_like(block)
                    for c in mid[::4]: mask[c // gw, c % gw] = True
                    mask = ndimage.binary_dilation(mask, iterations=20)
                    block &= ~mask
                    G2 = build_graph(block)
                    d2, p2 = dijkstra(G2, indices=start_cells[i], return_predecessors=True)
                    if not np.isfinite(d2[t]) or d2[t] > 1.35 * base_len: break
                    routes += 1; pc = path_cells(p2, t)
        nat = bases[exp[0]] if exp else None
        third = bases[exp[1]] if len(exp) > 1 else None
        mb = bases[main]
        out['players_data'].append(dict(
            player=su['player'], x=su['x'], y=su['y'], main=dict(minerals=mb['minerals'], geysers=mb['geysers'], mineral_amount=mb['mineral_amount'],
                                                                  gas_amount=mb['gas_amount'], mineral_side=mb['mineral_side'], mineral_dist=mb['mineral_dist'],
                                                                  geyser_side=mb['geyser_side'], geyser_offset=mb['geyser_offset'], mineral_offsets=mb['mineral_offsets'],
                                                                  elev=me, area_tiles=float(area_tiles), exits=exits),
            natural=None if nat is None else dict(dist=round(bd[exp[0]]), elev=nat['elev'], minerals=nat['minerals'], geysers=nat['geysers'],
                                                  mineral_side=nat['mineral_side'], geyser_side=nat['geyser_side'], x=nat['x'], y=nat['y'],
                                                  mineral_amount=nat['mineral_amount'], gas_amount=nat['gas_amount']),
            third=None if third is None else dict(dist=round(bd[exp[1]]), elev=third['elev'], minerals=third['minerals'], geysers=third['geysers']),
            exp_dists=[round(bd[k]) for k in exp], rush=rush, routes=routes))
    out['base_list'] = [{k: v for k, v in b.items() if k not in ('cell', 'mineral_offsets')} for b in bases]
    return out


if __name__ == '__main__':
    files = sys.argv[1:]
    res = []
    for f in files:
        try:
            r = analyze(f); res.append(r); print(os.path.basename(f), r['file'], r['players'], r['bases'], file=sys.stderr)
        except Exception as e:
            import traceback; traceback.print_exc(); print('FAIL', f, e, file=sys.stderr)
    json.dump(res, open('analysis.json', 'w', encoding='utf-8'), indent=1)
