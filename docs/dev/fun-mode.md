# Immersive mode (a spike; "fun mode" in the code)

Off by default, and locked: Program options > Appearance > Immersive mode shows a disabled check box, "Locked". A
hidden extra unlocks it: 9 clicks on the logo in Help > About StarDraft 2 (at each click the logo squashes and springs
back and green bits fly out of it with a thunk; at the 9th, "Immersive mode unlocked" shows under it for 2 seconds, and
the mode is on). From then on the option is a usual check box ("Sounds and effects for edits on the map"), and a click
on the logo turns the mode on or off too ("Immersive mode on", "Immersive mode off"). The settings are FunMode (the
mode) and ImmersiveUnlocked (the option is unlocked; a FunMode that is on counts as unlocked). In Immersive mode,
editing the map gives feedback:

- Painted terrain plays a short recorded sound (see "The sounds"), by the terrain that the edited tiles have now and
  did not have before (painting water on dirt: the shore tiles count as water): water splashes; tar, lava, magma and
  mud squelch; grass, jungle, snow and dunes swish; dirt crunches; rock, crags, shale and ice crunch; platform,
  plating, structure and the other built floors clank; space and pits whoosh. What was painted is the terrain of the
  tiles that became all of a terrain they did not have (the brush's rings of the terrains between it and the ground,
  Platform and High Platform round painted Space, do not count).
- A placed unit thuds softly (a building lower), squashes and springs back about its feet, and the first one says its
  ready sound (units.dat's Ready Sound: units 0 to 105; buildings have none). On a Cosmonarchy BW map it is the
  mod's only (cmbw::unitReadySound): one of the unit's range of sounds, as the mod's program picks them (71 units; 2
  have none, 1 has the adviser's sound), else units.datex's Ready Sound. A sound's file is its line of the mod's
  arr\sounds.txt (line n: the sound n, cmbw::soundFile), read from the mod's data, which has the game's files too.
  arr\sfxdata.dat's Sound File is not that line: do not use it. A placed doodad thunks and shakes the view a little.
- A deleted unit with a Death animation (iscript) plays it at its place, with the animation's sounds (at most 64 at
  a time, 240 ticks each at most); the map view draws frames until the last one ends. Other deleted units and
  doodads puff.
