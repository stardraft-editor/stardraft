# Units (units.dat)

units.dat has one entry for each unit type (228 entries: 0 is the Terran Marine). Each field below tells what it does
in the game, its raw value, and the value that the Data form shows after the box. For the units of the values, see
[Values and units](values-and-units.md).

## Graphics and parts

### Graphics

The movement entry of the unit: [flingy.dat](flingy-dat.md). It sets the unit's sprite, speed and turn speed.

- Raw value: a flingy.dat entry.

### Subunit 1

A second unit that the game attaches to this unit, for example the turret of a Siege Tank or a Goliath. The subunit
turns and attacks with its own weapons.

- Raw value: a units.dat entry. 228 is none.

### Subunit 2

A second subunit. No unit of the game uses it.

- Raw value: a units.dat entry. 228 is none.

### Infestation

The unit that this building becomes when a Queen infests it (the Terran Command Center becomes the Infested Command
Center). Only buildings (entries 106 to 201) have this field.

- Raw value: a units.dat entry.

### Construction animation

The image that shows while the unit is built, for example the Terran construction frame.

- Raw value: an [images.dat](images-dat.md) entry.

### Unit direction

The direction that the unit faces when it is made.

- Raw value: one of 32 directions (0 is up; the numbers go clockwise). 32 is a random direction.

### Elevation level

The layer that the game draws the unit in. A unit with a higher value shows on top of a unit with a lower value. Air
units have high values.

- Raw value: a number from 0 to 19.

### StarEdit placement box width

The width of the box that the map editor uses to place the unit.

- Raw value: pixels.

### StarEdit placement box height

The height of the box that the map editor uses to place the unit.

- Raw value: pixels.

### Addon horizontal (X) position

For an add-on (for example a Comsat Station): where the add-on is, from the building it attaches to.

- Raw value: pixels.

### Addon vertical (Y) position

For an add-on: where the add-on is, from the building it attaches to.

- Raw value: pixels.

### Unit size left

The unit's box for collision and for clicks: its distance from the unit's center to its left side.

- Raw value: pixels.

### Unit size up

The unit's box: its distance from the center to its top.

- Raw value: pixels.

### Unit size right

The unit's box: its distance from the center to its right side.

- Raw value: pixels.

### Unit size down

The unit's box: its distance from the center to its bottom.

- Raw value: pixels.

### Portrait

The portrait that the console shows when the unit is selected.

- Raw value: a portdata.dat entry.

## Health and armor

### Shield enable

Yes: the unit has shields (Protoss units).

- Raw value: 0 (no) or 1 (yes).

### Shield amount

The most shield points that the unit can have.

- Raw value: shield points.

### Hit points

The most hit points that the unit can have.

- Raw value: 1/256 hit points. A Marine has 40 hit points: the raw value is 10240.
- The form shows: hit points (raw / 256).

### Armor

The armor of the unit at upgrade level 0. The game removes this amount from the damage of each hit.

- Raw value: armor points.

### Armor upgrade

The upgrade that gives the unit more armor. Each level adds 1 armor.

- Raw value: an [upgrades.dat](upgrades-dat.md) entry.

### Unit size

The size of the unit: Independent, Small, Medium or Large. The weapon type (explosive, concussive) does more or less
damage to each size.

- Raw value: 0 (independent), 1 (small), 2 (medium) or 3 (large).

## Weapons and AI

### Ground weapon

The weapon that the unit uses against ground units.

- Raw value: a [weapons.dat](weapons-dat.md) entry. 130 is none.

### Max ground hits

The number of hits that the console shows for the ground weapon (for example "x2"). The hits that an attack really
does come from the unit's animation script.

- Raw value: hits.

### Air weapon

The weapon that the unit uses against air units.

- Raw value: a [weapons.dat](weapons-dat.md) entry. 130 is none.

### Max air hits

The number of hits that the console shows for the air weapon.

- Raw value: hits.

### Target acquisition range

The distance at which the unit finds targets by itself. When it is smaller than the weapon's range, the unit attacks
only the targets that come near.

