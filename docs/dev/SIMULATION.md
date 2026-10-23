# The simulation

StarDraft has a small simulation of StarCraft: Brood War 1.16.1 for two tests on a map: worker mining (how many
minerals a base gives) and combat (which army wins a fight, and what is left of it). It is in `tools/sim/`, the CMake
library `stardraft_sim`. The editor and the command line use it through `tools/sim/sim.h` (`sim::createSimulator()`).

## How it was made: a clean room

The engine was written from plain-language descriptions of the game's rules, which are not part of this repository.
The person who wrote this engine never saw another engine's code and used no other code: only those descriptions,
the public layouts of the game's data files (units.dat, weapons.dat, flingy.dat, sprites.dat, images.dat,
iscript.bin, the tilesets and the CHK sections), and black-box results of a reference engine. Those results are in
`tools/tests/sim-reference/` (fights, time series, per-unit traces, ground trips and a held-out check set), and
`sim_test` compares with them. Where the rules do not say enough, the engine takes the simplest behaviour that fits
the reference results; "Known gaps" lists these choices.

## The parts

| File | What it does |
|---|---|
| `sim.h` | The interface (the editor's): the options, the results, `Simulator`. |
| `world.h` | The inside: the data tables, the map, units, projectiles, and `World` (one game, a frame at a time). |
| `data.cpp` | Reads the .dat files, iscript.bin, and the map (DIM, ERA, MTXM with the tileset's CV5 and VF4 for walkable ground, UNIT). |
| `engine.cpp` | Units, the iscript interpreter, the images the editor draws, the frame loop, and the `Simulator`. |
| `movement.cpp` | Movement (flingy speed, acceleration, halt distance, turn rate; units with flingy control 2 move by their walking script's `move` instructions), placement, paths around unwalkable ground, ground collision in fights and the short path around units. |
| `mining.cpp` | The bases of a map, the melee start at each, the gather orders (one function by order state), the measures. |
| `orders.cpp` | The combat orders, one function by order and kind of unit: Spider Mine, Burrow, Interceptor, Medic, guard, Attack Move, Attack Unit with the Interceptor and Scarab launches. |
| `combat.cpp` | The armies, targets, firing, projectiles, splash, damage, the result. |
| `proximity.h`, `proximity.cpp` | The distances of the game's rules (box gap, point gap, touching, centre distance, straight distance, square reach) that every "in range", "near" and "touching" test uses, and the grid of the units by place: each unit is in the 64-pixel cells its box covers, updated only when it is made, crosses a cell edge or dies; a query gives the units near an area in their list order. |
| `rules.ini` | The game's rules that no data file holds: the game's own order numbers, unit roles, timings, distances and amounts (built into the program; a project can give its own `sim/rules.ini`). |
| `rules.h`, `rules.cpp` | Reads `rules.ini`, and gives each unit type and weapon its role from the data files and `rules.ini`. |
| `tools/tests/sim_test.cpp` | The test against the reference results. |

The game frame: movement for all units (as if at the same time: a unit held by one that moves later
in the frame tries again after it), then for each unit its timers (cooldowns, shield, Zerg and energy
regeneration), its order and its animation step, then the projectiles. Orders run on a 9-frame tick, at once for a new
order and then on the unit's own phase (Zerglings every frame while they can attack). Random
numbers come from the game's 32-bit linear congruential stream (seed 42), and the scripts draw them where the rules
say (waitrand, randcondjmp, random sounds).

**Mining.** The bases are the start locations, the placed resource depots, the bases in the options, and (with
`mineralClusters`) a base for each group of 3 or more mineral fields with no base within 12 tiles: a Command Center spot
on the tile grid, on walkable ground, not within 3 tiles of a resource, the nearest to the fields. Each mineral field
belongs to its nearest base within 12 tiles. A run is a melee start (a depot of the race, on the tile grid at the base;
the workers at the nearest free places), then the workers are split over the fields, nearest first. With `gas`, the
nearest geyser becomes a finished refinery with `gasWorkers` (1 to 3) more workers. The measures start after the
warm-up: minerals and gas a minute (at Fastest, 42 ms a frame) and each field's mean round trip (delivery to delivery).
With `mapRaces`, a start location's base takes its player's race from the map's SIDE section (a race picked in the
lobby or Random: a random race from `seed`); the other bases take `races[0]`. A run of 60 minutes with 60 workers at
both start locations of (2)Bottleneck.scm takes about 0.3 s (`sim_test`).
A mining worker plays its mining animation (Almost Built) while it mines and Ground Attack to Idle at the end; the
weapon that the SCV's and the Probe's script uses there (useweapon) gives its projectile's picture at the field, with
its sprites and sounds, and no hit (the Drone's has only its sounds). Gathering workers pass each other and the mineral
fields, but never walk into the box of a depot, a refinery or any other building: the terrain test, the paths and the
steps treat those boxes as unwalkable (a mask of the minitiles they cover, as mining's buildings never move), and a
worker goes to the nearest point where it touches its field or depot and fits. (mining.md has the fields block workers
too, as buildings; with that, the mining measures went from a mean difference of 4.0% to 5.8%, so the fields stay
passable until the stop point at a field is measured.)

**Combat.** As in the reference: each side's centre is `gap / 2` left and right of (`x`, `y`) (or the map's middle),
moved to the nearest walkable point; each side's units start there, placed in order by the free-spot search
(rings of 16 pixels), facing 8 times units.dat's direction (32: a random one); each unit
attack-moves to the other centre. The map's
own units other than the two players' stay as obstacles. With `placed`, the units stand where they are and
attack-move to the other side's middle. The fight ends when a side is dead, when a side has only units that cannot
attack (`helpless`), or at the last frame. A death counts as a kill for the enemy unit that hit it last. Zerglings and
Scourges cost half of units.dat's cost. Carriers start with 4 Interceptors and Reavers with 5 Scarabs; Interceptors
are not units of the side.

**Projectiles.** Each attack instruction of a unit's script fires its weapon: one projectile, or two at the same time
when weapons.dat's damage factor is 2 (the Scout's and the Goliath's air missiles; the Valkyrie's script fires 4
times, so 8 rockets). Each projectile has its own flight and hit with the full weapon damage, so the data's "14 x 2"
is 2 hits of 14. A weapon with a launch spin (heading offset) sends its projectile out to one side:
the first of an attack takes a random side, the next of the same unit the other side. Flying
projectiles move with their flingy data: the direction of travel turns by the full turn rate, so two missiles
curve out a little apart and in to the target; near the target (32 pixels) a projectile that still turns speeds up
only when it has room to turn, else it slows down, so it never circles round the target. A turning image's frame set
holds 17 directions; a set that its GRP has no room for (the death frames of a Zealot, Marine or Dragoon) is one
frame.

**The map's settings.** The .dat files come through `init` (the editor gives the map's unit settings and a project's
edited .dat files there). From the map's CHK the engine reads each player's upgrade start levels (PUPx, or UPGR) and
researched techs (PTEx, or PTEC); the upgrades count in fights (see "Known gaps" for which).

**Live games.** `startMining` (a player at each base, at most 8) and `startCombat` run a frame at a time with `step`;
`images` gives each unit's and projectile's main image (turrets too) with its GRP frame: the script's frame plus the
facing (17 frames, flipped for the other side). A sprite that sprites.dat marks as not visible is never given: the
projectiles of the Firebat, the Scarab, the Scourge and the Lurker (the Scarab is drawn with its own image, the
Lurker's spines with the sprites its projectile's script makes). `startMap` is not in the simulation yet.

**Game knowledge in one place.** The code holds no game values: the engine refers to unit types only by their roles
(`UnitRole`, `WeaponRole`), and to the game's numbers only by the names of `tools/sim/rules.ini`, which the program
reads at start (built into the program; a project can give its own `sim/rules.ini`, so a mod can change them). A data
file's value always wins. From units.dat: the races, the workers, the depots and refineries of each race, the static
defences (buildings with a weapon), the detectors and their range (sight), cloaked units, spellcasters (energy),
regeneration, the units targeted last (a race's units that are neither men nor buildings: larvae, eggs), half cost (two
from one egg), the neutral non-combatants (critters, power-ups), the mineral fields, geysers and start locations
(neutral resource containers by size and group, a neutral depot), and through its orders the units that launch Scarabs
or Interceptors, the Spider Mine and the Medic's heal; from weapons.dat the projectile kinds (follow, bounce, appear on
the target, go to the maximum range: a line of hits, suicide); from flingy.dat the movement kinds; from sprites.dat
which sprites are drawn; from iscript.bin the attack moments. `rules.ini` holds the rest: the numbers of the game's own
orders, the units a Reaver and a Carrier make and how many they hold, the Lurker (attacks only burrowed), the Zergling's
every-frame orders, the Archon's start delay, the carried-gas images, the upgrade effects, the Lurker's raised speed,
the Halo Rockets' limit, and the game's timings, distances and amounts (order tick, target search, the minimum damage,
priorities, call for help, bounces, damage factors, splash rings, regeneration, collision limits, the Medic, Scarab,
Interceptor and Spider Mine numbers, mining). A rule had to earn its place: each of the others was taken out when it
changed neither set's 20-seed means by more than 0.1 percentage point nor any winner or check (flying repulse, the
detector buildings' 7 tiles, the cooldown limits and others); the upgrade effects, the start energy and the Halo
Rockets' limit stay, because maps start with upgrades that the test fights do not use. The engine's own numbers (fixed
point, grid cells, search budgets, the stalemate) stay in the engine.

