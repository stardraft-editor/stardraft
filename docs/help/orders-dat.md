# Orders (orders.dat)

orders.dat has one entry for each order (189 entries). An order is what a unit does now: move, attack, gather, build,
cast a spell, and more. A unit's idle and attack orders are in [units.dat](units-dat.md#comp-ai-idle).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1)
(<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).

### Label

Unused. References a stat_txt.tbl string index for the order name.

- Raw value: a stat_txt.tbl string.

### Use weapon targeting

The order will abide by the targeting rules set for the specified weapon ID by the [Targeting](#targeting) field.

- Raw value: 0 (no) or 1 (yes).

### Can be interrupted

If set, the order can be removed from the shift-key queue by another order.

- Raw value: 0 (no) or 1 (yes).

### Can be queued

If set, the order can be added to the shift-key queue.

- Raw value: 0 (no) or 1 (yes).

### Targeting

Darkened Fantasies name: Weapon.

[Weapons.dat](weapons-dat.md) index for various purposes related to the weapon's properties, with targeting orders.

- Raw value: a [weapons.dat](weapons-dat.md) entry. 130 is none.

### Energy

[Techdata.dat](techdata-dat.md) index to determine the energy cost to perform the order.

- Raw value: a [techdata.dat](techdata-dat.md) entry. 44 is none.

### Animation

Iscript animation that will be played when performing the order. Effective only for the following spellcasting
abilities:

| Order | Name |
| --- | --- |
| 113 | Yamato Gun |
| 115 | Lockdown |
| 119 | Dark Swarm |
| 120 | Parasite |
| 121 | Spawn Broodlings |
| 122 | EMP Shockwave |
| 142 | Psionic Storm |
| 143 | Irradiate |
| 144 | Plague |
| 145 | Consume |
| 146 | Ensnare |
| 147 | Stasis Field |
| 180 | Restoration |
| 181 | Disruption Web |
| 185 | Optical Flare |
| 186 | Maelstrom |

- Raw value: a choice (the form names each one).

### Highlight

Frame index within cmdicons.grp that will appear highlighted while performing the order.

- Raw value: an icon (cmdicons.grp frame). 65535 is none.

### Obscured order

Darkened Fantasies name: Obscured.

When targeting a fog sprite, this is the orders.dat index for the order to be performed once the target becomes
revealed. Effective only for the following orders:

| Order | Name |
| --- | --- |
| 009 | Attack Obscured |
| 028 | Use Ability (infest 2) |
| 035 | Repair Obscured |
| 054 | Attack Obscured (carrier) |
| 060 | Attack Obscured (reaver) |
| 080 | Move to Harvest Obscured |
| 114 | Cast Yamato Gun Obscured |

- Raw value: an orders.dat entry.

### Unknown2

Unused.

- Raw value: a number.

### Unknown3

Unused.

- Raw value: a number.

### Unknown4

Darkened Fantasies name: Subunit Inheritance.

If set, when the base unit performs this order, the subunit also will.

- Raw value: a number.

### Unknown5

Unused.

- Raw value: a number.

### Unknown7

Darkened Fantasies name: Can Be Lifted.

Specific to orders that target a unit. If set, the "Lifted" (0x20) CUnit movement flag will be cleared.

- Raw value: a number.

### Unknown9

Darkened Fantasies name: Disabling Keeps Target.

When the unit becomes frozen by Lockdown, Stasis, or Maelstrom, the targeted unit will not be lost.

- Raw value: a number.

### Unknown10

Darkened Fantasies name: Can Be Obstructed.

Specific to orders that target a unit via mouse click. If set, the attacker will move towards the target if it becomes
obscured by the fog of war until it is back in sight and weapon range.

- Raw value: a number.

### Unknown11

Darkened Fantasies name: Fleeable.

If set, the unit will flee from an attacker it cannot retaliate against. Applies only to units that have the "Reacts"
(0x20000) CUnit status flag set, which are not under Dark Swarm, and are not computer guard units that are not at
their post.

- Raw value: a number.

### Unknown12

Unused.

- Raw value: a number.

### Unknown17

Darkened Fantasies name: DatReqs Index.

Intended to be set internally by the game, and should be left as 65535.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book (<https://armoha.github.io/eud-book/>), the community's book about
  the game's memory and data.
- The field descriptions are from the StarCraft DAT page of Darkened Fantasies. The rest of this page is from these
  other sources and from the game's data.
