"""Check the UI text of StarDraft against the text rules in docs/dev/PRINCIPLES.md ("Text").

Usage: python -I tools/ui_text_lint.py [--repo DIR] [--baseline FILE] [--warnings] [--dump FILE]

The tool extracts the strings that users see (menus, buttons, labels, titles, status texts, checker
messages, tooltips and the trigger block texts) and runs the checks on them. A violation that is in
the baseline file (tools/ui_text_lint_baseline.txt) is an accepted exception. The exit code is 1 if a
violation is not in the baseline, else 0.

--warnings shows the heuristic checks too (passive voice, lists of three). They never change the exit code.
--dump writes all extracted strings to a JSON file.
"""
import argparse
import json
import re
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

# --- Extraction ----------------------------------------------------------------------------------

SKIP_FILES = {"apps/editor/trigger_selftest.inc", "apps/editor/kit_gallery.inc", "apps/editor/theme.inc"}
# In main.cpp, the self-test, datashot and screenshot drivers (not seen by users) are between these lines.
SKIP_MARKERS = {"apps/editor/main.cpp": ("// --- Self test ---", "int WINAPI wWinMain(")}
LOG_CALLS = {"logLine", "log", "buildLogAppend", "OutputDebugStringW", "OutputDebugStringA", "printf", "fprintf",
             "wprintf", "logSetExpanded", "trace", "dbg"}
NON_UI_CALLS = {"GetPropW", "SetPropW", "RemovePropW", "LoadLibraryW", "GetProcAddress", "RegisterClassExW",
                "FindWindowW", "CreateFontW", "RegOpenKeyExW", "RegQueryValueExW", "RegSetValueExW",
                "GetEnvironmentVariableW", "_wgetenv", "ShellExecuteW", "find", "rfind", "starts_with", "ends_with",
                "compare", "wcscmp", "_wcsicmp", "lstrcmpiW", "CreateFileW", "fopen", "_wfopen", "path",
                "SetWindowTheme", "OpenThemeData", "GetModuleHandleW", "CreateMutexW", "getenv", "replace", "contains",
                "count", "at", "jsonString", "jsonInt", "jsonBool", "get", "setting", "readSetting", "writeSetting",
                "settingInt", "settingString", "saveSetting", "loadSetting", "WritePrivateProfileStringW",
                "GetPrivateProfileStringW", "GetPrivateProfileIntW", "regex", "wregex", "HttpOpenRequestW",
                "InternetOpenW", "InternetConnectW", "HttpSendRequestW", "WinHttpOpen", "WinHttpConnect",
                "WinHttpOpenRequest", "WinHttpAddRequestHeaders", "darkControl", "reg", "putW", "RegGetValueW",
                "RegSetKeyValueW", "RegCreateKeyExW", "readRegList", "putList", "github", "SetWindowSubclass",
                "needsVerification"}
BUTTON_CALLS = {"buttonFace", "iconButton", "link", "sideLink", "check", "radioMark", "pushButton", "button",
                "rowActions", "addButton", "makeButton", "createButton", "smallButton", "buttonsRight", "glyphButton",
                "linkButton", "radio", "option", "chipMenu"}
TITLE_CALLS = {"title", "popupFrame", "setTitle", "dialogTitle", "openDialog", "makeDialog", "createDialog",
               "fileDialog"}


def classify(stack, text, line_src):
    for s in reversed(stack):
        n, a, snip = s["name"], s["arg"], s["snip"]
        if n in LOG_CALLS:
            return "log"
        if n in NON_UI_CALLS:
            return "nonui"
        if n in ("AppendMenuW", "InsertMenuW", "ModifyMenuW"):
            return "menu"
        if n in ("MessageBoxW", "MessageBoxExW", "TaskDialog", "themedMessageBox"):
            return "msgbox.title" if a == 2 else "button" if a >= 4 else "msgbox.text"
        if n == "setMessage":
            return "status"
        if n == "issue":
            return "checker"
        if n == "SendMessageW" and "EM_SETCUEBANNER" in snip:
            return "cue"
        if n == "SendMessageW" and ("TCM_INSERTITEM" in snip or "TCIF_TEXT" in snip):
            return "tab"
        if n == "SendMessageW" and re.search(r"CB_ADDSTRING|LB_ADDSTRING|CB_INSERTSTRING", snip):
            return "listitem"
        if n == "CreateWindowExW":
            if a != 2:
                return "nonui"
            if re.search(r"BUTTON|WC_BUTTON", snip):
                return "button"
            if "STATIC" in snip:
                return "label"
            return "window.title"
        if n == "SetWindowTextW":
            return "window.text"
        if n in ("sectionHeader", "sideHeader", "sideGroup", "formTitle", "heading"):
            return "section"
        if n == "showListPicker":
            return "picker"
        if n == "DrawTextW":
            return "label"
        if n in BUTTON_CALLS:
            return "button"
        if n in ("fieldLabel", "label", "rowText", "tableText", "fieldBox"):
            return "label"
        if n in ("tableCell", "column", "addColumn", "header", "tableHeader"):
            return "column"
        if n in TITLE_CALLS:
            return "title"
        if "tip" in n.lower():
            return "tooltip"
    if re.search(r"\btips?\b|\.tip\b|tooltip|Tooltip|tips\.push_back|lpszText", line_src):
        return "tooltip"
    if re.search(r"hint|Hint|help|Help|about\[", line_src) and len(text.split()) > 3:
        return "help"
    return "other"


