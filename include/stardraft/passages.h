#pragma once
// Passages files (.scpass): ramps and bridges that a mod's tilesets add (Tools > Cosmonarchy > Ramp and bridge mapper).
// A mapping takes one of the game's kits as its template (a ramp block of a tileset in its 8 facings, or a tileset's
// bridge kit in its two orientations): the template gives the shape (the pieces, the steps, where each piece goes on
// the ISOM corners); the mapping gives each template tile the mod's tile, and the ISOM terrains it fits on. The Ramp
// brush and the Bridge tool then use each mapping as a kit of its own (src/ramps.cpp, src/bridges.cpp).
//
// The file is JSON: {"format": "StarCraft passages", "version": 1, "ramps": [...], "bridges": [...]}. A ramp:
// {"name", "tileset" (era 0..7), "template" (the block's first doodad), "terrains": [{"ground", "cliff"}...],
// "tiles": [[template tile, mod tile]...]}. A bridge: {"name", "tileset", "template" (the tileset of the template kit),
// "terrains": [{"coastA", "water", "coastB"}...], "tiles": [...]}. Terrain names are the map's (Editor::terrainName).
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace stardraft::passages {

struct RampTerrains {
    std::string ground, cliff;
};
struct BridgeTerrains {
    std::string coastA, water, coastB;  // coast A: under the bridge's start cap; coast B: under its far cap
};

// A template tile (MTXM value) -> the mod's tile. A template tile with none: the mapping does not have it yet (a
// facing or an orientation with such a tile is not used).
using TileMap = std::map<uint16_t, uint16_t>;

struct RampMapping {
    std::string name;
    int era = 0;       // the tileset (ERA value & 7)
    int block = -1;    // the template: the first doodad of a ramp block of the era (rampBlocks)
    std::vector<RampTerrains> terrains;
    TileMap tiles;
};
struct BridgeMapping {
    std::string name;
    int era = 0;
    int templateEra = -1;  // the tileset whose bridge kit is the template (bridgesAvailable)
    std::vector<BridgeTerrains> terrains;
    TileMap tiles;
};
struct Passages {
    std::vector<RampMapping> ramps;
    std::vector<BridgeMapping> bridges;
};

constexpr const char* kExtension = ".scpass";

std::string toText(const Passages& p);
bool parse(const std::string& text, Passages& out, std::string& error);
bool load(const std::filesystem::path& file, Passages& out, std::string& error);
bool save(const std::filesystem::path& file, const Passages& p, std::string& error);

// The passages the tools use (none: the game's kits only). revision() changes on each use(), so the kits made from
// them are made again.
void use(const Passages& p);
const Passages& inUse();
int revision();

// A template tile's mod tile (true), or none.
bool mapTile(const TileMap& m, uint16_t from, uint16_t& to);

}  // namespace stardraft::passages
