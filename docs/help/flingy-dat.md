# Movement (flingy.dat)

flingy.dat has one entry for each kind of movement (209 entries). A unit uses an entry through its graphics field in
[units.dat](units-dat.md#graphics), and a weapon's missile through its graphics field in
[weapons.dat](weapons-dat.md#graphics). For the units of the values, see [Values and units](values-and-units.md).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

### Sprite

[Sprites.dat](sprites-dat.md) index for the entity's sprite.

- Raw value: a sprites.dat entry.

### Speed

Speed at which acceleration will cease (the source calls it Top Speed). A top speed of 256 is equal to 1 pixel per
logical step, which is also equal to a movement of 1 in Iscript.

- Raw value: 1/256 pixels per frame. A raw speed of 1024 is 4 pixels each frame.
- The form shows: pixels each second at Fastest (raw x 24 / 256).

### Acceleration

Amount by which speed is incremented per logical step until Top Speed is reached. It is also the amount by which speed
is decremented when decelerating until speed is 0. If the destination has been reached before the entity has fully
decelerated, the unit will stop instantly.

- Raw value: 1/256 pixels per frame, each frame.
- The form shows: pixels each second, each second, at Fastest (raw x 24 x 24 / 256).

### Halt distance

Distance at which the entity will begin decelerating when moving at top speed. A value of 256 is equal to a distance of
1 pixel. The value used by most units in the stock game is Top Speed * Top Speed / (Acceleration * 2), rounded to the
nearest integer. This is the formula the game uses to calculate the halt distance when the unit is not moving at top
speed.

- Raw value: 1/256 pixels.
- The form shows: pixels (raw / 256).

### Turn radius

Maximum amount of angles the entity can turn per logical step (the source calls it Turn Rate). 256 angles make up 360
degrees, and a turn rate of at least 127 will provide instantaneous 180-degree turns.

- Raw value: 1/256 of a full turn each frame.
- The form shows: degrees each second at Fastest (raw x 360 / 256 x 24).

### Movement control

Defines whether the entity will use Flingy physics or Iscript animation for movement. Iscript movement is achieved with
the move opcode, which advances the entity by a specified amount of pixels. Flingy movement calculates the entity's
velocity each frame based on the Top Speed, Acceleration, and Halt Distance.

- Raw value: a choice (the form names each one).

- 0 Flingy control (the form calls it flingy.dat control)
- 1 Flingy control without deceleration (the form calls it Partially mobile, weapon)
- 2 Iscript control (the form calls it iscript.bin control)
- Other values have undefined behavior.

### Unused

Unused.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The field descriptions are from the Darkened Fantasies page. The rest of this page is from the other sources and from
  the game's data.
