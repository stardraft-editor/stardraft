# Doodad placement helper

When a doodad is on the cursor, faint green areas show the full footprints of legal placements. The helper uses the same terrain requirements and existing-doodad overlap checks as a click. Overlapping legal footprints form one area without darker stacked shading.

**Options > Doodads > Show available placement areas** turns the helper on or off. The choice is remembered after restart. The helper operates while placing the current doodad; Spray, Fill, Erase and clipboard placement keep their existing behavior.

Green marks footprint coverage, not valid cursor centers: the usual cursor preview still shows whether a click at the current point will fit. The mask fills incrementally and refreshes after terrain edits, placement changes, or a different doodad. It does not modify the map.

## Sources and credits

Uses StarDraft's existing map data and editing APIs.
