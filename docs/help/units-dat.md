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

A second subunit. No unit of the game uses it. In version 1.16.1, the game does not read this field.

- Raw value: a units.dat entry. 228 is none.

### Infestation

The unit that this building becomes when a Queen infests it (the Terran Command Center becomes the Infested Command
Center). Only buildings (entries 106 to 201) have this field.

- Raw value: a units.dat entry.

### Construction animation

The image that shows while the unit is built, for example the Terran construction frame. Zerg and Terran buildings
use it while they are built. Zerg units use it for their birth animation. The game also uses it for the overlay after
an infestation.

- Raw value: an [images.dat](images-dat.md) entry.

### Unit direction

The direction that the unit faces when it is made. The Siege Tank's base and turret also turn to this direction
before the tank changes its mode.

- Raw value: one of 32 directions (0 is up; the numbers go clockwise). 32 is a random direction.
- Values 0 to 16 use the frames of the unit's GRP file. Values 17 to 31 use the mirrored frames of values 15 to 1.
- The game wraps values above 32 to the range 0 to 31. Only the value 32 gives a random direction.

### Elevation level

The layer that the game draws the unit in. A unit with a higher value shows on top of a unit with a lower value. Air
units have high values.

- Raw value: a number from 0 to 19.

In version 1.16.1, the game sorts the sprites with this key. A sprite with a higher key shows on top:

```
key = (sprite index) | (is subunit << 13) | ((elevation <= 4 ? sprite Y position : 0) << 14) | (elevation << 27)
```

### StarEdit placement box width

The width of the box that the map editor uses to place the unit. The box has the unit at its center. The game also
uses this box, mostly to place buildings.

- Raw value: pixels.

### StarEdit placement box height

The height of the box that the map editor uses to place the unit. The box has the unit at its center.

- Raw value: pixels.

### Addon horizontal (X) position

For an add-on (for example a Comsat Station): where the add-on is, from the building it attaches to. The position is
from the upper-left corner of that building. Only entries 106 to 201 have this field.

- Raw value: pixels.

### Addon vertical (Y) position

For an add-on: where the add-on is, from the building it attaches to. The position is from the upper-left corner of
that building. Only entries 106 to 201 have this field.

- Raw value: pixels.

### Unit size left

The unit's box for collision and for clicks: its distance from the unit's center to its left side. The box can have
sides of different lengths, but the unit's center point does not move.

- Raw value: pixels.

### Unit size up

The unit's box: its distance from the center to its top.

- Raw value: pixels.

### Unit size right

The unit's box: its distance from the center to its right side. Each unit has a size of 1 x 1 pixel without the box.
Thus, the right side of the box is 1 pixel more than this value.

- Raw value: pixels.

### Unit size down

The unit's box: its distance from the center to its bottom. Each unit has a size of 1 x 1 pixel without the box.
Thus, the bottom of the box is 1 pixel more than this value.

- Raw value: pixels.

### Portrait

The portrait that the console shows when the unit is selected.

- Raw value: a portdata.dat entry. 65535 is no portrait.

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

The armor of the unit at upgrade level 0. The game removes this amount from the damage of each hit. When the armor is
more than the damage, the unit still gets half a hit point of damage.

- Raw value: armor points.

### Armor upgrade

The upgrade that gives the unit more armor. Each level adds 1 armor.

- Raw value: an [upgrades.dat](upgrades-dat.md) entry.

### Unit size

The size of the unit: Independent, Small, Medium or Large. The weapon type (explosive, concussive) does more or less
damage to each size.

- Raw value: 0 (independent), 1 (small), 2 (medium) or 3 (large).

| Value | Size | Damage that the unit gets |
| --- | --- | --- |
| 0 | Independent | 0% from all weapons |
| 1 | Small | 50% from explosive weapons |
| 2 | Medium | 75% from explosive weapons, 50% from concussive weapons |
| 3 | Large | 25% from concussive weapons |

The unit gets 100% from all other weapon types. Do not use other values: the game reads data outside its table.

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
only the targets that come near. When the value is 0, the game uses the maximum range of the ground weapon, in tiles.
When the unit has no ground weapon, the game uses the range of the air weapon.

- Raw value: tiles.
- The form shows: pixels (raw x 32).

### Sight range

The distance that the unit sees. The game removes the fog of war in this radius around the unit.

