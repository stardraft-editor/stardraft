#pragma once
#include "stardraft/tileset.h"
#include <array>
#include <unordered_map>

namespace stardraft {
struct MirroredTile {
    uint16_t tile = 0;
    bool exact = false, matched = false;
};
// Returns existing tile IDs from reflected artwork, without ISOM correction or CV5/VF4
// property constraints. Terrain never maps to doodad artwork. The source tileset must outlive this cache.
class TileMirrorMatcher {
public:
    explicit TileMirrorMatcher(const Tileset& ts) : ts_(ts) {}
    MirroredTile match(uint16_t tile);

private:
    struct Candidate {
        uint16_t tile;
        uint64_t hash;
        std::array<uint8_t, 192> signature;
    };
    const Tileset& ts_;
    bool ready_ = false;
    std::vector<Candidate> candidates_;
    std::unordered_map<uint16_t, MirroredTile> cache_;
};

} // namespace stardraft