## Results

`sim_test [<install>] [--series] [--seeds] [--traces <file>] [--trace <fight>]` runs these on `(2)Bottleneck.scm` and
prints them beside the reference (run it from the repository's root: it reads `tools/tests/sim-reference/`). `--series`:
the time series; `--seeds`: each fight with 20 other seeds (with the mean series and frame differences over them);
`--trace <fight>`: each unit's position beside the reference (100: the crowding test). It also checks that the map's
upgrades change a fight: the same map with a PUPx section that gives the Marines +3 weapons, +3 armor and U-238 Shells
(fight 1: 9 Marines and no Zergling alive, against no Marine and 6 Zerglings without).

Mining, 9 workers at the start location (3808, 2384), minerals a minute:

| Race | Reference | This engine |
|---|---|---|
| Terran | 549.4 | 552.0 (+0.5%) |
| Protoss | 546.8 | 536.0 (-2.0%) |
| Zerg | 541.4 | 533.3 (-1.5%) |

Combat, the 15 fights of the reference (seed 42): measured / reference. "Survivors": the winner's units alive, HP,
shields. "Series": the mean difference of HP + shields every 24 frames, in percent of the side's start (A / B).
"Seeds": in how many of 20 other seeds the reference's side wins.

| Fight | Winner | Frames | Survivors | Series | Seeds |
|---|---|---|---|---|---|
| 12 Marine vs 20 Zergling | B = B | 174 / 204 | B 11, 332 / 8, 239 | 5.5% / 5.5% | 20 |
| 6 Zealot vs 12 Marine | A = A | 337 / 288 | A 3, 267, 106 / 3, 266, 103 | 2.9% / 4.4% | 20 |
| 12 Marine + 4 Medic vs 20 Zergling + 4 Hydralisk | B = B | 256 / 308 | B 9, 382 / 7, 215 | 6.0% / 5.2% | 20 |
| 1 Marine vs 1 Zergling | B = B | 98 / 101 | B 1, 0 / 1, 0 | 0.0% / 5.7% | 20 |
| 8 Hydralisk vs 8 Dragoon | B = B | 253 / 270 | B 6, 558, 274 / 6, 503, 293 | 2.7% / 1.1% | 20 |
| 10 Zergling vs 4 Zealot | B = B | 233 / 229 | B 3, 203, 63 / 3, 170, 3 | 1.6% / 8.8% | 20 |
| 4 Siege Tank vs 12 Zergling | B = B | 261 / 301 | B 7, 201 / 5, 160 | 3.9% / 6.4% | 20 |
| 2 Reaver vs 16 Zergling | A = A | 38 / 114 | A 2, 200, 160 / 2, 200, 126 | 3.1% / 3.1% | 20 |
| 2 Carrier vs 12 Hydralisk | B = B | 315 / 353 | B 11, 803 / 11, 800 | 10.6% / 1.3% | 20 |
| 12 Mutalisk vs 6 Goliath | A = A | 309 / 291 | A 10, 1014 / 10, 1024 | 1.0% / 3.0% | 20 |
| 4 Lurker vs 12 Marine | A = A | 91 / 113 | A 4, 461 / 4, 417 | 6.3% / 2.1% | 20 |
| 8 Firebat vs 16 Zergling | A = A | 88 / 96 | A 7, 290 / 7, 294 | 1.9% / 2.5% | 20 |
| 6 Vulture vs 8 Zealot | B = B | 250 / 271 | B 6, 490, 244 / 5, 464, 240 | 3.1% / 0.7% | 20 |
| 4 Ultralisk vs 16 Marine | A = A | 201 / 227 | A 4, 1069 / 4, 1021 | 1.3% / 4.5% | 20 |
| 12 Scourge vs 2 Battlecruiser | B = B | 50 / 52 | B 1, 500 / 2, 679 | 8.3% / 7.2% | 20 |

All 15 winners as the reference (in all 20 other seeds too); the mean series difference is 4.0% (over the 20 seeds:
3.9%, and the end frame 27 frames from the reference's on average). Against the traces: the placement of 12 Marines, 12
Zerglings and 6 Dragoons is the reference's to the pixel; the mean position difference per unit (all frames / the first
48) is 14 / 8 px in fight 1, 56 / 19 px in fight 3, 8 px in fight 8, 10 / 8 px in fight 11, 14 / 11 px in fight 12 and 4
px in fight 15; the 16-Zergling crowding move differs by 4 px at frame 40 and 36 px on average.
The Marine's movement trace is the reference's to the pixel and the Zergling's is within 13 pixels after 60 frames.

**The CHECK set** (`tools/tests/sim-reference/reference-check.md`: 15 other fights, among them a choke between given
centres, and the mining of (4)Orbital Relay.scm) is held out: `sim_test` reports it, and the engine is not tuned to it.
Seed 42: the reference's winner in 15 of 15, mean series difference 3.8%, mean frame difference 23 (over the 20 seeds:
4.0%, and 27 frames from the reference's median; an Archon starts 17 frames after its creation; attack-move acquisition
takes the nearest target with no draw, also for Reavers and Carriers); the choke fight takes 195 frames against 211, 8
Corsairs 171 against 170. Orbital Relay mining: 12 runs (4 start locations, 3 races), mean difference 3.9%.

