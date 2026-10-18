# Movement (flingy.dat)

flingy.dat has one entry for each kind of movement (209 entries). A unit uses an entry through its graphics field in
[units.dat](units-dat.md#graphics), and a weapon's missile through its graphics field in
[weapons.dat](weapons-dat.md#graphics). For the units of the values, see [Values and units](values-and-units.md).

### Sprite

The sprite that moves: [sprites.dat](sprites-dat.md).

- Raw value: a sprites.dat entry.

### Speed

The top speed.

- Raw value: 1/256 pixels per frame. A raw speed of 1024 is 4 pixels each frame.
- The form shows: pixels each second at Fastest (raw x 24 / 256).

### Acceleration

How fast the unit gets to its top speed: the speed that it adds each frame.

- Raw value: 1/256 pixels per frame, each frame.
- The form shows: pixels each second, each second, at Fastest (raw x 24 x 24 / 256).

### Halt distance

The distance that the unit needs to stop from its top speed.

- Raw value: 1/256 pixels.
- The form shows: pixels (raw / 256).

### Turn radius

How fast the unit turns.

- Raw value: 1/256 of a full turn each frame.
- The form shows: degrees each second at Fastest (raw x 360 / 256 x 24).

### Movement control

What moves the unit: the values of this entry (flingy.dat), the animation script (iscript), or the values of this
entry with the weapon's movement.

- Raw value: a choice (the form names each one).

### Unused

The game does not use this field.

- Raw value: a number.

## Sources and credits

- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
