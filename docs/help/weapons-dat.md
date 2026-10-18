# Weapons (weapons.dat)

weapons.dat has one entry for each weapon (130 entries). A unit uses a weapon through its ground weapon and air
weapon fields in [units.dat](units-dat.md#ground-weapon); spells and some orders use weapons too. For the units of the
values, see [Values and units](values-and-units.md).

## Damage

### Damage amount

The damage of one hit at upgrade level 0.

- Raw value: damage points.

### Damage bonus

The damage that each level of the damage upgrade adds.

- Raw value: damage points.

### Damage upgrade

The upgrade that gives the weapon more damage.

- Raw value: an [upgrades.dat](upgrades-dat.md) entry.

### Damage factor

The number of times that the damage goes on the target for each hit (for example 2 for a weapon that fires two
missiles at the same time).

- Raw value: a number.

### Weapon type

The type of the damage. Explosive damage does 50% to small units and 75% to medium units. Concussive damage does 50% to
medium units and 25% to large units. Normal damage does the full damage to all sizes. Ignore armor does not use the
target's armor.

- Raw value: a choice: independent, explosive, concussive, normal or ignore armor.

### Explosion type

How the weapon gives its damage: to one target, in an area (splash), to enemies only, or a special effect (for example
Lockdown, Ensnare or Plague).

- Raw value: a choice (the form names each one).

### Weapon cooldown

The time between two attacks.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

## Range and splash

### Minimum range

The smallest distance at which the weapon can attack (the Siege Tank in siege mode has one).

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Maximum range

The largest distance at which the weapon can attack.

- Raw value: pixels. A Marine's weapon has 128 (4 tiles).
- The form shows: tiles (raw / 32).

### Inner splash range

For splash damage: the units in this distance from the hit get the full damage.

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Medium splash range

For splash damage: the units in this distance from the hit get 50% of the damage.

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Outer splash range

For splash damage: the units in this distance from the hit get 25% of the damage.

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Target flags

The targets that the weapon can attack: air, ground, mechanical, organic, not buildings, not robotic, terrain, organic
or mechanical, and own units.

- Raw value: a set of flags.

### Attack angle

The unit attacks only when it faces its target within this angle.

- Raw value: 1/256 of a full turn.
- The form shows: degrees (raw x 360 / 256).

## The missile

### Graphics

The movement entry of the weapon's missile or effect: [flingy.dat](flingy-dat.md).

- Raw value: a flingy.dat entry.

### Weapon behavior

How the missile moves: it flies to the target, it appears on the target, it bounces between targets (the Mutalisk), or
it goes down from above (Psionic Storm), and more.

- Raw value: a choice (the form names each one).

### Remove after

The time that the missile stays before the game removes it.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Launch spin

The angle from the unit's direction at which the missile leaves the unit (the missiles of the Wraith and the Valkyrie
turn out to the sides).

- Raw value: 1/256 of a full turn.
- The form shows: degrees (raw x 360 / 256).

### Forward offset

Where the missile starts: its distance in front of the unit.

- Raw value: pixels.

### Upward offset

Where the missile starts: its height above the unit.

- Raw value: pixels.

### Unused

The game does not use this field.

- Raw value: a number.

## Names and messages

### Label

The weapon's name in the console.

- Raw value: a stat_txt.tbl string.

### Target error message

The message when the target is not a target that the weapon can attack.

- Raw value: a stat_txt.tbl string.

### Icon

The weapon's icon in the console.

- Raw value: an icon (cmdicons.grp frame).

## Sources and credits

- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
