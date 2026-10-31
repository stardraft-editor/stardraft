#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;

enum class Era : uint16_t { Badlands, Platform, Install, Ashworld, Jungle, Desert, Ice, Twilight };

// CASC base name of a tileset ("badlands", "platform", ...). StarCraft masks ERA to 3 bits.
const char* tilesetName(uint16_t era);

// One CV5 tile group (52 bytes). MTXM/TILE values are (group << 4) | subtile.
struct TileGroup {
    uint16_t type = 0;  // terrain type / ISOM link
    uint8_t buildability = 0;
    uint8_t groundHeight = 0;
    std::array<uint16_t, 4> edges{};  // left, top, right, bottom
    std::array<uint16_t, 4> extra{};  // remaining editor fields (stack connections etc.)
    std::array<uint16_t, 16> megatiles{};
};

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

// Classic (SD) tileset graphics: tile groups -> megatiles (32x32) -> minitiles (8x8, palette indexed).
struct Tileset {
    std::vector<TileGroup> groups;                     // .cv5
    std::vector<std::array<uint32_t, 16>> megatiles;   // .vx4ex: (minitile << 1) | hflip, 4x4 per megatile
    std::vector<std::array<uint8_t, 64>> minitiles;    // .vr4: 8x8 palette indices
    std::array<Rgb, 256> palette{};                    // .wpe
    std::vector<std::array<uint16_t, 16>> minitileFlags;  // .vf4: per megatile, 4x4 minitile flags

    // CV5 group flags (low byte of the field after the type).
    static constexpr uint8_t kUnbuildable = 0x80;
    // VF4 minitile flags.
    static constexpr uint16_t kWalkable = 0x1;

    bool load(const GameData& data, uint16_t era, std::string& error);
    uint16_t era = 0;  // set by load
    // Terrains of groups that the CV5's isometric fields do not give (a mod's new groups, from its maps):
    // {group, terrain, the other terrain of a border (0: none)}, as tileGroupTerrains gives them.
    std::vector<std::array<uint16_t, 3>> groupTerrains;
    // A mod's own pieces for the isometric brush (from its maps): {its left tile, its right tile (tile values), the
    // game's left tile group of the rect it is a piece for}.
    std::vector<std::array<uint16_t, 3>> groupPieces;
    // Which of a mod's pieces stand beside each other on its maps (pieceNeighborKey: a piece's left tile, its neighbor's,
    // the place: 0 under it, 1 a rect to the right and a row down, 2 to the right and a row up), and the left tiles that
    // have such a rule. The brush takes a piece that the maps have beside the pieces around it.
    std::unordered_set<uint64_t> pieceNeighbors;
    std::unordered_set<uint16_t> pieceNeighborTiles;
    static uint64_t pieceNeighborKey(uint16_t a, uint16_t b, int place) { return uint64_t(a) << 18 | uint64_t(b) << 2 | uint64_t(place & 3); }
    // A mod's own terrains in the isometric terrain list (Cosmonarchy's, from its brush sets): the ISOM keeps the
    // stand-in's shapes (a game terrain with the same border to the ground), and the brush puts the mod's pieces in
    // place of the stand-in's tiles.
    struct ModTerrain {
        uint16_t id = 0;       // the terrain list's value (kModTerrainIds and up: no game terrain has it)
        uint16_t standIn = 0;  // the game terrain whose shapes the ISOM keeps
        uint16_t ground = 0;   // the terrain it borders (its set is made for that border; a game or an earlier mod terrain)
        uint16_t groundTemplate = 0;  // the game terrain its ground is in the template (a level higher: High Dirt on Dirt's
                                      // place); 0: the ground itself
        bool fill = true;      // false: inside it the stand-in's own tiles (only its edges are the mod's)
        uint32_t fillKey = 0;  // what is inside it (its fill groups, or a game terrain's tiles; 0: not known): two mod
                               // terrains with one fill, stand-in and height look the same inside (twin grounds)
        std::string name;
        std::vector<std::array<uint16_t, 3>> pieces;  // {the game's left tile group, the mod's left tile, its right tile}
        int emptyBlocks = 0;
        size_t setPieces = 0;  // pieces' first ones are its set's own; the rest its fill and its gaps
        size_t gapPieces = 0;  // pieces' last ones: the null tile for the border's game groups its set has no piece for
        uint16_t variantOf = 0;  // a variant: that terrain's set on another ground with its ground's tiles (the brush picks it)
        bool fromMaps = false;  // made from the maps (src/mods/cmbw_auto_terrains.inc), not a hand-made row
        bool draft = false;     // not checked by a person yet (the built-in rows: all of them; the terrain rules file keeps the flag)
        bool locked = false;    // protected: the terrain rules editor changes nothing of it until it is unprotected (kept in the file)
    };
    static constexpr uint16_t kModTerrainIds = 64;
    // A mod's variants (ModTerrain::variantOf) from here on: the rows before them may grow without moving them (saved maps
    // keep the ids and the ISOM values that come from them).
    static constexpr uint16_t kModVariantIds = kModTerrainIds + 48;
    std::vector<ModTerrain> modTerrains;
    const ModTerrain* modTerrain(uint16_t id) const {
        for (const ModTerrain& m : modTerrains)
            if (m.id == id) return &m;
        return nullptr;
    }

    // Megatile index for an MTXM/TILE value, or -1 if out of range.
    int megatileFor(uint16_t tileValue) const;
    // Group for an MTXM/TILE value, or nullptr if out of range.
    const TileGroup* groupFor(uint16_t tileValue) const;
};

}  // namespace stardraft