**Paths** (`tools/tests/sim-reference/reference-paths.md`): ground trips across Bottleneck and Orbital Relay (a Marine,
a Zergling, a Dragoon, an SCV, 12 Zerglings together, a short trip around a cliff). The path lengths are within 1.8% of
the reference's on average, the arrival frames within 6.7%; no unit's centre is ever on an unwalkable minitile. A unit
sent to a point it cannot reach stops at the reachable place nearest to it, and a Dragoon passes a bent corridor just as
wide as its box.

**Images**: each unit's main image, the overlays its scripts make (imgol, imgul and the like: the Archon's swirl, for
one), each playing its own script, and its turret; a new animation of the unit (walking, attack, death) starts in its
overlays too. Shadows are left to the editor. A dying unit's images play their Death animations, and the images and
sprites their scripts make (remnants, explosions) play until their end, the under kinds under the living units. These
pictures draw their own random numbers and have no part in the game. Each `LiveImage` has its sprite's `elevation`
level, the layer the editor draws by (lower levels first, then by `sortY` within one level):

- a unit, its overlays, turret and load: units.dat's Elevation Level (ground units 4, for example);
- a projectile and its hit: the level of the unit that fired it + `bulletElevationAbove` (rules.ini: 1), so hits
  are drawn on top of the ground units;
