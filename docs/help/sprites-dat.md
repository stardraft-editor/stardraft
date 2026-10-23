# Sprites (sprites.dat)

sprites.dat has one entry for each sprite (517 entries). A sprite is a thing that the game draws on the map: a unit,
a missile, an effect or a doodad. A [flingy.dat](flingy-dat.md#sprite) entry gives a unit its sprite.

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

### Image file

[Images.dat](images-dat.md) index for the sprite's main graphic.

- Raw value: an images.dat entry.

### Health bar

Length of the health bar in pixels. StarCraft: Remastered does not use a change to this value. Only sprites 130 to 516
have this field.

- Raw value: pixels.

### Is visible

If not set, the sprite will start off as invisible until something unhides it, such as training/construction
completion. Due to frequent hardcoding with sprite showing and hiding, this setting tends to have little to no effect,
with very few exceptions like buildings being constructed and unit transformations (e.g. Siege Tank).

- Raw value: 0 (no) or 1 (yes).

### Sel.circle image

[Images.dat](images-dat.md) index for the selection circle image. This value is an offset from index 561, and the
recommended range is between 0 and 9. StarCraft: Remastered does not use a change to this value. Only sprites 130 to
516 have this field.

- Raw value: a selection circle (the form shows its images.dat entry). The images.dat entry is 561 + the raw value.
  Use a value from 0 to 9.

### Sel.circle offset

Vertical position of the selection circle and health bar relative to the sprite's center (the source calls it Y
Position). StarCraft: Remastered does not use a change to this value. Only sprites 130 to 516 have this field.

- Raw value: pixels, signed (a negative value moves the circle and the bar up).

### Unknown2

Unused.

- Raw value: a number.

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book's offset table (<https://armoha.github.io/eud-book/offset_table.html>).
- The field descriptions are from the StarCraft DAT page of Darkened Fantasies. The rest of this page is from the other
  sources and from the game's data.
