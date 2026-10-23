# Sounds (sfxdata.dat)

sfxdata.dat has one entry for each sound (1144 entries), for example a unit's answer when you select it. Other .dat
files use these entries: for example, the sounds of a unit in [units.dat](units-dat.md#ready-sound).

### Sound File

The sound's file: a string in sfxdata.tbl with the path and the name of the WAV file. The path starts in the `sound\`
folder of the game's data. The value 0 gives the sound no file.

The game loads a sound when it plays the sound for the first time. A sound with the Preload flag (see
[Flags](#flags)) loads before the map's code starts. Thus a change to the file of such a sound has no effect.

- Raw value: a sfxdata.tbl string (1 is the first string; 0: no file).

### Priority

The importance of the sound when all sound channels are in use. A sound with a lower value cannot stop a sound with a
higher value.

- Raw value: a number (0 to 255).

### Flags

How the game loads and plays the sound. The names of our flags are different from the names on the StarCraft DAT page
of Darkened Fantasies. This table shows the two names of each flag.

| Value | Our name | Its name in the source | What it does |
| --- | --- | --- | --- |
| 0x01 | Preload | Permanently Loaded | The game loads the sound before the map starts and keeps it in memory. |
| 0x02 | Unit Speech | Unit Response | The sound obeys the game's sound settings (Unit Speech, Unit Acknowledgements and Building Sounds). When the applicable setting is off, the sound does not play. |
| 0x04 | Unused (0x04) | Recently Used | The game sets and clears this flag itself, when it loads and frees the sound. |
| 0x08 | Unused (0x08) | Buffer Creation Queued | The game sets and clears this flag itself, while it loads the sound. If the .dat file sets it, the sound never loads and never plays. |
| 0x10 | One At A Time | One At A Time | The sound does not play while the same sound plays. |
| 0x20 | Never Preempt | Never Preempt | When all sound channels are in use, no other sound can stop this sound. The priority has no effect. |
| 0x40 | Unused (0x40) | (no name) | The game does not use this flag. |
| 0x80 | Unused (0x80) | (no name) | The game does not use this flag. |

Our names "Unused (0x04)" and "Unused (0x08)" are not fully correct: the game uses these flags, but only for its own
work. Do not set them. A set 0x08 flag makes the sound silent.

- Raw value: a set of 8 flags.

### Length Adjust

A change to the time that the talking portrait shows when the sound plays in a transmission. Transmissions are, for
example, the advisor's messages and the error message when you cannot put a building in a place. The game starts from
the length of the sound and adds this value.

- Raw value: a 16-bit number. In version 1.16.1, the game reads it as a signed number (-32768 to 32767).

### Minimum Volume

The lowest volume of the sound, as a percentage, when the sound comes from a place far from the screen.

In version 1.16.1, the game calculates the volume of a sound as follows:

1. If the place of the sound is on the screen, the distance is 0. Else the distance is the horizontal distance plus the
   vertical distance from the nearest edge of the screen, in pixels.
2. The volume is 99 - (99 x distance / 512).
3. The trigger action "Mute Unit Speech" divides the volume by 5.
4. If the volume is 10 or less, the sound does not play.

This field sets the lowest volume that the distance can give the sound.

- Raw value: a percentage. In version 1.16.1, the game reads it as a signed 8-bit number (-128 to 127).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The StarDraft 2 developers wrote this page in their own words, from these sources and from the game's data.
