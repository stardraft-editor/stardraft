# Developing StarDraft

How the code is laid out, how to run and test it, what is generated, and the rules the code and the program's
texts follow.

## The code

| Folder | What is in it |
|---|---|
| `include/stardraft/`, `src/` | The library, without a window or a GPU: map files (`chk`, `map_file`: scenario.chk and the .scm/.scx archive), game data from the install (`game_data`: CASC), tilesets and ISOM terrain, units, sprites and iscript (`sprites`), doodads, locations, strings, the editor document with undo (`editor`), a classic map's unit, upgrade and tech settings (`classic_settings`) and triggers (`classic_triggers`), .dat editing (`dat_edit`, `dat_extras`), EUD projects and euddraft (`eud_project`, `euddraft`), triggers and their epScript (`triggers`), keys and mouse (`msqc`), briefings, portraits. |
| `src/render/`, `shaders/` | The map view's renderer: SDL3 GPU (D3D12), HLSL compiled by `dxc` at build time. |
| `apps/editor/` | The Windows program. `main.cpp` is the shell and most workspaces; the `.inc` files are parts of it: `ui_kit.inc` (the control kit), `data_extras.inc` (Strings, Buttons, requirements, unit display), `briefing_view.inc`, `files_view.inc`, `sound_files.inc`, `project_view.inc`, `kit_gallery.inc` (View > Control kit), `tiles_view.inc` (Terrain mode's Rectangle: the tools on tiles as they are, and the grid of every tile), `classic_settings.inc` (Scenario > Classic settings: a classic map's unit, upgrade and tech settings), `classic_triggers.inc` (Scenario > Classic triggers: a classic map's TRIG), `trigger_help.inc` (the block help page on F1, the fixes for heavy triggers, a euddraft error that selects its block), `trigger_selftest.inc` (the self-test's checks of those). |
| `src/data/` | Generated tables (see below). Do not edit them by hand. |
| `tools/` | Tools (each a CMake target) and the UI text lint. `tools/tests/`: the test programs; `tools/probes/`: one-off programs from finding out how the game works. |
| `docs/` | `dev/`: this guide, the principles, the control kit. `project/`: the roadmap, how feedback is used. `user/`: the changelog. |

The library knows nothing about Windows; the editor is the only Windows-only part. Keep it that way: platforms come
after 1.0 (see the roadmap).

## Build and run

You need Windows, Visual Studio 2022, CMake 3.20 or newer, and the Windows SDK (for `dxc`, the shader compiler).

    cmake -S . -B build
    cmake --build build --config Release --target stardraft_editor

To build while you use StarDraft (Windows locks a running .exe), give the build its own name and start the newest
build with `StarDraft-latest.exe` in the repository's root (built with the editor), which runs a copy of it; pin it to
Start or the taskbar. It also makes project.stardraft files open with it:

    cmake -S . -B build -DSTARDRAFT_EXE_NAME=stardraft-testing
    cmake --build build --config Release --target stardraft_editor
    StarDraft-latest.exe [map or project]

The window title shows when the build was made. The first build takes long: CMake fetches SDL3, StormLib, CascLib,
stb and libvpx.

