"""Builds the static website (docs/index.html, served by GitHub Pages) from docs/project/ROADMAP.md.

ROADMAP.md is the source. The page shows its "## Now", "## Next" and "## Later" sections. An item is
"- [ ] **R<n> Title.** *Tag:* area." and then, indented, a short paragraph that tells what the feature is.
The page shows the title and that paragraph. The paragraphs after it are only in ROADMAP.md.

    python tools/build_site.py          write docs/index.html and docs/assets
    python tools/build_site.py --check  exit 1 if they are out of date

The screenshots in docs/assets/shots are datashots (not made here), all in a 2100 x 1300 window and cut to the same
1890 x 1260 box (x 8 or 202, y 30), as WebP. The Data workspace needs this width: narrower, its pages go to one column.
Program options > Language must be English for them (it is read even in test runs), except korean.webp. Set it
back to the value it had before.
    stardraft-testing.exe <map> --datashots <dir> rpgdemo:   (makes the AshenCrown project)
    stardraft-testing.exe "(2)Crescent Dyne.scm" --datashots <dir> winsize:2100x1300 view:hd cam:1900:2600:0.7 ws:map
        new:generate
    stardraft-testing.exe "Cave Wars v012.scx" --datashots <dir> winsize:2100x1300 view:hd cam:3067:3058:0.7 ws:map
        quick:units:protoss  (quick.webp: the picker pasted over the map, with a soft shadow)
    stardraft-testing.exe <dir>/AshenCrown/AshenCrown.eds --datashots <dir> winsize:2100x1300 trig:2:2 ws:data units:20
        weapons:0 buttons:51 images:239
    stardraft-testing.exe "(1)Underdogs03.scx" --datashots <dir> winsize:2100x1300 brec:0:40000  (frame f00220.png)
    In Korean: AshenCrown.eds ... trig:2:2 options:  (korean.webp: options_dark.png over the dimmed triggers page)
"""
import html
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROADMAP = ROOT / "docs" / "project" / "ROADMAP.md"
OUT = ROOT / "docs" / "index.html"
ICONS = ROOT / "tools" / "icongen" / "out"
REPO = "https://github.com/stardraft-editor/stardraft"
DISCORD = "https://discord.gg/JBMCwSRJBm"
FEEDBACK = "https://discord.com/channels/1558446827391418458/1559379661111304262"  # the #feedback forum
VIDEO = "pUr3eruPKA4"

# The kinds of projects (and the groups that make them): id, name, UI icon, the features it gets.
PERSONAS = [
    ("melee", "Melee map makers", "terrain",
     "Paint terrain with brushes, spray, fill, replace and symmetry, or generate a full map. See the town area of "
     "each base, in HD, SD or Carbot graphics."),
    ("ums", "UMS & campaign makers", "flag",
     "A modern classic trigger editor, unit, upgrade and technology settings, and mission briefings with preview. "
     "Campaign projects are coming soon."),
    ("eud", "EUD map makers", "triggers",
     "Edit unit, weapon, image and sprite data, and write triggers as blocks in plain sentences, or in your own "
     "epScript files. Checks find EUD errors and desyncs before you build."),
    ("mod", "Mod makers", "molecule",
     "Integrated mod editing: make maps with your custom mod files, and manage them in one place. For mods for "
     "both StarCraft: Remastered and 1.16.1."),
]
BUCKETS = ["Now", "Next", "Later"]


def parse(text):
    """The items, and the sentence under each section heading."""
    items, intros, bucket = [], {}, None
    for block in re.split(r"\n(?=## |- \[)", text):
        h = re.match(r"## (\w+)(.*)", block, re.S)
        if h:
            bucket = h.group(1)
            intros[bucket] = " ".join(h.group(2).split())
            continue
        m = re.match(r"- \[ \] \*\*(R\d+) (.+?)\.?\*\*(.*)\n((?:  \S.*\n?)+)", block)
        if not m or bucket not in BUCKETS:  # done items ("- [x]") are not shown
            continue
        tag = re.sub(r"^\s*\*Tag:\*\s*", "", m.group(3)).strip().rstrip(".")
        tags = [x.strip() for x in tag.split(",") if x.strip()]
        items.append({"id": m.group(1), "title": m.group(2), "text": " ".join(m.group(4).split()),
                      "bucket": bucket, "tags": tags})
    return items, intros


