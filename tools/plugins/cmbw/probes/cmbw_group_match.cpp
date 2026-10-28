// Which game tiles the mod's tiles look like: for each tile of the mod's groups (each subtile that has a megatile), the
// game tiles of the given groups whose picture is most alike (the correlation of their gray levels, the means taken
// off: a recolored copy of a game tile scores near 1). For finding the game rect a mod piece is made after when no brush
// or map tells it (the shapes a brush set leaves out).
// usage: cmbw_group_match <Cosmonarchy install> <a map of the tileset> <mod first-last> <game first-last> [best=3]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/previews.h"
#include "stardraft/tileset.h"

using namespace stardraft;

namespace {

bool range(const char* s, int& a, int& b) { return std::sscanf(s, "%d-%d", &a, &b) == 2 && a <= b; }

// A tile's gray levels, the mean taken off, scaled to length 1 (empty: the tile has no picture).
std::vector<double> grays(const Tileset& ts, uint16_t value) {
    if (ts.megatileFor(value) <= 0) return {};
    const Sprite s = mapTilesPicture(ts, {value}, 1, 0, 0, 1, 1);
    if (s.width <= 0 || s.height <= 0) return {};
    std::vector<double> g(size_t(s.width) * s.height);
    double mean = 0;
    for (size_t i = 0; i < g.size(); ++i) mean += g[i] = 0.3 * s.rgba[i * 4] + 0.59 * s.rgba[i * 4 + 1] + 0.11 * s.rgba[i * 4 + 2];
    mean /= double(g.size());
    double len = 0;
    for (double& v : g) v -= mean, len += v * v;
    if (len <= 1e-9) return {};
    for (double& v : g) v /= std::sqrt(len);
    return g;
}

}  // namespace

int main(int argc, char** argv) {
    int m0, m1, g0, g1;
    if (argc < 5 || !range(argv[3], m0, m1) || !range(argv[4], g0, g1))
        return std::fprintf(stderr, "usage: cmbw_group_match <install> <map> <mod first-last> <game first-last> [best]\n"), 2;
    const int best = argc > 5 ? std::max(1, std::atoi(argv[5])) : 3;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const Tileset& ts = e.tileset();
    struct Tile {
        uint16_t value;
        std::vector<double> g;
    };
    std::vector<Tile> game;
    for (int h = g0; h <= g1; ++h)
        for (int k = 0; k < 16; ++k)
            if (auto g = grays(ts, uint16_t(h << 4 | k)); !g.empty()) game.push_back({uint16_t(h << 4 | k), std::move(g)});
    for (int m = m0; m <= m1; ++m)
        for (int k = 0; k < 16; ++k) {
            const std::vector<double> g = grays(ts, uint16_t(m << 4 | k));
            if (g.empty()) continue;
            std::vector<std::pair<double, uint16_t>> scores;
            for (const Tile& t : game)
                if (t.g.size() == g.size()) {
                    double c = 0;
                    for (size_t i = 0; i < g.size(); ++i) c += g[i] * t.g[i];
                    scores.push_back({c, t.value});
                }
            std::sort(scores.rbegin(), scores.rend());
            std::printf("%4d.%-2d", m, k);
            for (int i = 0; i < best && i < int(scores.size()); ++i)
                std::printf("  %4u.%-2u %.2f", unsigned(scores[size_t(i)].second >> 4), unsigned(scores[size_t(i)].second & 15), scores[size_t(i)].first);
            std::printf("\n");
        }
    return 0;
}
