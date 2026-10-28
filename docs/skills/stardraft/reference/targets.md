# 1v1 targets

From the 2-player ladder maps that StarCraft: Remastered installs (2017-2024; ranges are the 10th to the 90th
percentile) and a melee mappers' checklist. A tile is 32 px. "measure.py" rows are checked by the script; the other
rows by `get <map> walk` (one character per 8 px: `0`-`2` walkable ground of that level, `#` not walkable) or a
close `render`.

## Layout (measure.py)

| Measure | Target |
| --- | --- |
| Size | 128x112 or 112x128 (96-128 x 112-128; not 128x128) |
| Symmetry | a half turn (the ladder: half turn, else a mirror) |
| Bases for each player | 5-6 |
| Geysers on the map | 10-12 |
| Walkable part of the map | 65-79 % |
| Start location to the map's edge | 240-320 px (288: the main's fields 1-2 tiles from the edge) |
| Main | 9 fields of 1500, 1 geyser of 5000 above or left of the town hall; fields at the left or right, away from the map's center, never below |
| Main area (high ground) | 680-1150 tiles; one exit, a ramp of 64-136 px |
| Start location to its ramp (ground) | 593-1000 px |
| Natural (ground from the start location) | 916-1134 px; 7 fields, 1 geyser; lower than or level with the main |
| Natural's choke | 128-320 px; the checklist: 7-8 tiles (224-256 px), along an axis, not diagonal |
| Third (ground) | 1630-2493 px |
| Start to start, ground | 4554-5949 px; the checklist: 32-38 s (4640-5510 px at about 145 px a second) |
| Start to start, air | 3360-4651 px |
| Narrowest choke in the middle | 128-320 px (ladder median 229) |
| Routes between the mains (at most 1.35 x the shortest) | 1-2 |
| Fairness | each base and its image within 150 px of ground distance ("base pair spread"); the natural's chokes the same width on both sides |

## Bases and chokes (get, render)

| Measure | Target |
| --- | --- |
| Other bases | mostly 7 fields (6-8), 1 geyser; about 1 in 5 with no gas |
| Behind the fields | cliff, water or the map's edge within 96 px on about 80 % of the bases (`backing.py`); the natural: 1 tile or more of ground, not a cliff right against the fields |
| Main | in a corner; vision blockers not right below its cliffs |
| Natural's choke | starts 4-6 tiles from the town hall, 5-6 tiles deep; its center not buildable |
| Overlord spots | ground that is not walkable (water, a mesa with no ramp) of 8x6 tiles or more near each natural's choke and near the middle |
| Pocket base | for each player, one base with one entrance of 128-192 px (2-3 pylons) |
| Zealot holes | 1 tile of ground behind the fields of a possible third; no more than 2 such bases for each player |
| Middle | mostly flat and buildable; no choke under 128 px; doodads only at edges |
| Pathing | `get <map> pathing`: only pockets and small regions, none at the main's or the natural's choke |

## Looks, Jungle (looks.py, clutter.py)

| Measure | Target |
| --- | --- |
| Terrains (1 % of the map or more) | 5-8; the largest 30-51 % |
| Jungle's terrains (median share) | Dirt 30 %, Jungle 24 %, High Dirt 14 %, High Jungle 11 %, Water 6 %, Mud, Rocky Ground |
| Tiles with a neighbor of another terrain | 13-23 % |
| Patches of terrain per 1000 tiles | 3-11 (median patch 16 tiles) |
| Cliff edges along the axes / along the 2:1 diagonals | about 0.28 / 0.25 |
| Cliff: mean straight run; turns for each 1000 px | 60-204 px (med 86); 25-34 |
| Doodads per 1000 tiles | 6-34 (med 14); mostly 2x1 and 2x2 ground details, 39-100 % within 64 px of a cliff or water |
| Sprites per 1000 tiles | 1.2-10 (med 3.5); on unwalkable ground at a cliff or shore; Jungle's plants: sprites.dat 16-30 |
| Small obstacles (1-8 tiles in the open) per 1000 tiles | 0.4-3.2 |
