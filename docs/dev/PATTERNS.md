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
| It is a value of the tools that the user changes seldom (Detailed tiles) | Tool strip, under More (a fold at its end) |
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
  "Report feedback on…" at its end. A core view opens it with shellMenu (at the mouse) or shellMenuAt. Do not use
  shellContextMenu or kit::Menu::popup for new menus.
- **A choice of a few values** that the user sees all the time is a dropdown or a segmented control, not a menu.
- **A typed value** (a number, a size, a percent) is a kit text field in its place, with its range and its unit
  beside it. It refuses values out of range. Where there is no place for it, the number form asks for it
  (shellAskNumbers: a row for each number, its range, OK refuses a value out of it). Do not ask for a number in a text
  prompt.
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
- A core view (core/) asks through the shell's helpers (shellAskYesNo, shellErrorBox, shellAskSaveChanges). Only the
  host (the sdl_*.inc and shell_*.inc files) calls msgbox::ask, with buttons named by their actions.

## Masks and filters

A mask or a filter changes what the next edit or the list does. The user must always see it.

- **Set:** Alt+click on the item's row adds it to the mask or removes it (a toggle, the same in each list). The row's
  right-click menu has the same action ("Add to the mask", "Remove from the mask").
- **Show:** while a mask is on, the map shows a chip at its top left with what is in it and a close button. It is on
  the map, not in a panel, so that no list moves under the mouse when the mask starts. The items in the mask have an
  outline in their list.
- **Clear:** the close button of the chip, and the map's right-click menu ("Clear the mask"). The message names what
  was cleared.
- One mask kind for each terrain kind: the terrain mask for Isometric and Rectangle, the tile mask for Tile and Custom.

## Drag and drop

- **One helper:** kitui::ItemDrag (core/kit_surface.inc). A press becomes a drag after 4 pixels; the view captures the
  mouse while it drags; a release outside the view, a lost capture or Esc cancels it.
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

- Tiles grid: no Sort and no View button (kept: each row is a tile group of 16 tiles, in the tileset's order, so it is
  always this grid; its size follows the panel's width).
- Locations list: no View button (a list only: correct). Layer list: no search and no Sort button (kept: a map has a
  few layers, and their order is the order they paint in; "Sort by name" in its menu reorders them, an edit).
- Data table, Units list: sort by a click on a column header (correct for a table: a table is not a list).
- Quick picker: its own S, M, L buttons for the view.

**Settings**

- "Animate units" is in the View menu, but it is saved as a preference (correct: it changes what the map shows).

**Modifier keys**

- Ctrl+click is the eyedropper in Terrain, as in SCMDraft (kept: see "Modifier keys").

**Menus and pickers**

- A memory address in a text prompt (a hex number).

**Buttons and icons**

- The trigger sentences' value brackets (⟨ ⟩) are characters of the UI font. The kit still has
  IconGlyph::ofGlyph and the canvas's glyph swap (setGlyphIcons); no view uses them now.
- Painted buttons: the tool strip, the Layer list's top row, the Data table's bar, the Files rows, the quick picker,
  the New page.

**Messages**

- Direct msgbox::ask in two core views: the Custom isom terrain editor and the Ramp and bridge mapper (mod tools).

**Drag and drop**

- kitui::ItemDrag is used by the Layer list, the Custom list, Map strings, the Units list, Classic triggers and the
  Triggers pane. The kit's own drags (the tree, the tab strip) and the Map tabs have their own code, with the same 4 px.