- Raw value: tiles.
- The form shows: pixels (raw x 32).
- A value above 11 causes the game to crash, unless a mod adds more sight ranges.

### Comp AI idle

The order that the unit gets when the game makes it, or when its owner changes, if a computer player has it.

- Raw value: an [orders.dat](orders-dat.md) entry.

### Human AI idle

The order that the unit gets when the game makes it, or when its owner changes, if a human player has it.

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

| Value | Right click |
| --- | --- |
| 0 | No commands; the unit attacks automatically |
| 1 | Normal movement and normal attack |
| 2 | Normal movement, no attack; a rally point for a building |
| 3 | No movement, normal attack |
| 4 | Harvest |
| 5 | Harvest, repair, and SCV build |
| Other | Nothing, but the game shows the click indicator |

### AI internal

Flags for the computer player's scripts.

- Raw value: a set of flags.
- 0x01: the computer player does not send the unit on a Strategic Suicide Mission.
- 0x02: the computer player does not use the unit as a guard.

## Flags

### Special ability flags

The unit's properties, one flag each: building, add-on, flyer, worker, hero, detector, organic, mechanical, robotic,
burrowable, spellcaster and more. Many rules of the game use these flags, for example which spells can hit the unit.

- Raw value: a set of 32 flags.

The flags in version 1.16.1 (the name is the name in the form):

- 0x00000001 Building: weapons and other rules use the unit as a building. A Dark Swarm does not protect it.
- 0x00000002 Addon: the unit can attach to a parent building.
- 0x00000004 Flyer: weapons use the unit as an air unit. The unit does not use the ground paths.
- 0x00000008 Worker: the unit can mine resources and carry power-ups. The computer player uses it as a worker in its
  bases.
- 0x00000010 Subunit: the unit is a turret on a base unit.
- 0x00000020 Flying Building: the unit can lift off and land.
- 0x00000040 Hero: the unit can use all technologies and upgrades. Spawn Broodling, Yamato Gun, Plague, Feedback and
  Mind Control cannot hit it. Its wireframe in a group or transport has a white border.
- 0x00000080 Regenerates HP: the unit slowly gets back its hit points.
- 0x00000100 Animated Idle(?): a click on the unit's overlays also selects the unit. Without this flag, only the main
  image gets the click.
- 0x00000200 Cloakable: the unit can cloak and decloak.
- 0x00000400 Two Units in 1 Egg: a Zerg egg makes two of these units. The console shows twice the supply cost.
- 0x00000800 Single Entity: the player cannot select the unit in a group. Right-click commands ignore it.
- 0x00001000 Resource Depot: workers bring resources back to the unit. The computer player uses it as the center of a
  base.
- 0x00002000 Resource Container: the computer player uses the unit as a resource. Resource depots cannot be near it.
  Harvest orders can target it, but the game sets which units give resources.
- 0x00004000 Robotic Unit: weapons use the unit as robotic.
- 0x00008000 Detector: the unit shows cloaked units in its sight range (a building: in 7 tiles). The console shows
  "Detector".
- 0x00010000 Organic Unit: weapons use the unit as organic.
- 0x00020000 Requires Creep: a building must be on creep.
- 0x00040000 Unused(?): the game does not use this flag.
- 0x00080000 Requires Psi: a building must be in a psi field. Outside the field, the building has no power.
- 0x00100000 Burrowable: the unit can burrow and unburrow.
- 0x00200000 Spellcaster: the unit has energy. The console shows its energy bar and value.
- 0x00400000 Permanent Cloak: the unit is always cloaked, at no cost.
- 0x00800000 Pickup Item(?): the game does not use this flag.
- 0x01000000 Ignore Supply Check: the unit can be made when the player does not have sufficient supply.
- 0x02000000 Use Medium Overlays: the game adds 1 to the images.dat entry of the effect overlays (usually the medium
  image).
- 0x04000000 Use Large Overlays: the game adds 2 to the images.dat entry of the effect overlays (usually the large
  image). When both overlay flags are set, Use Medium Overlays is used.
- 0x08000000 Battle Reactions: the unit chases enemies in its target acquisition range. When it cannot attack, it
  runs away if its order has the "Fleeable" flag.
- 0x10000000 Full Auto-Attack: the unit attacks enemies in all directions automatically. Without this flag, the enemy
  must be in the weapon's attack angle, in front of the unit.
