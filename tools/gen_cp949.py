#!/usr/bin/env python3
"""Makes apps/common/cp949_table.inc: the data of the CP949 (Korean, Unified Hangul Code) decoder in text_codec.h.

The table has two parts:
- the 2350 Hangul syllables of KS X 1001 (lead bytes B0-C8), as a bit set over the 11172 syllables U+AC00-U+D7A3. The
  other 8822 syllables are the UHC extension (lead bytes 81-C6, trail bytes 41-5A, 61-7A, 81-FE), in the same order;
- the other KS X 1001 rows (lead bytes A1-AF and C9-FE, trail bytes A1-FE): one code point each (0: not mapped).

The script checks that the decoder's rule gives Python's cp949 codec for each of the 2-byte codes. The user-defined
rows C9 and FE are U+E000-U+E0BB, as Windows' code page 949 maps them (Python's codec leaves them out).
Run: python tools/gen_cp949.py
"""
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "apps", "common", "cp949_table.inc")


def dec(b):
    if len(b) == 2 and b[0] in (0xC9, 0xFE) and b[1] >= 0xA1:  # the user-defined rows: private use, as Windows maps them
        return 0xE000 + (94 if b[0] == 0xFE else 0) + b[1] - 0xA1
    try:
        s = b.decode("cp949")
        return ord(s) if len(s) == 1 else 0
    except UnicodeDecodeError:
        return 0


def uhc_trails(lead):
    t = list(range(0x41, 0x5B)) + list(range(0x61, 0x7B)) + list(range(0x81, 0xFF if lead < 0xA1 else 0xA1))
    return t


def main():
    ks = set()
    for lead in range(0xB0, 0xC9):
        for trail in range(0xA1, 0xFF):
            c = dec(bytes([lead, trail]))
            assert 0xAC00 <= c <= 0xD7A3, (lead, trail, c)
            ks.add(c)
    assert len(ks) == 2350
    bits = [0] * ((11172 + 31) // 32)
    for c in ks:
        i = c - 0xAC00
        bits[i // 32] |= 1 << (i % 32)
    rows = [lead for lead in range(0xA1, 0xFF) if not 0xB0 <= lead <= 0xC8]
    table = [dec(bytes([lead, trail])) for lead in rows for trail in range(0xA1, 0xFF)]
    assert all(c < 0x10000 for c in table)

    # The rule, as the decoder does it, against the codec for every 2-byte code.
    ext = [c for c in range(0xAC00, 0xD7A4) if c not in ks]
    ksl = sorted(ks)
    uhc = {}
    k = 0
    for lead in range(0x81, 0xC7):
        for trail in uhc_trails(lead):
            if k < len(ext):
                uhc[(lead, trail)] = ext[k]
            k += 1
    assert k >= len(ext)
    for lead in range(0x81, 0xFF):
        for trail in range(0x41, 0xFF):
            want = dec(bytes([lead, trail]))
            if 0xB0 <= lead <= 0xC8 and trail >= 0xA1:
                got = ksl[(lead - 0xB0) * 94 + trail - 0xA1]
            elif trail >= 0xA1 and lead >= 0xA1:
                got = table[rows.index(lead) * 94 + trail - 0xA1]
            else:
                got = uhc.get((lead, trail), 0)
            assert got == want, (hex(lead), hex(trail), hex(got), hex(want))

    with open(OUT, "w", newline="\n") as f:
        f.write("// Made by tools/gen_cp949.py (from Python's cp949 codec): do not change it by hand.\n")
        f.write("// The KS X 1001 Hangul syllables (bit i: U+AC00 + i).\n")
        f.write("inline constexpr uint32_t kCp949KsHangul[%d] = {\n" % len(bits))
        for i in range(0, len(bits), 8):
            f.write("    " + ", ".join("0x%08X" % b for b in bits[i:i + 8]) + ",\n")
        f.write("};\n")
        f.write("// The other KS X 1001 rows (lead bytes A1-AF, C9-FE; 94 trail bytes A1-FE each); 0: not mapped.\n")
        f.write("inline constexpr char16_t kCp949Rows[%d] = {\n" % len(table))
        for i in range(0, len(table), 12):
            f.write("    " + ", ".join("0x%04X" % c for c in table[i:i + 12]) + ",\n")
        f.write("};\n")
    print("wrote", os.path.normpath(OUT), len(table), "row codes,", len(ks), "KS syllables")


if __name__ == "__main__":
    main()
