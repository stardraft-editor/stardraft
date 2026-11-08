"""Make src/data/eud_editor3.inc from third_party/eud-editor-3/ (EUD Editor 3's files, MIT).

Usage: python -I tools/gen_eud_editor3.py [--repo DIR]

The .inc holds:
- kEe3Library: the files of EUD Editor 3's TriggerEditor library (Data/TriggerEditor/*.eps, *.py), which its builds
  copy beside the scripts as the module folder TriggerEditor. A project imported from EUD Editor 3 gets them
  (src/project_import.cpp), so that scripts that import TriggerEditor.TETools and the others build.
- kEe3KeyNames: the names that EUD Editor 3 writes in a text for a byte below 128 ("<Enter>", "<Ctrl+A>";
  Data/Texts/ASCII Codes.txt), one per byte.
- kEe3MacroNames: the functions of EUD Editor 3's Lua macros (Data/Lua/TriggerEditor/*.lua: macro_names.txt, the
  names of their "function Name(" lines). "@Name(...)" in a script is a macro that only EUD Editor 3's build expands.
"""
import argparse
from pathlib import Path


def literal(text):
    out = []
    for line in text.splitlines(keepends=True):
        s = line.replace("\r\n", "\n")
        esc = "".join(c if 0x20 <= ord(c) < 0x7F and c not in '"\\?' else
                      {"\n": "\\n", "\t": "\\t", '"': '\\"', "\\": "\\\\", "?": "\\?"}.get(c) or
                      "".join(f"\\x{b:02X}\"\"" for b in c.encode("utf-8"))
                      for c in s)
        out.append(f'    "{esc}"')
    return "\n".join(out) if out else '    ""'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    root = Path(ap.parse_args().repo)
    src = root / "third_party" / "eud-editor-3"
    parts = ["// Made by tools/gen_eud_editor3.py from third_party/eud-editor-3/ (EUD Editor 3, MIT, Copyright (c) 2019",
             "// LeeJungHun). Do not edit: run the tool.", "",
             "struct Ee3LibraryFile {", "    const char* name;", "    const char* text;", "};", "",
             "const Ee3LibraryFile kEe3Library[] = {"]
    for f in sorted((src / "TriggerEditor").iterdir()):
        if f.suffix not in (".eps", ".py"):
            continue
        text = f.read_bytes().decode("utf-8-sig")
        parts.append(f'    {{"{f.name}",\n{literal(text)}}},')
    parts.append("};")
    parts.append("")
    names = (src / "ascii_codes.txt").read_bytes().decode("utf-8-sig").splitlines()
    parts.append("const char* const kEe3KeyNames[] = {")
    parts += [f'    "{n.replace(chr(92), chr(92) * 2).replace(chr(34), chr(92) + chr(34)).replace("?", chr(92) + "?")}",' for n in names]
    parts.append("};")
    parts.append("")
    macros = (src / "macro_names.txt").read_text(encoding="utf-8").split()
    parts.append("const char* const kEe3MacroNames[] = {")
    parts += [f'    "{n}",' for n in macros]
    parts.append("};")
    out = root / "src" / "data" / "eud_editor3.inc"
    out.write_bytes(("\n".join(parts) + "\n").encode("utf-8"))
    print(out)


if __name__ == "__main__":
    main()
