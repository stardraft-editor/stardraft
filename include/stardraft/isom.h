#pragma once

// Isometric terrain (ISOM) editing.
//
// Model (derived from the map corpus):
// * ISOM is a grid of 2x1-tile rects, (width/2 + 1) x (height + 1), 4 u16 slots each
//   (left, top, right, bottom). Each rect is split diagonally between two diamonds.
// * Diamond centers sit on rect-grid points (x, y) with x + y even. A diamond's value
//   (bits 4-14 of its slots) is a "shape": the terrain at its four corners
//   (left, top, right, bottom), which lie on grid points with x + y odd and are shared
//   with neighboring diamonds.
// * Shapes below `firstTransition` are solid terrains. Above it, every linked terrain pair
//   (A, B) has 14 consecutive shapes covering the corner patterns that mix A and B.
//   A diamond can only mix two terrains that are linked; linked terrains form a tree.
// * Slot bits 1-3 are positional edge ids; StarEdit writes them only for diamonds it
//   updates (never-touched diamonds keep the initial `terrain << 4`).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class IsomTerrainTable {
public:
    struct Pair {
        uint16_t firstShape, a, b;
    };

    // Table for a tileset (all eight are known).
    static const IsomTerrainTable* forEra(uint16_t era);

    IsomTerrainTable(uint16_t firstTransition, std::vector<Pair> pairs, std::vector<const char*> names = {});

    // A brush terrain: a solid id with a name (some ids below the first transition are unused).
    bool isTerrain(uint16_t t) const { return t >= 1 && t <= names_.size() && names_[t - 1][0]; }
    // Brush terrains (ids 1..n) and their StarEdit names; ids without a name are not brushes.
    uint16_t terrainCount() const { return uint16_t(names_.size()); }
    const char* terrainName(uint16_t t) const { return t >= 1 && t <= names_.size() ? names_[t - 1] : ""; }
    bool linked(uint16_t a, uint16_t b) const;
    // Corner terrains (left, top, right, bottom) of a shape; all zero if unknown.
    std::array<uint16_t, 4> corners(uint16_t shape) const;
    // Shape for four corner terrains, or 0 if the combination is not representable.
    uint16_t shapeFor(const std::array<uint16_t, 4>& corners) const;
    // Terrains on the tree path from `from` to `to`, inclusive. Empty if unreachable.
    std::vector<uint16_t> path(uint16_t from, uint16_t to) const;
    // Steps between two terrains in the terrain tree (large if unreachable).
    int distance(uint16_t a, uint16_t b) const;

private:
    uint16_t firstTransition_;
    std::vector<Pair> pairs_;
    std::vector<const char*> names_;
    mutable std::vector<int> distances_;  // firstTransition_^2, filled on first use
};

// A corner of the diamond grid (rect-grid point with x + y odd) and the terrain it should have.
struct CornerValue {
    int x = 0, y = 0;
    uint16_t terrain = 0;
};

// Brush footprint in map pixels around the cursor. `size` is in diamonds (0.5 steps): a size-1
// diamond brush covers one diamond (128x64 px).
struct Brush {
    enum Shape { Diamond, Square, Circle } shape = Diamond;
    float size = 1;
};

class IsomGrid {
public:
    // Decode the ISOM section. Fails if the section is missing/short or the tileset has no table.
    bool load(const Chk& chk, std::string& error);

    // A map without an ISOM section (or a short one: made by a tool that writes only the tiles, or protected): makes
    // the ISOM from the tiles (TILE, else MTXM) and loads it. The tiles stay as they are. Each tile votes for the
    // diamond shapes that Blizzard's maps use with it; each corner takes the terrain that most of its diamonds give.
    bool rebuildFromTiles(Chk& chk, std::string& error);

    // Start a new terrain layer: sets DIM, ERA, ISOM (filled with `terrain`, as StarEdit does
    // for a new map), and generates TILE + MTXM. The grid is then loaded from it.
    bool create(Chk& chk, uint16_t width, uint16_t height, uint16_t era, uint16_t terrain, std::string& error);

    // Paint `terrain` on the diamond centered at rect-grid point (x, y) (x + y must be even).
    // `brushSize` > 1 paints a larger diamond of brushSize x brushSize diamonds centered there.
    // Neighboring corners are pulled towards `terrain` along the terrain tree so that every
    // diamond stays valid (e.g. water placed in high dirt gets a ring of dirt).
    // Returns false if the terrain is invalid for this tileset.
    bool paint(int x, int y, uint16_t terrain, int brushSize = 1);

    // Paint at a pixel position (nearest diamond center).
    bool paintAtPixel(int px, int py, uint16_t terrain, int brushSize = 1);

    // --- Terrain correction engine ------------------------------------------------------------
    // Every terrain tool reduces to "these corners must have these terrains". apply() sets them
    // (locked), then corrects the surrounding corners outward so every diamond is a valid shape,
    // widens transitions where no tiles exist, and (in store()) fixes cliff stacks. Returns false
    // if a terrain is not valid for the tileset.
    bool apply(const std::vector<CornerValue>& locked);

    // Corner queries.
    bool isCorner(int x, int y) const { return ((x + y) & 1) && x >= -1 && y >= -1 && x <= rectCols_ + 1 && y <= rectRows_ + 1; }
    uint16_t terrainAt(int x, int y) const { return isCorner(x, y) ? corner(x, y) : 0; }
    // Nearest corner to a map pixel.
    void cornerAtPixel(int px, int py, int& x, int& y) const;
    // Corners covered by a brush centered on a map pixel (at least the nearest corner).
    std::vector<std::pair<int, int>> brushCorners(int px, int py, const Brush& brush) const;
    // Corners connected to the corner nearest (px, py) that have the same terrain.
    std::vector<std::pair<int, int>> floodCorners(int px, int py) const;
    // Corners inside a pixel rectangle (left, top, right, bottom; right/bottom exclusive).
    std::vector<std::pair<int, int>> cornersInRect(int left, int top, int right, int bottom) const;