- a sprite that a script makes: sprol and grdsprol one level above its parent, sprul one below, highsprol the
  highest level (`elevationHighest`: 19), lowsprul the lowest (`elevationLowest`: 0: remnants under all); an image
  that a script makes (imgol, imgul) is part of its parent's sprite and has its level.

Script instructions for pictures: nobrkcodestart and nobrkcodeend (a new animation, such as the Archon's walking
during its Init, waits until the end of that part, so the Init's imgol of the Being and the Swirl runs);
tmprmgraphicstart and tmprmgraphicend (the image is not drawn between them: the Swirl's first frames);
trgtrangecondjmp (the jump when the unit's target is within the range); imgoluselo and imguluselo (the place from
the parent image's attack overlay .lo file at its frame, x mirrored with it). An overlay made by imgol is drawn just
above its parent image, one made by imgul just under it (the Archon: Energy, Being, attack overlay, Swirl).
setfldirect in an overlay's script (the Being's Init) is not applied: pictures do not change the game.

sim_test's Archon images check: the Archon's and the Dark Archon's Energy, Being and Swirl in that order, in their
frame ranges, and the Archon's attack overlay at its .lo offset.

sim_test's drawing layers check: in fight 1 and the Siege Tank fight, each hit image (a weapon's own image and the
sprites its script makes above it) has a level above the ground units'.

**Players and teams**: with `byPlayer`, placed units fight by player; players of one team (`team[8]`) are allies,
all others enemies; each unit attack-moves to the middle of its enemies; the result lists the teams and the winner.
The two-side mode (armies a and b, or placed units of side 0 and 1) is as before.

**Units in a fight**: no placed unit is removed. Every unit and building of a fighting player takes part; units that
cannot attack stand where they are, as targets. A building stands where placed; units collide with its units.dat box,
not its placement box (they walk in the gaps that its box leaves and reach it in melee range), and it is a target with
the last priority of the units that cannot attack (all buildings are complete; Zerg buildings need no creep). Only the
four static defences attack. Resources, invincible units, critters, units of the players 9 to 12 and the units with
`PlacedUnit::passive` (the map's units that are not selected) stay as the map's own units: they block movement and are
drawn, but they are never a target, never attack, take no damage or splash, and are not in the result. The army mode
(`a` and `b`) accepts only units and the four defences. The static defences (Missile Turret, Spore Colony, Sunken
Colony, Photon Cannon) stand where placed, look for a target every frame with no facing test, fire from their scripts
and detect (not the Sunken); Cannons count as powered. A unit that finds no way to its move target stays where it is and
tries again after `frozenFrames` (rules.ini: 24), so a unit held behind others goes round them when a place at its
target is free. Placed units that stand on one another go to the nearest free spots.

**Search and destroy** (placed units, both modes): a unit with no target that has reached its point, or every 24 frames
while it attack-moves, goes on to the nearest enemy it can attack and get at (ground units: in its walkable group;
flyers, and targets in the air: anywhere; a cloaked enemy with no detector near is left alone). The fight ends when no
team can get at and attack an enemy; each team's `unreachable` counts its units alive that no enemy could get at or
attack. When a unit goes again for the same enemy and that enemy is still in the same region, it walks on with no new
path. The fight also ends as a stalemate (`CombatResult::stalemate`, no winner) after 714 frames (30 seconds) with no
damage, no unit nearer than before (by 8 pixels) to the nearest enemy it can attack and get at, and no unit with a
shorter way left along its path than before (a long way round; at most the map's width and height of such walking until
it hits or is hit), or after 2 minutes (2856 frames) with no damage at all: for example two armies on each side of a
choke that passive units close. The army fights (sides a and b) are as before.

**Sounds**: `takeSounds` gives the sounds of a live game since the last call: the scripts' playsnd, playsndrand and
playsndbtwn of units, projectiles and death pictures (no unit responses).

**Speed** (sim_test PERF, Release): a 200-unit fight about 0.5 ms a frame, 400 units of 4 players about 0.5 ms, 800
units of 4 players about 1.3 ms, a mining game of 8 bases with 12 workers each well under 0.1 ms, and an order to an
unreachable place on a 256 x 256 map at most about 2 ms in one frame. The searches near a unit (targets, collision,
splash, the call for help, short paths) use a grid of 64-pixel cells made at the start of each frame and after its
movement, with the same choices as a search over all units; the minitile search keeps its buffers and tests each step
once; a goal in another group gets the nearest reachable place from the regions' groups first; a long path is planned 6
legs at a time. The editor's case (all the map's units passive, 120 selected units of 4 players, some that cannot be
reached; The Hunters and Bottleneck): at most about 3 ms in one frame. For that: a frame has at most 6 long path
searches and 10,000 minitiles of search (the others wait for the next frames); one search covers at most 6,000 minitiles
(then the unit walks to the nearest place found and searches on from there after a wait); a goal where the unit's box
does not fit becomes the nearest place near it where it fits; a goal a unit type could not reach from a region is
remembered with the place it reached (the terrain does not change), and a unit searches again for a goal it could not
reach only after 24 frames, then 48, 96 and 192.

**Defences**: the three probe fights go to the reference's side; mean series difference 4.7%.

## Known gaps

What is simplified or missing, and what it changes:

- **The short path's reach.** A ground unit's straight path in a fight goes at most 192 pixels along its main axis,
  then on to its move target: the reference's group stops there in the crowding test (each unit at its start + 192
  px, in its lane) before it goes on. The engine does not pause there as the reference does (20 to 40 frames; a pause
  did not change the results).
