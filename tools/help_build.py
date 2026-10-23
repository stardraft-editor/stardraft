#!/usr/bin/env python3
"""Builds the help pages (docs/help/*.md) for the program and for the web, and checks them.

usage: python3 -I tools/help_build.py [--check] [--cpp <file>] [--site <folder>]

  --check          only the checks (the default when no output is given)
  --cpp <file>     the C++ source with the pages for the program (include/stardraft/help.h, help_data); the build
                   (CMakeLists.txt) makes it. The file is written only when its text changes.
  --site <folder>  the pages for GitHub Pages: <folder>/<page>.html with the list of pages and a search, and
                   pages.json (the pages for the program, which loads them from the web)
                   (.github/workflows/pages.yml publishes the folder as /help/)

The checks (a problem: exit 1 and no output):
  - each link to a page (name.md) goes to a page that is there, and each link to a place (#id) to a heading that is
    there;
  - the index (index.md) links to each page;
  - each page other than the index has the heading "Sources and credits";
  - each field of units.dat, weapons.dat, flingy.dat, sprites.dat, images.dat, upgrades.dat, techdata.dat,
    orders.dat, sfxdata.dat and portdata.dat (src/data/dat_schema.inc) has a heading on its page (<dat>-dat.md), with
    the id that the program opens on F1 (include/stardraft/help.h: helpFieldTopic).

The Markdown: headings (#, ##, ###, with "{#id}" at the end for an id of your own; else the id is made from the text),
paragraphs, lists (- and 1.), tables (| a | b |), code blocks (```), > quotes, ---, and in a line `code`, **bold**,
*italic* and [links](page.md#id). Only the standard library: no packages to install.
"""

import argparse
import html
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HELP = os.path.join(ROOT, "docs", "help")
SCHEMA = os.path.join(ROOT, "src", "data", "dat_schema.inc")
ONLINE = "https://stardraft-editor.github.io/stardraft/help/"
REPO = "https://github.com/stardraft-editor/stardraft"
DATS = ["units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders", "sfxdata", "portdata"]


def anchor(text):
    """include/stardraft/help.h: helpAnchor."""
    return "-".join(re.findall(r"[A-Za-z0-9]+", text)).lower()


# --- Markdown -------------------------------------------------------------------------------------------------------

INLINE = re.compile(r"`([^`]+)`|\*\*(.+?)\*\*|\*(.+?)\*|\[([^\]]+)\]\(([^)\s]+)\)|<(https?://[^>\s]+)>")


def inline(text, links):
    """A line's Markdown as HTML; each link's target goes into `links`."""
    out, pos = [], 0
    for m in INLINE.finditer(text):
        out.append(html.escape(text[pos:m.start()], quote=False))
        code, bold, italic, label, href, auto = m.groups()
        if code is not None:
            out.append("<code>" + html.escape(code, quote=False) + "</code>")
        elif bold is not None:
            out.append("<strong>" + inline(bold, links) + "</strong>")
        elif italic is not None:
            out.append("<em>" + inline(italic, links) + "</em>")
        elif label is not None:
            links.append(href)
            out.append('<a href="%s">%s</a>' % (html.escape(page_href(href)), inline(label, links)))
        else:
            links.append(auto)
            out.append('<a href="%s">%s</a>' % (html.escape(auto), html.escape(auto, quote=False)))
        pos = m.end()
    out.append(html.escape(text[pos:], quote=False))
    return "".join(out)


def page_href(href):
    """A link to a page (name.md#id) goes to its HTML page (name.html#id)."""
    m = re.match(r"^([A-Za-z0-9_-]+)\.md(#.*)?$", href)
    return m.group(1) + ".html" + (m.group(2) or "") if m else href


def plain(fragment):
    """The text of an HTML fragment (for the search)."""
    return html.unescape(re.sub(r"<[^>]+>", "", fragment))


