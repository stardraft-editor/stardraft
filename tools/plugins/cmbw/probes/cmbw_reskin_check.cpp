// The re-skin idea, tested: a mod terrain is a game border with new art, so each of its tiles should have the minitile
// flags (.vf4: walkable, height) of the game tile whose rect it stands in. For each mod terrain: how many of its pieces'
// tiles have the game tile's walkability exactly (16 of 16 minitiles), how many also the heights, and for the ones
// that do not, whether another rect of the template has exactly their walkability (a piece in the wrong rect). And the
// game border of any tileset whose walkability patterns cover the most of the terrain's tiles (the source of a
// re-skin, when it is not the template).
// usage: cmbw_reskin_check <Cosmonarchy install> [era]
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {
const char* kEraName[8] = {"Badlands", "Platform", "Install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};

// A tile's minitile flags under a mask, as a string key ("" when the tile has no megatile).
std::string sig(const Tileset& ts, uint16_t tile, uint16_t mask) {
    const int m = ts.megatileFor(tile);
    if (m <= 0 || size_t(m) >= ts.minitileFlags.size()) return {};
    std::string s(16, '0');
    for (size_t i = 0; i < 16; ++i) s[i] = char('0' + (ts.minitileFlags[size_t(m)][i] & mask));
    return s;
}
struct Border {
    int era;
    uint16_t ground, terrain;
    std::set<std::string> kinds;
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_reskin_check <install> [era]\n"), 2;
    const int only = argc > 2 ? std::atoi(argv[2]) : -1;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    // Every game border of every tileset: its walkability patterns.
    std::vector<Border> borders;
    for (int e = 0; e < 8; ++e) {
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
        const Tileset& ts = ed.tileset();
        for (uint16_t g = 1; g < 40; ++g)
            for (uint16_t t = 1; t < 40; ++t) {
                if (g == t) continue;
                const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, g, t);
                if (rects.size() < 8) continue;
                Border b{e, g, t, {}};
                for (const tr::TemplateRect& r : rects)
                    for (uint16_t half = 0; half < 2; ++half) b.kinds.insert(std::to_string(half) + sig(ts, tr::gameTile(ts, uint32_t(r.group) + half), 1));
                borders.push_back(b);
            }
    }
    std::printf("%zu game borders in 8 tilesets\n", borders.size());
    std::printf("%-9s %-44s %5s %5s | %6s %6s | %5s %5s | %s\n", "tileset", "terrain", "tiles", "kinds", "walk=", "all=", "moved", "none",
                "best border anywhere: cover (own cover)");
    long long bestSum = 0, ownSum = 0;
    int totTiles = 0, totWalk = 0, totAll = 0, totMoved = 0, totNone = 0, terrains = 0, clean = 0;
    for (int e = 0; e < 8; ++e) {
        if (only >= 0 && e != only) continue;
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
        const Tileset& ts = ed.tileset();
        for (const Tileset::ModTerrain& m : ts.modTerrains) {
            const auto [ground, terrain] = tr::templateOf(ts, m);
            const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, ground, terrain);
            std::set<std::string> kinds;
            std::set<uint16_t> groups;
            for (const tr::TemplateRect& r : rects) {
                groups.insert(r.group);
                for (uint16_t half = 0; half < 2; ++half) kinds.insert(std::to_string(half) + sig(ts, tr::gameTile(ts, uint32_t(r.group) + half), 1));
            }
            int tiles = 0, walk = 0, all = 0, moved = 0, none = 0;
            std::vector<std::string> mineKinds;
            for (const auto& p : m.pieces) {
                if (!groups.count(p[0])) continue;
                for (uint16_t half = 0; half < 2; ++half) {
                    const uint16_t mine = p[1 + half];
                    if (tr::nullHalf(mine)) continue;
                    const uint16_t game = tr::gameTile(ts, uint32_t(p[0]) + half);
                    const std::string a = sig(ts, mine, 1), b = sig(ts, game, 1);
                    if (a.empty() || b.empty()) continue;
                    ++tiles;
                    mineKinds.push_back(std::to_string(half) + a);
                    if (a == b) {
                        ++walk;
                        all += sig(ts, mine, 0x07) == sig(ts, game, 0x07);
                    } else if (kinds.count(std::to_string(half) + a)) {
                        ++moved;
                    } else {
                        ++none;
                    }
                }
            }
            int bestCover = -1;
            const Border* best = nullptr;
            for (const Border& b : borders) {
                int cover = 0;
                for (const std::string& k : mineKinds) cover += int(b.kinds.count(k));
                if (cover > bestCover) bestCover = cover, best = &b;
            }
            if (bestCover < 0) bestCover = 0;
            ++terrains, clean += tiles && walk == tiles;
            totTiles += tiles, totWalk += walk, totAll += all, totMoved += moved, totNone += none;
            bestSum += bestCover, ownSum += walk + moved;
            auto pct = [](int n, int d) { return d ? 100.0 * n / d : 0.0; };
            std::printf("%-9s %-44.44s %5d %5zu | %5.1f%% %5.1f%% | %5d %5d | %s %u/%u %.0f%% (own %.0f%%)\n", kEraName[e], m.name.c_str(), tiles, kinds.size(),
                        pct(walk, tiles), pct(all, tiles), moved, none, best ? kEraName[best->era] : "-", unsigned(best ? best->ground : 0),
                        unsigned(best ? best->terrain : 0), pct(bestCover, tiles), pct(walk + moved, tiles));
        }
    }
    std::printf("\nall: %d terrains, %d with every tile's walkability as the game's; tiles %d: walk= %.1f%%, all= %.1f%%, moved %d, none %d\n", terrains, clean,
                totTiles, totTiles ? 100.0 * totWalk / totTiles : 0.0, totTiles ? 100.0 * totAll / totTiles : 0.0, totMoved, totNone);
    std::printf("cover of the tiles' patterns: own template %.1f%%, the best border anywhere %.1f%%\n", totTiles ? 100.0 * double(ownSum) / totTiles : 0.0,
                totTiles ? 100.0 * double(bestSum) / totTiles : 0.0);
    std::printf("(kinds: the template's different walkability patterns; walk=: the tile's walkable minitiles are the game tile's; all=: and the heights;\n"
                " moved: not the game tile's, but another rect of the template has exactly this pattern; none: no rect has it)\n");
    return 0;
}
