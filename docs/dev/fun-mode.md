# Fun mode (a spike)

Options > Fun mode, and Program options > Appearance > Fun mode (the same setting; off by default), gives feedback
when the map is edited:

- A short sound, made in code as bfxr and sfxr make them (a wave, a pitch slide, an envelope, one-pole filters and a
  "grain" that makes noise crunch, vibrato and one arpeggio step). No sound files. The sound is by what was edited:
  - terrain, by the terrain that the edited tiles have now and did not have before (painting water on dirt: the
    shore tiles count as water): water splashes; tar, lava, magma and mud gloop; grass, jungle, snow and dunes brush
    (the user's own bfxr setting: a noise swish that rises for 0.13 s and stops short); dirt crunches; rock, crags, shale and ice crunch harder; platform, plating, structure and the other built floors
    clank; space and pits drop away;
  - a placed doodad thunks, and the view shakes a little;
  - a placed unit plops (a building lower than a small unit), and squashes and springs back about its feet;
  - a deleted unit or doodad goes poof.
- Shards of the placed tiles fly off: each one is a jagged shape of 3 to 6 corners, 7 to 19 pixels across, cut from
  a place in the new tile at random with the tile's own pixels. Each one flies its own way at its own speed (some
  skid low, some go high), turns at its own rate and way, falls, gets smaller and fades. Water also throws white
  drops; metal throws sparks. A placed unit or doodad throws shards of the ground under it and a little dust.

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

## What works

- Every tool gets it because it is done from the map's changes: brush, spray, fill, shapes, paste, custom terrain,
  unit and doodad placement, Delete.
- In the snapshots, the water splash with its shards and drops reads at once; the Command Center's squash (wide, then
  tall, then still) shows in 3 frames. The sounds are only heard in WAV files so far: nobody has listened to them in
  the program yet.

## Limits now (rate and volume)

- At most one sound of each kind each 80 ms (50 to 60 ms for plops and thunks), at most 6 at a time. A long brush
  stroke is about 12 sounds a second. Each one is a little different (pitch +-12 %, noise at random).
- A sound's level goes up with the number of tiles that changed (log2), from 45 % to 100 %.
- At most 1,200 particles, of them at most 200 shards (up to about 250 rects each). About 100 tiles of an edit make
  shards (3 each); a fill of the whole map makes a few, spread out.

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
