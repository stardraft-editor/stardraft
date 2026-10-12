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

`third_party/libvpx/` holds only the build configuration that `tools/gen_libvpx.py` makes for libvpx (no libvpx code).

## Documents and data

| Source | Use | Link |
|---|---|---|
| staredit.net wiki: Scenario.chk | The map format | http://staredit.net/wiki/index.php/Scenario.chk |
| EUD Editor 2 and 3 (MIT) | Default requirements and button sets, decoded once into `src/data/dat_extras.inc` | https://github.com/Buizz/EUD-Editor-3, https://github.com/Buizz/EUDEditor |
| euddraft and eudplib | Build EUD maps from the triggers; StarDraft downloads euddraft from its releases the first time a project builds | https://github.com/armoha/euddraft, https://github.com/armoha/eudplib |

StarCraft and StarCraft: Remastered are trademarks of Blizzard Entertainment. StarDraft is not affiliated with
Blizzard. StarDraft reads the game's files from the user's own installation and contains none of them.
