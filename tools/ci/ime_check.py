"""Checks the IME workflow's results: what stardraft-next's search field holds after the keys.

    python ime_check.py korean|hanja|hangul-english|pinyin <run output>

korean: "gksrnrdj " typed: the field must be 한국어. hanja: 한, its Hanja (the first candidate), a space, "abc " in
English letters, then 국 in Hangul again: one Hanja, " abc 국". hangul-english: the same without the Hanja: "한 abc 국".
pinyin: "zhongwen ": 中文.
"""

import re
import sys


CHECKS = {
    "korean": ("한국어", "Korean"),
    "hanja": ("[㐀-鿿豈-﫿] abc 국", "Hanja, then 한/영"),
    "pinyin": ("中文", "Pinyin"),
    "hangul-english": ("한 abc 국", "한/영"),
}


def field(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.findall(r"^search field: (.*)$", text, re.M)
    return m[-1].rstrip() if m else None


def main(kind, path):
    sys.stdout.reconfigure(encoding="utf-8")  # (Windows' console code page has no Hangul)
    pattern, what = CHECKS[kind]
    got = field(path)
    ok = got is not None and re.fullmatch(pattern, got) is not None
    print(f"{'ok  ' if ok else 'FAIL'} {what}: {got!r}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
