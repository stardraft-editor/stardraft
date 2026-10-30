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

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class IsomTerrainTable {
public:
    struct Pair {
        uint16_t firstShape, a, b;
        // A mod's border (withExtras): the game's terrains whose border has its layout (its template: the shapes of
        // (ta, tb) for those of (a, b)), and its tile pieces (tag); a game border: 0.
        uint16_t ta = 0, tb = 0, tag = 0;
        bool twin = false;  // a mod terrain (a) beside a twin of its ground (b): a path takes it only from or to a
    };
    // A terrain of a mod (Tileset::modTerrains): its id (past the game's), the game terrain whose tiles its tiles take
    // the place of (templ), the terrain it borders, its tile pieces (tag: 1 and up), its name, and the game terrain its
    // ground is in its border's template (groundTempl: a terrain a level higher on the ground's place; 0: the ground).
    struct Extra {
        uint16_t id, templ, ground, tag;
        std::string name;
        uint16_t groundTempl = 0;
        std::vector<uint16_t> twins;  // more terrains it borders as it does its ground (the same inside and height)
    };

    // Table for a tileset (all eight are known).
    static const IsomTerrainTable* forEra(uint16_t era);
    // The game's table with the terrains of a mod: each takes the shapes from `firstValue` on (past the game's), its solid
    // shape and then the 14 of its border with its ground. The terrain engine (IsomGrid) paints them as the game's.
    static constexpr int kExtraSlots = 80;  // (the mod terrains of a tileset that withExtras makes room for before twins)
    static std::unique_ptr<IsomTerrainTable> withExtras(const IsomTerrainTable& game, const std::vector<Extra>& extras, uint16_t firstValue);
    bool hasExtras() const { return game_ != nullptr; }
    // A mod terrain a level higher in it (its border's ground is another game terrain in the template).
    bool hasLevels() const { return levels_; }
    // A mod terrain's template terrain and tag (withExtras); false: not a mod terrain.
    bool modTerrain(uint16_t id, uint16_t& templ, uint16_t& tag) const {
        for (const auto& s : solids_)
            if (s.id == id) return templ = s.templ, tag = s.tag, true;
        return false;
    }
    // The first value past the table's shapes.
    uint16_t shapeEnd() const;
    // The game's shape whose tiles a mod's shape takes the place of (its template); a game shape: itself.
    uint16_t templateShape(uint16_t shape) const;
    // The game's shapes for an ISOM rect of the mod's (its left and right diamonds), and the tile pieces for it (tag, 0:
    // the game's tiles). False: no game rect has the layout (two mod terrains in it, or a terrain that the two diamonds'
    // templates make two).
    bool templateRect(uint16_t left, uint16_t right, uint16_t& gameLeft, uint16_t& gameRight, uint16_t& tag) const;
    // A rect with a diamond of a mod terrain's border whose ground is another game terrain in the template (a terrain a
    // level higher): that ground (from), its template terrain (to) and the border's tag.
    bool levelGround(uint16_t left, uint16_t right, uint16_t& from, uint16_t& to, uint16_t& tag) const;

    IsomTerrainTable(uint16_t firstTransition, std::vector<Pair> pairs, std::vector<const char*> names = {});

    // A brush terrain: a solid id with a name (some ids below the first transition are unused).
    bool isTerrain(uint16_t t) const { return t >= 1 && t <= names_.size() && names_[t - 1][0]; }
    // Brush terrains (ids 1..n) and their StarEdit names; ids without a name are not brushes.
    uint16_t terrainCount() const { return uint16_t(names_.size()); }
    const char* terrainName(uint16_t t) const { return t >= 1 && t <= names_.size() ? names_[t - 1] : ""; }
    bool linked(uint16_t a, uint16_t b) const;
    // The borders (each: its first shape, the ground a, the terrain b on it).
    const std::vector<Pair>& pairs() const { return pairs_; }
    // a stands on b only as on a twin of its ground (withExtras), not by a border of their own.
    bool twinOnly(uint16_t a, uint16_t b) const;
    // Corner terrains (left, top, right, bottom) of a shape; all zero if unknown.
    std::array<uint16_t, 4> corners(uint16_t shape) const;
    // Shape for four corner terrains, or 0 if the combination is not representable.
    uint16_t shapeFor(const std::array<uint16_t, 4>& corners) const;
    // Terrains on the tree path from `from` to `to`, inclusive. Empty if unreachable.
    // (`avoid`: terrains the way does not go through, unless one is `from` or `to`)
    std::vector<uint16_t> path(uint16_t from, uint16_t to, const std::vector<uint16_t>* avoid = nullptr) const;
    // The terrains a mod terrain stands on: its ground and the twins of it (withExtras); empty for one with no twins.
    std::vector<uint16_t> groundsOf(uint16_t terrain) const;
    // Steps between two terrains in the terrain tree (large if unreachable).
    int distance(uint16_t a, uint16_t b) const;
    // The longest of the distances between two terrains (the tree's diameter): how far an edit's terrains can need to
    // reach (IsomGrid::settleFrom stops past it).
    int diameter() const;

private:
    struct Solid {
        uint16_t id, shape, templ, tag;
    };
    int idCount() const { return game_ ? std::max<int>(firstTransition_, int(names_.size()) + 1) : firstTransition_; }
    uint16_t firstTransition_;
    std::vector<Pair> pairs_;
    std::vector<const char*> names_;
    mutable std::vector<int> distances_;  // idCount()^2, filled on first use
    const IsomTerrainTable* game_ = nullptr;  // (withExtras: the game's table)
    bool levels_ = false;                     // (withExtras: hasLevels)
    std::vector<Solid> solids_;               // (withExtras: the mod's terrains' solid shapes)
    // (by shape value: the solid of solids_ and the pair of pairs_ it is one of, -1: none; made once the table is whole,
    // indexShapes: the whole-map passes look each rect's shapes up)
    std::vector<int16_t> shapeSolid_, shapePair_;
    void indexShapes();
    const Solid* solidOf(uint16_t shape) const { return shape < shapeSolid_.size() && shapeSolid_[shape] >= 0 ? &solids_[size_t(shapeSolid_[shape])] : nullptr; }
    const Pair* pairOf(uint16_t shape) const { return shape < shapePair_.size() && shapePair_[shape] >= 0 ? &pairs_[size_t(shapePair_[shape])] : nullptr; }
    std::vector<std::string> ownNames_;       // (withExtras: names_ points into it)
    std::vector<std::string> gameNames_;      // (withExtras: the game terrains' names with their ground, names_ points into it)
};

