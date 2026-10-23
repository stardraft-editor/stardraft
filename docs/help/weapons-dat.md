# Weapons (weapons.dat)

weapons.dat has one entry for each weapon (130 entries). A unit uses a weapon through its ground weapon and air
weapon fields in [units.dat](units-dat.md#ground-weapon); spells and some orders use weapons too. For the units of the
values, see [Values and units](values-and-units.md).

## Damage

### Damage amount

The damage of one hit at upgrade level 0.

- Raw value: damage points.
- The damage of one hit at an upgrade level: damage amount + (damage bonus x upgrade level).

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

| Value | Type | Small | Medium | Large |
|---|---|---|---|---|
| 0 | Independent | 0% | 0% | 0% |
| 1 | Explosive | 50% | 75% | 100% |
| 2 | Concussive | 100% | 50% | 25% |
| 3 | Normal | 100% | 100% | 100% |
| 4 | Ignore armor | 100% | 100% | 100% |

- All types do 0% damage to units with the size "independent".
- Ignore armor does not use the target's armor. It also does not use the target's shield upgrades.
- Do not use values above 4. The game then reads data outside its table, and the result is not known.

### Explosion type

How the weapon gives its damage: to one target, in an area (splash), to enemies only, or a special effect (for example
Lockdown, Ensnare or Plague).

- Raw value: a choice (the form names each one).

The values and what they do in version 1.16.1 (times are at Fastest):

| Value | Name | What it does |
|---|---|---|
| 0 | None | Nothing occurs to the target. The SCV and the Probe use it when they mine. |
| 1 | Normal hit | The target gets the full damage. If the missile misses, the target gets no damage. |
| 2 | Splash (radial) | All units near the hit get splash damage. The splash ranges set the distances. |
| 3 | Splash (enemy) | Same as splash (radial), but the units of the attacking player get no damage. |
| 4 | Lockdown | The target cannot move or act for approximately 44 seconds. |
| 5 | Nuclear missile | Same as splash (radial). The damage is one third of the unit's hit points, with a minimum of 500. If the unit has shields, the game adds its shields to its hit points. |
| 6 | Parasite | The game puts a parasite on the target. |
| 7 | Broodlings | The target dies, and two Broodlings appear on its position. The Broodlings stay for 1800 frames. |
| 8 | EMP shockwave | All units near the hit lose all their shields and energy. The area is a rectangle. The inner splash range sets its size. |
| 9 | Irradiate | The target gets radiation for 8 x the Irradiate weapon's cooldown. Each 8 frames, the radiation does the Irradiate weapon's damage divided by its cooldown. |
| 10 | Ensnare | All units in a 128 x 128 pixel rectangle around the hit move more slowly for approximately 24 seconds. Burrowed units are not affected. |
| 11 | Plague | All units in a 128 x 128 pixel rectangle around the hit get Plague for approximately 24 seconds. Each 8 frames, Plague does 1/71 of the Plague weapon's damage. Burrowed units are not affected. |
| 12 | Stasis field | All units in a 96 x 96 pixel rectangle around the hit cannot move or act, and cannot get damage, for approximately 44 seconds. Buildings and burrowed units are not affected. |
| 13 | Dark swarm | A Dark Swarm appears on the hit. It stays for 900 frames. |
| 14 | Consume | The target dies, and the attacker gets 50 energy. |
| 15 | Yamato gun | Same as normal hit, but the target gets the full damage also when the missile misses. |
| 16 | Restoration | The target loses these effects: Parasite, Optical Flare, Ensnare, Plague, Irradiate, Lockdown, Maelstrom and acid spores. |
| 17 | Disruption web | A Disruption Web appears on the hit. It stays for 360 frames. |
| 18 | Corrosive acid | The target gets damage. All units in a 128 x 128 pixel rectangle around the hit get 1 acid spore for approximately 50 seconds. |
| 19 | Mind control | Only the order can use this value. With a missile, the game can crash. |
| 20 | Feedback | Only the order can use this value. With a missile, the game can crash. |
| 21 | Optical flare | The target becomes blind. |
| 22 | Maelstrom | All organic units in a 96 x 96 pixel rectangle around the hit cannot move or act for approximately 7 seconds. Buildings and burrowed units are not affected. |
| 23 | Unused | The game does not use this value. It can cause a crash. |
| 24 | Splash (air) | Damage to air units around the target. See below. |

- Splash (air): if the target is the only unit in the outer splash range, it gets the full damage. If not, one random
  unit in the outer splash range gets the full damage. The other air units in the range get less damage, by the medium
  and outer splash ranges. Interceptors and the units of the attacking player get no damage.
- All effects go only to units that can get damage.

### Weapon cooldown

The time between two attacks: the number of frames before the weapon can attack again.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

## Range and splash

### Minimum range

The smallest distance at which the weapon can attack (the Siege Tank in siege mode has one). The unit cannot attack
a target that is nearer than this distance.

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Maximum range

The largest distance at which the weapon can attack.

- Raw value: pixels. A Marine's weapon has 128 (4 tiles).
- The form shows: tiles (raw / 32).

### Inner splash range

For splash damage: the units in this distance from the hit get the full damage.

- Raw value: pixels.
- The explosion type EMP shockwave also uses this field. It sets the size of the rectangle that loses shields and
  energy.
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

| Flag | Name | Meaning |
|---|---|---|
| 0x0001 | Air | The weapon can attack air units. |
| 0x0002 | Ground | The weapon can attack ground units. |
| 0x0004 | Mechanical | The target must be mechanical. |
| 0x0008 | Organic | The target must be organic. |
| 0x0010 | Non-building | The weapon cannot attack buildings. |
| 0x0020 | Non-robotic | The weapon cannot attack robotic units. |
| 0x0040 | Terrain | The weapon can attack a position on the terrain. |
| 0x0080 | Organic or mechanical | The target must be organic or mechanical. |
| 0x0100 | Own | The target must be a unit of the same player. |

- If you set Mechanical and Organic, the target must be mechanical and organic. If you set only "Organic or
  mechanical", the target can be one or the other.

### Attack angle

The unit attacks only when it faces its target within this angle.

- Raw value: 1/256 of a full turn. The value 0 means directly to the front. A value of 128 or more lets the unit attack
  in all directions.
- The form shows: degrees (raw x 360 / 256).

## The missile

### Graphics

The movement entry of the weapon's missile or effect: [flingy.dat](flingy-dat.md).

- Raw value: a flingy.dat entry.

### Weapon behavior

How the missile moves: it flies to the target, it appears on the target, it bounces between targets (the Mutalisk), or
it goes down from above (Psionic Storm), and more.

- Raw value: a choice (the form names each one).

| Value | Name | What it does |
|---|---|---|
| 0 | Fly and do not follow target | The missile flies to the position where the target was when the unit fired (the Vulture). |
| 1 | Fly and follow target | The missile follows the target until it hits (the Dragoon). |
| 2 | Appear on target unit | The missile hits the target immediately (the Marine). |
| 3 | Persist on target site | The missile appears on the target and stays. It does damage each 8 frames until the "Remove after" time ends. It plays the SpecialState2 animation of the iscript (Psionic Storm). |
| 4 | Appear on target site | Same as value 2. |
| 5 | Appear on attacker | The missile hits immediately on the attacker (the Infested Terran). |
| 6 | Attack and self-destruct | Same as value 5. The game also removes the attacker, with no death animation (the Scourge). |
| 7 | Bounce | The missile bounces a maximum of 2 times. Each bounce goes to a unit in a 192 x 192 pixel rectangle around the unit that it hit (the Mutalisk). |
| 8 | Attack target area | Same as value 1, but each attack aims at a different position near the target, from a fixed list (the Valkyrie). |
| 9 | Go to maximum range | The missile flies to the point at the weapon's maximum range, on the line from the attacker through the target (the Lurker). |

### Remove after

The time that the missile stays before the game removes it. If the missile does not hit its target in this time, it
explodes.

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

Where the missile starts: its height above the unit. The missile can also start below the unit.

- Raw value: pixels.

### Unused

The game does not use this field. Its value is a [techdata.dat](techdata-dat.md) entry.

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

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
