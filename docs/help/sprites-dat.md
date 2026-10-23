# Sprites (sprites.dat)

sprites.dat has one entry for each sprite (517 entries). A sprite is a thing that the game draws on the map: a unit,
a missile, an effect or a doodad. A [flingy.dat](flingy-dat.md#sprite) entry gives a unit its sprite.

### Image file

The first image of the sprite: [images.dat](images-dat.md).

- Raw value: an images.dat entry.

### Health bar

The length of the bar with the unit's hit points and shields under the selection circle. StarCraft: Remastered does
not use a change to this value. Only sprites 130 to 516 have this field.

- Raw value: pixels.

### Is visible

Yes: the sprite shows when the game makes it. No: the sprite is hidden until an event shows it, for example the end of
training or construction.

- Raw value: 0 (no) or 1 (yes).

The game code shows and hides many sprites itself. Thus, this value usually has almost no effect. Buildings under
construction and units that transform (for example the Siege Tank) are some of the few exceptions.

### Sel.circle image

The selection circle that shows when the unit is selected. StarCraft: Remastered does not use a change to this value.
Only sprites 130 to 516 have this field.

- Raw value: a selection circle (the form shows its images.dat entry). The images.dat entry is 561 + the raw value.
  Use a value from 0 to 9.

### Sel.circle offset

The distance of the selection circle under the sprite's center. The health bar also moves with this value.
StarCraft: Remastered does not use a change to this value. Only sprites 130 to 516 have this field.

- Raw value: pixels, signed (a negative value moves the circle and the bar up).

### Unknown2

In version 1.16.1, the game does not use this field.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book's offset table (<https://armoha.github.io/eud-book/offset_table.html>).
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