class Page:
    def __init__(self, name, path):
        self.name, self.path = name, path
        self.title = ""
        self.html = ""
        self.sections = []  # [id, title, level, [text parts]]
        self.ids = set()
        self.links = []  # (line, href)

    def problem(self, line, text):
        return "%s:%d: %s" % (os.path.relpath(self.path, ROOT).replace("\\", "/"), line, text)


HEADING = re.compile(r"^(#{1,3})\s+(.*?)(?:\s+\{#([A-Za-z0-9_-]+)\})?\s*$")
LIST_ITEM = re.compile(r"^(\s*)([-*]|\d+\.)\s+(.*)$")


def convert(page, lines, problems):
    out = []
    section = None

    def add_text(fragment):
        if section is not None:
            section[3].append(plain(fragment))
        out.append(fragment)

    i = 0
    while i < len(lines):
        line = lines[i]
        n = i + 1
        stripped = line.strip()
        if not stripped:
            i += 1
            continue
        if stripped.startswith("```"):
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            add_text("<pre><code>" + html.escape("\n".join(code), quote=False) + "</code></pre>\n")
            continue
        m = HEADING.match(line)
        if m:
            level, text, own = len(m.group(1)), m.group(2), m.group(3)
            hid = own or anchor(text)
            if hid in page.ids:
                problems.append(page.problem(n, 'two headings have the id "%s"' % hid))
            page.ids.add(hid)
            links = []
            body = inline(text, links)
            page.links += [(n, h) for h in links]
            if level == 1 and not page.title:
                page.title = plain(body)
            section = [hid, plain(body), level, []]
            page.sections.append(section)
            out.append('<h%d id="%s">%s</h%d>\n' % (level, hid, body, level))
            i += 1
            continue
        if re.match(r"^-{3,}$", stripped):
            out.append("<hr>\n")
            i += 1
            continue
        if stripped.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append((i + 1, [c.strip() for c in lines[i].strip().strip("|").split("|")]))
                i += 1
            t = ["<table>\n"]
            for k, (rn, cells) in enumerate(rows):
                if k == 1 and all(re.match(r"^:?-+:?$", c) for c in cells):
                    continue
                tag = "th" if k == 0 and len(rows) > 1 and all(re.match(r"^:?-+:?$", c) for c in rows[1][1]) else "td"
                links = []
                t.append("<tr>" + "".join("<%s>%s</%s>" % (tag, inline(c, links), tag) for c in cells) + "</tr>\n")
                page.links += [(rn, h) for h in links]
            t.append("</table>\n")
            add_text("".join(t))
            continue
        if stripped.startswith(">"):
            quote = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                quote.append((i + 1, lines[i].strip()[1:].strip()))
                i += 1
            links = []
            body = " ".join(inline(q, links) for _, q in quote)
            page.links += [(quote[0][0], h) for h in links]
            add_text("<blockquote><p>" + body + "</p></blockquote>\n")
            continue
        m = LIST_ITEM.match(line)
        if m:
            ordered = m.group(2)[0].isdigit()
            items = []
            while i < len(lines):
                m = LIST_ITEM.match(lines[i])
                if m and (m.group(2)[0].isdigit()) == ordered:
                    items.append((i + 1, [m.group(3).strip()]))
                elif lines[i].startswith("  ") and lines[i].strip() and items:
                    items[-1][1].append(lines[i].strip())  # (a line that goes on with the item)
                else:
                    break
                i += 1
            tag = "ol" if ordered else "ul"
            parts = ["<%s>\n" % tag]
            for ln, text in items:
                links = []
                parts.append("<li>" + inline(" ".join(text), links) + "</li>\n")
                page.links += [(ln, h) for h in links]
            parts.append("</%s>\n" % tag)
            add_text("".join(parts))
            continue
        para = []
        start = n
        while i < len(lines):
            s = lines[i].strip()
            if not s or HEADING.match(lines[i]) or s.startswith(("```", "|", ">")) or LIST_ITEM.match(lines[i]) or re.match(r"^-{3,}$", s):
                break
            para.append(s)
            i += 1
        links = []
        add_text("<p>" + inline(" ".join(para), links) + "</p>\n")
        page.links += [(start, h) for h in links]
    page.html = "".join(out)


