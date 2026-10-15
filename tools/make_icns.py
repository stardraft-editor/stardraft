"""The macOS app icon (apps/editor/res/stardraft.icns) from the Windows icon (apps/editor/res/stardraft.ico).

usage: python -I tools/make_icns.py [--check]

Each PNG picture of the .ico goes into the .icns at its size (16, 32, 64, 128 and 256 px), and also as the Retina
picture of the size half as large (32 px as 16 px @2x, and so on). The .icns is made again after a change to the .ico;
--check exits with 1 when the .icns is not the one the .ico makes. Needs only Python.
"""

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ICO = ROOT / "apps" / "editor" / "res" / "stardraft.ico"
ICNS = ROOT / "apps" / "editor" / "res" / "stardraft.icns"

# The .icns types of PNG pictures: the size in pixels, and its types (at 1x, then at 2x of half the size).
TYPES = {16: [b"icp4"], 32: [b"icp5", b"ic11"], 64: [b"icp6", b"ic12"], 128: [b"ic07"], 256: [b"ic08", b"ic13"],
         512: [b"ic09", b"ic14"], 1024: [b"ic10"]}


def ico_pngs(data: bytes) -> dict:
    """The PNG pictures of an .ico, by their width (pictures in the old BMP format are not used)."""
    _, kind, count = struct.unpack_from("<HHH", data, 0)
    if kind != 1:
        raise ValueError("not an icon file")
    out = {}
    for i in range(count):
        w, _, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        png = data[offset:offset + size]
        if png[:8] == b"\x89PNG\r\n\x1a\n":
            out[w or 256] = png
    return out


def make_icns(pngs: dict) -> bytes:
    body = b""
    for size in sorted(pngs):
        for kind in TYPES.get(size, []):
            body += kind + struct.pack(">I", 8 + len(pngs[size])) + pngs[size]
    if not body:
        raise ValueError("the icon file has no PNG picture of a size that an .icns takes")
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit with 1 when the .icns is not the one the .ico makes")
    args = ap.parse_args()
    icns = make_icns(ico_pngs(ICO.read_bytes()))
    if args.check:
        same = ICNS.exists() and ICNS.read_bytes() == icns
        print(f"{ICNS.relative_to(ROOT)}: {'up to date' if same else 'not the one the .ico makes: run tools/make_icns.py'}")
        return 0 if same else 1
    ICNS.write_bytes(icns)
    print(f"wrote {ICNS.relative_to(ROOT)} ({len(icns)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
