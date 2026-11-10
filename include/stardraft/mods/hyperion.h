#pragma once

// Hyperion (https://github.com/Mad-Man-Dan/hyperion-public): a 64-bit Brood War engine. Its own map format, .hxm, is a
// zip of map.json (the map as a JSON document), layers/<name>.bin (the per-tile layers) and files/<path> (the map's
// other files). It holds what a scenario.chk holds, and maps past the classic limits: sides up to 65,535 tiles and up
// to 64 player slots (the engine plays 1,024 and 64). Such a map compiles to a CHK of VER 1206 (1064 without Brood
// War) with Hyperion's sections HXPL, HXRG and HXTE.
//
// readHxm is Hyperion's own compile (hxm to CHK, port_hxm.cpp), and gives the same bytes as `Hyperion.exe
// --hxm-compile`. writeHxm is the inverse (from_chk): the CHK's sections as the document, with what the document has
// no field for kept in `compat` and `sections`, so that the map converts back to the same CHK.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "stardraft/map_file.h"

namespace stardraft::hyperion {

// A path that names a Hyperion map: the extension .hxm, in any case.
bool isHxm(const std::filesystem::path& path);

// A map read from .hxm: its scenario.chk and its other files (MPQ names: backslashes). `stardraft`: the document's
// "editor": {"stardraft": ...} object as JSON text (empty: none), which writeHxm takes back.
struct HxmMap {
    Bytes chk;
    std::vector<ArchiveFile> files;
    std::string stardraft;
};

// The colours a player at index 8 and up can have (HXPL's colour byte): StarCraft's and Remastered's 0 to 22, then
// Hyperion's own from 23 (65 in all). colorName: its name for a person ("Electric blue"); colorRgb: 0xRRGGBB (Hyperion's
// own: near, as Hyperion does not publish them). defaultColor: the colour of player index p that names none (its own up
// to 11; from 12, the next of Hyperion's).
int colorCount();
std::string colorName(int id);
uint32_t colorRgb(int id);
int defaultColor(int p);

// The .hxm file's bytes (a zip) as a map. False with `error` when it does not read or does not compile.
bool readHxm(std::span<const uint8_t> zip, HxmMap& out, std::string& error);
// A .hxm file, or the unpacked form: a folder with map.json, layers\ and files\.
bool readHxmFile(const std::filesystem::path& path, HxmMap& out, std::string& error);
// The compile alone: map.json's text and its layers (by name) to the CHK. `files` are not needed.
bool compileHxm(const std::string& mapJson, const std::vector<std::pair<std::string, Bytes>>& layers, Bytes& chk,
                std::string& error);

// The map as a .hxm file's bytes (a zip, deflated). `map.stardraft` goes under "editor" when it is not empty. `lost`
// (when given) gets what of the map the document cannot hold and so does not come back the same: trigger players past
// 2147483647, which EUD maps use (the compile reads them as 2147483647).
bool writeHxm(const HxmMap& map, Bytes& zip, std::string& error, std::vector<std::string>* lost = nullptr);
// writeHxm to a file (through a temporary file that then replaces `path`).
bool writeHxmFile(const std::filesystem::path& path, const HxmMap& map, std::string& error,
                  std::vector<std::string>* lost = nullptr);
// The map.json document that writeHxm writes for a CHK, and the layers beside it (by name).
bool chkToDocument(std::span<const uint8_t> chk, const std::string& stardraft, std::string& mapJson,
                   std::vector<std::pair<std::string, Bytes>>& layers, std::string& error,
                   std::vector<std::string>* lost = nullptr);

}  // namespace stardraft::hyperion
