# Pathfinding regions

Use **View > Overlays > Pathing > Regions** to show or hide the terrain regions
used by the simulation engine. Each walkable region has a color. Lines mark
region boundaries; impassable regions stay transparent.

Open **View > Overlays > Pathing > Region options** for a small window with
**Show regions** and an **Opacity** slider. Drag the slider or use its arrow keys.
Home sets 0%, and End sets 100%. At 0%, both fills and boundaries disappear.
Closing the window keeps the overlay on. The display mode and opacity are saved
in the editor settings.

Regions update in the background after terrain edits and undo/redo. An old
result is hidden while its replacement is made. Panning and zooming reuse the
current regions; they do not start a new analysis. Boundaries are hidden when
minitiles are too small to show clear lines.

These are static terrain regions at 8-pixel resolution. Buildings, resources,
units and creep can affect movement without changing this region view. Regions
do not predict every route or replace a simulation measurement. The map and its
undo history are not changed by the display.

**Connected areas** remains available in the same menu. **Tools > Check pathing**
keeps the existing pathing-problem analysis and fixes.

## Sources and credits

This display uses the independent MIT Map Simulation engine by Map Editor
contributors, also used by Mining Lab and Rush distance. Game data comes from
the user's installation. OpenBW is an optional development reference and is
not needed to display regions.
