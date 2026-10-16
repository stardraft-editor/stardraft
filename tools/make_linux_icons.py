"""The Linux icons (packaging/linux/icons/hicolor) from the Windows icons (apps/editor/res/stardraft.ico and
stardraft-document.ico).

usage: python -I tools/make_linux_icons.py [--check]

The program's icon goes to hicolor/<size>x<size>/apps/io.github.stardraft_editor.StarDraft.png and the map files' icon
to hicolor/<size>x<size>/mimetypes/io.github.stardraft_editor.StarDraft-map.png, at 16, 24, 32, 48, 64, 128, 256 and
512 px. A size that the .ico has is its own picture; another size is made from the next larger picture (512 px from the
256 px one), with Pillow. --check exits with 1 when a file is not the one the .ico makes (needs Pillow too).
"""

import argparse
import io
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "apps" / "editor" / "res"
OUT = ROOT / "packaging" / "linux" / "icons" / "hicolor"
APP_ID = "io.github.stardraft_editor.StarDraft"
SIZES = [16, 24, 32, 48, 64, 128, 256, 512]
ICONS = [(RES / "stardraft.ico", "apps", APP_ID), (RES / "stardraft-document.ico", "mimetypes", APP_ID + "-map")]


def ico_pngs(data: bytes) -> dict:
    """The pictures of an .ico by their width, as Pillow images (PNG and BMP pictures)."""
    _, kind, count = struct.unpack_from("<HHH", data, 0)
    if kind != 1:
        raise ValueError("not an icon file")
    out = {}
    for i in range(count):
        w = struct.unpack_from("<B", data, 6 + 16 * i)[0] or 256
        size, offset = struct.unpack_from("<II", data, 6 + 16 * i + 8)
        png = data[offset:offset + size]
        if png[:8] == b"\x89PNG\r\n\x1a\n":
            out[w] = Image.open(io.BytesIO(png)).convert("RGBA")
    if not out:  # (BMP pictures only: Pillow reads the largest)
        img = Image.open(io.BytesIO(data))
        for s in getattr(img, "ico").sizes():
            out[s[0]] = img.ico.getimage(s).convert("RGBA")
    return out


def picture(pics: dict, size: int) -> bytes:
    if size in pics:
        img = pics[size]
    else:
        larger = [s for s in pics if s > size]
        src = pics[min(larger)] if larger else pics[max(pics)]
        img = src.resize((size, size), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    differ = 0
    for ico, context, name in ICONS:
        pics = ico_pngs(ico.read_bytes())
        for size in SIZES:
            f = OUT / f"{size}x{size}" / context / f"{name}.png"
            data = picture(pics, size)
            if a.check:
                if not f.exists() or f.read_bytes() != data:
                    print(f"not made again: {f.relative_to(ROOT)}")
                    differ += 1
                continue
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(data)
            print(f"wrote {f.relative_to(ROOT)}")
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
