# Weapons (weapons.dat)

weapons.dat has one entry for each weapon (130 entries). A unit uses a weapon through its ground weapon and air
weapon fields in [units.dat](units-dat.md#ground-weapon); spells and some orders use weapons too. For the units of the
values, see [Values and units](values-and-units.md).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

## Damage

### Damage amount

Base damage of the weapon.

- Raw value: damage points.
- The damage of one hit at an upgrade level: damage amount + (damage bonus x upgrade level).

### Damage bonus

Bonus amount of damage per upgrade level.

- Raw value: damage points.

### Damage upgrade

[Upgrades.dat](upgrades-dat.md) index that will increase the weapon's damage.

- Raw value: an [upgrades.dat](upgrades-dat.md) entry.

### Damage factor

If set to 2, the weapon will attack twice per use. Any other value only multiplies the displayed damage stat.

- Raw value: a number.

### Weapon type

The damage type of the weapon.

- Raw value: a choice: independent, explosive, concussive, normal or ignore armor.

- 0 None (the form calls it Independent): deals 0% damage to all sizes. All other damage types also deal 0% to None
  unit size.
- 1 Explosive: deals 50% to Small, 75% to Medium, and 100% to Large.
- 2 Concussive: deals 100% to Small, 50% to Medium, and 25% to Large.
- 3 Normal: deals 100% to Small, Medium, and Large.
- 4 Ignore Armor: same as Normal but damage is not reduced by armor (including shields upgrades).
- Other values are undefined and will result in out-of-bounds array access.

### Explosion type

Defines how the weapon affects the target on hit.

- Raw value: a choice (the form names each one).

| Value | Name | What it does |
|---|---|---|
| 0 | None | No effect on target. Used for the weapon uses from SCVs and Probes while mining. |
| 1 | Normal Hit | Deals full damage to the target, or none if the bullet misses. |
| 2 | Splash (Radial) | Deals splash damage to all vulnerable units around the point of impact, as defined by the weapon's splash radii. |
| 3 | Splash (Enemy) | Same as above, but doesn't damage the player's own units. |
| 4 | Lockdown | Freezes the unit for 1044 +/- 4 ticks (~43.848s on fastest). |
| 5 | Nuclear Missile | Same as Splash (Radial), but deals damage equal to a third of the unit's Hit Points (plus Shield Points if Has Shields is set), down to a minimum of 500. |
| 6 | Parasite | Marks the unit with parasite. |
| 7 | Broodlings | Instantly kills the target and creates two Broodlings at its position, with a Removal Timer of 1800. |
| 8 | EMP Shockwave | Sets shields and energy to 0 for all vulnerable units around the point of impact, within a rectangular area defined by the weapon's Inner Splash radius. |
| 9 | Irradiate | Irradiates the target for a duration equal to 8 times the cooldown of irradiate +/- 4 ticks, and deals damage equal to irradiate's weapon damage divided by the cooldown of irradiate every 8th tick. |
| 10 | Ensnare | Slows down all vulnerable non-burrowed units within a 128x128 pixels rectangle around the point of impact, for a duration of 71 +/- 4 ticks (~23.856s on fastest). |
| 11 | Plague | Plagues all vulnerable non-burrowed units within a 128x128 pixels rectangle around the point of impact, for a duration of 71 +/- 4 ticks (~23.856s on fastest), and deals damage equal to plague's weapon damage times 256 divided by 71, every 8th tick. |
| 12 | Stasis Field | Freezes all vulnerable non-building non-burrowed units within a 96x96 pixels rectangle around the point of impact for 1044 +/- 4 ticks (~43.848s on fastest), and makes them invulnerable for the duration. |
| 13 | Dark Swarm | Creates a Dark Swarm at the point of impact, with a Removal Timer of 900. |
| 14 | Consume | Instantly kills the target and gives +50 energy to the attacker. |
| 15 | Yamato Gun | Same as Normal Hit, but deals full damage even if the bullet misses the target. |
| 16 | Restoration | Removes parasite, blindness, ensnare, plague, irradiate, lockdown, maelstrom, and acid spores from the target. |
| 17 | Disruption Web | Creates a Disruption Web at the point of impact, with a Removal Timer of 360. |
| 18 | Corrosive Acid | Deals damage to the target, and adds 1 point of acid spores to all vulnerable units within a 128x128 pixels rectangle around the point of impact, for a duration of 150 +/- 4 ticks (~50.4s on fastest). |
| 19 | Mind Control | Intended to be used directly by the order, and results in an error if used with a bullet. |
| 20 | Feedback | Same as above. |
| 21 | Optical Flare | Marks the unit with blindness. |
| 22 | Maelstrom | Freezes all vulnerable organic non-building non-burrowed units within a 96x96 pixels rectangle around the point of impact for 22 +/- 4 ticks (~7.392s on fastest). |
| 23 | Unknown (Crash) (the form calls it Unused) | Unused. |
| 24 | Splash (Air) | Deals full damage to the targeted unit if it's the only one found within the Outer Splash radius. Otherwise, deals full damage to one random vulnerable unit within the Outer Splash radius, and all other air units that are not Interceptors and are not the player's own are dealt reduced damage according to the Middle Splash and Outer Splash radii. |

- The Middle Splash radius is the [Medium splash range](#medium-splash-range) on the form. The Removal Timer of a
  bullet is its [Remove after](#remove-after) field.

### Weapon cooldown

How many logical frames before the weapon can be used again.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

## Range and splash

### Minimum range

Minimum range, in pixels, at which the weapon may be used (the Siege Tank in siege mode has one).

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Maximum range

Maximum range, in pixels, at which the weapon may be used.

- Raw value: pixels. A Marine's weapon has 128 (4 tiles).
- The form shows: tiles (raw / 32).

### Inner splash range

Radii in pixels where weapons of "splash" type will deal decreasing amount of damage to units found within. Inner
Splash deals 100% damage, Middle Splash (the form calls it Medium splash range) deals 50% damage, and Outer Splash deals
25% damage.

- Raw value: pixels.
- The explosion type EMP Shockwave also uses this field. It sets the size of the rectangle that loses shields and
  energy.
- The form shows: tiles (raw / 32).

### Medium splash range

Radii in pixels where weapons of "splash" type will deal decreasing amount of damage to units found within. Inner
Splash deals 100% damage, Middle Splash (the form calls it Medium splash range) deals 50% damage, and Outer Splash deals
25% damage.

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Outer splash range

Radii in pixels where weapons of "splash" type will deal decreasing amount of damage to units found within. Inner
Splash deals 100% damage, Middle Splash (the form calls it Medium splash range) deals 50% damage, and Outer Splash deals
25% damage.

- Raw value: pixels.
- The form shows: tiles (raw / 32).

### Target flags

Defines what the weapon can be used on.

- Raw value: a set of flags.

| Flag | Name | Meaning |
|---|---|---|
| 0x0001 | Air | Can target air units. |
| 0x0002 | Ground | Can target ground units. |
| 0x0004 | Mechanical | Must target a mechanical unit. |
| 0x0008 | Organic | Must target an organic unit. |
| 0x0010 | Non-Building | Cannot target buildings. |
| 0x0020 | Non-Robotic | Cannot target robotic units. |
| 0x0040 | Terrain | Can target terrain. |
| 0x0080 | Organic or Mechanical (the form calls it Org. or Mech.) | Must target a mechanical OR organic unit. Note that ticking both Mechanical and Organic flags requires the target to be both types, whereas this flag only requires the target to be either one. |
| 0x0100 | Own | Must target a player's own units. |

### Attack angle

Widest angle from which a weapon may be used, where 0 is directly in front, and 128+ is all-around.

- Raw value: 1/256 of a full turn.
- The form shows: degrees (raw x 360 / 256).

## The missile

### Graphics

[Flingy.dat](flingy-dat.md) index for the bullet's flingy property.

- Raw value: a flingy.dat entry.

### Weapon behavior

Defines how the weapon behaves when used.

- Raw value: a choice (the form names each one).

| Value | Name | What it does |
|---|---|---|
| 0 | Fly and Don't Follow Target | Bullet aims at the point where the target was when fired at. (e.g. Vulture) |
| 1 | Fly and Follow Target | Bullet homes in on the target until it hits it. (e.g. Dragoon) |
| 2 | Appear on Target Unit | Bullet instantly hits on top of the target. (e.g. Gauss Rifle) |
| 3 | Persist on Target Site | Bullet appears on top of the target and deals damage every 8th tick until Removal Timer (the form calls it [Remove after](#remove-after)) runs out. Plays the SpecialState2 Iscript animation. (e.g. Psi Storm) |
| 4 | Appear on Target Site | Identical to Appear on Target Unit. |
| 5 | Appear on Attacker | Bullet instantly hits on top of the attacker. (e.g. Infested Terran) |
| 6 | Attack and Self-Destruct | Same as above, but the attacker is also removed without a death animation. (e.g. Scourge) |
| 7 | Bounce | Bullet will bounce up to 2 times, each time on a vulnerable unit within a 192x192 pixels rectangle around the currently hit unit. (e.g. Mutalisk) |
| 8 | Attack Target 3x3 Area (the form calls it Attack target area) | Same as Fly and Follow Target, but each use of the weapon cycles through a list of predefined offset points, which are set relative to the target. (e.g. Valkyrie) |
| 9 | Go to Max. Range (the form calls it Go to maximum range) | Bullet aims at the point that is at the weapon's Max Range, aligned with the attacker and target. (e.g. Lurker) |

### Remove after

Lifetime of the bullet in logical frames, until which it will explode if it hasn't hit its target (the source calls
it Removal Timer).

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Launch spin

Angle at which the bullet will be created, where 128 is equal to 180 degrees (the missiles of the Wraith and the
Valkyrie turn out to the sides).

- Raw value: 1/256 of a full turn.
- The form shows: degrees (raw x 360 / 256).

### Forward offset

Distance ahead of the unit, in pixels, at which the bullet will be created.

- Raw value: pixels.

### Upward offset

Distance above/below the unit, in pixels, at which the bullet will be created (the source calls it Vertical Offset).

- Raw value: pixels.

### Unused

Unused. References a [techdata.dat](techdata-dat.md) index (the source calls it Tech).

- Raw value: a number.

## Names and messages

### Label

Stat_txt.tbl string index for the weapon name in the console unit status area.

- Raw value: a stat_txt.tbl string.

### Target error message

Stat_txt.tbl string index for the error message shown when used on an invalid target.

- Raw value: a stat_txt.tbl string.

### Icon

Frame index within cmdicons.grp for the weapon's icon in the console unit status area.

- Raw value: an icon (cmdicons.grp frame).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The field descriptions are from the Darkened Fantasies page. The rest of this page is from the other sources and from
  the game's data.
