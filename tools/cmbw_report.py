#!/usr/bin/env python3
"""Makes the terrain report for the Cosmonarchy BW team: a plain HTML page (index.html and its pictures) that lists each
mod terrain StarDraft 2 makes from the mod's brush sets, by tileset, and which pieces of it the set has.

Run it after each update of the mod (with the new Prerelease folder), and put the folder on the website
(stardraft-editor.github.io, folder cosmonarchy/).

How it decides:
- Each mod terrain is made as a game border (its template: a game terrain against another, as Asphalt on Dirt is made
  as the game's Rocky Ground on Dirt). The ISOM gives that border's rects (parts): two half-diamonds each, their corners
  of the stand-in or of the ground. For each part the game has one tile group or more; a part is covered when the set
  has a piece for one of them.
- Required: the parts most sets made as that border cover (straight edges, outer and inner corners), over the tilesets
  with the game's same tile layout (Jungle, Desert, Ice and Twilight share one). With all of them a terrain paints
  without gaps. Extra: the border's other parts (thin, single-diamond and concave shapes); without them the editor
  moves a corner instead.
- Each part is named by its two half-diamonds' corners and shown as the game's Badlands Asphalt on Dirt with the same
  corners (clear edges: a wireframe of the part); the set's piece where it has one.

usage: python tools/cmbw_report.py --install <Cosmonarchy Prerelease> --bin <build/Release> --out <folder>
       [--maps <CMBW-maps folder>] [--raw <folder>] [--single <file.html>]
Needs Pillow (the page's small pictures).
"""
import argparse
import base64
import datetime
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

# A map of each tileset (the probe paints on a copy of it).
MAPS = ['vt02.scx', '1. Further Notice.scx', '(2)Otherworld.scx', '01. Clenched Fist.scx', '(1)Three Hong Kong!.scx',
        '(2)Wellington.scx', '(2)The Benefactor.scx']


def find_map(root, name):
    for folder, _, files in os.walk(root):
        if name in files:
            return os.path.join(folder, name)
    return None


def run_probe(exe, install, maps, raw):
    os.makedirs(raw, exist_ok=True)
    for name in MAPS:
        path = find_map(maps, name)
        if not path:
            print(f'no map {name} under {maps}', file=sys.stderr)
            continue
        print(f'painting the terrains on {name}')
        subprocess.run([exe, install, path, raw], check=True, stdout=subprocess.DEVNULL)


def set_status(bin_dir, install, maps):
    """The brush sets that are no terrain yet ("-") and the ones left out ("out"), by tileset (cmbw_set_status)."""
    exe = os.path.join(bin_dir, 'cmbw_set_status.exe' if os.name == 'nt' else 'cmbw_set_status')
    if not os.path.exists(exe):
        return None
    text = subprocess.run([exe, install, '-1', maps], capture_output=True, text=True, errors='replace').stdout
    tileset, todo, out = '', [], []
    for line in text.splitlines():
        if line.startswith('== '):
            tileset = line[3:].split(' (')[0]
        elif line.startswith('  -    ') or line.startswith('  out  '):
            name = line[7:].split()[0].replace('Cosmonarchy_', '')
            (todo if line.startswith('  -') else out).append((tileset, name))
    return todo, out


# A half-diamond by its stand-in corners (bits: 1 west, 2 north, 4 east, 8 south).
HALF = {0: 'ground', 15: 'fill', 1: 'W tip', 2: 'N tip', 4: 'E tip', 8: 'S tip', 3: 'NW edge', 6: 'NE edge', 12: 'SE edge',
        9: 'SW edge', 5: 'W-E saddle', 10: 'N-S saddle', 14: 'W notch', 13: 'N notch', 11: 'E notch', 7: 'S notch'}


def part_name(p):
    return f'{HALF[p[1]]} | {HALF[p[2]]}'


SMALL_H = 530  # (an overview's thumbnail height at 640 wide: the painting is 70 x 58 tiles)
NEAR_SRC = {}  # (a near_ picture: the painting it comes from, own / orig / tpl)
WHITE, YELLOW = (240, 240, 240), (255, 150, 20)  # (a part's box: the set has its piece; it is missing)


