#pragma once

// Terrain stamps: named sets of tiles that the user keeps (one file each, in the user's own folder: not in a map), to
// place again on any map of the same tileset. The editor's Terrain mode makes them (a selection of tiles, Save as
// terrain stamp) and places them (the Stamps list).
//
// The file (<folder>/<tileset>/<file name>.sdstamp; <tileset> is tilesetName(era)) is JSON, version 1:
//
//   {"format": "StarDraft terrain stamp", "version": 1, "name": "Jungle_Bridges_Left", "tileset": "jungle", "era": 4,
//    "created": 1792310400, "width": 3, "height": 2, "tiles": [208, 209, 0, 224, 225, 226], "mask": "110111"}
//
// tiles: width x height MTXM tile values (group << 4 | tile), row by row. mask: one character for each of them, "1"
// for a tile that is in the stamp, "0" for one that is not (a stamp need not be a rectangle; its value there is 0 and
// placing the stamp leaves the map's tile there as it is). created: seconds since 1970-01-01 UTC. A reader takes a file
// of a newer version when these fields are there, and refuses one without them.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace stardraft {

struct TerrainStamp {
    std::string name;             // UTF-8 (terrainStampNameOk)
    uint16_t era = 0;             // the tileset (0 to 7)
    int width = 0, height = 0;    // tiles
    std::vector<uint16_t> tiles;  // width x height, row by row
    std::vector<uint8_t> mask;    // width x height: 1 in the stamp, 0 not
    int64_t created = 0;          // seconds since 1970 (UTC)
    std::filesystem::path file;   // where it was read from or written to

    bool has(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height && mask[size_t(y) * size_t(width) + size_t(x)]; }
    int count() const;  // the tiles in it
};

constexpr int kTerrainStampVersion = 1;
constexpr size_t kTerrainStampNameMax = 64;       // characters
constexpr size_t kTerrainStampFileNameMax = 120;  // the file name's characters (terrainStampFileName), without ".sdstamp"
constexpr int kTerrainStampSizeMax = 256;    // tiles across and down (a map's largest side)

// A name is 1 to kTerrainStampNameMax characters: letters, digits, spaces, "_" and "-", and no space at the start or
// the end; its file name is kTerrainStampFileNameMax characters at most. `why` tells what is wrong.
bool terrainStampNameOk(const std::string& name, std::string& why);
// The file's name for a stamp name (without the folder and ".sdstamp"): the name's ASCII letters, digits, "_" and "-"
// as they are, each other byte as %XX (a space too), and a name that Windows keeps for a device (CON, NUL, COM1...)
// with its first letter as %XX. Two names that differ only in case (Windows and macOS do not tell them apart) get
// the same file; terrainStampFind finds a stamp by its name in the same way.
std::string terrainStampFileName(const std::string& name);
// The stamp's file in `folder`: folder/<tileset>/<file name>.sdstamp.
std::filesystem::path terrainStampPath(const std::filesystem::path& folder, uint16_t era, const std::string& name);

std::string terrainStampText(const TerrainStamp& s);  // the file's text
bool readTerrainStamp(const std::filesystem::path& file, TerrainStamp& out, std::string& error);
// Writes the stamp to terrainStampPath(folder, s.era, s.name) (makes the folders), and sets s.file.
bool writeTerrainStamp(const std::filesystem::path& folder, TerrainStamp& s, std::string& error);
// Every stamp of tileset `era` in `folder` (files that cannot be read are left out).
std::vector<TerrainStamp> terrainStamps(const std::filesystem::path& folder, uint16_t era);
// Two names that are the same stamp (the same file name: case does not count).
bool terrainStampSameName(const std::string& a, const std::string& b);

// The groups of a name: its parts between "_" ("Jungle_Bridges_Left": Jungle, Bridges, Left). Empty parts are left
// out; a name with no "_" is one part.
std::vector<std::string> terrainStampNameParts(const std::string& name);

}  // namespace stardraft
