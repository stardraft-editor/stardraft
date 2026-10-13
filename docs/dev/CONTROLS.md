# StarDraft's controls

Every control has one look, from the kit in `apps/editor/ui_kit.inc`. A control takes what it needs to fit its
place (a tone, an icon, a width), never its own colors: the colors are the tokens at the top of
`apps/editor/main.cpp`. Native controls (push buttons, check boxes, radio buttons, edit boxes, list boxes) are
painted to the same look by the dark-control subclass (`darkControl`), which calls the kit.

The rules for the UI and its text are in PRINCIPLES.md. This file lists the controls and where each is used.

## Colors

| Token | Use |
|---|---|
| `kBg` | The window. |
| `kPanel` | Side panels: the rail, References, the code pane, painted scroll tracks. |
| `kCtl`, `kCtlHot`, `kCtlDown` | A control's face; under the mouse; pressed. |
| `kReadOnly`, `kReadOnlyLine` | A field that cannot be changed. |
| `kText`, `kDim`, `kFaint` | Text; labels and notes; what cannot be used now. |
| `kBorder`, `kBorderHot`, `kBorderStrong` | A control's 1px line; under the mouse; a check box's box. |
| `kAccent`, `kAccentBright`, `kAccentLine` | Links, the selected item's text, focus; the same under the mouse; the line of a primary button. |
| `kSel`, `kSelHot` | A selected row, a primary button, a toggle that is on; under the mouse. |
| `kRowHot` | A list row under the mouse. |
| `kGood`, `kGoodFill`, `kGoodLine` | Changed from the game's value, or right. |
| `kWarn`, `kBad` | A warning (and a block that is turned off); an error or a removal. |

Every box has the same corner radius, `ui::kRadius` (4 px).

## Scale

The sizes are constants at the top of `apps/editor/main.cpp` (namespace `ui`), so the dialogs made before the kit
use them too. Use these values only.

| What | Size | Constant |
|---|---|---|
| Spacing | 4 / 8 / 12 / 16 / 24 / 32 px | |
| A dialog's edge to its content (all sides) | 16 | `ui::kDialogPad` |
| A list row; a row of a dense form (the Data form) | 26 | `ui::kRowH` |
| A row of a dialog's form (the pitch) | 32 | `ui::kFormRowH` |
| A field, a dropdown, a search box | 24 | `ui::kFieldH` |
| A button | 28 high, at least 88 wide, 8 apart | `ui::kButtonH`, `ui::kButtonMinW`, `ui::kButtonGap` |
| An icon button in a row | 24 | `ui::kIconBox` |

Type: the title 1.5x (`ui::title`); a section heading 1.0x in the text color with a line under it
(`ui::sectionHeader`); a side panel's heading small capitals, dim (`ui::sideGroup`, side panels only); a field label
1.0x, dim (`ui::fieldLabel`); a note small and dim (`ui::smallFont`). No bold: bold text becomes a section heading or
a title. Numbers in fields are right-aligned in the monospace font (the Data form, Unit properties, Map data).

## Which component for which job

