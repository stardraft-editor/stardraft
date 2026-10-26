# Per-map rows and the ladder's ranges from analysis.json: python ladder_summary.py [analysis.json]
import collections, json, re, statistics as st, sys

A = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'analysis.json', encoding='utf-8'))


def key(f):  # one version of each map: "(4)Fighting_Spirit_1.3.scx" -> "fighting spirit"
    n = re.sub(r'^.*[\\/]', '', f)
    n = re.sub(r'\.(scx|scm)$', '', n, flags=re.I)
    n = re.sub(r'^\(\d\)', '', n)
    n = re.sub(r'_iccup|\[p\]|\(n\)', '', n, flags=re.I)
    n = re.sub(r'[ _]*v?\d+(\.\d+)*[a-z]?$', '', n.replace('_', ' '), flags=re.I)
    return n.strip().lower()


maps = {}
for m in A:
    if m['players'] < 2: continue
    maps[key(m['file'])] = m  # the files come sorted by hash: any version is fine; later ones replace
print(len(A), 'files,', len(maps), 'maps')


def med(v):
    v = [x for x in v if x is not None]
    return st.median(v) if v else None


def pct(v, q):
    v = sorted(x for x in v if x is not None)
    return v[min(len(v) - 1, int(q * len(v)))] if v else None


rows = []
for k, m in sorted(maps.items()):
    P = m['players_data']
    best = max(m['symmetry'], key=m['symmetry'].get)
    mains = [p['main'] for p in P]
    nat = [p['natural'] for p in P if p['natural']]
    rush = [min(r['ground'] for r in p['rush'] if r) for p in P if any(p['rush'])]
    air = [min(r['air'] for r in p['rush'] if r) for p in P if any(p['rush'])]
    ramp, natch, nchokes = [], [], []
    for p in P:
        if not any(p['rush']): continue  # an island start: no ground path
        r = min((r for r in p['rush'] if r), key=lambda r: r['ground'])
        ch = [c for c in r['chokes'] if 0 < c['at'] < r['ground'] - 400]
        rp = next((c for c in ch if c['elev_before'] != c['elev_after']), None)
        if rp: ramp.append(rp['width'])
        nd = p['natural']['dist'] if p['natural'] else 0
        after = [c for c in ch if c['at'] > (rp['at'] if rp else 0) + 200 and c['at'] < nd + 1200]
        if after: natch.append(min(c['width'] for c in after))
        nchokes.append(sum(1 for c in ch if c['width'] < 200))
    main_elev = [p['main']['elev'] for p in P]
    nat_rel = [('higher' if p['natural']['elev'] > p['main']['elev'] else 'same' if p['natural']['elev'] == p['main']['elev'] else 'lower') for p in P if p['natural']]
    row = dict(map=k, players=m['players'], size=f"{m['w']}x{m['h']}", tileset=m['tileset'], sym=best, sym_score=round(m['symmetry'][best], 2),
               bases=m['bases'], bpp=round(m['bases_per_player'], 1), passable=round(m['passable'], 2),
               main_min=med([x['minerals'] for x in mains]), main_gas=med([x['geysers'] for x in mains]),
               field_amount=med([x['mineral_amount'] / max(1, x['minerals']) for x in mains]),
               main_side=collections.Counter(x['mineral_side'] for x in mains).most_common(1)[0][0],
               main_gas_side=collections.Counter(s for x in mains for s in x['geyser_side']).most_common(1)[0][0] if any(x['geyser_side'] for x in mains) else None,
               field_dist_min=med([min(x['mineral_dist']) for x in mains]), field_dist_med=med([med(x['mineral_dist']) for x in mains]),
               geyser_dist=med([max(abs(a), abs(b)) for x in mains for a, b in x['geyser_offset']]),
               main_elev=med(main_elev), main_area=round(med([x['area_tiles'] for x in mains])), main_exits=med([len(x['exits']) for x in mains]),
               ramp=med(ramp), nat_dist=med([n['dist'] for n in nat]), nat_min=med([n['minerals'] for n in nat]), nat_gas=med([n['geysers'] for n in nat]),
               nat_rel=collections.Counter(nat_rel).most_common(1)[0][0] if nat_rel else None, nat_choke=med(natch),
               third=med([p['third']['dist'] for p in P if p['third']]), rush=med(rush), air=med(air), routes=med([p['routes'] for p in P]),
               chokes=med(nchokes),
               nat_spread=(max(n['dist'] for n in nat) - min(n['dist'] for n in nat)) if nat else None,
               third_spread=(max(p['third']['dist'] for p in P if p['third']) - min(p['third']['dist'] for p in P if p['third'])) if all(p['third'] for p in P) else None,
               within3000=med([sum(1 for d in p['exp_dists'] if d <= 3000) for p in P]),
               elev_high=round(m['elev_frac'].get('2', 0) + m['elev_frac'].get('1', 0), 2))
    rows.append(row)

cols = ['map', 'players', 'size', 'sym', 'sym_score', 'bpp', 'main_min', 'main_gas', 'main_side', 'main_gas_side', 'field_dist_min', 'main_elev', 'main_area',
        'main_exits', 'ramp', 'nat_dist', 'nat_min', 'nat_gas', 'nat_rel', 'nat_choke', 'third', 'rush', 'air', 'routes', 'nat_spread', 'third_spread']
print('\t'.join(cols))
for r in rows: print('\t'.join(str(r[c]) for c in cols))

print('\n== ranges (10th / median / 90th percentile), all maps ==')
num = ['sym_score', 'bpp', 'passable', 'main_min', 'main_gas', 'field_amount', 'field_dist_min', 'field_dist_med', 'geyser_dist', 'main_area', 'main_exits', 'ramp',
       'nat_dist', 'nat_min', 'nat_gas', 'nat_choke', 'third', 'rush', 'air', 'routes', 'chokes', 'nat_spread', 'third_spread', 'within3000', 'elev_high']
for c in num:
    v = [r[c] for r in rows]
    print(f'{c:15}', pct(v, .1), med(v), pct(v, .9))
for pl in (2, 3, 4):
    sub = [r for r in rows if r['players'] == pl]
    print(f'\n== {pl} players ({len(sub)}) ==')
    for c in ('rush', 'air', 'nat_dist', 'third', 'bpp', 'main_area', 'routes', 'nat_choke', 'ramp'):
        v = [r[c] for r in sub]
        print(f'{c:15}', pct(v, .1), med(v), pct(v, .9))
for c in ('sym', 'size', 'tileset', 'main_side', 'main_gas_side', 'nat_rel', 'main_elev'):
    print(c, collections.Counter(r[c] for r in rows).most_common())
json.dump(rows, open('summary.json', 'w', encoding='utf-8'), indent=1)
