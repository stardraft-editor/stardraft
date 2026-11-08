"""Make src/data/ee3_classic_functions.inc from third_party/eud-editor-3/ (EUD Editor 3's files, MIT).

Usage: python -I tools/gen_ee3_classic.py [--repo DIR]

EUD Editor 3's classic triggers write a block only when its function is in the editor's lists (TriggerCodeBlock.vb,
GetCodeFunction; else the block writes no text). The .inc holds those lists, by name:
- kEe3ClassicActions, kEe3ClassicConditions, kEe3ClassicPlib: the functions of classic/epsFunctions.txt (EUD Editor 3's
  Data/TriggerEditor/epsFunctions.txt), sorted into actions, conditions and plib functions as TriggerFunction.vb does
  (GetListFromEpScript and GetFunctionFromStr): a function with a "/*** ... ***/" tooltip has the type of its "@Type"
  line ("A" action, "C" condition, "F" plib; no type: action), and a function without a tooltip is a plib function.
- kEe3ClassicLua: the Lua macro functions: the names in macro_names.txt (the functions of Data/Lua/TriggerEditor/*.lua)
  and the functions that MacroManager.vb registers itself (preDefine, echo, ...).
"""
import argparse
import re
from pathlib import Path

# MacroManager.vb (New): the functions that it adds to FunctionList itself, before the .lua files.
BUILT_IN_MACROS = ["preDefine", "mainPreDefine", "onPluginText", "beforeText", "afterText", "echo", "GetBGMIndex",
                   "GetReturnBGMIndex"]


def strip_braces(text):
    """GetListFromEpScript: the text outside of { } (the functions' bodies and the objects' members go)."""
    out, depth = [], 0
    for c in text:
        if c == "{":
            depth += 1
        if depth == 0:
            out.append(c)
        if c == "}" and depth > 0:
            depth -= 1
    return "".join(out)


def tooltip_type(tooltip):
    """GetFunctionFromStr: the type from the tooltip's lines (" * @Type", then " * A" / " * C" / " * F")."""
    lines = tooltip.strip().split("\n")  # (the file as EUD Editor 3's release has it: one line end, LF)
    ftype, is_type = "A", False  # (EFType.Action is the enum's default)
    for i, line in enumerate(lines):
        t = line.replace("\r", "")
        if i == 0:
            if t == "/***":
                continue
            break
        if i == len(lines) - 1:
            if t == "***/":
                continue
            break
        if t.find(" * ") != 0:
            break
        t = t.replace(" * ", "")
        head = t.split(".")[0]
        if head == "@Type":
            is_type = True
        elif head in ("@Summary", "@param", "@Group"):
            is_type = False
        elif is_type:
            ftype = {"F": "F", "C": "C", "A": "A"}.get(t, ftype)
    return ftype


def functions(text):
    text = strip_braces("\n" + text.replace("\r\n", "\n") + "\n")
    tooltip_re = re.compile(r"(/\*\*\*[^/]*\*\*\*/\s+)function\s+(\w+)\(([/*\w,:\s]*)\)[^;]")
    normal_re = re.compile(r"function\s+(\w+)\(([/*\w,:\s]*)\)[^;]")
    found = []
    for m in reversed(list(tooltip_re.finditer(text))):
        found.append((tooltip_type(m.group(1)), m.group(2)))
        # (EUD Editor 3 cuts the match out but keeps its last character: Mid(..., Index + Length) is 1-based)
        text = text[:m.start()] + text[m.end() - 1:]
    for m in normal_re.finditer(text):
        found.append(("F", m.group(1)))
    return found


def array(name, names):
    return [f"const char* const {name}[] = {{"] + [f'    "{n}",' for n in sorted(set(names))] + ["};", ""]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    root = Path(ap.parse_args().repo)
    src = root / "third_party" / "eud-editor-3"
    found = functions((src / "classic" / "epsFunctions.txt").read_bytes().decode("utf-8-sig"))
    lua = (src / "macro_names.txt").read_text(encoding="utf-8").split() + BUILT_IN_MACROS
    parts = ["// Made by tools/gen_ee3_classic.py from third_party/eud-editor-3/ (EUD Editor 3, MIT, Copyright (c) 2019",
             "// LeeJungHun). Do not edit: run the tool.", ""]
    parts += array("kEe3ClassicActions", [n for t, n in found if t == "A"])
    parts += array("kEe3ClassicConditions", [n for t, n in found if t == "C"])
    parts += array("kEe3ClassicPlib", [n for t, n in found if t == "F"])
    parts += array("kEe3ClassicLua", lua)
    out = root / "src" / "data" / "ee3_classic_functions.inc"
    out.write_bytes(("\n".join(parts)).encode("utf-8"))
    print(out, {t: sum(1 for x, _ in found if x == t) for t in "ACF"}, len(set(lua)))


if __name__ == "__main__":
    main()
