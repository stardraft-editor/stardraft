# Orders (orders.dat)

orders.dat has one entry for each order (189 entries). An order is what a unit does now: move, attack, gather, build,
cast a spell, and more. A unit's idle and attack orders are in [units.dat](units-dat.md#comp-ai-idle).

### Label

The order's name.

The game does not use this value.

- Raw value: a stat_txt.tbl string.

### Use weapon targeting

Yes: the order obeys the target rules of the weapon in the Targeting field.

- Raw value: 0 (no) or 1 (yes).

### Can be interrupted

Yes: a different order can remove this order from the unit's list of orders (the Shift list).

- Raw value: 0 (no) or 1 (yes).

### Can be queued

Yes: a player can add the order to the unit's list of orders (with Shift).

- Raw value: 0 (no) or 1 (yes).

### Targeting

The weapon whose targets the order uses (for example a spell's weapon).

The game uses the properties of this weapon for different purposes in orders that have a target.

- Raw value: a [weapons.dat](weapons-dat.md) entry. 130 is none.

### Energy

The technology whose energy the order uses.

The energy cost of this technology is the energy that the unit uses for the order.

- Raw value: a [techdata.dat](techdata-dat.md) entry. 44 is none.

### Animation

The animation of the unit's script that the order plays (for example the attack or the cast animation).

The game uses this value only for these spell orders:

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

The button that shows as active while the unit does the order.

- Raw value: an icon (cmdicons.grp frame). 65535 is none.

### Obscured order

The order that the unit does when it cannot see its target.

When the target is a fog sprite, the unit does this order after the target becomes visible again. The game uses this
value only for these orders:

| Order | Name |
| --- | --- |
| 9 | Attack Obscured |
| 28 | Use Ability (Infest 2) |
| 35 | Repair Obscured |
| 54 | Attack Obscured (Carrier) |
| 60 | Attack Obscured (Reaver) |
| 80 | Move to Harvest Obscured |
| 114 | Cast Yamato Gun Obscured |

- Raw value: an orders.dat entry.

### Unknown2

In version 1.16.1, the game does not use this field.

- Raw value: a number.

### Unknown3

In version 1.16.1, the game does not use this field.

- Raw value: a number.

### Unknown4

The Darkened Fantasies page names this field "Subunit Inheritance". Yes (1): when the base unit does this order, its
subunit (for example a turret) also does it.

- Raw value: a number.

### Unknown5

In version 1.16.1, the game does not use this field.

- Raw value: a number.

### Unknown7

The Darkened Fantasies page names this field "Can Be Lifted". It applies only to orders that have a target unit. Yes
(1): the order clears the "Lifted" movement flag (0x20) of the unit.

- Raw value: a number.

### Unknown9

The Darkened Fantasies page names this field "Disabling Keeps Target". Yes (1): the unit keeps its target when
Lockdown, Stasis Field or Maelstrom stops it.

- Raw value: a number.

### Unknown10

The Darkened Fantasies page names this field "Can Be Obstructed". It applies only to orders that target a unit with a
mouse click. Yes (1): when the fog of war hides the target, the unit moves to it. The unit moves until it sees the
target and the target is in weapon range.

- Raw value: a number.

### Unknown11

The Darkened Fantasies page names this field "Fleeable". Yes (1): the unit runs away from an attacker that it cannot
attack. This applies only to a unit that has these conditions:

- The unit has the "Reacts" status flag (0x20000).
- The unit is not under Dark Swarm.
- The unit is not a computer guard unit that is away from its post.

- Raw value: a number.

### Unknown12

In version 1.16.1, the game does not use this field.

- Raw value: a number.

### Unknown17

The Darkened Fantasies page names this field "DatReqs Index". The game sets this value itself. Keep it at 65535.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book (<https://armoha.github.io/eud-book/>), the community's book about
  the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