- **Ground collision.** Partial steps (full, half, quarter), then the collision reaction: settle, freeze,
  yield (with the 25-frame limit), a new short path around the blocker or around all moving units (deferred
  until the path is 7 frames old), the failed-path wait. The short path is the engine's own: an A* over 8-pixel
  steps within 128 pixels, around the units at rest. Not there: the slide and the
  overlap rule (no units overlap in these fights). Units that follow a target go to a free point of its outline.
- **Random draws.** The random headings at creation take the low 5 bits of a draw. In the reference the Marines'
  and Medics' headings are draws 115, 119, 123, ... of the stream (one in 4); the engine draws them in a row from the
  start, as what the other draws are is not known. So the random target picks of Reavers and Carriers differ too.
- **Reavers and Scarabs**: the 60-frame lock, the Scarab's 3-frame stand, 16 pixels a frame, the explosion within
  10 pixels, the blast 4 frames later. In the reference the Reavers' first Scarabs leave one Zergling alive (it
  started at the back) and the fight ends at 114 frames; here the Reavers take other targets of equal distance, the
  two blasts kill all 16, and the fight ends at 38 frames.
- **Carriers.** The target search takes Interceptors, and a hit by an Interceptor makes its Carrier
  the auto-target, and an attacking unit re-chooses its target only when it is ready to attack. The
  fight takes 315 frames (reference 353); the Hydralisks keep an Interceptor 27% of their unit-frames (reference
  39%).