    // Write ISOM back to the chk, and regenerate TILE + MTXM for the changed area.
    bool store(Chk& chk, std::string& error);

    int rectCols() const { return rectCols_; }
    int rectRows() const { return rectRows_; }
    uint16_t shapeAt(int x, int y) const;  // diamond center (x + y even), 0 if outside
    // Number of non-border diamonds whose corners are not a valid shape (should be 0 after
    // paint()). Border diamonds are skipped: StarEdit does not keep them consistent.
    int invalidDiamonds() const;
    const std::vector<uint8_t>& dirtyRects() const { return dirtyRects_; }

private:
    bool inGrid(int x, int y) const { return x >= 0 && y >= 0 && x <= rectCols_ && y <= rectRows_; }
    uint16_t& corner(int x, int y);
    uint16_t corner(int x, int y) const;
    std::array<uint16_t, 4> cornersOf(int x, int y) const;
    void markDiamondDirty(int x, int y);
    void setCorner(int x, int y, uint16_t terrain);
    // Pull corners of rects that have no tiles for their ISOM combination one step further
    // toward `terrain`, until every changed rect can be tiled.
    void repairUntileable();
    void settleFrom(const std::vector<CornerValue>& seeds, bool markSource);
    bool rectTileable(int rx, int ry) const;
    bool pushRectToward(int rx, int ry, uint16_t terrain);
    // Smallest change to an untileable rect's unlocked corners that makes it tileable without making
    // a valid neighborhood invalid or untileable; false if none within three corners.
    // `stacks`: also require cliff pieces that can stack with the rects above and below.
    // `grow` false: never sets a corner to `prefer` (the painted terrain), so it cannot spread it.
    bool fixRectMinimal(int rx, int ry, uint16_t prefer, bool stacks = false, bool grow = true);
    // Can some tile for this rect sit under some tile for the rect above (learned candidates)?
    bool stacksWithAbove(int rx, int ry) const;
    bool pushRect(int rx, int ry) { uint16_t t = sourceNear(rx, ry); return t && pushRectToward(rx, ry, t); }
    void encodeDirty();
    void writeIsom(Chk& chk) const;
    // Rects (x, y) whose tile cannot stack under the tile above, within `area` (per-rect mask).
    std::vector<std::pair<int, int>> stackBreaks(const Chk& chk, const std::vector<uint8_t>& area) const;
    uint16_t lastTerrain_ = 0;
    std::vector<uint16_t> source_;  // per corner: terrain of the edit that last changed it
    std::vector<uint8_t> locked_;   // per corner: set by the last edit itself (repairs keep these)
    uint16_t sourceNear(int rx, int ry) const;
    uint16_t& sourceAt(int x, int y) { return source_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)]; }
    uint16_t sourceAt(int x, int y) const { return source_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)]; }

    const IsomTerrainTable* table_ = nullptr;
    uint16_t era_ = 0;
    int mapWidth_ = 0, mapHeight_ = 0;  // tiles
    int rectCols_ = 0, rectRows_ = 0;   // width/2 + 1, height + 1
    std::vector<uint16_t> isom_;        // raw slots
    std::vector<uint16_t> shapes_;      // per grid point (diamond centers only)
    std::vector<uint16_t> corners_;     // per grid point incl. 1-point margin (corners only)
    std::vector<uint8_t> diamondDirty_; // per grid point
    std::vector<uint8_t> dirtyRects_;   // per rect
};

// Regenerate TILE (and MTXM) values for tiles in the given rects from ISOM, top-down,
// continuing downward in each column while results differ from the existing tiles
// (cliff pieces stack vertically). Uses corpus-learned rules; returns false if the
// tileset has no rules.
bool regenerateTiles(Chk& chk, const std::vector<uint8_t>& dirtyRects, std::string& error);
// How regenerateTiles found its tiles since the last reset (for tests and paint_stress): from the rules, predicted for a
// combination the rules lack, or not at all (the old tile stays: a visible glitch).
struct TileRegenStats {
    long chosen = 0, predicted = 0, unknown = 0;
};
TileRegenStats& tileRegenStats();
// Tiles that do not fit the tile above them (a cliff stack) or to their left (an edge pair Blizzard's maps never have):
// seen as glitches. `where` gets their tile positions (x, y) when given.
int countTileMisfits(const Chk& chk, std::vector<std::pair<int, int>>* where = nullptr);

// Tiles placed as they are (rectangular terrain, roadmap N.7) are in TILE and MTXM, and the ISOM does not give them.
// The terrain correction (store, regenerateTiles) keeps such a tile unless an edit changes the ISOM of its own rect.
// tileFromIsom: could the ISOM have given TILE's tile at (tx, ty) (a group Blizzard's maps use for its rect)?
bool tileFromIsom(const Chk& chk, int tx, int ty);
bool tileFromIsom(const Chk& chk, const struct TerrainLayer& tile, int tx, int ty);  // with TILE read already

// Per tile group of the tileset (CV5 index < groupCount): the ISOM terrains Blizzard's maps use it with (from the
// learned tile rules). A solid group has one, a transition two; {0, 0} when no Blizzard map uses it.
std::vector<std::array<uint16_t, 2>> tileGroupTerrains(uint16_t era, size_t groupCount);

// Tile group Blizzard's maps use under solid `terrain` (from the learned tile rules), or -1.
int solidTileGroup(uint16_t era, uint16_t terrain, int half = 0);  // half: 0 left, 1 right tile of a rect

}  // namespace stardraft
