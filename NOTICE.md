# Third-party software and sources

StarDraft is MIT-licensed (see LICENSE). It uses the projects below. They are not copied into this repository: the
build downloads the libraries (CMakeLists.txt, FetchContent), and the documents and data are linked.

## Libraries

| Library | Use | License | Source |
|---|---|---|---|
| StormLib | Reads and writes maps (MPQ archives); its zlib | MIT | https://github.com/ladislav-zezula/StormLib |
| CascLib | Reads the game's files (CASC storage) | MIT | https://github.com/ladislav-zezula/CascLib |
| SDL3 | The window and the GPU renderer | zlib | https://github.com/libsdl-org/SDL |
| stb (stb_image, stb_vorbis) | Images and OGG Vorbis sounds | MIT or public domain | https://github.com/nothings/stb |
| libvpx | Decodes the game's WebM portraits | BSD-3-Clause | https://github.com/webmproject/libvpx |
| litehtml | The HTML view (the release notes): lays out HTML and CSS | BSD-3-Clause | https://github.com/litehtml/litehtml |
| Gumbo (in litehtml) | Parses the HTML for litehtml | Apache-2.0 | https://github.com/litehtml/litehtml/tree/master/src/gumbo |
| Dear ImGui | stardraft-next's UI (the new shell's spike) | MIT | https://github.com/ocornut/imgui |
| Noto Sans, Noto Sans KR, Noto Sans SC | The fonts (Latin, Korean, Chinese) on macOS and Linux; on Windows only Noto Sans, when Segoe UI is not there | SIL Open Font License 1.1 | https://github.com/notofonts |

`third_party/libvpx/` holds only the build configuration that `tools/gen_libvpx.py` makes for libvpx (no libvpx code).

## Documents and data

| Source | Use | Link |
|---|---|---|
| staredit.net wiki: Scenario.chk | The map format | http://staredit.net/wiki/index.php/Scenario.chk |
| EUD Editor 2 and 3 (MIT) | Default requirements and button sets, decoded once into `src/data/dat_extras.inc` | https://github.com/Buizz/EUD-Editor-3, https://github.com/Buizz/EUDEditor |
| OpenBW (bwgame.h: regions_create) | The steps of the game's pathing regions (View > Layers > Pathing, Scenario > Check pathing), written again in `src/pathing_regions.cpp` from the steps; no code copied | https://github.com/OpenBW/openbw |
| euddraft and eudplib | Build EUD maps from the triggers; StarDraft downloads euddraft from its releases the first time a project builds | https://github.com/armoha/euddraft, https://github.com/armoha/eudplib |

StarCraft and StarCraft: Remastered are trademarks of Blizzard Entertainment. StarDraft is not affiliated with
Blizzard. StarDraft reads the game's files from the user's own installation and contains none of them.
