// A block's variants side by side: for each first group given, its `rows` groups from the top, each variant (subtile
// pair 0-1, 2-3, ... 14-15) a column of two tiles, a gap between them; one band per group. For seeing which of a
// set's variants are what (gates, lights) and which join.
// usage: cmbw_variants <Cosmonarchy install> <era> <rows> <out .png> <first group> [first group ...]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 6) return std::fprintf(stderr, "usage: cmbw_variants <install> <era> <rows> <out .png> <first group> ...\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Tileset t;
    if (!t.load(data, uint16_t(std::atoi(argv[2])), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const int rows = std::atoi(argv[3]), bands = argc - 5;
    TerrainLayer sheet;
    sheet.width = 8 * 3, sheet.height = uint16_t(bands * (rows + 1));
    sheet.tiles.assign(size_t(sheet.width) * sheet.height, 0);
    for (int b = 0; b < bands; ++b) {
        const int g0 = std::atoi(argv[5 + b]);
        for (int r = 0; r < rows; ++r)
            for (int v = 0; v < 8; ++v) {
                const size_t k = size_t(b * (rows + 1) + r) * sheet.width + size_t(v * 3);
                sheet.tiles[k] = uint16_t((g0 + r) << 4 | (2 * v));
                sheet.tiles[k + 1] = uint16_t((g0 + r) << 4 | (2 * v + 1));
            }
    }
    return writePng(argv[4], renderTerrain(t, sheet)) ? 0 : 1;
}
