"""The background of the macOS disk image (StarDraft-*.dmg, tools/ci/mac_dist.sh): a light window with a faint map
grid and an arrow from the app (left) to the Applications folder (right). No text: it needs no translation.

usage: python3 -I tools/make_dmg_background.py <out.png> <out@2x.png>

The window is 660 x 400 points (tools/ci/dmg_settings.py: window_rect, icon_locations); the second picture is for
Retina displays (1320 x 800). mac_dist.sh puts both in one TIFF (tiffutil -cathidpicheck). Needs only Python.
"""

import math
import struct
import sys
import zlib

W, H = 660, 400
APP_X, APPS_X, ICON_Y = 165, 495, 190  # (dmg_settings.py: icon_locations)
TOP, BOTTOM = (246, 247, 249), (226, 229, 234)
GRID = (220, 223, 229)
ARROW = (120, 134, 156)


def png(path, w, h, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def arrow_distance(x, y):
    """The distance (points) from (x, y) to the arrow: a round-ended shaft and a triangle head; negative inside."""
    x0, x1, half = APP_X + 92, APPS_X - 112, 3.0
    head0, head1, headHalf = x1 - 6, APPS_X - 86, 15.0
    # the shaft: a capsule from x0 to head0
    cx = min(max(x, x0), head0)
    shaft = math.hypot(x - cx, y - ICON_Y) - half
    # the head: a triangle (head0, ICON_Y +- headHalf) - (head1, ICON_Y)
    tip_len = head1 - head0
    t = (x - head0) / tip_len
    if t < 0 or t > 1:
        head = min(abs(x - head0), abs(x - head1)) + max(0.0, abs(y - ICON_Y) - headHalf * max(0.0, min(1.0, 1 - t)))
        head = max(head, 0.5)
    else:
        allowed = headHalf * (1 - t)
        edge = (abs(y - ICON_Y) - allowed) * tip_len / math.hypot(tip_len, headHalf)
        head = max(edge, head0 - x)
    return min(shaft, head)


def picture(scale):
    w, h = W * scale, H * scale
    rows = []
    for py in range(h):
        y = (py + 0.5) / scale
        t = y / H
        bg = tuple(round(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3))
        row = bytearray(bg * w)
        # the faint grid: a line every 40 points
        if (py % (40 * scale)) < scale:
            for px in range(w):
                row[px * 3:px * 3 + 3] = bytes(GRID)
        else:
            for gx in range(0, w, 40 * scale):
                row[gx * 3:gx * 3 + 3 * scale] = bytes(GRID) * scale
        if abs(y - ICON_Y) < 20:
            for px in range(int((APP_X + 80) * scale), int((APPS_X - 76) * scale)):
                x = (px + 0.5) / scale
                a = max(0.0, min(1.0, 0.5 - arrow_distance(x, y) * scale))
                if a > 0:
                    base = row[px * 3:px * 3 + 3]
                    row[px * 3:px * 3 + 3] = bytes(round(base[i] * (1 - a) + ARROW[i] * a) for i in range(3))
        rows.append(row)
    return w, h, rows


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    for path, scale in ((sys.argv[1], 1), (sys.argv[2], 2)):
        w, h, rows = picture(scale)
        png(path, w, h, rows)
        print(f"wrote {path} ({w} x {h})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