# Icons that only the website uses, in the style of the UI icons (16 px box, lines of weight 1 on .5).
_LINE = 'fill="none" stroke="#fff" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
SITE_ICONS = {
    "flag": f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">'
            f'<path d="M3.5 1.5V14.5" {_LINE}/><path d="M3.5 2.5H13L10.5 5.5L13 8.5H3.5" {_LINE}/></svg>',
    "molecule": f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">'
                f'<circle cx="8" cy="4" r="2" {_LINE}/><circle cx="4" cy="12" r="2" {_LINE}/>'
                f'<circle cx="12" cy="12" r="2" {_LINE}/>'
                f'<path d="M7.1 5.8L4.9 10.2M8.9 5.8L11.1 10.2M6 12H10" {_LINE}/></svg>',
}


def icon(name):
    """A UI icon (tools/icongen/out/ui/theme, or SITE_ICONS) as inline SVG in the text colour."""
    svg = SITE_ICONS.get(name) or (ICONS / "ui" / "theme" / f"{name}.svg").read_text(encoding="utf-8")
    svg = svg.replace('<rect width="16" height="16" fill="#000"/>', "").replace("#fff", "currentColor")
    return svg.replace('width="16" height="16"', 'class="icon" overflow="visible" aria-hidden="true"', 1)


