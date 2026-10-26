# Lagoon: a 1v1 map built to the ladder maps' measures and looks (reference/ladder-measures.md).
#   python terrain.py; python build.py [picture.png]   (makes lagoon.scx in the current folder)
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'scripts'))
from sdcli import cli
M = 'lagoon.scx'
ROUGH = int(os.environ.get('ROUGH', 60))
W, H = 4096, 3584   # 128 x 112: 2-player ladder maps are 96-128 x 112-128, never 128 x 128
def apply(ops, label, turns=1):
    json.dump({"options": {"turns": turns}, "ops": ops}, open('ops.tmp.json', 'w'))   # (a file: the ops can be long)
    r = cli('apply', M, 'ops.tmp.json', '--label', label)
    gone = [x for x in r['results'] if x.get('units_removed') or x.get('doodads_removed')]
    if gone: print(label, 'removed:', gone)
    return r['results']
def img(x, y): return W - x, H - y   # the half turn
cli('close', M, '--discard', ok=False)
cli('new', M, '--size', '128,112', '--tileset', 'jungle', '--terrain', 'Jungle', '--force')
cli('open', M)

T = json.load(open(os.path.join(HERE, 'terrain.json')))
apply(T, "Terrain", turns=2)

# Rough edges (the skill's shapes.roughen): bulges and bites on the cliffs and shores, not near the bases, the ramps
# and the natural's choke.
import shapes
B0 = json.load(open(os.path.join(HERE, 'bases.json')))
KEEP = [[b["x"] - 448, b["y"] - 320, b["x"] + 448, b["y"] + 320] for b in B0]
KEEP += [[x - 256, y - 256, x + 256, y + 256] for x, y in ((832, 896), (1408, 2272), (1408, 2912))] + [[576, 1664, 1088, 2112]]
KEEP += [[W - r, H - b, W - l, H - t] for l, t, r, b in KEEP]
apply(shapes.roughen(cli, M, keep=KEEP, count=ROUGH, seed=3, half=lambda x, y: x < W // 2), "Roughen", turns=2)

# Texture, as on the ladder maps (looks.py): patches of other terrains of the same level, away from the cliffs and the
# shores so that no cliff moves. Jungle (grass), Mud and Rocky Ground on the low ground, High Jungle on the high
# ground; the blobs are in the left half, and the half turn paints the right half.
import random
import numpy as np
from scipy import ndimage
def texture(seed=5):
    a = np.array([list(r) for r in cli('get', M, 'walk')['walk']['rows']])
    lvl = np.full(a.shape, -1)
    for k in '012': lvl[a == k] = int(k)
    rng = random.Random(seed)
    ops, taken = [], []
    for level, terrain, count, sizes in TEXTURE:
        room = ndimage.distance_transform_edt(lvl == level) * 8   # px to other ground
        ys, xs = np.nonzero(room > 0)
        tries = 0
        n = 0
        while n < count and tries < 20000:
            tries += 1
            k = rng.randrange(len(xs))
            x, y = int(xs[k]) * 8 + 4, int(ys[k]) * 8 + 4
            size = rng.choice(sizes)
            if x >= W // 2 or room[y // 8, x // 8] < 64 * size + 128: continue
            if any(abs(x - tx) < 96 * (size + ts) // 2 and abs(y - ty) < 48 * (size + ts) // 2 for tx, ty, ts in taken): continue
            taken.append((x, y, size))
            ops.append({"op": "paint_terrain", "terrain": terrain, "points": [[x, y]], "brush": {"shape": "circle", "size": size}})
            n += 1
    return ops
TEXTURE = [(1, "High Jungle", 40, (2, 3)), (0, "Jungle", 100, (1, 2, 3)), (0, "Mud", 16, (1, 2)), (0, "Rocky Ground", 8, (1, 2))]
apply(texture(), "Texture", turns=2)

# Ramps (one by one: each side needs its own doodad).
RAMPS = [((832, 896), True), ((1408, 2272), False), ((1408, 2912), True)]   # (point, a bottom edge)   # the main's; the plateau's top and bottom
# The mains' ramps: the wide ones (128 px: 293 at a bottom edge, 284 at a top edge), as on the ladder maps.
# Each ramp on a straight edge, the ramp doodad of that edge given (Jungle: 293 at a bottom edge, 284 at a top edge):
# the half turn puts the other side's ramp on the other kind of edge. (A diagonal edge that faces up takes no ramp in
# Jungle: only cliff pieces fit there, and they look broken.)
print(apply([{"op": "place_ramp", "x": x, "y": y, "doodads": [d]} for (x, y), down in RAMPS
             for (x, y), d in (((x, y), 293 if down else 284), (img(x, y), 284 if down else 293))], "Ramps"))

# A cliff that faces up is lower than one that faces down, so the two sides differ where a shore meets a cliff: on
# P2's side a strip of ground that no unit can reach stays there. fix_pockets makes such ground not walkable.
print(apply([{"op": "fix_pockets", "closed_passages": True}], "Fix pockets"))

# Bases: the ladder layout (place_base), the fields away from the map's center; the half turn makes P2's.
B = json.load(open(os.path.join(HERE, 'bases.json')))
apply(B, "Bases", turns=2)
# The ladder maps' doodads are mostly small ground details (2x1 and 2x2 tiles) of the ground's own group.
SMALL = [d['id'] for d in cli('get', M, 'doodad-types')['doodad-types'] if d['width'] <= 2 and d['height'] <= 2
         and d['name'] in ('Dirt', 'High Dirt', 'Jungle', 'High Jungle', 'Rocky Ground', 'Mud')]
LARGER = [d['id'] for d in cli('get', M, 'doodad-types')['doodad-types'] if 2 < max(d['width'], d['height']) <= 4
          and d['name'] in ('Dirt', 'High Dirt', 'Jungle', 'High Jungle', 'Rocky Ground')]
# Decoration: not on the town halls' areas, the ramps or the natural's choke (and their images). It can make small
# pockets at a cliff: then the next seed.
CLEAR = [[b["x"] - 480, b["y"] - 384, b["x"] + 480, b["y"] + 384] for b in B if b["op"] == "place_base"]
CLEAR += [[x - 192, y - 192, x + 192, y + 192] for (x, y), _ in RAMPS] + [[576, 1664, 1088, 2112]]
CLEAR += [[W - r, H - b, W - l, H - t] for l, t, r, b in CLEAR]
for seed in range(1, 80):
    apply([{"op": "decorate", "level": 3, "doodads": SMALL, "seed": seed, "keep_clear": CLEAR},
           {"op": "decorate", "level": "few", "doodads": LARGER, "seed": seed + 100, "keep_clear": CLEAR}], "Decorate")
    # (a small doodad can close a few minitiles in: a pocket that no unit can get to and that blocks nothing, or make a
    # small region; the ladder maps have them too. Gaps and closed passages are not allowed.)
    if not [p for p in cli('get', M, 'pathing')['pathing']['problems'] if p['kind'] not in ('pocket', 'small_region')]:
        print('decorate seed', seed); break
    cli('undo', M)
# Sprites, as delicately as on the ladder maps (clutter.py): 56-90 % of their sprites are on ground that is not
# walkable, at its edge (a cliff, a shore, a doodad), never in the open where they look like obstacles; about 3.5 for
# each 1000 tiles; mostly one by one. Here: the jungle's plants (sprites.dat 16-30), each on a not-walkable cell with
# walkable ground 8-24 px away, 320 px apart at least, now and then a second one beside it.
rng = __import__('random').Random(11)
ground = [r for r in cli('get', M, 'walk')['walk']['rows']]
gh, gw = len(ground), len(ground[0])
def edge_cell(cx, cy):
    if ground[cy][cx] != '#': return False
    near = [ground[cy + dy][cx + dx] for dy in range(-3, 4) for dx in range(-3, 4) if 0 <= cy + dy < gh and 0 <= cx + dx < gw]
    return any(c in '012' for c in near) and 'x' not in near
cells = [(cx * 8 + 4, cy * 8 + 4) for cy in range(2, gh - 2, 2) for cx in range(2, gw // 2, 2) if edge_cell(cx, cy)]
rng.shuffle(cells)
SPRITES = int(os.environ.get('SPRITES', 24))   # for P1's half; the half turn makes as many
spots = []
for x, y in cells:
    if len(spots) >= SPRITES: break
    if any(abs(x - sx) < 320 and abs(y - sy) < 320 for sx, sy in spots): continue
    if any(l - 32 <= x <= r + 32 and t - 32 <= y <= b + 32 for l, t, r, b in CLEAR): continue
    spots.append((x, y))
ops = []
for x, y in spots:
    ops.append({"op": "place_sprite", "sprite": rng.randrange(16, 31), "x": x, "y": y, "player": 12})
    if rng.random() < 0.35: ops.append({"op": "place_sprite", "sprite": rng.randrange(16, 31), "x": x + rng.choice((-40, 40)), "y": y + rng.choice((-24, 24)), "player": 12})
apply(ops, "Sprites", turns=2)
apply([{"op": "set_player", "player": 2, "force": 2}, {"op": "set_force", "force": 1, "name": "West"},
       {"op": "set_force", "force": 2, "name": "East"}], "Forces")
print(json.dumps(cli('check', M)))
cli('render', M, '-o', sys.argv[1] if len(sys.argv) > 1 else 'lagoon.png', '--grid', '512')
