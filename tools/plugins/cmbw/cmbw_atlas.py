#!/usr/bin/env python3
"""Packs the terrain report's pictures into a few sheets (atlas/) and rewrites its index.html to show them from there:
a page of one file per picture asks the server for about 10,000 files and is rate limited. The tile parts go in WebP
sheets, the overviews in JPEG sheets; the large pictures stay in img/ (they load when opened), and the pictures the page
does not use are removed. It also gives the page its narrow-screen menu: the list of terrains is a panel that a
floating button opens and closes.

usage: python tools/plugins/cmbw/cmbw_atlas.py <the report's folder>   (with index.html and img/; run it once, after
cmbw_report.py)
"""
import os, re, sys
from PIL import Image

root = sys.argv[1]
page = os.path.join(root, 'index.html')
img = os.path.join(root, 'img')
out = os.path.join(root, 'atlas')
os.makedirs(out, exist_ok=True)
html = open(page, encoding='utf-8').read()

# The parts: 192x160 pictures shown at 144x120
CW, CH, DW, DH, COLS, ROWS = 192, 160, 144, 120, 16, 12
part_re = re.compile(r'<img class="(c[12])" src="img/([^"]+\.webp)" width="144" height="120" alt="">')
names = list(dict.fromkeys(m.group(2) for m in part_re.finditer(html)))
per = COLS * ROWS
where = {}
for s in range(0, len(names), per):
    chunk = names[s:s + per]
    k = s // per
    rows = (len(chunk) + COLS - 1) // COLS
    sheet = Image.new('RGB', (COLS * CW, rows * CH))
    for i, n in enumerate(chunk):
        im = Image.open(os.path.join(img, n)).convert('RGB')
        assert im.size == (CW, CH), n
        sheet.paste(im, ((i % COLS) * CW, (i // COLS) * CH))
        where[n] = (k, i % COLS, i // COLS, rows)
    sheet.save(os.path.join(out, f'p{k}.webp'), quality=90, method=6)  # (the parts are lossy WebP; 90 keeps their size)
nparts = k + 1

def part(m):
    k, c, r, rows = where[m.group(2)]
    return (f'<i class="{m.group(1)} p{k}" style="background-position:-{c * DW}px -{r * DH}px;'
            f'background-size:{COLS * DW}px {rows * DH}px"></i>')
html = part_re.sub(part, html)

# The small overviews: 640x530 shown at up to 320 wide (they shrink with the column)
SW, SH, SC, SR = 640, 530, 3, 4
small_re = re.compile(r'<img src="img/([^"]+_small\.jpg)" width="320" height="265" alt="">')
smalls = [m.group(1) for m in small_re.finditer(html)]
sper = SC * SR
swhere = {}
for s in range(0, len(smalls), sper):
    chunk = smalls[s:s + sper]
    k = s // sper
    rows = (len(chunk) + SC - 1) // SC
    sheet = Image.new('RGB', (SC * SW, rows * SH))
    for i, n in enumerate(chunk):
        im = Image.open(os.path.join(img, n)).convert('RGB')
        assert im.size == (SW, SH), n
        sheet.paste(im, ((i % SC) * SW, (i // SC) * SH))
        swhere[n] = (k, i % SC, i // SC, rows)
    sheet.save(os.path.join(out, f's{k}.jpg'), quality=90)
nsmalls = k + 1

def small(m):
    k, c, r, rows = swhere[m.group(1)]
    x = c / (SC - 1) * 100 if SC > 1 else 0
    y = r / (rows - 1) * 100 if rows > 1 else 0
    return (f'<i class="sm s{k}" style="background-position:{x:g}% {y:g}%;'
            f'background-size:{SC * 100}% {rows * 100}%"></i>')
html = small_re.sub(small, html)

css = ('.cx>i{background-repeat:no-repeat}'
       '.sm{display:block;width:320px;max-width:100%;aspect-ratio:320/265;background-repeat:no-repeat}'
       + ''.join(f'.p{k}{{background-image:url(atlas/p{k}.webp)}}' for k in range(nparts))
       + ''.join(f'.s{k}{{background-image:url(atlas/s{k}.jpg)}}' for k in range(nsmalls)))
# On a narrow screen the list of terrains is a panel that a floating button opens and closes
css += ('.mb,.scrim{display:none}'
        '@media (max-width:900px){'
        '.side{position:fixed;top:0;right:0;bottom:0;left:auto;width:min(300px,84vw);max-height:none;z-index:20;'
        'background:var(--panel);border-left:1px solid var(--rule);padding:12px 12px 80px;'
        'transform:translateX(100%);visibility:hidden;transition:transform .18s,visibility .18s}'
        'body.menu .side{transform:none;visibility:visible}'
        'body.menu .scrim{display:block;position:fixed;inset:0;z-index:19;background:rgba(0,0,0,.55)}'
        '.mb{display:flex;align-items:center;gap:6px;position:fixed;right:16px;bottom:max(16px,env(safe-area-inset-bottom,0px));z-index:21;'
        'font:600 13px var(--sans);color:var(--fg);background:#2a2f36;border:1px solid var(--dim);border-radius:22px;'
        'padding:10px 16px;box-shadow:0 4px 14px rgba(0,0,0,.5);cursor:pointer}'
        '.mb:focus-visible{outline:2px solid var(--frame)}}')
html = html.replace('</style>', css + '\n</style>', 1)
html = html.replace('<nav class="side"', '<nav class="side" id="side"', 1)
html = html.replace('</nav>', '</nav><div class="scrim"></div>'
                    '<button class="mb" aria-controls="side" aria-expanded="false">&#9776; Terrains</button>', 1)
html = html.replace('</script>', ';(()=>{const b=document.querySelector(".mb"),set=o=>{document.body.classList.toggle("menu",o);'
                    'b.setAttribute("aria-expanded",o);b.innerHTML=o?"&#10005; Close":"&#9776; Terrains";if(o)mark()};'
                    'b.addEventListener("click",()=>set(!document.body.classList.contains("menu")));'
                    'document.querySelector(".scrim").addEventListener("click",()=>set(false));'
                    'document.querySelectorAll(".side a").forEach(a=>a.addEventListener("click",()=>set(false)));'
                    'addEventListener("keydown",e=>{if(e.key==="Escape")set(false)})})()</script>', 1)
open(page, 'w', encoding='utf-8', newline='\n').write(html)

# What the page still uses from img/ (the large pictures, loaded when opened); the rest goes
keep = set(re.findall(r'src="img/([^"]+)"', html))
gone = [f for f in os.listdir(img) if f not in keep]
for f in gone:
    os.remove(os.path.join(img, f))
print(f'{len(names)} parts in {nparts} sheets, {len(smalls)} overviews in {nsmalls} sheets; '
      f'{len(keep)} pictures kept in img/, {len(gone)} removed')
