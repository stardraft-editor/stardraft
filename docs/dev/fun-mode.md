# Fun mode (a spike)

Options > Fun mode, and Program options > Appearance > Fun mode (the same setting; off by default), gives feedback
when the map is edited:

- A short sound, made in code (no sound files; see "The sounds"). The sound is by what was edited:
  - terrain, by the terrain that the edited tiles have now and did not have before (painting water on dirt: the
    shore tiles count as water): water splashes and bubbles; tar, lava, magma and mud gloop; grass, jungle, snow and
    dunes rustle; dirt crunches; rock, crags, shale and ice click and crack; platform, plating, structure and the
    other built floors clank; space and pits drop away;
  - a placed doodad lands with a heavy thump, and the view shakes a little;
  - a placed unit lands softly (a building lower than a small unit), squashes and springs back about its feet, and
    the first one says its ready sound (units.dat's Ready Sound: units 0 to 105; buildings have none);
  - a deleted unit or doodad goes poof.
- Small shards of the placed terrain hop off (low and few: 1 or 2 a tile): each one is a jagged shape of 3 to 6
  corners, 5 to 12 pixels across, cut with the terrain's own pixels (from a tile of the edit that is all of the new
  terrain, so grass painted on dirt throws grass only). Each one goes its own way at its own speed, turns at its own
  rate and way, falls, gets smaller and fades. Water also throws white drops; metal throws sparks. A placed unit or
  doodad throws shards of the ground under it and a little dust.
- The sound and the shards are by the tiles that got a terrain they did not have: the dirt round a grass stroke,
  which the brush makes again, makes no dirt sound.
- What is placed is louder than what it replaces: the sound of the terrain that the edited tiles lost (dirt under
  painted grass, the ground under a doodad) plays with it at 35 % of its level.

## How it works

`apps/editor/core/fun_mode.inc`. No tool knows about it: after each `buildScene`, `funModeScene` compares the map with
its copy from the frame before.

- The tiles, when `terrainRevision` changed (a full compare of MTXM: 65,536 values at most, only on a change).
- The units by serial (a moved unit keeps its serial: no plop), the doodads by (id, x, y), when `revision` changed.
- A map that was read anew (`loadMark`: open, undo, redo) or another tab only takes a new copy: undo makes no sound.
- The pop is a new `SpriteDraw::scaleX/scaleY` about a pivot in the renderer. A shard is `RectDraw` fills, one for each
  of its pixels at its turned place (a little larger than a pixel, so no gaps show); the shake moves the scene's
  camera.
- The sounds go through `openPlaybackSdl` (`funPlayPcm` in `sdl_data.inc`): a test run plays nothing, and the volume
  is the program's volume. `STARDRAFT_FUN_WAV=<folder>` writes each sound to a WAV file, also in a test run.
- A snapshot's `wait` moves the particles and the pops on by that time (`funAdvanceFor`), so the frames of a script
  are the same each run.

The demo: `--script` with `command options.funmode`, the edits, and `wait 33` / `frame <png>` pairs after each.

## The sounds

They started as bfxr and sfxr sounds (a wave, a pitch slide, an envelope, one-pole filters). Then they were made more
natural from recorded game sounds (steps and landings on grass, dirt and sand, stone hits, item drops), measured for
length, attack, the number of small hits in them and their energy in bands:

- A step or a landing is 0.15 to 0.5 s of 4 to 13 small hits, not one smooth sound. Most of its energy is at 150 Hz
  to 2.5 kHz, very little above 6 kHz; a landing is brighter at its start than at its end. A soft landing on sand is
  mostly a thump under 150 Hz; a stone hit is a 13 ms click at 1 to 6 kHz.
- So the sounds are made of: grains (2 to 30 ms of noise through a two-pole band-pass, more of them early, each
  softer), a low thump (a sine that falls in pitch), bubbles (a sine that goes up as it rings out: water) and a metal
  plate's partials (not in tune with each other, each dying at its own speed). Grass keeps the user's bfxr swish
  (quieter, darker) under its rustle.
