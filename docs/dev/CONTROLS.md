# StarDraft's controls

Every control has one look, from the kit (the widgets in `apps/kit/`, the painted parts in
`apps/editor/core/kit_paint.inc`). A control takes what it needs to fit its place (a tone, an icon, a width), never its
own colors: the colors are the tokens in `apps/editor/core/colors.inc`. Native controls (push buttons, check boxes, radio buttons, edit boxes, list boxes) are
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
| The status bar | Flat: `kPanel`, a hairline on top and between the parts (`StatusView::paint`). The map's parts show in Map only; the message shows in the log line. |

## Controls

| Control | Kit | Where |
|---|---|---|
| Button | `ui::button`, `ui::buttonFace`, `ui::buttonsRight` | Dialogs (Make this map a project, Unit properties, Location properties, rename), the map pages' Apply and Revert, the New page, the start page, pop-outs (value, button, block picker), the Project page, the triggers' empty category ("New trigger", ...), the Data list's Changed/All, the map's View. Tones: Normal; Primary for what a dialog or form is for (OK, Create, New trigger); Danger. Native push buttons: the default button is Primary. |
| Segmented choice | `ui::buttonFace` (the chosen one Primary), 4 px apart | The map's modes in the top tool bar (Terrain, Units, Doodads, Sprites, Locations); Isometric and Rectangle at the top of the Terrain tool strip. |
| Slider | `ui::sliderFace`, `ui::trackbarCustomDraw` | The New page's amounts and shape (Generate). |
| Number box | `ui::fieldBox` and the chevrons (`numberArrows`, `ui::glyph` E70E and E70D) | The tool strip's Size and Density (Terrain, and the sprays of Units and Doodads): the name, then the value in a field with up and down arrows at its right. The arrows and the mouse wheel step through the values (Size: 1, 2, ... 16, then 18 to 32; Density: 1 to 100%, in steps of 5); click the value to type a number (Enter or leaving the box sets it, into the range, Size to a whole number; Esc cancels; Up and Down step). Density is how much of the brush one spray puts down, each spray of a held spray too: that part of the terrain corners, rects or tiles, or one unit, doodad or sprite for that part of its tiles. Units, doodads and sprites go only where they fit, thus fewer can land; the status bar shows how many. The default value is 25% for Terrain and 10% for the others. The Data form's numbers have the same arrows. The simulation bar's mining panel has the same number box (`SimNumberBox`, sdl_mining_panel.inc) for the workers and each boost search's search time. |
| Link | `ui::link` (`linkButton`) | Only to another entity (PRINCIPLES.md, "Buttons for actions, links for entities"). To change: these are actions and become quiet buttons with sentence case. A command inside text or under a list: "+ action", "+ button", "+ file", "+ new script", "Reset", "Make a list", "Copy a button from a set...". Quiet tone (dim) for second commands ("Rename", "Remove the list") and the triggers' "+ condition" / "+ action". |
| Icon button | `ui::iconButton` (Bare or Boxed), `ui::iconFace` | Boxed: players and tool bars (the image player, the sound player, briefing playback, damage steps). Bare: beside fields (open an entry, another string), in table rows (play). |
| Row actions | `ui::rowActions`, `ui::rowActionRect` + `ui::iconFace` | On the row under the mouse (or the selected one), at its right edge: Classic triggers (a trigger: turn off, duplicate, delete; a condition or action: turn off, move out, move in, duplicate, delete; a block: edit the block), trigger blocks (turn off, duplicate, delete), briefing actions (the same), buttons of a set (earlier, later, remove), scripts (open in your editor, show in folder, earlier, later, remove; a linked file: Remove link), files in memory, requirements and map sounds (remove), game sounds (add), Classic triggers (triggers: duplicate, delete; conditions and actions: turn off, duplicate, delete). Each has a tooltip. |
| List row | `ui::listRow`, `ui::ownerRow`, `ui::rowText`, `ui::changedDot` | Every list: Data entries, the map palette, the quick picker, the list picker, the block picker, value suggestions, the image player's animations, briefing owners, buttons of a set, sound tables, scripts, the Map data lists, the Classic triggers lists. Selected: `kSel` and a 2 px accent bar; under the mouse: `kRowHot`. |
| Table | `ui::Column`, `ui::tableHeader`, `ui::tableRow`, `ui::tableCell`, `ui::tableText`, `ui::tableFrame` | The Files workspace's tables (map sounds, music tracks, dot images, game sounds). A header (names dim), rows with hairlines, a frame; columns are fixed widths or share what is left. |
| List that can be reordered | `ui::reorderRow`, `ui::reorderDropLine`; `ui::grip`, `ui::dropLine` | Drag a row by its grip (shown on the row under the mouse); the taken row is tinted and a line shows where it goes. Buttons of a set, scripts on the Project page, Classic triggers (triggers, conditions, actions). Trigger blocks and briefing actions (they move groups and nest) keep their own drag, with the kit's grip and line. Earlier and Later row actions move a row too. |
| Multi-pick list | `showListPickerMulti` (the list picker in multi mode): the searchable list with check marks; OK and Cancel under it | Classic triggers: the values of a block (its dialog: Add values), the forces of a trigger (Forces). A click or Space checks a row and the list stays open; Enter or OK keeps the checks; Esc, Cancel and a click outside keep the old ones; Tab does nothing (the keys in the search box). |
| Check box | `ui::check`, `ui::checkMark`, `ui::radioMark` | Painted: .dat flags, Build options on the Project page. Native (painted by the kit): Players' force flags, Map data (use default, players), Classic triggers (players, Turn off, Always show), Unit properties' states, Location properties' elevations. Radio marks: the New page's Type and Start. |
| Field | `ui::fieldBox` (Normal, Hot, Focus, Changed, ReadOnly) | Painted values in the Data form, the Project page's paths; native edit boxes get the same frame. |
| Game text editor | `gameTextCreate` (`game_text.inc`) | Every text with the game's color codes: the map's title and description, briefing texts, Data > Strings, classic trigger texts. Preview (the text in its colors, lines aligned as the game aligns them) and Raw (codes as `<XX>`); under the box the palette's swatches (in-game, or briefing and scenario), the align buttons and Preview \| Raw. A text that is not changed keeps its bytes. The trigger text pop-out (Display text, objectives, transmissions, leaderboard labels, player names, unit names, button tooltips) has the same swatches, align buttons and Preview \| Raw. |
| Dropdown | `ui::pickBox`, `ui::pickFace` | Data form references and choices (units.dat Rank/Sublabel: the title from stat_txt.tbl, "Private" to "Admiral"; the Display section's Status panel: the kind of panel), the Buttons tab, the button pop-out, briefing fields, background music. The arrow is a chevron glyph; an entry that can be opened has the open icon beside it. A list with a search row and `onNumber` (Map properties: each player's Type, Race and Color) also takes a number from 0 to 255: type it in the open list, then press Enter. A number that no item has is a row of its own, Unknown, with the number at its right; the field then shows that row. |
| Wireframe preview | `paintWireframePreviews` (data_extras.inc); `Wireframes` (src/wireframes.cpp) | The Units form's Display section: to the right of the three wireframe rows (under them when the form is narrow), three black boxes with a name under each: Wireframe, Group, Transport. Each box shows the picture of the wireframe that its row selects, as the status panel shows it at full health (and full shields when the unit has shields), in the art that View selects (SD, HD or Carbot). When you change the row, the picture changes. A dash: the game has no such wireframe for this unit. The tooltip gives the frame and the file. |
| Section box | A native drop-down list (`gPal.sectionBox`; the Tiles grid's own `gTiles.filterBox`); the mouse wheel over the closed box goes to the next or the previous entry | Above the map palette's search box. Units, Doodads, Sprites: "All units" (doodads, sprites), then the palette's sections. Terrain in Rectangle mode: "All tiles", then the tileset's terrain types (the names of Isometric mode; the terrain types of a tile group come from the tile rules), "Doodad tiles" and "Null tiles". The grid shows only the tiles of that choice, still in one grid with no headings; the search box works together with it. |
| Add row | `ui::addRow` (with `enabled`) | Under a list: Add a trigger, Add a condition, Add an action (the actions; the blocks first only when new blocks are on, `kClassicBatchesOffered`), Add values. At the limit it is gray, with no click; its tooltip gives the reason. |
| Value chip | `ui::chipFace` | Values in a sentence: trigger blocks, briefing actions, Classic triggers' conditions and actions (a batch's variable: `variable "unit"`). Dim when the block is off; red when the value must be fixed; a 2 px accent line when Tab moved to it. Rich-text chips in the text pop-out are drawn by the rich text field (they hold a color and dashes for a player's color). |
| Badge | `kit::Badge` (apps/kit/badge.h) | A state in a small box, kRadius corners, as wide as its text, at the left or the right of its frame: Neutral (kChipDim, kBorder, kDim), Good (kGoodFill, kGoodLine, kGood), Warn (kChipDim, kWarn), Bad (kBadFill, kBad). The text says the state; the tone repeats it. File > Import a project: Found, Not found, Included, From the build folder, Not imported: no build folder, Left out, Not supported. Not a value chip: it takes no input. |
| Card | `kit::Card` (apps/kit/card.h) | A box in kPanel with a 1 px kBorder and kRadius corners, under a group of controls that belong together (its children, or views at its place added after it). File > Import a project: the step rail, the file, the new project, the map, the build folder, each part, the summary's counts. |
| Step box | `optionsview::StepBox`, `optionsview::paintStepBox` (core/views_options.inc) | A number with a - button at the box's left end and a + button at its right end (the signs as the simulation bar's speed has them); a click on one, or the mouse wheel on the box, steps the value by 1 within its range; a sign at its end of the range is faint. Program options > Simulation. |
| Stepper | `kit::Stepper` (apps/kit/stepper.h) | The steps of a dialog that goes step by step, one under the other (`kit::kStepH`, 60 px): a 26 px circle, the name, a dim note. Done: a check mark (the check box's mark) on kAccentFill; the step that shows: its number on kAccentFill with a kAccent line; later: its number in kDim in a kBorderStrong circle; kAccentFill or kBorder lines join them. A click on a done step goes back to it (onSelect); it takes no keys (the dialog's Back and Next do). File > Import a project. |
| Title | `ui::title` (`formTitle`) | Data entries, the Strings and Buttons tabs, the Project page, the block help page (F1 in the Triggers workspace). Icon, name, and a dim line under it with "changed · Reset". |
| Section heading | `ui::sectionHeader` | Data form sections, Buttons, Files, Project page sections, the block help page. |
| Field label | `ui::fieldLabel` | Every form row: dim; green when the value is changed; faint when it cannot be changed. |
| References side bar | `ui::sidePanel`, `ui::sideHeader`, `ui::sideGroup`, `ui::sideLink` | Data workspace, Files workspace. |
| Scroll bar (painted) | `ui::scrollFace` | The briefing's action list (it pages by rows). Other lists use the system scroll bar, dark. |
| Pop-out | `ui::popupFrame` | The list picker (and its multi-pick mode), the quick picker, the value pop-out, the button pop-out: rounded corners and a border from Windows 11. |
| Painted page | `ui::Surface` | A page drawn with the kit: click areas, drags, hover repaint, cursors, tooltips, right-click menus (`DataWidgetHit::context`: the row's menu) (Files, the Project page, the gallery, Classic triggers). |

**The keyboard focus.** Tab and Shift+Tab go to each part that a click uses, in the order of the screen. In the main
window: the menu bar, the top bar (its icons, the modes, Build, Debug mode, Update, Report feedback), the document tabs
(each tab, its x, the +), the rail, the left side bar from the top down (the minimap, the panel tabs, the tool strip, the
boxes, the list), the map, the right side bar, the Log's bar. Enter and Space press the part with the focus; the arrow
keys move in a strip or a list; Esc gives the keyboard back to the map. A part that a view paints itself shows the focus
with a 2 px accent outline (`core/item_focus.inc`: `ItemFocus`, `PartStop`; `kitui::Surface` gives its click areas the
same keys). The outline shows only when the keyboard moved the focus, not after a click. Each new view with click areas
of its own does the same: `View::tabbedTo`, `tabWithin`, `focusOnClick` (`apps/common/view.h`).

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

## The Assets workspace's music tracks and dot images

In an EUD project, the Assets workspace (`core/views_files.inc`) has two more tables between Map sounds and Game sounds,
each under its section heading. Their lists are the project's (`media.json`, `core/media_model.inc`), and the blocks
Play music track and Draw dot image select from them.

- *Music*: the same columns as the sound tables (Play, name, kind, length, size); the remove cross on the row under the
  mouse. Under the table: Add music… (Normal) and, with a track selected, Rename (Quiet). File > Project > Add music…
  runs the same command.
- *Dot images*: a taller row with a preview of the picture in EUD Editor 3's 21 dot colors (white is the background:
  the row shows through), the name, the size (16 x 12) and the remove cross. Under the table: Add dot images… and
  Rename. File > Project > Add dot images… runs the same command.