| Job | Use |
|---|---|
| A dialog's OK / Cancel (or Create, Send, Generate) | Painted: `ui::buttonsRight`. Native: `ui::nativeButtonsRight` (`ui::dlgButton` finds the buttons). The main action is Primary (the default push button). |
| Buttons from the left (Randomize, New seed) | `ui::nativeButtonsLeft` |
| A question, an error or a note in a box (Save the changes?, Could not open the map) | `themedMessageBox` (message_box.inc), never `MessageBoxW`: the same flags and return values, in the theme. The icon in the theme colors (Error `kBad`, Warning `kWarn`, a question or a note `kAccent`), the text wrapped at 420 px, the kit's buttons at the bottom right. Give the buttons verbs where the box asks a question ("Save", "Don't save", "Cancel"); "&" marks the access key. Enter presses the default button, Esc presses Cancel (or No, or OK). Only the crash handler (crash.inc) keeps `MessageBoxW`. |
| A form row in a dialog | A dim label (a static the dialog colors `kDim`, `SS_CENTERIMAGE`) in one label column, the field 24 high, rows 32 apart. |
| A heading in a main pane or a dialog | `ui::sectionHeader`; native dialogs: a static in `kText` and a 1 px `kBorder` line under it, drawn by the dialog's `WM_PAINT`. |
| A heading in a side panel | `ui::sideGroup` (small capitals) |
| A list group heading (the map palette) | The heading's text color, then a `kBorder` line after the name. |
| A value on a range | `ui::sliderFace`; native trackbars: answer `NM_CUSTOMDRAW` with `ui::trackbarCustomDraw`. |
| A dropdown | `ui::pickBox` (painted); native combo boxes get the same look from `darkControl`; owner-drawn ones (player colors) are drawn in full by `paintOwnerCombo`. |
| A button that opens a menu ("View") | A push button whose name ends in U+25BE: it is drawn with the kit's chevron. |
| A fold | The chevrons (Segoe MDL2 E76C right, E70D down) through `ui::glyph`. |
| A warning or "all right" line | `UiIcon::Warning` in `kWarn`, `UiIcon::Check` in `kGood`, then the text in the same color. |
| A scroll bar | The system one with the theme (`darkControl(w, L"DarkMode_Explorer")`, also for painted pages); `ui::scrollFace` for lists that page by rows. |
| The status bar | Flat: `kPanel`, a hairline on top and between the parts (`paintStatusBar`). The map's parts show in Map only; the message shows in the log line. |

## Controls

