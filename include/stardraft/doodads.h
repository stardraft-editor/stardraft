#pragma once

// Doodads: tile groups in the tileset (CV5 index >= 1024) placed into MTXM, recorded in DD2,
// plus an overlay sprite in THG2 for doodads that have one.

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;
struct Tileset;

// Each tile group's piece (terrain type, edges, ground height): groups of the same piece are variants of one tile
// (cliff pieces that differ only in how they stack), and a doodad fits on any of them.
struct DoodadGroundInfo {
    std::vector<uint64_t> piece;
};

// THG2 entry (10 bytes).
namespace SpriteFlag {
constexpr uint16_t DrawAsSprite = 0x1000, Disabled = 0x8000;
}
struct MapSprite {
    uint16_t id = 0;  // sprites.dat id if DrawAsSprite, else units.dat id
    uint16_t x = 0, y = 0;
    uint8_t owner = 0, unused = 0;
    uint16_t flags = 0;
};

struct DoodadType {
    uint16_t id = 0;             // DD2 number / dddata.bin index
    uint16_t firstGroup = 0;     // CV5 group of the top row; row r uses firstGroup + r
    uint16_t width = 0, height = 0;  // tiles
    uint16_t overlaySprite = 0;  // sprites.dat id drawn on top (0 = none); the units.dat id when overlayUnit
    bool overlayUnit = false;    // the overlay is a unit (Installation: the doors and the traps); THG2 has it without DrawAsSprite
    // The overlay's flags in THG2.
    uint16_t overlayFlags() const { return overlayUnit ? uint16_t(0) : SpriteFlag::DrawAsSprite; }
    // Whether a map sprite is this doodad's overlay, by its id and kind (the place is the caller's to compare).
    bool overlayIs(const MapSprite& s) const {
        return overlaySprite && s.id == overlaySprite && (s.flags & SpriteFlag::DrawAsSprite) == overlayFlags();
    }
    // The overlay's id and kind as one number, as mapSpriteKey gives a map sprite's.
    uint32_t overlayKey() const { return overlaySprite | (overlayUnit ? 0x10000u : 0u); }
    uint16_t groupString = 0;    // stat_txt string id (1-based: stat_txt.tbl's first string is 1) of the editor group
    std::vector<uint16_t> required;  // width*height tile groups required underneath (0 = any); dddata.bin rows are width long
    bool inPalette = false;          // has an editor group and tiles (placeholder ids have neither)
    std::shared_ptr<const DoodadGroundInfo> ground;  // the tileset's groups (shared by all types)
    // Whether any cell needs a tile group: a doodad that needs none could go on any terrain by dddata.bin, so nothing
    // tells where it belongs (Decorate leaves it out unless it is chosen).
    bool needsTiles() const {
        for (uint16_t g : required)
            if (g) return true;
        return false;
    }
};

class DoodadTypes {
public:
    // Builds the list from the tileset's CV5 groups and TileSet\<name>\dddata.bin.
    bool load(const GameData& data, const Tileset& tileset, uint16_t era, std::string& error);
    const std::vector<DoodadType>& all() const { return types_; }
    const DoodadType* find(uint16_t id) const;

private:
    std::vector<DoodadType> types_;
    std::vector<uint32_t> byId_;  // a doodad id's index in types_ (kNone: no doodad of that id), for find
    static constexpr uint32_t kNone = ~0u;
};

// DD2 entry (8 bytes).
struct Doodad {
    uint16_t id = 0, x = 0, y = 0;  // center, pixels
    uint8_t owner = 0;
    uint8_t disabled = 0;
    bool noOverlay = false;  // (not in DD2; map layers, Editor::layerDoodads: its overlay sprite was removed, and stays out)
};

// A map sprite's id and kind as one number (a unit's: + 0x10000).
inline uint32_t mapSpriteKey(const MapSprite& s) { return s.id | ((s.flags & SpriteFlag::DrawAsSprite) ? 0u : 0x10000u); }

std::vector<Doodad> readDoodads(const Chk& chk);
void writeDoodads(Chk& chk, const std::vector<Doodad>& doodads);
std::vector<MapSprite> readSprites(const Chk& chk);
void writeSprites(Chk& chk, const std::vector<MapSprite>& sprites);

// Whether `type` fits with its top-left tile at (tileX, tileY): on the map and, with
// `checkTerrain`, on the terrain dddata.bin asks for. Sets `reason` when it does not.
bool doodadFits(const Chk& chk, const DoodadType& type, int tileX, int tileY, bool checkTerrain, std::string* reason = nullptr);

// Place a doodad with its top-left tile at (tileX, tileY): writes its tiles into MTXM (only where
// the doodad has a megatile), appends a DD2 entry and, if it has one, its overlay sprite in THG2.
// Refuses (returns false, sets `reason`) when doodadFits() fails.
bool placeDoodad(Chk& chk, const DoodadType& type, const Tileset& tileset, int tileX, int tileY, uint8_t owner,
                 bool checkTerrain, std::string* reason = nullptr);

}  // namespace stardraft