- An empty table shows None. F2 renames the selected item, Del removes it, Space plays the selected track, a double
  click plays a track or renames a dot image. Each edit is one undo step of the lists: in the Assets workspace, Undo
  takes the newest step, the lists' or the map's.

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
    Seed and New seed, Symmetry (a kit dropdown), Smallest feature (rows on the 32 px pitch), "Amounts" and "Shape" with
    `ui::sliderFace` rows, and the links Randomize and Randomize all (each quick click randomizes: a double click is a click too; the preview is made again once, for the last values). With Blank, all of it is hidden.
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

## Import a project

File > Import a project… (`core/views_import_project.inc`, over `stardraft/project_import.h`): a kit Dialog, 880 x 584,
that makes a new EUD project from an EUD Editor 3 (.e3s), EUD Editor 2 (.e2s) or euddraft (.eds) project.

- *The rail* at the left: a `kit::Card` with a `kit::Stepper` of the four steps (Project, Map, Parts, Summary); a click
  on a done step goes back to it.
- *The step*: its title (bold, 20 px) and a dim line under it, then `kit::Card`s. Project: the
  file's card (its name, its editor and version in kGood or why it does not read in kBad, its folder in kFaint;
  Select… or Change…) and the New project card (name, location). Map: the map's card (a Found or Not found badge, and why Next is off;
  the output map as two radio buttons) and, only when a part of the file needs it, the old editor's build folder.
  Parts: a scrolled list with a card per part: its icon, a check box (Clear it to leave the part out), what it holds
  (dim) and a `kit::Badge` at the right (Included, From the build folder, Not imported: no build folder, Left out, Not supported);
  under Scripts, when there is a build folder, "Scripts from" as two radio buttons. Summary: three counts in cards
  (parts to import, parts not imported, things to do), "Select the build folder…" (back to step 2) when a part needs
  it, then the lists under their headings (To import, Not imported, To do), each line a `kit::Label` with an icon
  (`Check`, `Remove`, `Warning`); the new project's name and folder come first.