def similar(t, game, era):
    """The game terrain most like a mod terrain, to show the parts it lacks: of the same kind, the most words of its name
    shared (its own name, the terrain in brackets left out; "high" counts little), on its own tileset first (game: of
    every tileset, each with its "era"); None: no word shared."""
    words = lambda n: set(re.findall(r'[a-z]+', n.split(' (')[0].lower()))
    mine, best, score = words(t['name']), None, 0
    for g in game:
        if g['kind'] != t.get('kind', 'edge'):
            continue
        shared = mine & words(g['name'])
        sc = sum(0.25 if w == 'high' else 2 for w in shared) + (3 if words(g['name']) == mine else 0)
        sc = 0 if sc < 2 else sc + (1 if g['era'] == era else 0)  # (a word besides "high" shared)
        if sc > score:
            best, score = g, sc
    return best


def analyse(reports):
    """Each terrain's parts as covered or not, the required ones by template."""
    by_template = {}
    game = [dict(g, era=r['era']) for r in reports for g in r.get('game', [])]
    for r in reports:
        for t in r['terrains']:
            t['_like'] = similar(t, game, r['era'])
            have = {p['game'] for p in t['pieces']}
            t['_rects'] = [tuple(x['o']) for x in t['rects']]
            t['_pattern'] = {tuple(x['o']): (x['p'], x['l'], x['r']) for x in t['rects']}
            t['_covered'] = {o for o in t['_rects'] if any(g in have for g in o)}
            t['_piece'] = {o: next(g for g in o if g in have) for o in t['_covered']}
            layout = 'shared' if r['era'] in (4, 5, 6, 7) else r['era']  # (the game's same groups for the same borders)
            # (its kind's name: a flat border of 14 parts has no inner corners in the game: rocky ground, shale)
            kind = t.get('kind', 'edge')
            t['_kind'] = 'edge, no inner' if kind == 'edge' and len(t['_rects']) <= 14 else 'cliff' if kind.startswith('cliff') else kind
            by_template.setdefault((layout, tuple(t['templateIds'])), []).append(t)
    for terrains in by_template.values():
        counts = {}
        for t in terrains:
            for o in t['_covered']:
                counts[o] = counts.get(o, 0) + 1
        required = {o for o, c in counts.items() if c * 2 > len(terrains)}
        for t in terrains:
            order = sorted(t['_rects'], key=lambda o: (t['_pattern'][o][1:], t['_pattern'][o][0], o))
            t['_required'] = list(order)  # (every part: a set needs them all to paint every shape the game's terrain can)
            t['_missing'] = [o for o in t['_required'] if o not in t['_covered']]
            t['_extra'] = []
            variants = {p['game']: p['variants'] for p in t['pieces']}
            counts_v = [max((variants.get(g, 0) for g in o), default=0) for o in t['_required'] if o in t['_covered']]
            t['_variations'] = min(counts_v) if counts_v else 0


