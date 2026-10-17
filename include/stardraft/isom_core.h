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
    // as StarEdit changes them, and so do their tiles. False when the terrain is not a brush of the tileset or (x, y)
    // is no diamond.
    bool paint(int x, int y, uint16_t terrain, int extent = 1);

    // `terrain` on any set of diamonds (x + y even; others are skipped), the way StarEdit's brush paints its own: each
    // is set to the terrain, then the diamonds around them change as StarEdit changes them. For StarDraft's brushes that
    // StarEdit lacks (square, circle, an other height), fill and replace. False when none could be painted.
    bool paintDiamonds(const std::vector<std::pair<int, int>>& diamonds, uint16_t terrain);

    // The tiles (TILE and MTXM) of the diamonds that changed, as StarEdit picks them. paint and paintDiamonds do it
    // themselves (each starts a new changed area); this is for a change made otherwise.
    void updateTiles();

    // The tiles of the rects marked in `rects` (per ISOM rect, row-major: (width / 2 + 1) x (height + 1); empty: all)
    // made again from the ISOM as StarEdit makes them, down the cliff stacks under them. Rects that no tile group fits
    // keep their tiles and are marked in `missed` (when given). For the old engine's edits and tests.
    void retile(const std::vector<uint8_t>& rects, std::vector<uint8_t>* missed = nullptr);

    // StarEdit's new map: every diamond `fill` (an ISOM solid value), and all the tiles from it. False when `fill` is
    // no brush of the tileset.
    bool fill(uint16_t fill);

    // ScmDraft's resize (StarEdit's): the map becomes width x height tiles, the old terrain moved by (xOffset, yOffset)
    // tiles (xOffset even: whole rects), the new area filled with `fill` (an ISOM solid value) and the edges between
    // them made as ScmDraft makes them; the tiles that stay on the map keep theirs. Same tileset only. False when
    // `fill` is no brush of the tileset.
    bool resize(uint16_t width, uint16_t height, int xOffset, int yOffset, uint16_t fill);

    // Writes DIM, ISOM, TILE and MTXM to `chk` (ISOM without the core's editing flags).
    void store(Chk& chk) const;

    // Rects that the paints since load left without a tile: their ISOM has no tile group in the tileset, so StarEdit's rule
    // gives them the null tile (0). StarEdit makes these too, where a map's ISOM is not as StarEdit would make it; the
    // editor then repairs the edit (Editor::applyCore).
    int untileable() const;

    // The ISOM solid values that are brushes of the loaded tileset (StarEdit's brush list, in its order).
    std::vector<uint16_t> brushes() const;

    bool loaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The terrain core's tables for a tileset (made from its CV5 once), as questions about rects, for StarDraft's own
// terrain engine (isom.h) in place of tables learned from maps. A rect (2 x 1 tiles, rect-grid point (rx, ry)) lies
// between two diamonds; `parity` is (rx + ry) & 1, `left` and `right` are their shapes (ISOM values): even parity, the
// diamonds (rx, ry) and (rx + 1, ry + 1); odd parity, (rx, ry + 1) and (rx + 1, ry). Its ISOM slots follow from those
// (with StarEdit's edge ids), and so do the tiles StarEdit picks for it.
class IsomTiles {
public:
    // The tables of `tileset` (kept for each tileset and CV5); nullptr when it has no groups.
    static const IsomTiles* forTileset(const Tileset& tileset);

    // The tile groups StarEdit picks from for the rect's left tile (its right tile: the next group), in StarEdit's
    // order (the first unless the stack above asks for another). Empty: no group fits (StarEdit gives the null tile).
    const std::vector<uint16_t>& groups(int parity, uint16_t left, uint16_t right) const;
    // The rects whose groups hold `group` (a left tile's group).
    struct Rect {
        uint8_t parity;
        uint16_t left, right;
    };
    const std::vector<Rect>& rectsOf(uint16_t group) const;
    // CV5's stack connections of a group (0: none): a group sits under another when its top is the other's bottom.
    uint16_t stackTop(uint16_t group) const;
    uint16_t stackBottom(uint16_t group) const;
    // Can these two rects (their left tiles' groups) sit side by side? Neighboring rects share a diamond (the left
    // rect's right one is the right rect's left one): some rect of each with that diamond's shape in common.
    bool beside(uint16_t leftGroup, uint16_t rightGroup) const;
    // ISOM values (shapes) are below this.
    uint16_t shapeCount() const;

    struct Data;
    explicit IsomTiles(std::unique_ptr<Data> data);
    ~IsomTiles();

private:
    std::unique_ptr<Data> d_;
};

}  // namespace stardraft