ESC = {"n": "\n", "t": "\t", "r": "\r", "0": "", "\\": "\\", '"': '"', "'": "'"}


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            d = s[i + 1]
            if d in ESC:
                out.append(ESC[d]); i += 2; continue
            if d == "x":
                m = re.match(r"[0-9a-fA-F]+", s[i + 2:])
                if m:
                    out.append(chr(int(m.group(0), 16))); i += 2 + len(m.group(0)); continue
            if d == "u":
                out.append(chr(int(s[i + 2:i + 6], 16))); i += 6; continue
            out.append(d); i += 2; continue
        out.append(c); i += 1
    return "".join(out)


def lex(src, rel):
    """Literals with their call stack. A small C++ lexer: skips comments and preprocessor lines, joins
    adjacent literals, and keeps a stack of open calls with the argument index."""
    lines = src.split("\n")
    i, n, line = 0, len(src), 1
    stack, recs = [], []
    last_ident = ""
    pending = None

    def flush():
        nonlocal pending
        if pending:
            recs.append(pending)
            pending = None

    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; i += 1; continue
        if src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j; continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2); line += src.count("\n", i, j); i = j + 2; continue
        if c == "#" and src[src.rfind("\n", 0, i) + 1:i].strip() == "":
            j = src.find("\n", i)
            while j > 0 and src[j - 1] == "\\":
                j = src.find("\n", j + 1)
            if j < 0:
                j = n
            line += src.count("\n", i, j); i = j; continue
        m = re.match(r'(L|u8|u|U)?(R)?"', src[i:i + 4])
        if m and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            pre, raw = m.group(1) or "", bool(m.group(2))
            start_line = line
            i += len(m.group(0))
            if raw:
                d = src.find("(", i); delim = src[i:d]; end = src.find(")" + delim + '"', d)
                val = src[d + 1:end]; line += val.count("\n"); i = end + len(delim) + 2
            else:
                j = i
                while src[j] != '"':
                    j += 2 if src[j] == "\\" else 1
                val = unescape(src[i:j]); i = j + 1
            if pending and pending["_adj"]:
                pending["text"] += val
            else:
                flush()
                ctx = [dict(s, snip=src[s["pos"]:i]) for s in stack if s["kind"] == "("]
                pending = {"file": rel, "line": start_line, "wide": pre == "L", "text": val, "stack": ctx,
                           "callee": ctx[-1]["name"] if ctx else "", "src": lines[start_line - 1].strip()}
            k = i
            while k < n and src[k] in " \t\r\n":
                k += 1
            pending["_adj"] = bool(re.match(r'(L|u8)?"', src[k:k + 3]))
            if not pending["_adj"]:
                flush()
            continue
        if c == "'":
            j = i + 1
            while src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1; continue
        if c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*", src[i:])
            last_ident = m.group(0).split("::")[-1]; i += len(m.group(0)); continue
        if c in "([{":
            stack.append({"kind": c, "name": last_ident if c == "(" else "", "arg": 0, "pos": i})
        elif c in ")]}":
            if stack:
                stack.pop()
        elif c == "," and stack:
            stack[-1]["arg"] += 1
        if not c.isspace() and c not in "<>":
            last_ident = ""
        i += 1
    flush()
    return recs


def looks_ui(t):
    s = t.strip()
    if len(s) < 2 or not re.search(r"[A-Za-z]{2}", s):
        return False
    if s == "StarDraft":
        return True  # a window or message box title can be the program name alone (check "generic-title")
    if re.fullmatch(r"[\w.\-/\\:%*?<>|]+", s) and not s[:1].isupper():
        return False  # ids, paths, keys
    if re.search(r"\.(dat|exe|dll|json|txt|scx|scm|chk|eps|png|bmp|wav|ogg|mpq|casc|eds|ini|lua|py)$", s) and " " not in s:
        return False
    if re.match(r"^(https?://|\\\\|[A-Z]:\\)", s):
        return False
    if " " not in s and s.endswith(("Class", "Wnd", "Window")):
        return False
    if "%" in s and " " not in s:
        return False
    if " " not in s and ("_" in s or re.fullmatch(r"[A-Z][a-z]+([A-Z][a-z0-9]+)+", s)):
        return False  # registry keys, class names
    return True