def inline(s):
    s = html.escape(s, quote=False)
    s = re.sub(r"\[(.+?)\]\((https://[^)\s]+)\)", r'<a href="\2">\1</a>', s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
    return re.sub(r"\*(.+?)\*", r"<em>\1</em>", s)


def item_html(i):
    tags = i["tags"]
    rid = f'R{int(i["id"][1:]):04}'  # R36 shows as R0036
    anchor = rid.lower()
    # The last line: the id, which links to the item (to copy a link to it), then the tags.
    meta = " · ".join([f'<a class="rid" href="#{anchor}" title="Link to this item">#{rid}</a>'] +
                      [html.escape(t) for t in tags])
    return (f'        <li class="item" id="{anchor}"><h4>{inline(i["title"])}</h4>'
            f'<p>{inline(i["text"])}</p><p class="who">{meta}</p></li>')


def build(items, intros):
    personas = "\n".join(
        f'      <div class="persona{" soon" if pid == "mod" else ""}"><span class="tile">{icon(ic)}</span>'
        f'<h3>{html.escape(n)}</h3><p>{html.escape(t)}</p>'
        f'{"<p class=badge>Coming soon</p>" if pid == "mod" else ""}</div>'
        for pid, n, ic, t in PERSONAS)
    buckets = []
    for name in BUCKETS:
        cards = "\n".join(item_html(i) for i in items if i["bucket"] == name)
        body = f'<ul class="items">\n{cards}\n      </ul>' if cards else '<p class="empty">Nothing listed yet.</p>'
        intro = f'<p>{inline(intros[name])}</p>' if intros.get(name) else ""
        buckets.append(f'      <div class="bucket" id="{name.lower()}">\n        <div class="rail"><h3>{name}</h3>'
                       f'{intro}</div>\n      {body}\n      </div>')
    return TEMPLATE.format(personas=personas, buckets="\n".join(buckets), repo=REPO, discord=DISCORD, feedback=FEEDBACK, video=VIDEO,
                           i_select=icon("select"), i_feedback=icon("feedback"), i_issue=icon("issue"))


TEMPLATE = """<!doctype html>
<!-- Generated by tools/build_site.py from docs/project/ROADMAP.md. Do not edit. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>StarDraft 2</title>
<meta name="description" content="StarDraft 2 is a StarCraft creation suite for map and mod creators, and the spiritual successor to StarDraft by Camelot Systems.">
<meta name="color-scheme" content="dark">
<link rel="icon" type="image/png" sizes="32x32" href="assets/app-32.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>
:root {{
  --bg: #0c0e11; --raise: #13161a; --line: #252a31; --grid: rgba(92, 224, 150, 0.06);
  --text: #e9e5dc; --muted: #8e939b; --accent: #5ce096; --on-accent: #04130a;
  --display: "Chakra Petch", system-ui, sans-serif;
  --sans: "IBM Plex Sans", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, Consolas, monospace;
}}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; }}
body {{ margin: 0; background: var(--bg); color: var(--text); font: 16px/1.6 var(--sans);
       -webkit-font-smoothing: antialiased; }}
.wrap {{ max-width: 1280px; margin: 0 auto; padding: 0 16px; }}
@media (min-width: 700px) {{ .wrap {{ padding: 0 32px; }} }}
a {{ color: inherit; text-decoration: underline; text-underline-offset: 2px; }}
h1, h2, h3 {{ font-family: var(--display); font-weight: 600; letter-spacing: 0.01em; }}
.icon {{ width: 24px; height: 24px; color: var(--accent); display: block; flex: none; }}

/* Hero on a faint 32 px tile grid, the size of one map tile. */
header {{ position: relative; padding: 96px 0 104px; border-bottom: 1px solid var(--line); }}
header::before {{ content: ""; position: absolute; inset: 0; pointer-events: none;
  background-image: linear-gradient(var(--grid) 1px, transparent 1px),
                    linear-gradient(90deg, var(--grid) 1px, transparent 1px);
  background-size: 32px 32px; background-position: -1px -1px;
  -webkit-mask-image: linear-gradient(#000 40%, transparent);
          mask-image: linear-gradient(#000 40%, transparent); }}
.hero {{ position: relative; display: grid; gap: 48px 64px; align-items: center; }}
@media (min-width: 1000px) {{ .hero {{ grid-template-columns: 5fr 6fr; }} }}
.shot {{ display: block; width: 100%; height: auto; border: 1px solid var(--line);
        box-shadow: 0 24px 64px rgba(0, 0, 0, 0.5); }}
.slides {{ display: flex; overflow-x: auto; scroll-snap-type: x mandatory; scrollbar-width: none; outline: none; }}
.slides::-webkit-scrollbar {{ display: none; }}
.carousel {{ position: relative; min-width: 0; }}
.slide {{ flex: 0 0 100%; scroll-snap-align: start; margin: 0; padding: 1px; }}
.slide .shot {{ aspect-ratio: 3 / 2; object-fit: cover; object-position: left top; box-shadow: none; cursor: zoom-in; }}
/* While the carousel plays, a faint ring around the next arrow fills up; when it closes, the next slide shows. */
.controls .arrow {{ position: relative; }}
.ring {{ position: absolute; inset: 6px; width: 28px; height: 28px; transform: rotate(-90deg); pointer-events: none; }}
.ring circle {{ fill: none; stroke: var(--muted); stroke-width: 1; opacity: 0; stroke-dasharray: 84.82;
               stroke-dashoffset: 84.82; }}
.ring.run circle {{ opacity: 0.55; animation: ring 6s linear forwards; }}
.carousel:hover .ring.run circle {{ animation-play-state: paused; }}
@keyframes ring {{ to {{ stroke-dashoffset: 0; }} }}
.slide figcaption {{ margin-top: 12px; padding-right: 96px; font: 0.78rem/40px var(--mono); color: var(--muted);
                    white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
@media (max-width: 599px) {{ .slide figcaption {{ padding-right: 0; }}
  .carousel .controls {{ position: static; justify-content: center; margin-top: 4px; }} }}
.controls {{ position: absolute; right: 0; bottom: 0; display: flex; align-items: center; gap: 2px; }}
.controls button {{ display: flex; align-items: center; justify-content: center; width: 40px; height: 40px;
                   padding: 0; background: none; border: 0; cursor: pointer; color: var(--muted); }}
.controls button:hover {{ color: var(--text); }}
.controls button:focus-visible {{ outline: 1px solid var(--muted); }}

/* The large view: one screenshot, whole, over the dimmed page, with the same caption and arrows. */
.lightbox {{ width: 100vw; height: 100vh; max-width: none; max-height: none; margin: 0; padding: 32px; border: 0;
            background: transparent; color: var(--text); }}
.lightbox[open] {{ display: flex; flex-direction: column; align-items: center; justify-content: center; }}
.lightbox::backdrop {{ background: rgba(6, 7, 9, 0.88); backdrop-filter: blur(2px); }}
.lightbox img {{ display: block; max-width: min(1600px, 100%); max-height: calc(100vh - 140px); width: auto; height: auto;
                border: 1px solid var(--line); box-shadow: 0 24px 80px rgba(0, 0, 0, 0.6); cursor: zoom-out; }}
.lightbox .frame {{ display: flex; flex-direction: column; max-width: 100%; }}
.lightbox:focus {{ outline: none; }}
.lightbox .bar {{ display: flex; align-items: center; gap: 16px; margin-top: 12px; }}
.lightbox .bar p {{ flex: 1; margin: 0; font: 0.8rem/40px var(--mono); color: var(--muted); }}
.lightbox .controls {{ position: static; }}

h1 {{ font-size: clamp(2.6rem, 5vw, 3.8rem); line-height: 1; margin: 0 0 28px; white-space: nowrap;
      display: flex; align-items: center; gap: 0.3em; }}
h1 img {{ width: 1.05em; height: 1.05em; filter: drop-shadow(0 0 24px rgba(92, 224, 150, 0.25)); }}
.lede {{ font-size: clamp(1.2rem, 2.4vw, 1.5rem); line-height: 1.45; color: var(--text); max-width: 32em;
        margin: 0 0 16px; }}
.heritage {{ color: var(--muted); max-width: 40em; margin: 0 0 36px; }}
.actions {{ display: flex; flex-wrap: wrap; gap: 12px; }}
.btn {{ display: inline-block; padding: 12px 20px; text-decoration: none; font-weight: 600; font-size: 0.95rem;
       border: 1px solid var(--line); color: var(--text); background: var(--raise); border-radius: 2px; }}
.btn:hover {{ border-color: var(--muted); }}
.btn.primary {{ background: var(--accent); border-color: var(--accent); color: var(--on-accent); }}
.btn.primary:hover {{ filter: brightness(1.08); }}
.note {{ margin: 16px 0 0; font: 0.8rem var(--mono); color: var(--muted); letter-spacing: 0.02em; }}
.video {{ position: relative; aspect-ratio: 16 / 9; background: #000; border: 1px solid var(--line); }}
.video::after {{ content: ""; position: absolute; inset: -6px; border: 1px solid var(--grid); pointer-events: none; }}
.video iframe {{ position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }}

section {{ padding: 72px 0; border-bottom: 1px solid var(--line); }}
h2 {{ font-size: clamp(1.8rem, 4vw, 2.4rem); line-height: 1.1; margin: 0 0 40px; }}
.sub {{ margin: -28px 0 40px; color: var(--muted); }}

/* One panel cut into four cells by hairlines (the grid's 1 px gaps show the line colour). */
.personas {{ display: grid; gap: 1px; background: var(--line); border: 1px solid var(--line);
            grid-template-columns: repeat(auto-fit, minmax(min(100%, 240px), 1fr)); }}
.persona {{ background: var(--bg); padding: 28px 28px 32px; display: flex; flex-direction: column;
           transition: background 0.2s; }}
.persona:hover {{ background: var(--raise); }}
.persona .tile {{ display: block; margin-bottom: 40px; }}
.persona .icon {{ width: 26px; height: 26px; }}
.persona h3 {{ font-size: 1.15rem; margin: 0 0 8px; }}
.persona p {{ margin: 0; color: var(--muted); font-size: 0.95rem; }}
.persona.soon .icon {{ color: var(--muted); }}
.persona .badge {{ margin-top: auto; padding-top: 16px; color: var(--muted); font: 500 0.7rem var(--mono); text-transform: uppercase; letter-spacing: 0.08em; }}

.bucket {{ display: grid; gap: 16px 48px; padding: 40px 0; border-top: 1px solid var(--line); }}
.bucket:last-child {{ padding-bottom: 0; }}
@media (min-width: 860px) {{ .bucket {{ grid-template-columns: 200px 1fr; }}
  .rail {{ position: sticky; top: 24px; align-self: start; }} }}
.rail h3 {{ font-size: 2rem; line-height: 1; margin: 0 0 10px; }}
.rail p {{ margin: 0; color: var(--muted); font-size: 0.9rem; max-width: 24em; }}
.items {{ list-style: none; margin: 0; padding: 0; display: grid; gap: 32px 40px;
         grid-template-columns: repeat(auto-fill, minmax(min(100%, 300px), 1fr)); }}
.item {{ border-top: 1px solid var(--line); padding-top: 14px; scroll-margin-top: 24px; }}
.item:target {{ border-top-color: var(--accent); }}
.rid {{ color: var(--muted); text-decoration: none; }}
.rid:hover {{ color: var(--text); text-decoration: underline; }}
.item h4 {{ font-size: 1rem; font-weight: 600; margin: 0 0 6px; }}
.item p {{ margin: 0; font-size: 0.9rem; color: var(--muted); }}
.item .who {{ margin-top: 10px; font: 0.72rem var(--mono); text-transform: uppercase; letter-spacing: 0.06em; }}
.empty {{ margin: 0; color: var(--muted); font: 0.85rem var(--mono); align-self: center; }}

.ways {{ display: grid; gap: 32px 40px; grid-template-columns: repeat(auto-fit, minmax(min(100%, 260px), 1fr)); }}
.way {{ border-top: 1px solid var(--line); padding-top: 14px; }}
.way h3 {{ font: 500 0.75rem var(--mono); text-transform: uppercase; letter-spacing: 0.1em; color: var(--accent);
          margin: 0 0 6px; display: flex; align-items: center; gap: 10px; }}
.way .icon {{ width: 20px; height: 20px; }}
.way p {{ margin: 0; color: var(--muted); }}

footer {{ padding: 32px 0 56px; font: 0.8rem var(--mono); color: var(--muted); }}
footer a {{ color: var(--muted); margin-right: 20px; }}
footer a:hover {{ color: var(--text); }}
</style>
</head>
<body>
<header>
  <div class="wrap hero">
    <div>
    <h1><img src="assets/app-256.png" alt="">StarDraft 2</h1>
    <p class="lede">A StarCraft creation suite for map and mod creators. Built by the community, for the
      community.</p>
    <p class="heritage">StarDraft 2 aims to be the spiritual successor to StarDraft, the 1998 editor by Camelot
      Systems that powered the StarCraft modding community of the early 2000s.</p>
    <div class="actions">
      <a class="btn primary" href="{repo}/releases/latest">Download for Windows</a>
      <a class="btn" href="{discord}">Join the Discord</a>
    </div>
    <p class="note">Mac and Linux builds coming soon</p>
    </div>
    <div class="carousel">
      <div class="slides" id="slides" tabindex="0" aria-label="Screenshots">
        <figure class="slide" aria-label="1 of 10">
          <img class="shot" src="assets/shots/map.webp" width="1890" height="1260"
            alt="The Map workspace in HD graphics: the jungle map Crescent Dyne">
          <figcaption>Map editing in HD, SD or Carbot graphics &middot; <a href="https://scmscx.com/map/hksRyYbr">Crescent Dyne</a></figcaption>
        </figure>
        <figure class="slide" aria-label="2 of 10">
          <img class="shot" src="assets/shots/quick.webp" width="1890" height="1260" loading="lazy"
            alt="The quick picker over the map Cave Wars: protoss typed, and the matching units">
          <figcaption>Quick search: press Q to find any unit or tile &middot; <a href="https://scmscx.com/map/3pV5r4BV">Cave Wars</a></figcaption>
        </figure>
        <figure class="slide" aria-label="3 of 10">
          <img class="shot" src="assets/shots/generate.webp" width="1890" height="1260" loading="lazy"
            alt="The new map page: a generated jungle map with sliders for each terrain and the shape">
          <figcaption>Terrain generation from a tileset, amounts and shape</figcaption>
        </figure>
        <figure class="slide" aria-label="4 of 10">
          <img class="shot" src="assets/shots/triggers.webp" width="1890" height="1260" loading="lazy"
            alt="The Triggers workspace: a hero power on the Q key as blocks, and its epScript">
          <figcaption>EUD triggers as blocks, with the epScript beside them</figcaption>
        </figure>
        <figure class="slide" aria-label="5 of 10">
          <img class="shot" src="assets/shots/data.webp" width="1890" height="1260" loading="lazy"
            alt="The Data workspace: Jim Raynor with changed hit points and armor, and his weapon ranges">
          <figcaption>Unit editing with a live range preview</figcaption>
        </figure>
        <figure class="slide" aria-label="6 of 10">
          <img class="shot" src="assets/shots/weapons.webp" width="1890" height="1260" loading="lazy"
            alt="A weapon in the Data workspace: its damage against each unit size and upgrade, and its range">
          <figcaption>Weapon editing: damage for each size, upgrades and range</figcaption>
        </figure>
        <figure class="slide" aria-label="7 of 10">
          <img class="shot" src="assets/shots/buttons.webp" width="1890" height="1260" loading="lazy"
            alt="Infested Kerrigan's button set: its buttons, their command card and the selected button's fields">
          <figcaption>Button set editing with a live command card</figcaption>
        </figure>
        <figure class="slide" aria-label="8 of 10">
          <img class="shot" src="assets/shots/images.webp" width="1890" height="1260" loading="lazy"
            alt="The image player: the marine's animations, played frame by frame">
          <figcaption>iScript player for every unit animation</figcaption>
        </figure>
        <figure class="slide" aria-label="9 of 10">
          <img class="shot" src="assets/shots/briefing.webp" width="1890" height="1260" loading="lazy"
            alt="The Briefing workspace of Underdogs: Kerrigan and Raynor talk in the preview, beside the briefing actions">
          <figcaption>Briefing editing with a timeline and preview &middot; <a href="https://staredit.net/topic/18491/">Underdogs</a></figcaption>
        </figure>
        <figure class="slide" aria-label="10 of 10">
          <img class="shot" src="assets/shots/korean.webp" width="1890" height="1260" loading="lazy"
            alt="Program options in Korean, over the Triggers workspace in Korean">
          <figcaption>Multi-language support</figcaption>
        </figure>
      </div>
      <div class="controls">
        <button class="arrow" data-step="-1" aria-label="Previous screenshot"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M10 3L5 8L10 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
        <button class="arrow" data-step="1" aria-label="Next screenshot"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M6 3L11 8L6 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg><svg class="ring" viewBox="0 0 28 28" aria-hidden="true"><circle cx="14" cy="14" r="13.5"/></svg></button>
      </div>
    </div>
  </div>
</header>

<section>
  <div class="wrap">
    <h2>Who it is for</h2>
    <div class="personas">
{personas}
    </div>
  </div>
</section>

<section id="video">
  <div class="wrap">
    <div class="video">
      <iframe src="https://www.youtube-nocookie.com/embed/{video}" title="StarDraft 2 video" loading="lazy"
        referrerpolicy="strict-origin-when-cross-origin"
        allow="accelerometer; encrypted-media; gyroscope; picture-in-picture; fullscreen" allowfullscreen></iframe>
    </div>
  </div>
</section>

<section id="roadmap">
  <div class="wrap">
    <h2>Roadmap</h2>
    <p class="sub">Sourced from <a href="{repo}/issues">user reports</a> and <a href="{feedback}">feedback</a>.</p>
    <div class="buckets">
{buckets}
    </div>
  </div>
</section>

<section id="feedback">
  <div class="wrap">
    <h2>Feedback</h2>
    <div class="ways">
      <div class="way"><h3>{i_select}In StarDraft 2</h3><p>Right-click the part that is wrong and select
        <em>Report feedback on &hellip;</em></p></div>
      <div class="way"><h3>{i_feedback}On Discord</h3><p>Post in the <a href="{feedback}">#feedback</a> channel.</p></div>
      <div class="way"><h3>{i_issue}On GitHub</h3><p><a href="{repo}/issues">Open an issue</a>.</p></div>
    </div>
  </div>
</section>

<footer>
  <div class="wrap">
    <a href="{repo}">GitHub</a><a href="{repo}/blob/main/docs/user/CHANGELOG.md">Changelog</a><a href="{discord}">Discord</a><a href="{repo}/issues">Report a problem</a>
  </div>
</footer>
<dialog class="lightbox" id="lightbox" aria-label="Screenshot" tabindex="-1">
  <div class="frame">
  <img id="lb-img" src="" alt="">
  <div class="bar">
    <p id="lb-cap"></p>
    <div class="controls">
      <button class="arrow" data-step="-1" aria-label="Previous screenshot"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M10 3L5 8L10 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
      <button class="arrow" data-step="1" aria-label="Next screenshot"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M6 3L11 8L6 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
      <button class="close" aria-label="Close"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M4 4L12 12M12 4L4 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg></button>
    </div>
  </div>
  </div>
</dialog>
<script>
// The screenshots carousel: the arrows (and the arrow keys) scroll the slides, from the last to the first again.
// It plays by itself until the viewer uses it (an arrow, a key, a swipe or a scroll): a faint ring around the next
// arrow fills up, and when it closes the next slide shows. The ring stops while the mouse is over the carousel. With
// reduced motion, the slides change at once instead of sliding.
(() => {{
  const slides = document.getElementById("slides"), n = slides.children.length;
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const at = () => Math.round(slides.scrollLeft / slides.clientWidth);
  const go = i => slides.scrollTo({{ left: ((i + n) % n) * slides.clientWidth, behavior: still ? "instant" : "smooth" }});
  const ring = document.querySelector(".carousel .ring");
  let playing = true;
  const run = () => {{
    ring.classList.remove("run");
    void ring.getBoundingClientRect();  // starts the animation again
    if (playing) ring.classList.add("run");
  }};
  ring.addEventListener("animationend", () => {{
    if (!playing) return;
    go(at() + 1);
    run();
  }});
  const stop = () => {{ playing = false; ring.classList.remove("run"); }};
  run();
  document.querySelectorAll(".carousel .controls .arrow").forEach(a => a.addEventListener("click", () => {{
    stop();
    go(at() + +a.dataset.step);
  }}));
  slides.addEventListener("keydown", e => {{
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") stop(), e.preventDefault(), go(at() + (e.key === "ArrowLeft" ? -1 : 1));
  }});
  for (const ev of ["pointerdown", "wheel", "touchstart"]) slides.addEventListener(ev, stop, {{ passive: true }});

  // A click on a screenshot opens the large view; its arrows and keys go through the same slides. Esc, the close
  // button, a click beside the picture or on it closes it, at the slide it showed last.
  const box = document.getElementById("lightbox"), img = document.getElementById("lb-img");
  const cap = document.getElementById("lb-cap");
  let cur = 0;
  const show = i => {{
    cur = (i + n) % n;
    const s = slides.children[cur], pic = s.querySelector("img");
    img.src = pic.src, img.alt = pic.alt, cap.innerHTML = s.querySelector("figcaption").innerHTML;
  }};
  [...slides.children].forEach((s, i) => s.querySelector("img").addEventListener("click", () => {{
    stop();
    show(i);
    box.showModal();
    box.focus();
  }}));
  box.querySelectorAll(".arrow").forEach(a => a.addEventListener("click", () => show(cur + +a.dataset.step)));
  box.querySelector(".close").addEventListener("click", () => box.close());
  box.addEventListener("click", e => {{ if (e.target === box || e.target === img) box.close(); }});
  box.addEventListener("keydown", e => {{
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") e.preventDefault(), show(cur + (e.key === "ArrowLeft" ? -1 : 1));
  }});
  box.addEventListener("close", () => slides.scrollTo({{ left: cur * slides.clientWidth, behavior: "instant" }}));
}})();
</script>
</body>
</html>
"""

if __name__ == "__main__":
    page = build(*parse(ROADMAP.read_text(encoding="utf-8")))
    assets = {OUT.parent / "assets" / f"app-{n}.png": ICONS / "app" / "png" / f"app-{n}.png" for n in (32, 256)}
    if "--check" in sys.argv:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != page or any(not d.exists() or d.read_bytes() != s.read_bytes() for d, s in assets.items()):
            sys.exit("docs/index.html is out of date: run python tools/build_site.py")
    else:
        for dst, src in assets.items():
            dst.parent.mkdir(exist_ok=True)
            shutil.copyfile(src, dst)
        OUT.write_text(page, encoding="utf-8", newline="\n")
