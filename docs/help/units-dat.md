# Units (units.dat)

units.dat has one entry for each unit type (228 entries: 0 is the Terran Marine). Each field below tells what it does
in the game, its raw value, and the value that the Data form shows after the box. For the units of the values, see
[Values and units](values-and-units.md).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

## Graphics and parts

### Graphics

[Flingy.dat](flingy-dat.md) index for the unit's flingy property.

- Raw value: a flingy.dat entry.

### Subunit 1

Units.dat index for the unit to be used as a turret.

- Raw value: a units.dat entry. 228 is none.

### Subunit 2

Unused. No unit of the game uses it.

- Raw value: a units.dat entry. 228 is none.

### Infestation

Units.dat index for the unit to transform into after being infested.

This field only has 96 entries, pertaining to unit IDs 106-201.

- Raw value: a units.dat entry.

### Construction animation

[Images.dat](images-dat.md) index for the image used for the generic construction graphic for Zerg and Terran
structures being built, the Zerg birth animation, and the overlay created after infestation.

- Raw value: an images.dat entry.

### Unit direction

The visual direction the unit will be facing when created, and the direction the siege tank base and turret will face
before switching mode.

A valid range of 0 to 31 for a specific direction, and 32 for a random direction. 0-16 are the angles matching the GRP
frameset angles (clockwise turntable), and 17-31 are the mirrored angles of angles 1-15. Values above 32 will wrap
around, but only 32 will give a random direction.

- Raw value: one of 32 directions (0 is up; the numbers go clockwise). 32 is a random direction.

### Elevation level

Z-sorting for the unit's sprite, determining which unit appears above or below others. Other criteria come into play
for sprites on the same elevation level, which can be described as:

```
(Sprite Index) | (isSubunit << 13) | ((Elevation <= 4 ? (Sprite PosY) : 0) << 14) | (Elevation << 27)
```

- Raw value: a number from 0 to 19.

### StarEdit placement box width

Width of a rectangle centered on the unit, used mostly for building placement.

- Raw value: pixels.

### StarEdit placement box height

Height of a rectangle centered on the unit, used mostly for building placement.

- Raw value: pixels.

### Addon horizontal (X) position

X position of the unit as an add-on, in pixels, relative to the upper-left corner of the building it connects to.

This field only has 96 entries, pertaining to unit IDs 106-201.

- Raw value: pixels.

### Addon vertical (Y) position

Y position of the unit as an add-on, in pixels, relative to the upper-left corner of the building it connects to.

This field only has 96 entries, pertaining to unit IDs 106-201.

- Raw value: pixels.

### Unit size left

Distance in pixels of the left rectangle edge around the unit, shaping its collision box (the four Unit size fields
are the Unit Dimensions). While the collision box may be asymmetrical, it does not change the position of the unit's
center point.

- Raw value: pixels.

### Unit size up

Distance in pixels of the top rectangle edge around the unit, shaping its collision box.

- Raw value: pixels.

### Unit size right

Distance in pixels of the right rectangle edge around the unit, shaping its collision box. Units inherently have a
size of 1x1, so consider 1 pixel to be added to the right and bottom lengths.

- Raw value: pixels.

### Unit size down

Distance in pixels of the bottom rectangle edge around the unit, shaping its collision box. Units inherently have a
size of 1x1, so consider 1 pixel to be added to the right and bottom lengths.

- Raw value: pixels.

### Portrait

[Portdata.dat](portdata-dat.md) index for the unit's portrait. A value of 65535 is defined as having no portrait.

- Raw value: a portdata.dat entry. 65535 is no portrait.

## Health and armor

### Shield enable

If set, the unit will use shield points (the source calls it Has Shields).

- Raw value: 0 (no) or 1 (yes).

### Shield amount

How much shields the unit has.

- Raw value: shield points.

### Hit points

How much health the unit has. 256 is equal to 1 full point.

- Raw value: 1/256 hit points. A Marine has 40 hit points: the raw value is 10240.
- The form shows: hit points (raw / 256).

### Armor

Base armor level. Damage taken by the unit is reduced by armor points. In the event that armor exceeds the damage
received, units cannot take less than half a point of damage.

- Raw value: armor points.

### Armor upgrade

[Upgrades.dat](upgrades-dat.md) index that will increase the unit's armor.

- Raw value: an upgrades.dat entry.

### Unit size

Modifies the amount of damage taken by certain weapon types.

