# Third-party software and sources

StarDraft is MIT-licensed (see LICENSE). It uses the projects below. Except for IsomTerrain, they are not copied into
this repository: the build downloads the libraries (CMakeLists.txt, FetchContent), and the documents and data are linked.

## Libraries

| Library | Use | License | Source |
|---|---|---|---|
| StormLib | Reads and writes maps (MPQ archives); its zlib | MIT | https://github.com/ladislav-zezula/StormLib |
| CascLib | Reads the game's files (CASC storage) | MIT | https://github.com/ladislav-zezula/CascLib |
| SDL3 | The window and the GPU renderer | zlib | https://github.com/libsdl-org/SDL |
| stb (stb_image, stb_vorbis) | Images and OGG Vorbis sounds | MIT or public domain | https://github.com/nothings/stb |
| libvpx | Decodes the game's WebM portraits | BSD-3-Clause | https://github.com/webmproject/libvpx |
| libogg, libvorbis | Encode the music tracks that are WAV or MP3 files to Ogg Vorbis (Build; `src/media.cpp`) | BSD-3-Clause, Copyright (c) Xiph.Org Foundation | https://github.com/xiph/ogg, https://github.com/xiph/vorbis |
| Lua 5.1.5 | Runs EUD Editor 3's Lua macros in File > Import a project, in a sandbox (`src/import_ee3_macros.cpp`; `lstrlib.c` changed at build time, CMakeLists.txt) | MIT, Copyright (c) 1994-2012 Lua.org, PUC-Rio | https://www.lua.org |
| dr_wav, dr_mp3 (dr_libs) | Decode the music tracks that are WAV or MP3 files | Public domain (Unlicense) or MIT No Attribution, David Reid | https://github.com/mackron/dr_libs |
| litehtml | The HTML view (the release notes): lays out HTML and CSS | BSD-3-Clause | https://github.com/litehtml/litehtml |
| Gumbo (in litehtml) | Parses the HTML for litehtml | Apache-2.0 | https://github.com/litehtml/litehtml/tree/master/src/gumbo |
| Map Simulation | The simulation bar's mining and rush distance (Tools > Simulate mining, Simulate rush distance; `tools/map_lab/`) | MIT, Copyright (c) 2026 Map Editor contributors | Pull request 260 (`third_party/map-simulation/README.md`) |
| IsomTerrain | The isometric terrain core: StarEdit's brush and tiles (`src/isom_core.cpp`) | MIT, Copyright (c) 2023 Justin F | https://github.com/TheNitesWhoSay/IsomTerrain |
| Dear ImGui | stardraft-next's UI (the new shell's spike) | MIT | https://github.com/ocornut/imgui |
| Noto Sans, Noto Sans KR, Noto Sans SC | The fonts (Latin, Korean, Chinese) on macOS and Linux; on Windows only Noto Sans, when Segoe UI is not there | SIL Open Font License 1.1 | https://github.com/notofonts |

`third_party/IsomTerrain/` is IsomTerrain at one commit: its `IsomApi.h`, LICENSE, README and the test maps that its
reference tests use, unchanged, and StarDraft's stand-ins for the two Chkdraft headers it includes
(`third_party/IsomTerrain/STARDRAFT.md`).

`third_party/map-simulation/` is the Map Simulation engine as pull request 260 gave it, unchanged (its LICENSE, README,
COMPATIBILITY.md and SOURCE.json, which fingerprints its files: `python -I third_party/map-simulation/verify_source.py`).
Its README says that its authors wrote it on their own, with OpenBW only as a behavior reference, and that it has no
OpenBW or BWAPI code. `tools/map_lab/native_world.cpp` (MIT, its LICENSE) loads a map into it; `tools/map_lab/bar_sim.cpp`
is StarDraft's own code.

`third_party/ogg/` holds only libogg's `config_types.h`, which its own configure step makes (StarDraft's file, no libogg code).

`third_party/libvpx/` holds only the build configuration that `tools/gen_libvpx.py` makes for libvpx (no libvpx code).

## Documents and data

| Source | Use | Link |
|---|---|---|
| staredit.net wiki: Scenario.chk | The map format | http://staredit.net/wiki/index.php/Scenario.chk |
| OpenBW (bwgame.h: regions_create; its repository gives no license) | Only the facts of how the game makes its pathing regions (the steps, the limits and the game's region values) for View > Layers > Pathing and Scenario > Check pathing. `src/pathing_regions.cpp` is StarDraft's own code (MIT), with its own structure; no OpenBW code is copied | https://github.com/OpenBW/openbw |
| EUD Editor 2 and 3 (MIT) | Default requirements and button sets, decoded once into `src/data/dat_extras.inc`; the .dat fields, their names and their places in memory (`src/data/dat_schema.inc`, the help pages) | https://github.com/Buizz/EUD-Editor-3, https://github.com/Buizz/EUDEditor |
| EUD Editor 3 (MIT, Copyright (c) 2019 LeeJungHun) | File > Import a project: its TriggerEditor library (`third_party/eud-editor-3/TriggerEditor`, unchanged; an imported project gets the files its scripts import; its dotTool.eps is also in the code that Build makes for Draw dot image, `src/media.cpp`), its BGM data, dot data and dot colors (the music tracks and dot images, `src/media.cpp`), its key names for texts and the names of its Lua macros (`third_party/eud-editor-3`, built into `src/data/eud_editor3.inc` by `tools/gen_eud_editor3.py`); the .e3s and .e2s formats, read from its source | https://github.com/Buizz/EUD-Editor-3 |
| EUD Editor 3 (MIT) | File > Import a project: the names of its classic triggers' actions, conditions and functions (`third_party/eud-editor-3/classic/epsFunctions.txt`, unchanged; built into `src/data/ee3_classic_functions.inc` by `tools/gen_ee3_classic.py`); its classic trigger and block script code generators, rewritten in C++ (`src/import_ee3_classic.cpp`, `src/import_ee3_blocks.cpp`); its Lua macro files and .dat field addresses (`third_party/eud-editor-3/Lua`, `third_party/eud-editor-3/Offset.txt`, unchanged; built into `src/data/ee3_lua.inc` by `tools/gen_ee3_lua.py`) and its macro functions, rewritten in C++ (`src/import_ee3_macros.cpp`) | https://github.com/Buizz/EUD-Editor-3 |
| EUD Editor 2 (MIT, Copyright (c) 2018 LeeJungHun) | File > Import a project: its trigger editor's actions and conditions, functions, .dat field tables, memory offsets and its punitloop and customText modules (`third_party/eud-editor-2/`, unchanged, from its repository and its 0.17.8.2 release; built into `src/data/ee2_triggers.inc` by `tools/gen_ee2_triggers.py`); its trigger editor's code generator, rewritten in C++ (`src/import_ee2_triggers.cpp`) | https://github.com/Buizz/EUDEditor |
| The EUD Book | The .dat fields that a map cannot change (the offset table), and the units of the values on the help pages (`docs/help`, written in StarDraft 2's own words) | https://armoha.github.io/eud-book/ || euddraft and eudplib | Build EUD maps from the triggers; StarDraft downloads euddraft from its releases the first time a project builds | https://github.com/armoha/euddraft, https://github.com/armoha/eudplib |

StarCraft and StarCraft: Remastered are trademarks of Blizzard Entertainment. StarDraft is not affiliated with
Blizzard. StarDraft reads the game's files from the user's own installation and contains none of them.
