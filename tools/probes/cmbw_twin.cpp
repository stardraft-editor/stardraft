// The game's rects of one border that stand where the given rects of another border stand: each rect's shapes with the
// terrains swapped (corner by corner: terrain a for a2, b for b2), and the group whose rects are those, with the same
// stack links. To give a mod's brush set made like one border the groups of another (its stand-in's).
// usage: cmbw_twin <Cosmonarchy install> <tileset> <a> <b> <tileset 2> <a2> <b2> <group>...
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 9) return std::fprintf(stderr, "usage: cmbw_twin <install> <tileset> <a> <b> <tileset 2> <a2> <b2> <group>...\n"), 2;
    std::string err;
    GameData data;
    Tileset t1, t2;
    if (!cmbw::open(data, argv[1], err) || !t1.load(data, uint16_t(std::atoi(argv[2])), err) || !t2.load(data, uint16_t(std::atoi(argv[5])), err))
        return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const uint16_t a = uint16_t(std::atoi(argv[3])), b = uint16_t(std::atoi(argv[4])), a2 = uint16_t(std::atoi(argv[6])), b2 = uint16_t(std::atoi(argv[7]));
    const IsomTiles *i1 = IsomTiles::forTileset(t1), *i2 = IsomTiles::forTileset(t2);
    const IsomTerrainTable *m1 = IsomTerrainTable::forEra(t1.era), *m2 = IsomTerrainTable::forEra(t2.era);
    auto swap = [&](uint16_t s) -> int {
        auto c = m1->corners(s);
        if (!c[0]) return -1;
        for (auto& v : c) {
            if (v == a) v = a2;
            else if (v == b) v = b2;
            else return -1;
        }
        const uint16_t r = m2->shapeFor(c);
        return r ? int(r) : -1;
    };
    for (int k = 8; k < argc; ++k) {
        const uint16_t g = uint16_t(std::atoi(argv[k]));
        const auto& rs = i1->rectsOf(g);
        int found = -1, others = 0;
        for (size_t h = 0; h + 1 < t2.groups.size(); ++h) {
            if (i2->stackTop(uint16_t(h)) != i1->stackTop(g) || i2->stackBottom(uint16_t(h)) != i1->stackBottom(g)) continue;
            const auto& rs2 = i2->rectsOf(uint16_t(h));
            if (rs2.empty()) continue;
            bool all = !rs.empty();  // each of g's rects (in the border's terrains) is one of h's
            for (const auto& r : rs) {
                const int l = swap(r.left), rr = swap(r.right);
                if (l < 0 || rr < 0) continue;  // (a rect with another terrain: not compared)
                all = all && std::any_of(rs2.begin(), rs2.end(), [&](const IsomTiles::Rect& x) { return x.parity == r.parity && x.left == l && x.right == rr; });
            }
            if (!all) continue;
            if (found < 0) found = int(h);
            else ++others;
        }
        std::printf("%u -> %d%s\n", unsigned(g), found, others ? " (more)" : "");
    }
    return 0;
}
