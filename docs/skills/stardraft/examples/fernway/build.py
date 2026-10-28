# Fernway: a 1v1 Jungle map, 128x112, half turn. Organic shapes only (blobs, worms); the ramps go on the shapes'
# outermost points. A river runs corner to corner with three fords; the main's ramp leads east to the natural.
#   python build.py [picture.png]   (makes fernway.scx in the current folder)
import json, os, random, sys
import numpy as np
from scipy import ndimage
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get('SKILL_SCRIPTS', os.path.join(HERE, '..', '..', 'scripts')))   # (a copy: SKILL_SCRIPTS)
from sdcli import cli
from shapes import blob, worm, fill, roughen, open_doodads, narrow_rects, extreme, back, ramp_pair
M = 'fernway.scx'
W, H = 4096, 3584
def img(x, y): return W - x, H - y
def apply(ops, label, turns=1):
    json.dump({"options": {"turns": turns}, "ops": ops}, open('ops.tmp.json', 'w'))
    return cli('apply', M, 'ops.tmp.json', '--label', label)['results']

cli('close', M, '--discard', ok=False)
cli('new', M, '--size', '128,112', '--tileset', 'jungle', '--terrain', 'Jungle', '--force')
cli('open', M)

# 1. Terrain: P1's side (top-left); the half turn paints P2's.
MAIN = blob(500, 470, 580, 540, amp=0.1, seed=11, waves=(2, 3))              # in the corner
PLATEAU = blob(520, 1520, 470, 290, amp=0.1, seed=12, waves=(2, 3))          # the third's high ground, one ramp to the east
T = [{"op": "set_scenario", "name": "Fernway", "description": "1v1. Made with stardraft-cli."}]
T += fill(blob(1300, 420, 420, 380, amp=0.12, seed=2), "Dirt")    # the natural
T += fill(blob(1400, 1500, 600, 380, amp=0.15, seed=3), "Dirt")   # the ground below the natural's choke
T += fill(MAIN, "High Dirt") + fill(PLATEAU, "High Dirt")
T += fill(worm([(-120, 1080), (500, 1060), (980, 1000)], 180, amp=0.25, seed=4), "Water")       # between main and plateau
T += fill(blob(1010, 1010, 300, 150, amp=0.18, seed=5), "Water")                               # the natural's south side
T += fill(worm([(1880, -80), (1800, 260), (1940, 620), (1860, 1000)], 100, amp=0.3, seed=6), "Water")   # a stream: the natural's east side
T += fill(worm([(4300, -100), (3900, 200), (3600, 300), (3400, 560), (3150, 820), (2850, 900), (2650, 1150), (2400, 1300), (2250, 1560), (2048, 1792)], 110, amp=0.35, step=40, seed=7), "Water")   # the river, winding
T += fill(blob(3100, 870, 230, 170, amp=0.15, seed=8), "Dirt") + fill(blob(2048, 1792, 240, 170, amp=0.12, seed=9), "Dirt")  # fords
T += fill(back({"x": 2432, "y": 640, "side": "right"}, depth=300, seed=10), "High Dirt")         # a cliff behind the top base's fields
T += fill(blob(1560, 1980, 200, 140, amp=0.2, seed=13), "High Dirt")                          # a mesa by the river (an overlord spot)
apply(T, "Terrain", turns=2)

