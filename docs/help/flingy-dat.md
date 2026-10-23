# Movement (flingy.dat)

flingy.dat has one entry for each kind of movement (209 entries). A unit uses an entry through its graphics field in
[units.dat](units-dat.md#graphics), and a weapon's missile through its graphics field in
[weapons.dat](weapons-dat.md#graphics). For the units of the values, see [Values and units](values-and-units.md).

### Sprite

The sprite that moves: [sprites.dat](sprites-dat.md).

- Raw value: a sprites.dat entry.

### Speed

The top speed. When the unit gets to this speed, it stops its acceleration.

- Raw value: 1/256 pixels per frame. A raw speed of 1024 is 4 pixels each frame. A raw speed of 256 is 1 pixel each
  frame, the same distance as a "move 1" in the iscript.
- The form shows: pixels each second at Fastest (raw x 24 / 256).

### Acceleration

How fast the unit gets to its top speed: the speed that it adds each frame.

- Raw value: 1/256 pixels per frame, each frame.
- The unit also uses this value to decrease its speed when it stops.
- If the unit gets to its destination before it stops fully, it stops immediately.
- The form shows: pixels each second, each second, at Fastest (raw x 24 x 24 / 256).

### Halt distance

The distance that the unit needs to stop from its top speed. At this distance from the destination, the unit starts to
decrease its speed.

- Raw value: 1/256 pixels.
- Most units in the game have this value: speed x speed / (acceleration x 2), rounded to the nearest integer.
- When the unit does not move at its top speed, the game calculates the distance with the same formula. It then does
  not use this field.
- The form shows: pixels (raw / 256).

### Turn radius

How fast the unit turns.

- Raw value: 1/256 of a full turn each frame. This is the largest angle that the unit can turn in one frame.
- A value of 127 or more lets the unit turn 180 degrees immediately.
- The form shows: degrees each second at Fastest (raw x 360 / 256 x 24).

### Movement control

What moves the unit: the values of this entry (flingy.dat), the animation script (iscript), or the values of this
entry with the weapon's movement.

- Raw value: a choice (the form names each one).
- With flingy.dat control, the game calculates the speed each frame from the speed, acceleration and halt distance.
- With iscript.bin control, the "move" command of the iscript moves the unit a number of pixels.
- Do not use values above 2. The result is not known.

### Unused

The game does not use this field.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
