#pragma once

// A mod's isometric terrain rules as a file (.sdterrain): for each tileset, each mod terrain with its pieces (the mod's
// tiles for each rect of its model, the game terrain whose shapes it takes). StarDraft has its rules built in
// (src/mods/cmbw.cpp); a file the mod ships in its mpq folder (terrains.sdterrain) replaces them, and a temporary
// override (Program options > Plugins) replaces both, terrain by terrain: a terrain of the file takes the place of the
// one with its id, a new id is a new terrain. The Custom isom terrain editor (Tools > Cosmonarchy) saves these files.
//
// The file (little-endian):
//   "SDTR", u16 version (1), u16 tilesets
//   per tileset: u8 era, u32 groups, u64 fingerprint (the tileset's groups: a file made for other groups is not used),
//                u16 terrains
//   per terrain: u16 id, u8 kind, u8 flags (1 fill, 2 made from maps), u16 stand-in, u16 ground, u16 ground template,
//                u16 variant of, u32 fill key, u32 set pieces, u32 gap pieces, string name (u16 bytes, UTF-8),
//                u32 pieces, each u16 game group, u16 left tile, u16 right tile (0 and 1: the null tile)

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "stardraft/tileset.h"

namespace stardraft::terrainrules {

// The kind of a terrain: field (fill only: Dirt), path (edges and fill: Asphalt), wall (sides and tops: High Dirt).
enum class Kind : uint8_t { Unknown = 0, Field = 1, Path = 2, Wall = 3 };

struct Terrain {
    Kind kind = Kind::Unknown;
    Tileset::ModTerrain t;  // (emptyBlocks is not kept)
};
struct TilesetRules {
    uint8_t era = 0;
    uint32_t groups = 0;
    uint64_t fingerprint = 0;
    std::vector<Terrain> terrains;
};
struct Rules {
    std::vector<TilesetRules> tilesets;
};

constexpr uint16_t kVersion = 1;

// The tileset's groups as one number (their megatiles and types): a mod update that renumbers groups changes it.
uint64_t fingerprint(const Tileset& t);
// The kind of a mod terrain, from its pieces: walls have rects stacked in rows, fields have no border pieces.
Kind kindOf(const Tileset& t, const Tileset::ModTerrain& m);
// The rules a loaded tileset has now (built in, or after a file was applied).
TilesetRules rulesOf(const Tileset& t);

std::vector<uint8_t> write(const Rules& r);
bool read(const std::vector<uint8_t>& bytes, Rules& r, std::string& err);
bool save(const std::filesystem::path& file, const Rules& r, std::string& err);
bool load(const std::filesystem::path& file, Rules& r, std::string& err);

// Puts the rules for `t` (its era, its fingerprint) into it: each terrain replaces the one with its id, or is added.
// False (nothing changed) when the file has no rules for this tileset or they were made for other groups.
bool apply(Tileset& t, const Rules& r, std::string* why = nullptr);

}  // namespace stardraft::terrainrules
