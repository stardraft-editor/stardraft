# Getting started

This page tells how to start with StarDraft 2: find the game, open a map, edit it, and save it.

## Before you start

StarDraft 2 reads the graphics and the data of StarCraft: Remastered from your installation. It does not include
files of the game. When StarDraft 2 cannot find the game, the start page asks for its folder (the folder with the file
`.build.info`). You can change the folder later in Options > Program options (Ctrl+,).

## Open or make a map

- File > Open (Ctrl+O) opens a map (.scm or .scx) or an EUD project.
- File > New project (Ctrl+N) opens the New page: select a classic map or an EUD project, the tileset and the size.
- You can also drag a map file onto the window.

Each open map or project has a tab at the top of the window.

## The workspaces

The bar on the left side of the window has the workspaces of the open tab:

- **Map**: the terrain, the units, the doodads, the sprites and the locations. The modes at the top select what you
  edit.
- **Data**: the game's data (.dat files) of an EUD project. See [the .dat pages](index.md#the-game-s-data-dat-files).
  Entry shows the form of one entry. Table (Ctrl+E) shows the .dat file (or Strings) as a table: a row for each entry, a column for
  each value, and a check box column for each flag. Select entries in the list (Ctrl+click, Shift+click) to show only
  them. Select cells as in a spreadsheet, then type a value: it goes into each selected cell (type +10 or *1.5 to change each number by that step). The
  box at the top filters the columns by name, and Changed shows only the changed columns. Import CSV and Export CSV
  read and write the table as a CSV file: the "id" column gives the entries, and the first row gives the columns.
- **Triggers**: the triggers of the map. Press F1 on a block for its help.
- **Properties** and the other map pages: the title, the players, the forces and the settings.

## EUD projects

An EUD project keeps a map with the changes that the game's data and the triggers make. Build (Ctrl+B) makes the map
that StarCraft plays. File > Project > Make this map a project makes a project from the open map.

## Save

- File > Save (Ctrl+S) saves the map or the project.
- File > Save as (Ctrl+Shift+S) saves the map in a new file.
- Edit > Undo (Ctrl+Z) undoes a change. Edit > Undo history (Ctrl+Shift+H) shows all the changes.

## Help

- Help > Documentation (F1) opens these pages. In the Data workspace, F1 opens the page of the field under the mouse
  or of the field that you clicked last.
- Help > Report feedback sends your feedback to the developers.

## Sources and credits

The StarDraft 2 developers wrote this page.
