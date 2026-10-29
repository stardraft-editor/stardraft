# Developing StarDraft

How the code is laid out, how to run and test it, what is generated, and the rules the code and the program's
texts follow.

## The code

| Folder | What is in it |
|---|---|
| `include/stardraft/`, `src/` | The library, without a window or a GPU: map files (`chk`, `map_file`: scenario.chk and the .scm/.scx archive), game data from the install (`game_data`: CASC), tilesets and ISOM terrain, units, sprites and iscript (`sprites`), doodads, locations, strings, the editor document with undo (`editor`), a classic map's unit, upgrade and tech settings (`classic_settings`) and triggers (`classic_triggers`), .dat editing (`dat_edit`, `dat_extras`), EUD projects and euddraft (`eud_project`, `euddraft`), triggers and their epScript (`triggers`), keys and mouse (`msqc`), briefings, portraits. |
| `src/render/`, `shaders/` | The map view's renderer: SDL3 GPU (Direct3D 12 on Windows, Vulkan on Linux, Metal on macOS). HLSL, compiled to DXIL by `dxc` at build time; the SPIR-V and MSL in `shaders/gen/` are generated (see below). |
| `apps/editor/` | The editor. `main_sdl.cpp` is the program (SDL3 on Windows, macOS and Linux); `shell_sdl.inc` and `shell_sdl_win.inc` (its Windows parts: the crash handler, the file types, the files built into the program) are the host's side of `shell.h`; the `sdl_*.inc` files are the windows, the panes and the dialogs. `core/` is the portable core (`core/core.inc`: the commands, the map's tools and every view, drawn on the Canvas of `apps/common/canvas.h`); `core/core_check.cpp` (target `editor_core_check`) compiles it with no Win32 headers. |
| `src/data/` | Generated tables (see below). Do not edit them by hand. |
| `tools/` | Tools (each a CMake target) and the UI text lint. `tools/tests/`: the test programs; `tools/probes/`: one-off programs from finding out how the game works; `tools/plugins/<plugin>/`: a plugin's scripts, probes and tests (`cmbw`: docs/dev/cmbw-update.md). |
| `docs/` | `dev/`: this guide, the principles, the control kit. `project/`: the roadmap, how feedback is used. `user/`: the changelog. `help/`: the help pages (see "Help pages" below). |

The library knows nothing about Windows; in the editor, only `shell_sdl_win.inc` and `apps/common`'s `*_win32.cpp`
files are Windows-only. Keep it that way.

## Build and run

You need Windows, Visual Studio 2022, CMake 3.20 or newer, and the Windows SDK (for `dxc`, the shader compiler).

    cmake -S . -B build
    cmake --build build --config Release --target stardraft_editor

The target `stardraft_editor` is `stardraft.exe`: the SDL program (`apps/editor/main_sdl.cpp` and its `sdl_*.inc`
files, which draw the portable views in `apps/editor/core`), the same program as on macOS and Linux. Its fonts and
icons are in the program file, so `stardraft.exe` runs alone (a release is that one file); the `res` and `fonts`
folders that the build puts next to it come first when they are there.

To build while you use StarDraft (Windows locks a running .exe), give the build its own name and start the newest
build with `StarDraft-latest.exe` in the repository's root (built with the editor), which runs a copy of it; pin it to
Start or the taskbar. It also makes project.stardraft files open with it:

    cmake -S . -B build -DSTARDRAFT_EXE_NAME=stardraft-testing
    cmake --build build --config Release --target stardraft_editor
    StarDraft-latest.exe [map or project]

The window title shows when the build was made.
The first build takes long: CMake fetches SDL3, StormLib, CascLib, stb, libvpx and Dear ImGui, and the fonts.

### Snapshots

A snapshot draws one frame without a window and writes it as a PNG; a snapshot run asks nothing, sends nothing and
changes no settings:

    set SDL_VIDEODRIVER=offscreen
    build\Release\stardraft.exe [map] --snapshot out.png
    build\Release\stardraft.exe [map] --workspace triggers --snapshot out.png
    build\Release\stardraft.exe [map] --dialog options --size 720x820 --snapshot out.png

