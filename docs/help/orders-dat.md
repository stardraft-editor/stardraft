# Orders (orders.dat)

orders.dat has one entry for each order (189 entries). An order is what a unit does now: move, attack, gather, build,
cast a spell, and more. A unit's idle and attack orders are in [units.dat](units-dat.md#comp-ai-idle).

### Label

The order's name.

- Raw value: a stat_txt.tbl string.

### Use weapon targeting

Yes: the order uses the targets of the weapon in its targeting field.

- Raw value: 0 (no) or 1 (yes).

### Can be interrupted

Yes: a new order can stop this order.

- Raw value: 0 (no) or 1 (yes).

### Can be queued

Yes: a player can add the order to the unit's list of orders (with Shift).

- Raw value: 0 (no) or 1 (yes).

### Targeting

The weapon whose targets the order uses (for example a spell's weapon).

- Raw value: a [weapons.dat](weapons-dat.md) entry. 130 is none.

### Energy

The technology whose energy the order uses.

- Raw value: a [techdata.dat](techdata-dat.md) entry. 44 is none.

### Animation

The animation of the unit's script that the order plays (for example the attack or the cast animation).

- Raw value: a choice (the form names each one).

### Highlight

The button that shows as active while the unit does the order.

- Raw value: an icon (cmdicons.grp frame). 65535 is none.

### Obscured order

The order that the unit does when it cannot see its target.

- Raw value: an orders.dat entry.

### Unknown2

The community does not know what this field does.

- Raw value: a number.

### Unknown3

The community does not know what this field does.

- Raw value: a number.

### Unknown4

The community does not know what this field does.

- Raw value: a number.

### Unknown5

The community does not know what this field does.

- Raw value: a number.

### Unknown7

The community does not know what this field does.

- Raw value: a number.

### Unknown9

The community does not know what this field does.

- Raw value: a number.

### Unknown10

The community does not know what this field does.

- Raw value: a number.

### Unknown11

The community does not know what this field does.

- Raw value: a number.

### Unknown12

The community does not know what this field does.

- Raw value: a number.

### Unknown17

The community does not know what this field does.

- Raw value: a number.

## Sources and credits

- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book (<https://armoha.github.io/eud-book/>), the community's book about
  the game's memory and data.
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
