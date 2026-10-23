# Technologies (techdata.dat)

techdata.dat has one entry for each technology (44 entries): the spells and abilities, for example Stim Packs or
Psionic Storm. For the units of the values, see [Values and units](values-and-units.md).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

### Mineral cost

The mineral cost (Ore Cost).

- Raw value: minerals.

### Vespene cost

The vespene cost (Gas Cost).

- Raw value: vespene gas.

### Research time

Research time in logical frames. 1 is equal to 42 milliseconds at fastest game speed.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Energy required

Energy cost for casting the spell, if used as a unit ability.

- Raw value: energy points.

### Icon

Frame index within cmdicons.grp for the command card button.

- Raw value: an icon (a frame of cmdicons.grp).

### Label

String index within stat_txt.tbl for the command card tooltip.

- Raw value: a stat_txt.tbl string.

### Race

Used only by StarEdit.

- Raw value: a choice: Zerg, Terran, Protoss or neutral.

### Brood War flag

Unused in the game (BroodWar).

- Raw value: 0 (no) or 1 (yes).

### Unknown 4

These bytes are two arrays of 16-bit values, one value for each technology:

- DatReqs Index;
- UseReqs Index.

Intended to be set internally by the game, and should be left as 65535.

This field shows the bytes in groups of 4, so one value of this field does not agree with one technology: it holds the
16-bit values of two technologies.

- Raw value: a number (4 bytes).

### Unused

Researched: unused in the game.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The field descriptions are from the StarCraft DAT page of Darkened Fantasies. The rest of this page is from the other
  sources and from the game's data.
