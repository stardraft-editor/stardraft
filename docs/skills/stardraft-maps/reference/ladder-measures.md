# The measures of the ladder maps

From the ladder maps that StarCraft: Remastered installs (`<StarCraft>/Maps/ladder`, seasons 2017-2024): 76 map
files, 66 maps. Ranges are the 10th to the 90th percentile; "med" is the median. Per map: `ladder-maps.txt` (layout)
and `ladder-looks.txt` (looks). To measure them again: `scripts/ladder_extract.py`, `scripts/analyze.py`,
`scripts/ladder_summary.py`, `scripts/looks.py`.

## Layout

| Measure | All | 2 players | 4 players |
| --- | --- | --- | --- |
| Size | 128x128 (46 of 66) | 96-128 x 112-128 (16 of 16 not 128x128) | 128x128 |
| Symmetry | half turn 37, mirror 20, diagonal mirror 7, quarter turn 2 | | |
| Bases for each player | 3-6, med 4.1 | 5-6, med 6 | 3-4.8, med 4 |
| Start location to the map's edge | 240-320 px, med 288 | | |
| Main: fields, geysers | 8-10 (med 9), 1 | | |
| Main area (high ground, tiles) | 680-1150 (a main that is not higher than the ground round it: more) | | |
| Main exits | 1 ramp | | |
| Main ramp width | 64-136 px, med 80 | | |
| Start location to its ramp (ground) | 593-1000 px, med 741 | | |
| Natural (ground from the start location) | 879-1193 px, med 1002 | 916-1134 | 879-1193 |
| Natural: fields, geysers | 7 (6-8), 1 | | |
| Natural lower than the main | 34 of 64 (same 20, higher 10) | | |
| Natural's choke | 128-320 px | | |
| Third (ground) | 1493-2493 px, med 1960 | 1630-2493 | 1515-2553 |
| Bases within 3000 px (ground) | 2-5, med 3.5 | | |
| Start location to start location, ground | | 4554-5949, med 5184 | 4104-4735, med 4505 |
| Same, air | | 3360-4651 | 3160-3520 |
| Routes between the mains (at most 1.35 x the shortest) | 1-3 | 1-2 | 1-3 |
| Natural and third: the difference between the players | med 89 px; keep it under 150 | | |
| Walkable part of the map | 65-79 %, med 72 % | | |

## Resources

- Main: 9 fields of 1500 and a geyser of 5000. 58 of the 101 mains with 9 fields use the same arc (offsets from the
  town hall's center, fields at the right): (160, 160), (192, 128), (192, 96), (224, 64), (192, 32), (224, 0),
  (192, -32), (224, -64), (192, -96). The geyser: (0, -176), above the town hall (61 of 66 maps). `place_base` makes
  this layout.
- The fields are at the side away from the map's center (183 of 183 mains), at the left or the right (213 of 252).
- The mirror image of a main is the same way up: the arc and the geyser are not mirrored top to bottom.
- Other bases: 7 fields (343), 8 (149), 6 (110); 1 geyser (544), none (150: about 1 in 5).
- Expansions at the top or the bottom of a town hall happen (about 1 in 5); mains: almost never.
- Behind the mineral line (`backing.py`: from the town hall through each field and on, the distance from the field to
  the first cliff, water, structure or the map's edge): on 50-100 % of a map's bases (median 80 %) it is 96 px or
  less; the median distance is 56-96 px (median 80). Mineral fields are rarely out in the open.

## Looks (all tilesets; Jungle where it is different)

| Measure | All | Jungle |
| --- | --- | --- |
| Terrains used (1 % of the map or more) | 5-9, med 7 | 5-8 |
| Largest terrain's share | 0.29-0.50, med 0.36 | 0.30-0.51 |
| Tiles whose neighbor has another terrain ("changes") | 0.13-0.21, med 0.17 | 0.13-0.23 |
| Patches of terrain per 1000 tiles | 4.3-10.7, med 6.2 | 2.6-10.9 |
| Median patch (tiles) | 6-48, med 16 | 7-93 |
| Cliff edges along the axes / along the tiles' 2:1 diagonals | 0.30 / 0.25 (meds) | 0.28 / 0.25 |
| Doodads per 1000 tiles (maps that keep their doodad list) | 7-24, med 15 | 6-34, med 14 |
| Sprites per 1000 tiles | 0.6-9.5, med 2.9 | 1.2-10, med 3.5 |

How fluid the edges are (`looks.py`; a run: a piece of edge that goes one way):

| Measure | All | Jungle |
| --- | --- | --- |
| Cliff: mean straight run (px) | 67-137, med 86 | 60-204, med 86 |
| Cliff: turns for each 1000 px of edge | 24-35, med 29 | 25-34, med 29 |
| Cliff: px of edge for each tile of the map | 0-3.2, med 2.0 | 0.7-2.6, med 1.9 |
| Walkable ground's edge: mean straight run (px) | 49-83, med 60 | 48-70, med 56 |
| Walkable ground's edge: turns for each 1000 px | 33-44, med 38 | 32-42, med 38 |
| High ground's share of the walkable ground | 0.19-0.89, med 0.43 | |

Walking and sprites (`clutter.py`, `measure.py`; Jungle):

| Measure | Ladder maps |
| --- | --- |
| Narrowest choke in the middle (rush path, past the naturals) | 86-283 px, med 229 |
| Small obstacles (1-8 tiles, walkable ground round them) per 1000 tiles | 0.4-3.2 |
| Walkable ground where a medium unit (32 px) does not fit | 0.6-4 % |
| Pathing regions per 1000 tiles; small regions | 19-31; 0-1 |
| Sprites per 1000 tiles (not doodads' overlays) | 0-10, med about 2 |
| Sprites on ground that is not walkable | 56-90 % (one map: 0 %) |
| Sprites at an edge (64 px or less from ground that is not walkable) | about all |
| Doodads 64 px or less from a cliff or water / in the open (over 128 px) | 39-100 % / 0-51 % |

Jungle's terrains (median share, 90th percentile): Dirt 0.30 (0.37), Jungle 0.24 (0.51), High Dirt 0.14 (0.29),
High Jungle 0.11 (0.17), Water 0.06 (0.22), Temple 0.01 (0.10), Mud 0.01 (0.06), Ruins (0.09), Rocky Ground (0.05).

- The ground is in patches: grass, mud and rocks on the dirt, grass on the high ground.
- Cliffs and shores run along the axes and along the 2:1 diagonals in about equal parts; water is often a river
  along a diagonal, with bridges.
- The doodads are mostly small ground details (2x1 and 2x2 tiles) of the ground's own palette group, densely; a few
  bigger ones. The sprites are the tileset's plants (Jungle: sprites.dat 16-30) in small groups.
