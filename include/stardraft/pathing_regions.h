#pragma once

// The game's ground pathing regions (StarCraft's SAI regions) and the pathing check (roadmap R35, feedback #75).
//
// computePathingRegions splits the map into regions as StarCraft does when a game starts (from MTXM and the tileset's
// VF4 flags only: buildings, resources and doodad sprites are not in the regions; creep is not either). The steps
// follow the open-source reimplementation of the game's region code (OpenBW, bwgame.h: regions_create), written again
// here:
//   1. Each 8x8 minitile is walkable when its VF4 flag says so; the bottom of the map that the game's console covers is
//      not (the last 4 minitile rows; the 20 minitiles at each end of the 4 rows above them).
//   2. Each 32x32 tile gets a kind from its 16 minitiles: walkable when more than 12 are walkable (else unwalkable),
//      and when walkable low, middle or high ground (12 or more minitiles of that level). The two last rows lose their
//      walkability at the console (5 tiles at each end of the row above the last; all of the last row).
//   3. Regions of tiles of one kind: the game scans the map for a tile with no region, takes the largest rectangle of
//      that kind from it (at most 8 x 7 tiles, never more than 3 times as long as wide; up to 25 more tries when the
//      rectangle is smaller than the last one), and grows each new region by the tiles of its kind beside it.
//   4. A region of fewer than 6 tiles goes into its smallest neighbor of the same kind; then of fewer than 8, 10, ...
//      until fewer than 2500 regions are left.
//   5. Neighbors: tiles beside each other (diagonal neighbors of walkable regions only where a 16x16 unit fits through
//      the corner). The connected areas (the game's region groups) are the regions that reach each other.
//   6. A tile with walkable and unwalkable minitiles is split: its minitiles of the other kind go to the neighbor
//      region of that kind that has the most minitiles of it at the tile's edge.
// Where this differs from the game: the game stops with "too many regions (nooks and crannies)" when step 3 makes
// 5000 regions; here the regions stop at 5000 and `tooMany` is set. The game's step 4 has no end when no merge is
// possible and 2500 or more regions are left; here it ends then. The contours (the game's unit collision with the
// terrain edges) are not made.
//
// The pathing check (checkPathing) finds the nooks and crannies: walkable ground that no unit can use but that the
// game's regions count, the places where only small units pass (they can look closed), and the places where units can
// walk but the game's regions do not join the two sides (units find no path there). GPU-free and UI-free.

#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace stardraft {

struct PathingRegion {
    uint16_t flags = 0x1ffd;  // the game's: 0x1ffd unwalkable, 0x1ffb low, 0x1ff9 middle, 0x1ffa high ground
    bool walkable() const { return flags != 0x1ffd; }
    int elevation() const { return flags == 0x1ffa ? 2 : flags == 0x1ff9 ? 1 : 0; }  // (walkable regions)
    int tiles = 0;            // tiles whose region it is (a split tile counts for its own region)
    int minitiles = 0;        // minitiles in the region (with the split tiles' parts)
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the tiles' box (x1, y1 not in it)
    int centerX = 0, centerY = 0;        // the game's center: a tile of the first rectangle (in tiles)
    int group = 0;            // the connected area (the game's group index; 0x4000: the console's region)
    std::vector<int> neighbors;  // the regions of the same walkability beside it
};

struct PathingRegions {
    int width = 0, height = 0;  // in tiles
    std::vector<uint8_t> walk;  // per minitile (width * 4 x height * 4): 1 walkable (step 1), else 0
    // Per tile: the region (0 or more), or -1 - k for split k.
    std::vector<int32_t> tile;
    struct Split {
        uint16_t mask = 0;  // bit (y * 4 + x) set: that minitile is in `b`, else in `a`
        int a = 0, b = 0;   // the walkable part's region and the unwalkable part's
    };
    std::vector<Split> splits;
    std::vector<PathingRegion> regions;  // region 0 is the console's unwalkable bottom row
    int groups = 0;          // connected areas (walkable and unwalkable): group indexes 1..groups
    int firstRegions = 0;    // the regions after step 3 (StarCraft allows fewer than 5000)
    bool tooMany = false;    // step 3 made 5000 regions: StarCraft does not start the map
    double ms = 0;           // the time it took

