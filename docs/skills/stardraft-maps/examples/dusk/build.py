# Dusk: a 1v1 Twilight map, 112 x 128 (taller than wide), made with the stardraft-maps skill.
#   python build.py [picture.png]   (makes dusk.scx in the current folder)
import json, os, random, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'scripts'))
import numpy as np
from scipy import ndimage
from sdcli import cli
from shapes import blob, worm, rect, fill, roughen, open_doodads, narrow_rects, back

M = 'dusk.scx'
W, H = 3584, 4096
def img(x, y): return W - x, H - y   # the half turn
def apply(ops, label, turns=1):
    json.dump({"options": {"turns": turns}, "ops": ops}, open('ops.tmp.json', 'w'))
    r = cli('apply', M, 'ops.tmp.json', '--label', label)
    gone = [x for x in r['results'] if x.get('units_removed') or x.get('doodads_removed')]
    if gone: print(label, 'removed:', gone)
    return r['results']
def walk_rows(): return cli('get', M, 'walk')['walk']['rows']

cli('close', M, '--discard', ok=False)
cli('new', M, '--size', '112,128', '--tileset', 'twilight', '--terrain', 'Dirt', '--force')
cli('open', M)

# 1. The mains. In Twilight only lower diagonal edges take a ramp, so a half turn cannot give both mains a ramp
#    (as on the Twilight ladder maps, e.g. Neo Dark Origin): P1's main ramp is on its south-east edge; P2's main is
#    the half-turn image of P1's with a notch of low ground cut into it beside the ramp's place, so that its edge there
#    faces south-west. The two ramps are half-turn images of each other in place, not in the edge they sit on.
P1_MAIN = [[0, 0, 1024, 512, 'rect'], [-256, 128, 1280, 896, 'diamond']]
NOTCH = [2304, 3392, 2944, 3712, 'diamond']     # P2's: its upper-right edge (2624, 3392)-(2944, 3552) is the ramp's
apply([{"op": "set_scenario", "name": "Dusk", "description": "1v1. Made with stardraft-cli to the measures and the looks of the ladder maps."}]
      + [{"op": "paint_area", "terrain": "High Dirt", "area": a[:4], "shape": a[4]} for a in P1_MAIN], "Mains", turns=2)
apply([{"op": "paint_area", "terrain": "Dirt", "area": NOTCH[:4], "shape": NOTCH[4]},
       # (the area the notch takes, given back on the main's north side)
       {"op": "paint_area", "terrain": "High Dirt", "area": [2944, 2944, 3584, 3264]}], "P2's notch")

# 2. The rest with the half turn: a lake in the middle, lakes that make the naturals' chokes, a mesa.
T = []
T += fill(blob(1792, 2048, 230, 330, amp=0.22, seed=5), "Water")
T += fill(worm([(-100, 1450), (300, 1400), (620, 1480)], 220, amp=0.3, seed=7), "Water")
T += fill(blob(2600, 520, 280, 170, amp=0.25, seed=8), "Water")
T += fill(blob(560, 3500, 240, 150, amp=0.25, seed=9), "Water")
T += fill(worm([(-120, 2420), (200, 2390), (580, 2400)], 220, amp=0.3, seed=12), "Water")   # the far left lane closed
for i, (x, y, rx, ry) in enumerate([(620, 2380, 210, 130)]):
    T += fill(blob(x, y, rx + 120, ry + 90, amp=0.2, seed=20 + i), "Dirt")
    T += fill(blob(x, y, rx, ry, amp=0.25, seed=30 + i), "High Dirt")
# Terrain at the back of each mineral line that is not at the map's edge: a cliff (high ground) or water behind it.
for i, b in enumerate(json.load(open(os.path.join(HERE, 'bases.json')))):
    if b.get('player'): continue
    edge = b['x'] - 256 - 160 < 0 if b['side'] == 'left' else b['x'] + 256 + 160 > W
    if not edge: T += fill(back(b, 'High Dirt' if i % 2 else 'Water', seed=40 + i), 'High Dirt' if i % 2 else 'Water')
apply(T, "Terrain", turns=2)

RAMPS = [((896, 704), 1), ((2784, 3472), 0)]   # (point, doodad): P1 south-east edge, P2 the notch's (south-west)
B = json.load(open(os.path.join(HERE, 'bases.json')))      # the main and the shared bases: the half turn makes the other player's
KEEP = [[b["x"] - 448, b["y"] - 320, b["x"] + 448, b["y"] + 320] for b in B]
KEEP += [[W - r, H - b, W - l, H - t] for l, t, r, b in KEEP]

KEEP += [[x - 288, y - 288, x + 288, y + 288] for (x, y), _ in RAMPS]
def free(x, y): return not any(l <= x <= r and t <= y <= b for l, t, r, b in KEEP)