RAMPS = [(extreme(MAIN, 'right'), 281), (extreme(PLATEAU, 'right'), 281)]   # (point, doodad); P2's: the images, 302
B = json.load(open(os.path.join(HERE, 'bases.json')))
KEEP = [[b["x"] - 448, b["y"] - 320, b["x"] + 448, b["y"] + 320] for b in B]
KEEP += [[x - 256, y - 256, x + 256, y + 256] for (x, y), _ in RAMPS]
KEEP += [[1280, 768, 1792, 1152], [1792, 1600, 2304, 1984], [2880, 704, 3328, 1024]]   # the natural's choke, the fords
KEEP += [[W - r, H - b, W - l, H - t] for l, t, r, b in KEEP]
apply(roughen(cli, M, keep=KEEP, count=100, seed=3, half=lambda x, y: x < W // 2), "Roughen", turns=2)

# 2. Texture: patches of the other terrains of the same level, away from cliffs and shores.
def texture(seed=5):
    a = np.array([list(r) for r in cli('get', M, 'walk')['walk']['rows']])
    lvl = np.full(a.shape, -1)
    for k in '012': lvl[a == k] = int(k)
    rng, ops, taken = random.Random(seed), [], []
    for level, terrain, count, sizes in [(1, "High Jungle", 40, (2, 3)), (0, "Jungle", 100, (1, 2, 3)), (0, "Mud", 16, (1, 2)), (0, "Rocky Ground", 8, (1, 2))]:
        room = ndimage.distance_transform_edt(lvl == level) * 8
        ys, xs = np.nonzero(room > 0)
        n = tries = 0
        while n < count and tries < 20000:
            tries += 1
            k = rng.randrange(len(xs)); x, y = int(xs[k]) * 8 + 4, int(ys[k]) * 8 + 4; size = rng.choice(sizes)
            if x >= W // 2 or room[y // 8, x // 8] < 64 * size + 128: continue
            if any(abs(x - tx) < 96 * (size + ts) // 2 and abs(y - ty) < 48 * (size + ts) // 2 for tx, ty, ts in taken): continue
            taken.append((x, y, size)); n += 1
            ops.append({"op": "paint_terrain", "terrain": terrain, "points": [[x, y]], "brush": {"shape": "circle", "size": size}})
    return ops
apply(texture(), "Texture", turns=2)

# 3. Ramps, one by one with the doodad of the edge (the image of a right edge is a left edge).
flip = {293: 284, 284: 293, 281: 302, 302: 281}
RAMP_OPS = [o for p, d in RAMPS for o in ramp_pair(cli, M, p, d, img, flip[d], along='x' if d in (293, 284) else 'y')]
print(apply(RAMP_OPS, "Ramps"))

# 4. Bases, then the pockets that the turn leaves.
apply(B, "Bases", turns=2)
json.dump({"ops": [{"op": "fix_pockets", "closed_passages": True}]}, open('ops.tmp.json', 'w'))
r = cli('apply', M, 'ops.tmp.json', '--label', 'Fix pockets', ok=False)
if not r.get('ok'): apply([{"op": "fix_pockets"}], "Fix pockets")

# 5. Doodads: small ground details, a few bigger ones; not on bases, ramps, chokes or narrow passages.
types = cli('get', M, 'doodad-types')['doodad-types']
GROUNDS = ('Dirt', 'High Dirt', 'Jungle', 'High Jungle', 'Rocky Ground', 'Mud')
SMALL = [d['id'] for d in types if d['width'] <= 2 and d['height'] <= 2 and d['name'] in GROUNDS]
LARGER = [d['id'] for d in types if 2 < max(d['width'], d['height']) <= 4 and d['name'] in GROUNDS]
CLEAR = KEEP + narrow_rects(cli, M, corridor=320)
for seed in range(1, 80):
    apply([{"op": "decorate", "level": 2, "doodads": SMALL, "seed": seed, "keep_clear": CLEAR},
           {"op": "decorate", "level": "few", "doodads": LARGER, "seed": seed + 100, "keep_clear": CLEAR}], "Decorate")
    if not [p for p in cli('get', M, 'pathing')['pathing']['problems'] if p['kind'] not in ('pocket', 'small_region')]: break
    cli('undo', M)
gone = open_doodads(cli, M, corridor=480, wall_px=24)
if gone: apply([{"op": "delete_doodads", "doodads": gone}], "Open doodads")

# 6. Sprites: the jungle's plants (16-30) on unwalkable ground at a cliff or shore, one or two together.
rng = random.Random(11)
ground = cli('get', M, 'walk')['walk']['rows']
gh, gw = len(ground), len(ground[0])
def edge_cell(cx, cy):
    if not (3 <= cx < gw - 3 and 3 <= cy < gh - 3) or ground[cy][cx] != '#': return False
    near = [ground[cy + dy][cx + dx] for dy in range(-3, 4) for dx in range(-3, 4)]
    return any(c in '012' for c in near) and 'x' not in near
cells = [(cx * 8 + 4, cy * 8 + 4) for cy in range(3, gh - 3, 2) for cx in range(3, gw // 2, 2) if edge_cell(cx, cy)]
rng.shuffle(cells)
spots = []
for x, y in cells:
    if len(spots) >= 24: break
    if any(abs(x - sx) < 320 and abs(y - sy) < 320 for sx, sy in spots): continue
    if any(l - 32 <= x <= r + 32 and t - 32 <= y <= b + 32 for l, t, r, b in CLEAR): continue
    spots.append((x, y))
ops = []
for x, y in spots:
    ops.append({"op": "place_sprite", "sprite": rng.randrange(16, 31), "x": x, "y": y, "player": 12})
    if rng.random() < 0.35: ops.append({"op": "place_sprite", "sprite": rng.randrange(16, 31), "x": x + rng.choice((-40, 40)), "y": y + rng.choice((-24, 24)), "player": 12})
for o in ops:   # (one by one: a sprite whose image does not fit is left out)
    json.dump({"options": {"turns": 2}, "ops": [o]}, open('ops.tmp.json', 'w'))
    cli('apply', M, 'ops.tmp.json', '--label', 'Sprite', ok=False)

apply([{"op": "set_player", "player": 2, "force": 2}, {"op": "set_force", "force": 1, "name": "West"},
       {"op": "set_force", "force": 2, "name": "East"}], "Forces")
print(json.dumps(cli('check', M)))
cli('render', M, '-o', sys.argv[1] if len(sys.argv) > 1 else 'fernway.png', '--grid', '512')
