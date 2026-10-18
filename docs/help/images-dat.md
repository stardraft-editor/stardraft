# Images (images.dat)

images.dat has one entry for each image (999 entries). An image is a graphic with its animation. A sprite
([sprites.dat](sprites-dat.md#image-file)) has one image first and can add more (for example a shadow or an attack's
flash).

### GRP file

The graphics file of the image. StarCraft: Remastered does not use a change to this value.

- Raw value: an images.tbl string.

### Gfx turns

Yes: the image has frames for 17 directions, and the game mirrors them for the other directions (32 in all).

- Raw value: 0 (no) or 1 (yes).

### Clickable

Yes: a click on the image selects the unit.

- Raw value: 0 (no) or 1 (yes).

### Use full iscript

Yes: the game plays all the animations of the image's script, not only the first ones.

- Raw value: 0 (no) or 1 (yes).

### Draw if cloaked

Yes: the image shows when its unit is cloaked.

- Raw value: 0 (no) or 1 (yes).

### Draw function

How the game draws the image: normal, with the player's color, cloaked, as a shadow, with a color table (remapping),
and more.

- Raw value: a choice (the form names each one).

### Remapping

The color table for the draw function "remapping" (for example the colors of fire).

- Raw value: a choice (the form names each one).

### Iscript ID

The animation script of the image (iscript.bin): which frames show, the sounds, and when the unit attacks.

- Raw value: an iscript entry.

### Shield overlay

The image that shows when the unit's shields take a hit. StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Attack overlay

The image of the attack's flash. StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Damage overlay

The images of the fire and the damage of a building with few hit points. StarCraft: Remastered does not use a change
to this value.

- Raw value: a file (an images.tbl string).

### Special overlay

The images of special effects of the unit. StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Landing dust overlay

The dust when a Terran building lands. StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Lift-off overlay

The dust when a Terran building lifts off. StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

## Sources and credits

- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book's offset table (<https://armoha.github.io/eud-book/offset_table.html>).
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
