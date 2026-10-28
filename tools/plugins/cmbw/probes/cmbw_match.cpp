// Matches the pieces of one of the mod's terrain brush sets (its SCMDraft brush file) to the game's isometric rects of a
// border: each brush row (a rect: two tiles at an even column) gets the game rect whose shape fits it best.
// The mod's side: each minitile is the fill's (F), the ground's (G) or neither (from the solid groups of two terrains, or
// given fill groups). The game's side: a point of a rect has the terrain of the nearest ISOM corner (the rect's two
// diamonds' corners, from their shapes), so each game rect of the border has an exact high/low mask.
// usage: cmbw_match <Cosmonarchy install> <tileset> <set name text> <border terrain high> <border terrain low>
//                   <fill terrain> <ground terrain> [fill group...]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain_stamp.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 8) return std::fprintf(stderr, "usage: cmbw_match <install> <tileset> <set> <high> <low> <fill terrain> <ground terrain> [fill group...]\n"), 2;
    const uint16_t era = uint16_t(std::atoi(argv[2]));
    const std::string text = argv[3];
    const uint16_t high = uint16_t(std::atoi(argv[4])), low = uint16_t(std::atoi(argv[5]));
    const int fillTerrain = std::atoi(argv[6]), groundTerrain = std::atoi(argv[7]);
    std::string err;
    GameData data;
    Tileset ts;
    if (!cmbw::open(data, argv[1], err) || !ts.load(data, era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const IsomTiles* tiles = IsomTiles::forTileset(ts);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    const auto terrains = tileGroupTerrains(ts);
    auto mini = [&](uint16_t value, int k) -> uint32_t {
        const int m = ts.megatileFor(value);
        return m >= 0 ? ts.megatiles[size_t(m)][size_t(k)] >> 1 : 0;
    };
    std::set<uint32_t> fill, ground;
    auto add = [&](std::set<uint32_t>& to, size_t g) {
        for (int k = 0; k < 16; ++k)
            if (ts.groups[g].megatiles[size_t(k)])
                for (int i = 0; i < 16; ++i) to.insert(mini(uint16_t(g << 4 | k), i));
    };
    for (size_t g = 0; g < terrains.size() && g < ts.groups.size(); ++g) {
        if (terrains[g][1] == 0 && terrains[g][0] == fillTerrain) add(fill, g);
        if (terrains[g][1] == 0 && terrains[g][0] == groundTerrain) add(ground, g);
    }
    for (int i = 8; i < argc; ++i) add(fill, size_t(std::atoi(argv[i])));
    for (uint32_t m : fill) ground.erase(m);
    // A rect's mask: 8 x 4 minitiles, +1 fill (high), -1 ground (low), 0 neither.
    using Mask = std::array<int, 32>;
    auto modMask = [&](uint16_t l, uint16_t r) {
        Mask m{};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x) {
                const uint32_t e = mini(x < 4 ? l : r, y * 4 + x % 4);
                m[size_t(y * 8 + x)] = fill.count(e) ? 1 : ground.count(e) ? -1 : 0;
            }
        return m;
    };
    // The game's rects of the border, each with its mask (+1 high, -1 low) from its first rect shape.
    struct GameRect {
        uint16_t group;
        Mask mask;
        int parity;
        uint16_t left, right;
    };
    std::vector<GameRect> game;
    for (size_t g = 0; g + 1 < terrains.size(); ++g) {
        if (!terrains[g][1] || std::minmax(terrains[g][0], terrains[g][1]) != std::minmax(high, low)) continue;
        const auto& rs = tiles->rectsOf(uint16_t(g));
        if (rs.empty()) continue;
        const auto& r = rs[0];
        // Corners in rect-grid coordinates around the rect (0..1 x 0..1): the two diamonds' corners.
        // Even parity: diamonds at (0,0) and (1,1); odd: (0,1) and (1,0). corners(): left, top, right, bottom.
        std::vector<std::array<double, 3>> cs;  // x, y, terrain
        auto diamond = [&](double cx, double cy, uint16_t s) {
            const auto c = table->corners(s);
            cs.push_back({cx - 1, cy, double(c[0])}), cs.push_back({cx, cy - 1, double(c[1])});
            cs.push_back({cx + 1, cy, double(c[2])}), cs.push_back({cx, cy + 1, double(c[3])});
        };
        if (r.parity == 0) diamond(0, 0, r.left), diamond(1, 1, r.right);
        else diamond(0, 1, r.left), diamond(1, 0, r.right);
        GameRect gr{uint16_t(g), {}, r.parity, r.left, r.right};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x) {
                const double px = (x + 0.5) / 8, py = (y + 0.5) / 4;
                double best = 1e9, t = 0;
                for (const auto& c : cs) {
                    const double d = (c[0] - px) * (c[0] - px) + (c[1] - py) * (c[1] - py);
                    if (d < best - 1e-9) best = d, t = c[2];
                }
                gr.mask[size_t(y * 8 + x)] = t == high ? 1 : t == low ? -1 : 0;
            }
        game.push_back(gr);
    }
    std::printf("%zu game rects of %s / %s\n", game.size(), table->terrainName(high), table->terrainName(low));
    auto show = [](const Mask& m, int y) {
        std::string s;
        for (int x = 0; x < 8; ++x) s += m[size_t(y * 8 + x)] > 0 ? 'F' : m[size_t(y * 8 + x)] < 0 ? 'G' : '.';
        return s;
    };
    for (const TerrainStamp& st : cmbw::brushStamps(argv[1], era)) {
        if (st.name.find(text) == std::string::npos) continue;
        std::printf("%s (%dx%d)\n", st.name.c_str(), st.width, st.height);
        for (int row = 0; row < st.height; ++row) {
            const uint16_t l = st.tiles[size_t(row) * st.width], r = st.width > 1 ? st.tiles[size_t(row) * st.width + 1] : l;
            const Mask m = modMask(l, r);
            int known = 0;
            for (int v : m) known += v != 0;
            std::vector<std::pair<double, size_t>> scores;
            for (size_t i = 0; i < game.size(); ++i) {
                int agree = 0, n = 0;
                for (size_t k = 0; k < 32; ++k)
                    if (m[k] && game[i].mask[k]) ++n, agree += m[k] == game[i].mask[k];
                scores.push_back({n ? double(agree) / 32 + 0.001 * n : 0, i});
            }
            std::sort(scores.rbegin(), scores.rend());
            std::printf("  row %d %4u.%-2u %4u.%-2u known %2d:", row, unsigned(l >> 4), unsigned(l & 15), unsigned(r >> 4), unsigned(r & 15), known);
            for (size_t i = 0; i < 3 && i < scores.size(); ++i) std::printf("  %u (%.2f)", unsigned(game[scores[i].second].group), scores[i].first);
            std::printf("\n");
            if (std::getenv("CMBW_SHOW"))
                for (int y = 0; y < 4; ++y)
                    std::printf("      %s   %s\n", show(m, y).c_str(), scores.empty() ? "" : show(game[scores[0].second].mask, y).c_str());
        }
    }
    return 0;
}