// The diamond (rect-grid point with x + y even) nearest a map pixel: grid points are 64 px apart across, 32 down.
void diamondAtPixel(int px, int py, int& x, int& y);

// The diamonds StarEdit's brush paints: extent x extent diamonds around the diamond (x, y); an even extent grows down
// and left, as StarEdit's does (IsomTerrain's placeIsomTerrain).
std::vector<std::pair<int, int>> starEditBrushDiamonds(int x, int y, int extent);

// StarEdit's brush at a map pixel: an odd extent is centered on the nearest diamond, an even one on the nearest corner
// (the middle of starEditBrushDiamonds), so it moves in half-diamond steps; both are StarEdit's blocks, the same shape
// wherever the brush is. (Half a diamond, one corner, is the 0.5 brush: not StarEdit's.)
std::vector<std::pair<int, int>> starEditBrushAt(int px, int py, int extent);

// The lattice point StarEdit's brush of this extent is centered on at a map pixel (see starEditBrushAt): a diamond
// (x + y even) for an odd extent, a corner for an even one. Mirrors of this point are exact for the mirrors that keep
// the lattice (the terrain brush mirrors from it, so the copies stay on the lattice).
std::pair<int, int> starEditCenter(int px, int py, int extent);

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
    float sizeY = 0;  // its height, when it differs from its width (GitHub #113); 0: the same as size
    float height() const { return sizeY > 0 ? sizeY : size; }
    // StarEdit's own brush: a diamond of whole diamonds, as wide as high. It paints starEditBrushDiamonds around the
    // diamond nearest the cursor (the terrain core paints it as StarEdit does).
    bool starEdit() const { return shape == Diamond && size >= 1 && size == float(int(size)) && height() == size; }
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
    // for a new map), and generates TILE + MTXM (the terrain core, from `tileset`, the era's). The grid is then loaded
    // from it, with that tileset.
    bool create(Chk& chk, uint16_t width, uint16_t height, uint16_t era, uint16_t terrain, const struct Tileset& tileset, std::string& error);

    // Paint `terrain` on the diamond centered at rect-grid point (x, y) (x + y must be even).
    // `brushSize` > 1 paints a larger diamond of brushSize x brushSize diamonds centered there.
    // Neighboring corners are pulled towards `terrain` along the terrain tree so that every
    // diamond stays valid (e.g. water placed in high dirt gets a ring of dirt).
    // Returns false if the terrain is invalid for this tileset.
    bool paint(int x, int y, uint16_t terrain, int brushSize = 1);

    // Paint at a pixel position (nearest diamond center).
    bool paintAtPixel(int px, int py, uint16_t terrain, int brushSize = 1);
    // A new stroke: the next edit chooses again which ground a terrain with twin grounds takes (apply).
    void beginStroke() { strokeTerrain_ = strokeGround_ = 0; }

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
    // The game rect whose tiles the rect (rx, ry) takes in the template, and its mod terrain's tag (rectTemplate).
    bool templateOf(int rx, int ry, uint16_t& gameLeft, uint16_t& tag) const {
        uint16_t gr = 0;
        return rectTemplate(rx, ry, gameLeft, gr, tag);
    }
    // Corners connected to the corner nearest (px, py) that have the same terrain.
    std::vector<std::pair<int, int>> floodCorners(int px, int py) const;
    // Corners inside a pixel rectangle (left, top, right, bottom; right/bottom exclusive).
    std::vector<std::pair<int, int>> cornersInRect(int left, int top, int right, int bottom) const;

    // The tiles of rect (rx, ry) are made again on the next store(), also when its corners did not change (tiles that
    // do not show the ISOM terrain, such as null tiles placed in Rectangle mode).
    void markRectDirty(int rx, int ry) {
        if (rx >= 0 && ry >= 0 && rx < rectCols_ && ry < rectRows_ && size_t(ry) * rectCols_ + rx < dirtyRects_.size()) dirtyRects_[size_t(ry) * rectCols_ + rx] = 1;
    }

    // Write ISOM back to the chk, and regenerate TILE + MTXM for the changed area (with the tileset when it is set:
    // regenerateTiles through the terrain core).
    bool store(Chk& chk, std::string& error);
    // The map's tileset: store's tiles and the checks of which tiles fit come from its CV5 (the terrain core's tables,
    // IsomTiles); none: no tiles can be made (store fails).
    void setTileset(const struct Tileset* tileset);
    // The map's terrain table when it is not the game's (IsomTerrainTable::withExtras: a mod's terrains, whose tiles are
    // the pieces of the tileset's modTerrains, tag 1 the first); none: the game's. The next load() takes it.
    void setTable(const IsomTerrainTable* table) { ownTable_ = table; }
    // tileFromIsom with the map's table: in the rect of a mod's terrain, the mod's pieces for the rect of its template
    // (and the template's tiles) are from the ISOM too.
    bool tileFromIsom(const Chk& chk, const struct TerrainLayer& tile, int tx, int ty) const;

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
    // Can some tile for this rect sit under some tile for the rect above (the CV5 groups StarEdit picks for each)?
    bool stacksWithAbove(int rx, int ry) const;
    bool pushRect(int rx, int ry) { uint16_t t = sourceNear(rx, ry); return t && pushRectToward(rx, ry, t); }
    void encodeDirty();
    // `templates`: each mod shape as its template's (IsomTerrainTable::templateShape), for the terrain core.
    void writeIsom(Chk& chk, bool templates = false) const;
    // The tiles of `rects` from the copy of the map the terrain core made them on (`work`, the templates' tiles), with
    // the mod's pieces for them in the rects of a mod's terrain.
    void putPieces(Chk& chk, const Chk& work, const std::vector<uint8_t>& rects) const;
    // One of a rect's mod pieces (`h`: the place's hash): one that the mod's maps have beside the pieces already placed
    // around it in `tile` (above, a row up to the left and to the right: Tileset::pieceNeighbors); fewer of those
    // neighbors when none fits all, any piece when none fits one.
    std::pair<uint16_t, uint16_t> pickPiece(const std::vector<std::pair<uint16_t, uint16_t>>& pieces, const TerrainLayer& tile, int rx, int ry,
                                            uint32_t h) const;
    // The game's shapes of a rect's template (its left and right diamond: IsomTerrainTable::templateRect; no rect of the
    // game's: each diamond's own template); a game rect: its shapes. templateSlots: of rect (rx, ry) as its shapes are now.
    std::pair<uint16_t, uint16_t> templatePair(uint16_t left, uint16_t right) const;
    std::pair<uint16_t, uint16_t> templateSlots(int rx, int ry) const;
    // Rect (rx, ry)'s template (templateRect); a rect of one terrain around the border of a mod terrain a level higher (two
    // rects above and below, one beside) as that border's template ground (the game's walls go down over it). `ground`:
    // in such rects and in that border's own, the ground whose own tiles go where the set has no piece (0: none).
    // False: no rect of the game's.
    bool rectTemplate(int rx, int ry, uint16_t& gameLeft, uint16_t& gameRight, uint16_t& tag, uint16_t* ground = nullptr) const;
    // Whether slot k (left, top, right, bottom) of rect (rx, ry) holds its right diamond's shape (encodeDirty's slots).
    static bool rightSlot(int rx, int ry, int k) { return k == 2 || (k == 1 && ((rx + ry) & 1)) || (k == 3 && !((rx + ry) & 1)); }
    // Rects (x, y) whose tile cannot stack under the tile above, within `area` (per-rect mask).
    std::vector<std::pair<int, int>> stackBreaks(const Chk& chk, const std::vector<uint8_t>& area) const;
    uint16_t lastTerrain_ = 0;
    // An edit's terrain that has more grounds (its own and twins: Caution on Substructure on Dirt or on Jungle): the
    // grounds the settle does not take for it this edit (all but the one the stroke started on), so that one stroke makes
    // one ring and not two that cannot meet (apply).
    std::vector<uint16_t> avoid_;
    uint16_t painted_ = 0;  // (the edit's terrain while it settles: only it takes a twin of its ground beside it)
    uint16_t strokeTerrain_ = 0, strokeGround_ = 0;  // the stroke's terrain and the ground it chose (beginStroke)
    std::vector<uint16_t> source_;  // per corner: terrain of the edit that last changed it
    std::vector<uint8_t> locked_;   // per corner: set by the last edit itself (repairs keep these)
    uint16_t sourceNear(int rx, int ry) const;
    uint16_t& sourceAt(int x, int y) { return source_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)]; }
    uint16_t sourceAt(int x, int y) const { return source_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)]; }

    const IsomTerrainTable* table_ = nullptr;
    const IsomTerrainTable* diameterOf_ = nullptr;  // (settleFrom: the table whose diameter_ it is)
    int diameter_ = 0;
    int reachX0_ = 0, reachY0_ = 0, reachX1_ = -1, reachY1_ = -1;  // (the last edit's reach, corner coordinates: settleFrom, repairUntileable)
    const IsomTerrainTable* ownTable_ = nullptr;  // (setTable)
    // A mod's terrains: per tag, the game's left tile group -> the mod's pieces (left, right tile) for its rect; and the
    // game's tile for each tile of a piece (the terrain core reads the stack connections of the tiles above).
    std::vector<std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>>> pieces_;
    std::unordered_map<uint16_t, uint16_t> gameTile_;
    // A mod terrain a level higher: per rect, the border it is around (rectTemplate), made once for a pass over the map
    // (LevelMask: writeIsom, stackBreaks, putPieces; the shapes do not change during one) instead of a search per rect.
    struct LevelRect {
        uint16_t from = 0, to = 0, tag = 0;
    };
    mutable std::vector<LevelRect> levelMask_;
    struct LevelMask {
        const IsomGrid& grid;
        bool own = false;
        explicit LevelMask(const IsomGrid& g);
        ~LevelMask();
    };
    bool piecesOnly_ = false;  // (repairUntileable: rectTileable asks for the mod's piece too)
    std::unordered_map<uint64_t, uint8_t> fixFailed_;  // (fixRectMinimal: calls that found nothing, by their rect and the corners around it; an edit clears it)
    const struct Tileset* tileset_ = nullptr;
    const class IsomTiles* tiles_ = nullptr;  // (tileset_'s)
    uint16_t era_ = 0;
    int mapWidth_ = 0, mapHeight_ = 0;  // tiles
    int rectCols_ = 0, rectRows_ = 0;   // width/2 + 1, height + 1
    std::vector<uint16_t> isom_;        // raw slots
    std::vector<uint16_t> shapes_;      // per grid point (diamond centers only)
    std::vector<uint16_t> corners_;     // per grid point incl. 1-point margin (corners only)
    std::vector<uint8_t> diamondDirty_; // per grid point
    std::vector<uint8_t> dirtyRects_;   // per rect
};

