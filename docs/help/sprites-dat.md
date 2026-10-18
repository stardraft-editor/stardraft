# Sprites (sprites.dat)

sprites.dat has one entry for each sprite (517 entries). A sprite is a thing that the game draws on the map: a unit,
a missile, an effect or a doodad. A [flingy.dat](flingy-dat.md#sprite) entry gives a unit its sprite.

### Image file

The first image of the sprite: [images.dat](images-dat.md).

- Raw value: an images.dat entry.

### Health bar

The length of the bar with the unit's hit points and shields under the selection circle. StarCraft: Remastered does
not use a change to this value.

- Raw value: pixels.

### Is visible

Yes: the sprite shows when the game makes it.

- Raw value: 0 (no) or 1 (yes).

### Sel.circle image

The selection circle that shows when the unit is selected. StarCraft: Remastered does not use a change to this value.

- Raw value: a selection circle (the form shows its images.dat entry).

### Sel.circle offset

The distance of the selection circle under the sprite's center. StarCraft: Remastered does not use a change to this
value.

- Raw value: pixels.

### Unknown2

The community does not know what this field does.

- Raw value: a number.

## Sources and credits

- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book's offset table (<https://armoha.github.io/eud-book/offset_table.html>).
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
