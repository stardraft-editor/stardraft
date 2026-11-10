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
| Key hints: a strip at the bottom of the map with the keys of the tool that is on. The hint of a held key is bright. View > Show key hints. | `core/tool_hints.inc` |
| Smooth zoom: a wheel notch and Zoom in / Zoom out glide to the new zoom. | `core/map_input.inc` (ZoomGlide) |
| Units, right-click: Select more (in the view, of the player, invert) and Give to player. | `core/views_map_menu.inc` |
| Quick picker: the last 8 picks of the mode come first while nothing is typed. | `core/palette_items.inc` |

Performance: the quick performance KPIs are in their targets with these changes (no number is worse).

## Next rounds (ideas, not in an order)

- Key hints for the content of the cursor (R and M turn and mirror it) and for a drag that is under way.
- Fit map and "go to" moves (a unit, a location, the minimap) glide as the zoom does.
- Arrange for selected units: align and distribute (a submenu of the right-click menu).
- A pie menu for the tools of the mode (as Space is for terrain).
- Recent picks as a row at the top of each palette.
- Performance: one KPI improved and its target made tighter in each round.
