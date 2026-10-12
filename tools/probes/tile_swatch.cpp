// Renders tile groups of a tileset side by side: tile_swatch <install> <era> <out.png> <group>...
// Each group becomes a 4x2-tile patch (subtiles 0-7). Used to identify terrains by appearance.
#include <cstdio>
#include <cstdlib>
#include "stardraft/game_data.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
using namespace stardraft;
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Tileset ts;
    if (!ts.load(data, uint16_t(std::atoi(argv[2])), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    int n = argc - 4;
    TerrainLayer t;
    t.width = uint16_t(n * 5), t.height = 2;
    t.tiles.assign(size_t(t.width) * t.height, 0);
    for (int i = 0; i < n; ++i) {
        int g = std::atoi(argv[4 + i]);
        for (int k = 0; k < 8; ++k) t.tiles[size_t(k / 4) * t.width + i * 5 + k % 4] = uint16_t(g << 4 | k);
    }
    return writePng(argv[3], renderTerrain(ts, t, 1)) ? 0 : 1;
}