- 0x20000000 Invincible: the unit cannot be a target and gets no damage.
- 0x40000000 Mechanical Unit: weapons use the unit as mechanical.
- 0x80000000 Produces Units(?): the building makes creep in a larger area when it is built, made or moved. Only the
  Hatchery, Lair, Hive, Creep Colony, Sunken Colony and Spore Colony use this flag.

### Unknown (old movement)

Flags that the community does not fully know. The form names them as the community does.

- Raw value: a set of flags.

The game data uses these values:

| Value | Units |
| --- | --- |
| 0x00 | Buildings on the ground and all other units |
| 0x40 | Sunken Colony |
| 0x41 | Ground units |
| 0xC1 | Ground units that hover |
| 0xC5 | Air units and power-ups |

In version 1.16.1, only the full value 0xC1 has an effect. The flag 0x80 (Mine-safe) alone does nothing. A unit with 0xC1 is not a target for Spider Mines. When it
comes out of a Zerg egg, the game moves it 7 pixels up.

### Staredit group flags

How the map editor groups the unit: its race, and whether it is a unit, a building or neutral. The race flags also
set which supply the unit uses and some texts of the race. Triggers use the other flags.

- Raw value: a set of flags.
- 0x01 Zerg: the unit can go into a Nydus Canal. A building can be on creep.
- 0x02 Terran: the unit can go into a Bunker and shows its rank. A building burns down below 33% of its hit points.
- 0x04 Protoss: a Shield Battery can recharge the unit's shields.
- 0x08 Men.
- 0x10 Building.
- 0x20 Factory.
- 0x40 Independent: the game does not use this flag.
- 0x80 Neutral: the game keeps a preplaced unit with this flag in game types other than Use Map Settings.

### Staredit availability flags

Where the map editor shows the unit, for example in the unit list or only in the triggers. Only the first two flags
have an effect in the game.

- Raw value: a set of flags.
- 0x0001 Non-Neutral: when the unit does not have the Building flag, the game adds it to the vision check. The game
  uses this check to find desyncs in multiplayer games.
- 0x0002 Unit Listing&Palette: the "Create Unit" and "Create Unit With Properties" trigger actions can make the unit.
- 0x0004 Mission Briefing.
- 0x0008 Player Settings.
- 0x0010 All Races.
- 0x0020 Set Doodad State.
- 0x0040 Non-Location Triggers.
- 0x0080 Unit&Hero Settings.
- 0x0100 Location Triggers.
- 0x0200 BroodWar Only.

### Broodwar unit flag

Yes: the unit is only in Brood War. The game does not use this value.

- Raw value: 0 (no) or 1 (yes).

## Sounds

### Ready sound

The sound when the unit is complete. Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

### What sound start

The first of the sounds when a player selects the unit.

- Raw value: a sfxdata.dat entry.

### What sound end

The last of the sounds when a player selects the unit.

- Raw value: a sfxdata.dat entry.

### Piss sound start

The first of the sounds when a player clicks the unit many times. Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

### Piss sound end

The last of the sounds when a player clicks the unit many times. Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

### Yes sound start

The first of the sounds when the unit gets an order. Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

### Yes sound end

The last of the sounds when the unit gets an order. Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

## Costs and score

### Mineral cost

The minerals that the unit costs.

- Raw value: minerals.

### Vespene cost

The vespene gas that the unit costs.

- Raw value: vespene gas.

### Build time

The time to make the unit. One frame is 42 milliseconds at Fastest speed.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).
- A value of 0 causes the game to crash.

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

The points that a player gets for each of these units that the player makes. The score triggers and the score screen
at the end of the game use these points.

- Raw value: points.

### Destroy score

The points that a player gets for each of these units that the player destroys. The score triggers and the score
screen at the end of the game use these points.

- Raw value: points.

## Strings

### Rank/sublabel

The title under the unit's name in the console, for example a hero's rank. Only Terran units show this title. When
a player selects many units, the unit with the highest rank gives the portrait and the sounds.

- Raw value: a rank number (0 is none). The game reads the title from stat_txt.tbl.

### Unit map string

A string of the map that names the unit. 0 is the default name. The game uses the string only when the map has at
least that number of strings. Usually, the game sets this value itself: keep the value 0.

- Raw value: a string number.

### Unknown1

The Darkened Fantasies page names this field "DatReqs Index". The game sets the value itself. Keep the value 65535.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change, and the units of the values: the EUD Book (<https://armoha.github.io/eud-book/>),
  the community's book about the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
