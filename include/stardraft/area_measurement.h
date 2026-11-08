#pragma once

#include "stardraft/terrain.h"
#include <utility>


namespace stardraft {

struct MeasuredTile {
    int x = 0, y = 0;
    uint16_t walk = 0;
    bool buildable = false;
};
struct MeasuredArea {
    std::vector<MeasuredTile> tiles;
    size_t walkCells = 0, buildTiles = 0;
    double totalNexus() const { return tiles.size() / 12.0; }
    double walkableNexus() const { return walkCells / 192.0; }
    double buildableNexus() const { return buildTiles / 12.0; }
};

// Vertices are tile-grid intersections. Even-odd fill of tile centers, clipped to the map.
// Terrain totals exclude units and resource exclusion zones; they are not a packing count.
MeasuredArea measureArea(const TerrainLayer&, const Tileset&, const std::vector<std::pair<int, int>>& vertices);
// A row-major tile mask permits disconnected regions and holes. Nonzero cells are selected.
MeasuredArea measureAreaMask(const TerrainLayer&, const Tileset&, const std::vector<uint8_t>& mask);


} // namespace stardraft
