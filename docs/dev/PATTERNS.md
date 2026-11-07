# StarDraft design patterns

PRINCIPLES.md tells what StarDraft must do. This document tells how: one pattern for each job that comes back in many
places. When two places do the same job, they look the same and work the same. Then a user who learns one place knows
the others.

**The rule.** Before you add a control, a menu, a setting or a gesture, find its job in this document and use that
pattern. If the job is not here, add the pattern here first, then build it. A one-off (a place that does a job in a
different way) needs a reason in "Known one-offs" below. "It was faster to build" is not a reason.

**Reuse the code too.** A pattern has one implementation in the kit or in a shared helper. Do not copy it into a view.
If the helper does not do what you need, change the helper, so that all places get the change.

## Lists

A list of things to pick (units, doodads, sprites, terrains, tiles, custom terrain, locations, layers, entries).

- **The top row:** the search box, then the Sort button (an icon button, if the list has more than one order), then the
  View button (if the list has more than one view). All lists put them in this order, at the same sizes
  (core/layout.inc, the search row).
- **The View menu:** List or Grid, then Normal, Large, Extra large, then the list's own items. The right-click menu of
  the list has the same View items at its end.
- **The Sort menu:** the orders as "Name: A to Z", "Name: Z to A", "Number: low to high", "Date: newest first". One
  command id for each order.
- **The state of a list** (its view, its order, its closed headings) belongs to its panel. A second panel of the same
  list keeps its own state.
- **Headings:** a chevron, the name and a line to the right edge. A click closes or opens the heading.
- **Selection:** a click selects one item, Shift+click selects a range, Ctrl+click adds or removes one item.
- **An empty list** shows "None", or one or two dim lines that tell how to add the first item.

## Where a setting goes

Use the first line of this table that agrees with the setting.

| The setting | Its place |
|---|---|
| It changes what the map shows, not the map | View menu |
| It is a value of the tool in use, and the user changes it while working (size, density, shape, symmetry) | Tool strip |
| It is a rule for one kind of object (units, doodads, sprites, locations) | Options > that kind |
| It is a rule for all kinds of objects | Options, under the kinds |
| It is a preference that the user sets once (folders, limits, updates, language) | Program options |
| It is part of the map or the project | Map properties, or the project's page |

- A setting is in one place only. A menu item and a check box in Program options for the same setting is a defect.
- The default value gives the old behavior (PRINCIPLES.md, "Do not change what users know").

## Modifier keys

A modifier key has one meaning for each kind of surface. There are two kinds:

- **A list or grid of items:** rows or cells that are things to pick (the palettes, the Custom list, the Layer list,
  the Data lists). A click selects an item.
- **An area selection:** a surface where the user selects an area of cells, with a box, a lasso or a wand (the map's
  Select, Lasso and Magic wand, and the Tiles grid).

| Key | A list or grid of items | An area selection |
|---|---|---|
| Click | Select one item | Select the area (a drag), or the cell |
| Shift | Shift+click: select a range | Shift+drag: add an area |
| Ctrl | Ctrl+click: add or remove one item | Ctrl+drag: take an area out |
| Alt | Alt+click: the item's alternate action (terrains, tiles, custom terrain: in or out of the mask) | - |

On the map, outside the area selection tools:

| Key | Meaning |
|---|---|
| Ctrl+click | Terrain: pick the terrain or the tile under the mouse (the eyedropper). Other modes: add or remove one object |
| Alt+click | The pipette (the modes other than Terrain) |
| Alt+wheel | The brush size |

- **Ctrl+click is the eyedropper in Terrain**, as in SCMDraft: the mappers know it. It is a known difference from
  Ctrl+click in the other modes, and it stays (the user, 2026-11-07).
- **Alt+click on an item** does its alternate action. Each list that has one names it in the item's tooltip and has
  the same action in the item's right-click menu.
- A tool can give a modifier key a second meaning only while it is the tool in use, and its tooltip and the status
  bar say so.

## Menus and pickers

- **A popup menu** is a MenuModel, shown with sdlPopupMenu. Then it has access keys, check marks, gray items and
  "Report feedback on…" at its end. Do not use shellPopupPick, shellContextMenu or kit::Menu::popup for new menus.
- **A choice of a few values** that the user sees all the time is a dropdown or a segmented control, not a menu.
- **A typed value** (a number, a size, a percent) is a kit text field in its place, with its range and its unit
  beside it. It refuses values out of range. Do not ask for a number in a prompt.