- The keyboard: each step puts the focus on its first control (a field of a hidden step takes no keys).
- The dialog's buttons: Back (left), Next (Import on the last step; off until the step is complete: a file that reads
  and a free project name, then a map) and Cancel. Back and Next are step buttons: they do not close the dialog
  (`kit::DialogButton::step`, `Dialog::stepped`).
- Import makes the project, opens it, writes `notes/Import report.txt` (what came in and what did not), and the status
  bar says so.

The `dialog-import-1` to `-4` and `import-project` snapshot cases capture it (`--dialog import:<file>|<step>[|<build
folder>]`, `--importproject <file>|<folder>`).

## The map's right-click menu and Location properties

Only the middle button moves the map: it scrolls it (the farther the mouse from where you pressed, the faster) or, with
Program options > Mouse > Middle mouse click and hold > Drag map, moves it with the mouse. The left and the right button
never move the map (2026-10-15: the right button is for clicks). While you place, paste or paint, a right click stops it and
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
clamped to the map. The Anywhere location is not in the list and keeps the whole map. The snapshot case
map-dialog-locprops (`tools/sdl_snapshots.py`) captures it.

## Town areas (View menu)

An overlay on the map helps to judge balance (roadmap R24). It is a check item in the View menu, as Show grid and
Show locations are. It is drawn with the renderer's rects and lines, in the
theme's colors, with transparency (`apps/editor/path_overlay.inc`).

