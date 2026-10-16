#pragma once
// A map's picture without a window or a GPU: its terrain at one block of pixels a tile (the minimap's colors), the units
// as player-colored dots and the start locations as frames. The macOS Quick Look extensions (Finder's icons and the
// space bar's preview, apps/quicklook/*.mm) and the editor's Spotlight entries use it; it is plain C++ on every system
// (the editor's --check preview draws it on Windows and Linux too).
//
// The terrain's colors come from the game's tilesets, which only the editor reads (the extensions are in a sandbox and
// have no game data): the editor writes each tileset's table of tile colors (tile-colors-<era>.bin) when a map of that
// tileset opens. With no table, the terrain is drawn in the tileset's base color, a little lighter or darker for each
// tile group, so the map's shape still shows.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace stardraft::preview {

struct MapInfo {
    std::string name, description;  // UTF-8, without the game's color codes
    int width = 0, height = 0;      // in tiles
    uint16_t era = 0;               // the tileset (ERA & 7)
    int players = 0;                // human and computer slots of players 1 to 8 (OWNR)
    int startLocations = 0;
};

// The CV5 tile value (group << 4 | subtile) -> 0xRRGGBB: the average color of the tile's megatile, as the minimap draws it.
std::vector<uint32_t> tileColors(const Tileset& ts);

// The table's file in `dir`: tile-colors-<era>.bin. Written to a new file that then takes the old one's place.
std::filesystem::path tileColorsFile(const std::filesystem::path& dir, uint16_t era);
bool writeTileColors(const std::filesystem::path& file, const std::vector<uint32_t>& colors, std::string& error);
bool readTileColors(const std::filesystem::path& file, std::vector<uint32_t>& colors);

// The folder for the tables: <the user's home>/Library/Application Support/StarDraft/quicklook on macOS (the real home,
// also in a sandbox), else empty.
std::filesystem::path tileColorsFolder();

// A map's text in UTF-8: the game's color and format codes go, line ends stay; UTF-8, else CP949 or Windows-1252.
std::string plainText(const std::string& gameText);

MapInfo readInfo(const Chk& chk);

// The picture, at most `maxSide` pixels on its long side (an integer number of pixels for each tile, at least one).
// `colors`: a tile color table of the map's tileset, or null (the base color). RGBA, opaque.
Image render(const Chk& chk, const std::vector<uint32_t>* colors, int maxSide);

// A map file (.scm, .scx): its scenario.chk, its info and its picture; the table from `colorsDir` when it has one
// (`usedTable` says whether it did). False, with the reason, when the file is no map.
bool renderFile(const std::filesystem::path& map, const std::filesystem::path& colorsDir, int maxSide, Image& out,
                MapInfo& info, bool* usedTable, std::string& error);

}  // namespace stardraft::preview
