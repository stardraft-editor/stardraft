"""Makes the map view's shaders for Vulkan (SPIR-V) and Metal (MSL) from shaders/*.hlsl, into shaders/gen/.

    python -I tools/gen_shaders.py --shadercross <shadercross program>
    python -I tools/gen_shaders.py --check

The DXIL for Direct3D 12 is still made by the build (dxc from the Windows SDK); these two formats are made here, once,
and kept in git, so a build on Linux or macOS needs no shader tools. shadercross is SDL_shadercross
(github.com/libsdl-org/SDL_shadercross, commit 1ff05bec573988a98ef9e0260b4da44f512b8367: the program of its CI build
"SDL3_shadercross-<system>"); it has no releases.

Each shader becomes shaders/gen/<name>_<stage>_<format>.h with g_<name>_<stage>_<format>[] and its size. sources.txt
lists the SHA-256 of each .hlsl (with LF line ends) that the headers were made from: --check (GitHub runs it) needs no
shadercross and exits with 1 when a .hlsl changed after its headers were made.
"""

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHADERS = ROOT / "shaders"
GEN = SHADERS / "gen"
NAMES = ["terrain", "sprite", "solid", "downsample"]
STAGES = [("vs", "vertex", "VSMain"), ("ps", "fragment", "PSMain")]
FORMATS = [("spirv", "SPIRV", "spv"), ("msl", "MSL", "msl")]
MSL_VERSION = "2.1.0"  # macOS 10.14 and newer


def source_hash(name):
    text = (SHADERS / f"{name}.hlsl").read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(text).hexdigest()


def sources_text():
    lines = ["# SHA-256 of each shader (LF line ends) that the headers in this folder were made from (tools/gen_shaders.py)."]
    lines += [f"{source_hash(n)}  {n}.hlsl" for n in NAMES]
    return "\n".join(lines) + "\n"


def header(symbol, data, text):
    out = [f"// Made by tools/gen_shaders.py from shaders/{symbol.split('_')[1]}.hlsl. Do not edit.", "#pragma once", "",
           "#include <cstddef>", ""]
    if text:  # MSL source: SDL compiles it when the shader is made
        data = data + b"\0"
    out.append(f"alignas(4) static const unsigned char {symbol}[] = {{")
    for i in range(0, len(data), 24):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    out.append("};")
    size = len(data) - 1 if text else len(data)  # (without the NUL)
    out.append(f"static const size_t {symbol}_size = {size};")
    return "\n".join(out) + "\n"


def generate(shadercross):
    GEN.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name in NAMES:
            for stage, kind, entry in STAGES:
                for fmt, dest, ext in FORMATS:
                    out = Path(tmp) / f"{name}_{stage}.{ext}"
                    cmd = [shadercross, str(SHADERS / f"{name}.hlsl"), "-s", "HLSL", "-d", dest, "-t", kind, "-e", entry, "-o", str(out)]
                    if fmt == "msl":
                        cmd += ["--msl-version", MSL_VERSION]
                    r = subprocess.run(cmd, capture_output=True, text=True)
                    if r.returncode != 0 or not out.is_file():
                        sys.exit(f"shadercross failed for {name}.hlsl ({kind}, {dest}):\n{r.stdout}{r.stderr}")
                    data = out.read_bytes()
                    if fmt == "msl":
                        data = data.replace(b"\r\n", b"\n")
                    symbol = f"g_{name}_{stage}_{fmt}"
                    (GEN / f"{symbol[2:]}.h").write_text(header(symbol, data, fmt == "msl"), encoding="utf-8", newline="\n")
                    print(f"{symbol[2:]}.h: {len(data)} bytes")
    (GEN / "sources.txt").write_text(sources_text(), encoding="utf-8", newline="\n")


def check():
    path = GEN / "sources.txt"
    have = path.read_text(encoding="utf-8").replace("\r\n", "\n") if path.is_file() else ""
    if have != sources_text():
        print("shaders/gen/ was made from other shaders: run tools/gen_shaders.py --shadercross <program> (see its text)")
        return 1
    missing = [f"{n}_{s}_{f}.h" for n in NAMES for s, _, _ in STAGES for f, _, _ in FORMATS if not (GEN / f"{n}_{s}_{f}.h").is_file()]
    if missing:
        print("shaders/gen/ has no " + ", ".join(missing))
        return 1
    print("shaders/gen/ is up to date")
    return 0


def main(argv):
    if argv[:1] == ["--check"]:
        return check()
    if len(argv) == 2 and argv[0] == "--shadercross":
        generate(argv[1])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