// TILE and MTXM of the given rects (per ISOM rect, row-major) made again from the ISOM by the terrain core (IsomCore::
// retile): StarEdit's tiles, from the tileset's CV5, down the cliff stacks under them. A rect that no tile group fits
// keeps its tiles. False when the tileset is not the map's.
bool regenerateTiles(Chk& chk, const std::vector<uint8_t>& dirtyRects, const struct Tileset& tileset, std::string& error);
// Tiles that do not fit by the tileset's CV5 (IsomTiles): a tile under one it cannot stack under, rects side by side
// that share no diamond shape. None on Blizzard's maps; seen as glitches. `where` gets their tile positions (x, y).
int countTileMisfits(const Chk& chk, const struct Tileset& tileset, std::vector<std::pair<int, int>>* where = nullptr);

// Tiles placed as they are (rectangular terrain, roadmap N.7) are in TILE and MTXM, and the ISOM does not give them.
// The terrain correction (store, regenerateTiles) keeps such a tile unless an edit changes the ISOM of its own rect.
// tileFromIsom: could the ISOM have given TILE's tile at (tx, ty)? By the tileset's CV5: a group StarEdit picks for its
// rect, or a cliff piece under the stack above.
bool tileFromIsom(const Chk& chk, const struct TerrainLayer& tile, int tx, int ty, const struct Tileset& tileset);

// Per tile group of the tileset: the ISOM terrains StarEdit uses it with (the terrains at the corners of the rects it
// picks the group for, from the CV5): a group of one terrain has one, a transition two; {0, 0} when none.
std::vector<std::array<uint16_t, 2>> tileGroupTerrains(const struct Tileset& tileset);

// The tile group StarEdit puts under solid `terrain` (from the CV5), or -1. half: 0 left, 1 right tile of a rect.
int solidTileGroup(const struct Tileset& tileset, uint16_t terrain, int half = 0);

}  // namespace stardraft