def read_pages(problems):
    pages = {}
    for f in sorted(os.listdir(HELP)):
        if not f.endswith(".md"):
            continue
        name = f[:-3]
        if not re.match(r"^[a-z0-9-]+$", name):
            problems.append("docs/help/%s: a page's file name has only a-z, 0-9 and -" % f)
            continue
        p = Page(name, os.path.join(HELP, f))
        with open(p.path, encoding="utf-8") as fh:
            convert(p, fh.read().splitlines(), problems)
        if not p.title:
            problems.append(p.problem(1, "the page has no title (a # heading)"))
        pages[name] = p
    return pages


def check(pages, problems):
    if "index" not in pages:
        problems.append("docs/help/index.md: there is no index")
        return []
    for p in pages.values():
        for n, href in p.links:
            if re.match(r"^[a-z]+:", href):
                continue  # (a place outside: not checked here)
            m = re.match(r"^(?:([A-Za-z0-9_-]+)\.md)?(?:#([A-Za-z0-9_-]+))?$", href)
            if not m or not (m.group(1) or m.group(2)):
                problems.append(p.problem(n, 'the link "%s" is not to a page (name.md), a place (#id) or a web address' % href))
                continue
            target = pages.get(m.group(1)) if m.group(1) else p
            if target is None:
                problems.append(p.problem(n, 'the link "%s" goes to a page that is not there' % href))
            elif m.group(2) and m.group(2) not in target.ids:
                problems.append(p.problem(n, 'the link "%s" goes to a place that is not on %s.md' % (href, target.name)))
        if p.name != "index" and "sources-and-credits" not in p.ids:
            problems.append(p.problem(1, 'the page has no heading "Sources and credits"'))
    order = ["index"]
    for _, href in pages["index"].links:
        m = re.match(r"^([A-Za-z0-9_-]+)\.md", href)
        if m and m.group(1) in pages and m.group(1) not in order:
            order.append(m.group(1))
    for name in sorted(pages):
        if name not in order:
            problems.append("docs/help/%s.md: the index (index.md) does not link to it" % name)
    # The .dat fields: each one's heading, with the id of helpFieldTopic.
    with open(SCHEMA, encoding="utf-8") as fh:
        fields = re.findall(r'^\s*\{"([a-z]+)", \d+, "([^"]*)"', fh.read(), re.M)
    for dat in DATS:
        p = pages.get(dat + "-dat")
        if p is None:
            problems.append("docs/help/%s-dat.md: there is no page for %s.dat" % (dat, dat))
            continue
        for d, field in fields:
            hid = anchor(field.replace("Resarch", "Research"))
            if d == dat and hid not in p.ids:
                problems.append(p.problem(1, 'the field "%s" has no heading with the id "%s"' % (field, hid)))
    return order


# --- The program's pages (C++) --------------------------------------------------------------------------------------

def c_literals(text, size=1500):
    """A string as C string literals (each at most `size` bytes)."""
    data = text.encode("utf-8")
    parts = []
    for k in range(0, len(data), size):
        chunk = data[k:k + size]
        s = []
        for b in chunk:
            c = chr(b)
            if c == "\\":
                s.append("\\\\")
            elif c == '"':
                s.append('\\"')
            elif c == "\n":
                s.append("\\n")
            elif c == "?":
                s.append("\\?")  # (no trigraphs)
            elif 0x20 <= b < 0x7F:
                s.append(c)
            else:
                s.append("\\%03o" % b)
        parts.append('"' + "".join(s) + '"')
    return parts or ['""']


def c_parts(text):
    """A string as a list of C string literals, ending with nullptr (help_data's parts)."""
    return "{" + ", ".join(c_literals(text) + ["nullptr"]) + "}"