StarDraft needs StarCraft: Remastered (it reads the game's files from the install). It finds the usual install
folder, or asks for it; Options > Program options > StarCraft folder changes it. EUD projects download euddraft on their first build.

## Tests

The tests need the StarCraft install; they do not run on GitHub. In the commands, `<install>` is the folder with
`.build.info` (usually `C:\Program Files (x86)\Starcraft Remastered\StarCraft`) and maps come from its `Maps` folder.
Give each test a new, empty scratch folder.

| Test | Command | What it checks |
|---|---|---|
| Self-test | `stardraft.exe <map> --selftest <dir>` | Drives the real program through a scripted session (every workspace) and saves a capture after each step; `selftest.txt` lists the results. Use `(2)Bottleneck.scm`. |
| `trigger_test` | `trigger_test <dir> <install> <map.scx> [--update-lock]` (a Brood War map, e.g. `Maps\BroodWar\(2)Binary Burghs.scx`) | The block library, the checker, the generated epScript, and that euddraft builds a map with it. Linked .eps files (R5): saved relative, built with what they import, a missing one stops Build, an error names the file and line. |
| `dat_test` | `dat_test <install> <map.scx> <dir>` | .dat editing, strings, requirements, buttons, unit display; euddraft builds a map with them. |
| `editor_test` | `editor_test <install> <map> <dir> [sounds]` | The editor document: placement, selection, copy and paste, undo, save and reopen. |
| `tools_test` | `tools_test <install> <badlands map> <dir>` | The terrain, doodad and unit tools. |
| `classic_trigger_test` | `classic_trigger_test <install> <maps folder>... [--survey]`; `classic_trigger_test <install> --migrate <map> <dir>` | Classic triggers (L.2): every map's TRIG and MBRF read and write back byte-identical; `--survey`: the flags of each condition and action type. `--migrate` (L.4): the map made a project and built by euddraft. |
| `briefing_test` | `briefing_test <install> [campaign folder]` | Briefings read and write back the same (campaign maps). |
| `portrait_test`, `hd_test`, `new_map_test` | `<install> ...` (see the usage line in each) | Portrait videos, HD graphics, new maps. |
| `paint_stress` | | Terrain correction: no cliff-stack breaks. |
| `terrain_check` | `terrain_check <install> <maps folder>...` | Without the GPU: the HD, HD2 and Carbot tiles of each tileset against the SD megatiles; each map's MTXM tiles resolve; each map sprite (THG2) has graphics and the frame and images the game starts it with. |
| `terrain_render_test` | `terrain_render_test <install> [<maps folder>...]` | The map view's renderer, offscreen: no tile grid when zoomed out (mipmaps), every megatile in the HD, HD2 and Carbot atlases, and each map in SD at 100% the same as `render_map`. |

Known failures (they come from before 0.5 and are on the list to fix): `editor_test` fails checks that depend on the
map (on Terran01: 2 about pasting; more on other maps); `tools_test` fails "2.1 sprayed doodads fit" and "2.3 every sprayed unit passes
checkPlacement".

Run the tests that a change needs; the whole set when the owner asks for it.

### UI text

`python -I tools/ui_text_lint.py` checks the texts that users see against the rules in `docs/dev/PRINCIPLES.md` ("Text"). It
extracts the strings of `apps/editor/main.cpp`, the `.inc` files of the editor, the checker messages in
`src/triggers.cpp` and the block texts (in `src/data/trigger_blocks.inc`), and it reads `README.md` and
`docs/user/CHANGELOG.md`. It needs only Python, and GitHub runs it before the build. A new violation makes it exit with
1. An accepted exception goes into `tools/ui_text_lint_baseline.txt`, one per line with its reason.

- `--warnings` also shows the heuristic checks (passive voice, lists of three). They do not change the exit code.
- `--dump <file>` writes all the extracted strings (file, line, surface, text) to a JSON file.

The tool does not see the self-test, the kit gallery, log lines and the theme code.

### Captures and measurements

`stardraft.exe <map or project> --datashots <dir> <spec>...` opens the file, does each spec and saves pictures into
`<dir>`, without a person at the mouse. Some specs:

| Spec | Result |
|---|---|
| `units:0`, `images:239`, `buttons:7`, ... | The Data workspace at that .dat entry. |
| `mouse:<x>:<y>` | The next captures show the data form as if the mouse were at that place of it (hover states: number arrows, grid labels). The real mouse does not move. |
| `ws:map`, `ws:data`, `ws:triggers`, `ws:briefing`, `ws:files`, `ws:project` | A workspace as it opens. |
| `trig:<root>:<index>...` | The Triggers workspace with that node selected. |
| `kit:` | The control kit's gallery, page by page. |
| `perf:` | `perf.txt`: start-up steps, opening the file, each workspace's first show and paint, the map frame, memory. |
| `view:sd`, `view:hd2`, `view:hd`, `view:carbot`, `cam:<x>:<y>:<zoom>` | The next captures use those graphics, and the map view centered on map pixel (x, y) at that zoom (1 = 100%). |
| `viewshot:<name>` | The map view only (no tool bars), as `<name>.png`. With `view:` and `cam:` before it: StarDraft's picture of a spot to compare with the game (see "Checks in the game through replays"). |
| `irec:<image>:<anim>:<ticks>`, `ideath:<anim>[:from:to]` | The image player tick by tick; every image's animation played to its end (a crash test). |
| `btnpop:`, `scriptpreview:`, `trigtext:...` | Pop-outs and panes. |
| `blockhelp:<block id>` | The help page of a trigger block (F1), as `blockhelp_<id>.png`. Put `theme:light:0` before it for the light theme. |
| `drawtest:` | D.2: a project `DrawFunctionTest` with one unit type for each draw function (created by a trigger at the start, in a 4 x 4 grid), built; `drawtest_reference.png` is StarDraft's picture of the same, `drawtest.txt` the grid. Play the map and compare. |
| `remaketest:`, `remake:on`, `remake:off` | R8: a project `RemakeTest` whose Terran Marine has the Terran Firebat's graphics, with preplaced marines and New graphics for preplaced units on, built. `remake.txt`: the changed unit types, the build and `generated/remake.eps`. `remake:on|off` sets the option for the next captures (`viewshot:`). |
| `classiccorpus:<folder>` | L.1 on a map corpus: opens each .scm, .scx and .chk under the folder by itself (classic mode), checks the rail, the grayed project commands, Build and Data refused, "(map)" in the title, Briefing and Assets, then saves it unchanged and compares each CHK section. `classiccorpus.txt`: a line per map and the totals. Protected maps do not open. |
| `classic:`, `classic:set` | L.3: the Classic settings window, each tab (`classic_units.png`, `classic_upgrades.png`, `classic_tech.png`). With `set`: the marine, Terran Infantry Armor and Stim Packs set through the fields, OK, saved and opened again, then undone; `classic.txt` says what holds. Open a classic map (not a project). |
| `classictrig:`, `classictrig:set`, `classicproject:` | L.2: the Classic triggers window (`classic_triggers.png`, `_light` in the light theme). With `set`: a trigger made through the window (player 1, Bring, Create unit, a text), OK, saved and opened again, undone. `classicproject:` (L.4): the map made a project, its sections compared, Classic triggers opened in the project. `classictrig.txt`, `classicproject.txt` say what holds. Open a classic map. |
| `classictrig:batch` | R23: a trigger for Player 1 with a block "For each unit" (Terran Marine, Terran Ghost, Zerg Zergling) that holds a block "For each player" (Player 1, Player 2), and Create unit in the inner block: its unit is the outer variable, its player the inner one. `classic_triggers_batch.png` (`_light` in the light theme); `classictrig.txt` says that the blocks read back the same after Apply and after a save and reopen, and how many undo steps take the page's changes back. Open a classic map. |
| `tiles:[<search>]` | N.7: Terrain mode in Rectangle, the Tiles grid's search set, the first tile with a picture picked: `tiles.png` and `tiles_panel.png`; `datashots.txt` counts the tiles shown and the null tiles. |
| `tilesplace:[<tile value>]` | After `tiles:`: places the picked tile on one tile and as a 6 x 4 rectangle in the middle of the map (`tiles_placed.png`), then paints a brush beside them (`tiles_brushed.png`); `datashots.txt` says how many placed tiles stay. |

The full list is in `main.cpp` (search for `--datashots`).

Test runs never play sound and never take the focus or move the mouse: they work in a window off the screen, and the
user may be at the computer. They also leave the user's settings (recent files, options, panes) alone. Keep it so
for new tests.

## Generated files

The tables in `src/data/` are generated, and the scripts that generate them are not in the repository. Do not edit
the tables by hand.

| File | From |
|---|---|
| `src/data/trigger_blocks.inc` | the block library |
| `src/data/dat_schema.inc` | EUD Editor 2's .def files and the EUD book's offset table |
| `src/data/dat_names.inc` | EUD Editor 2's name lists (entries, icons, iscript IDs) |
| `src/data/dat_extras.inc` | EUD Editor 3's memory dumps |
| `src/data/<tileset>_tile_rules.inc` | Blizzard maps (corpus) |
| `src/data/tile_edges.inc` | the CV5 files from CASC |
| `src/data/eud_names.inc` | eudplib's name tables (in euddraft) |
| `apps/editor/res/` (icons, UI icon atlases, `ui_icons.h`) | the icon tool (`python tools/import_icons.py`) |

EUD Editor 2 and 3 are MIT licensed (LeeJungHun); `NOTICE.md` has the credits.

## Rules

The principles for the product, the UI and the text (game data, no desync, speed, undo, the control kit, ASD-STE100)
are in `docs/dev/PRINCIPLES.md`. Read them before a change. The rules here are for the code only.

**Triggers.** The block library is data (`src/data/trigger_blocks.inc`); a block's code is generated in
`src/triggers.cpp`.

**Storage buffers in shaders.** SDL binds every storage buffer as a raw view. Read it as `ByteAddressBuffer`, never as
`StructuredBuffer`: some drivers (GTX 1080) then read one float per field, and no sprite or rectangle draws.

## Crash reports, feedback and updates

All three use GitHub, with the token in the repository secret `STARDRAFT_ISSUES_TOKEN`: a fine-grained token for
this repository only, with **Issues: read and write** and **Contents: read**. CI builds it into the program. A build made here reads the token from `.issues-token` in the
repository's root (one line; not in git); without it, reports and feedback stay on disk and go when a build with a
token starts. The environment variable `STARDRAFT_ISSUES_TOKEN` also gives one, at build time or when StarDraft runs. All three send their requests through `apps/editor/github.inc`; a feedback file stays when GitHub's reply cannot be read.

- **Crash reports** (`apps/editor/crash.inc`): a crash writes `crash.dmp` and `report.txt` (version, commit, Windows,
  video card, the exception, the stack, the last log lines) into `%LOCALAPPDATA%\StarDraft\crashes\<time>\` and
  sends the report at once, with no user action: an issue with the label `crash`, or a comment on the open issue for
  the same place. Reports that could not go are sent at the next start. `stardraft.exe --crash-test` crashes on
  purpose. Each CI run keeps the build's `stardraft.pdb` (artifact `stardraft-symbols-<commit>`, 90 days), and each
  release keeps it for good in the draft release `symbols-v<version>`: open `crash.dmp` with it to get function names
  and lines.
- **Feedback** (`apps/editor/feedback.inc`): a right click on almost anything gives "Report feedback on <the
  thing>...", and Help > Report feedback. Every context menu goes through `popupMenu()`, which adds the item; where a
  right click opens no menu, the main loop opens one with only that item. A painted pane names what is under the
  mouse by answering `WM_FEEDBACK_NAME`; else a tooltip's text, a control's text or the pane's name is used. Send
  makes an issue with the label `feedback` (no user name); feedback that could not go waits in
  `%LOCALAPPDATA%\StarDraft\feedback\`. The glyph and its color (`kFeedback`, yellow) are the same everywhere.
- **Updates** (`apps/editor/update.inc`): Help > Check for updates compares the newest release's title ("... (build
  <commit>)") with the build's commit, downloads the zip, puts its `stardraft.exe` in place of the running one (that
  one becomes `stardraft.exe.old`, deleted at the next start) and starts it. A release build also checks at start and
  says so in the status bar.

## Commits and releases

Commit messages say what changed for the user, in a few lines. Every push builds `stardraft.exe` on GitHub (an
artifact of the run). A push never makes a release: push when the work is done. Build before you push; run the tests
that a change needs.

A release is a tag `v<version>`. The owner says when, and approves the scope and the release notes. Then
`tools/release.py` does the rest (`python -I tools/release.py` lists its commands):

1. `release.py pending`: the newest release, the commits after it (`*`: not pushed) and the next version number. The
   release notes come from this list.
2. A `## <version>` section at the top of `docs/user/CHANGELOG.md`, in the groups the file uses (New, Map, Data,
   Triggers, Fixes, Known problems). Each line names a feature that users get, as a short fact: "New language support:
   Russian and Chinese (simplified)." No changes for developers only (CI, build, tests), no label renames, no
   implementation details; the program is not an actor ("asks", "shows its ...").