def extract(repo):
    out = []
    sources = ["apps/editor/main.cpp"] + sorted(
        str(p.relative_to(repo)).replace("\\", "/") for p in (repo / "apps/editor").glob("*.inc"))
    sources.append("src/triggers.cpp")
    for rel in sources:
        p = repo / rel
        if rel in SKIP_FILES or not p.exists():
            continue
        src = p.read_text(encoding="utf-8", errors="replace")
        skip = None
        if rel in SKIP_MARKERS:
            a, b = SKIP_MARKERS[rel]
            ia, ib = src.find(a), src.find(b)
            if ia >= 0 and ib > ia:
                skip = (src.count("\n", 0, ia) + 1, src.count("\n", 0, ib) + 1)
        for r in lex(src, rel):
            if skip and skip[0] <= r["line"] < skip[1]:
                continue
            in_issue = r["callee"] == "issue" or any(s["name"] == "issue" for s in r["stack"])
            if rel.endswith("triggers.cpp") and not in_issue:
                continue
            if not rel.endswith("triggers.cpp") and not r["wide"]:
                continue
            if "<<" in r["src"] and "log" in r["src"].lower():
                continue
            if not looks_ui(r["text"]):
                continue
            surf = classify(r["stack"], r["text"], r["src"])
            if surf in ("log", "nonui"):
                continue
            out.append({"file": rel, "line": r["line"], "text": r["text"], "surface": surf})
    path = repo / "research/data/trigger_blocks.json"
    if not path.exists():  # research/ is local only (not in the repository): no block texts to check
        return out
    raw = path.read_text(encoding="utf-8")
    bj = json.loads(raw)
    rawl = raw.split("\n")

    def lineof(s):
        needle = json.dumps(s, ensure_ascii=False)[1:-1][:60]
        return next((k + 1 for k, l in enumerate(rawl) if needle in l), 0)

    rel = "research/data/trigger_blocks.json"
    for cats in bj["categories"].values():
        for c in cats:
            out.append({"file": rel, "line": lineof(c), "text": c, "surface": "block.category"})
    for b in bj["blocks"]:
        for f in ("title", "sentence", "docs", "example", "pitfalls"):
            v = b.get(f)
            if isinstance(v, str) and v:
                out.append({"file": rel, "line": lineof(v), "text": v, "surface": "block." + f})
    return out


# --- Checks --------------------------------------------------------------------------------------

# Proper nouns, game names and key names that keep their capitals in sentence case.
PROPER = set("""StarDraft StarCraft Remastered Brood War GitHub Discord EUD euddraft epScript Windows Terran Zerg Protoss
Independent Neutral Carbot Ctrl Shift Alt Space Del Esc Enter Tab Backspace Home End PgUp PgDn Insert F1 F2 F3 F4 F5
F6 F7 F8 F9 F10 F11 F12 Player Turbo MSQC CASC MPQ SD HD OK StarEdit Blizzard Camelot Systems Battle.net I""".split())
SMALL = set("a an the and or of in on at to for by with from as is are vs nor but into per".split())
TITLE_SURFACES = {"menu", "button", "title", "window.title", "msgbox.title", "section", "tab", "column",
                  "block.title", "block.category"}
NO_STOP_SURFACES = {"menu", "button", "label", "section", "title", "window.title", "msgbox.title", "cue", "picker",
                    "tab", "column", "block.title", "block.category"}
ONE_SENTENCE_SURFACES = {"status", "checker", "tooltip"}
BANNED = ["allow", "allows", "allowed", "enable", "enables", "enabled", "may", "should", "whether", "automatically",
          "approximately", "starts as", "can't", "unable", "please", "simply", "utilize", "in order to"]