- Raw value: 0 (independent), 1 (small), 2 (medium) or 3 (large).

| Value | Size | Damage taken |
| --- | --- | --- |
| 0 | None (the form calls it Independent) | Takes 0% damage from all sources. |
| 1 | Small | Takes 50% damage from Explosive. |
| 2 | Medium | Takes 75% damage from Explosive and 50% from Concussive. |
| 3 | Large | Takes 25% damage from Concussive. |
| Other | | Other values are undefined and will result in out-of-bounds array access. |

The unit takes 100% damage from all other weapon types.

## Weapons and AI

### Ground weapon

[Weapons.dat](weapons-dat.md) index for the ground attack.

- Raw value: a weapons.dat entry. 130 is none.

### Max ground hits

Used only by StarEdit. The console shows it as the number of hits of the ground weapon (for example "x2"); the hits
that an attack really does come from the unit's animation script.

- Raw value: hits.

### Air weapon

[Weapons.dat](weapons-dat.md) index for the air attack.

- Raw value: a weapons.dat entry. 130 is none.

### Max air hits

Used only by StarEdit. The console shows it as the number of hits of the air weapon.

- Raw value: hits.

### Target acquisition range

Radius in tiles (32 pixels) at which an enemy unit will be automatically acquired. If this value is 0, the ground
weapon's Max Range in tiles will be used, or that of the air weapon if no ground weapon is specified.

- Raw value: tiles.
- The form shows: pixels (raw x 32).

### Sight range

Radius in tiles (32 pixels) that the fog will be revealed around the unit.

Values above 11 will crash the game unless more sight ranges have been modded in.

- Raw value: tiles.
- The form shows: pixels (raw x 32).

### Comp AI idle

[Orders.dat](orders-dat.md) entry: the order to be performed when the unit is initially created or changes ownership,
if owned by a Computer player (the source calls it Computer Idle Order).

- Raw value: an orders.dat entry.

### Human AI idle

[Orders.dat](orders-dat.md) entry: the order to be performed when the unit is initially created or changes ownership,
if the unit is (generally) owned by a Human player (the source calls it Human Idle Order).

- Raw value: an orders.dat entry.

### Return to idle

[Orders.dat](orders-dat.md) entry: the order to be performed when the unit is made to become idle.

- Raw value: an orders.dat entry.

### Attack unit

[Orders.dat](orders-dat.md) entry: the order to be performed when using Attack on a unit.

- Raw value: an orders.dat entry.

### Attack move

[Orders.dat](orders-dat.md) entry: the order to be performed when using Attack on the ground.

- Raw value: an orders.dat entry.

### Right-click action

- Raw value: a choice (the form names each one).

| Value | Right-click action |
| --- | --- |
| 0 | No Commands/Auto Attack |
| 1 | Normal Movement/Normal Attack |
| 2 | Normal Movement/No Attack/Rally |
| 3 | No Movement/Normal Attack |
| 4 | Harvest |
| 5 | Harvest/Repair/SCV Build |
| Other | Nothing (with indicator) |

### AI internal

The AI Action Flags.

- Raw value: a set of flags.
- 0x01: Unit will not be included in Strategic Suicide Mission.
- 0x02: Unit will not be assigned as a guard by Computer players.

## Flags

### Special ability flags

The general attributes of the unit (the source calls them Base Properties), some of which have too many effects for an
exhaustive list here.

- Raw value: a set of 32 flags.

The flags (where the form uses another name, it is in parentheses):

- 0x00000001 Building: Considered to be a building for weapon targeting and other various behaviors. Also does not
  take cover from Dark Swarm.
- 0x00000002 Addon: Allows the unit to connect to a parent building.
- 0x00000004 Flyer: Considered an "air" unit for weapon targeting, and does not use terrain pathfinding.
- 0x00000008 Worker: Considered to be a worker for AI town management, and can mine resources and carry powerups.
- 0x00000010 Subunit: Considered to be a turret attached to a base unit.
- 0x00000020 Flying Building: Allows the unit to use the lift-off and landing abilities.
- 0x00000040 Hero: The unit has unrestricted tech and upgrade usage, and prevents the unit from being targeted by
  Spawn Broodling, Yamato Gun, Plague, Feedback, and Mind Control. Also gives the group-selection and transport
  wireframes a white border.
- 0x00000080 Regenerate (the form calls it Regenerates HP): Health will be slowly replenished if below max.
- 0x00000100 Has Overlay (the form calls it Animated Idle(?)): If set, clickable overlays are included in
  click-through detection. Otherwise, only the main graphic is checked.