- A very large brush (an edit of 80 tiles or more: a brush of about 12 and up, a big fill) shakes the view a little,
  more for more tiles (up to 1.5 times a doodad's thunk), at most every 0.25 s: a long drag is a light rumble.
- Every changed tile throws small shards of its own new tile (a border tile its own mix of terrains), low and few:
  2 a tile, 1 when more than 40 tiles changed, spread over all of them (about 110 tiles at most a time). Each one is a
  jagged shape of 3 to 6 corners, 5 to 12 pixels across, cut from a place in the tile with its own pixels; it goes
  its own way at its own speed as its terrain moves (water higher, grass floats down, metal fast and short, space
  floats away), turns at its own rate and way, falls, gets smaller and fades. A placed doodad throws shards of its own
  tiles, a deleted one of its old tiles (it crumbles), a placed or deleted unit of the ground under it. No flat-color
  dots.
- The sound is by the tiles that got a terrain they did not have: the dirt round a grass stroke,
  which the brush makes again, makes no dirt sound.
- What is placed is louder than what it replaces: the sound of the terrain that the edited tiles lost (dirt under
  painted grass, the ground under a doodad) plays with it at 35 % of its level.

## How it works

`apps/editor/core/fun_mode.inc`. No tool knows about it: after each `buildScene`, `funModeScene` compares the map with
its copy from the frame before.

- The tiles, when `terrainRevision` changed (a full compare of MTXM: 65,536 values at most, only on a change).
- The units by serial (a moved unit keeps its serial: no pop), the doodads by (id, x, y), when `revision` changed.
- A map that was read anew (`loadMark`: open, undo, redo) or another tab only takes a new copy: undo makes no sound.
- The pop is a new `SpriteDraw::scaleX/scaleY` about a pivot in the renderer. A shard is `RectDraw` fills, one for each
  of its pixels at its turned place (a little larger than a pixel, so no gaps show); the shake moves the scene's
  camera.
- The sounds go through `openPlaybackSdl` (`funPlayPcm` in `sdl_data.inc`): a test run plays nothing, and the volume
  is the program's volume. `STARDRAFT_FUN_WAV=<folder>` writes each sound to a WAV file, also in a test run.
- A snapshot's `wait` moves the particles and the pops on by that time (`funAdvanceFor`), so the frames of a script
  are the same each run.

The demo: `--script` with `command craftmode` (the command that the logo runs), the edits, and `wait 33` / `frame <png>` pairs after each.

## The sounds

Every sound is a recorded take; the sounds first made in code (bfxr and sfxr style, then more natural layers of
grains, thumps and bubbles) are gone. Each play is at a pitch of its own: 4 semitones down to 4 up, never the same
step twice in a row (the whole sound faster or slower). Its level is louder for larger edits.

## Recorded sounds

All are CC0 1.0 recordings from Freesound (the first ones picked by the user; thunk, plop, poof and void found with
the Freesound API, a CC0 filter, from their HQ previews). Each kind has 6 takes: the main moments of its recordings
(from just before the rise to where the hit has died to 12 % of its peak, with no lead-in or long tail; a 2 ms
fade-in, a 12 ms fade-out), and, to make 6, variations of them: 2 or 3 semitones up or down, a low-pass, or another
moment under it at 35 %. Thunk is 2 semitones down and darker; void 5 semitones down and darker (hollow). Made tiny
and bit-like: mono, 11,025 Hz, 64 levels, 8-bit WAV (0.06 to 0.45 s, 0.7 to 5 KB each, 93 KB for all 60). A play
takes one of the takes at random (not the last one again).

The WAV files are in `apps/editor/res/fun/`; `tools/fun_sounds_gen.py` makes `fun_sounds.inc` from them (the
program has them in it: no files beside it).

| Sound | Freesound | Author | License |
|---|---|---|---|
| splash (water) | 763619 "Rock splash" | KalanaJ | CC0 1.0 |
| gloop (tar, lava, mud) | 207142 "MudSquelch.wav" | ahill86 | CC0 1.0 |
| brush (grass, jungle, snow, dunes) | 543508 "grass_rustling_uncut.wav" | Clusman | CC0 1.0 |
| crunch (dirt) | 512243 "Rock Smash" | NeoSpica | CC0 1.0 |
| rock (rock, crags, shale, ice) | 336023 "Rocks.wav" | adamgryu | CC0 1.0 |
| clank (platform, plating, structure, temple, basilica, outpost, the other built floors) | 558254 "Heavy Stone Push" | msavioti | CC0 1.0 |
| thunk (a placed doodad) | 215162 "thud.wav" | OtisJames | CC0 1.0 |
| thunk | 449955 "Wooden Thud (Mono)" | Breviceps | CC0 1.0 |
| thunk | 342535 "Medium Thud 1" | sgrowe | CC0 1.0 |
| plop (a placed unit) | 336495 "LowHit_01.wav" | Faulkin | CC0 1.0 |
| plop | 653910 "soft-hit.wav" | Krokulator | CC0 1.0 |
| plop | 536766 "Thud.ogg" | egomassive | CC0 1.0 |
| poof (a delete) | 208111 "Poof of Smoke" | Planman | CC0 1.0 |
| poof | 714257 "Puff of Smoke" | qubodup | CC0 1.0 |
| poof | 471204 "poof.wav" | AuDRoger | CC0 1.0 |
| void (space, pits) | 381853 "wind_gust_short_sqeeeek.wav" | sqeeeek | CC0 1.0 |
| void | 243400 "woosh.wav" | freakinbehemoth | CC0 1.0 |

Each sound's page: `https://freesound.org/s/<number>/`. CC0 1.0: no rights reserved, no credit needed (given here
anyway); they can ship.

## What works

- Every tool gets it because it is done from the map's changes: brush, spray, fill, shapes, paste, custom terrain,
  unit and doodad placement, Delete.
- In the snapshots, the water splash with its shards and drops reads at once; the Command Center's squash (wide, then
  tall, then still) shows in 3 frames. The sounds are only heard in WAV files so far: nobody has listened to them in
  the program yet.

## Limits now (rate and volume)

- Each sound plays to its end: the program mixes them itself, on one playback stream, each on a voice of its own (12
  voices; the sum through a soft limit, so many at once never clip). At most one sound of each kind each 80 ms, and at
  most 12 at a time: with all voices busy, a new sound takes the voice of the one nearest its end only when that one
  is in its fade-out (no loud stop), else it does not play. Each one that plays makes the next one softer (1 / (1 +
  0.5 n): 4 together are about 2.6 times one, 12 about 4.4), so a long stroke or a big fill is one soft texture, not a wall. The replaced terrain's sound starts after the placed
  one's, so it is ducked too and stays the quieter one.
- Each sound fades in for 8 ms and out for 30 ms (no hard start or stop); the takes stay short and punchy.
- Each one is a little different (its take and its pitch step).
- A unit's ready sound: at half the volume, at most one each 0.6 s (a row or a paste of many units says it once), at
  most 2 at a time.
- A sound's level goes up with the number of tiles that changed (log2), from 45 % to 100 %.
- At most 120 shards fly at a time (up to about 100 rects each); about 110 tiles of an edit throw them, spread
  over all its tiles, so a fill of the whole map makes a few.

## Open questions

- Volume: a slider of its own (Immersive mode's volume), or the program's volume only? 12 sounds a second may be too much
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
