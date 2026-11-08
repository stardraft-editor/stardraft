# Symmetry copies

Use **Mode > Configure symmetry copies** to choose Mirrors, Rotations or Custom copies. The spatial preview and copy list show enabled and disabled copies. Click a copy to choose its terrain; double-click toggles it. Source is always placed. **Enable all** and **Source only** affect the complete set.

Turn on **Mode > Show symmetry options** to keep a compact floating panel visible
while editing. Select a copy from the colored list or use the previous/next buttons.
Pick terrain directly from the compact thumbnail grids, or toggle **Place this copy**;
the highlighted tile shows the selection, and hovering shows its full name. Changes
apply immediately. **Configure copies** opens the full composer. Closing
the panel turns the toggle off. The visibility choice is saved locally.

Rotations support 2–16 copies and optional mirrored rotations. Custom copies combine rotations and selected mirrors; **Include combinations** adds their combinations. Turn it off for a direct union, including deliberately incomplete layouts. Existing combinations retain their behavior when the composer opens.

Copies inherit **Default terrain**, or each can use a different ISOM terrain. **Raw tile IDs** expose optional overrides for Rectangle/custom tile brushes. The source brush wins at overlaps. Every placement remains one undo step. Erase uses the same ground at all enabled copies.

Copy terrain and placement choices are remembered for each source terrain in the
current map. For example, High Dirt can keep two different mirrored terrains;
selecting Water restores Water's setup. A source with no saved setup uses the
source brush for every copy. Mirror axes and copy count stay as selected.

These choices follow stable transforms when counts change. They are saved in the
map's local editor settings and restored when the map is reopened, with separate
profiles for each tileset. No extra data is written into the map file. An older
single configuration is assigned only to the source that was selected when saved.

## Sources and credits

Uses StarDraft's existing map data and editing APIs.