STYLE = """
:root{--bg:#121417;--panel:#181b1f;--rule:#2a2f36;--fg:#d3d8de;--dim:#7d8590;--ok:#5cbf7d;--no:#ff9614;
--terr:#8e97a3;--high:#c6ccd4;--ground:#3a4048;--line:#f2f4f7;--wall:#5e6670;--frame:#f2b544;--edge:#5b626c;--mono:ui-monospace,"Cascadia Mono",Consolas,monospace;--sans:system-ui,-apple-system,"Segoe UI",sans-serif;color-scheme:dark}
body{background:var(--bg);color:var(--fg);font:13px/1.45 var(--sans);margin:0;
padding-block:16px;padding-left:max(16px,env(safe-area-inset-left,0px));padding-right:max(16px,env(safe-area-inset-right,0px))}
.layout{display:grid;grid-template-columns:230px minmax(0,1fr);gap:28px;max-width:1820px;margin:0 auto}
main{min-width:0}
.side{position:sticky;top:0;align-self:start;max-height:100vh;overflow:auto;padding:4px 8px 24px 0;box-sizing:border-box;font-size:12px}
.side h4{margin:14px 0 4px;font-size:12px}.side h4 a{color:var(--fg)}.side ul{list-style:none;margin:0;padding:0}
.side li a{display:flex;justify-content:space-between;gap:8px;padding:2px 6px;border-radius:4px;color:var(--dim);text-decoration:none}
.side li a:hover{background:var(--panel);color:var(--fg)}.side li a b{font:400 10px var(--mono)}
.t,h2{scroll-margin-top:12px}.t:target{background:var(--panel);outline:1px solid var(--rule)}
@media (max-width:900px){.layout{grid-template-columns:minmax(0,1fr)}.side{position:static;max-height:none}}h1{font-size:18px;margin:0 0 4px}h2{font-size:14px;margin:28px 0 6px}
p{color:var(--dim);margin:4px 0;overflow-wrap:anywhere}code{font-family:var(--mono);color:var(--fg)}
.parts{display:flex;flex-wrap:wrap;gap:6px 6px;margin:4px 0 2px}
.part{position:relative;width:144px;font:10px/1.2 var(--mono);color:var(--fg)}
.nm small{font-size:9px;color:#cfd6dd}.part>.nm{display:none;position:absolute;left:0;right:0;bottom:0;padding:3px 4px;background:rgba(0,0,0,.8);color:#fff;user-select:text;cursor:text}
.side li a.cur{background:#2a2f36;color:#fff;box-shadow:inset 2px 0 0 #b9c1ca}.side li a.cur b{color:#b9c1ca}
.part:hover>.nm{display:block}
.part .px{display:block;width:96px;height:48px;image-rendering:pixelated;background-size:96px 48px;outline:1px solid var(--rule)}
.cx{display:block;width:144px;height:120px;outline:1px solid var(--rule)}.cx>*{display:block;width:144px;height:120px;background-size:144px 120px}
.sw .c2{display:none}.sw:hover .c1{display:none}.sw:hover .c2{display:block}.sw{cursor:zoom-in}
.ref{display:block;position:relative;outline:1px dashed rgba(255,150,20,.55);outline-offset:-1px}
.ref .cx{opacity:.55;filter:grayscale(.35);outline:0;transition:opacity .12s}
.ref>b{position:absolute;left:5px;top:4px;color:rgba(255,170,60,.85);font:600 10px var(--mono);pointer-events:none}
.part:hover .ref .cx{opacity:.9;filter:none}.nm i{color:#ffb24a;font-style:normal}
.cx.blank{outline:2px dashed rgb(255,150,20);outline-offset:-2px;background:none}
.cx.none{outline:1px dashed var(--dim);outline-offset:-1px;display:flex;align-items:center;justify-content:center;text-align:center;font:11px/1.2 var(--mono);color:var(--dim);padding:4px;box-sizing:border-box}
.part .empty{outline:1px dashed var(--dim);outline-offset:-1px}.part.no{color:var(--dim)}.part.no .wf{opacity:.6}
.flt{display:flex;flex-wrap:wrap;gap:6px;margin:14px 0 0}.flt button{font:11px var(--mono);color:var(--dim);background:var(--panel);
border:1px solid var(--rule);border-radius:3px;padding:3px 8px;cursor:pointer}.flt button[aria-pressed="true"]{color:var(--fg);border-color:var(--dim)}
.flt button:focus-visible{outline:2px solid var(--frame)}body[data-f="ok"] [data-ok="0"],body[data-f="no"] [data-ok="1"]{display:none}
.t{border-top:1px solid var(--rule);padding:10px 0;display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:4px 16px}
.t h3{font-size:13px;margin:0}.meta{font:11px var(--mono);color:var(--dim)}.ok{color:var(--ok)}.no{color:var(--no)}
.note{border-left:2px solid var(--dim);padding:2px 0 2px 10px;color:var(--dim);max-width:70ch}
.lbl{font:600 10px var(--mono);color:var(--dim);margin-top:4px}.pic{grid-row:1/span 6;grid-column:2}.pic img{display:block;max-width:100%;height:auto}
.pv{padding:0;border:0;background:none;cursor:zoom-in;display:block;max-width:100%}
.pop{padding:0;border:1px solid var(--rule);background:var(--bg);overflow:hidden}
.pop::backdrop{background:rgba(0,0,0,.7)}.pop img{display:block;max-width:86vw;max-height:86vh;width:auto;height:auto}
img{display:block;max-width:100%}
@media (max-width:640px){.t{grid-template-columns:minmax(0,1fr)}.pic{grid-row:auto;grid-column:1}}
"""