def c_str(text):
    """A short string as one C string (its literals side by side)."""
    return " ".join(c_literals(text))


def pages_stamp():
    """The pages' time: the last commit that changed docs/help (else the last commit; 0 without git), in Unix seconds.
    The program's pages and the web's pages.json both carry it: the program takes the web's only when they are as new."""
    here = os.path.dirname(os.path.abspath(__file__))
    for args in (["log", "-1", "--format=%ct", "--", "docs/help"], ["log", "-1", "--format=%ct"]):
        try:
            out = subprocess.run(["git", "-C", os.path.join(here, ".."), *args], capture_output=True, text=True, timeout=20).stdout.strip()
            if out.isdigit():
                return int(out)
        except (OSError, subprocess.SubprocessError):
            pass
    return 0


# Under each page's title: the pages are new, and the readers' reports make them better.
WIP_NOTE = ('<blockquote><p>These help pages are a work in progress and will get better. When something is wrong, missing '
            'or not clear, tell us: use Send feedback about this page (or right-click the page and select Report feedback), '
            'or <a href="' + REPO + '/issues">report an issue on GitHub</a>.</p></blockquote>\n')


def write_cpp(pages, order, path):
    out = ["// Generated by tools/help_build.py from docs/help/*.md. Do not edit.\n", '#include "stardraft/help.h"\n\n',
           "namespace stardraft::help_data {\n\n"]
    for k, name in enumerate(order):
        p = pages[name]
        out.append("static const char* const kHtml%d[] = %s;\n" % (k, c_parts(p.html)))
        for j, s in enumerate(p.sections):
            out.append("static const char* const kText%d_%d[] = %s;\n" % (k, j, c_parts(" ".join(" ".join(s[3]).split()))))
        out.append("static const Section kSections%d[] = {\n" % k)
        for j, s in enumerate(p.sections):
            out.append("    {%s, %s, %d, kText%d_%d},\n" % (c_str(s[0]), c_str(s[1]), s[2], k, j))
        out.append("};\n\n")
    out.append("const Page kPages[] = {\n")
    for k, name in enumerate(order):
        p = pages[name]
        out.append("    {%s, %s, kHtml%d, kSections%d, %d},\n" % (c_str(name), c_str(p.title), k, k, len(p.sections)))
    out.append("};\nconst int kPageCount = %d;\nconst long long kPagesStamp = %d;\n\n}  // namespace stardraft::help_data\n" % (len(order), pages_stamp()))
    text = "".join(out)
    old = None
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            old = fh.read()
    if old != text:
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)


# --- The web pages --------------------------------------------------------------------------------------------------

CSS = """
:root { --bg: #ffffff; --text: #1f2328; --dim: #59636e; --line: #d1d9e0; --side: #f6f8fa; --accent: #0969da; --code: #eff1f3; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #0d1117; --text: #e6edf3; --dim: #9198a1; --line: #3d444d; --side: #151b23; --accent: #4493f8; --code: #262c36; }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 16px/1.55 -apple-system, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
.layout { display: flex; min-height: 100vh; }
nav { width: 270px; flex: none; background: var(--side); border-right: 1px solid var(--line); padding: 20px 16px; }
nav .home { font-weight: 600; font-size: 18px; color: var(--text); display: block; margin-bottom: 12px; }
nav input { width: 100%; padding: 6px 10px; border: 1px solid var(--line); border-radius: 6px; background: var(--bg); color: var(--text); font: inherit; }
nav ul { list-style: none; padding: 0; margin: 14px 0 0; }
nav li a { display: block; padding: 4px 8px; border-radius: 6px; color: var(--text); }
nav li a.here { background: var(--line); font-weight: 600; }
nav li .in { display: block; padding: 0 8px 4px; color: var(--dim); font-size: 13px; }
main { flex: 1; min-width: 0; max-width: 900px; padding: 24px 40px 60px; }
h1 { font-size: 32px; margin: 0 0 16px; padding-bottom: 8px; border-bottom: 1px solid var(--line); }
h2 { font-size: 24px; margin: 32px 0 12px; padding-bottom: 6px; border-bottom: 1px solid var(--line); }
h3 { font-size: 19px; margin: 24px 0 8px; }
code { background: var(--code); padding: 2px 5px; border-radius: 4px; font-size: 90%; }
pre { background: var(--code); padding: 12px; border-radius: 6px; overflow-x: auto; }
pre code { padding: 0; background: none; }
table { border-collapse: collapse; margin: 12px 0; display: block; overflow-x: auto; }
th, td { border: 1px solid var(--line); padding: 6px 12px; text-align: left; vertical-align: top; }
blockquote { margin: 12px 0; padding: 0 14px; color: var(--dim); border-left: 4px solid var(--line); }
footer { margin-top: 40px; padding-top: 12px; border-top: 1px solid var(--line); color: var(--dim); font-size: 14px; }
@media (max-width: 760px) {
  .layout { display: block; }
  nav { width: auto; border-right: none; border-bottom: 1px solid var(--line); }
  main { padding: 16px; }
}
"""

