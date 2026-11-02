# Create a mirrored terrain version

Select terrain with Select / copy, then right-click **Create mirrored version**, or press **Ctrl+Shift+M** (**Cmd+Shift+M** on macOS). The command puts a left/right reflection into the raw tile clipboard. The paste preview follows the mouse; click to place it. The source stays unchanged, and a paste is one undo step. The selection shape and holes are preserved.

The command searches existing tile artwork from the current tileset. Exact reflected artwork is preferred; when none exists, a visual approximation is used. This is direct tile mapping: it does not regenerate ISOM or require reflected terrain height, buildability or minitile flags. Terrain is never replaced by doodad artwork.

The status reports approximate and unmatched tiles. Inspect the preview and terrain joins. Approximate artwork does not guarantee seamless joins or identical walkability.

Search runs in the background. Escape, a new copy/paste command, a changed selection or tool, a map edit, or a map switch discards an obsolete result. Panning does not cancel it.

## Sources and credits

Uses StarDraft's existing map data and editing APIs.
