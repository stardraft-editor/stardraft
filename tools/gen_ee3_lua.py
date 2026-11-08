"""Make src/data/ee3_lua.inc from third_party/eud-editor-3/ (EUD Editor 3's files, MIT).

Usage: python -I tools/gen_ee3_lua.py [--repo DIR]

The .inc holds:
- kEe3LuaFiles: EUD Editor 3's Lua macro files (Data/Lua/TriggerEditor/*.lua: third_party/eud-editor-3/Lua/), byte
  for byte, in the order that EUD Editor 3 runs them when it starts (the order of Windows' file list: the names
  compared in capital letters). src/import_ee3_macros.cpp runs them in its sandbox before a project's macros.
- kEe3Offsets: EUD Editor 3's Data/Offset.txt ("units_Hit Points=0x662350"): the memory address of each .dat field,
  for the macros DatOffset, GetDatFile, SetDatFile and ConditionDatFile.
"""
import argparse
from pathlib import Path

# MSVC cannot compile a string literal of more than 65535 bytes (C2026), also when it is made of parts.
MAX_LITERAL = 65535


def literal(data):
    """The bytes as C string literal parts, one per line of the file; each byte that is not printable ASCII as \\xNN."""
    out = []
    for line in data.splitlines(keepends=True):
        parts = []
        hexed = False  # (after a \\xNN, a hex digit must start a new part: "\\x41" "1")
        for b in line:
            c = chr(b)
            if 0x20 <= b < 0x7F and c not in '"\\?':
                if hexed and c in "0123456789abcdefABCDEF":
                    parts.append('""')
                parts.append(c)
                hexed = False
            elif c in '"\\?':
                parts.append("\\" + c)
                hexed = False
            elif c == "\n":
                parts.append("\\n")
                hexed = False
            elif c == "\r":
                parts.append("\\r")
                hexed = False
            elif c == "\t":
                parts.append("\\t")
                hexed = False
            else:
                parts.append(f"\\x{b:02X}")
                hexed = True
        out.append('    "' + "".join(parts) + '"')
    return "\n".join(out) if out else '    ""'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args()
    repo = Path(args.repo)
    src = repo / "third_party" / "eud-editor-3"
    files = sorted((p for p in (src / "Lua").iterdir() if p.suffix.lower() == ".lua"), key=lambda p: p.name.upper())
    out = [
        "// Made by tools/gen_ee3_lua.py from third_party/eud-editor-3/ (EUD Editor 3, MIT, Copyright (c) 2019",
        "// LeeJungHun). Do not edit: run the tool.",
        "",
        "struct Ee3LuaFile {",
        "    const char* name;",
        "    const char* text;",
        "    size_t size;  // (the text's bytes)",
        "};",
        "",
        "const Ee3LuaFile kEe3LuaFiles[] = {",
    ]
    for p in files:
        data = p.read_bytes()
        if len(data) >= MAX_LITERAL:
            raise SystemExit(f"{p.name}: {len(data)} bytes, too long for one string literal")
        out.append(f'    {{"{p.name}",')
        out.append(literal(data) + ",")
        out.append(f"    {len(data)}}},")
    out.append("};")
    out.append("")
    offsets = (src / "Offset.txt").read_bytes()
    if len(offsets) >= MAX_LITERAL:
        raise SystemExit("Offset.txt: too long for one string literal")
    out.append("const char kEe3Offsets[] =")
    out.append(literal(offsets) + ";")
    out.append("")
    (repo / "src" / "data" / "ee3_lua.inc").write_bytes("\n".join(out).encode("ascii"))
    print(f"src/data/ee3_lua.inc: {len(files)} Lua files, Offset.txt")


if __name__ == "__main__":
    main()
