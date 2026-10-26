// Prints a map's tiles in an area as group.subtile, and its ISOM terrains there: to see how a mod's maps build a terrain.
// usage: cmbw_tile_dump <map> <x0> <y0> <x1> <y1> (tiles, x1 and y1 not included)
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 6) return std::fprintf(stderr, "usage: cmbw_tile_dump <map> <x0> <y0> <x1> <y1>\n"), 2;
    Bytes raw;
    std::string err;
    if (!readMapFile(argv[1], raw, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const Chk chk = Chk::parse(raw);
    const TerrainLayer tile = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
    const int x0 = std::atoi(argv[2]), y0 = std::atoi(argv[3]), x1 = std::atoi(argv[4]), y1 = std::atoi(argv[5]);
    std::printf("map %ux%u\n", tile.width, tile.height);
    for (int y = y0; y < y1 && y < tile.height; ++y) {
        std::printf("%3d:", y);
        for (int x = x0; x < x1 && x < tile.width; ++x) {
            const uint16_t v = tile.tiles[size_t(y) * tile.width + x];
            std::printf(" %4u.%-2u", unsigned(v >> 4), unsigned(v & 15));
        }
        std::printf("\n");
    }
    return 0;
}
