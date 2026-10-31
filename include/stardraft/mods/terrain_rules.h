#pragma once

// A mod's isometric terrain rules as a file (.isom): for each tileset, each mod terrain with its pieces (the mod's
// tiles for each rect of its template, the game border whose shapes it takes). StarDraft has its rules built in
// (src/mods/cmbw.cpp); a file the mod ships in its mpq folder (terrains.isom) replaces them, and a temporary
// override (Program options > Plugins) replaces both, terrain by terrain: a terrain of the file takes the place of the
// one with its id, a new id is a new terrain. The Custom isom terrain editor (Tools > Cosmonarchy) saves these files.
//
// The template: a mod terrain stands in for a game terrain (standIn) on a game ground (its ground, or the game terrain
// its mod ground stands for: groundTemplate); the game border of the two has a fixed set of rects (its tile groups),
// the same in every tileset for a border of its family (templateRects), each with a role (edge N, corner NE top...).
// A terrain is complete when every rect of its template has a piece with two tiles: the mod's own, or the game's own
// tiles of that rect (borrowed: the set paints like the game there until the mod's tiles come). A NULL half is a hole.
//
// The file (little-endian):
//   "SDTR", u16 version (2), u16 tilesets
//   per tileset: u8 era, u32 groups, u64 fingerprint (the tileset's groups: a file made for other groups is not used),
//                u16 terrains
//   per terrain: u16 id, u8 flags (1 fill, 2 made from maps, 4 draft), u16 stand-in, u16 ground, u16 ground template,
//                u16 variant of, u32 fill key, u32 set pieces, u32 gap pieces, string name (u16 bytes, UTF-8),
//                u32 pieces, each u16 game group, u16 left tile, u16 right tile (0 and 1: the null tile)
//   (version 1 had a u8 kind before the flags; it is read and ignored)

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "stardraft/tileset.h"

namespace stardraft::terrainrules {

struct Terrain {
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

constexpr uint16_t kVersion = 2;

// The families of the game's borders, by their rects (the same in every tileset): a patch (14 rects: Rocky Ground on
// Dirt), a blend (24, texture on texture: Grass on Dirt), a pavement (24, flat, one stack link id: Asphalt on Dirt), a
// wall (36: Structure on Asphalt, Temple, shorelines) and a cliff (44 and up: High Dirt on Dirt).
enum class Family : uint8_t { Other = 0, Patch = 1, Field = 2, Pavement = 3, Wall = 4, Cliff = 5 };
const char* familyName(Family f);

// A rect of a game border's template: its tile group (the left tile's; the right tile's is the next), its role from
// the terrains at its corners (edge N, corner NE, inner corner S; a cliff's rows: top, middle, bottom) and the parts of
// that: dir 0..7 from N round to NW (-1: none), fill (how many of its 6 corner points are the terrain's: 0 to 6), row
// (0: none, 1 top, 2 middle, 3 bottom).
struct TemplateRect {
    uint16_t group = 0;
    std::string role;
    int dir = -1;
    float fill = 0;
    int row = 0;
};
// The rects of a game border (ground a, terrain b on it): the tileset's groups the game uses for rects with both.
std::vector<TemplateRect> templateRects(const Tileset& t, uint16_t ground, uint16_t terrain);
Family familyOf(const std::vector<TemplateRect>& rects, const Tileset& t);
// The template border of a mod terrain: {the game ground, the game terrain} its stand-in and ground stand for.
std::pair<uint16_t, uint16_t> templateOf(const Tileset& t, const Tileset::ModTerrain& m);

// A piece's state: both tiles the mod's own; borrowed (the game's own tiles of its rect: left in the rect's group, right
// in the next); a hole (both halves NULL); partial (one half NULL).
enum class PieceState : uint8_t { Own = 0, Borrowed = 1, Null = 2, Partial = 3 };
PieceState stateOf(const std::array<uint16_t, 3>& piece);
inline bool nullHalf(uint16_t v) { return v <= 1; }
// The game's own tile of a group (its first subtile that is a tile); 0: none.
uint16_t gameTile(const Tileset& t, uint32_t group);

// The terrain in the order the engine wants: its pieces with a tile first (the set, partial ones among them), then its
// fill, then the holes (the gaps); a template rect with no piece gets a NULL one, a NULL piece for a rect outside the
// template goes. Pieces with tiles for rects outside the template stay in the set.
void normalize(const Tileset& t, Tileset::ModTerrain& m);
// How many of the template's rects have a complete piece (own or borrowed), and how many rects there are.
std::pair<int, int> completeness(const Tileset& t, const Tileset::ModTerrain& m);

// The tileset's groups as one number (their megatiles and types): a mod update that renumbers groups changes it.
uint64_t fingerprint(const Tileset& t);
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