- Measured against the recordings: dirt has 11 to 14 hits and its center at about 1.9 kHz (the steps: 7 to 13, 1.8
  to 2.9 kHz); grass 6 to 12 hits at 2.5 to 3.3 kHz (the steps: 2 to 7, 1.9 to 3.1 kHz); a doodad's thump has 42 %
  of its energy under 150 Hz and 41 % at 400 Hz to 2.5 kHz (a hard landing: 35 % and 55 %).
- Each sound plays at a pitch of its own: 4 semitones down to 4 up, never the same step twice in a row (the whole
  sound faster or slower, so noise changes too). Each kind is set to one peak level (louder for larger edits).

## Recorded sounds

Water, mud, grass, dirt and rock play recorded sounds (from Freesound, picked by the user); the others are made in
code. Each recording gave up to 3 takes: its loudest moments (the splash, the squelch, the swish, the crunch), from
just before the rise to where the hit has died to 12 % of its peak, with no lead-in or long tail (a 2 ms fade-in, a
12 ms fade-out). Made tiny and bit-like: mono, 11,025 Hz, 64 levels, 8-bit WAV (0.08 to 0.15 s, 0.9 to 1.7 KB each,
18 KB for all 14). A play takes one of the takes at random
(not the last one again). The sounds made in code go through the same crush, so all of them sound alike.

The WAV files are in `apps/editor/res/fun/`; `tools/fun_sounds_gen.py` makes `fun_sounds.inc` from them (the
program has them in it: no files beside it).

| Sound | Takes | Freesound | Author | License |
|---|---|---|---|---|
| splash (water) | `splash1`, `splash2` | 763619 "Rock splash" | KalanaJ | CC0 1.0 |
| gloop (tar, lava, mud) | `gloop1` to `gloop3` | 207142 "MudSquelch.wav" | ahill86 | CC0 1.0 |
| brush (grass, jungle, snow, dunes) | `brush1` to `brush3` | 543508 "grass_rustling_uncut.wav" | Clusman | CC0 1.0 |
| crunch (dirt) | `crunch1` to `crunch3` | 512243 "Rock Smash" | NeoSpica | CC0 1.0 |
| rock (rock, crags, shale, ice) | `rock1` to `rock3` | 336023 "Rocks.wav" | adamgryu | CC0 1.0 |

CC0 1.0: no rights reserved, no credit needed (given here anyway); they can ship.

## What works

- Every tool gets it because it is done from the map's changes: brush, spray, fill, shapes, paste, custom terrain,
  unit and doodad placement, Delete.
- In the snapshots, the water splash with its shards and drops reads at once; the Command Center's squash (wide, then
  tall, then still) shows in 3 frames. The sounds are only heard in WAV files so far: nobody has listened to them in
  the program yet.

## Limits now (rate and volume)

- At most one sound of each kind each 80 ms (50 to 60 ms for plops and thunks), at most 6 at a time. A long brush
  stroke is about 12 sounds a second. Each one is a little different (its pitch step, its grains at random).
- A unit's ready sound: at half the volume, at most one each 0.6 s (a row or a paste of many units says it once), at
  most 2 at a time.
- A sound's level goes up with the number of tiles that changed (log2), from 45 % to 100 %.
- At most 1,200 particles, of them at most 120 shards (up to about 100 rects each). About 100 tiles of an edit make
  shards (1 or 2 each); a fill of the whole map makes a few, spread out.

## Open questions

- Volume: a slider of its own (Fun mode's volume), or the program's volume only? 12 sounds a second may be too much
  for some people: a "calm" level that plays only at a stroke's start and end?
- Should a stroke's sound pitch follow the mouse speed, or the brush size?
- Undo: silent now. A reversed sound could be fun.
- Moved units and doodads: no feedback now (a small thud at the drop?).
- Sprites, locations, triggers: nothing yet.
- Mod terrains (Cosmonarchy) are sorted by name; names that do not match go to the dirt crunch.
- The pop changes only the map's draw (not the minimap, the map image export or a layer's ghost).
- A shard is many rects. As a small textured, turned quad of the tileset it would cost 1 instance (the renderer's
  rects have no rotation or texture now).
- Shards have no shadow (on request): on their own terrain (dirt on dirt) they show less.
- A doodad's own tiles cannot pop (they are terrain); only the view shakes.
