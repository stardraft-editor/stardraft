# Cosmonarchy BW: isometric terrains from the mod's brush sets

The mod's SCMDraft brush sets (Starcraft\Maps\CMBW-Root\CMBW-editor\brush\Cosmonarchy-<tileset>.scmdbrh) are the
mod's terrain edges. On a Cosmonarchy tab each usable set is a terrain of its own in the isometric terrain list:

- Engine: a per-tab terrain table (IsomTerrainTable::withExtras, made in Editor::reload from Tileset::modTerrains). Each
  mod terrain has its own ISOM values past the game's shapes (15 each: solid, then its border with its ground) and a
  template: the game terrain whose tiles its pieces replace. Cosmonarchy tabs use the editor's own engine (IsomGrid);
  the terrain core makes the tiles of a copy of the map with each rect's template, then IsomGrid::putPieces puts the
  set's pieces in. Classic tabs: no change.
- The map file keeps the mod's values (SCMDraft does not know them; vanilla regions work as usual).
- Tests: tools/tests/cmbw_test.cpp (needs the Cosmonarchy install; not in CI).

## How to add a mod terrain

The mod keeps adding brush sets and tilesets. Each set becomes a row in src/mods/cmbw.cpp kModTerrainSets; no engine
change is needed for the kinds below. Steps:

1. List the tileset's sets: `cmbw_brushes <Prerelease> <era>` (era: 0 Badlands, 1 Platform, 2 Installation, 3 Ash World,
   4 Jungle, 5 Desert, 6 Ice, 7 Twilight). (Names: "dirt1/2/3" are Dirt, High Dirt and the third level.)
2. Read a set's blocks: `cmbw_brushes <Prerelease> <era> "_<Set>_" x.png > b.txt`, then `python tools/cmbw_blocks.py b.txt`
   (each block's groups per row and its variants' subtiles). Named sets: W/E north (2 rows), south,
   outer (3 rows), inner (2 rows). Unnamed sets: the same blocks in their brushes' order. In the cmbw_brushes text a
   brush's grid comes before its name line. A set can pack two blocks in one group (W in subtiles 0-4, E in 6-10:
   the block's sub field), and a set's last brushes can be its fill.
