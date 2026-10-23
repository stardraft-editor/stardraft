# Upgrades (upgrades.dat)

upgrades.dat has one entry for each upgrade (61 entries), for example Terran Infantry Weapons. An upgrade can have more
than one level. The cost and the time of a level are the base value plus the factor for each level after the first:
level n costs base + factor x (n - 1). For the units of the values, see [Values and units](values-and-units.md).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

### Mineral cost base

The initial mineral cost (Ore Cost).

- Raw value: minerals.

### Mineral cost factor

By how much the mineral cost will increase per level (Ore Cost Factor).

- Raw value: minerals.

### Vespene cost base

The initial vespene cost (Gas Cost).

- Raw value: vespene gas.

### Vespene cost factor

By how much the vespene cost will increase per level (Gas Cost Factor).

- Raw value: vespene gas.

### Research time base

Research time in logical frames. 1 is equal to 42 milliseconds at fastest game speed.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Research time factor

By how long the time cost will increase per level.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Max. repeats

How many times the upgrade can be researched. The map's settings (Upgrades) can set a smaller maximum for each player.

- Raw value: levels.

### Icon

Frame index within cmdicons.grp for the command card button.

- Raw value: an icon (a frame of cmdicons.grp).

### Label

Stat_txt.tbl string index for the command card tooltip.

- Raw value: a stat_txt.tbl string.

### Race

Used only by StarEdit.

- Raw value: a choice: Zerg, Terran, Protoss or neutral.

### Broodwar flag

Unused in the game (BroodWar).

- Raw value: 0 (no) or 1 (yes).

### Unknown6

DatReqs Index: intended to be set internally by the game, and should be left as 65535.

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