- **A name** (rename, a new name) uses shellPromptText. The same text is the title of the prompt and of its error box.
- **A pick from game data** (a unit, a sound, a string) uses runListPick or a value pop-out (PRINCIPLES.md, "Edit
  values in pop-outs").

## Buttons and icons

- **A symbol is an icon, never a font.** Each symbol (an arrow, a chevron, a check, a button's picture) comes from
  the UiIcon atlas (tools/icongen). Do not draw a symbol with a font: no Segoe glyphs (Face::Glyphs,
  IconGlyph::ofGlyph), no symbol characters of the UI font. If the atlas has no icon for the job, add one.
- **A button** is a kit widget (kit::Button, kit::IconButton). A painted area that acts as a button (a StripHit, a hit
  rectangle) is only for a surface that the kit cannot make yet. Then it has the kit's hover, focus and tooltip.
- **An action** is a button, **an entity** is a link (PRINCIPLES.md, "Buttons for actions, links for entities").

## Messages and questions

- **A result** goes to the status bar with setMessage. It says what changed, then the next step if there is one:
  "Moved Loose to the folder Rocks".
- **A question** uses shellAskYesNo, with the buttons named by their actions ("Replace", "Cancel"), not "Yes" and "No".
- **An error box** uses shellErrorBox. A typed value that is not valid is refused in its field; it does not open a box.
- **"Do not ask again"** is a check box in the question. The same setting is a check box in Program options, and the
  status bar tells where it is when the user checks it.
- Do not call msgbox or runMessageBox directly from a view.

## Masks and filters

A mask or a filter changes what the next edit or the list does. The user must always see it.

- **Set:** Alt+click on the item's row adds it to the mask or removes it (a toggle, the same in each list). The row's
  right-click menu has the same action ("Add to the mask", "Remove from the mask").
- **Show:** while a mask is on, the tool strip shows it (a chip with its name and a close button). The items in the
  mask have an outline in their list.
- **Clear:** the close button of the chip, and the map's right-click menu ("Clear the mask"). The message names what
  was cleared.
- One mask kind for each terrain kind: the terrain mask for Isometric and Rectangle, the tile mask for Tile and Custom.

## Drag and drop

- **The threshold:** a press becomes a drag after scale(4) pixels.
- **The capture:** the view captures the mouse while it drags. A release outside the view, or Esc, cancels the drag.
- **The sign:** a row that moves shows the grip and the kit's drop line (kitui::reorderRow, kitui::reorderDropLine). A
  drop into a group (a folder, a layer) outlines the group's row.
- **The cursor** shows the move (SizeAll for a move on the map, SizeNS for a row).
- Each drag is one undo step, or (a file that moves) one file operation with a message.

## Folders and groups

- A group that the user makes is a real folder or a real record, not a part of a name.
- The right-click menu of a group: New folder in this folder, Rename folder, Delete folder. A folder is deleted only
  when it is empty.
- A drag onto a group puts the item into it. A drag under all groups takes it out.

## Known one-offs

Places that do not follow a pattern yet. Each has a reason, or it is work to do. The check of 2026-11-07 found these.

**Lists**

- Tiles grid: no Sort and no View button; its right-click menu has no View items.
- Locations list: no View button (a list only: correct). Layer list: no search, and "Sort by name" is an action, not
  an order.
- Data table, Units list: sort by a click on a column header (correct for a table: a table is not a list).
- Quick picker: its own S, M, L buttons for the view.
- Custom list: its view is one value for all panels; "Date: newest first" uses the command id of "Number: low to high".

**Settings**

- "Many start locations for each player" is in Program options; it is a placement rule (Options > Units).
- "Detailed tiles" is in Program options; it is a value of the terrain tools (tool strip). It is there for now, as the
  user asked (2026-11-07), until the tool strip has a place for the tools' less used values.
- "Animate units" is in the View menu, but it is saved as a preference (correct: it changes what the map shows).

**Modifier keys**

- Alt+click on a Custom list row replaces the whole mask; in the terrain list and the Tiles grid it adds or removes
  one item. No list has the mask in the row's right-click menu yet.
- The second and third palettes do not take Shift ranges.

**Menus and pickers**

- The brush shape box uses shellPopupPick; the Tiles favorites and the Layer list use shellContextMenu; the panel tab
  side menu uses kit::Menu::popup. None of them has "Report feedback".
- Typed numbers in prompts: View > Grid > Custom (four numbers in one field), the new map size, the seconds of an
  establishing shot, a memory address.
- stampsItemMenu (core/stamps.inc) has no callers.

**Buttons and icons**

- The trigger sentences' value brackets (⟨ ⟩) are characters of the UI font. The kit still has
  IconGlyph::ofGlyph and the canvas's glyph swap (setGlyphIcons); no view uses them now.
- Painted buttons: the tool strip, the Layer list's top row, the Data table's bar, the Files rows, the quick picker,
  the New page.

**Messages**

- Direct msgbox::ask in Program options, Simulate mining, updates, the Briefing and the Ramp and bridge mapper.
- A bad typed value: a status message on the New page, an error box in the establishing shot.
- Masks have no sign on the tool strip; "Clear the terrain mask" says "all terrains" also when it clears the tile mask.

**Drag and drop**

- kitui::reorderRow is used only in the gallery. Classic triggers, Layers, Map strings, Units list, Triggers pane,
  Triggers tree, Map tabs and the Custom list have their own drag code, with thresholds of 4 and 6 pixels.
- Doodad pins move with Move up and Move down only.
