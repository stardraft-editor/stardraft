// Small isometric shapes of a terrain on its ground and the tile groups of a range each uses (each rect's left group):
// random unions of a few diamonds in the middle of a new map, painted with the editor's brush. Prints one line for each
// shape that uses a group no smaller shape used before, or uses as many groups in a smaller box:
//   groups <g,g,...> box <w>x<h> strokes <dx,dy,size;...>
// (strokes from the map's middle, as cmbw_paint_one's PAINT_STROKES). A set cover of these lines gives the smallest
// set of shapes that uses every group (research/terrain-builder: min_isom.py).
// usage: cmbw_min_isom <StarCraft install> <tileset> <terrain> <ground> <first group> <last group> <tries> [seed] [reach] [strokes]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 8) return std::fprintf(stderr, "usage: cmbw_min_isom <install> <tileset> <terrain> <ground> <first> <last> <tries> [seed] [reach] [strokes]\n"), 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const uint16_t era = uint16_t(std::atoi(argv[2])), terrain = uint16_t(std::atoi(argv[3])), ground = uint16_t(std::atoi(argv[4]));
    const int first = std::atoi(argv[5]), last = std::atoi(argv[6]), tries = std::atoi(argv[7]);
    std::mt19937 rng(argc > 8 ? unsigned(std::atoi(argv[8])) : 1u);
    const int reach = argc > 9 ? std::atoi(argv[9]) : 3, most = argc > 10 ? std::atoi(argv[10]) : 4;
    std::map<int, long> smallest;  // group -> the smallest box that used it
    std::map<std::string, long> sets;  // a group set -> its smallest box
    const int N = 40;
    for (int t = 0; t < tries; ++t) {
        Editor e(data);
        if (!e.newMap(N, N, era, ground, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const int cx = N * 16, cy = N * 16, k = 1 + int(rng() % unsigned(most));
        std::string strokes;
        for (int i = 0; i < k; ++i) {
            const int dx = (int(rng() % unsigned(2 * reach + 1)) - reach) * 64, dy = (int(rng() % unsigned(2 * reach + 1)) - reach) * 32;
            const int size = 1 + int(rng() % 2);
            e.paintBrush(cx + dx, cy + dy, terrain, Brush{Brush::Diamond, float(size)});
            strokes += (i ? ";" : "") + std::to_string(dx) + "," + std::to_string(dy) + "," + std::to_string(size);
        }
        const TerrainLayer l = readTerrain(e.chk(), "TILE");
        std::set<int> have;
        int x0 = N, y0 = N, x1 = -1, y1 = -1;
        for (int y = 0; y < l.height; ++y)
            for (int x = 0; x < l.width; ++x) {
                const int g = l.tiles[size_t(y) * l.width + x] >> 4;
                if (g >= first && g <= last + 1) {
                    if (!(x & 1) && !(g & 1)) have.insert(g);
                    x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                }
            }
        if (have.empty()) continue;
        const long area = long(x1 - x0 + 1) * (y1 - y0 + 1);
        std::string key;
        for (int g : have) key += (key.empty() ? "" : ",") + std::to_string(g);
        bool news = false;
        for (int g : have)
            if (!smallest.count(g) || area < smallest[g]) smallest[g] = area, news = true;
        if (!sets.count(key) || area < sets[key]) sets[key] = area, news = true;
        if (news) std::printf("groups %s box %dx%d strokes %s\n", key.c_str(), x1 - x0 + 1, y1 - y0 + 1, strokes.c_str()), std::fflush(stdout);
    }
    std::printf("seen");
    for (auto [g, a] : smallest) std::printf(" %d", g);
    std::printf("\n");
    return 0;
}
