# Measure one map (opened with stardraft-cli open) against the ladder ranges: python measure.py <map>
import json, os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sdcli import cli

M = sys.argv[1]
d = cli('get', M, 'summary', 'units', 'distances', 'walk'); d['file'] = os.path.basename(M)
tmp = os.path.join(tempfile.mkdtemp(), 'm.json'); json.dump(d, open(tmp, 'w', encoding='utf-8'))
m = analyze.analyze(tmp)
P = m['players_data']
pl = m['players']
# Ladder ranges (10th-90th percentile of 2017-2024 ladder maps; 2-player values where they differ).
R2 = dict(rush=(4554, 5949), air=(3360, 4651), nat_dist=(916, 1134), third=(1630, 2493), bpp=(5, 6))
R4 = dict(rush=(4104, 4735), air=(3160, 3520), nat_dist=(879, 1193), third=(1515, 2553), bpp=(3, 4.8))
R = R2 if pl == 2 else R4
R.update(main_area=(680, 1150), ramp=(64, 136), ramp_dist=(593, 1000), nat_choke=(128, 320), spread=(0, 150), edge=(240, 320),
         routes=(1, 3), passable=(0.65, 0.79))


def show(name, v, rng=None):
    ok = '' if rng is None or v is None else ('ok' if rng[0] <= v <= rng[1] else 'OUT')
    print(f'{name:28} {v!s:>10}   {"" if rng is None else rng}  {ok}')


best = max(m['symmetry'], key=m['symmetry'].get)
show('size', f"{m['w']}x{m['h']}")
show('symmetry', f"{best} {m['symmetry'][best]:.2f}")
show('bases per player', round(m['bases_per_player'], 1), R['bpp'])
show('passable', round(m['passable'], 2), R['passable'])
W, H = m['w'] * 32, m['h'] * 32
for p in P:
    print(f"-- player {p['player']} ({p['x']}, {p['y']})")
    mn = p['main']
    show('main fields / geysers', f"{mn['minerals']}/{mn['geysers']}")
    show('main field side / gas', f"{mn['mineral_side']} / {mn['geyser_side']}")
    show('start to map edge', min(p['x'], W - p['x'], p['y'], H - p['y']), R['edge'])
    show('main area (tiles)', round(mn['area_tiles']), R['main_area'])
    show('main exits', len(mn['exits']), (1, 1))
    rr = [x for x in p['rush'] if x]
    if not rr: print('   no ground path to an enemy'); continue
    r0 = min(rr, key=lambda x: x['ground'])
    show('rush ground', r0['ground'], R['rush'])
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
    # the narrowest choke in the middle (past the natural, before the enemy's): ladder 86-283 px, med 229
    mid = [c for c in ch if c is not rp and nd and nd + 600 < c['at'] < r0['ground'] - nd - 600]
    show('narrowest mid-map choke', min((c['width'] for c in mid), default=None), (128, 320))
    show('third distance', p['third'] and p['third']['dist'], R['third'])
    show('bases within 3000 px', sum(1 for x in p['exp_dists'] if x <= 3000), (2, 5))
    show('routes between mains', p['routes'], R['routes'])
    print('   chokes on the rush path (at, width):', [(c['at'], c['width']) for c in ch])
    print('   expansion distances:', p['exp_dists'])
nat = [p['natural']['dist'] for p in P if p['natural']]
thr = [p['third']['dist'] for p in P if p['third']]
if nat: show('natural spread', max(nat) - min(nat), R['spread'])
if len(thr) == len(P): show('third spread', max(thr) - min(thr), R['spread'])