- **Archons, Corsairs, cloaking**: the Archon's hit comes at the start of its attack (its animation has
  no attack instruction); air splash spares Interceptors; a Dark Templar, an Observer or a burrowed unit is seen only
  in range of an enemy detector. The Archons beat 16 Hydralisks here (the reference: the Hydralisks win), and 8
  Corsairs take 171 frames against 170: a unit whose attack animation still runs when its cooldown ends checks again
  the next frame (a Corsair fires every 8 frames).
- **Spider Mines and Siege Mode**: a placed Spider Mine arms (63 frames), burrows (9), watches a
  square of 32 x its acquisition range, unburrows (18), chases within 576 px, runs its fuse within 30 px (5 frames, 9
  when it did not move), dies, and its splash (friendly units too) lands 5 frames later; no collision from its burrow
  until it is up. Not there: a "hovering" exclusion that would keep a mine from Zerglings. A Siege
  Mode tank fights with its turret's data and does not move. Siege and unsiege (65 and 120 frames) and the Vulture's
  mines come only from orders, and a fight here gives none.
- **Scourge.** A Scourge is gone 3 frames after it fires. A hit that kills does not make the unit or its allies take the
  attacker as auto-target (the reference's Scourges fly on to the other centre: trace 13 to 4 px). Both
  Battlecruisers take the same Scourge for their second shots, so one more Scourge gets through, and one
  Battlecruiser dies (reference: both live, 679 HP).
- **Lurkers and Vultures** (91 / 113 and 250 / 271 frames): in the reference the last Marine walks past the burrowed
  Lurkers and dies at frame 113; the Vultures' first moves depend on their random headings.
- **Upgrades.** The map's start levels (PUPx, or UPGR) count: armor and weapons (units.dat's and weapons.dat's
  upgrades: damage bonus × level), and from rules.ini's `[upgrades]` the 9 speed upgrades (+1/2 speed for
  script-moved units too: an assumption), the 4 range upgrades (Charon Boosters: the Goliath's air weapon only),
  Adrenal Glands, Chitinous Plating, Reaver and Carrier Capacity, Caduceus Reactor and Plasma Shields. sim_test's
  upgrade check covers the speed and cooldown upgrades (a Zergling with Metabolic Boost and Adrenal Glands); a fight
  with the Marines' +3 weapons, +3 armor and U-238 Shells covers weapons, armor and range. The researched techs (PTEx,
  or PTEC) are read but nothing uses them yet (no spells). The other energy reactors are not applied.
