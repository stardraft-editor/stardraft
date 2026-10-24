# Reference combat numbers (the reference engine)

Engine: the reference engine (Brood War 1.16.1 rules), Fastest speed (42 ms a frame). Map: (2)Bottleneck.scm (read as Brood War).
Random seed: 42 (the reference engine's value at map load, not changed; each scenario starts from a fresh map load).
Placement: first start location at (3808, 2384). Side A centre at (3680, 2384) and side B centre at (3936, 2384), each moved to the nearest walkable point; all of a side's units are created at its centre and the engine places them at the nearest free spot.
The map's own units of players 0 and 1 are removed first. Each unit gets Attack Move to the other side's centre.
Limit: 2857 frames (2 minutes at Fastest); a fight that reaches it is a timeout. Times are game frames.

Notes on the numbers:

- Each scenario is a fresh map load, so each starts from the same seed. The seed is 42, the reference engine's value at map load (no replay seed is given).
- A Carrier starts with 4 Interceptors inside and a Reaver with 5 Scarabs inside (the capacity without upgrades). The reference engine's starting units have none, and these are added by the reference tool. They are not counted as units of the side.
- Kills are the reference engine's own attribution: a death counts for the enemy unit that targeted the dead unit last. Treat them as approximate.
- Lurkers: a Burrow order is given at frame 0 and the attack move follows once the Lurker is burrowed (the frame of the last Lurker to burrow is stated).
- First shot: the frame a unit's weapon cooldown starts (an attack is made). First damage taken: the first frame a unit of that side loses HP or shields.
- Centre distance: the distance between the mean positions of each side's living units.
- Movement traces: the unit stops when it is within 16 px of the target, or when its order ends. The target is 320 px away on a straight walkable line.

Time series rows are every 24 frames plus the final frame. Frames are game frames at Fastest.

## 1. 12 Marine vs 20 Zergling

- Winner: B. Frames: 204 (8.6 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 12 | 0 | 0 | 0 | 12 |
| Total | 12 | 0 | 0 | 0 | 12 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 20 | 8 | 239 | 0 | 12 |
| Total | 20 | 8 | 239 | 0 | 12 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 12 | 20 | 480 | 700 | 264.8 |
| 24 | 12 | 20 | 480 | 664 | 114.6 |
| 48 | 12 | 18 | 410 | 552 | 95.2 |
| 72 | 9 | 14 | 355 | 478 | 91.6 |
| 96 | 8 | 13 | 290 | 419 | 73.7 |
| 120 | 7 | 11 | 240 | 361 | 56.7 |
| 144 | 6 | 9 | 160 | 303 | 38.3 |
| 168 | 3 | 9 | 85 | 261 | 34.8 |
| 192 | 1 | 8 | 20 | 245 | 36.6 |
| 204 | 0 | 8 | 0 | 239 | - |

First shot: side A frame 18, side B frame 24. First damage taken: side A frame 25, side B frame 20 (-1: none).

## 2. 6 Zealot vs 12 Marine

- Winner: A. Frames: 288 (12.1 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zealot | 6 | 3 | 266 | 103 | 12 |
| Total | 6 | 3 | 266 | 103 | 12 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 12 | 0 | 0 | 0 | 3 |
| Total | 12 | 0 | 0 | 0 | 3 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 6 | 12 | 960 | 480 | 238.0 |
| 24 | 6 | 12 | 912 | 480 | 101.1 |
| 48 | 6 | 12 | 819 | 432 | 67.0 |
| 72 | 5 | 10 | 718 | 384 | 85.3 |
| 96 | 5 | 9 | 657 | 328 | 70.3 |
| 120 | 4 | 8 | 594 | 280 | 58.0 |
| 144 | 4 | 7 | 509 | 224 | 57.6 |
| 168 | 4 | 5 | 475 | 192 | 59.1 |
| 192 | 4 | 4 | 435 | 152 | 44.2 |
| 216 | 4 | 3 | 412 | 96 | 49.9 |
| 240 | 3 | 2 | 388 | 64 | 50.1 |
| 264 | 3 | 1 | 379 | 24 | 69.5 |
| 288 | 3 | 0 | 369 | 0 | - |

First shot: side A frame 34, side B frame 18. First damage taken: side A frame 20, side B frame 37 (-1: none).

## 3. 12 Marine + 4 Medic vs 20 Zergling + 4 Hydralisk

- Winner: B. Frames: 308 (12.9 s at Fastest).
- The fight ended because side A has only units that cannot attack.
- Medic (side A): starting energy 50.
- Medic (side A): starting energy 50.
- Medic (side A): starting energy 50.
- Medic (side A): starting energy 50.

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 12 | 0 | 0 | 0 | 17 |
| Medic | 4 | 4 | 236 | 0 | 0 |
| Total | 16 | 4 | 236 | 0 | 17 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 20 | 6 | 174 | 0 | 11 |
| Hydralisk | 4 | 1 | 41 | 0 | 1 |
| Total | 24 | 7 | 215 | 0 | 12 |

Time series (every 24 frames): units alive and HP+shields per side, and the distance between the two sides' centres (mean position of the living units).

| Frame | A alive | B alive | A HP+shields | B HP+shields | Centre distance |
|---|---|---|---|---|---|
| 0 | 16 | 24 | 720 | 1020 | 246.1 |
| 24 | 16 | 24 | 701 | 978 | 176.1 |
| 48 | 15 | 23 | 671 | 889 | 160.4 |
| 72 | 15 | 21 | 621 | 790 | 148.1 |
| 96 | 15 | 19 | 601 | 707 | 143.1 |
| 120 | 14 | 19 | 569 | 605 | 124.5 |
| 144 | 12 | 16 | 535 | 534 | 97.2 |
| 168 | 12 | 13 | 511 | 459 | 69.2 |
| 192 | 11 | 12 | 478 | 418 | 33.4 |
| 216 | 11 | 10 | 435 | 343 | 21.1 |
| 240 | 8 | 9 | 380 | 284 | 53.3 |
| 264 | 7 | 8 | 335 | 255 | 91.9 |
| 288 | 6 | 7 | 278 | 227 | 135.1 |
| 308 | 4 | 7 | 236 | 215 | 212.6 |

First shot: side A frame 13, side B frame 4. First damage taken: side A frame 8, side B frame 15 (-1: none).

## 4. 1 Marine vs 1 Zergling

- Winner: B. Frames: 101 (4.2 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 1 | 0 | 0 | 0 | 0 |
| Total | 1 | 0 | 0 | 0 | 0 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 1 | 1 | 0 | 0 | 1 |
| Total | 1 | 1 | 0 | 0 | 1 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 1 | 1 | 40 | 35 |
| 24 | 1 | 1 | 40 | 29 |
| 48 | 1 | 1 | 30 | 23 |
| 72 | 1 | 1 | 20 | 11 |
| 96 | 1 | 1 | 5 | 6 |
| 101 | 0 | 1 | 0 | 0 |

## 5. 8 Hydralisk vs 8 Dragoon

- Winner: B. Frames: 270 (11.3 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 8 | 0 | 0 | 0 | 2 |
| Total | 8 | 0 | 0 | 0 | 2 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Dragoon | 8 | 6 | 503 | 293 | 8 |
| Total | 8 | 6 | 503 | 293 | 8 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 8 | 8 | 640 | 1440 |
| 24 | 8 | 8 | 640 | 1420 |
| 48 | 8 | 8 | 520 | 1271 |
| 72 | 6 | 8 | 450 | 1162 |
| 96 | 5 | 7 | 370 | 1093 |
| 120 | 5 | 7 | 340 | 1022 |
| 144 | 5 | 7 | 265 | 949 |
| 168 | 3 | 6 | 180 | 909 |
| 192 | 3 | 6 | 92 | 862 |
| 216 | 1 | 6 | 80 | 828 |
| 240 | 1 | 6 | 80 | 811 |
| 264 | 1 | 6 | 5 | 805 |
| 270 | 0 | 6 | 0 | 796 |

## 6. 10 Zergling vs 4 Zealot

- Winner: B. Frames: 229 (9.6 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 10 | 0 | 0 | 0 | 1 |
| Total | 10 | 0 | 0 | 0 | 1 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zealot | 4 | 3 | 170 | 3 | 10 |
| Total | 4 | 3 | 170 | 3 | 10 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 10 | 4 | 350 | 640 |
| 24 | 10 | 4 | 350 | 640 |
| 48 | 10 | 4 | 302 | 570 |
| 72 | 9 | 4 | 251 | 476 |
| 96 | 7 | 4 | 213 | 388 |
| 120 | 6 | 4 | 162 | 315 |
| 144 | 5 | 4 | 127 | 253 |
| 168 | 3 | 4 | 89 | 215 |
| 192 | 3 | 4 | 33 | 182 |
| 216 | 1 | 3 | 3 | 176 |
| 229 | 0 | 3 | 0 | 173 |

## 7. 4 Siege Tank (Tank Mode) vs 12 Zergling

- Winner: B. Frames: 301 (12.6 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Siege Tank | 4 | 0 | 0 | 0 | 7 |
| type 6 | 4 | 0 | 0 | 0 | 0 |
| Total | 4 | 0 | 0 | 0 | 7 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 12 | 5 | 160 | 0 | 4 |
| Total | 12 | 5 | 160 | 0 | 4 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 8 | 12 | 600 | 420 |
| 24 | 8 | 11 | 600 | 385 |
| 48 | 8 | 10 | 584 | 335 |
| 72 | 8 | 10 | 552 | 335 |
| 96 | 8 | 9 | 488 | 285 |
| 120 | 6 | 8 | 418 | 250 |
| 144 | 6 | 8 | 358 | 250 |
| 168 | 6 | 7 | 290 | 215 |
| 192 | 6 | 7 | 206 | 210 |
| 216 | 2 | 6 | 150 | 180 |
| 240 | 2 | 5 | 134 | 175 |
| 264 | 2 | 5 | 86 | 175 |
| 288 | 2 | 5 | 34 | 160 |
| 301 | 0 | 5 | 0 | 160 |

## 8. 2 Reaver vs 16 Zergling

- Winner: A. Frames: 114 (4.8 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Reaver | 2 | 2 | 200 | 126 | 3 |
| Total | 2 | 2 | 200 | 126 | 3 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 16 | 0 | 0 | 0 | 0 |
| Total | 16 | 0 | 0 | 0 | 0 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 2 | 16 | 360 | 560 |
| 24 | 2 | 16 | 360 | 560 |
| 48 | 2 | 1 | 360 | 35 |
| 72 | 2 | 1 | 350 | 35 |
| 96 | 2 | 1 | 336 | 35 |
| 114 | 2 | 0 | 326 | 0 |

## 9. 2 Carrier vs 12 Hydralisk

- Winner: B. Frames: 353 (14.8 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Carrier | 2 | 0 | 0 | 0 | 1 |
| Total | 2 | 0 | 0 | 0 | 1 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Hydralisk | 12 | 11 | 800 | 0 | 2 |
| Total | 12 | 11 | 800 | 0 | 2 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 2 | 12 | 900 | 960 |
| 24 | 2 | 12 | 900 | 960 |
| 48 | 2 | 12 | 800 | 960 |
| 72 | 2 | 12 | 682 | 948 |
| 96 | 2 | 12 | 630 | 900 |
| 120 | 2 | 12 | 546 | 888 |
| 144 | 2 | 11 | 483 | 880 |
| 168 | 2 | 11 | 422 | 874 |
| 192 | 2 | 11 | 378 | 838 |
| 216 | 1 | 11 | 325 | 832 |
| 240 | 1 | 11 | 315 | 804 |
| 264 | 1 | 11 | 294 | 798 |
| 288 | 1 | 11 | 246 | 798 |
| 312 | 1 | 11 | 181 | 800 |
| 336 | 1 | 11 | 68 | 800 |
| 353 | 0 | 11 | 0 | 800 |

## 10. 12 Mutalisk vs 6 Goliath

- Winner: A. Frames: 291 (12.2 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Mutalisk | 12 | 10 | 1024 | 0 | 6 |
| Total | 12 | 10 | 1024 | 0 | 6 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Goliath | 6 | 0 | 0 | 0 | 2 |
| type 4 | 6 | 0 | 0 | 0 | 0 |
| Total | 6 | 0 | 0 | 0 | 2 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 12 | 12 | 1440 | 750 |
| 24 | 12 | 12 | 1410 | 750 |
| 48 | 12 | 12 | 1340 | 641 |
| 72 | 12 | 12 | 1280 | 574 |
| 96 | 11 | 10 | 1251 | 522 |
| 120 | 11 | 8 | 1201 | 433 |
| 144 | 11 | 8 | 1161 | 336 |
| 168 | 10 | 6 | 1121 | 285 |
| 192 | 10 | 6 | 1102 | 230 |
| 216 | 10 | 4 | 1062 | 158 |
| 240 | 10 | 2 | 1043 | 97 |
| 264 | 10 | 2 | 1034 | 49 |
| 288 | 10 | 2 | 1024 | 9 |
| 291 | 10 | 0 | 1024 | 0 |

## 11. 4 Lurker (burrowed) vs 12 Marine

- Winner: A. Frames: 113 (4.7 s at Fastest).
- Lurkers: Burrow order given at the start; each attack-moves once burrowed. All Lurkers burrowed by frame 42.

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Lurker | 4 | 4 | 417 | 0 | 5 |
| Total | 4 | 4 | 417 | 0 | 5 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 12 | 0 | 0 | 0 | 0 |
| Total | 12 | 0 | 0 | 0 | 0 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 4 | 12 | 500 | 480 |
| 24 | 4 | 12 | 475 | 480 |
| 48 | 4 | 12 | 415 | 400 |
| 72 | 4 | 5 | 415 | 160 |
| 96 | 4 | 1 | 416 | 20 |
| 113 | 4 | 0 | 417 | 0 |

## 12. 8 Firebat vs 16 Zergling

- Winner: A. Frames: 96 (4.0 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Firebat | 8 | 7 | 294 | 0 | 10 |
| Total | 8 | 7 | 294 | 0 | 10 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zergling | 16 | 0 | 0 | 0 | 1 |
| Total | 16 | 0 | 0 | 0 | 1 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 8 | 16 | 400 | 560 |
| 24 | 8 | 16 | 392 | 560 |
| 48 | 8 | 9 | 340 | 171 |
| 72 | 8 | 4 | 304 | 21 |
| 96 | 7 | 0 | 294 | 0 |

## 13. 6 Vulture vs 8 Zealot

- Winner: B. Frames: 271 (11.4 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Vulture | 6 | 0 | 0 | 0 | 3 |
| Total | 6 | 0 | 0 | 0 | 3 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Zealot | 8 | 5 | 464 | 240 | 6 |
| Total | 8 | 5 | 464 | 240 | 6 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 6 | 8 | 480 | 1280 |
| 24 | 6 | 8 | 480 | 1240 |
| 48 | 6 | 8 | 464 | 1123 |
| 72 | 6 | 8 | 392 | 1042 |
| 96 | 5 | 7 | 344 | 983 |
| 120 | 5 | 7 | 272 | 889 |
| 144 | 4 | 6 | 256 | 883 |
| 168 | 4 | 6 | 192 | 806 |
| 192 | 3 | 6 | 112 | 749 |
| 216 | 1 | 5 | 80 | 741 |
| 240 | 1 | 5 | 64 | 742 |
| 264 | 1 | 5 | 32 | 723 |
| 271 | 0 | 5 | 0 | 704 |

## 14. 4 Ultralisk vs 16 Marine

- Winner: A. Frames: 227 (9.5 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Ultralisk | 4 | 4 | 1021 | 0 | 16 |
| Total | 4 | 4 | 1021 | 0 | 16 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Marine | 16 | 0 | 0 | 0 | 0 |
| Total | 16 | 0 | 0 | 0 | 0 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 4 | 16 | 1600 | 640 |
| 24 | 4 | 16 | 1560 | 600 |
| 48 | 4 | 14 | 1430 | 560 |
| 72 | 4 | 14 | 1310 | 460 |
| 96 | 4 | 10 | 1242 | 400 |
| 120 | 4 | 8 | 1157 | 300 |
| 144 | 4 | 7 | 1112 | 240 |
| 168 | 4 | 5 | 1069 | 160 |
| 192 | 4 | 3 | 1039 | 100 |
| 216 | 4 | 1 | 1026 | 20 |
| 227 | 4 | 0 | 1021 | 0 |

## 15. 12 Scourge vs 2 Battlecruiser

- Winner: B. Frames: 52 (2.2 s at Fastest).

Side A survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Scourge | 12 | 0 | 0 | 0 | 0 |
| Total | 12 | 0 | 0 | 0 | 0 |

Side B survivors (started, alive, HP, shields, kills):

| Unit | Started | Alive | HP (sum) | Shields (sum) | Kills |
|---|---|---|---|---|---|
| Battlecruiser | 2 | 2 | 679 | 0 | 5 |
| Total | 2 | 2 | 679 | 0 | 5 |

Time series (every 24 frames): units alive and HP+shields per side.

| Frame | A alive | B alive | A HP+shields | B HP+shields |
|---|---|---|---|---|
| 0 | 12 | 2 | 300 | 1000 |
| 24 | 10 | 2 | 250 | 1000 |
| 48 | 5 | 2 | 125 | 679 |
| 52 | 0 | 2 | 0 | 679 |

## Movement traces (Move 320 px on open ground)

Start: (3680, 2384) (the side A centre); target: (4000, 2384), 320 px away on a walkable line. Position every 4 frames until the unit arrives (within 8 px of the target or order Stop).

### Marine

| Frame | X | Y | Distance from start |
|---|---|---|---|
| 0 | 3680 | 2384 | 0.0 |
| 4 | 3684 | 2384 | 4.0 |
| 8 | 3700 | 2384 | 20.0 |
| 12 | 3716 | 2384 | 36.0 |
| 16 | 3732 | 2384 | 52.0 |
| 20 | 3748 | 2384 | 68.0 |
| 24 | 3764 | 2384 | 84.0 |
| 28 | 3780 | 2384 | 100.0 |
| 32 | 3796 | 2384 | 116.0 |
| 36 | 3812 | 2384 | 132.0 |
| 40 | 3828 | 2384 | 148.0 |
| 44 | 3844 | 2384 | 164.0 |
| 48 | 3860 | 2384 | 180.0 |
| 52 | 3876 | 2384 | 196.0 |
| 56 | 3892 | 2384 | 212.0 |
| 60 | 3908 | 2384 | 228.0 |
| 64 | 3924 | 2384 | 244.0 |
| 68 | 3940 | 2384 | 260.0 |
| 72 | 3956 | 2384 | 276.0 |
| 76 | 3972 | 2384 | 292.0 |
| 79 | 3984 | 2384 | 304.0 |

Arrived at frame 79 (order Move), distance covered 304.0 px, distance from the target 16.0 px.

### Zergling

| Frame | X | Y | Distance from start |
|---|---|---|---|
| 0 | 3680 | 2384 | 0.0 |
| 4 | 3688 | 2384 | 8.0 |
| 8 | 3715 | 2384 | 35.0 |
| 12 | 3736 | 2384 | 56.0 |
| 16 | 3753 | 2384 | 73.0 |
| 20 | 3777 | 2384 | 97.0 |
| 24 | 3794 | 2384 | 114.0 |
| 28 | 3819 | 2384 | 139.0 |
| 32 | 3838 | 2384 | 158.0 |
| 36 | 3865 | 2384 | 185.0 |
| 40 | 3881 | 2384 | 201.0 |
| 44 | 3901 | 2384 | 221.0 |
| 48 | 3925 | 2384 | 245.0 |
| 52 | 3940 | 2384 | 260.0 |
| 56 | 3968 | 2384 | 288.0 |
| 60 | 3987 | 2384 | 307.0 |

Arrived at frame 60 (order Move), distance covered 307.0 px, distance from the target 13.0 px.

