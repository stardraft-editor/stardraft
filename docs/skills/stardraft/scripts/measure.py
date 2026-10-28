# Measure a 1v1 map (2 start locations) against the targets in reference/targets.md: python measure.py <map>
# `OUT` lines are out of range.
import json, math, os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sdcli import cli

M = sys.argv[1]
d = cli('get', M, 'summary', 'units', 'distances', 'walk'); d['file'] = os.path.basename(M)
tmp = os.path.join(tempfile.mkdtemp(), 'm.json'); json.dump(d, open(tmp, 'w', encoding='utf-8'))
m = analyze.analyze(tmp)
P = m['players_data']
if m['players'] != 2: sys.exit(f"a 1v1 map has 2 start locations; this one has {m['players']}")
# 2-player ladder maps 2017-2024 (10th-90th percentile) and the mappers' checklist (rush seconds at about 145 px of
# ground a second, geysers on the map).
R = dict(rush=(4554, 5949), rush_s=(32, 38), air=(3360, 4651), nat_dist=(916, 1134), third=(1630, 2493), bpp=(5, 6),
         geysers=(10, 12), main_area=(680, 1150), ramp=(64, 136), ramp_dist=(593, 1000), nat_choke=(128, 320),
         mid_choke=(128, 320), spread=(0, 150), edge=(240, 320), routes=(1, 2), passable=(0.65, 0.79))


def show(name, v, rng=None):
    # (a value with a range that is missing, such as a choke not found, is OUT too)
    ok = '' if rng is None else 'OUT' if v is None else ('ok' if rng[0] <= v <= rng[1] else 'OUT')
    print(f'{name:28} {v!s:>10}   {"" if rng is None else rng}  {ok}')


best = max(m['symmetry'], key=m['symmetry'].get)
show('size', f"{m['w']}x{m['h']}")
show('symmetry', f"{best} {m['symmetry'][best]:.2f}")
show('bases per player', round(m['bases_per_player'], 1), R['bpp'])
show('passable', round(m['passable'], 2), R['passable'])
show('geysers on the map', sum(b['geysers'] for b in m['base_list']), R['geysers'])
W, H = m['w'] * 32, m['h'] * 32
for p in P:
    print(f"-- player {p['player']} ({p['x']}, {p['y']})")
    mn = p['main']
    show('main fields / geysers', f"{mn['minerals']}/{mn['geysers']}")
    show('main field side / gas', f"{mn['mineral_side']} / {mn['geyser_side']}")
    # fields below the town hall are hard to split to; the geyser mines best above or at the left
    if mn['mineral_side'] and 'bottom' in mn['mineral_side']: print('   OUT: main fields below the town hall')
    if any(g not in ('top', 'left', 'top-left') for g in mn['geyser_side']): print('   OUT: main geyser not above or at the left')
    show('start to map edge', min(p['x'], W - p['x'], p['y'], H - p['y']), R['edge'])
    show('main area (tiles)', round(mn['area_tiles']), R['main_area'])
    show('main exits', len(mn['exits']), (1, 1))
    rr = [x for x in p['rush'] if x]
    if not rr: print('   no ground path to the enemy'); continue
    r0 = rr[0]
    show('rush ground', r0['ground'], R['rush'])
    show('rush seconds (approx.)', round(r0['ground'] / 145), R['rush_s'])
    show('rush air', r0['air'], R['air'])
    ch = [c for c in r0['chokes'] if 0 < c['at'] < r0['ground'] - 400]
    rp = next((c for c in ch if c['elev_before'] != c['elev_after']), None)
    show('ramp width', rp and rp['width'], R['ramp'])
    show('ramp ground distance', rp and rp['at'], R['ramp_dist'])
    nd = p['natural']['dist'] if p['natural'] else None
    show('natural distance', nd, R['nat_dist'])
    if p['natural']: show('natural fields/gas, level', f"{p['natural']['minerals']}/{p['natural']['geysers']} {p['natural']['elev']} (main {mn['elev']})")
    after = [c for c in ch if c['at'] > (rp['at'] if rp else 0) + 200 and c['at'] < (nd or 0) + 1200]
    show('natural choke width', min((c['width'] for c in after), default=None), R['nat_choke'])
    mid = [c for c in ch if c is not rp and nd and nd + 600 < c['at'] < r0['ground'] - nd - 600]
    show('narrowest mid-map choke', min((c['width'] for c in mid), default=None), R['mid_choke'])
    show('third distance', p['third'] and p['third']['dist'], R['third'])
    show('bases within 3000 px', sum(1 for x in p['exp_dists'] if x <= 3000), (2, 5))
    show('routes between mains', p['routes'], R['routes'])
    print('   chokes on the rush path (at, width, x, y):', [(c['at'], c['width'], c['x'], c['y']) for c in ch])

# Fairness from the editor's ground distances: each base of P1's half against its image (the map's symmetry), the difference
# between the players' distances to them. (More reliable than the natural and third above: those come from a guess
# of where each town hall stands.)
dist = d['distances']
S = sorted(dist['start_locations'], key=lambda s: s['player'])
to = [{b['base']: b['distance'] for b in s['to_bases']} for s in S]
T = analyze.TRANSFORMS[best]
worst = (0, None)
for b in dist['bases']:
    ix, iy = T(b['x'], b['y'], W, H)
    o = min(dist['bases'], key=lambda c: math.hypot(c['x'] - ix, c['y'] - iy))
    a, c = to[0].get(b['base']), to[1].get(o['base'])
    if a is None or c is None:   # (no ground path)
        if a != c: print('   OUT: only one player can walk to the base at', (b['x'], b['y']))
        continue
    if a <= (to[1].get(b['base']) or 1e9):   # (P1's half)
        diff = abs(a - c)
        if diff > worst[0]: worst = (diff, (b['x'], b['y']))
show('base pair spread', worst[0], R['spread'])
if worst[1]: print('   (worst pair: the base at', worst[1], ')')