- Raw value: tiles.
- The form shows: pixels (raw x 32).

### Sight range

The distance that the unit sees.

- Raw value: tiles.
- The form shows: pixels (raw x 32).

### Comp AI idle

The order that the unit does when it has no orders and a computer player has it.

- Raw value: an [orders.dat](orders-dat.md) entry.

### Human AI idle

The order that the unit does when it has no orders and a human player has it.

- Raw value: an [orders.dat](orders-dat.md) entry.

### Return to idle

The order that the unit does after it completes an order.

- Raw value: an [orders.dat](orders-dat.md) entry.

### Attack unit

The order that the unit does when it attacks a unit.

- Raw value: an [orders.dat](orders-dat.md) entry.

### Attack move

The order that the unit does for an attack move (Attack on the ground).

- Raw value: an [orders.dat](orders-dat.md) entry.

### Right-click action

What a right click does with the unit selected: move, attack, or nothing.

- Raw value: a choice (the form names each one).

### AI internal

Flags for the computer player's scripts.

- Raw value: a set of flags.

## Flags

### Special ability flags

The unit's properties, one flag each: building, add-on, flyer, worker, hero, detector, organic, mechanical, robotic,
burrowable, spellcaster and more. Many rules of the game use these flags, for example which spells can hit the unit.

- Raw value: a set of 32 flags.

### Unknown (old movement)

Flags that the community does not fully know. The form names them as the community does.

- Raw value: a set of flags.

### Staredit group flags

How the map editor groups the unit: its race, and whether it is a unit, a building or neutral.

- Raw value: a set of flags.

### Staredit availability flags

Where the map editor shows the unit, for example in the unit list or only in the triggers.

- Raw value: a set of flags.

### Broodwar unit flag

Yes: the unit is only in Brood War.

- Raw value: 0 (no) or 1 (yes).

## Sounds

### Ready sound

The sound when the unit is complete.

- Raw value: a sfxdata.dat entry.

### What sound start

The first of the sounds when a player selects the unit.

- Raw value: a sfxdata.dat entry.

### What sound end

The last of the sounds when a player selects the unit.

- Raw value: a sfxdata.dat entry.

### Piss sound start

The first of the sounds when a player clicks the unit many times.

- Raw value: a sfxdata.dat entry.

### Piss sound end

The last of the sounds when a player clicks the unit many times.

- Raw value: a sfxdata.dat entry.

### Yes sound start

The first of the sounds when the unit gets an order.

- Raw value: a sfxdata.dat entry.

### Yes sound end

The last of the sounds when the unit gets an order.

- Raw value: a sfxdata.dat entry.

## Costs and score

### Mineral cost

The minerals that the unit costs.

- Raw value: minerals.

### Vespene cost

The vespene gas that the unit costs.

- Raw value: vespene gas.

### Build time

The time to make the unit.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Supply provided

The supply that the unit gives (a Supply Depot, an Overlord, a Pylon).

- Raw value: half supply. A Supply Depot gives 8 supply: the raw value is 16.
- The form shows: supply (raw / 2).

### Supply required

The supply that the unit takes.

- Raw value: half supply. A Marine takes 1 supply: the raw value is 2. A Zergling takes half a supply: the raw value
  is 1.
- The form shows: supply (raw / 2).

### Space required

The space that the unit takes in a transport (a Marine takes 1, a Siege Tank takes 4).

- Raw value: transport slots.

### Space provided

The space that a transport has (a Dropship has 8).

- Raw value: transport slots.

### Build score

The points that a player gets for each of these units that the player makes.

- Raw value: points.

### Destroy score

The points that a player gets for each of these units that the player destroys.

- Raw value: points.

## Strings

### Rank/sublabel

The title under the unit's name in the console, for example a hero's rank.

- Raw value: a rank number (0 is none). The game reads the title from stat_txt.tbl.

### Unit map string

A string of the map that names the unit. 0 is the default name.

- Raw value: a string number.

### Unknown1

The community does not know what this field does.

- Raw value: a number.

## Sources and credits

- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
