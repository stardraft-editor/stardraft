"""Make apps/editor/res/fun/fun_sounds.inc (Fun mode's recorded sounds in the program) from the WAV files beside it.

Usage: python -I tools/fun_sounds_gen.py [--repo DIR]

Each apps/editor/res/fun/<kind><n>.wav is mono 8-bit unsigned PCM (11,025 Hz: tiny and bit-like). The kind is the
name without its number (splash, gloop, brush, crunch, rock: core/fun_mode.inc's kKindNames). The output has each
file's samples and rate; core/fun_mode.inc plays them in place of the sound made in code. Sources and licenses:
docs/dev/fun-mode.md.
"""
import argparse
import re
import wave
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--repo", default=Path(__file__).resolve().parent.parent)
repo = Path(ap.parse_args().repo)
folder = repo / "apps" / "editor" / "res" / "fun"
out = ["// Made by tools/fun_sounds_gen.py from the WAV files here: do not edit. Fun mode's recorded sounds",
       "// (core/fun_mode.inc): {kind, rate, samples (8-bit unsigned, mono), count}.", ""]
entries = []
for f in sorted(folder.glob("*.wav")):
    m = re.fullmatch(r"([a-z]+)(\d+)", f.stem)
    if not m:
        raise SystemExit(f"{f.name}: the name is not <kind><n>.wav")
    with wave.open(str(f), "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 1:
            raise SystemExit(f"{f.name}: not mono 8-bit")
        rate, data = w.getframerate(), w.readframes(w.getnframes())
    var = f"kFun_{f.stem}"
    out.append(f"const uint8_t {var}[] = {{")
    for i in range(0, len(data), 24):
        out.append("    " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
    out.append("};")
    entries.append(f'    {{"{m.group(1)}", {rate}, {var}, sizeof {var}}},')
out += ["struct FunSample {", "    const char* kind;", "    int rate;", "    const uint8_t* data;", "    size_t size;", "};",
        "const FunSample kFunSamples[] = {"] + entries + ["};", ""]
(folder / "fun_sounds.inc").write_bytes("\r\n".join(out).encode())
print(f"{len(entries)} sounds")