UK = r"\b(colour\w*|behaviour\w*|centre\w*|grey\w*|cancelled|cancelling|licence|organis\w+|recognis\w+|initialis\w+|favourite\w*|analys(e|ed|ing))\b"
FILLER = ["just", "successfully", "currently", "note that", "be able to", "basically", "very"]
CONTRACTION = (r"(?i)\b\w+(n['’]t|['’](re|ll|ve|m|d))\b|\b(it|that|there|here|what|let|who|where)['’]s\b")
SELF = r"\b([Ww]e|[Oo]urs?|us)\b|(^|[\s(])I\s+[a-z]"
# Words from the Terms table. "picker" (the noun) and the Scenario menu ("Scenario >", "&Scenario") are not in it.
TERMS = {
    "scenario": r"\bscenarios?\b",
    "pick": r"(?i)\bpick(s|ed|ing)?\b",
    "choose": r"(?i)\b(choose|chooses|chose|chosen|choosing)\b",
    "compile": r"(?i)\bcompil(e|es|ed|ing|er|ation)\b",
    "erase": r"(?i)\beras(e|es|ed|ing)\b",
    "tech": r"\btech\b|^Tech$",
    "failed": r"(?i)\b(failed|fails to|fail to)\b",
    "player-n": r"(?<!the )(?<!this )(?<!that )(?<!each )\bplayer \d",
}
ERROR_WORDS = r"(?i)\b(invalid|illegal|something went wrong|an error occurred|wrong input|oops)\b"
GENERIC_TITLES = {"Error", "Warning", "StarDraft", "Information", "Info", "Notice", "Message", "Question", "Confirm",
                  "Alert"}
PASSIVE = (r"\b(is|are|was|were|be|been|being|gets|get)\s+(\w+ly\s+)?(\w+ed|built|made|shown|given|done|kept|set|put|"
           r"sent|read|written|hidden|taken|seen|known|found|run|chosen|drawn)\b")


def clean(t):  # a menu or button text without accelerator, mnemonic and ellipsis
    t = t.split("\t")[0].replace("&&", "\0").replace("&", "").replace("\0", "&")
    return t.strip().rstrip(".…").strip()


def is_title_case(t):
    words = re.findall(r"[A-Za-z][A-Za-z'’.-]*", re.sub(r"\([^)]*\b(Ctrl|Shift|Alt|Space|Del|Esc)\b[^)]*\)", "", clean(t)))
    if len(words) < 2:
        return False
    rest = [w for w in words[1:] if w.lower() not in SMALL and not w.isupper() and w.rstrip(".") not in PROPER
            and not re.search(r"[a-z][A-Z]", w)]
    return bool(rest) and all(w[0].isupper() for w in rest)


def sentences(t):
    return [s for s in re.split(r"(?<=[.?])\s+|\n+", t) if s.strip()]


def check(r):
    """The violations of one string: a list of check names."""
    t, s = r["text"], r["surface"]
    v = []
    if "..." in t:
        v.append("three-dots")
    if re.search("[—–]", t) and t.strip() != "—":
        v.append("dash")
    if re.search(r"[A-Za-z)]!(\s|$)", t):
        v.append("exclamation")
    if re.search(r"\b(Note|Tip|Important|Hint):", t):
        v.append("note-label")
    if re.search("[\U0001F300-\U0001FAFF☀-✒✔-➿]", t):
        v.append("emoji")
    for w in BANNED:
        if re.search(r"(?<![\w.])" + re.escape(w) + r"(?![\w'])", t, re.I):
            v.append("word:" + w)
    if re.search(r"\b(e\.g\.|etc\.|i\.e\.)", t):
        v.append("eg-etc")
    if re.search(UK, t, re.I):
        v.append("uk-spelling")
    if re.search(r"(?<![A-Za-z]\()\bP\d{1,2}\b", t):
        v.append("player-pn")
    if re.search(r"\b\d+ ?[-–] ?\d+\b", t) and not re.search(r"\d{4}-\d\d-\d\d", t):
        v.append("range")
    if s in TITLE_SURFACES and is_title_case(t):
        v.append("title-case")
    st = t.rstrip()
    # A full stop only when the string has two or more sentences. A fragment that starts with ". " continues
    # a sentence from another literal, so it counts as more than one.
    ends_stop = bool(re.search(r"[A-Za-z0-9)\"]\.$", st)) and not st.endswith("..")
    continued = t.lstrip().startswith(".") or t.endswith(". ") or t[:1].islower() or t[:1] == " "
    one_sentence = len(sentences(t.strip())) == 1 and not continued
    if ends_stop and one_sentence and s in NO_STOP_SURFACES | ONE_SENTENCE_SURFACES:
        v.append("full-stop")
    if s.startswith("block.") and s != "block.sentence":
        if any(len(x.split()) > 25 for x in sentences(t)):
            v.append("long-sentence")
    if s in ("label", "section") and st.endswith(":"):
        v.append("label-colon")
    if re.search(r"\s&\s", t):
        v.append("ampersand")
    plain = t.replace("&&", "\0").replace("&", "").replace("\0", "&")  # without access-key marks: "Do&n't" is "Don't"
    plain = re.sub(r"\{\w+\}", "{}", plain)  # the placeholders of a block sentence
    if re.search(CONTRACTION, plain):
        v.append("contraction")
    if re.search(SELF, plain):
        v.append("we-our-i")
    for w in FILLER:
        if re.search(r"(?<![\w.])" + re.escape(w) + r"(?![\w'])", plain, re.I):
            v.append("filler:" + w)
    for name, pat in TERMS.items():
        if re.search(pat, plain):
            v.append("term:" + name)
    if re.search(ERROR_WORDS, plain):
        v.append("error-words")
    if re.search(r"\w\(s\)", plain):
        v.append("plural-s")
    if "×" in t or re.search(r"\b[1-9]\d*[xX]\d+\b|\b\d+ X \d+\b", t):
        v.append("size-x")  # sizes are "128 x 128"
    if s == "msgbox.title" and clean(t) in GENERIC_TITLES:
        v.append("generic-title")
    if s == "button" and clean(t) in ("Yes", "No"):
        v.append("yes-no-button")
    # Two or more sentences: each has a full stop, the last one too. A checker message never ends with a full stop,
    # and a literal that ends with a space or a colon continues in the next one.
    if (s not in ("checker", "block.sentence") and not s.startswith("block.")
            and re.search(r"[a-z0-9)\"']\.\s+[A-Z]|^\.\s+[A-Z]", t.strip("\r\n"))
            and re.search(r"[A-Za-z0-9)\"']$", t) and not re.search(r"[.?!:…][)\"']$", t) and "\t" not in t):
        v.append("last-stop")
    return v