3. Find the game's border with the same layout: `cmbw_rect_shapes <Prerelease> <era> <first> <last>` prints each game
   group's rects and corner terrains. Known templates:
   - Flat edges: Ash World Dirt/Lava 192-222 (+48: High Dirt/High Lava 240-270); jungle layout 228-258 (on High Dirt),
     276-298 (on Dirt); Badlands Dirt/Grass 188-218, High Dirt/High Grass 236-266, Dirt/Asphalt 312-342 (one layout).
   - Walls: Badlands Asphalt/Structure 360-398 (Dirt/High Dirt 44-82 +316, each pair's groups the other way round).
   - Cliffs (Dirt/High Dirt): Ash World 120-158 (W north 120, E north 124, W south 128-132, E south 134-138, W outer
     140-144, E outer 146-150, W inner 152/154, E inner 156/158); Badlands 44-90 (46/44, 50/48, 58-62, 52-56, 64-68,
     70-74, 80/82, 76/78); jungle layout 36-74 (38/36, 42/40, 44-48, 50-54, 56-60, 62-66, E inner 68/70, W inner 72/74);
     Platform High Platform cliff 230-268; Ash World Dirt/Magma 48-86.
4. Pick the kind (the row's fields: era, name, stand-in = the template terrain, ground, variants, blocks, fill,
   groundTemplate, groundMod):
   - X on a game ground: stand-in = the game terrain whose border has the layout; fill = the set's fill group or a game
     terrain's tiles.
   - X at the foot of a cliff ("Lava at High Dirt cliffs"): stand-in = the cliff's low terrain, ground = the high one.
   - A third height ("Higher Dirt"): stand-in = High Dirt, ground High Dirt, groundTemplate = Dirt (the ground read a
     level lower); fill = the set's own ground if it has one (a "main" block), else 0 with fillTerrain High Dirt.
   - X on another mod terrain: groundMod = its name (groundTemplate: that one's template, or given).
   Never link two terrains directly beside a third (triangles break the engine): each set is its own terrain.
5. Paint one stroke: `cmbw_paint_one <Prerelease> <map of that tileset> "<name>" 4 out.png`, and look at the picture.
   Test maps per tileset: `cmbw_map_eras <CMBW-maps>`.
6. Fix what the picture shows. Seen often: a set names its two south blocks (or inner corners) the other way round
   (a wall that steps or breaks: swap them); some variants do not line up (use the first only: the block's variants
   field 1); a fill group paired with the next one may be a wall tile (the mod's groups are single textures: pair
   two floor groups, check with cmbw_sheet's mean colors); a set drawn on a lighter ground than the game's tiles under it shows squares (not added; noted in the code).
   A set whose edge rects show its own terrain on the outside is inside-out: its blocks go the other way (north to
   south, outer to inner), or it is the other terrain on top (look where the maps put it: `cmbw_group_maps <CMBW-maps>
   <era> <first-last>`, then `cmbw_tile_at <Prerelease> <map> <x> <y>`). A fill whose subtiles are a pattern's parts:
   the row's fillSubtiles. A new row goes after the last row of its tileset (the ids count in order; saved maps keep them).
   When stuck, look at the maps for the combos they use before guessing.
   Shapes a brush file leaves out (thin, single-diamond and concave rects: the null tile) can be in the set's next
   groups as recolored copies of the game's rects: `cmbw_group_match <Prerelease> <map> <mod first-last> <game
   first-last>` gives the game tile each mod tile looks like most (near 1: a copy).
8. After adding or changing terrains: `cmbw_neighbors <CMBW-maps> --inc src/mods/cmbw_neighbors.inc <Prerelease>` again.
   It writes which of the mod terrains' edge and wall pieces stand beside each other on the maps (under, and a rect to
   the right a row down or up); the brush takes only pieces that the maps have beside the ones around them, so a
   wall's gates and panels come out as the maps have them (Tileset::pieceNeighbors, IsomGrid::pickPiece).
9. A new terrain's line goes into tools/tests/cmbw_pinned_ids.inc (cmbw_test prints it). cmbw_test fails when a terrain's
   id moves (saved maps keep the ids), when a block gives no pieces (the mod renumbered its groups?) and when the
   neighbor table is not the one the maps give now.
7. `cmbw_test <Prerelease> <map> <new empty folder>` on that tileset's map (each mod terrain: its own pieces only, saved
   with its own ISOM values), delete the folder after (a run writes about 1 GB). Commit.

The palette shows each mod terrain with its fill pieces (src/previews.cpp terrainPicture).

## After each update of the mod

1. `cmbw_set_status <Prerelease> -1 <CMBW-maps>`: the brush sets that are no terrain yet ("-"). A new set: make its row
   (the steps above), or add it to kLeftOut (tools/probes/cmbw_set_status.cpp) with the reason in its row's place in
   src/mods/cmbw.cpp and in "Not added" below.
2. `cmbw_test` on each tileset's map and `cmbw_stroke_bench <Prerelease> <map> --all` (the slowest stroke beside each
   terrain).
3. The report for the mod's team: `python tools/cmbw_report.py --install <Prerelease> --bin build/Release --out
   <stardraft-editor.github.io>/cosmonarchy --raw <a new empty folder>`, then commit the folder in the website repository
   (it is served at https://stardraft-editor.github.io/cosmonarchy/). For each terrain: the game border it is made as,
   its required pieces (the rects most sets of that border have: edges, outer and inner corners) and the missing ones,
   its variations, and the extra rects it lacks (thin, single and concave shapes: the game's tiles to draw); and the sets
   that are no terrain yet. The probe behind it: tools/probes/cmbw_report.cpp.

## Done by tileset (update this list with each new terrain)
Names: "X (Y)", the tile X on, above or at Y (the ground it stands on); in a Cosmonarchy tab the game's terrains too
(High Dirt (Dirt), Grass (Dirt)). Notes on each terrain (its layout, its known flaws) are in its row's comment in the code.

- Ash World 24: Asphalt (Dirt), Structure (Dirt), Crushed Rock (Dirt), High Crushed Rock (High Dirt), High Lava (High Dirt), Cooled Lava (Dirt), Lava (High Dirt cliffs), Higher Dirt (High Dirt), Crushed Rock (High Dirt cliffs), Higher Crushed Rock (Higher Dirt), Higher Lava (Higher Dirt), Structure (Asphalt), Flagstones (Crushed Rock), High Flagstones (Crushed Rock), Higher Flagstones (Crushed Rock), Lava (Magma), Crushed Rock (Magma), Basilica (Crushed Rock), High Basilica (Crushed Rock), Basilica (Basilica), Basilica (Flagstones), Highest Dirt (Higher Dirt), Higher Shale (Higher Dirt), Higher Basilica (Higher Crushed Rock).
- Badlands 34: Structure (Dirt), High Asphalt (High Dirt), Asphalt (Grass), High Asphalt (High Grass), Asphalt (Mud), High Structure (High Dirt), Structure (Structure), Asphalt (Water), Substructure (Dirt), Floor (Substructure), Roof (Floor), Plating (Floor), Plating (Substructure), Plating 2 (Substructure), Floor (Asphalt), Floor (Dirt), High Floor (High Dirt), Jungle (Dirt), Substructure (Jungle), Caution (Substructure), Halfsphalt (Substructure), Structure (Halfsphalt), Plating (Roof), Halfsphalt (Structure), Jungle (Floor), High Floor (Asphalt (Water)), High Floor (High Asphalt (High Dirt)), High Floor (High Asphalt (High Grass)), High Dirt (Mud), High Dirt (Grass), Higher Dirt (High Dirt), Highest Dirt (Higher Dirt), Basilica (Dirt), High Basilica (High Dirt).
- Desert 19: High Dried Mud (High Dirt), Asphalt (Dirt), High Asphalt (High Dirt), Cooled Lava (Dirt), High Cooled Lava (High Dirt), Compound (Dirt), Higher Dirt (High Dirt), Higher Asphalt (Higher Dirt), Higher Dried Mud (Higher Dirt), Higher Sand Dunes (Higher Dirt), High Compound (High Dirt), Compound (Compound), Higher Dirt (High Sand Dunes), Higher Dirt Cliff (Higher Dirt), High Dirt (Sand Dunes), Tar (Sand Dunes cliffs), Structure (Asphalt), Structure (Structure), Dried Mud (Sandy Sunken Pit).
- Ice 26: Outpost (Snow), Asphalt (Snow), High Asphalt (High Snow), Mud (Dirt), High Mud (High Dirt), Mud (Grass), High Mud (High Grass), High Outpost (High Snow), Outpost (Outpost), Outpost (Water), Higher Snow (High Snow), Water (Snow cliffs), Outpost (Grass), High Outpost (High Grass), Submerged Outpost (Water), Mesh (Outpost), High Mesh (High Outpost), Snow (High Dirt cliffs), High Outpost (Mesh), High Moguls (High Snow), Higher Moguls (Higher Snow), Dirt (High Dirt cliffs), Higher Outpost (Mesh), High Basilica (High Snow), Basilica (Snow), Higher Basilica (Higher Snow).
- Jungle 30: Temple (Dirt), High Temple (High Dirt), Temple (Temple), Temple (Ruins), Asphalt (Dirt), High Asphalt (High Dirt), Grass (Dirt), High Grass (High Dirt), Mud (Grass), Water (High Dirt cliffs), Jungle (High Dirt cliffs), Dirt (Raised Jungle), Mud (Jungle), Mud (Raised Jungle), High Mud (High Dirt), Structure (Asphalt), Structure (Structure), Raised Jungle (Raised Jungle), Temple (Raised Jungle), Higher Dirt (Dirt), Higher Grass (Higher Dirt), Asphalt (Jungle), Asphalt (Grass), High Asphalt (High Grass), Higher Mud (Higher Dirt), Structure (Grass), Higher Asphalt (Higher Dirt), Water (Higher Dirt cliffs), Water (Asphalt), Higher Dirt 2 (High Dirt).
- Platform 15: Platform Cliff (Low Platform), Platform (Plating), Higher Platform (High Platform), Low Platform (Space), Dirt (Space), Low Dirt (Space), Platform (Low Dirt), Dirt Cliff (Low Dirt), High Dirt Cliff (Dirt), Plating (Low Platform), Plating (Low Dirt), Plating (Dirt), Catwalk (Space), High Platform (Plating), Higher Dirt Cliff (High Dirt Cliff).
- Twilight 12: High Mud (High Dirt), Higher Dirt (High Dirt), Higher Crushed Rock (Higher Dirt), Higher Mud (Higher Dirt), High Crushed Rock (High Basilica), Crushed Rock (High Dirt cliffs), High Crushed Rock (Higher Dirt cliffs), Basilica (Flagstones), Chasm (Dirt), Sea Wall (Crushed Rock), High Basilica (High Basilica), Basilica (Crushed Rock).
- Not added (reason in the code): Twilight's Flagstones-dirt3 (flagstone patches with ragged edges and no isometric shapes, as the game's Flagstones-dirt1/2: in the Doodads palette), Jungle's RaisedJungle-Edge-Dirt-2 (square patches of grass on dirt, straight edges: not an isometric border), Badlands' Mud-Dirt2-Edge (mud puddles of four sides and no corners, inside High Dirt on the maps), Platform's Organ-Cliff-Flesh-1 (its walls have flesh on both sides, and flesh is no isometric terrain: the maps place it by hand on Platform; the walls are on no map), Ice's Basilica sets (no floor group found); Jungle's Jungle-Rjungle-cliff and -high-cliff: ramps (in the Doodads palette) and more inner corners for the game's own Raised Jungle cliffs, no border of their own. Not isometric borders (Rectangle mode or the Custom stamps): Jungle's Urban-edge* (one row of tiles, a line),
  Overgrown-* (three blocks of decoration), Swamp-* and Mud-Swamp-* (patches of swamp: islands, edges and fill; in the Doodads palette, each piece placed one by one). Not done yet: grounds the game lacks (Badlands grass/mud/urban, Jungle swamp/overgrown/urban, dirt
  on Platform), water coasts, the rest of Jungle's and Ice's sets.

## Checking a map in SCMDraft

tools/scmdraft/open_cm.ahk (AutoHotkey v2) starts SCMDraft with a map and picks the CM profile in its Select Profile
dialog; tools/scmdraft/paint_save.ahk picks a terrain and saves the map under another name (by window messages: the
mouse and the focus stay the user's; SCMDraft does not paint from posted clicks). Errors go to errors.log beside the
scripts. cmbw_map_terrains <install> <map> then shows what StarDraft reads back. Checked 2026-10-25: a map with Asphalt
opened and saved by SCMDraft (CM profile) comes back with the same terrains and mod tiles.

Painting in SCMDraft takes real input (only when the user is away: it moves the mouse and takes the focus):
tools/scmdraft/steps.ahk runs clicks (by screen pixels or by a control's ClassNN), drags and keys, then puts the mouse
back; menus.ahk lists the menus' command ids; save.ahk saves under another name. The terrain to paint is in Windows >
Terrain Palette (command 32771): a window of swatches in the game's brush order (the tree at the left is the doodad
palette). cmbw_map_diff <install> <before> <after> lists the tiles that changed. Checked 2026-10-25: a High Dirt stroke
painted in SCMDraft beside a map's Asphalt keeps all of Asphalt's corners and tiles; SCMDraft tiles again only the hand-placed
tiles beside its stroke, as any isometric stroke does.
