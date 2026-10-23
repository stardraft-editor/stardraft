# Images (images.dat)

images.dat has one entry for each image (999 entries). An image is a graphic with its animation. A sprite
([sprites.dat](sprites-dat.md#image-file)) has one image first and can add more (for example a shadow or an attack's
flash).

### GRP file

The graphics file of the image. StarCraft: Remastered does not use a change to this value.

The string is the path and the name of the GRP file. The path starts in the unit\ folder of the game's MPQ files.

- Raw value: an images.tbl string.

### Gfx turns

Yes: the image has frames for 17 directions, and the game mirrors them for the other directions (32 in all).

The direction of the image's unit (or of a different source) selects the frame, from 0 to 16. When the image points
to the left, the game shows the frame mirrored.

- Raw value: 0 (no) or 1 (yes).

### Clickable

Yes: a click on the image selects the unit.

The image's unit must be a unit that a player can select. The click must be on the visible pixels of the image's
current frame.

- Raw value: 0 (no) or 1 (yes).

### Use full iscript

Yes: the game plays all the animations of the image's script, not only the first ones.

No: the image plays only its Init and Death animations. Use this for an overlay that must not follow the animations
of the unit's main image.

- Raw value: 0 (no) or 1 (yes).

### Draw if cloaked

Yes: the image shows when its unit is cloaked.

When the unit becomes cloaked, the image gets the cloak animation and the cloak colors. No: the image disappears
when its unit becomes cloaked.

- Raw value: 0 (no) or 1 (yes).

### Draw function

How the game draws the image: normal, with the player's color, cloaked, as a shadow, with a color table (remapping),
and more.

| Value | Draw function | What the game does |
| --- | --- | --- |
| 0 | Normal | It draws the image with the game's palette. |
| 1 | Normal (no hallucination) | It draws the image as Normal (the community calls it "Overlay"). The cloak does not change the image; it stays fully visible. |
| 2 | Non-vision cloaking | It plays the start of the cloak that the enemy cannot see. |
| 3 | Non-vision cloaked | It bends the pixels below the image. A table for each palette color sets the strength. |
| 4 | Non-vision decloaking | It plays the end of the cloak that the enemy cannot see. |
| 5 | Vision cloaking | It plays the start of the cloak that a detector sees. |
| 6 | Vision cloaked | It draws the image with the trans50.pcx color table (approximately 50% transparent). |
| 7 | Vision decloaking | It plays the end of the cloak that a detector sees. |
| 8 | EMP shockwave | It bends the pixels below the image. The palette index of each image pixel sets the strength. |
| 9 | Use remapping | It draws the image with the color table in the Remapping field. |
| 10 | Shadow | It draws the image with row 19 of the dark.pcx color table. Units use it for their shadows. |
| 11 | HP bar (crashes the game) | It does not draw the GRP. It draws a hit point bar with the size and position from sprites.dat. |
| 12 | Warp texture | It covers the image with the blue warp texture of a Protoss building under construction. |
| 13 | Selection circle | It draws the image with the tselect.pcx color table. Only palette colors 1 to 8 change. |
| 14 | Player color override (flags) | It draws the image as Normal, with the player color that the image kept at its start. |
| 15 | Hide graphics, show size rectangle | It does not draw the image. It draws a green rectangle around the image's visible pixels. |
| 16 | Hallucination | It draws the image with the shift.pcx color table. |
| 17 | Warp flash | It draws the image with the white colors of the ofire.pcx color table (the white flash). |

- Value 11 (HP bar) needs an image that has a unit. If the image has no unit, the game crashes.
- Value 13 changes only palette colors 1 to 8. Thus it has no visible effect on most graphics.
- Value 14 keeps the color of the first owner. The flags in Capture the Flag use it.
- Value 15 shows the outline of a building before a player places it.

- Raw value: a choice (the form names each one).

### Remapping

The color table for the draw function "remapping" (for example the colors of fire).

The game uses this value only when the Draw function is 9 (Use remapping). In version 1.16.1, these are the tables:

| Value | Color table |
| --- | --- |
| 0 | None. The game crashes. |
| 1 | ofire.pcx (orange fire) |
| 2 | gfire.pcx (green fire) |
| 3 | bfire.pcx (blue fire) |
| 4 | bexpl.pcx (blue explosion) |
| 5 | trans50.pcx (50% transparency) |
| 6 | red.pcx |
| 7 | green.pcx |

- Raw value: a choice. The form names the values 0 to 5.

### Iscript ID

The animation script of the image (iscript.bin): which frames show, the sounds, and when the unit attacks.

- Raw value: an iscript entry.

### Shield overlay

The image that shows when the unit's shields take a hit. StarCraft: Remastered does not use a change to this value.

The string is the name of the LO file. This file sets the positions of the shield hit image. Usual values:

| Value | Shield size |
| --- | --- |
| 0 | None |
| 2 | Medium |
| 133 | Small |
| 184 | Large |

- Raw value: a file (an images.tbl string).

### Attack overlay

The image of the attack's flash. StarCraft: Remastered does not use a change to this value.

The game uses this file for the attack image of the Bunker. The iscript commands imgoluselo, imguluselo and
sproluselo also use it for the images of attacks and spells.

- Raw value: a file (an images.tbl string).

### Damage overlay

The images of the fire and the damage of a building with few hit points. StarCraft: Remastered does not use a change
to this value.

The game uses this file for the fire and the blood on a damaged building.

- Raw value: a file (an images.tbl string).

### Special overlay

The images of special effects of the unit. StarCraft: Remastered does not use a change to this value.

The game uses this file for these positions:

- The resources or the power-up that a worker carries.
- The images of the iscript command creategasoverlays.
- The position of a subunit, with the iscript commands imgoluselo, imguluselo and sproluselo.

- Raw value: a file (an images.tbl string).

### Landing dust overlay

The dust when a Terran building lands. StarCraft: Remastered does not use a change to this value.

The file sets the positions of the dust clouds.

- Raw value: a file (an images.tbl string).

### Lift-off overlay

The dust when a Terran building lifts off. StarCraft: Remastered does not use a change to this value.

The file sets the positions of the dust clouds.

- Raw value: a file (an images.tbl string).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book's offset table (<https://armoha.github.io/eud-book/offset_table.html>).
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
