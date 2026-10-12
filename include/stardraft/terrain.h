#pragma once

#include <cstdint>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/tileset.h"

namespace stardraft {

struct TerrainLayer {
    uint16_t width = 0, height = 0;  // in 32x32 tiles
    std::vector<uint16_t> tiles;     // width * height, row-major
};

// Map dimensions and ERA as StarCraft reads them (DIM, ERA & 7).
bool readDimensions(const Chk& chk, uint16_t& width, uint16_t& height);
uint16_t readEra(const Chk& chk);

// Terrain StarCraft displays: MTXM sections applied in file order, each overwriting
// from the start (a short section keeps earlier tiles; never-set tiles are 0).
// Pass "TILE" to get the editor's terrain instead.
TerrainLayer readTerrain(const Chk& chk, SectionName section = "MTXM");

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// Render terrain at full resolution (32 px per tile) divided by `downscale` (1, 2, 4, 8 ...),
// box-filtered. Tiles that do not resolve to a megatile are drawn magenta.
Image renderTerrain(const Tileset& tileset, const TerrainLayer& terrain, int downscale = 1);

bool writePng(const char* path, const Image& image);

}  // namespace stardraft
