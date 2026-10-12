# StarDraft

StarDraft is a map editor for StarCraft: Remastered, for Windows. It edits classic maps, and it makes EUD maps
with new units, weapons and game rules from trigger blocks in plain sentences.

## Download

[Download the latest release](https://github.com/stardraft-editor/stardraft/releases/latest). Unzip it and run
`stardraft.exe`. StarCraft: Remastered must be installed.

See it in use: [watch the video](https://www.youtube.com/watch?v=pUr3eruPKA4). Questions and feedback: join the
[StarDraft Discord](https://discord.gg/JBMCwSRJBm).

Found a bug? Report it in StarDraft: right-click the part that is wrong and select "Report feedback on …". We
plan to release new versions often (about every day) to make StarDraft better quickly. Help > Check for updates
gets the newest one.

To build StarDraft from the source, see [docs/dev/DEVELOPING.md](docs/dev/DEVELOPING.md).

## Who StarDraft is for

1. For **Melee mappers** who create maps for ladders, tournaments and casual games, StarDraft provides powerful terraining tools like generation and symmetry.

2. For **UMS Scenario and Campaign mappers** who create custom maps such as defense, RPGs and or multi-map campaigns, StarDraft offers a modern classic trigger editor, easy to use unit settings, and a Mission Briefing editor with preview.

3. For **EUD mappers** who create interactive maps with customized units, weapons and game rules, StarDraft lets users edit StarCraft data and Advanced EUD Triggers in an intuitive way, with powerful debug tools for mappers who write their own EPS scripts.

### Maps

- Open and save .scm and .scx maps.
- Paint terrain with brushes of different shapes and sizes, spray, fill, replace, copy and paste, and symmetry.
- Place units, doodads, sprites and locations, with snap and placement rules.
- See the map in HD, SD or Carbot graphics.
- Show the buildable area around each base.
- Edit classic triggers and the unit, upgrade and technology settings of maps without EUDs. Classic maps keep up to
  3,400 units, save and load, and replays.

### EUD projects

- Keep the source map, triggers, data changes and scripts together in one project.
- Start a project from a blank map, a generated map or an existing map.
- Build the playable map with one command. StarDraft gets and runs [euddraft](https://github.com/armoha/euddraft).
- Link your own epScript files. StarDraft builds them as they are, and a build error names the file and the line.

### Data

- Edit units, weapons, upgrades, technologies, images, sprites and other .dat data, the strings, the requirements
  and the button sets.
- See what each value does: ranges, wireframes, the command card, and an image player that plays the unit's
  animations as the game does.

### Triggers

- Write triggers as blocks in plain sentences, with values from the game, loops, keys and the mouse, and colored text.
  No code is necessary.
- See the generated epScript beside the blocks.
- Find EUD errors and desyncs before you build.

### Briefings and sounds

- Edit briefings and the map's sounds, and play them back as the game does.

## Documentation

- [docs/dev/DEVELOPING.md](docs/dev/DEVELOPING.md): working on StarDraft: the code's layout, running it, the tests, the
  generated files, the conventions, releases.
- [docs/project/ROADMAP.md](docs/project/ROADMAP.md): the work that comes next.
- [docs/dev/PRINCIPLES.md](docs/dev/PRINCIPLES.md): the rules for each change: the product, new features, the UI and the text.
- [docs/dev/CONTROLS.md](docs/dev/CONTROLS.md): the control kit (one look for each kind of control) and where each control is used.
- [docs/project/FEEDBACK.md](docs/project/FEEDBACK.md): how feedback from users is used, and what an agent may do with it.
- [docs/user/CHANGELOG.md](docs/user/CHANGELOG.md): what is in each version, and the known problems.
- [NOTICE.md](NOTICE.md): the work StarDraft uses, and credits.
