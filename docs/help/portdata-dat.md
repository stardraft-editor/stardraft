# Portraits (portdata.dat)

portdata.dat has the portraits: the animations of a unit's face in the console. The game has 110 portraits. Each
portrait has an idle animation and a talking animation. The Data workspace shows 220 entries:

- entries 0 to 109 are the idle animations of portraits 0 to 109;
- entries 110 to 219 are the talking animations of portraits 0 to 109.

The [Portrait](units-dat.md#portrait) field of units.dat gives a unit its portrait (0 to 109).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

### Portrait File

Idle Dir and Talking Dir: the portdata.tbl string index for the SMK path and filename, in the MPQ `portrait\`
directory. The filenames should be formatted as to have the same filename suffixed with a number, starting with 0. The
portdata.tbl string is to be the filename without the number suffix, onto which the game will be appending a randomly
generated number to shuffle between the different portrait animations.

The game is restricted to a range of 4 SMKs per idle and talking portrait; SMKs whose filenames are suffixed with
numbers above 4 will not appear.

- Raw value: a portdata.tbl string.

### SMK Change

The field name comes from EUD Editor 2. The StarCraft DAT page of Darkened Fantasies gives each portrait four values,
Idle Change 0 to 3:

- entries 0 to 109 of this field are Idle Change 0 of portraits 0 to 109;
- entries 110 to 219 of this field are Idle Change 1 of portraits 0 to 109, not values of the talking animations.

The other two values, Idle Change 2 and 3, are in [Unknown1](#unknown1).

Idle Change 0 defines how likely the first idle portrait (index 0) will play. If the next random number (between 0-99)
is higher than this value, one of the other idle animations will play. Which animation will play depends on the value
of the random number and the other Idle Change fields.

Idle Change 1, 2 and 3: each of these values help determine which idle animation will play. Let R be the random
number, C be the value of Idle Change 0, and N be 0:

1. While R is greater than C, increment N by 1 and add the value of IdleChange[N] to C.
2. If N is greater than 3, animation index 0 is played. Otherwise animation index N is played.

- Raw value: a number (0 to 255).

### Unknown1

This field has Idle Change 2 and Idle Change 3 (see [SMK Change](#smk-change)):

- entries 0 to 109 of this field are Idle Change 2 of portraits 0 to 109;
- entries 110 to 219 of this field are Idle Change 3 of portraits 0 to 109.

Thus entries 110 to 219 are not values of the talking animations.

- Raw value: a number (0 to 255).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The field descriptions are from the StarCraft DAT page of Darkened Fantasies. The rest of this page is from the other
  sources and from the game's data.