SEARCH_JS = """
// On a Mac, the shortcuts in the page as the Mac shows them (as the program does: Ctrl+Shift+S is \u21e7\u2318S).
(function () {
  if (!/Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent)) return;
  var keys = {Backspace: "\u232b", Plus: "+", Minus: "-", Del: "\u2326", Tab: "\u21e5", Enter: "\u21a9", Esc: "\u238b",
              Home: "\u2196", End: "\u2198", PgUp: "\u21de", PgDn: "\u21df", Left: "\u2190", Right: "\u2192", Up: "\u2191",
              Down: "\u2193", Space: "Space"};
  var re = /(^|[^A-Za-z0-9])((?:(?:Ctrl|Cmd|Shift|Alt)\\+)+)(Backspace|Plus|Minus|Del|Tab|Enter|Esc|Home|End|PgUp|PgDn|Left|Right|Up|Down|Space|click|wheel|drag|arrows|F\\d+|[^\\s])/g;
  function mac(text) {
    return text.replace(re, function (all, before, mods, key) {
      var m = (/Alt\\+/.test(mods) ? "\u2325" : "") + (/Shift\\+/.test(mods) ? "\u21e7" : "") + (/(Ctrl|Cmd)\\+/.test(mods) ? "\u2318" : "");
      var k = keys[key] || (/^(click|wheel|drag|arrows)$/.test(key) ? "-" + key : key.length == 1 ? key.toUpperCase() : key);
      return before + m + k;
    });
  }
  var main = document.querySelector("main");
  if (!main) return;
  var walk = document.createTreeWalker(main, NodeFilter.SHOW_TEXT), n;
  while ((n = walk.nextNode())) if (/(Ctrl|Cmd|Shift|Alt)\\+/.test(n.nodeValue)) n.nodeValue = mac(n.nodeValue);
})();
(function () {
  var box = document.getElementById("search"), list = document.getElementById("pages"), pages = list.innerHTML;
  var index = null;
  function words(q) { return (q.toLowerCase().match(/[a-z0-9\\u0080-\\uffff]+/g) || []); }
  function has(text, ws) { for (var i = 0; i < ws.length; i++) if (text.indexOf(ws[i]) < 0) return false; return true; }
  function show() {
    var ws = words(box.value);
    if (!ws.length || !index) { list.innerHTML = pages; return; }
    var first = [], then = [];
    index.forEach(function (p) {
      p.sections.forEach(function (s) {
        var title = s.title.toLowerCase(), inTitle = has(title, ws) || (s.level == 1 && has(p.title.toLowerCase(), ws));
        if (!inTitle && !has(title + " " + s.text.toLowerCase() + " " + p.title.toLowerCase(), ws)) return;
        var href = p.name + ".html" + (s.level == 1 ? "" : "#" + s.id);
        (inTitle ? first : then).push('<li><a href="' + href + '">' + s.title.replace(/</g, "&lt;") + '</a><span class="in">' +
                                      p.title.replace(/</g, "&lt;") + "</span></li>");
      });
    });
    var all = first.concat(then).slice(0, 50);
    list.innerHTML = all.length ? all.join("") : '<li><span class="in">No pages have these words</span></li>';
  }
  box.addEventListener("input", function () {
    if (index) return show();
    fetch("search.json").then(function (r) { return r.json(); }).then(function (j) { index = j; show(); });
  });
})();
"""


