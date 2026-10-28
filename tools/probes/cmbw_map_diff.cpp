// Two versions of a map (before and after another editor changed it): the tiles that differ, each with its old and new
// value, whether either is a mod terrain's piece, and the ISOM terrain there in each, as StarDraft opens them on a
// Cosmonarchy tab.
// usage: cmbw_map_diff <Cosmonarchy install> <map before> <map after>
#include <cstdio>
#include <set>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_map_diff <install> <before> <after>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor a(data), b(data);
    if (!a.open(argv[2], err) || !b.open(argv[3], err)) return std::fprintf(stderr, "open: %s\n", err.c_str()), 1;
    std::set<uint16_t> mine;
    for (const auto& m : a.tileset().modTerrains)
        for (const auto& p : m.pieces) mine.insert(p[1]), mine.insert(p[2]);
    const TerrainLayer ta = readTerrain(a.chk(), "TILE"), tb = readTerrain(b.chk(), "TILE");
    int n = 0;
    for (size_t k = 0; k < ta.tiles.size() && k < tb.tiles.size(); ++k) {
        if (ta.tiles[k] == tb.tiles[k]) continue;
        const int x = int(k % ta.width), y = int(k / ta.width);
        if (++n <= 80)
            std::printf("%d,%d: %u.%u%s -> %u.%u%s  isom %u -> %u\n", x, y, unsigned(ta.tiles[k] >> 4), unsigned(ta.tiles[k] & 15),
                        mine.count(ta.tiles[k]) ? " (mod)" : "", unsigned(tb.tiles[k] >> 4), unsigned(tb.tiles[k] & 15),
                        mine.count(tb.tiles[k]) ? " (mod)" : "", unsigned(a.terrainAt(x * 32, y * 32)), unsigned(b.terrainAt(x * 32, y * 32)));
    }
    std::printf("tiles changed: %d\n", n);
    return 0;
}
