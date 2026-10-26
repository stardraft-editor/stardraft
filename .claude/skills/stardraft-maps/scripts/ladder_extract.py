# Copy each map of a folder (once, by its contents) to <out>/maps, then save its summary, units, distances and
# walk grid as <out>/data/<hash>.json, and a picture as <out>/png/<hash>.png.
#   python ladder_extract.py "<StarCraft folder>/Maps/ladder" <out folder>
import hashlib, json, os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sdcli import cli as run
SRC, HERE = sys.argv[1], sys.argv[2]
for sub in ('maps', 'data', 'png'): os.makedirs(os.path.join(HERE, sub), exist_ok=True)
seen = {}
for root, _, files in os.walk(SRC):
    for f in sorted(files):
        if not f.lower().endswith(('.scm', '.scx')): continue
        p = os.path.join(root, f)
        h = hashlib.sha1(open(p, 'rb').read()).hexdigest()[:10]
        if h in seen: continue
        seen[h] = p
print(len(seen), 'unique maps')
def cli(*a):
    return run(*a, ok=False)
for h, p in seen.items():
    out = os.path.join(HERE, 'data', h + '.json')
    if os.path.exists(out): continue
    m = os.path.join(HERE, 'maps', h + os.path.splitext(p)[1])
    shutil.copyfile(p, m)
    o = cli('open', m)
    if not o.get('ok'): print('FAIL', p, o.get('error')); continue
    g = cli('get', m, 'summary', 'units', 'distances', 'walk')
    cli('render', m, '-o', os.path.join(HERE, 'png', h + '.png'), '--grid', '512', '--zoom', '0.25')
    cli('close', m, '--discard')
    g['file'] = os.path.relpath(p, SRC)
    json.dump(g, open(out, 'w', encoding='utf-8'))
    print(h, g['file'], g.get('ok'))
