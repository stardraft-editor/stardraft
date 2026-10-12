#pragma once

// Ground pathing for the map view's overlays (roadmap R24): where ground units can walk, the separate ground
// regions, the shortest ground paths between the start locations, and the town area of each base.
//
// The walk grid has one cell per 8x8 minitile, as the game's pathing has: a cell is walkable when the tileset's VF4
// flag of that minitile says so (bit 0) and no static obstacle covers the cell's middle. Static obstacles are the
// ground buildings and resources on the map (UNIT, and the units of THG2), with their units.dat collision box, as
// units collide with them in the game. The start location markers are not obstacles. Mobile units are not obstacles.
// GPU-free and UI-free.

#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/doodads.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace stardraft {

struct WalkGrid {
    int width = 0, height = 0;  // in cells (minitiles): map tiles * 4
    // Per cell: bit 0 the terrain is walkable (VF4), bit 1 an obstacle covers it, bits 2-3 the VF4 elevation
    // (0 low, 1 middle, 2 high).
    std::vector<uint8_t> cells;
    static constexpr uint8_t kTerrainWalkable = 1, kBlocked = 2;

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    bool walkable(int x, int y) const {
        return inside(x, y) && (cells[size_t(y) * width + x] & (kTerrainWalkable | kBlocked)) == kTerrainWalkable;
    }
    int elevation(int x, int y) const { return (cells[size_t(y) * width + x] >> 2) & 3; }
};

// The data the analysis reads; every pointer is needed except `sprites`.
struct PathingInput {
    const TerrainLayer* terrain = nullptr;  // MTXM
    const Tileset* tileset = nullptr;
    const UnitTypes* types = nullptr;
    const std::vector<Unit>* units = nullptr;
    const std::vector<MapSprite>* sprites = nullptr;  // THG2: the entries that are units (not DrawAsSprite) count
};

// The size of the unit that the paths are for: a path goes only where a square of this many cells (8 px each) is
// walkable. Small: a zergling (16 px); Medium: a dragoon or a siege tank (32 px); Large: an ultralisk (40 px).
enum class PathUnitSize { Small, Medium, Large };
int pathUnitCells(PathUnitSize size);
const char* pathUnitSizeName(PathUnitSize size);

struct PathingOptions {
    PathUnitSize unitSize = PathUnitSize::Medium;
    int townRadius = 12;      // tiles: the buildable tiles of a town area are this near its town hall
    int resourceRadius = 12;  // tiles: the resources of a start location are this near it
    bool expansions = true;   // also find the resource groups with no start location
};

// A unit is a static obstacle for ground units: a ground building or a resource (not a start location).
bool isStaticObstacle(const UnitType& type, uint16_t typeId);
// A unit is a resource for the town areas: the Resources group of StarEdit's palette (minerals and geysers).
bool isResource(const UnitType& type);

WalkGrid buildWalkGrid(const PathingInput& in);

// Connected walkable areas (4-connected cells). id: the region of each cell, -1 where not walkable.
// Regions are numbered by size, the largest first.
struct GroundRegions {
    std::vector<int32_t> id;
    std::vector<int> size;  // cells per region
};
GroundRegions findRegions(const WalkGrid& grid);

// For each cell: the size (in cells, at most 15) of the largest walkable square that has the cell at its top-left.
std::vector<uint8_t> clearance(const WalkGrid& grid);

struct GroundPath {
    int from = -1, to = -1;  // indexes into PathingAnalysis::bases
    bool found = false;
    double lengthTiles = 0;  // the path's length in 32 px tiles
    std::vector<std::pair<float, float>> points;  // map pixels, from `from` to `to`; corners only
};

// The shortest 8-connected path (A*; no corner cutting) for a unit of `cells` from (x0, y0) to (x1, y1) (map pixels).
// The ends move to the nearest cell the unit can stand on (at most 6 tiles away).
GroundPath findGroundPath(const WalkGrid& grid, const std::vector<uint8_t>& clear, int cells, float x0, float y0, float x1, float y1);

struct Base {
    bool start = false;  // a start location (else an expansion: a resource group with no start location)
    int owner = -1;      // the start location's player (0-based), -1 for an expansion
    float x = 0, y = 0;  // the town hall's middle (the start location), map pixels
    int minerals = 0, geysers = 0;           // resource units
    uint32_t mineralAmount = 0, gasAmount = 0;
    std::vector<int> resources;   // indexes into the input's units
    std::vector<int> buildable;   // tiles (y * map width + x) of the town area
    int region = -1;              // ground region of the town hall, -1 if none
};

struct PathingAnalysis {
    WalkGrid grid;
    GroundRegions regions;
    std::vector<Base> bases;  // start locations (by owner), then expansions
    std::vector<GroundPath> paths;  // one for each pair of start locations
    int unitCells = 4;
    // Milliseconds per step: walk grid, regions, clearance, bases (town areas), paths, and all.
    double gridMs = 0, regionsMs = 0, clearanceMs = 0, basesMs = 0, pathsMs = 0, totalMs = 0;
};

PathingAnalysis analyzePathing(const PathingInput& in, const PathingOptions& options = {});

}  // namespace stardraft
