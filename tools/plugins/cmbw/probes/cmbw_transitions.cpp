// How the mod's maps build the game's terrain borders (transitions): for each tile of a border rect (the ISOM's two
// terrains), the tile groups the maps put there, the game's or the mod's. Exploration for item 4.
// usage: cmbw_transitions <maps folder> <mod tileset folder> [tileset terrain terrain: each tile of that border]
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <cstdlib>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

uint16_t u16(const Bytes& b, size_t o) { return uint16_t(b[o] | b[o + 1] << 8); }

bool loadCv5(const fs::path& file, uint16_t era, Tileset& ts) {
    std::ifstream in(file, std::ios::binary);
    const Bytes cv5((std::istreambuf_iterator<char>(in)), {});
    if (cv5.empty() || cv5.size() % 52) return false;
    ts.era = era;
    ts.groups.resize(cv5.size() / 52);
    for (size_t i = 0; i < ts.groups.size(); ++i) {
        const size_t o = i * 52;
        TileGroup& g = ts.groups[i];
        g.type = u16(cv5, o), g.buildability = cv5[o + 2], g.groundHeight = cv5[o + 3];
        for (int k = 0; k < 4; ++k) g.edges[size_t(k)] = u16(cv5, o + 4 + 2 * k), g.extra[size_t(k)] = u16(cv5, o + 12 + 2 * k);
        for (int k = 0; k < 16; ++k) g.megatiles[size_t(k)] = u16(cv5, o + 20 + 2 * k);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_transitions <maps folder> <mod tileset folder>\n"), 2;
    const std::array<unsigned, 8> stock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};
    const char* files[8] = {"badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
    std::array<Tileset, 8> sets;
    std::array<const IsomTiles*, 8> tiles{};
    for (uint16_t e = 0; e < 8; ++e)
        if (loadCv5(fs::path(argv[2]) / (std::string(files[e]) + ".cv5"), e, sets[e])) tiles[e] = IsomTiles::forTileset(sets[e]);
    struct Use { int total = 0, game = 0; std::map<uint16_t, int> mod; };
    std::array<std::map<uint16_t, Use>, 8> uses;  // want tile group -> what the maps put there
    // CMBW_GROUP=<group>: where that group's tiles (subtile) go.
    const char* watchEnv = std::getenv("CMBW_GROUP");
    const int watch = watchEnv ? std::atoi(watchEnv) : -1;
    std::map<std::pair<uint16_t, uint16_t>, int> watched;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[1], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        std::string err;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* era = chk.find("ERA ");
        const Section* isom = chk.find("ISOM");
        if (!era || era->data.size() < 2 || !isom) continue;
        const int e = (era->data[0] | era->data[1] << 8) & 7;
        const IsomTiles* t = tiles[size_t(e)];
        if (!t) continue;
        const TerrainLayer tile = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
        const int w = tile.width, h = tile.height, rectCols = w / 2 + 1;
        if (isom->data.size() < size_t(rectCols) * (h + 1) * 8) continue;
        auto shape = [&](int rx, int ty, int k) { return uint16_t(u16(isom->data, ((size_t(ty) * rectCols + rx) * 4 + size_t(k)) * 2) >> 4); };
        auto expected = [&](int tx, int ty) {
            const int rx = tx / 2;
            const auto& gs = t->groups((rx + ty) & 1, shape(rx, ty, 0), shape(rx, ty, 2));
            return gs.empty() ? -1 : int(gs[0]) + (tx & 1);
        };
        int agree = 0, game = 0;
        std::map<uint16_t, Use> mine;
        for (int ty = 0; ty < h; ++ty)
            for (int tx = 0; tx < w; ++tx) {
                const uint16_t g = uint16_t(tile.tiles[size_t(ty) * w + tx] >> 4);
                const int want = expected(tx, ty);
                if (want < 0) continue;
                Use& u = mine[uint16_t(want)];
                ++u.total;
                if (g < stock[size_t(e)]) {
                    if (t->rectsOf(uint16_t(g & ~1u)).empty()) { --u.total; continue; }
                    ++game, ++u.game;
                    agree += tileFromIsom(chk, tile, tx, ty, sets[size_t(e)]);
                } else {
                    ++u.mod[g];
                    if (g == watch) ++watched[{uint16_t(want), uint16_t(tile.tiles[size_t(ty) * w + tx] & 15)}];
                }
            }
        if (game == 0 || agree * 10 < game * 9) continue;
        for (auto& [want, u] : mine) {
            Use& all = uses[size_t(e)][want];
            all.total += u.total, all.game += u.game;
            for (auto& [g, n] : u.mod) all.mod[g] += n;
        }
    }
    // A mod group -> the uses of it in each game tile group's place (all places, for how specific it is).
    std::array<std::map<uint16_t, std::map<uint16_t, int>>, 8> placesOf;
    for (int e = 0; e < 8; ++e)
        for (auto& [want, u] : uses[size_t(e)])
            for (auto& [g, n] : u.mod) placesOf[size_t(e)][g][want] += n;
    for (auto& [k, n] : watched)
        if (n >= 3) std::printf("group %d subtile %2u in place of tile group %4u: %d\n", watch, unsigned(k.second), unsigned(k.first), n);
    if (argc > 5) {
        const int e = std::atoi(argv[3]);
        const auto ab = std::minmax(uint16_t(std::atoi(argv[4])), uint16_t(std::atoi(argv[5])));
        const auto terrains = tileGroupTerrains(sets[size_t(e)]);
        for (auto& [want, u] : uses[size_t(e)]) {
            if (want >= terrains.size() || std::minmax(terrains[want][0], terrains[want][1]) != ab) continue;
            int mod = 0;
            std::vector<std::pair<int, uint16_t>> ranked;
            for (auto& [g, n] : u.mod) mod += n, ranked.push_back({n, g});
            std::sort(ranked.rbegin(), ranked.rend());
            std::printf("tile group %4u (%s): uses %6d, game %6d, mod %5d in %3zu groups:", unsigned(want), (want & 1) ? "R" : "L", u.total, u.game, mod,
                        ranked.size());
            for (size_t i = 0; i < ranked.size() && i < 6; ++i) {
                int all = 0;
                for (auto& [w2, n] : placesOf[size_t(e)][ranked[i].second]) all += n;
                std::printf(" %u x%d (%d%% here)", unsigned(ranked[i].second), ranked[i].first, 100 * ranked[i].first / std::max(1, all));
            }
            std::printf("\n");
        }
        return 0;
    }
    for (int e = 0; e < 8; ++e) {
        if (sets[size_t(e)].groups.empty()) continue;
        const auto terrains = tileGroupTerrains(sets[size_t(e)]);
        const IsomTerrainTable* tt = IsomTerrainTable::forEra(uint16_t(e));
        struct Pair { int total = 0, game = 0, mod = 0, wants = 0, dominated = 0; };
        std::map<std::pair<uint16_t, uint16_t>, Pair> pairs;
        for (auto& [want, u] : uses[size_t(e)]) {
            if (want >= terrains.size() || !terrains[want][0]) continue;
            const auto key = std::minmax(terrains[want][0], terrains[want][1]);
            Pair& p = pairs[{key.first, key.second}];
            p.total += u.total, p.game += u.game, ++p.wants;
            int best = 0;
            for (auto& [g, n] : u.mod) p.mod += n, best = std::max(best, n);
            if (u.total >= 8 && best * 2 >= u.total) ++p.dominated;
        }
        std::printf("== %s\n", files[e]);
        for (auto& [k, p] : pairs)
            if (p.total >= 100)
                std::printf("  %-22s / %-22s tiles %7d  mod %5.1f%%  tile groups %4d, a mod group in half+ of the uses: %d\n", tt->terrainName(k.first),
                            k.second ? tt->terrainName(k.second) : "(alone)", p.total, 100.0 * p.mod / p.total, p.wants, p.dominated);
    }
    return 0;
}
