// Learns the isometric terrain of each tile group Cosmonarchy BW's tilesets add (they have no isometric data in the
// .cv5: type and edges 0) from the mod's maps: where a map's TILE has a new group, its ISOM says which rect (the two
// diamonds' shapes) the mapper painted there, and so its terrains. The new groups are single tiles (either half of a
// rect), placed over a terrain and its borders: a group whose rects have one terrain in 80 % of its uses (8 or more)
// belongs to that terrain; when a second terrain is in 40 % of them or more, it is a border of the two.
// Only maps whose game tiles agree with their ISOM (90 % or more) count: their ISOM is up to date.
// Whole pieces too: a pair of the mod's tiles that the maps put in one game rect (8+ uses, 30 %+ of the pair's) is a piece
// of the mod's for that rect: the brush can pick it.
// usage: cmbw_isom_learn <maps folder> <mod tileset folder> [out .inc] [pieces out .inc] [pair votes out .txt]
// The pair votes: every pair of the mod's tiles in a rect and the game rects (left groups) the maps' ISOM has there, with
// their uses, no threshold: "tileset left right game uses" lines (tools/plugins/cmbw/probes/cmbw_auto_rows.cpp reads
// them).
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

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
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_isom_learn <maps folder> <mod tileset folder> [out .inc] [pieces out .inc]\n"), 2;
    // The game's group counts: the mod's groups from these on are new.
    const std::array<unsigned, 8> stock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};
    const char* files[8] = {"badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
    std::array<Tileset, 8> sets;
    std::array<const IsomTiles*, 8> tiles{};
    for (uint16_t e = 0; e < 8; ++e)
        if (loadCv5(fs::path(argv[2]) / (std::string(files[e]) + ".cv5"), e, sets[e])) tiles[e] = IsomTiles::forTileset(sets[e]);
    // Per tileset: new group -> (the game's group for its rect -> uses).
    std::array<std::map<uint16_t, std::map<uint16_t, int>>, 8> votes;
    std::array<std::map<uint32_t, std::map<uint16_t, int>>, 8> pairVotes;  // (left tile << 16 | right tile) -> game left group -> uses
    std::array<int, 8> mapsUsed{}, mapsSkipped{};
    std::array<std::set<uint16_t>, 8> lefts;
    std::array<uint64_t, 8> beyond{};  // ISOM values past the game's shapes, in the maps  // the game groups that are a rect's left tile
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
        // The rect's game group for tile (tx, ty) (-1: none fits).
        auto expected = [&](int tx, int ty) {
            const int rx = tx / 2;
            const auto& gs = t->groups((rx + ty) & 1, shape(rx, ty, 0), shape(rx, ty, 2));
            if (!gs.empty()) lefts[size_t(e)].insert(gs[0]);
            return gs.empty() ? -1 : int(gs[0]) + (tx & 1);
        };
        // ISOM shapes past the game's (a terrain of the mod's own): counted for the report.
        for (size_t i = 0; i + 1 < isom->data.size(); i += 2)
            if ((u16(isom->data, i) >> 4) >= t->shapeCount()) ++beyond[size_t(e)];
        int agree = 0, game = 0;
        std::map<uint16_t, std::map<uint16_t, int>> mine;
        std::map<uint32_t, std::map<uint16_t, int>> minePairs;  // a rect's two tiles of the mod's -> the game's left group there
        for (int ty = 0; ty < h; ++ty)
            for (int tx = 0; tx + 1 < w; tx += 2) {
                const uint16_t l = tile.tiles[size_t(ty) * w + tx], r = tile.tiles[size_t(ty) * w + tx + 1];
                const int want = expected(tx, ty);
                if (want >= 0 && (l >> 4) >= stock[size_t(e)] && (r >> 4) >= stock[size_t(e)]) ++minePairs[uint32_t(l) << 16 | r][uint16_t(want)];
            }
        for (int ty = 0; ty < h; ++ty)
            for (int tx = 0; tx < w; ++tx) {
                const uint16_t g = uint16_t(tile.tiles[size_t(ty) * w + tx] >> 4);
                const int want = expected(tx, ty);
                if (want < 0) continue;
                if (g < stock[size_t(e)]) {
                    if (t->rectsOf(uint16_t(g & ~1u)).empty()) continue;  // (doodads: no rule)
                    ++game;
                    agree += tileFromIsom(chk, tile, tx, ty, sets[size_t(e)]);
                } else {
                    ++mine[g][uint16_t(want)];
                }
            }
        if (game == 0 || agree * 10 < game * 9) {
            ++mapsSkipped[size_t(e)];
            continue;
        }
        ++mapsUsed[size_t(e)];
        for (const auto& [g, c] : mine)
            for (const auto& [want, n] : c) votes[size_t(e)][g][want] += n;
        for (const auto& [lr, c] : minePairs)
            for (const auto& [want, n] : c) pairVotes[size_t(e)][lr][want] += n;
    }
    FILE* out = argc > 3 ? std::fopen(argv[3], "wb") : nullptr;
    if (out)
        std::fprintf(out, "// Made by tools/plugins/cmbw/probes/cmbw_isom_learn.cpp from the mod's maps (CMBW-maps): {tileset, new group, its terrain, the\n"
                          "// other terrain of a border (0: none)} (isometric terrain ids, isom.cpp IsomTerrainTable). Do not edit by hand.\n");
    for (int e = 0; e < 8; ++e) {
        int learned = 0, seen = 0, few = 0, mixed = 0;
        // The terrains of each game group's rects (one: a rect of one terrain; two: a border).
        const auto terrains = sets[size_t(e)].groups.empty() ? std::vector<std::array<uint16_t, 2>>{} : tileGroupTerrains(sets[size_t(e)]);
        for (const auto& [g, c] : votes[size_t(e)]) {
            std::map<uint16_t, int> in;  // terrain -> the uses whose rect has it
            int total = 0;
            for (const auto& [want, n] : c) {
                if (want >= terrains.size() || terrains[want][0] == 0) continue;
                total += n;
                in[terrains[want][0]] += n;
                if (terrains[want][1]) in[terrains[want][1]] += n;
            }
            ++seen;
            if (total < 8) { ++few; continue; }
            std::vector<std::pair<int, uint16_t>> ranked;
            for (const auto& [t, n] : in) ranked.push_back({n, t});
            std::sort(ranked.rbegin(), ranked.rend());
            if (ranked.empty() || ranked[0].first * 10 < total * 8) { ++mixed; continue; }
            const uint16_t other = ranked.size() > 1 && ranked[1].first * 10 >= total * 4 ? ranked[1].second : 0;
            ++learned;
            if (out) std::fprintf(out, "{%d, %u, %u, %u},\n", e, unsigned(g), unsigned(ranked[0].second), unsigned(other));
        }
        std::printf("%-9s maps used %3d (skipped %3d: ISOM out of date); new groups in rects %4d, learned %4d (few uses %d, mixed %d)\n", files[e],
                    mapsUsed[size_t(e)], mapsSkipped[size_t(e)], seen, learned, few, mixed);
    }
    if (out) std::fclose(out);
    // The pieces: a pair of the mod's tiles (left, right) that the maps put in a rect, for each game rect (its left
    // group) with 8+ of the pair's uses and 30 %+ of them (the mod packs its pieces as subtiles: group 2279's subtiles
    // 0 and 1 are a piece, 2 and 3 another). The look filter of the brush checks each against the game's piece.
    FILE* pieces = argc > 4 ? std::fopen(argv[4], "wb") : nullptr;
    if (pieces)
        std::fprintf(pieces, "// Made by tools/plugins/cmbw/probes/cmbw_isom_learn.cpp from the mod's maps (CMBW-maps): {tileset, the mod's left tile, its right\n"
                             "// tile (tile values: group * 16 + subtile), the game's left tile group of the rect it is a piece for}. Do not edit by hand.\n");
    for (int e = 0; e < 8; ++e) {
        const auto terrains = sets[size_t(e)].groups.empty() ? std::vector<std::array<uint16_t, 2>>{} : tileGroupTerrains(sets[size_t(e)]);
        int n = 0, borders = 0;
        std::set<uint16_t> rects;
        for (const auto& [lr, c] : pairVotes[size_t(e)]) {
            int total = 0;
            for (const auto& [want, k] : c) total += k;
            for (const auto& [want, k] : c) {
                if (k < 8 || k * 10 < total * 3 || !lefts[size_t(e)].count(want)) continue;
                // (only the mod's two layouts: the next subtile, or the next group's; other pairs are single tiles side by side)
                const uint16_t l = uint16_t(lr >> 16), r = uint16_t(lr);
                if (!(r == l + 1 && !(l & 1)) && r != l + 16) continue;
                ++n, rects.insert(want);
                if (want < terrains.size() && terrains[want][1]) ++borders;
                if (pieces) std::fprintf(pieces, "{%d, %u, %u, %u},\n", e, unsigned(lr >> 16), unsigned(lr & 0xffff), unsigned(want));
            }
        }
        std::printf("%-9s pieces: %d (borders %d) for %zu game rects; ISOM values past the game's shapes: %llu\n", files[e], n, borders, rects.size(),
                    (unsigned long long)beyond[size_t(e)]);
    }
    if (pieces) std::fclose(pieces);
    if (FILE* votesOut = argc > 5 ? std::fopen(argv[5], "wb") : nullptr) {
        for (int e = 0; e < 8; ++e)
            for (const auto& [lr, c] : pairVotes[size_t(e)])
                for (const auto& [want, k] : c) std::fprintf(votesOut, "%d %u %u %u %d\n", e, unsigned(lr >> 16), unsigned(lr & 0xffff), unsigned(want), k);
        std::fclose(votesOut);
    }
    return 0;
}