- 0x00000200 Cloakable: Allows the unit to use the cloaking and decloaking abilities.
- 0x00000400 Paired Spawn (the form calls it Two Units in 1 Egg): Sets the unit to be duplicated when birthing from
  Zerg eggs, and the displayed supply cost is doubled.
- 0x00000800 Single Entity: Cannot be group-selected, and is ignored by contextual right-click commands.
- 0x00001000 Resource Depot: Considered to be a town center for returning resources to, and AI town management.
- 0x00002000 Resource Container: Considered to be a resource node for AI town management, and prevents Resource Depots
  from being located near the unit. Can also be targeted by Harvest orders, although which units can actually be
  harvested from is hardcoded.
- 0x00004000 Robotic (the form calls it Robotic Unit): Considered a "robotic" unit for weapon targeting.
- 0x00008000 Detector: Reveals cloaked units within its sight range, or within 7 tiles if a building. Will display
  the "Detector" status.
- 0x00010000 Organic (the form calls it Organic Unit): Considered an "organic" unit for weapon targeting.
- 0x00020000 Requires Creep: If a building, cannot be placed off the creep.
- 0x00040000 Unused (the form calls it Unused(?)).
- 0x00080000 Requires Psi: If a building, cannot be placed outside of the psi field, and will become unpowered if no
  longer in it.
- 0x00100000 Burrowable: Allows the unit to use the burrow and unburrow abilities.
- 0x00200000 Spellcaster: Considered to be a spellcaster, and displays the energy bar and counter.
- 0x00400000 Permanently Cloaked (the form calls it Permanent Cloak): Unit is cloaked for free.
- 0x00800000 Unused (the form calls it Pickup Item(?)).
- 0x01000000 Ignore Supply Check: Training/construction will not be blocked if there isn't enough available supply.
- 0x02000000 Use Medium Overlays: +1 is added to the specified effect overlay Image ID, which is typically the
  "medium" graphic.
- 0x04000000 Use Large Overlays: +2 is added to the specified effect overlay Image ID, which is typically the "large"
  graphic. Use Medium Overlays takes precedence if both flags are set.
- 0x08000000 Battle Reactions: Will react to enemies by chasing after them if they enter within the Target
  Acquisition Range, or by fleeing from them if it cannot retaliate and the order being performed has the Fleeable
  flag set.
- 0x10000000 Full Auto-Attack: If not set, the unit will only auto-attack an enemy if it is in front, within its
  weapon's Attack Angle.
- 0x20000000 Invincible: Cannot be targeted or receive damage.
- 0x40000000 Mechanical (the form calls it Mechanical Unit): Considered a "mechanical" unit for weapon targeting.
- 0x80000000 Wide Starting Creep (the form calls it Produces Units(?)): If set, creep appears in a wider area around
  the building when built, created, or moved. This flag only works for Hatchery, Lair, Hive, Creep Colony, Sunken
  Colony, and Spore Colony due to hardcode.

### Unknown (old movement)

The Movement Flags. The form names the flags as the community does.

Relatively unused. It is set to 64 (0x40) for sunken colony, 65 (0x41) for ground units, 193 (0xC1) for ground
hovering units, 197 (0xC5) for air units and powerups, and 0 for non-flying buildings and anything else.

However the only significance it has in the game is whether it is equal to 193 (0xC1) or not; if it is, the unit
cannot be targeted by spider mines and the unit will be moved by 7 pixels upwards when birthing from a Zerg egg. The
flag 0x80 (Mine-safe) alone does nothing.

- Raw value: a set of flags.

### Staredit group flags

The Group Flags. The race flags determine which supply type the unit will use and some race-specific text, among some
other things. The other flags generally are for the purpose of triggers.

- Raw value: a set of flags.
- 0x01 Zerg: Allows units to enter Nydus Canals, and buildings to be placed on creep.
- 0x02 Terran: Allows units to enter Bunkers, show unit rank, and buildings will burn down when below 33% health.
- 0x04 Protoss: Allows units to have their shields recharged by Shield Batteries.
- 0x08 Men.
- 0x10 Building.
- 0x20 Factory.
- 0x40 Independent: Unused.
- 0x80 Neutral: Preplaced units will not be removed in non-UMS game modes.

### Staredit availability flags

The usage of these flags is mostly related to StarEdit, with only the first two having in-game behavior.