def warnings(r):
    t, w = r["text"], []
    if re.search(PASSIVE, t):
        w.append("passive")
    if r["surface"] != "block.sentence" and re.search(r"\b\w+(?: \w+)?, \w+(?: \w+)?,? (and|or) \w+", t):
        w.append("list-of-three")
    return w


DOCS = ["README.md", "docs/user/CHANGELOG.md"]


def check_docs(repo):
    out = []
    for f in DOCS:
        p = repo / f
        if not p.exists():
            continue
        for k, line in enumerate(p.read_text(encoding="utf-8").split("\n")):
            r = {"file": f, "line": k + 1, "text": line.strip(), "surface": "doc"}
            v = []
            if re.match(r"\s*[-*] \*\*[^*]+:?\*\*:?", line):
                v.append("bold-lead-in")
            if re.search("[—–]", line):
                v.append("dash")
            if re.search(r"[A-Za-z)]!(\s|$)", line):
                v.append("exclamation")
            if re.search(r"\b(e\.g\.|etc\.|i\.e\.)", line):
                v.append("eg-etc")
            if v:
                out.append((r, v))
    return out


# --- Baseline ------------------------------------------------------------------------------------

def key(check_name, r):
    return f'{check_name} | {r["file"]} | {r["text"].strip()[:120]!r}'


def load_baseline(path):
    keys = set()
    if not path.exists():
        return keys
    for line in path.read_text(encoding="utf-8").split("\n"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        k = line.rsplit(" | ", 1)[0] if line.count(" | ") >= 3 else line  # the last field is the reason
        keys.add(k)
    return keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--baseline", default=None)
    ap.add_argument("--warnings", action="store_true")
    ap.add_argument("--dump", default=None)
    a = ap.parse_args()
    repo = Path(a.repo)
    base_path = Path(a.baseline) if a.baseline else repo / "tools/ui_text_lint_baseline.txt"
    base = load_baseline(base_path)
    recs = extract(repo)
    if a.dump:
        Path(a.dump).write_text(json.dumps(recs, ensure_ascii=False, indent=0), encoding="utf-8")
    found = [(r, check(r)) for r in recs] + check_docs(repo)
    new, used = [], set()
    for r, vs in found:
        for c in vs:
            k = key(c, r)
            if k in base:
                used.add(k)
            else:
                new.append((c, r))
    for c, r in new:
        print(f'{r["file"]}:{r["line"]}: {c} [{r["surface"]}] {r["text"].strip()[:140]!r}')
    if a.warnings:
        for r in recs:
            for w in warnings(r):
                print(f'warning: {r["file"]}:{r["line"]}: {w} [{r["surface"]}] {r["text"].strip()[:140]!r}')
    stale = base - used
    for k in sorted(stale):
        print(f"note: baseline entry not used: {k}")
    print(f"ui_text_lint: {len(recs)} strings, {len(new)} new violations, {len(used)} baseline exceptions used")
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
