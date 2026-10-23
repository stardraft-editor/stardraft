# Upgrades (upgrades.dat)

upgrades.dat has one entry for each upgrade (61 entries), for example Terran Infantry Weapons. An upgrade can have more
than one level. The cost and the time of a level are the base value plus the factor for each level after the first:
level n costs base + factor x (n - 1). For the units of the values, see [Values and units](values-and-units.md).

### Mineral cost base

The minerals of the first level.

- Raw value: minerals.

### Mineral cost factor

The minerals that each level after the first adds to the cost.

- Raw value: minerals.

### Vespene cost base

The vespene gas of the first level.

- Raw value: vespene gas.

### Vespene cost factor

The vespene gas that each level after the first adds to the cost.

- Raw value: vespene gas.

### Research time base

The time to research the first level.

- Raw value: frames. One frame is approximately 42 milliseconds at the Fastest game speed.
- The form shows: seconds at Fastest (raw / 24).

### Research time factor

The time that each level after the first adds.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Max. repeats

The number of levels: how many times a player can research the upgrade. The map's settings (Upgrades) can set a
smaller maximum for each player.

- Raw value: levels.

### Icon

The upgrade's icon in the console and on its button in the command card.

- Raw value: an icon (a frame of cmdicons.grp).

### Label

The upgrade's name. The tooltip of the upgrade's button shows this text.

- Raw value: a stat_txt.tbl string.

### Race

The race of the upgrade. Only the map editor (StarEdit) uses this value. The game does not use it.

- Raw value: a choice: Zerg, Terran, Protoss or neutral.

### Broodwar flag

Yes: the upgrade is only in Brood War. The game does not use this value.

- Raw value: 0 (no) or 1 (yes).

### Unknown6

In version 1.16.1, this field is the upgrade's place in the list of research requirements (the community calls it
"DatReqs Index"). The game sets this value itself. Keep the value 65535.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
