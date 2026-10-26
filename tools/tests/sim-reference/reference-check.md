# Reference check set (held out, not for tuning)

Black-box numbers from the reference engine (Brood War 1.16.1 rules), Fastest speed (42 ms a frame). No code or internals. This set is a held-out check: nothing here was used to tune the simulation.

## Setup (as in reference-combat.md)

- Map: (2)Bottleneck.scm. First start location at (3808, 2384).
- Side A centre (3680, 2384) and side B centre (3936, 2384): each side's units are created at its centre (the sides stand 128 px either side of the start location, each moved to the nearest walkable point), and the engine places them at the nearest free spot.
- Each unit gets Attack Move to the other side's centre. The map's own units of players 0 and 1 are removed first.
- Limit 2857 frames (2 minutes at Fastest). A fight that reaches it is a timeout.
- Seed 42 for each detailed block (the engine's value at map load).
- Fight 15 (the choke) uses different centres, given in its section.
- Regression: the same tool gives the reference-combat.md numbers for its first fight (12 Marine vs 20 Zergling: 204 frames, 8 Zerglings alive with 239 HP, the same time series).

## How the seed is varied

For each fight, after the map is loaded and before the units are created, the engine's random state is set to 1, 2, ..., 20 in turn (the map-load value is 42, so seed 42 is the detailed run above). Everything else is identical, so the 20 runs differ only by the seed. The summary line gives how often each side wins, the number of timeouts, and the median, minimum and maximum frames. The per-seed list gives "seed:winner frames" (n = timeout).

## Reading the tables

- Survivors: the units alive at the end of the fight, with the sums of their hit points and shields. Kills are the engine's own attribution (a death counts for the enemy unit that targeted the dead unit last); treat them as approximate.
- Time series: every 24 frames plus the final frame: units alive, the sum of HP plus shields, and the distance between the two sides' centres (the mean position of the living units).
- First shot and first damage: the frame a side's unit first starts a weapon cooldown, and the first frame a unit of that side loses HP or shields (-1: none).

## Choke (fight 15)

- The choke is a line of 6 walkable cells (32 px grid) on the vertical line x = 832 px, from y = 3872 to y = 4032 (grid cells (26, 121) to (26, 126)). With those cells blocked, the walkable ground splits into the large side (about 10,700 cells) and a 20-cell pocket behind the line; checked: the two centres do not connect without the line.
- Side A centre: (736, 3808), on the large side. Side B centre: (928, 3968), inside the pocket, about 3 cells from the line. All units of a side are created at its centre.
- Caveat: the narrower cuts found (a 1-cell cut and a 2-cell diagonal cut) let no unit move in the engine (units stood still for all 2857 frames), so they were not used. The 6-cell line is the first cut the engine lets units cross. It is therefore not a narrow ramp; it is the narrowest cut that works at this resolution.

## Fights (reference engine output)

## 1. 16 Zergling vs 8 Zealot

- Winner: B. Frames: 213 (8.9 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 16 | 0 | 0 | 0 | 0 |
| Total | 16 | 0 | 0 | 0 | 0 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zealot | 8 | 8 | 552 | 108 | 16 |
| Total | 8 | 8 | 552 | 108 | 16 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 16 | 8 | 560 | 1280 | 250.1 |
| 24 | 16 | 8 | 560 | 1270 | 97.6 |
| 48 | 16 | 8 | 488 | 1190 | 51.9 |
| 72 | 13 | 8 | 423 | 1079 | 42.4 |
| 96 | 11 | 8 | 321 | 970 | 40.4 |
| 120 | 7 | 8 | 229 | 864 | 36.2 |
| 144 | 6 | 8 | 170 | 790 | 24.9 |
| 168 | 5 | 8 | 87 | 731 | 10.3 |
| 192 | 3 | 8 | 25 | 681 | 32.9 |
| 213 | 0 | 8 | 0 | 660 | - |

First shot: side A frame 23, side B frame 29. First damage taken: side A frame 32, side B frame 24 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 0, B won 20, draws 0, timeouts 0; median 237.5 frames (min 198, max 276).
Per seed (seed:winner frames): 1:B276, 2:B246, 3:B241, 4:B198, 5:B245, 6:B215, 7:B242, 8:B212, 9:B210, 10:B246, 11:B254, 12:B230, 13:B220, 14:B266, 15:B213, 16:B225, 17:B275, 18:B234, 19:B270, 20:B205

## 2. 12 Hydralisk vs 6 Siege Tank (tank mode)

- Winner: A. Frames: 278 (11.7 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 12 | 6 | 414 | 0 | 6 |
| Total | 12 | 6 | 414 | 0 | 6 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Siege Tank | 6 | 0 | 0 | 0 | 6 |
| type 6 | 6 | 0 | 0 | 0 | 0 |
| Total | 6 | 0 | 0 | 0 | 6 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 12 | 960 | 900 | 256.3 |
| 24 | 11 | 12 | 857 | 855 | 206.9 |
| 48 | 10 | 10 | 755 | 750 | 221.0 |
| 72 | 10 | 10 | 732 | 732 | 192.1 |
| 96 | 9 | 10 | 652 | 642 | 168.0 |
| 120 | 8 | 10 | 618 | 534 | 163.6 |
| 144 | 8 | 8 | 549 | 405 | 168.7 |
| 168 | 8 | 6 | 484 | 342 | 162.9 |
| 192 | 8 | 4 | 485 | 282 | 157.5 |
| 216 | 6 | 4 | 458 | 183 | 141.2 |
| 240 | 6 | 2 | 436 | 141 | 151.4 |
| 264 | 6 | 2 | 437 | 51 | 139.7 |
| 278 | 6 | 0 | 414 | 0 | - |

First shot: side A frame 16, side B frame 5. First damage taken: side A frame 7, side B frame 20 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 278.5 frames (min 250, max 331).
Per seed (seed:winner frames): 1:A282, 2:A296, 3:A269, 4:A304, 5:A295, 6:A272, 7:A271, 8:A268, 9:A314, 10:A293, 11:A273, 12:A331, 13:A267, 14:A275, 15:A268, 16:A301, 17:A270, 18:A282, 19:A296, 20:A250

## 3. 10 Marine + 2 Medic vs 6 Zealot + 2 Dragoon

- Winner: B. Frames: 245 (10.3 s at Fastest).
- The fight ended because side A has only units that cannot attack.
- Medic (side A): starting energy 50.
- Medic (side A): starting energy 50.

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 10 | 0 | 0 | 0 | 2 |
| Medic | 2 | 2 | 112 | 0 | 0 |
| Total | 12 | 2 | 112 | 0 | 2 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zealot | 6 | 5 | 482 | 170 | 10 |
| Dragoon | 2 | 1 | 100 | 80 | 0 |
| Total | 8 | 6 | 582 | 250 | 10 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 8 | 520 | 1320 | 259.4 |
| 24 | 12 | 8 | 520 | 1284 | 120.4 |
| 48 | 12 | 8 | 484 | 1197 | 92.4 |
| 72 | 11 | 7 | 460 | 1122 | 94.1 |
| 96 | 10 | 7 | 440 | 1060 | 64.8 |
| 120 | 9 | 7 | 392 | 986 | 31.9 |
| 144 | 8 | 6 | 334 | 950 | 13.1 |
| 168 | 7 | 6 | 304 | 890 | 3.3 |
| 192 | 6 | 6 | 232 | 864 | 21.2 |
| 216 | 4 | 6 | 158 | 839 | 58.2 |
| 240 | 3 | 6 | 128 | 830 | 101.1 |
| 245 | 2 | 6 | 112 | 832 | 179.7 |

First shot: side A frame 19, side B frame 19. First damage taken: side A frame 32, side B frame 21 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 0, B won 20, draws 0, timeouts 0; median 234.5 frames (min 196, max 299).
Per seed (seed:winner frames): 1:B261, 2:B215, 3:B206, 4:B239, 5:B196, 6:B200, 7:B299, 8:B199, 9:B230, 10:B256, 11:B242, 12:B236, 13:B262, 14:B233, 15:B237, 16:B221, 17:B260, 18:B255, 19:B201, 20:B207

## 4. 8 Dragoon vs 12 Marine

- Winner: A. Frames: 281 (11.8 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Dragoon | 8 | 7 | 534 | 323 | 12 |
| Total | 8 | 7 | 534 | 323 | 12 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 12 | 0 | 0 | 0 | 1 |
| Total | 12 | 0 | 0 | 0 | 1 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 8 | 12 | 1440 | 480 | 244.1 |
| 24 | 8 | 12 | 1392 | 480 | 102.0 |
| 48 | 8 | 12 | 1291 | 400 | 102.0 |
| 72 | 8 | 10 | 1180 | 320 | 102.7 |
| 96 | 7 | 9 | 1127 | 260 | 107.6 |
| 120 | 7 | 8 | 1031 | 230 | 113.1 |
| 144 | 7 | 7 | 977 | 190 | 112.2 |
| 168 | 7 | 5 | 933 | 150 | 107.6 |
| 192 | 7 | 3 | 910 | 110 | 128.5 |
| 216 | 7 | 2 | 882 | 80 | 152.0 |
| 240 | 7 | 2 | 869 | 80 | 152.0 |
| 264 | 7 | 1 | 860 | 30 | 148.0 |
| 281 | 7 | 0 | 857 | 0 | - |

First shot: side A frame 18, side B frame 18. First damage taken: side A frame 20, side B frame 29 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 276.5 frames (min 247, max 336).
Per seed (seed:winner frames): 1:A276, 2:A289, 3:A278, 4:A279, 5:A269, 6:A253, 7:A287, 8:A274, 9:A251, 10:A257, 11:A272, 12:A282, 13:A277, 14:A280, 15:A270, 16:A292, 17:A248, 18:A247, 19:A284, 20:A336

## 5. 6 Goliath vs 12 Hydralisk

- Winner: B. Frames: 222 (9.3 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Goliath | 6 | 0 | 0 | 0 | 4 |
| type 4 | 6 | 0 | 0 | 0 | 0 |
| Total | 6 | 0 | 0 | 0 | 4 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 12 | 8 | 524 | 0 | 6 |
| Total | 12 | 8 | 524 | 0 | 6 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 12 | 750 | 960 | 233.5 |
| 24 | 12 | 12 | 714 | 876 | 181.3 |
| 48 | 12 | 11 | 606 | 784 | 173.6 |
| 72 | 12 | 10 | 498 | 716 | 162.5 |
| 96 | 10 | 10 | 400 | 656 | 160.2 |
| 120 | 6 | 9 | 303 | 614 | 169.5 |
| 144 | 6 | 8 | 241 | 581 | 149.4 |
| 168 | 4 | 8 | 178 | 557 | 141.4 |
| 192 | 4 | 8 | 70 | 534 | 142.6 |
| 216 | 2 | 8 | 8 | 523 | 145.3 |
| 222 | 0 | 8 | 0 | 524 | - |

First shot: side A frame 1, side B frame 14. First damage taken: side A frame 18, side B frame 1 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 0, B won 20, draws 0, timeouts 0; median 212.0 frames (min 193, max 232).
Per seed (seed:winner frames): 1:B199, 2:B200, 3:B196, 4:B201, 5:B219, 6:B216, 7:B222, 8:B201, 9:B215, 10:B210, 11:B224, 12:B212, 13:B226, 14:B212, 15:B211, 16:B193, 17:B201, 18:B219, 19:B232, 20:B214

## 6. 12 Wraith vs 8 Mutalisk

- Winner: A. Frames: 255 (10.7 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Wraith | 12 | 11 | 1000 | 0 | 8 |
| Total | 12 | 11 | 1000 | 0 | 8 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Mutalisk | 8 | 0 | 0 | 0 | 1 |
| Total | 8 | 0 | 0 | 0 | 1 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 8 | 1440 | 960 | 260.1 |
| 24 | 12 | 8 | 1440 | 910 | 155.7 |
| 48 | 12 | 7 | 1363 | 790 | 115.6 |
| 72 | 12 | 6 | 1285 | 690 | 122.4 |
| 96 | 12 | 6 | 1232 | 591 | 120.6 |
| 120 | 11 | 4 | 1164 | 480 | 130.4 |
| 144 | 11 | 4 | 1117 | 390 | 126.1 |
| 168 | 11 | 3 | 1087 | 310 | 127.5 |
| 192 | 11 | 3 | 1052 | 210 | 126.1 |
| 216 | 11 | 2 | 1022 | 131 | 129.6 |
| 240 | 11 | 1 | 1000 | 50 | 128.1 |
| 255 | 11 | 0 | 1000 | 0 | - |

First shot: side A frame 2, side B frame 19. First damage taken: side A frame 27, side B frame 11 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 258.0 frames (min 241, max 284).
Per seed (seed:winner frames): 1:A254, 2:A260, 3:A278, 4:A277, 5:A250, 6:A243, 7:A280, 8:A257, 9:A255, 10:A250, 11:A259, 12:A259, 13:A257, 14:A244, 15:A261, 16:A241, 17:A275, 18:A284, 19:A255, 20:A280

## 7. 8 Corsair vs 12 Mutalisk

- Winner: A. Frames: 170 (7.1 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Corsair | 8 | 7 | 627 | 282 | 8 |
| Total | 8 | 7 | 627 | 282 | 8 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Mutalisk | 12 | 0 | 0 | 0 | 1 |
| Total | 12 | 0 | 0 | 0 | 1 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 8 | 12 | 1440 | 1440 | 252.4 |
| 24 | 8 | 12 | 1440 | 1253 | 170.1 |
| 48 | 8 | 12 | 1292 | 964 | 128.6 |
| 72 | 8 | 12 | 1205 | 650 | 126.1 |
| 96 | 7 | 10 | 1119 | 392 | 123.3 |
| 120 | 7 | 9 | 1016 | 225 | 124.7 |
| 144 | 7 | 5 | 937 | 42 | 121.6 |
| 168 | 7 | 1 | 911 | 0 | 113.9 |
| 170 | 7 | 0 | 909 | 0 | - |

First shot: side A frame 2, side B frame 21. First damage taken: side A frame 30, side B frame 2 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 152.0 frames (min 137, max 171).
Per seed (seed:winner frames): 1:A165, 2:A141, 3:A154, 4:A153, 5:A148, 6:A152, 7:A146, 8:A149, 9:A152, 10:A152, 11:A152, 12:A137, 13:A143, 14:A155, 15:A171, 16:A143, 17:A165, 18:A146, 19:A152, 20:A170

## 8. 4 Archon vs 16 Hydralisk

- Winner: B. Frames: 231 (9.7 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Archon | 4 | 0 | 0 | 0 | 8 |
| Total | 4 | 0 | 0 | 0 | 8 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 16 | 7 | 359 | 0 | 4 |
| Total | 16 | 7 | 359 | 0 | 4 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 4 | 16 | 1440 | 1280 | 227.6 |
| 24 | 4 | 16 | 1400 | 1280 | 164.5 |
| 48 | 4 | 16 | 1180 | 1152 | 121.2 |
| 72 | 3 | 15 | 980 | 1072 | 122.1 |
| 96 | 3 | 14 | 741 | 979 | 105.8 |
| 120 | 3 | 13 | 512 | 749 | 107.6 |
| 144 | 2 | 9 | 382 | 504 | 101.7 |
| 168 | 1 | 9 | 310 | 471 | 144.9 |
| 192 | 1 | 8 | 201 | 458 | 135.8 |
| 216 | 1 | 8 | 61 | 400 | 88.7 |
| 231 | 0 | 7 | 0 | 359 | - |

First shot: side A frame 28, side B frame 19. First damage taken: side A frame 23, side B frame 35 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 0, B won 20, draws 0, timeouts 0; median 215.0 frames (min 194, max 230).
Per seed (seed:winner frames): 1:B220, 2:B219, 3:B212, 4:B228, 5:B212, 6:B225, 7:B218, 8:B212, 9:B204, 10:B210, 11:B213, 12:B221, 13:B218, 14:B209, 15:B230, 16:B225, 17:B216, 18:B212, 19:B194, 20:B214

## 9. 6 Ultralisk vs 6 Archon

- Winner: B. Frames: 357 (15.0 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Ultralisk | 6 | 0 | 0 | 0 | 1 |
| Total | 6 | 0 | 0 | 0 | 1 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Archon | 6 | 5 | 50 | 1128 | 6 |
| Total | 6 | 5 | 50 | 1128 | 6 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 6 | 6 | 2400 | 2160 | 235.6 |
| 24 | 6 | 6 | 2400 | 2160 | 146.7 |
| 48 | 6 | 6 | 2245 | 2100 | 115.2 |
| 72 | 6 | 6 | 2061 | 2021 | 103.4 |
| 96 | 6 | 6 | 1855 | 1921 | 84.2 |
| 120 | 6 | 6 | 1532 | 1783 | 79.0 |
| 144 | 4 | 6 | 1249 | 1704 | 62.1 |
| 168 | 4 | 5 | 1093 | 1603 | 69.3 |
| 192 | 3 | 5 | 812 | 1506 | 71.7 |
| 216 | 3 | 5 | 642 | 1426 | 71.7 |
| 240 | 3 | 5 | 485 | 1349 | 71.7 |
| 264 | 3 | 5 | 328 | 1272 | 71.7 |
| 288 | 1 | 5 | 286 | 1252 | 107.8 |
| 312 | 1 | 5 | 200 | 1215 | 85.9 |
| 336 | 1 | 5 | 84 | 1197 | 77.4 |
| 357 | 0 | 5 | 0 | 1178 | - |

First shot: side A frame 28, side B frame 28. First damage taken: side A frame 35, side B frame 32 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 0, B won 20, draws 0, timeouts 0; median 342.0 frames (min 322, max 365).
Per seed (seed:winner frames): 1:B352, 2:B344, 3:B343, 4:B334, 5:B365, 6:B346, 7:B338, 8:B338, 9:B359, 10:B338, 11:B341, 12:B340, 13:B359, 14:B347, 15:B337, 16:B331, 17:B322, 18:B333, 19:B354, 20:B363

## 10. 20 Zergling vs 6 Firebat + 6 Marine

- Winner: B. Frames: 90 (3.8 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 20 | 0 | 0 | 0 | 1 |
| Total | 20 | 0 | 0 | 0 | 1 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 6 | 6 | 190 | 0 | 0 |
| Firebat | 6 | 5 | 222 | 0 | 11 |
| Total | 12 | 11 | 412 | 0 | 11 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 20 | 12 | 700 | 540 | 244.2 |
| 24 | 20 | 12 | 676 | 523 | 92.7 |
| 48 | 12 | 12 | 284 | 476 | 83.7 |
| 72 | 4 | 11 | 62 | 431 | 65.6 |
| 90 | 0 | 11 | 0 | 412 | - |

First shot: side A frame 20, side B frame 19. First damage taken: side A frame 21, side B frame 21 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 0, B won 20, draws 0, timeouts 0; median 91.0 frames (min 75, max 110).
Per seed (seed:winner frames): 1:B102, 2:B82, 3:B100, 4:B102, 5:B91, 6:B97, 7:B75, 8:B94, 9:B88, 10:B81, 11:B98, 12:B103, 13:B91, 14:B80, 15:B83, 16:B89, 17:B110, 18:B88, 19:B100, 20:B80

## 11. 2 Battlecruiser vs 8 Hydralisk

- Winner: B. Frames: 510 (21.4 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Battlecruiser | 2 | 0 | 0 | 0 | 5 |
| Total | 2 | 0 | 0 | 0 | 5 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 8 | 3 | 121 | 0 | 2 |
| Total | 8 | 3 | 121 | 0 | 2 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 2 | 8 | 1000 | 640 | 247.0 |
| 24 | 2 | 8 | 1000 | 590 | 194.7 |
| 48 | 2 | 7 | 958 | 560 | 146.6 |
| 72 | 2 | 7 | 895 | 510 | 140.8 |
| 96 | 2 | 7 | 818 | 485 | 140.8 |
| 120 | 2 | 6 | 741 | 480 | 136.8 |
| 144 | 2 | 6 | 685 | 430 | 136.8 |
| 168 | 2 | 5 | 615 | 400 | 135.2 |
| 192 | 2 | 5 | 559 | 375 | 135.2 |
| 216 | 2 | 5 | 503 | 350 | 135.2 |
| 240 | 1 | 5 | 465 | 300 | 147.3 |
| 264 | 1 | 5 | 409 | 277 | 147.3 |
| 288 | 1 | 4 | 360 | 271 | 143.3 |
| 312 | 1 | 4 | 304 | 271 | 143.3 |
| 336 | 1 | 4 | 276 | 247 | 143.3 |
| 360 | 1 | 4 | 220 | 222 | 143.3 |
| 384 | 1 | 4 | 192 | 199 | 143.3 |
| 408 | 1 | 4 | 136 | 193 | 143.3 |
| 432 | 1 | 3 | 108 | 193 | 134.6 |
| 456 | 1 | 3 | 73 | 169 | 134.6 |
| 480 | 1 | 3 | 38 | 144 | 134.6 |
| 504 | 1 | 3 | 10 | 120 | 134.6 |
| 510 | 0 | 3 | 0 | 121 | - |

First shot: side A frame 2, side B frame 36. First damage taken: side A frame 40, side B frame 4 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 2, B won 18, draws 0, timeouts 0; median 541.0 frames (min 469, max 665).
Per seed (seed:winner frames): 1:B551, 2:B579, 3:B593, 4:B547, 5:B514, 6:B661, 7:A469, 8:B616, 9:B529, 10:B525, 11:B599, 12:B497, 13:A496, 14:B665, 15:B567, 16:B493, 17:B584, 18:B498, 19:B505, 20:B535

## 12. 8 Hydralisk + 4 Lurker (burrowed) vs 16 Marine

- Winner: A. Frames: 150 (6.3 s at Fastest).
- Lurkers: Burrow order given at the start; each attack-moves once burrowed. All Lurkers burrowed by frame 39.

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 8 | 5 | 371 | 0 | 11 |
| Lurker | 4 | 4 | 497 | 0 | 1 |
| Total | 12 | 9 | 868 | 0 | 12 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 16 | 0 | 0 | 0 | 3 |
| Total | 16 | 0 | 0 | 0 | 3 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 16 | 1140 | 640 | 246.2 |
| 24 | 12 | 16 | 1111 | 625 | 196.6 |
| 48 | 11 | 15 | 1049 | 505 | 180.8 |
| 72 | 11 | 8 | 984 | 260 | 167.7 |
| 96 | 11 | 5 | 924 | 85 | 173.6 |
| 120 | 10 | 4 | 881 | 65 | 165.6 |
| 144 | 10 | 1 | 871 | 10 | 163.6 |
| 150 | 9 | 0 | 868 | 0 | - |

First shot: side A frame 10, side B frame 4. First damage taken: side A frame 6, side B frame 14 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 135.0 frames (min 125, max 161).
Per seed (seed:winner frames): 1:A133, 2:A134, 3:A161, 4:A143, 5:A128, 6:A142, 7:A153, 8:A127, 9:A126, 10:A136, 11:A160, 12:A161, 13:A126, 14:A142, 15:A126, 16:A142, 17:A125, 18:A145, 19:A125, 20:A126

## 13. 4 Reaver vs 8 Dragoon

- Winner: A. Frames: 295 (12.4 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Reaver | 4 | 2 | 102 | 80 | 7 |
| Total | 4 | 2 | 102 | 80 | 7 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Dragoon | 8 | 0 | 0 | 0 | 2 |
| Total | 8 | 0 | 0 | 0 | 2 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 4 | 8 | 720 | 1440 | 217.7 |
| 24 | 4 | 8 | 720 | 1440 | 146.3 |
| 48 | 4 | 8 | 560 | 969 | 146.3 |
| 72 | 3 | 5 | 540 | 727 | 170.2 |
| 96 | 3 | 5 | 500 | 728 | 140.9 |
| 120 | 3 | 4 | 460 | 303 | 131.1 |
| 144 | 3 | 3 | 381 | 223 | 140.0 |
| 168 | 2 | 3 | 360 | 225 | 143.2 |
| 192 | 2 | 3 | 340 | 226 | 125.5 |
| 216 | 2 | 2 | 280 | 96 | 136.4 |
| 240 | 2 | 2 | 261 | 96 | 136.4 |
| 264 | 2 | 2 | 222 | 98 | 136.4 |
| 288 | 2 | 2 | 182 | 100 | 136.4 |
| 295 | 2 | 0 | 182 | 0 | - |

First shot: side A frame -1, side B frame 18. First damage taken: side A frame 33, side B frame 31 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 13, B won 7, draws 0, timeouts 0; median 342.0 frames (min 207, max 404).
Per seed (seed:winner frames): 1:A382, 2:A209, 3:A207, 4:B339, 5:A297, 6:B365, 7:A385, 8:A294, 9:A272, 10:B328, 11:A250, 12:B404, 13:B366, 14:A291, 15:A292, 16:A358, 17:B377, 18:B345, 19:A358, 20:A378

## 14. 6 Dark Templar vs 12 Zergling (no detector)

- Winner: A. Frames: 160 (6.7 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Dark Templar | 6 | 6 | 480 | 240 | 12 |
| Total | 6 | 6 | 480 | 240 | 12 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 12 | 0 | 0 | 0 | 0 |
| Total | 12 | 0 | 0 | 0 | 0 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 6 | 12 | 720 | 420 | 244.7 |
| 24 | 6 | 12 | 720 | 420 | 67.1 |
| 48 | 6 | 10 | 720 | 350 | 41.9 |
| 72 | 6 | 7 | 720 | 245 | 24.6 |
| 96 | 6 | 4 | 720 | 140 | 82.7 |
| 120 | 6 | 3 | 720 | 105 | 61.7 |
| 144 | 6 | 1 | 720 | 35 | 42.1 |
| 160 | 6 | 0 | 720 | 0 | - |

First shot: side A frame 24, side B frame -1. First damage taken: side A frame -1, side B frame 28 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 149.0 frames (min 123, max 176).
Per seed (seed:winner frames): 1:A142, 2:A123, 3:A128, 4:A147, 5:A144, 6:A142, 7:A176, 8:A153, 9:A160, 10:A161, 11:A166, 12:A134, 13:A150, 14:A133, 15:A161, 16:A170, 17:A136, 18:A161, 19:A148, 20:A156

## 15. Choke: 12 Marine vs 16 Zergling, either side of a choke

- Winner: A. Frames: 211 (8.9 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 12 | 7 | 250 | 0 | 16 |
| Total | 12 | 7 | 250 | 0 | 16 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 16 | 0 | 0 | 0 | 5 |
| Total | 16 | 0 | 0 | 0 | 5 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 16 | 480 | 560 | 249.0 |
| 24 | 12 | 15 | 480 | 525 | 204.1 |
| 48 | 12 | 13 | 455 | 443 | 174.4 |
| 72 | 11 | 11 | 435 | 343 | 143.0 |
| 96 | 10 | 8 | 390 | 268 | 116.3 |
| 120 | 10 | 6 | 360 | 192 | 82.2 |
| 144 | 9 | 5 | 310 | 146 | 72.4 |
| 168 | 7 | 3 | 280 | 99 | 69.1 |
| 192 | 7 | 2 | 260 | 52 | 45.0 |
| 211 | 7 | 0 | 250 | 0 | - |

First shot: side A frame 3, side B frame 27. First damage taken: side A frame 28, side B frame 5 (-1: none).

Seeds 1 to 20 (same scenario, only the random seed changed): A won 20, B won 0, draws 0, timeouts 0; median 188.0 frames (min 168, max 232).
Per seed (seed:winner frames): 1:A180, 2:A183, 3:A183, 4:A168, 5:A193, 6:A211, 7:A183, 8:A172, 9:A170, 10:A211, 11:A200, 12:A232, 13:A203, 14:A194, 15:A170, 16:A180, 17:A183, 18:A199, 19:A213, 20:A195


## Mining: (4)Orbital Relay.scm, every start location

9 workers per base, Terran, Protoss and Zerg, Fastest. 720 frames of warm-up, then 4286 frames measured. Each base is mined in its own game (placed depots left out: the start locations only). Minerals a minute, and each mineral field's mean round trip in frames, nearest field first (8 fields per base).

| Start location | Race | Minerals a minute | Field round trips (frames) |
|---|---|---|---|
| (352, 656) | Terran | 482.6 | 200.7, 215.0, 213.8, 213.0, 203.0, 224.8, 216.9, 211.5 |
| (352, 656) | Protoss | 517.3 | 181.0, 196.2, 193.5, 192.7, 205.3, 186.7, 226.5, 211.8 |
| (352, 656) | Zerg | 520.0 | 180.6, 191.3, 197.8, 194.2, 200.9, 190.8, 225.1, 225.1 |
| (2720, 688) | Terran | 512.0 | 198.5, 197.8, 212.8, 203.9, 187.6, 203.5, 211.7, 198.2 |
| (2720, 688) | Protoss | 509.3 | 197.1, 206.5, 207.3, 191.5, 191.0, 199.9, 221.8, 205.6 |
| (2720, 688) | Zerg | 506.6 | 192.7, 192.2, 206.2, 200.9, 229.9, 207.7, 205.9, 204.8 |
| (352, 2256) | Terran | 480.0 | 200.7, 215.0, 213.8, 213.0, 210.0, 224.8, 216.9, 211.5 |
| (352, 2256) | Protoss | 536.0 | 161.5, 200.4, 188.2, 205.9, 194.2, 186.7, 212.3, 211.8 |
| (352, 2256) | Zerg | 512.0 | 200.8, 185.8, 198.0, 194.4, 200.9, 190.8, 225.1, 225.1 |
| (2720, 2256) | Terran | 509.3 | 191.0, 194.9, 200.9, 205.9, 198.3, 199.9, 210.0, 224.9 |
| (2720, 2256) | Protoss | 517.3 | 193.6, 184.4, 198.5, 200.1, 190.9, 183.2, 217.7, 229.5 |
| (2720, 2256) | Zerg | 504.0 | 198.1, 194.5, 211.3, 199.4, 192.5, 209.7, 223.5, 229.8 |

Same setup on (2)Bottleneck.scm (the regression for the mining numbers in reference-results.md): the start location at (3808, 2384) gives Terran 549.3, Protoss 546.6, Zerg 541.3 minerals a minute (reference-results.md: 549.4, 546.8 and 541.4). The mining runs use the default seed (42) only; they were not repeated over seeds.

## Not run, and limits

- Dark Templar with a detector (6 Dark Templar vs 12 Zergling plus an Overseer): not run. The engine's unit list has no Overseer as a starting unit (only the Overlord; the Overseer is a morph), so a detector could not be placed. Detection is shown only indirectly: in fight 14 the Dark Templar are never hit (no Zergling fires; first damage taken: none on side A), so the Zerglings cannot see them and all 12 die.
- Reaver (fight 13): the first-shot value for side A is -1 because the first-shot tracker reads weapon cooldowns and does not catch Scarab launches. The Reavers' first damage taken and the rest of the fight are reliable; ignore the Reaver first-shot value.
- Siege Tank: tank mode only (as requested).
- Lurkers (fight 12): all four are burrowed (the frame of the last one is given); the Hydralisks attack-move from the start.
- Medics (fight 3): the starting energy is the engine's value (50).
- Kills are the engine's attribution and only approximate.
- The 20 seeds change only the random state at the start of each fight; they do not change the map, the unit placement rule or the order of the orders.
- The choke fight (15) is the first passable 6-cell line on the 32 px grid, not a narrow ramp (see the choke section).
- The reference tool's source (in the reftool folder) gained a seed option, a walkability dump, a cut check, and the mining and choke modes for this run. Another edit to the same sources (sprite positions in the live-image record) was already there; it is left as it is.
