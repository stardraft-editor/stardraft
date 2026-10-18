# Values and units

The game keeps each value of its data as a whole number: the raw value. Many raw values count small units, for
example frames or 1/256 pixels. The Data form shows the raw value in its box. When the raw value has a unit that is
difficult to read, the form shows the converted value after the box (for example "= 15 s (fastest)").

## Frames and time {#frames}

The game calculates in frames. At the game speed Fastest, the game runs approximately 24 frames each second (one frame
is 42 ms). The online game always uses Fastest.

- Raw value: frames.
- Converted value: seconds at Fastest (raw / 24).
- Example: a build time of 360 frames is 15 seconds at Fastest.

## Pixels and tiles {#pixels}

A tile of the map is 32 by 32 pixels.

- Weapon ranges are in pixels. The form shows them in tiles too (raw / 32). Example: a range of 128 pixels is 4 tiles.
- Sight ranges and target acquisition ranges are in tiles. The form shows them in pixels too (raw x 32).

## 1/256 parts {#fractions}

Some values have more precision than a whole number. The game keeps them in 1/256 parts.

- Hit points: raw in 1/256 hit points. A Marine has 40 hit points: the raw value is 10240.
- Speed: raw in 1/256 pixels per frame. A raw speed of 1024 is 4 pixels each frame, or 96 pixels each second at
  Fastest.
- Acceleration: raw in 1/256 pixels per frame, each frame.
- Halt distance: raw in 1/256 pixels.

## Directions and angles {#angles}

The game divides a full turn into 256 parts. The values that are angles (attack angle, launch spin, turn speed) count
these parts. The form shows them in degrees (raw x 360 / 256). Units face one of 32 directions (0 is up; the numbers
go clockwise).

## Supply {#supply}

The game counts supply in halves. A unit that takes 1 supply (a Marine) has the raw value 2. The form shows the
supply (raw / 2).

## Sources and credits

- The units of the values: the EUD Book (<https://armoha.github.io/eud-book/>), the community's book about the game's
  memory and data, and EUD Editor 2 (<https://github.com/Buizz/EUDEditor>, MIT license, LeeJungHun).
- The StarDraft 2 developers wrote this page in their own words.
