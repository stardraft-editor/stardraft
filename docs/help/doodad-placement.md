# Doodad placement helper

When a doodad is on the cursor, faint green areas show the full footprints of legal placements. The helper uses the same terrain requirements and existing-doodad overlap checks as a click. Overlapping legal footprints form one area without darker stacked shading.

**Options > Doodads > Show available placement areas** turns the helper on or off. The choice is remembered after restart. The helper operates while placing the current doodad; Spray, Fill, Erase and clipboard placement keep their existing behavior.

Green marks footprint coverage, not valid cursor centers: the usual cursor preview still shows whether a click at the current point will fit. The mask fills incrementally and refreshes after terrain edits, placement changes, or a different doodad. It does not modify the map.

## Sources and credits

- Behavior adapted from [Map Studio](https://github.com/RustyNovoBland77464/scmdraft-native/tree/e35461dea3b4387b0956feb59ed07ab4ee8d3fa6), MIT. Copyright (c) 2026 ScmDraft Fan Add-on contributors; the notice is retained in `licenses/doodad-placement.txt`.
- Placement rules use StarDraft's existing `doodadFits` and `Editor::doodadOverlaps`, which read the active game's or mod's data.
