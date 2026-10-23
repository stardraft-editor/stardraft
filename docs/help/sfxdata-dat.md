# Sounds (sfxdata.dat)

sfxdata.dat has one entry for each sound (1144 entries), for example a unit's answer when you select it. Other .dat
files use these entries: for example, the sounds of a unit in [units.dat](units-dat.md#ready-sound).

The descriptions are from the StarCraft DAT page of Darkened Fantasies (game version 1.16.1):
<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>.

### Sound File

Sfxdata.tbl string index for the WAV path and filename, in the MPQ `sound\` directory. The value 0 gives the sound no
file.

The game loads a sound when it plays the sound for the first time. A sound with the Preload flag (see
[Flags](#flags)) loads before the map's code starts. Thus a change to the file of such a sound has no effect.

- Raw value: a sfxdata.tbl string (1 is the first string; 0: no file).

### Priority

When all sound channels are full, sounds with a higher priority value will not be preempted by those with a lower
value.

- Raw value: a number (0 to 255).

### Flags

The sound's Properties flags:

- 0x01 Permanently Loaded (the form: Preload): The sound will never be freed from memory.
- 0x02 Unit Response (the form: Unit Speech): Depending on the sound event, it will not play if either the "Unit
  Speech", "Unit Acknowledgements", or "Building Sounds" settings are turned off.
- 0x04 Recently Used (the form: Unused (0x04)): Managed by the game internally. Set when the game demands the sound to
  be played (is queued to be loaded in memory), and cleared on the first attempt to be freed from memory. The sound
  will be freed from memory on the next attempt if the flag remains cleared.
- 0x08 Buffer Creation Queued (the form: Unused (0x08)): Managed by the game internally. Set when the sound is queued
  to be loaded in memory, and cleared when the sound thread has loaded the sound. If this flag is manually set in the
  DAT file, the sound will never be loaded or played.
- 0x10 One At A Time: The sound will not play while an instance of this sound is already playing.
- 0x20 Never Preempt: When all sound channels are full, the sound will never be cut off regardless of priority value.
- 0x40 and 0x80 (the form: Unused (0x40), Unused (0x80)): the game does not use these flags.

Our names "Unused (0x04)" and "Unused (0x08)" are not fully correct: the game uses these flags, but only for its own
work. Do not set them. A set 0x08 flag makes the sound silent.

- Raw value: a set of 8 flags.

### Length Adjust

Portrait Duration: adjusts the speaking portrait time during transmissions (advisor alerts, building placement error,
etc.), which is based on the sound length.

- Raw value: a signed 16-bit number (-32768 to 32767).

### Minimum Volume

Defines the minimum volume percentage the sound will be played at when heard from a distance.

Volume strength is 99 - (99 * dist / 512), with dist being the sum distance from both nearest X and Y screen borders
when the sound origin is out of view (otherwise 0). The "Mute Unit Speech" trigger action divides the result by 5. If
the final volume strength is 10 or lower, the sound will not play.

- Raw value: a percentage, a signed 8-bit number (-128 to 127).

## Sources and credits

- The meaning of the fields and their values: the StarCraft DAT page of Darkened Fantasies
  (<https://www.darkenedfantasies.com/resources/?i=sc_fmt_dat>).
- The fields, their names and their places in the game's memory: EUD Editor 2 (<https://github.com/Buizz/EUDEditor>,
  MIT license, LeeJungHun).
- The field descriptions are from the StarCraft DAT page of Darkened Fantasies. The rest of this page is from the other
  sources and from the game's data.
