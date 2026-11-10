# UX overhaul (test branch)

Branch `test/ux-overhaul`. The aim: the program is easier for a new mapper, gives more to an expert through submenus,
shortcuts and modifier keys, and the map work is smooth. Each round is a small set of changes that can go to `master`
or stay out.

## The audit

The program has many functions. The gaps are in how a mapper finds them and in the flow of the work:

- The modifier keys of a tool show only when a key is down (the mark at the mouse). Nothing says which keys there are.
- The right-click menu on the map has few items for units; the faster selections are not there.
- A wheel notch changes the zoom in one jump.
- The quick picker (Q) starts each time from the full list.

## Round 1 (done)

| Change | Where |
|---|---|
| Key hints: a strip at the bottom of the map with the keys of the tool that is on. The hint of a held key is bright. View > Show key hints (off for a new user). | `core/tool_hints.inc` |
| Smooth zoom: a wheel notch and Zoom in / Zoom out glide to the new zoom. | `core/map_input.inc` (ZoomGlide) |
| Units, right-click: Select more (in the view, of the player, invert) and Give to player. | `core/views_map_menu.inc` |
| Quick picker: the last 8 picks of the mode come first while nothing is typed. | `core/palette_items.inc` |

Performance: the quick performance KPIs are in their targets with these changes (no number is worse).

## Round 2 (done)

New tools:

| Change | Where |
|---|---|
| Arrange: 12 commands for the selected units (align on 6 lines, equal spaces across and down, in a row, a column, a grid, a circle). Buildings stay on their tiles; units that do not fit stay in their places. One undo step. Edit > Arrange, the map's right-click menu, the selection bar. | `src/editor.cpp` (arrangeSelection), `core/map_commands.inc` |
| Selection bar: buttons over the selected units (Duplicate, Delete, Arrange, Give to player, Snap to the grid, Properties). View > Show selection bar (off for a new user). | `core/selection_bar.inc` |
| Pie palette for Units, Doodads and Sprites: while Space is held, rings open at the mouse. Ring 1 has Recent and the groups of the mode's list; the mouse over a group opens its ring outside it; the last ring has the items as pictures. Letting go over an item places it. | `core/views_pick_pie.inc` |
| Overview: while Ctrl+Space is held, the view shows the whole map with an outline at the mouse. Letting go puts that place in the view at the zoom from before. | `core/map_input.inc` (MapPeek) |
| Glides: Fit map, Actual size, a click on the minimap, a unit of the list and a location of the quick picker move the view in a glide. | `core/map_input.inc` (ViewGlide) |

## Next rounds (ideas, not in an order)

- Key hints for the content of the cursor (R and M turn and mirror it) and for a drag that is under way.
- Recent picks as a row at the top of each palette.
- Performance: one KPI improved and its target made tighter in each round.
