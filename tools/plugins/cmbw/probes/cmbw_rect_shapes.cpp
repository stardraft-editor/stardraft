// The isometric rects (parity, left and right diamond shapes, and the shapes' corner terrains) of a range of a
// tileset's tile groups, and their stack links: to see which rects of a border repeat a shape of another.
// usage: cmbw_rect_shapes <Cosmonarchy install> <tileset> <first group> <last group>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_rect_shapes <install> <tileset> <first group> <last group>\n"), 2;
    std::string err;
    GameData data;
    Tileset ts;
    if (!cmbw::open(data, argv[1], err) || !ts.load(data, uint16_t(std::atoi(argv[2])), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const IsomTiles* tiles = IsomTiles::forTileset(ts);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(ts.era);
    for (int g = std::atoi(argv[3]); g <= std::atoi(argv[4]); ++g) {
        std::printf("group %4d stack top %u bottom %u:", g, unsigned(tiles->stackTop(uint16_t(g))), unsigned(tiles->stackBottom(uint16_t(g))));
        for (const auto& r : tiles->rectsOf(uint16_t(g))) {
            const auto a = table->corners(r.left), b = table->corners(r.right);
            std::printf("  [p%d %u(%u%u%u%u) %u(%u%u%u%u)]", r.parity, unsigned(r.left), a[0], a[1], a[2], a[3], unsigned(r.right), b[0], b[1], b[2], b[3]);
        }
        std::printf("\n");
    }
    return 0;
}
