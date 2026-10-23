# Images (images.dat)

images.dat has one entry for each image (999 entries). An image is a graphic with its animation. A sprite
([sprites.dat](sprites-dat.md#image-file)) has one image first and can add more (for example a shadow or an attack's
flash).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1)
(<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).

### GRP file

Images.tbl string index defining the GRP path and filename for the image's graphic, in the MPQ unit\ directory.

StarCraft: Remastered does not use a change to this value.

- Raw value: an images.tbl string.

### Gfx turns

Darkened Fantasies name: Graphic Turns.

If set, the image's frame index will vary between 0-16 depending on the direction the image is facing, as decided by
the parent entity's facing angle or some other means. The image will also be mirrored when facing to the left.

- Raw value: 0 (no) or 1 (yes).

### Clickable

Allows the image to trigger mouse interaction if it is part of a selectable entity, when the mouse cursor is within the
visible boundary of the image's current GRP frame.

- Raw value: 0 (no) or 1 (yes).

### Use full iscript

If not set, the image is restricted to playing the Init and Death animation only. The main use case for this is for
image overlays that should not follow the main graphic's animations.

- Raw value: 0 (no) or 1 (yes).

### Draw if cloaked

If set, the image will have the cloaking animation and remapping applied to it, when the image's entity is made to
cloak. Otherwise, the image will simply disappear.

- Raw value: 0 (no) or 1 (yes).

### Draw function

How the image's graphic is drawn on screen.

| Value | Draw function | Description |
| --- | --- | --- |
| 0 | Normal Draw (the form: Normal) | Painted normally using the game color palette. |
| 1 | Overlay Draw (the form: Normal (no hallucination)) | Same as Normal Draw, but is skipped by the cloaking procedures; the image will remain fully visible as though it was not under cloaking. |
| 2 | Hidden Cloaking (the form: Non-vision cloaking) | Plays the fade-in animation of the undetected type of cloaking. |
| 3 | Hidden Cloaked (the form: Non-vision cloaked) | Painted as a distortion of the pixels beneath itself. The strength of the distortion is based on a precomputed map for each palette color. |
| 4 | Hidden Decloaking (the form: Non-vision decloaking) | Plays the fade-out animation of the undetected type of cloaking. |
| 5 | Visible Cloaking (the form: Vision cloaking) | Plays the fade-in animation of the detected type of cloaking. |
| 6 | Visible Cloaked (the form: Vision cloaked) | Painted using the trans50.pcx color look-up table, which is a paletted approximation of 50% transparency. |
| 7 | Visible Decloaking (the form: Vision decloaking) | Plays the fade-out animation of the detected type of cloaking. |
| 8 | EMP Shockwave | Painted in a manner similar to Hidden Cloaking, but the strength of the distortion is taken directly from the palette index of the pixel from the image's graphic. |
| 9 | Use Remapping | Painted using one of the color remapping tables defined by the [Remapping](#remapping) field. |
| 10 | RLE_SHADOW (the form: Shadow) | Painted using the 19th row of the dark.pcx color look-up table, which is a paletted approximation of a vertical darkness gradient. Normally used for drawing the shadows of units, for example. |
| 11 | RLE_HPFLOATDRAW (the form: HP bar (crashes the game)) | Ignores the GRP graphic defined for the image and draws a health bar based on the health bar size and position defined in [sprites.dat](sprites-dat.md), and of course the unit's health. The CImage requires a valid unit pointer or this draw function will lead to a crash. |
| 12 | Warp Texture | The image is masked by the blue warping texture seen on Protoss buildings that finish constructing. |
| 13 | RLE_OUTLINE (the form: Selection circle) | Painted using the tselect.pcx color look-up table. Only the first 8 palette indexes (after index 0) are remapped, so using this on most graphics will have no apparent effect. Normally used to draw the selection circles. |
| 14 | RLE_PLAYER_SIDE (the form: Player color override (flags)) | Same as Normal Draw, but the player color is remapped by a value initially stored by the image instead of the sprite's current player ID. Normally used by the flag powerup to have it preserve the original owner's color when picked up. |
| 15 | Image Bounds (the form: Hide graphics, show size rectangle) | The image is not drawn, and instead a green rectangle outlines what would have been the visible pixels of the image. Normally used for drawing the outlines around building placement previews. |
| 16 | Hallucination | Painted using the shift.pcx color look-up table. |
| 17 | Warp Flash | Painted using the white gradient from the ofire.pcx color look-up table, making it appear as the fading-out white flash seen on Protoss buildings that finish constructing. |

- Raw value: a choice (the form names each one).

### Remapping

Darkened Fantasies name: Color Shift.

The color look-up table used when [Draw function](#draw-function) is set to Use Remapping.

| Value | Color table |
| --- | --- |
| 0 | none (crash) |
| 1 | ofire.pcx |
| 2 | gfire.pcx |
| 3 | bfire.pcx |
| 4 | bexpl.pcx |
| 5 | trans50.pcx |
| 6 | red.pcx |
| 7 | green.pcx |

- Raw value: a choice. The form names the values 0 to 5.

### Iscript ID

Iscript ID within iscript.bin.

- Raw value: an iscript entry.

### Shield overlay

Images.tbl string index defining the LO filename for the positioning of the shield hit overlay. Normally 0 for None, 2
for Medium, 133 for Small, and 184 for Large.

StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Attack overlay

Used for the Bunker attack overlay, and the iscript imgoluselo, imguluselo and sproluselo opcodes for various attack or
casting effects.

StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Damage overlay

Used for the flames and blood on damaged buildings.

StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Special overlay

Used for the position of carried resources/powerups on workers, the iscript creategasoverlays opcode, as well as the
imgoluselo, imguluselo and sproluselo opcodes for subunit placement.

StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Landing dust overlay

Used for the landing and liftoff smoke puffs.

StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

### Lift-off overlay

Darkened Fantasies name: Liftoff Dust Overlay.

Used for the landing and liftoff smoke puffs.

StarCraft: Remastered does not use a change to this value.

- Raw value: a file (an images.tbl string).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The fields that a map can change: the EUD Book's offset table (<https://armoha.github.io/eud-book/offset_table.html>).
- The field descriptions are from the StarCraft DAT page of Darkened Fantasies. The rest of this page is from these
  other sources and from the game's data.
