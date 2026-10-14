"""Checks the IME workflow's results: what stardraft-next's search field holds after the keys.

    python ime_check.py <hanja run output> <pinyin run output>

The Hanja run typed 한, its Hanja (the first candidate), a space, "abc " in English letters, then 국 in Hangul again:
the field must be one Hanja, " abc 국". The Pinyin run typed "zhongwen ": the field must be 中文.
"""

import re
import sys


def field(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.findall(r"^search field: (.*)$", text, re.M)
    return m[-1].rstrip() if m else None


def main(hanja, pinyin):
    sys.stdout.reconfigure(encoding="utf-8")  # (Windows' console code page has no Hangul)
    failed = 0
    checks = [(hanja, r"[㐀-鿿豈-﫿] abc 국", "Hanja, then 한/영"),
              (pinyin, r"中文", "Pinyin")]
    for path, pattern, what in checks:
        got = field(path)
        ok = got is not None and re.fullmatch(pattern, got) is not None
        print(f"{'ok  ' if ok else 'FAIL'} {what}: {got!r}")
        failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
