"""Make src/data/ee2_triggers.inc from third_party/eud-editor-2/ (EUD Editor 2's files, MIT).

Usage: python -I tools/gen_ee2_triggers.py [--repo DIR]

The .inc holds what src/import_ee2_triggers.cpp needs to write the TriggerEditor.eps that EUD Editor 2 builds from its
trigger editor:
- kEe2Actions, kEe2Conditions: the trigger editor's actions and conditions (Data/TriggerEditor/action.json and
  condition.json: name, code text with $Value$ places, the value kinds).
- kEe2DatFields: the fields of the eight .dat files in EUD Editor 2's order (Data/<dat>.def: name as EUD Editor 2 reads
  it, size in bytes) with their StarCraft: Remastered addresses (Data/OffsetRemastered.txt; 0: none).
- kEe2CUnitFields: the CUnit fields of the "struct" actions (Data/CUnitToTriggerEditor.txt).
- kEe2Modules: the modules that EUD Editor 2 writes beside TriggerEditor.eps (Resources/punitloop.txt as
  punitloop.py, Resources/customText.txt as tempcustomText.py).
- kEe2Functions: the functions that EUD Editor 2 adds from its TEFunction folder when a trigger calls one that the
  project does not define (TEFunction/*.tfn).
"""
import argparse
import json
import re
from pathlib import Path

DATS = ["units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders"]


def cstr(text):
    out = []
    for c in text:
        if c == "\n":
            out.append("\\n")
        elif c == "\r":
            out.append("\\r")
        elif c == "\t":
            out.append("\\t")
        elif c in '"\\?':
            out.append("\\" + c)
        elif 0x20 <= ord(c) < 0x7F:
            out.append(c)
        else:
            out.append("".join(f'\\x{b:02X}""' for b in c.encode("utf-8")))
    return '"' + "".join(out) + '"'


def literal(text):
    lines = text.splitlines(keepends=True)
    if not lines:
        return '    ""'
    return "\n".join("    " + cstr(line.replace("\r\n", "\n")) for line in lines)


def load_json(path):
    text = path.read_bytes().decode("utf-16")
    text = re.sub(r",(\s*[\]}])", r"\1", text)  # the files have trailing commas, which Json.NET takes
    return json.loads(text)


def read_def(text, key):
    # EUD Editor 2's ReadDEF: the line "<key>=...", its value trimmed and, when it has a colon, cut after the colon.
    m = re.search(r"^" + re.escape(key) + r"=(.*)$", text, re.M)
    if not m:
        return None
    v = m.group(1).strip()
    if ":" in v:
        v = v[: v.index(":") + 1]
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    root = Path(ap.parse_args().repo)
    src = root / "third_party" / "eud-editor-2"
    parts = ["// Made by tools/gen_ee2_triggers.py from third_party/eud-editor-2/ (EUD Editor 2, MIT, Copyright (c) 2018",
             "// LeeJungHun). Do not edit: run the tool.", ""]

    parts += ["struct Ee2ActCon {", "    const char* name;", "    const char* code;",
              "    const char* defs;  // the value kinds, comma separated", "};", ""]
    for var, file in (("kEe2Actions", "action.json"), ("kEe2Conditions", "condition.json")):
        parts.append(f"const Ee2ActCon {var}[] = {{")
        for a in load_json(src / "Data" / "TriggerEditor" / file):
            parts.append(f'    {{{cstr(a["Name"])}, {cstr(a["CodeText"])}, {cstr(",".join(a["ValuesDef"]))}}},')
        parts.append("};")
        parts.append("")

    offsets = {}
    for line in (src / "Data" / "OffsetRemastered.txt").read_bytes().decode("utf-8").splitlines():
        m = re.match(r"^\[(.*)=0x([0-9A-Fa-f]{6})", line.strip())
        if m:
            offsets.setdefault(m.group(1), int(m.group(2), 16))
    parts += ["struct Ee2DatField {", "    int dat;           // 0 units ... 7 orders", "    const char* name;",
              "    int size;          // bytes per entry", "    unsigned offset;   // the field's address (0: none)", "};",
              "", "const Ee2DatField kEe2DatFields[] = {"]
    for d, dat in enumerate(DATS):
        text = (src / "Data" / f"{dat}.def").read_bytes().decode("utf-8", "replace").replace("\r\n", "\n")
        count = int(read_def(text, "Varcount"))
        for i in range(count):
            name = read_def(text, f"{i}Name")
            size = int(read_def(text, f"{i}Size"))
            arr = read_def(text, f"{i}VarArray")
            if arr is not None:
                size *= int(arr)
            off = offsets.get(f"{dat}_{name}", 0)
            parts.append(f"    {{{d}, {cstr(name)}, {size}, 0x{off:X}}},")
    parts.append("};")
    parts.append("")

    parts += ["struct Ee2CUnitField {", "    const char* offset;  // hex, no 0x", "    const char* kind;",
              "    const char* name;", "    int size;            // -1: from the next field's offset", "};", "",
              "const Ee2CUnitField kEe2CUnitFields[] = {"]
    for line in (src / "Data" / "CUnitToTriggerEditor.txt").read_bytes().decode("utf-8").splitlines():
        if not line.strip():
            continue
        f = line.split(",")
        size = int(f[3]) if len(f) >= 4 else -1
        parts.append(f"    {{{cstr(f[0])}, {cstr(f[1])}, {cstr(f[2])}, {size}}},")
    parts.append("};")
    parts.append("")

    parts += ["struct Ee2TextFile {", "    const char* name;", "    const char* text;", "};", "",
              "const Ee2TextFile kEe2Modules[] = {"]
    for name, file, enc in (("punitloop.py", "punitloop.txt", "cp949"), ("tempcustomText.py", "customText.txt", "utf-8-sig")):
        text = (src / "Resources" / file).read_bytes().decode(enc)
        parts.append(f"    {{{cstr(name)},\n{literal(text)}}},")
    parts.append("};")
    parts.append("")
    parts.append("const Ee2TextFile kEe2Functions[] = {")
    for f in sorted((src / "TEFunction").glob("*.tfn")):
        text = f.read_bytes().decode("utf-8-sig")
        parts.append(f"    {{{cstr(f.stem)},\n{literal(text)}}},")
    parts.append("};")

    out = root / "src" / "data" / "ee2_triggers.inc"
    out.write_bytes(("\n".join(parts) + "\n").encode("utf-8"))
    print(out)


if __name__ == "__main__":
    main()