- **Paths** (compact). A ground unit's box never goes onto an unwalkable minitile. Regions are
  rectangles of one class (at most 8 by 7 tiles, 3:1), without expansion and merging; the long path goes
  over their centres (toward the centre two regions ahead), each leg an A* over the minitiles, each with the place
  nearest its centre where the unit's box fits, and steps where the box fits all the way (or round a corner); a
  straight line that stays on walkable ground needs no path. The short path's corner expansion, its caps and
  the 30-frame scout check are not there.
- **Order ticks.** A new order runs at once; after that a unit's order runs every 9 frames on its own phase, fixed for
  its life: in a fight its number mod 9 (units made one after another act on frames one after another), in mining a hash
  of its number (the workers of a base are made in a row, and mod 9 there makes a Zerg base mine 5% too much). The
  game's own way of spreading the phases is not modelled.
- **The map's own units** (mineral fields and the like) stay in a fight, as in the reference: nobody attacks them, and
  they are in the way.
- **Special units.** Turret units (Siege Tank, Goliath) fire from the turret's script, and the turret faces its target
  at once; no siege or unsiege. Reavers: one Scarab out at a time, 25 pixels ahead, when facing the target. Lurkers
  burrow first (stop, turn, the burrow script) and their spines hit along a line (each unit within the inner radius of
  the way the spine went since its last hit), each target once in 32 frames. Mutalisk glaives bounce with /1, /3, /9.
  Firebat: the three flames of its script. Not there: spells, stim, and the 14 attack-position offsets.
- **Target choice.** Within a priority bucket the nearest (a Reaver's or a Carrier's new target in its attack: a random
  one), in unit-list order; no 16-candidate cap.
- **Mining.** The arrival at a field and the delivery at a depot each take one more order poll; workers walk to the edge
  of the field or depot box.
- **Images.** Each unit's main image (and its turret), with the script's frame and the facing, and a worker's load
  above it (at its main image's special overlay offset); mineral fields by their amount left; no other
  overlays, shadows, death animations or remapping.
- **Run the map** (`startMap`) is not in the simulation.

## Tests

Build without SDL and run the test from the repository's root:

    cmake -S . -B build-sim -DSTARDRAFT_RENDERER=OFF
    cmake --build build-sim --config Release --target sim_test
    build-sim\Release\sim_test.exe [<install>] [--series] [--seeds] [--trace <fight>]

It fails only when the game data, the map, the API, a path check or the upgrade check fails; the numbers are for a
person to compare.