| Control | Kit | Where |
|---|---|---|
| Button | `ui::button`, `ui::buttonFace`, `ui::buttonsRight` | Dialogs (Make this map a project, Unit properties, Location properties, rename), the map pages' Apply and Revert, the New page, the start page, pop-outs (value, button, block picker), the Project page, the triggers' empty category ("New trigger", ...), the Data list's Changed/All, the map's View. Tones: Normal; Primary for what a dialog or form is for (OK, Create, New trigger); Danger. Native push buttons: the default button is Primary. |
| Segmented choice | `ui::buttonFace` (the chosen one Primary), 4 px apart | The map's modes in the top tool bar (Terrain, Units, Doodads, Sprites, Locations); Isometric and Rectangle at the top of the Terrain tool strip. |
| Slider | `ui::sliderFace`, `ui::trackbarCustomDraw` | The New page's amounts and shape (Generate). |
| Number box | `ui::fieldBox` and the chevrons (`numberArrows`, `ui::glyph` E70E and E70D) | The tool strip's Size and Density (Terrain, and the sprays of Units and Doodads): the name, then the value in a field with up and down arrows at its right. The arrows and the mouse wheel step through the values (Size: 1, 2, ... 16, then 18 to 32; Density: 1 to 50, in steps of 1); click the value to type a number (Enter or leaving the box sets it, into the range, Size to a whole number; Esc cancels; Up and Down step). Density is the number of things that one spray puts down in the brush (each spray of a held spray too): terrain dabs (Rectangle: tiles), units or doodads. Units and doodads go only where they fit, thus fewer can land; the status bar shows how many. The default value is 5 for Terrain and 10 for Units and Doodads. The Data form's numbers have the same arrows. |
| Link | `ui::link` (`linkButton`) | Only to another entity (PRINCIPLES.md, "Buttons for actions, links for entities"). To change: these are actions and become quiet buttons with sentence case. A command inside text or under a list: "+ action", "+ button", "+ file", "+ new script", "Reset", "Make a list", "Copy a button from a set...". Quiet tone (dim) for second commands ("Rename", "Remove the list") and the triggers' "+ condition" / "+ action". |
| Icon button | `ui::iconButton` (Bare or Boxed), `ui::iconFace` | Boxed: players and tool bars (the image player, the sound player, briefing playback, damage steps). Bare: beside fields (open an entry, another string), in table rows (play). |
| Row actions | `ui::rowActions`, `ui::rowActionRect` + `ui::iconFace` | On the row under the mouse (or the selected one), at its right edge: trigger blocks (turn off, duplicate, delete), briefing actions (the same), buttons of a set (earlier, later, remove), scripts (open in your editor, show in folder, earlier, later, remove; a linked file: Remove link), files in memory, requirements and map sounds (remove), game sounds (add), Classic triggers (triggers: duplicate, delete; conditions and actions: turn off, duplicate, delete). Each has a tooltip. |
| List row | `ui::listRow`, `ui::ownerRow`, `ui::rowText`, `ui::changedDot` | Every list: Data entries, the map palette, the quick picker, the list picker, the block picker, value suggestions, the image player's animations, briefing owners, buttons of a set, sound tables, scripts, the Map data lists, the Classic triggers lists. Selected: `kSel` and a 2 px accent bar; under the mouse: `kRowHot`. |
| Table | `ui::Column`, `ui::tableHeader`, `ui::tableRow`, `ui::tableCell`, `ui::tableText`, `ui::tableFrame` | The Files workspace's sound tables. A header (names dim), rows with hairlines, a frame; columns are fixed widths or share what is left. |
| List that can be reordered | `ui::reorderRow`, `ui::reorderDropLine`; `ui::grip`, `ui::dropLine` | Drag a row by its grip (shown on the row under the mouse); the taken row is tinted and a line shows where it goes. Buttons of a set, scripts on the Project page, Classic triggers (triggers, conditions, actions). Trigger blocks and briefing actions (they move groups and nest) keep their own drag, with the kit's grip and line. Earlier and Later row actions move a row too. |
| Check box | `ui::check`, `ui::checkMark`, `ui::radioMark` | Painted: .dat flags, Build options on the Project page. Native (painted by the kit): Players' force flags, Map data (use default, players), Classic triggers (players, Turn off, Always show), Unit properties' states, Location properties' elevations. Radio marks: the New page's Type and Start. |
| Field | `ui::fieldBox` (Normal, Hot, Focus, Changed, ReadOnly) | Painted values in the Data form, text frames (Strings, briefing text), the Project page's paths; native edit boxes get the same frame. |
| Dropdown | `ui::pickBox`, `ui::pickFace` | Data form references and choices (units.dat Rank/Sublabel: the title from stat_txt.tbl, "Private" to "Admiral"; the Display section's Status panel: the kind of panel), the Buttons tab, the button pop-out, briefing fields, background music. The arrow is a chevron glyph; an entry that can be opened has the open icon beside it. |
| Wireframe preview | `paintWireframePreviews` (data_extras.inc); `Wireframes` (src/wireframes.cpp) | The Units form's Display section: to the right of the three wireframe rows (under them when the form is narrow), three black boxes with a name under each: Wireframe, Group, Transport. Each box shows the picture of the wireframe that its row selects, as the status panel shows it at full health (and full shields when the unit has shields), in the art that View selects (SD, HD or Carbot). When you change the row, the picture changes. A dash: the game has no such wireframe for this unit. The tooltip gives the frame and the file. |
| Section box | A native drop-down list (`gPal.sectionBox`; the Tiles grid's own `gTiles.filterBox`); the mouse wheel over the closed box goes to the next or the previous entry | Above the map palette's search box. Units, Doodads, Sprites: "All units" (doodads, sprites), then the palette's sections. Terrain in Rectangle mode: "All tiles", then the tileset's terrain types (the names of Isometric mode; the terrain types of a tile group come from the tile rules), "Doodad tiles" and "Null tiles". The grid shows only the tiles of that choice, still in one grid with no headings; the search box works together with it. |
| Value chip | `ui::chipFace` | Values in a sentence: trigger blocks, briefing actions, Classic triggers' conditions and actions. Dim when the block is off; red when the value must be fixed; a 2 px accent line when Tab moved to it. Rich-text chips in the text pop-out are drawn by the rich text field (they hold a color and dashes for a player's color). |
| Title | `ui::title` (`formTitle`) | Data entries, the Strings and Buttons tabs, the Project page, the block help page (F1 in the Triggers workspace). Icon, name, and a dim line under it with "changed · Reset". |
| Section heading | `ui::sectionHeader` | Data form sections, Buttons, Files, Project page sections, the block help page. |
| Field label | `ui::fieldLabel` | Every form row: dim; green when the value is changed; faint when it cannot be changed. |
| References side bar | `ui::sidePanel`, `ui::sideHeader`, `ui::sideGroup`, `ui::sideLink` | Data workspace, Files workspace. |
| Scroll bar (painted) | `ui::scrollFace` | The briefing's action list (it pages by rows). Other lists use the system scroll bar, dark. |
| Pop-out | `ui::popupFrame` | The list picker, the quick picker, the value pop-out, the button pop-out: rounded corners and a border from Windows 11. |
| Painted page | `ui::Surface` | A page drawn with the kit: click areas, drags, hover repaint, cursors, tooltips (Files, the Project page, the gallery). |

## The rail and the map pages

The rail (`paintWsBar`) is down the left edge, under the tabs. At the top: the workspaces of the tab's mode (a
project: Map, Data, Triggers, Briefing, Assets, Project; a classic map: Map, Briefing, Assets). Under a hairline: the
map's pages: Properties (Ctrl+M) and Players (Ctrl+P) in every mode; Classic triggers for a classic map, or a project
whose map has them; Map data (the unit, upgrade and tech settings) for a classic map only (never in a project, also
when its map has these settings: the built map keeps them, and the Data workspace is the project's editor). Each item is an icon over its label. The item of the page that shows is
on (`kSel`, the accent, a 3 px accent bar at the left edge); under the mouse: `kHot`. A `ui::changedDot` at the top
right of an item: its page has changes that are not applied.

The map pages (`apps/editor/map_pages.inc`) are workspaces (`Workspace::Properties`, `Players`, `ClassicTriggers`,
`ClassicSettings`) in the main area, as the other workspaces are. No popup windows. The Scenario menu's items and
Ctrl+M and Ctrl+P open them too. A page has three parts:

- *The header*: the page's title in the title font; for Map data the Units, Upgrades and Tech tabs under it
  (icon and name; the tab that shows: `kSel` and a 2 px accent line). A hairline under the header.
- *The content*, which scrolls when the page is smaller than it (the system scroll bars, dark): the form of
  Map properties (`map_properties.inc`), Players and forces (main.cpp), Classic triggers (`classic_triggers.inc`) or
  Map data (`classic_settings.inc`). Native controls (edit boxes, combo boxes, check boxes) are children of the
  page. Their rows use the kit's metrics: fields 24 high, rows 32 apart, `kDialogPad` at the edges.
- *The bar*, which does not scroll: a hairline, then at the left a `ui::changedDot` and "Changes that are not applied"
  (`kDim`) while the page has them; at the bottom right Apply (Primary) and Revert (`ui::button`, 28 high,
  `kButtonGap` apart). Both are off until something changed.

Each page edits a working copy. Apply sets the copy in the map as one undo step and shows the map's values again;
Revert shows the map's values again. The copy stays with its tab: leave the page (another workspace, another tab) with
changes that are not applied, and they are there when you come back; the rail's dot shows them. Closing the tab, or
StarDraft, asks to apply them (Yes, No, Cancel). A copy with no changes reads the map again when the map changes (undo,
another edit). On a page, Tab moves between the fields; the shortcuts that are not the page's own keys still work.

The `mappages:` test run captures each page of the tab's mode (`mappage_<page>.png`, and `_changed` with a change not
applied; `_light` in the light theme) and checks Apply, Revert and leaving the page with changes (mappages.txt).