# 3. Rough edges (both a point and its image must be clear: the mains are not half-turn images).
ops = [o for o in roughen(cli, M, keep=KEEP, count=60, seed=3, half=lambda x, y: x < W // 2)
       if free(*img(*o["points"][0]))]
apply(ops, "Roughen", turns=2)

# 4. Texture as on the Twilight ladder maps: High Crushed Rock on the high ground, Crushed Rock, Mud and Flagstones on
#    the low ground; away from cliffs and shores, at a point and at its image.
def texture(seed=5):
    a = np.array([list(r) for r in walk_rows()])
    lvl = np.full(a.shape, -1)
    for k in '012': lvl[a == k] = int(k)
    rng = random.Random(seed)
    out, taken = [], []
    for level, terrain, count, sizes in [(1, "High Crushed Rock", 40, (2, 3)), (0, "Crushed Rock", 60, (1, 2, 3)),
                                         (0, "Mud", 24, (1, 2)), (0, "Flagstones", 8, (1,))]:
        room = ndimage.distance_transform_edt(lvl == level) * 8
        ys, xs = np.nonzero(room > 0)
        n = tries = 0
        while n < count and tries < 20000:
            tries += 1
            k = rng.randrange(len(xs))
            x, y = int(xs[k]) * 8 + 4, int(ys[k]) * 8 + 4
            size = rng.choice(sizes)
            ix, iy = img(x, y)
            need = 64 * size + 128
            if x >= W // 2 or room[y // 8, x // 8] < need or room[iy // 8, ix // 8] < need: continue
            if any(abs(x - tx) < 96 * (size + ts) // 2 and abs(y - ty) < 48 * (size + ts) // 2 for tx, ty, ts in taken): continue
            taken.append((x, y, size))
            out.append({"op": "paint_terrain", "terrain": terrain, "points": [[x, y]], "brush": {"shape": "circle", "size": size}})
            n += 1
    return out
apply(texture(), "Texture", turns=2)

# 5. Ramps, one by one with the doodad of the edge.
print(apply([{"op": "place_ramp", "x": x, "y": y, "doodads": [d]} for (x, y), d in RAMPS], "Ramps"))

# 6. Bases (the half turn makes P2's).
apply(B, "Bases", turns=2)
# (fix_pockets after the bases: with closed_passages it checks the ways from the start locations, so they must be there)
json.dump({"ops": [{"op": "fix_pockets", "closed_passages": True}]}, open('ops.tmp.json', 'w'))
r = cli('apply', M, 'ops.tmp.json', '--label', 'Fix pockets', ok=False)
if not r.get('ok'):   # a closed passage on a way: leave the closed passages, fix the pockets only
    print('fix_pockets with closed passages refused:', r.get('error', '')[:80])
    r = {"results": apply([{"op": "fix_pockets"}], "Fix pockets")}
print(r['results'])

# 7. Doodads: small ground details densely, a few bigger ones; not on the bases, ramps, chokes.
types = cli('get', M, 'doodad-types')['doodad-types']
GROUNDS = ('Dirt', 'High Dirt', 'Crushed Rock', 'High Crushed Rock', 'Flagstones', 'High Flagstones')
SMALL = [d['id'] for d in types if d['width'] <= 2 and d['height'] <= 2 and d['name'] in GROUNDS]
LARGER = [d['id'] for d in types if 2 < max(d['width'], d['height']) <= 4 and d['name'] in GROUNDS]
CLEAR = KEEP + narrow_rects(cli, M)   # (not on the bases, the ramps, or in a narrow passage)
for seed in range(1, 80):
    apply([{"op": "decorate", "level": 3, "doodads": SMALL, "seed": seed, "keep_clear": CLEAR},
           {"op": "decorate", "level": "few", "doodads": LARGER, "seed": seed + 100, "keep_clear": CLEAR}], "Decorate")
    if not [p for p in cli('get', M, 'pathing')['pathing']['problems'] if p['kind'] not in ('pocket', 'small_region')]:
        print('decorate seed', seed); break
    cli('undo', M)
# (the doodads in the open go: they would be obstacles in the way; the ladder maps keep theirs near edges)
gone = open_doodads(cli, M, corridor=480, wall_px=24)
if gone: apply([{"op": "delete_doodads", "doodads": gone}], "Open doodads")
print('doodads in the open removed:', len(gone))

# 8. Sprites, delicately: on not-walkable ground at its edge (a cliff, a shore), one by one or two, at a point whose
#    image is such a place too.
rng = random.Random(11)
ground = walk_rows()
gh, gw = len(ground), len(ground[0])
def edge_cell(cx, cy):
    if not (3 <= cx < gw - 3 and 3 <= cy < gh - 3) or ground[cy][cx] != '#': return False
    near = [ground[cy + dy][cx + dx] for dy in range(-3, 4) for dx in range(-3, 4)]
    return any(c in '012' for c in near) and 'x' not in near
cells = [(cx * 8 + 4, cy * 8 + 4) for cy in range(3, gh - 3, 2) for cx in range(3, gw // 2, 2) if edge_cell(cx, cy)]
cells = [(x, y) for x, y in cells if edge_cell(img(x, y)[0] // 8, img(x, y)[1] // 8)]
rng.shuffle(cells)
SPRITE_IDS = [390, 390, 386, 395, 396, 397, 398, 399, 400, 401, 98, 99]
spots = []
for x, y in cells:
    if len(spots) >= 22: break
    if any(abs(x - sx) < 320 and abs(y - sy) < 320 for sx, sy in spots) or not free(x, y): continue
    spots.append((x, y))
ops = []
for x, y in spots:
    ops.append({"op": "place_sprite", "sprite": rng.choice(SPRITE_IDS), "x": x, "y": y, "player": 12})
    if rng.random() < 0.35:
        ops.append({"op": "place_sprite", "sprite": rng.choice(SPRITE_IDS), "x": x + rng.choice((-40, 40)), "y": y + rng.choice((-24, 24)), "player": 12})
for o in ops:   # (one by one: a sprite whose image does not fit is left out)
    json.dump({"options": {"turns": 2}, "ops": [o]}, open('ops.tmp.json', 'w'))
    cli('apply', M, 'ops.tmp.json', '--label', 'Sprite', ok=False)

apply([{"op": "set_player", "player": 2, "force": 2}, {"op": "set_force", "force": 1, "name": "North"},
       {"op": "set_force", "force": 2, "name": "South"}], "Forces")
print(json.dumps(cli('check', M)))
cli('render', M, '-o', sys.argv[1] if len(sys.argv) > 1 else 'dusk.png', '--grid', '512')
