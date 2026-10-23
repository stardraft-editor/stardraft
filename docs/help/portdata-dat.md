# Portraits (portdata.dat)

portdata.dat has the portraits: the animations of a unit's face in the console. The game has 110 portraits. Each
portrait has an idle animation and a talking animation. The Data workspace shows 220 entries:

- entries 0 to 109 are the idle animations of portraits 0 to 109;
- entries 110 to 219 are the talking animations of portraits 0 to 109.

The [Portrait](units-dat.md#portrait) field of units.dat gives a unit its portrait (0 to 109).

### Portrait File

The animation's files: a string in portdata.tbl with the path and the start of the file name. The path starts in the
`portrait\` folder of the game's data.

A portrait can have more than one idle animation and more than one talking animation. Each file has the same name with
a number at the end, starting with 0. The string does not include this number. The game adds a random number to select
one of the animations. In version 1.16.1, the files are SMK videos. The game uses a maximum of 4 files for each
idle animation and 4 for each talking animation.

- Raw value: a portdata.tbl string.

### SMK Change

In version 1.16.1, the game uses these values to select the idle animation that plays next. The field name comes from
EUD Editor 2. The StarCraft DAT page of Darkened Fantasies gives each portrait four values, Idle Change 0 to 3:

- entries 0 to 109 of this field are Idle Change 0 of portraits 0 to 109;
- entries 110 to 219 of this field are Idle Change 1 of portraits 0 to 109, not values of the talking animations.

The other two values, Idle Change 2 and 3, are in [Unknown1](#unknown1). The game selects the idle animation as
follows:

1. The game gets a random number from 0 to 99.
2. It starts with animation 0 and a total of Idle Change 0.
3. While the random number is more than the total, the game goes to the next animation. It adds the Idle Change value
   of that animation to the total.
4. If the game goes past animation 3, it plays animation 0. Else it plays the animation where it stopped.

Thus a high Idle Change 0 makes animation 0 play more frequently.

- Raw value: a number (0 to 255). The game compares it with a random number from 0 to 99.

### Unknown1

In version 1.16.1, this field has the last two values that select the idle animation (see [SMK Change](#smk-change)):

- entries 0 to 109 of this field are Idle Change 2 of portraits 0 to 109;
- entries 110 to 219 of this field are Idle Change 3 of portraits 0 to 109.

Thus entries 110 to 219 are not values of the talking animations.

- Raw value: a number (0 to 255).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