## The Project page's scripts

The Scripts section (`project_view.inc`) lists the scripts that Build gives to euddraft, in build order: a
`ui::listRow` each, the path in the monospace font, then a `kDim` note. The note tells where the code comes from: "from
the Data workspace" (generated code), "euddraft plugin", or for a script file its last change ("changed 2026-10-11
14:02"). A file outside the project folder is a *linked file* (R5): "linked" starts its note, and its tooltip gives the
full path. A file that is not found: "not found: Build stops" in `kWarn`. A change after the last build (the file is
newer than the built map): "after the last build" in `kAccent`. The page reads the times again when StarDraft becomes
the active program; it does not build by itself. Under the list: "+ new script" and "Link a file…" (`ui::link`). StarDraft
never writes to a linked file: Open in your editor (the system's "edit" verb for the file, else "open", else Notepad)
and Remove link (the file stays) are its row actions.

## New page and start page

Two painted pages (`apps/editor/new_page.inc`), each in a tab of its own. The page covers the area under the tabs (the
rail too); the top tool bar, the tabs, the log and the status bar stay. Commands that act on a map do nothing in
these tabs. The tab holds a small blank map until it becomes a document.

- **New page** (File > New, Ctrl+N; New project, Ctrl+Shift+N; the tab strip's +). Two screens in one page, not a
  wizard. File > New and the + show the cards first; New project and the start page's buttons go to the settings of
  their kind.
  - *The cards*: the title "New project" and the four kinds side by side as large cards, in the middle of the page
    (across, and up and down above the footer). All three are projects, one for each audience (docs/project/ROADMAP.md,
    "Who StarDraft is for"): **Classic project** (a map without EUDs), **EUD project** (data, block triggers, euddraft) and
    **Campaign project** and **Mod project** (gray, kReadOnly and kFaint, with a "Coming later" mark at the top right; they cannot
    be selected). Cards are
    `ui::box` (kCtl, kBorder; under the mouse kCtlHot and kBorderHot; the keyboard's card has a kAccent line). Each card
    has a 32 px icon in kAccent, the name in the title font, and two sentences (kDim) on what the kind lets you make.
    What the kind cannot do (for an EUD project: up to 1,700 units, no save and load, no replays) shows as warning
    lines (`UiIcon::Warning`, kWarn) under the title of its settings, not on the card. Click a card, or Left or Right and
    then Enter, to go to its settings. The footer has only Cancel.
  - *The settings*: a "Back" link to the cards (and Esc), the title (the kind's name) and a dim line with the choices.
    Then the columns, each with a `ui::sectionHeader`. The tab is "New project" on the cards, then "New classic
    project" or "New EUD project". Menu: File > New project (Ctrl+N) shows the cards; File > Project > New EUD project
    (Ctrl+Shift+N) opens the EUD project's settings.
  - *Project* (only for an EUD project): the name, location and output file (native edit boxes, 24 high, the label dim
    above each; Browse… buttons 28 high). Under the location: the folder that StarDraft makes, small and dim; or a
    warning line (`UiIcon::Warning`, kWarn) when the name is wrong or the folder is there and not empty. Then Create
    is off and its tooltip gives the reason. A classic map has no Project column.
  - *Terrain*: the tilesets as list rows (`ui::listRow`) with a picture of each (a small generated map); Width and
    Height as segmented choices of the game's sizes (64, 96, 128, 192, 256); the start terrain (`ui::pickBox`),
    gray with Generate.
  - *Start*: Blank or Generate (radio marks); an EUD project also has Import an existing map: a map file (Browse…), copied into the project folder (the file stays as it is), and then the Terrain column only says that the map has the tileset, size and terrain. With Generate: the preview (a picture of the game: dark in both themes),
    Seed and New seed, Symmetry, Smallest feature (rows on the 32 px pitch), "Amounts" and "Shape" with
    `ui::sliderFace` rows, and the links Randomize and Randomize all. With Blank, all of it is hidden.
  - A footer that does not scroll: a hairline, then Create (Primary, only in the settings) and Cancel at the bottom
    right. Create makes the map or the project in the same tab; Cancel closes the tab (the only tab goes back to the
    start page).
- **Start page** (StarDraft starts with it when "Open the last project on start" is off, or nothing opened). One block
  in the middle of the page (across, and up and down): the title and the version; New project (Primary; it shows the
  New page's cards) and Open… (a project or a map); then "Recent": the recent projects and maps in one list, the newest
  first. Each row has the icon (project or map), the name, the folder (dim, path ellipsis) and the date (dim; "Not
  found" in kWarn, and last). Click a row to open it; its row action removes it from the list. An empty list shows
  "None".

The `new:` (the cards), `new:map`, `new:project`, `new:generate`, `start:` and `start:empty` test runs capture them
(with the theme in the name).

## The map's right-click menu and Location properties

A right-drag moves the map with the mouse. The middle button scrolls it (the farther the mouse from where you pressed,
the faster) or, with Program options > Mouse > Middle button > Drag, moves it with the mouse as a right-drag does. The
left button never moves the map. While you place, paste or paint, a right click stops it and
goes back to Select. In Select, a right click opens the map's menu through `popupMenu()` (`mapMenu`, `mapMenuPick`);
the thing under the mouse is selected first when it is not selected.

| Mode | The thing under the mouse adds |
|---|---|
| Units | Select all of this type; Select all of this type for Player N (its owner); Properties… (Unit properties) |
| Doodads | Select all of this type; Convert to terrain (the selected doodads: their tiles stay as placed tiles, their doodad entries and overlay sprites go; one undo step) |
| Sprites | Select all of this type (the same id, drawn the same way; not the doodads' overlays) |
| Locations | Properties… (Enter), Rename (F2), the six elevations as check items |

Cut, Copy, Paste, Duplicate, Delete, Replace with… and Select all are in every mode's menu. "Select all of this
type" selects in the whole map, in place of the selection, and the status bar says how many it selected
(`Editor::selectUnitsOfType`, `doodadsOfType`, `spritesOfType`).

*Location properties* (a double-click on a location in Locations mode, Enter with one selected, a double-click on its
row in the list, or Properties… in its menu) is a native dialog as Unit properties is: Name; Left, Top, Right and
Bottom in pixels (monospace, right-aligned, two columns); Size (dim: pixels and tiles); the heading "Elevations in the
location" with a hairline, then the six elevations as check boxes (checked: in the location); OK (Primary) and
Cancel at the bottom right. While right is not more than left, or bottom not more than top, a red line at the left
of the buttons says so and OK is off. OK sets all of it as one undo step (`Editor::editLocation`); the edges are
clamped to the map. The Anywhere location is not in the list and keeps the whole map. The self-test captures it
(location_properties.png, _light, _wrong).

## Town areas (View menu)

An overlay on the map helps to judge balance (roadmap R24). It is a check item in the View menu, as Show grid and
Show locations are. It is drawn with the renderer's rects and lines, in the
theme's colors, with transparency (`apps/editor/path_overlay.inc`).

| View menu item | What shows |
|---|---|
| Show town areas | For each start location and each expansion: the buildable tiles near its town hall (green), its resources (orange boxes), and a label with the counts ("P1  392 buildable, 7 minerals, 1 gas"). |

- **The legend** in the view's bottom-left corner tells what each color is, for the overlays that show. The map's
  labels do not show through it.
- **The status bar:** when the overlay goes on, it tells what the colors mean. On a base's box it tells the base's
  counts, with the amounts of minerals and gas.
- **The data** comes from the library (`stardraft/pathing.h`, `analyzePathing`): the walk grid has one cell for each
  8x8 minitile, from the tileset's VF4 walkable flag. Ground buildings and
  resources on the map (UNIT, and the units of THG2) block the cells under their collision box; the start location
  markers and mobile units do not. The paths are A* over the grid (8 directions, no cut corners) for a square of
  the unit's size. A town area is the buildable tiles (CV5) that join the town hall's tiles through buildable tiles
  that no building or resource covers, at most 12 tiles from the town hall. Resources are the units of StarEdit's
  Resources group with 64 or more in them (less: a wall, not a resource) and at most 12 tiles from a start location
  (each goes to the nearest). An expansion is a group of the other resources (each within 7 tiles of the next; a
  geyser by itself joins a mineral line at most 12 tiles from it) with 3 or more mineral fields or a geyser, and a
  place where a town hall can go (4x3 buildable tiles, 3 tiles from every resource) near them.
- **Speed:** the analysis runs on a worker thread on copies of the map's data each time the map changes (the editor's
  revision); the map shows the last result until the new one is ready, so edits do not wait. `tools/tests/pathing_test`
  measures each map of the game's Maps folder: 1 to 25 ms on the 128x128 maps (8 starts: 28 paths), at most about
  85 ms on the 256x256 maps. The labels go on by priority (the one under the mouse, the start locations, the
  counts, the expansions, the path lengths); a label that would cover one before it does not show.
- **Tests:** `pathing_test <install> <maps dir>` checks a ground path on (2)Bottleneck.scm, none on (5)Island Hop.scm,
  the town area counts and the time (`--dump <dir>` writes a picture of each map with the overlays). The self-test
  turns each overlay on, captures it (01b_pathing_*.png) and checks the path, the status bar on it, the town counts,
  and the update after a building is put on the path. `<map> --pathshots <dir>` captures the three overlays together
  in the dark and the light theme.

## The Options menu and the Program options window

The Options menu has the options that people change while they edit a map: the snap and placement rules of each kind,
and the terrain copy. Its last item, after a separator, is "Program options…" (Ctrl+,): the window with the options
of the program. There is no Options item in File.

**The rules of each kind.** Each kind has a submenu with the same items, in the same words. A rule that does not
change for a kind shows grayed, with its value (a check mark or a radio mark) and the reason after a colon. The name
is in `kFaint`; the reason is in `kDim` (`markMenuReason()`), so it is easy to read on the menu and on the row under
the mouse.

| Item | Units | Doodads | Sprites | Locations |
|---|---|---|---|---|
| Do not snap; Snap to tiles (32 px); Snap to half tiles (16 px); Snap to quarter tiles (8 px) | Choice (default: Do not snap) | Always tiles: a doodad is made of tiles | Choice (default: Do not snap) | Choice (default: tiles) |
| Place only where it fits; Move to the nearest place where it fits; Place anywhere | Choice (default: Place only where it fits) | Choice (default: Place only where it fits). Place anywhere does not check the terrain; a doodad stays on the map | Always Place anywhere (on the map) | Always Place anywhere |
| Can be on top of others | On or off (default: off) | Never: a doodad replaces the tiles under it | Always | Always |
| Dragged units go back to the last place where they fit | On or off (default: on; grayed with Place anywhere) | — | — | — |

A snap puts a click in the middle of a grid cell and moves a drag in whole cells. It applies to placing, paste
(also duplicate), drag and the arrow keys (units move one grid cell). Location edges go to the nearest grid line, for
a new location, a move, a resize and a paste. Spray scatters at random and does not snap. The place rule of units
applies to placing, paste, duplicate, drag, the arrow keys and terrain paste (docs/project/ROADMAP.md N.2); the rule of
doodads applies to placing, paste, drag, spray and Replace with. The options are not kept after StarDraft closes.

**The Program options window** (`apps/editor/options_window.inc`, a `ui::Surface`) has the dialog metrics: 16 px
padding, rows 32 apart, fields 24 high, dim labels, the system scroll bar in the theme when it scrolls, and Close
(Primary) at the bottom right.

| Section | Options | Component |
|---|---|---|
| Appearance | Theme (dark, light); accent color; animate units | Segmented choice; color swatches (a ring on the selected one, the name as a tooltip); `ui::check` |
| Start | Open the last project; look for updates (and "Check now") | `ui::check`; `ui::link` |
| StarCraft folder | The folder in use, Browse… | A read-only `ui::fieldBox`; `ui::button` |

A change applies at once, through the same command as the menu (`command(ID_OPT_...)`, `ID_SNAP_...`,
`ID_DOODAD_...`, `ID_THEME_...`), so a shortcut and the tests get the same result. `updateMenus()` sets the marks in
the Options menu and paints the window again after each command, and a theme change paints it in the new theme. The
`options:` test run captures the window in both themes (options_dark.png, options_light.png); the self-test also
captures it (91_options.png, 91_options_light.png) and writes the Options menu as text in its log ("options menu:").

## The gallery

View > Control kit (only in debug builds) shows every control in each of its states, and the controls there work. The `kit:` test run
captures it (kit_1.png, kit_2.png, ...).

## Feedback

`kFeedback` (yellow) and `kFeedbackGlyph` (Segoe MDL2 E939): the menu item "Report feedback on ..." (`feedbackBitmap()`
gives the glyph for menus), Help > Report feedback and the feedback dialog. Use them together, and only for feedback.

## Themes and the accent color

Options > Program options > Appearance: dark (the default) or light, and one of eight accent colors (blue is the default).
`apps/editor/theme.inc` gives every token its value for the choice (`setThemeColors`); `applyTheme` also makes the
brushes again, gives the Windows controls the light or dark Windows themes, and paints every window again. The choice
is kept in the settings.

- Every accent has the same perceived brightness (OKLCH lightness) as the default blue. `kAccentBright`,
  `kAccentLine`, `kAccentFill`, `kSel`, `kSelHot`, `kOnText`, `kChip` and `kChipLine` come from the accent's hue.
- Draw with the tokens only. A color that is not a token must have a value for each theme (as the trigger editor's
  `trigWords()`, `trigContainerFill()`), or be a picture of the game.
- Pictures of the game stay dark in both themes: the briefing preview, game text with its color codes, the image
  and weapon previews, the button card.
- Every text token has a contrast of at least 4.5:1 to the grounds it is drawn on, in both themes and with every
  accent (the self-test checks it; `--datashots <dir> contrast:` writes the table).
