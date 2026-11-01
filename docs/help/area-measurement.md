# Measure terrain area

Open **Tools > Measure area > Draw area**. The small tool window measures a tile-snapped selection without changing the map or its undo history.

**Outline** accepts corners or freehand parts across separate gestures. Click the first corner, double-click, press Enter or use **Finish** to close it. Right-click removes the last corner; Escape stops editing. Closing the window keeps the selected area.

**Mark tiles** and **Remove tiles** paint or erase a selection across repeated strokes. Change the brush width to cover more tiles. Selections can contain disconnected areas and holes. **Undo selection** restores up to 20 previous mask edits, including **Clear**; **Resume selection** lets you edit a completed selection.

**All area**, **Walkable** and **Buildable** show terrain-only totals in Nexus equivalents (one Nexus is 4×3 tiles). Walkability counts the 8-pixel minitiles from the loaded VF4 data. Buildability uses CV5 flags and excludes the reserved bottom map edge. Units and resource exclusion zones are not subtracted, and the result does not count how many buildings can be packed into the shape. Editing or switching the map clears the old measurement.

## Sources and credits

Adapted from Map Studio, MIT, Copyright (c) 2026 ScmDraft Fan Add-on contributors. [Source](https://github.com/RustyNovoBland77464/scmdraft-native), revision `e35461dea3b4387b0956feb59ed07ab4ee8d3fa6`. The original MIT notice is included in `licenses/area-measurement.txt`.