def page(reports, stamp, sets=None, inline=None, contexts=()):
    """The page: files under img/; inline: {picture name: data URI} for one self-contained file (each picture once)."""
    def px(name):
        if inline is not None:
            return f'<i class="px q{abs(hash(name)) % 10**9}"></i>' if name in inline else '<i class="px"></i>'
        return f'<img class="px" src="img/{name}" alt="">'

    def where(p, kind, own=None, template=None, tile=None, like=None):
        """Where the part sits, boxed: in the terrain itself (own: its stem), else in its template on its tileset (template:
        "<era>_<stand-in>_<ground>"), else in the game's Asphalt (flat edge) or High Dirt (cliff) on Dirt on Badlands."""
        k = 'c' if kind.startswith('cliff') else 'e'
        l, r = (p[1] ^ 15, p[2] ^ 15) if kind == 'cliff down' else (p[1], p[2])  # (its terrain the low side: the high side's corners)
        tail = f'{p[0]}_{p[1]}_{p[2]}.webp'
        near = [f'near_{tile[5:]}'] if tile else []
        name = next((n for n in ([f'own_{own}_{tail}'] if own else near) + ([f'orig_{like}_{tail}'] if like else [])
                     + ([f'tpl_{template}_{tail}'] if template else []) + near
                     + [f'ctx_{k}_{p[0]}_{l}_{r}.webp'] if n in contexts), None)
        solo = tile if tile in contexts else None  # (the tile alone: hover shows it in place of the context)

        def pic(n, cls):
            if inline is not None:
                return f'<i class="{cls} q{abs(hash(n)) % 10**9}"></i>'
            return f'<img class="{cls}" src="img/{n}" width="144" height="120" alt="">'
        src = (NEAR_SRC.get(name, 'tpl') + ' near' if name.startswith('near_') else name.split('_')[0]) if name else 'tile'
        if name and solo:
            return f'<span class="cx sw">{pic(name, "c1")}{pic(solo, "c2")}</span>', src
        if name or solo:
            return f'<span class="cx">{pic(name or solo, "c1")}</span>', src
        return '<i class="cx none">no example</i>', None

    def part(name, p, kind, piece, title, own=None, template=None, tile=None, like=None, sources=None, ids=''):
        """A part: where it sits in the terrain (piece False: the legend's), or a blank box with an orange dashed outline
        (piece None: the set has no piece for it); its tile ids on hover."""
        hole = f'near_{tile[5:]}' if tile else ''
        if piece is None:  # (missing: where the tile would be, empty, in the set's own terrain)
            pic = (f'<i class="cx q{abs(hash(hole)) % 10**9}"></i>' if inline is not None else
                   f'<span class="cx"><img class="c1" src="img/{hole}" width="144" height="120" alt=""></span>') if hole in contexts else '<i class="cx blank"></i>'
        else:
            pic = where(p, kind, own, template, tile, like)[0]
        info = ('not in the set<br>' if piece is None else '') + ids
        label = f'<span class="nm"><small>{info}</small></span>' if info else ''
        return f'<div class="part{" no" if piece is None else ""}" title="{html.escape(title)}">{pic}{label}</div>'

    head = ['<title>Cosmonarchy Terrain Report</title>', '<meta name="claude:viewport-fit-sides" content="cover">'] if inline is not None else \
        ['<!doctype html>', '<meta charset="utf-8">', '<meta name="viewport" content="width=device-width,initial-scale=1">',
         '<title>Cosmonarchy Terrain Report</title>']
    style = STYLE
    if inline is not None:
        style += ''.join(f'.q{abs(hash(n)) % 10**9}{{background-image:url({u})}}' for n, u in inline.items())
    total = sum(len(r['terrains']) for r in reports)
    complete = sum(1 for r in reports for t in r['terrains'] if not t['_missing'])
    # (the legend: parts of each shape, as a flat edge and as a cliff)
    patterns = sorted({t['_pattern'][o] for r in reports for t in r['terrains'] for o in t['_rects']})
    def visible(p):  # (the corners a part shows in its rect: both kinds, or the part looks all one terrain)
        par, l, r = p
        return {l >> 2 & 1, l >> 3 & 1, r >> 1 & 1, r & 1} if par == 0 else {l >> 1 & 1, l >> 2 & 1, r & 1, r >> 3 & 1}
    sample, shapes = [], set()
    for p in patterns:
        shape = (HALF[p[1]].split()[-1], HALF[p[2]].split()[-1])
        if shape not in shapes and len(sample) < 6 and len(visible(p)) == 2:
            shapes.add(shape), sample.append(p)
    legend = ('<div class="lbl">flat edge: where each part sits (box) in the game&rsquo;s Asphalt on Dirt</div><div class="parts">' +
              ''.join(part('', p, 'edge', False, '') for p in sample) + '</div>')
    legend += ('<div class="lbl">cliff: the same in the game&rsquo;s High Dirt on Dirt</div><div class="parts">' +
               ''.join(part('', p, 'cliff up', False, '') for p in sample) + '</div>')
    legend += ('<div class="lbl">in a terrain: the part in the terrain itself; an orange dashed box: missing. '
               'Hover: its tile alone and its ids</div>')
    def anchor(*parts):  # (a link's name: lowercase words joined by '-')
        return '-'.join(w for p in parts for w in re.findall(r'[a-z0-9]+', p.lower()))
    nav = ['<nav class="side" aria-label="Terrains">']
    for r in sorted(reports, key=lambda r: r['era']):
        nav.append(f'<h4><a href="#{anchor(r["tileset"])}">{html.escape(r["tileset"].title())}</a></h4><ul>')
        for t in r['terrains']:
            have = len(t['_required']) - len(t['_missing'])
            nav.append(f'<li data-ok="{0 if t["_missing"] else 1}"><a href="#{anchor(r["tileset"], t["name"])}">'
                       f'<span>{html.escape(t["name"])}</span><b>{have}/{len(t["_required"])}</b></a></li>')
        nav.append('</ul>')
    nav.append('</nav>')
    out = head + [f'<style>{style}</style>', '<div class="layout">'] + nav + ['<main>', '<h1>Cosmonarchy terrain report</h1>',
                  '<p class="note">Made by StarDraft from its own reading of the mod&rsquo;s brush sets and tiles. It rests on '
                  'assumptions (which game tiles a part needs, which tiles a set has for it) and can contain inaccuracies.</p>',
                  f'<p>{html.escape(stamp)} &middot; Prerelease brush sets &rarr; StarDraft 2 ISOM terrains &middot; '
                  f'<span class="{"ok" if complete == total else "no"}">{complete}/{total} complete</span></p>',
                  '<p>Part = one ISOM rect (two tiles side by side) of the border. Picture: where the part sits in the terrain '
                  '(white box); an orange dashed box: the set has no piece for it. Hover: the tile alone and its ids (missing: the '
                  'game&rsquo;s tile group for it). Kinds: <code>cliff</code> a level up or down; '
                  '<code>edge</code> flat, with inner corners (grass, asphalt, mud); <code>edge, no inner</code> flat, the game has '
                  'no inner corners (rocky ground, shale).</p>',
                  '<p><code>parts</code> the parts the set has of all the game&rsquo;s border has: all present = it paints every shape '
                  'as the game&rsquo;s terrain does. <code>var</code> fewest pieces the set has for one part. Overview: click it for '
                  'every example shape, large. Tooltip: CV5 groups.</p>',
                  f'<div class="legend">{legend}</div>',
                  f'<div class="flt" role="group" aria-label="show"><button data-f="all" aria-pressed="true">all {total}</button>'
                  f'<button data-f="ok" aria-pressed="false">complete {complete}</button>'
                  f'<button data-f="no" aria-pressed="false">incomplete {total - complete}</button></div>']
    if sets is not None:
        todo, left = sets
        out.append('<h2>Sets not yet terrains</h2>')
        out.append('<p>' + (', '.join(f'{html.escape(n)} ({html.escape(ts)})' for ts, n in todo) if todo else 'none') + '</p>')
        if left:
            out.append('<p>left out: ' + ', '.join(f'{html.escape(n)} ({html.escape(ts)})' for ts, n in left) + '</p>')
    for r in sorted(reports, key=lambda r: r['era']):
        ok = sum(1 for t in r['terrains'] if not t['_missing'])
        out.append(f'<h2 id="{anchor(r["tileset"])}">{html.escape(r["tileset"].title())} &middot; {ok}/{len(r["terrains"])}</h2>')
        for t in r['terrains']:
            stem = f'{r["era"]}_{t["n"]}'
            req = len(t['_required'])
            have = req - len(t['_missing'])

            def strip(rects):
                cells, seen = [], {}
                for o in rects:
                    p = t['_pattern'][o]
                    name = ''
                    covered = o in t['_covered']
                    piece = px(f'{stem}_m{t["_piece"][o]}.webp') if covered else None
                    pc = next((x for x in t['pieces'] if x['game'] == t['_piece'][o]), None) if covered else None
                    if pc and 'tiles' in pc:  # (the set's tiles: group.index in the .cv5, their megatiles in the .vx4ex)
                        a, b = pc['tiles']
                        ids = (f'tile {a >> 4}.{a & 15} + {b >> 4}.{b & 15} ({r["tileset"]}.cv5)<br>'
                               f'VX4 {pc["mega"][0]} + {pc["mega"][1]} ({r["tileset"]}.vx4ex)')
                    else:
                        ids = 'game tile group ' + ', '.join(map(str, o)) + f' ({r["tileset"]}.cv5)'
                    cells.append(part(name, p, t.get('kind', 'edge'), piece, 'CV5 ' + ', '.join(map(str, o)),
                                      stem if covered else None, f'{r["era"]}_{t["templateIds"][0]}_{t["templateIds"][1]}',
                                      f'tile_{stem}_{t["_piece"][o]}h.webp' if covered else f'tile_{stem}_{o[0]}m.webp',
                                      f'{t["_like"]["era"]}_{t["_like"]["id"]}' if t['_like'] and not covered else None,
                                      {'orig': html.escape(t['_like']['name']) if t['_like'] else '', 'tpl': html.escape(t['template'])}, ids))
                return f'<div class="parts">{"".join(cells)}</div>' if cells else '<div class="meta">&ndash;</div>'
            if inline is not None:
                small = f'{stem}_small.jpg'
                pic = f'<img src="{inline[small]}" width="320" height="{SMALL_H // 2}" alt="">' if small in inline else ''
            else:
                pic = (f'<button class="pv" popovertarget="pv{stem}" title="every example shape, large">'
                       f'<img src="img/{stem}_small.jpg" width="320" height="{SMALL_H // 2}" alt=""></button>'
                       f'<div popover id="pv{stem}" class="pop"><img loading="lazy" src="img/{stem}_paint.jpg" alt=""></div>')
            def files_of(r, t):  # (where the set's tiles are: its tile groups in the .cv5, their megatiles in the .vx4ex)
                ts = [v for pc in t['pieces'] for v in pc.get('tiles', [])]
                ms = [m for pc in t['pieces'] for m in pc.get('mega', [])]
                if not ts:
                    return ''
                return (f'<br>tile groups {min(ts) >> 4}&ndash;{max(ts) >> 4} in {r["tileset"]}.cv5 &middot; '
                        f'megatiles {min(ms)}&ndash;{max(ms)} in {r["tileset"]}.vx4ex')
            out.append(f'<div class="t" id="{anchor(r["tileset"], t["name"])}" data-ok="{0 if t["_missing"] else 1}"><h3>{html.escape(t["name"])}</h3><div class="pic">{pic}</div>'
                       f'<div class="meta">{t["_kind"]} &middot; template {html.escape(t["template"])} &middot; '
                       f'parts <span class="{"ok" if not t["_missing"] else "no"}">{have}/{req}</span> &middot; var {t["_variations"]}'
                       f'{files_of(r, t)}'
                       '</div>'
                       f'{strip(t["_required"])}</div>')
    out.append('</main></div>')
    # (the wireframes, once: the rect's clip and a symbol each)
    # (the filter: all, complete or incomplete terrains)
    out.append('<script>document.querySelectorAll(".flt button").forEach(b=>b.addEventListener("click",()=>{'
               'document.body.dataset.f=b.dataset.f;document.querySelectorAll(".flt button").forEach(x=>x.setAttribute("aria-pressed",x===b))}));'
               # (the sidebar marks the terrain at the top of the view and its tileset, and keeps it in sight)
               'const links={};document.querySelectorAll(".side a").forEach(a=>links[a.getAttribute("href").slice(1)]=a);'
               'let cur=[];function mark(){let best=null;for(const el of document.querySelectorAll("main .t,main h2")){'
               'const r=el.getBoundingClientRect();if(el.offsetParent===null)continue;if(r.top<=innerHeight*0.35)best=el;else break}'
               'cur.forEach(a=>a.classList.remove("cur"));cur=[];if(!best)return;let h=best;'
               'while(h&&h.tagName!=="H2")h=h.previousElementSibling;for(const el of [best,h]){const a=el&&links[el.id];'
               'if(a){a.classList.add("cur");cur.push(a)}}const a=links[best.id];if(a){const s=document.querySelector(".side"),'
               'ar=a.getBoundingClientRect(),sr=s.getBoundingClientRect();if(ar.top<sr.top+40||ar.bottom>sr.bottom-40)'
               's.scrollTop+=ar.top-sr.top-sr.height/2}}addEventListener("scroll",()=>requestAnimationFrame(mark),{passive:true});mark()</script>')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--install', required=True)
    ap.add_argument('--bin', required=True, help='the folder with cmbw_report(.exe)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--maps')
    ap.add_argument('--single', help='also one HTML file with its pictures in it (no skeleton: for a preview)')
    ap.add_argument('--raw', help='the folder for the probe\'s output (one with its reports already: no painting)')
    a = ap.parse_args()
    from PIL import Image
    maps = a.maps or os.path.join(a.install, '..', 'Starcraft', 'Maps', 'CMBW-Root', 'CMBW-maps')
    exe = os.path.join(a.bin, 'cmbw_report.exe' if os.name == 'nt' else 'cmbw_report')
    raw = a.raw or tempfile.mkdtemp(prefix='cmbw_report_')
    if not any(f.startswith('report_') for f in os.listdir(raw)) if os.path.isdir(raw) else True:
        run_probe(exe, a.install, maps, raw)
    reports = [json.load(open(os.path.join(raw, f), encoding='utf-8')) for f in sorted(os.listdir(raw)) if f.startswith('report_')]
    analyse(reports)
    img = os.path.join(a.out, 'img')
    if os.path.isdir(img):  # (the pictures of the run before: a renumbered terrain leaves none behind)
        for f in os.listdir(img):
            os.remove(os.path.join(img, f))
    os.makedirs(img, exist_ok=True)
    def small_copy(src, name):  # (a part's picture: lossless WebP, a third of the PNG)
        Image.open(src).save(os.path.join(img, name), 'WEBP', lossless=True, quality=100, method=6)
    # (each part's place: the middle 6 x 5 tiles of the crop, full size, the part boxed; the ones the page shows: a terrain's
    # own for the parts it has, its template's for those it lacks, the game's on Badlands for the legend)
    from PIL import ImageDraw
    wanted = {f for f in os.listdir(raw) if f.startswith('ctx_') and f.endswith('.png')}
    for r in reports:
        for t in r['terrains']:
            for o in t['_rects']:
                p = t['_pattern'][o]
                tail = f'{p[0]}_{p[1]}_{p[2]}.png'
                if o in t['_covered']:
                    wanted.add(f'own_{r["era"]}_{t["n"]}_{tail}')
                else:
                    wanted.add(f'tpl_{r["era"]}_{t["templateIds"][0]}_{t["templateIds"][1]}_{tail}')
                    if t['_like']:
                        wanted.add(f'orig_{t["_like"]["era"]}_{t["_like"]["id"]}_{tail}')
    contexts = set()
    for f in wanted:
        if not os.path.exists(os.path.join(raw, f)):
            continue
        im = Image.open(os.path.join(raw, f)).convert('RGB').crop((64, 48, 256, 208))  # (full size: 192 x 160)
        ImageDraw.Draw(im).rectangle((63, 47, 128, 80), outline=WHITE if f.startswith(('own_', 'ctx_')) else YELLOW, width=3)
        name = f.replace('.png', '.webp')
        im.save(os.path.join(img, name), 'WEBP', quality=90, method=6)
        contexts.add(name)
    # (a part no painting has: the painting whose rect's corners come closest (the terrain's own, the game terrain like
    # it, its template), its tile put in the box: near_<as its tile alone>)
    raw_files = os.listdir(raw)
    for r in reports:
        for t in r['terrains']:
            stem = f'{r["era"]}_{t["n"]}'
            like = f'{t["_like"]["era"]}_{t["_like"]["id"]}' if t['_like'] else None
            tpl = f'{r["era"]}_{t["templateIds"][0]}_{t["templateIds"][1]}'
            for o in t['_rects']:
                p = t['_pattern'][o]
                covered = o in t['_covered']
                tail = f'{p[0]}_{p[1]}_{p[2]}.webp'
                if covered and f'own_{stem}_{tail}' in contexts:
                    continue
                g = t['_piece'][o] if covered else o[0]
                # (the tile in the box: the set's piece; missing: the like game terrain's tile, else its template's)
                tile_src = 'own' if covered else 'orig'
                src = os.path.join(raw, f'{stem}_map_{g}.png' if covered else f'origtile_{like}_{tail[:-5]}.png')
                if not covered and (not like or not os.path.exists(src)):
                    tile_src, src = 'tpl', os.path.join(raw, f'{stem}_miss_{g}.png')
                if covered and not os.path.exists(src):
                    continue
                best = None
                for rank, pre in enumerate([f'own_{stem}_'] + ([f'orig_{like}_'] if like else []) + [f'tpl_{tpl}_']):
                    for f in raw_files:
                        if not (f.startswith(pre) and f.endswith('.png')) or f.endswith('_all.png'):
                            continue
                        try:
                            par, l, rr = map(int, f[len(pre):-4].split('_'))
                        except ValueError:
                            continue
                        if par != p[0]:
                            continue
                        key = (rank, bin(l ^ p[1]).count('1') + bin(rr ^ p[2]).count('1'))  # (the set's own painting first)
                        if best is None or key < best[0]:
                            best = (key, f)
                if not best:
                    continue
                im = Image.open(os.path.join(raw, best[1])).convert('RGB')
                if covered:
                    im.paste(Image.open(src).convert('RGB'), (128, 96))
                else:  # (missing: the place left empty, an orange dashed outline round it)
                    im.paste((18, 20, 23), (128, 96, 192, 128))
                im = im.crop((64, 48, 256, 208))
                d = ImageDraw.Draw(im)
                if covered:
                    d.rectangle((63, 47, 128, 80), outline=WHITE, width=3)
                else:
                    for x in range(63, 129, 8):
                        d.line((x, 47, min(x + 4, 128), 47), fill=YELLOW, width=2), d.line((x, 80, min(x + 4, 128), 80), fill=YELLOW, width=2)
                    for y in range(47, 81, 8):
                        d.line((63, y, 63, min(y + 4, 80)), fill=YELLOW, width=2), d.line((128, y, 128, min(y + 4, 80)), fill=YELLOW, width=2)
                name = f'near_{stem}_{g}{"h" if covered else "m"}.webp'
                NEAR_SRC[name] = tile_src
                im.save(os.path.join(img, name), 'WEBP', quality=90, method=6)
                contexts.add(name)
    # (each part's tile alone, twice its size, boxed: the set's piece, or the game's tile from the terrain's tileset where it
    # has none; hover shows it in place of the part's context, and it stands alone where no painting has the part)
    for r in reports:
        for t in r['terrains']:
            stem = f'{r["era"]}_{t["n"]}'
            for o in t['_rects']:
                p = t['_pattern'][o]
                tail = f'{p[0]}_{p[1]}_{p[2]}.webp'
                tpl = f'tpl_{r["era"]}_{t["templateIds"][0]}_{t["templateIds"][1]}_{tail}'
                g = t['_piece'][o] if o in t['_covered'] else o[0]
                src = os.path.join(raw, f'{stem}_map_{g}.png' if o in t['_covered'] else f'{stem}_miss_{g}.png')
                if not os.path.exists(src):
                    continue
                im = Image.new('RGB', (192, 160), (24, 27, 31))
                im.paste(Image.open(src).convert('RGB').resize((128, 64), Image.NEAREST), (32, 48))  # (twice: alone, it reads)
                ImageDraw.Draw(im).rectangle((31, 47, 160, 112), outline=WHITE if o in t['_covered'] else YELLOW, width=3)
                name = f'tile_{stem}_{g}{"h" if o in t["_covered"] else "m"}.webp'
                im.save(os.path.join(img, name), 'WEBP', quality=90, method=6)
                contexts.add(name)
    for r in reports:
        for t in r['terrains']:
            stem = f'{r["era"]}_{t["n"]}'
            paint = os.path.join(raw, f'own_{stem}_all.png')  # (its overview: every shape of the examples painted)
            if os.path.exists(paint):
                im = Image.open(paint).convert('RGB')
                im.save(os.path.join(img, f'{stem}_paint.jpg'), quality=92)
                im.resize((640, SMALL_H), Image.LANCZOS).save(os.path.join(img, f'{stem}_small.jpg'), quality=90)
            for g in set(t['_piece'].values()):
                src = os.path.join(raw, f'{stem}_map_{g}.png')
                if os.path.exists(src):
                    small_copy(src, f'{stem}_m{g}.webp')
    stamp = datetime.date.today().isoformat()
    sets = set_status(a.bin, a.install, maps)
    with open(os.path.join(a.out, 'index.html'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(page(reports, stamp, sets, contexts=contexts))
    if a.single:  # (one file, its pictures in it: a preview of the page anywhere)
        inline = {}
        for name in sorted(os.listdir(img)):
            if name.endswith('_paint.jpg'):
                continue
            kind = 'image/jpeg' if name.endswith('.jpg') else 'image/webp'
            with open(os.path.join(img, name), 'rb') as pic:
                inline[name] = f'data:{kind};base64,' + base64.b64encode(pic.read()).decode()
        with open(a.single, 'w', encoding='utf-8', newline='\n') as f:
            f.write(page(reports, stamp, sets, inline, contexts))
    total = sum(len(r['terrains']) for r in reports)
    complete = sum(1 for r in reports for t in r['terrains'] if not t['_missing'])
    print(f'{os.path.join(a.out, "index.html")}: {complete} of {total} terrains complete')


if __name__ == '__main__':
    main()