| View menu item | What shows |
|---|---|
| Show town areas | Each resource area in its own color: the buildable tiles near its town hall, its resources (boxes, with a line from the area's position to each), the area's position (a square), and a label with the name and the counts ("P1, area 1" and "7 minerals, 1 gas, 392 tiles"). |

- **The status bar** tells what each color is, for the overlays that show (there is no legend on the map). The
  labels are in the UI font (dark boxes, the same size at each zoom).
- **The status bar:** when the overlay goes on, it tells what the colors mean. On a base's box it tells the base's
  counts, with the amounts of minerals and gas.
- **The data** comes from the library (`stardraft/pathing.h`, `analyzePathing`): the walk grid has one cell for each
  8x8 minitile, from the tileset's VF4 walkable flag. Ground buildings and
  resources on the map (UNIT, and the units of THG2) block the cells under their collision box; the start location
  markers and mobile units do not. The paths are A* over the grid (8 directions, no cut corners) for a square of
  the unit's size. A town area is the buildable tiles (CV5) that join the town hall's tiles through buildable tiles
  that no building or resource covers, at most 12 tiles from the town hall. The bases are the game's resource areas
  (the areas that a computer player makes its towns at), each with its number. The start locations of players 1 to 8
  are areas 1 and up: a resource goes to the first of them that is at most 640 px away (a geyser: 896 px), on the
  same ground level and a ground walk of at most 20 tiles away (a geyser: 28). Then the other geysers, then the
  other mineral fields, make areas: the one with the most resources near it (by their amounts) makes an area round
  the mineral field nearest the middle of those resources; the area takes all the resources near that field that
  have no area, and its position is the middle of the box round them. Each area shows (there is no minimum of
  resources). The town hall of such an area is the place where one can go (4x3 buildable tiles, 3 tiles from every
  resource) nearest its resources; an area with no such place shows at its position. The ground walk is by the walk
  grid (the terrain only), not by the game's path regions: a resource at the limit of the walk can be in another
  area than in the game.
- **Speed:** the analysis runs on a worker thread on copies of the map's data each time the map changes (the editor's
  revision); the map shows the last result until the new one is ready, so edits do not wait. `tools/tests/pathing_test`
  measures each map of the game's Maps folder: less than 50 ms on the 128x128 maps (8 starts: 28 paths), at most
  about 120 ms on the 256x256 maps. The labels go on by priority (the one under the mouse, the start locations, the
  expansions, the path lengths); a label that would cover one before it does not show.
