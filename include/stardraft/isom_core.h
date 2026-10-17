#pragma once

// IsomTerrain as StarDraft's isometric terrain core (docs/dev/isom-terrain-plan.md).
//
// IsomTerrain (third_party/IsomTerrain, MIT, by the author of Chkdraft) is StarEdit's own ISOM editing: its brush and
// the tiles it gives match StarEdit and ScmDraft exactly. It is used as it is (IsomApi.h, unchanged); this adapter
// gives it a map's terrain (DIM, ERA, ISOM, TILE, MTXM) and the tileset's CV5 groups, and takes the results back.
// Its terrain values are the ISOM's own solid values, the same ids as IsomTerrainTable's terrains (isom.h).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/tileset.h"

namespace stardraft {

class IsomCore {
public:
    IsomCore();
    ~IsomCore();
    IsomCore(const IsomCore&) = delete;
    IsomCore& operator=(const IsomCore&) = delete;

    // Takes the map's terrain and the tables of its tileset (made from `tileset`'s CV5 groups once per tileset). False
    // with `error` when the map has no full ISOM section or the tileset no groups.
    bool load(const Chk& chk, const Tileset& tileset, std::string& error);

    // StarEdit's brush: `terrain` (an ISOM solid value) on extent x extent diamonds around the diamond (x, y) of the
    // rect grid (x + y even; an even extent grows down and left, as StarEdit's does). The diamonds around it change
    // as StarEdit changes them. False when the terrain is not a brush of the tileset or (x, y) is no diamond.
    bool paint(int x, int y, uint16_t terrain, int extent = 1);

    // The tiles (TILE and MTXM) of the diamonds that changed since the last update, as StarEdit picks them.
    void updateTiles();

    // Writes ISOM, TILE and MTXM to `chk` (ISOM without the core's editing flags).
    void store(Chk& chk) const;

    // The ISOM solid values that are brushes of the loaded tileset (StarEdit's brush list, in its order).
    std::vector<uint16_t> brushes() const;

    bool loaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace stardraft