    int walkW() const { return width * 4; }
    int walkH() const { return height * 4; }
    bool walkable(int mx, int my) const { return mx >= 0 && my >= 0 && mx < walkW() && my < walkH() && walk[size_t(my) * walkW() + mx]; }
    // The region of minitile (mx, my), -1 outside the map.
    int regionAt(int mx, int my) const;
};

PathingRegions computePathingRegions(const TerrainLayer& terrain, const Tileset& tileset);

// --- The pathing check ---------------------------------------------------------------------------------------------

enum class PathingProblemKind {
    TooManyRegions,  // StarCraft does not start the map
    Pocket,          // walkable ground apart from the others, too small for a unit or with no start location
    Gap,             // walkable minitiles too narrow for any unit that join two ground areas in the game's regions
    SmallUnitsOnly,  // a passage where only small units fit (16 or 24 px wide): it can look closed
    ClosedPassage,   // units can walk through, but the game's regions have no path there (they are not connected)
    SmallRegion,     // a pathing region with few walkable minitiles
};

struct PathingProblem {
    PathingProblemKind kind = PathingProblemKind::Pocket;
    int x = 0, y = 0;                    // the place: a minitile in it (map pixels: its middle)
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // its minitiles' box (x1, y1 not in it)
    int minitiles = 0;                   // walkable minitiles in it
    bool fitsUnit = false;               // (Pocket) a small unit (16 x 16 px) fits in it
    int region = -1;                     // (SmallRegion) the region
    std::vector<int> cells;              // its minitiles (y * walkW + x), at most 4096
    // The fix (a Pocket in one tile): the tile and the tile value that has no walkable minitile there; else -1.
    int fixTileX = -1, fixTileY = -1;
    uint16_t fixValue = 0;
};

struct PathingCheckOptions {
    int smallRegionMinitiles = 16;  // a region with fewer walkable minitiles is a SmallRegion
    int pocketMinitiles = 256;      // ground with no start location and fewer minitiles (16 tiles) is a Pocket
    int areaMinitiles = 64;         // a ground area for Gap and SmallUnitsOnly has this many minitiles or more
    int closedMinitiles = 16;       // a ClosedPassage has this many minitiles or more
    size_t most = 500;              // the most problems in the list
};

struct PathingCheck {
    // In their order: TooManyRegions, Pocket, Gap, SmallUnitsOnly, ClosedPassage, SmallRegion.
    std::vector<PathingProblem> problems;
    size_t more = 0;  // problems not in the list (over `most`)
    int pockets = 0, gaps = 0, narrow = 0, closed = 0, smallRegions = 0;  // the counts of each kind (all, also those over `most`)
};

// The start locations (map pixels) are the units of type 214 in `units`.
PathingCheck checkPathing(const PathingRegions& regions, const TerrainLayer& terrain, const Tileset& tileset, const std::vector<Unit>& units,
                          const PathingCheckOptions& options = {});

// A tile value of the tileset for tile (tx, ty) whose minitiles are walkable as they are now except those in `cells`
// (minitiles of that tile, y * walkW + x), which are not: of the same tile group if one is, else the nearest tile of the
// map (at most 8 tiles away) that is so, else of a group with the same terrain type and edges, else with the same edges.
// Returns false when there is none.
bool findUnwalkableTile(const TerrainLayer& terrain, const Tileset& tileset, int tx, int ty, const std::vector<int>& cells, uint16_t& value);

const char* pathingProblemName(PathingProblemKind kind);

}  // namespace stardraft