- **Tests:** `pathing_test <install> <maps dir>` checks a ground path on (2)Bottleneck.scm, none on (5)Island Hop.scm,
  the town area counts and the time (`--dump <dir>` writes a picture of each map with the overlays). The snapshot case
  map-view-towns turns the town areas on and captures them. `<map> --pathshots <dir>` captures the three overlays together
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
| Snap to the grid (the grid of View > Grid) | On or off (default: off) | Always tiles: a doodad is made of tiles | On or off (default: off) | On or off (default: on) |
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

The window has seven tabs (`optionsview::kTabs`, core/views_options.inc), each with its sections:

| Tab | Sections | Components |
|---|---|---|
| General | Start; Language and sound; Reset | Check boxes; a drop-down; a button |
| Appearance | Appearance (theme, accent color, unit icons, Show key hints); Graphics and diagnostics | Segmented choice; color swatches (a ring on the selected one, the name as a tooltip); radio rows |
| Mouse and keyboard | Mouse; Keyboard | Drop-downs and check boxes |
| Map | Unit names; Map unit limit; Saving maps | Radio rows; a number field (right-aligned, the heading names the unit); check boxes |
| Plugins | Plugins (not in release builds) | A list with check boxes |
| Folders | Working folder; StarCraft folder | A read-only field and Browse… (a button) for each folder |
| Updates | Updates and crash reports | Check boxes, radio rows and "Check now" (a button) |

A change applies at once, through the same command as the menu (`command(ID_OPT_...)`, `ID_SNAP_...`,
`ID_DOODAD_...`, `ID_THEME_...`), so a shortcut and the tests get the same result. `updateMenus()` sets the marks in
the Options menu and paints the window again after each command, and a theme change paints it in the new theme. The
snapshot cases dialog-options and dialog-options-light capture the window in both themes, and dialog-options-tab-<name>
captures each tab.

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
  accent.