3. Screenshots, one for each visible change, at most 10: write `tools/shots/<version>.json` (see
   `tools/shots/README.md`) and run `release.py shots <version>`. Put each `![...](images/<version>/...)` line that it
   writes under its group.
4. `release.py check <version>` changes nothing. It checks that the version comes after the newest tag, the push
   account works, nothing else is open, the section and its pictures are good, the UI text and the translation tables
   pass, and all targets build (in `build`, the testing build).
5. `release.py publish <version> --yes` checks again, then commits "Changelog <version>" (the section and its
   pictures), tags `v<version>`, and pushes `main` and the tag together as `milestone-dev` (gh's active account stays
   as it is). Then it waits for CI (about 8 minutes) and confirms the release. `--dry-run` shows the commands only.

CI (`.github/workflows/build.yml`) checks the tag first (`release.py ci-check`: a version, its section, its pictures,
a commit on `main`). Then it builds that commit as that version and makes the release "StarDraft 2 - v<version> (build
<commit>)" with `StarDraft-<version>-win64.zip` (only `stardraft.exe`), marked Latest. Its text is the version's
section, then the changelog's last section (what StarDraft is), with the pictures as addresses at the tag
(`release.py notes <version>` shows the text). Discord gets one post with the pictures. The symbols (`stardraft.pdb`)
go to the draft release `symbols-v<version>`, which only the repository's members see, for crash reports. Check for
updates finds the release. Each release keeps its own download count: `gh api repos/stardraft-editor/stardraft/releases
--jq '.[] | "\(.tag_name)  \([.assets[].download_count] | add)"'` lists them.

**When a release run fails.**

- Before the release is there (the tag check, the build): fix it on `main`, then `release.py untag <version>` (it
  deletes the tag here and on GitHub) and `release.py publish <version> --yes` again. Nobody has the release.
- The push stops (main moved on GitHub, or no network): publish deletes the tag here and keeps the "Changelog" commit.
  `git pull --rebase`, then publish again.
- After the release is there (a Discord post did not go; the run is red): post again in that channel with Actions >
  Post screenshots to Discord. A re-run of the run changes nothing in a release that has its zip.
- A release that people have is never moved, and only the newest version is released (CI refuses an older tag): fix
  it in a new version.

The website (`stardraft-editor/stardraft-editor.github.io`) needs no release step: its download button gets the newest
release. A change to `docs/project/ROADMAP.md` builds it again (`.github/workflows/roadmap.yml`).
