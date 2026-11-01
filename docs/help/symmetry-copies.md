# Symmetry copies

Use **Mode > Configure symmetry copies** to choose Mirrors, Rotations or Custom copies. The spatial preview and copy list show enabled and disabled copies. Click a copy to choose its terrain; double-click toggles it. Source is always placed. **Enable all** and **Source only** affect the complete set.

Rotations support 2–16 copies and optional mirrored rotations. Custom copies combine rotations and selected mirrors; **Include combinations** adds their combinations. Turn it off for a direct union, including deliberately incomplete layouts. Existing combinations retain their behavior when the composer opens.

Copies inherit **Default terrain**, or each can use a different ISOM terrain. **Raw tile IDs** expose optional overrides for Rectangle/custom tile brushes. The source brush wins at overlaps. Every placement remains one undo step. Erase uses the same ground at all enabled copies.

Settings follow stable transforms when counts change and remain saved per palette in local map settings; no extra data is written into the map file.

## Sources and credits

Uses StarDraft's existing map data and editing APIs.