The comments on the options in `main()` of `main_sdl.cpp` (`--workspace`, `--dialog`, `--data`, ...) say what a
snapshot can show. `tools/sdl_snapshots.py` takes one snapshot of each workspace and dialog and checks each one
(`--help` tells how; `--install`, `--map` and `--project` add the cases that need them):

    python -I tools/sdl_snapshots.py --exe build\Release\stardraft.exe --out snapshots

`--jobs <n>` runs n cases at the same time (each case has its own files; the shared input files are written once,
before the runs). With all the data, the 215 cases take about 3.5 minutes one at a time and 1.2 minutes with `--jobs 3`;
use at most `--jobs 3` on a computer with 8 GB of free memory.

GitHub builds `stardraft.exe` on each push (the job windows), runs `sdl_snapshots.py` on it, takes a snapshot of it
alone in a folder (as a release has it), and keeps the snapshots (with no game data: no map) and the program with the
run. GitHub's Windows machines have no GPU: there Direct3D 12 draws on the CPU (WARP).

### Linux and macOS

The library, the tools and the tests also build on Linux (GCC or Clang) and macOS (Xcode's Clang); GitHub builds
them on each push. The SDL program builds there as `stardraft` (target `stardraft_editor`). The renderer and its tests use Vulkan on Linux and Metal on macOS.
On Linux, SDL needs the X11 or Wayland headers (SDL's `docs/README-linux.md`), or `-DSDL_UNIX_CONSOLE_BUILD=ON`
for the tools only; `-DSTARDRAFT_RENDERER=OFF` builds without SDL.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

To run the SDL program and take its snapshots (the same options as on Windows):

    build/stardraft [map]                                        # Linux
    open build/stardraft.app --args [map]                        # macOS
    python3 -I tools/sdl_snapshots.py --exe build/stardraft --out snapshots
    python3 -I tools/sdl_snapshots.py --exe build/stardraft.app/Contents/MacOS/stardraft --out snapshots   # macOS

The `res` and `fonts` folders next to `stardraft` (on macOS, in the app) must stay with it.

The renderer's tools render offscreen; with no display, set `SDL_VIDEODRIVER=offscreen`. On Windows,
`STARDRAFT_GPU_DRIVER=vulkan` (an environment variable) makes the renderer, the editor's map view included, use
Vulkan instead of Direct3D 12.

What does not work there:

- euddraft has no Linux arm64 build, and its macOS build compiles `.eps` scripts only on an arm64 Mac. On Linux,
  installing euddraft needs `unzip` and `curl`.
- epTrace is a Windows program: `eptrace::exe()` is always empty on other systems.
- CascLib needs the install's file names in their exact case on a case-sensitive file system (`Data/data`, ...).

StarDraft needs StarCraft: Remastered (it reads the game's files from the install). It finds the usual install
folder, or asks for it; Options > Program options > StarCraft folder changes it. EUD projects download euddraft on their first build.

### The macOS app: the disk image, signing and notarizing

The macOS download is `StarDraft-<version>-macos-arm64.dmg` (Apple silicon): `StarDraft.app` in a disk image with a
background and a link to Applications. The `macos` job of CI makes it on each push (`tools/ci/mac_dist.sh`, an
artifact of the run). For a tag `v<version>`, the job `macos-release` attaches it to the release that the `windows` job
made, but only when the repository variable `RELEASE_MAC` is `on` (default: off; a release has only the Windows build
for now). To make it on a Mac:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build
    bash tools/ci/mac_dist.sh build/stardraft.app StarDraft.dmg

What is in the app for the system:

- Info.plist (`apps/editor/Info.plist.in`) exports the types of `.scm` and `.scx` maps
  (`io.github.stardraft-editor.map-scm`, `-scx`). StarDraft is their owner, so Finder opens them with it. Their icons
  are made by the system from the app's icon and the extension. The type's name ("StarCraft map") is translated in
  `<language>.lproj/InfoPlist.strings`, which CMake makes from the line `StarCraft map` of `res/lang/*.tsv`.
- Two Quick Look extensions in `Contents/PlugIns` (`apps/quicklook`, built by CMake): `StarDraftThumbnail.appex` is a
  map's icon in Finder, `StarDraftPreview.appex` is the space bar's preview (the picture, the name, the size, the
  description). Both draw the map with `map_preview.cpp` (the minimap's colors, no GPU). An extension is in a sandbox
  and cannot read the game data: when a map opens, the editor writes its tileset's colors to
  `~/Library/Application Support/StarDraft/quicklook/tile-colors-<era>.bin`, which the extensions can read
  (`quicklook.entitlements`). Before that, a map is drawn in the tileset's base color.
- Spotlight: when a map opens, the editor adds its name, its description and its picture to Spotlight (CoreSpotlight,
  `mac_dist.mm`). A Spotlight result opens the map in StarDraft.

Test runs: `--check bundle` prints the app's types, extensions and signatures, `--check spotlight` makes the open map's
Spotlight entry and removes it, `--check preview:<folder>` draws the open map's Quick Look picture (on every system).
`tools/sdl_snapshots.py` runs them (`check-bundle`, `map-check-spotlight`, `map-check-preview`); `map_preview_test`
tests the picture with no game data.

**Signing.** The app and its extensions get the hardened runtime and their entitlements
(`apps/editor/stardraft.entitlements`: the network for updates and feedback, the files that the user picks;
`apps/quicklook/quicklook.entitlements`: the sandbox, the map that Quick Look gives, the tile colors' folder). Metal
(the GPU) needs no entitlement. The app itself is not in a sandbox: it reads the StarCraft install and runs euddraft.
`mac_dist.sh` signs each extension first, then the app, then the disk image. These repository secrets (Settings >
Secrets and variables > Actions) give it the Developer ID:

| Secret | What it is |
|---|---|
| `APPLE_CERT_P12` | The "Developer ID Application" certificate with its private key, exported from Keychain Access as a `.p12` file, in base64 (`base64 -i cert.p12 \| pbcopy`). |
| `APPLE_CERT_PASSWORD` | The password of that `.p12` file. |
| `APPLE_ID` | The Apple ID (an e-mail address) of the developer account, for `notarytool`. |
| `APPLE_TEAM_ID` | The team ID of the account (10 characters: developer.apple.com > Account > Membership). |
| `APPLE_APP_PASSWORD` | An app-specific password of that Apple ID (account.apple.com > Sign-In and Security > App-Specific Passwords). |

All five need an Apple Developer Program membership (paid, each year). With the first two, the app and the disk image
are signed with the Developer ID and a timestamp; with all five, Apple notarizes the disk image (`xcrun notarytool
submit --wait`, a few minutes) and its ticket is stapled (`xcrun stapler staple`), so macOS opens the app with no
warning, also with no network. Without the secrets (a fork, a pull request, or no account yet), the app is signed ad
hoc and the log says "Signing skipped". macOS then shows a warning the first time: the user right-clicks the app and
selects Open, or allows it in System Settings > Privacy & Security.

## Tests

Most tests need the StarCraft install, so GitHub does not run them (it runs `trigger_test` without a map and
`render_smoke_test`). In the commands, `<install>` is the folder with
`.build.info` (usually `C:\Program Files (x86)\Starcraft Remastered\StarCraft`) and maps come from its `Maps` folder.
Give each test a new, empty scratch folder.

| Test | Command | What it checks |
|---|---|---|
| `sdl_snapshots.py` | `python -I tools/sdl_snapshots.py --exe <stardraft.exe> --out <dir> --install <install> --map <map> [--project <project>] [--jobs 3]` | The real program, with no window: each workspace, mode and dialog, and the cases that drive it with the mouse and the keys (place, select, paste, undo, save, the Data form, the triggers) and check what its output says. Use `(2)Bottleneck.scm`. See "Snapshots" above. |
| `trigger_test` | `trigger_test <dir> <install> <map.scx> [--update-lock]` (a Brood War map, e.g. `Maps\BroodWar\(2)Binary Burghs.scx`) | The block library, the checker, the generated epScript, and that euddraft builds a map with it. Linked .eps files (R5): saved relative, built with what they import, a missing one stops Build, an error names the file and line. |
| `dat_test` | `dat_test <install> <map.scx> <dir>` | .dat editing, strings, requirements, buttons, unit display; euddraft builds a map with them. New graphics for preplaced units (R8): the plan, the code that makes the units, and the built map without them. |
| `editor_test` | `editor_test <install> <map> <dir> [sounds]` | The editor document: placement, selection, copy and paste, undo, save and reopen. |
| `tools_test` | `tools_test <install> <badlands map> <dir>` | The terrain, doodad and unit tools. |
| `classic_trigger_test` | `classic_trigger_test <install> <maps folder>... [--survey]`; `classic_trigger_test <install> --migrate <map> <dir>` | Classic triggers (L.2): every map's TRIG and MBRF read and write back byte-identical; the block kind table (one row per kind) and the turned-off flag (bit 8) read and write back; `--survey`: the flags of each condition and action type. `--migrate` (L.4): the map made a project and built by euddraft. |
| `briefing_test` | `briefing_test <install> [campaign folder]` | Briefings read and write back the same (campaign maps). |
| `portrait_test`, `hd_test`, `new_map_test` | `<install> ...` (see the usage line in each) | Portrait videos, HD graphics, new maps. |
| `paint_stress` | | Terrain correction: no cliff-stack breaks. |
| `terrain_check` | `terrain_check <install> <maps folder>...` | Without the GPU: the HD, HD2 and Carbot tiles of each tileset against the SD megatiles; each map's MTXM tiles resolve; each map sprite (THG2) has graphics and the frame and images the game starts it with. |
| `commands_test` | `commands_test` | Without game data: the command table (shortcuts, run by id, enabled, checked, menus). |
| `help_test` | `help_test` | Without game data: the help pages built into the program (`stardraft/help.h`), their links, the place of each .dat field (F1), the search, and the converted values of the Data form (`stardraft/dat_units.h`). |
| `sim_test` | `sim_test [<install>]` (build with `-DSTARDRAFT_RENDERER=OFF` if you have no SDL) | The simulation (`tools/sim`, see `SIMULATION.md`): mining and three fights on `(2)Bottleneck.scm`, beside the reference results. |
| `platform_test` | `platform_test <dir>` | Without game data or network: `apps/common`'s self-update parts (unpacking, replacing a program file) and the crash report's title. |
| `settings_test` | `settings_test <dir>` | Without game data: the settings file (each kind of value written and read again). |
| `render_smoke_test` | `render_smoke_test [out.png]` | Without game data: the renderer's device and pipelines (each shader in the GPU API's format), and rectangles, lines and text in an offscreen frame. GitHub runs it on Linux (Vulkan on lavapipe). |
| `terrain_render_test` | `terrain_render_test <install> [<maps folder>...]` | The map view's renderer, offscreen: no tile grid when zoomed out (mipmaps), every megatile in the HD, HD2 and Carbot atlases, and each map in SD at 100% the same as `render_map`. |

Known failures (they come from before 0.5 and are on the list to fix): `editor_test` fails checks that depend on the
map (on Terran01: 2 about pasting; more on other maps); `tools_test` fails "2.1 sprayed doodads fit" and "2.3 every sprayed unit passes
checkPlacement".

Run the tests that a change needs; the whole set when the owner asks for it.

### Help pages

The help pages are Markdown files in `docs/help/`, one page per topic; `index.md` links to each page, in the order of
the help window's list. `python -I tools/help_build.py` checks them (each link goes to a page and a heading that are
there, each page other than the index has "Sources and credits", and each field of the eight .dat files has a heading
on its page, `<dat>-dat.md`, with the id that F1 opens). The build runs it to put the pages into the program
(`generated/help_pages.cpp`: Help > Documentation and F1 work offline), so a broken link stops the build; GitHub runs
it before the build too, and `.github/workflows/pages.yml` publishes the same pages (`--site`) on GitHub Pages under
`/help/` after a push to master. Write the pages in the program's own words (ASD-STE100, as the UI texts) and name the
sources at the end of each page. A heading's id is its text in lower case with a "-" for each run of other
characters ("Rank/sublabel": `rank-sublabel`); `{#id}` at the end of a heading gives it another id. A .dat field
whose raw value is in a unit that is difficult to read gets its converted value in `src/dat_units.cpp` (the Data form
shows it after the value), and its page says the same.

### UI text

`python -I tools/ui_text_lint.py` checks the texts that users see against the rules in `docs/dev/PRINCIPLES.md` ("Text"). It
extracts the strings of `apps/editor/main_sdl.cpp`, the `.inc` files of the editor, the checker messages in
`src/triggers.cpp` and the block texts (in `src/data/trigger_blocks.inc`), and it reads `README.md` and
`docs/user/CHANGELOG.md`. It needs only Python, and GitHub runs it before the build. A new violation makes it exit with
1. An accepted exception goes into `tools/ui_text_lint_baseline.txt`, one per line with its reason.

- `--warnings` also shows the heuristic checks (passive voice, lists of three). They do not change the exit code.
- `--dump <file>` writes all the extracted strings (file, line, surface, text) to a JSON file.

The tool does not see the kit gallery, log lines and the theme code.

### Captures and measurements

Captures are snapshots (see "Snapshots" above): `--snapshot <png>` with the options of `main()` in `main_sdl.cpp`
(`--workspace`, `--mode`, `--dialog`, `--data`, `--trig`, `--classictrig`, `--key`, `--click`, `--drag`, `--lostdrag`, `--type`,
...), which open a state or drive the program with the mouse and the keys before the picture. Each case of
`tools/sdl_snapshots.py` is one such run (`--list` shows them); a case's check reads what the run prints. The release
pictures are cases too (`tools/shots/README.md`).

A long session is `--script <file>[:<times>]` (`apps/editor/sdl_soak.inc`): the inputs of a file, one a line
(`click 700,650`, `key Ctrl+Z`, `mode terrain`, `workspace data`, `open <map>`), that many times over. `undocheck`
undoes and redoes the undo steps and compares the map at each step, `roundtrip <file>` saves the map and opens it again
(`{n}` in a value: the time over), and `stats` prints the memory (the working set, the GPU's, the bitmaps with a
texture), the undo steps and the frame times since the last `stats`; `open` prints the open's time and the first
frame's. The cases `map-script-session`, `map-script-dialogs` and `project-script-reopen` use it.

Test runs never play sound and never take the focus or move the mouse: they draw with no window
(`SDL_VIDEODRIVER=offscreen`), and the user may be at the computer. They also leave the user's settings (recent files, options, panes) alone. Keep it so
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
| `src/data/eud_names.inc` | eudplib's name tables (in euddraft) |
| `apps/editor/res/` (icons, UI icon atlases, `ui_icons.h`) | the icon tool (`python tools/import_icons.py`) |
| `generated/help_pages.cpp` (in the build folder, not in the repository) | `docs/help/*.md`, by `tools/help_build.py` at build time |
| `shaders/gen/` (SPIR-V and MSL) | `shaders/*.hlsl`, by `python -I tools/gen_shaders.py --shadercross <program>` (its text says which SDL_shadercross). After a change to a shader, run it and commit the result: GitHub checks that `shaders/gen/sources.txt` matches the shaders. |

EUD Editor 2 and 3 are MIT licensed (LeeJungHun); `NOTICE.md` has the credits.

## Rules

The principles for the product, the UI and the text (game data, no desync, speed, undo, the control kit, ASD-STE100)
are in `docs/dev/PRINCIPLES.md`. Read them before a change. The rules here are for the code only.

**Triggers.** The block library is data (`src/data/trigger_blocks.inc`); a block's code is generated in
`src/triggers.cpp`.

**Storage buffers in shaders.** SDL binds every storage buffer as a raw view. Read it as `ByteAddressBuffer`, never as
`StructuredBuffer`: some drivers (GTX 1080) then read one float per field, and no sprite or rectangle draws.

## Settings

The program keeps its settings (options, layout, recent files) in `%APPDATA%\StarDraft\settings.json`
(`stardraft::Settings`, `apps/editor/core/prefs.inc`). A value the file does not have yet is read from the registry
(`HKCU\Software\StarDraft`), where builds before the file kept it; the registry is not written. The environment
variable `STARDRAFT_SETTINGS` gives another file (and then the registry is not read: a test never reads the user's).
File associations stay in the registry. A snapshot reads a settings file as a start does with `--settings <file>`, and
on Windows the registry of the builds before the file from a registry file of its own with `--legacyhive <file>`;
`--check settings` prints what it read (`tools/sdl_snapshots.py`: the `update-*` cases).

The user's custom terrain (Terrain mode's Custom: `apps/editor/core/stamps.inc`) is one file each, in
`stamps\<tileset>\` beside the settings file (Linux: `$XDG_DATA_HOME/StarDraft/stamps`); `include/stardraft/terrain_stamp.h`
has the file's format. `STARDRAFT_STAMPS_DIR` or `--stamps <folder>` gives another folder; a test or snapshot run without
them uses a new folder in the temporary folder, never the user's.

## Crash reports, feedback and updates

All three use GitHub, with the token in the repository secret `STARDRAFT_ISSUES_TOKEN`: a fine-grained token for
this repository only, with **Issues: read and write** and **Contents: read**. CI builds it into the program. A build made here reads the token from `.issues-token` in the
repository's root (one line; not in git); without it, reports and feedback stay on disk and go when a build with a
token starts. The environment variable `STARDRAFT_ISSUES_TOKEN` also gives one, at build time or when StarDraft runs. All three send their requests through `apps/common/http.h` (WinHTTP on Windows, curl elsewhere; `http_get <url>` tries one by hand); a feedback file stays when GitHub's reply cannot be read.

- **Crash reports** (`apps/editor/shell_sdl_win.inc` and `core/releases.inc`; the crash handler and the sending in `apps/common/crash_report.h`): a crash writes `crash.dmp` and `report.txt` (version, commit, Windows,
  video card, the exception, the stack, the last log lines) into `%LOCALAPPDATA%\StarDraft\crashes\<time>\` and
  sends the report at once, with no user action: an issue with the label `crash`, or a comment on the open issue for
  the same place. Reports that could not go are sent at the next start. `stardraft.exe --crash-test` crashes on
  purpose (a test run: the report stays on disk; `--crash-test stack`: with a stack overflow, the report is written by
  a thread of its own); `--check crashes` prints the reports and what the next start does
  with them. On macOS, Linux and the BSDs a signal handler (`apps/common/crash_posix.cpp`) writes `report.txt` only (no
  minidump) into `~/Library/Application Support/StarDraft/crashes/<time>/` (macOS) or
  `$XDG_STATE_HOME/StarDraft/crashes/<time>/` (Linux and the BSDs); the question, the sending and `--check crashes` are
  the same as on Windows (`apps/editor/shell_sdl_win.inc`). Each CI run keeps the build's `stardraft.pdb` (artifact `stardraft-symbols-<commit>`, 90 days), and each
  release keeps it for good in the draft release `symbols-v<version>`: open `crash.dmp` with it to get function names
  and lines.
- **Feedback** (`core/views_feedback.inc`, `core/feedback_send.inc`, `sdl_options.inc`): a right click on almost
  anything gives "Report feedback on <the thing>...", and Help > Report feedback. Every context menu adds the item;
  where a right click opens no menu, one opens with only that item. A view names what is under the mouse
  (`feedbackName`). Send
  makes an issue with the label `feedback` (no user name); feedback that could not go waits in
  `%LOCALAPPDATA%\StarDraft\feedback\`. The glyph and its color (`kFeedback`, yellow) are the same everywhere.
- **Updates** (`apps/editor/sdl_updates.inc` and `core/releases.inc`; unpacking and replacing the program in `apps/common/update.h`): Help > Check for updates compares the newest release's title ("... (build
  <commit>)") with the build's commit, downloads the zip, puts its `stardraft.exe` in place of the running one (that
  one becomes `stardraft.exe.old`) and starts it with `--restart <pid>` and the open maps and projects (as Restart
  now); the new program waits for the old one to end and deletes `stardraft.exe.old`. A new program that does not
  start (a damaged download) goes, and the old one is in its place again. A release build also checks at start and
  says so in the status bar. An installed `stardraft.exe` of an older release (the Windows program before the SDL
  one) gets the new program in this way: the program in the zip is `stardraft.exe`. The zip also has
  `stardraft-cli.exe`, which an update puts beside the program (`placeReleaseCli`). `stardraft.exe --update-test <zip>
  [options]` does the install with a zip on this computer (no network) in a copy of the program in a temporary
  folder: the zip's program starts with the options after the zip (`--snapshot <png> --check update`).

## CI

`.github/workflows/build.yml` builds and tests each push and pull request; `.github/workflows/release.yml` makes the
release of a tag. The jobs of build.yml:

| Job | What it does |
|---|---|
| windows | The UI text and translation table checks; `stardraft.exe` with MSVC and Ninja (Ninja Multi-Config: the files go to `build/Release/`, as with Visual Studio's generator); the check that `stardraft.pdb` matches the exe (`tools/ci/pdb_check.py`); the snapshots (also of the exe alone in a folder) and `sdl_snapshots.py --jobs 3`; then all the other targets. Its `stardraft.exe` and `stardraft.pdb` are the release's files. |
| linux | GCC: all targets, the desktop files and the thumbnailer, the tests that need no game data, the snapshots of stardraft-next, the kit demo and the editor. |
| linux-clang | Clang: all targets; the kit's tests with AddressSanitizer and UndefinedBehaviorSanitizer. |
| linux-appimage | The AppImage (Ubuntu 22.04, GCC 12). |
| macos | The app, its checks and tests, the snapshots and the disk image (signed and notarized when the secrets are there). |
| linux-appimage-run, flatpak, wine | Only on `port/kit`, `main`, tags and runs by hand (Actions > Build > Run workflow). |
| release-test-macos | Only on `port/kit`, the `port/t-*` branches and runs by hand. |

The tests with the game data (the job linux-data, on the owner's self-hosted runner: repository variable
`STARDRAFT_DATA_RUNNER`) are a workflow of their own, `.github/workflows/data.yml`: a release never waits for that
runner.

- A push that changes only docs (`docs/`, `*.md`) starts no run. `README.md`, `docs/user/CHANGELOG.md` (the UI text
  check reads them), `docs/user/images/` and `tools/preview_notes.md` (a release reads them) still start one.
- A newer push to a branch stops the older run of that branch. Runs of `main` and of tags always go to their end.
- Compiler caches: sccache on Windows (the editor has `/Z7`: the debug information is in each .obj, so sccache can keep
  it; the linker still writes `stardraft.pdb`), ccache on Linux, macOS and the AppImage. Each job keeps its cache with
  actions/cache (`sccache-windows-`, `ccache-linux-gcc-`, `ccache-linux-clang-`, `ccache-appimage-`, `ccache-macos-`
  and the run's id). A run takes the newest cache that GitHub lets it see: its own branch's, else `main`'s. Only the
  pushes to `main`, `port/kit` and the `port/ci-*` branches save a cache (GitHub keeps 10 GB for each repository and
  removes the oldest). The step "The compiler cache" shows the hits and misses. `main_sdl.cpp` is compiled again in
  each run: it has the build's time (`__DATE__`, `__TIME__`) and its commit (`build_info.h`).
- `main_sdl.cpp` is one translation unit (it includes the editor's 45,000 lines of .inc files into one anonymous
  namespace). It is not split: the parts share internal functions and state, and a split needs headers for all of
  them.

**Releases from the tested build.** A tagged commit's push builds as that version (`tools/ci/version_name.sh`:
`release.py publish` pushes `main` and the tag together, so the push run sees the tag). The job `tested` of
release.yml finds build.yml's push run of the tag's commit (`tools/ci/tested_run.py`: the run's `head_sha` is the
tag's commit, all its jobs passed, and its `build-version-windows-<commit>` and `build-version-macos-<commit>` files
name the tag's version). It waits while that run is not done. Then the job `release` downloads that run's files (their
artifact names have the commit too; the job checks that `stardraft.exe` names the commit and that the pdb matches it)
and only packs, attaches and posts (`tools/ci/release_make.ps1`). With no such run (a tag on a commit that was pushed
before the tag, a preview tag pushed after its branch, a run that failed or was stopped), release.yml builds and tests
the commit itself (build.yml as a called workflow) and releases those files. For a fast preview, push the branch and
its tag together: `git push --atomic origin port/kit v<version>-preview.<n>`.

A test of the release path: a tag `ci-test-<n>` (or `ci-test-<n>-preview`) on a commit with a passed push run makes the
same release as a draft (the version is the changelog's newest), with the macOS and Linux files and no Discord post.
Check its files, then delete it and its symbols: `gh release delete ci-test-<n> --yes --cleanup-tag` and
`gh release delete symbols-ci-test-<n> --yes`.

**Larger runners (not used).** GitHub's larger runners (more cores: 8, 16 or more vCPUs) need a paid plan with billing
on the organization; their minutes are not free, also for a public repository. To use them: Settings > Actions >
Runners > New GitHub-hosted runner (for example Windows 8 cores, Ubuntu 8 cores), then `runs-on: <the runner's name>`
in the jobs windows, linux, linux-clang and linux-appimage. The gain: the cold builds (5 to 6 minutes on 4 vCPUs) would
take about half; with a warm compiler cache the gain is small, as most of the time is then `main_sdl.cpp` alone (one
translation unit: one core) and the steps that are not builds. macOS: the `macos-15-xlarge` runner (M-series, more
cores) has the same billing.

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
   as it is). Then it waits for the tag's run of `.github/workflows/release.yml` (it waits for the build and tests
   of master's push, then releases in a few minutes) and confirms the release. `--dry-run` shows the commands only.

CI (`.github/workflows/release.yml`) checks the tag first (`release.py ci-check`: a version, its section, its pictures,
a commit on `main`). Then it takes the files that build.yml built and tested for that commit as that version (see
"CI": releases from the tested build) and makes the release "StarDraft 2 - v<version> (build <commit>)" with `StarDraft-<version>-win64.zip` (only `stardraft.exe`), marked Latest. Its text is the version's
section, then the changelog's last section (what StarDraft is), with the pictures as addresses at the tag
(`release.py notes <version>` shows the text). In Discord (`release.py discord`), each channel gets the release's name
and link, then the version's section as Discord text (`release.py discord-text <version>` shows the messages), then
each picture as a message of its own with its text, so that the notes and the pictures show in the channel (a link
alone shows neither; the owner's requirement). The symbols (`stardraft.pdb`)
go to the draft release `symbols-v<version>`, which only the repository's members see, for crash reports. For now the
release has only the Windows build. The repository variable `RELEASE_MAC` (Settings > Secrets and variables > Actions >
Variables), default off, = `on` attaches the macOS disk image `StarDraft-<version>-macos-arm64.dmg` (see "The macOS
app"); off, it is only an artifact of the run. `RELEASE_LINUX` = `on` attaches the AppImage
`StarDraft-<version>-x86_64.AppImage` in the same way. A preview tag (`v<version>-preview.<n>`, from `port/kit`) is a
pre-release with the files of all three systems, never Latest and not posted in Discord. The linux job's
`stardraft-linux.tar.gz` is never put on a release (it needs Ubuntu 24.04's glibc and has no desktop files). Check for
updates finds the release. Each release keeps its own download count: `gh api repos/stardraft-editor/stardraft/releases
--jq '.[] | "\(.tag_name)  \([.assets[].download_count] | add)"'` lists them.

**When a release run fails.**

- Before the release is there (the tag check, the build): fix it on `main`, then `release.py untag <version>` (it
  deletes the tag here and on GitHub) and `release.py publish <version> --yes` again. Nobody has the release.
- The push stops (main moved on GitHub, or no network): publish deletes the tag here and keeps the "Changelog" commit.
  `git pull --rebase`, then publish again.
- After the release is there (a Discord post did not go; the run is red): post again in that channel with Actions >
  Post screenshots to Discord (What: all for the whole post again; screenshots, when only the pictures did not go). A re-run of the run changes nothing in a release that has its zip.
- A release that people have is never moved, and only the newest version is released (CI refuses an older tag): fix
  it in a new version.

The website (`stardraft-editor/stardraft-editor.github.io`) needs no release step: its download button gets the newest
release. A change to `docs/project/ROADMAP.md` builds it again (`.github/workflows/roadmap.yml`).