- Raw value: a set of flags.
- 0x0001 Non-Neutral: If the unit doesn't have the Building flag (in Special ability flags) set, it will be included
  in the vision hash (multiplayer game desync detection).
- 0x0002 Unit Listing&Palette: The unit can be created by the "Create Unit" and "Create Unit With Properties" trigger
  actions.
- 0x0004 Mission Briefing.
- 0x0008 Player Settings.
- 0x0010 All Races.
- 0x0020 Set Doodad State.
- 0x0040 Non-Location Triggers.
- 0x0080 Unit&Hero Settings.
- 0x0100 Location Triggers.
- 0x0200 BroodWar Only.

### Broodwar unit flag

Unused in the game.

- Raw value: 0 (no) or 1 (yes).

## Sounds

### Ready sound

[Sfxdata.dat](sfxdata-dat.md) index for the sound played when the unit finishes training. Only entries 0 to 105 have
this field.

- Raw value: a sfxdata.dat entry.

### What sound start

First [sfxdata.dat](sfxdata-dat.md) index of the range for the sounds played when the unit is selected.

- Raw value: a sfxdata.dat entry.

### What sound end

Last [sfxdata.dat](sfxdata-dat.md) index of the range for the sounds played when the unit is selected.

- Raw value: a sfxdata.dat entry.

### Piss sound start

First [sfxdata.dat](sfxdata-dat.md) index of the range for the sounds played when the unit is being excessively
selected (the source calls it Annoyed Start). Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

### Piss sound end

Last [sfxdata.dat](sfxdata-dat.md) index of the range for the sounds played when the unit is being excessively
selected (the source calls it Annoyed End). Only entries 0 to 105 have this field.

- Raw value: a sfxdata.dat entry.

### Yes sound start

First [sfxdata.dat](sfxdata-dat.md) index of the range for the sounds played when the unit is ordered. Only entries 0
to 105 have this field.

- Raw value: a sfxdata.dat entry.

### Yes sound end

Last [sfxdata.dat](sfxdata-dat.md) index of the range for the sounds played when the unit is ordered. Only entries 0
to 105 have this field.

- Raw value: a sfxdata.dat entry.

## Costs and score

### Mineral cost

Mineral cost to be trained/constructed.

- Raw value: minerals.

### Vespene cost

Vespene cost to be trained/constructed.

- Raw value: vespene gas.

### Build time

Training/construction time in logical frames. 1 is equal to 42 milliseconds at fastest game speed.

A value of 0 will crash the game.

- Raw value: frames.
- The form shows: seconds at Fastest (raw / 24).

### Supply provided

How much supply the unit adds to the total supply pool.

A value of 1 is equal to half a supply point in-game (e.g. Zerglings and Scourge), whereas 2 is one full supply point.
Meaning even numbers are integral supply points, and odd numbers are so-and-a-half points.

- Raw value: half supply. A Supply Depot gives 8 supply: the raw value is 16.
- The form shows: supply (raw / 2).

### Supply required

How much the unit uses from the available supply pool.

A value of 1 is equal to half a supply point in-game (e.g. Zerglings and Scourge), whereas 2 is one full supply point.
Meaning even numbers are integral supply points, and odd numbers are so-and-a-half points.

- Raw value: half supply. A Marine takes 1 supply: the raw value is 2. A Zergling takes half a supply: the raw value
  is 1.
- The form shows: supply (raw / 2).

### Space required

How much room the unit takes up when entering a transport (a Marine takes 1, a Siege Tank takes 4).

- Raw value: transport slots.

### Space provided

How much room the unit has to carry other units (a Dropship has 8).

- Raw value: transport slots.

### Build score

The score points given for training the unit, for the purpose of the Score triggers and the post-game scoreboard.

- Raw value: points.

### Destroy score

The score points given for killing the unit, for the purpose of the Score triggers and the post-game scoreboard.

- Raw value: points.

## Strings

### Rank/sublabel

The unit with the highest rank will have its portrait and unit responses used when multiple units are selected.

Also the string index within stat_txt.tbl to be displayed under the unit name if the unit is Terran.

- Raw value: a rank number (0 is none). The game reads the title from stat_txt.tbl.

### Unit map string

Intended to be set internally by the game, and should be left as 0. If not, the unit name will use the specified
string index from the map, provided it contains at least that many strings.

- Raw value: a string number.

### Unknown1

The DatReqs Index. Intended to be set internally by the game, and should be left as 65535.

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