def write_site(pages, order, folder):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "style.css"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(CSS.lstrip())
    with open(os.path.join(folder, "search.js"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(SEARCH_JS.lstrip())
    index = [{"name": n, "title": pages[n].title,
              "sections": [{"id": s[0], "title": s[1], "level": s[2], "text": " ".join(" ".join(s[3]).split())} for s in pages[n].sections]}
             for n in order]
    with open(os.path.join(folder, "search.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(index, fh, ensure_ascii=False, separators=(",", ":"))
    # The pages for the program (pages.json): it loads them from here, so that its help is the web's (the pages built
    # into it are for when it has no network). Format 1: the pages in their order (the index first), each with its body.
    bundle = {"format": 1, "stamp": pages_stamp(), "pages": [dict(entry, html=pages[entry["name"]].html) for entry in index]}
    with open(os.path.join(folder, "pages.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(bundle, fh, ensure_ascii=False, separators=(",", ":"))
    for name in order:
        p = pages[name]
        nav = "".join('<li><a href="%s.html"%s>%s</a></li>' % (n, ' class="here"' if n == name else "", html.escape(pages[n].title))
                      for n in order if n != "index")
        issue = REPO + "/issues/new?title=" + re.sub(r"[^A-Za-z0-9._~-]", lambda m: "%%%02X" % ord(m.group(0)) if ord(m.group(0)) < 128 else
                                                     "".join("%%%02X" % b for b in m.group(0).encode("utf-8")), "Help page: " + p.title)
        page = ("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                "<title>%s - StarDraft 2 help</title>\n<link rel=\"stylesheet\" href=\"style.css\">\n</head>\n<body>\n"
                "<div class=\"layout\">\n<nav>\n<a class=\"home\" href=\"index.html\">StarDraft 2 help</a>\n"
                "<input id=\"search\" type=\"search\" placeholder=\"Search the help\" aria-label=\"Search the help\">\n"
                "<ul id=\"pages\">%s</ul>\n</nav>\n<main>\n%s<footer><a href=\"%s\">Send feedback about this page</a> &middot; "
                "<a href=\"%s\">StarDraft 2 on GitHub</a></footer>\n</main>\n</div>\n<script src=\"search.js\"></script>\n</body>\n</html>\n"
                % (html.escape(p.title), nav, p.html, html.escape(issue), REPO))
        with open(os.path.join(folder, name + ".html"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(page)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="only the checks")
    ap.add_argument("--cpp", help="the C++ source for the program")
    ap.add_argument("--site", help="the folder for the web pages")
    a = ap.parse_args()
    problems = []
    pages = read_pages(problems)
    order = check(pages, problems)
    if problems:
        for p in problems:
            print("help: " + p, file=sys.stderr)
        print("help: %d problems" % len(problems), file=sys.stderr)
        return 1
    for p in pages.values():  # (the note under each page's title: in the program and on the web)
        p.html = p.html.replace("</h1>\n", "</h1>\n" + WIP_NOTE, 1)
    if a.cpp:
        write_cpp(pages, order, a.cpp)
    if a.site:
        write_site(pages, order, a.site)
    print("help: %d pages, %d links, no problems" % (len(order), sum(len(p.links) for p in pages.values())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
