# Technologies (techdata.dat)

techdata.dat has one entry for each technology (44 entries): the spells and abilities, for example Stim Packs or
Psionic Storm. For the units of the values, see [Values and units](values-and-units.md).

### Mineral cost

The minerals of the research.

- Raw value: minerals.

### Vespene cost

The vespene gas of the research.

- Raw value: vespene gas.

### Research time

The time to research the technology.

- Raw value: frames. One frame is approximately 42 milliseconds at the Fastest game speed.
- The form shows: seconds at Fastest (raw / 24).

### Energy required

The energy that a unit uses to cast the spell. This value applies only when a unit uses the technology as an ability.

- Raw value: energy points.

### Icon

The technology's icon on its button in the command card.

- Raw value: an icon (a frame of cmdicons.grp).

### Label

The technology's name. The tooltip of the technology's button shows this text.

- Raw value: a stat_txt.tbl string.

### Race

The race of the technology. Only the map editor (StarEdit) uses this value. The game does not use it.

- Raw value: a choice: Zerg, Terran, Protoss or neutral.

### Brood War flag

Yes: the technology is only in Brood War. The game does not use this value.

- Raw value: 0 (no) or 1 (yes).

### Unknown 4

In version 1.16.1, these bytes are two lists of 16-bit values, one value for each technology:

- the technology's place in the list of research requirements ("DatReqs Index");
- the technology's place in the list of use requirements ("UseReqs Index").

The game sets these values itself. Keep each 16-bit value at 65535. This field shows the bytes in groups of 4, so one
value of this field does not agree with one technology: it holds the 16-bit values of two technologies.

- Raw value: a number (4 bytes).

### Unused

The game does not use this field. In version 1.16.1, the community calls it "Researched".

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
