// Makes terrains of the mod's brush sets from its maps, for the sets that are not terrains yet: each rect of a set's
// brushes (its left and right tile) is a piece for the game rect that the maps' ISOM has under it most (the pair votes
// of cmbw_isom_learn: 2 or more uses, half of them or more); the set's border is the game border most of its edge
// pieces are for (its two terrains), its own terrain the one its solid pieces are of (its fill), the other its ground.
// Prints each set's result; with an out file, writes the rows (src/mods/cmbw_auto_terrains.inc).
// usage: cmbw_auto_rows <Cosmonarchy install> <pair votes .txt> [out .inc]
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain_stamp.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_auto_rows <install> <pair votes .txt> [out .inc]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::map<std::tuple<int, uint16_t, uint16_t>, std::map<uint16_t, int>> votes;  // (era, left, right) -> game group -> uses
    {
        std::ifstream in(argv[2]);
        int e, k;
        unsigned l, r, g;
        while (in >> e >> l >> r >> g >> k) votes[{e, uint16_t(l), uint16_t(r)}][uint16_t(g)] += k;
    }
    FILE* out = argc > 3 ? std::fopen(argv[3], "wb") : nullptr;
    if (out)
        std::fprintf(out, "// Made by tools/plugins/cmbw/probes/cmbw_auto_rows.cpp from the mod's maps and brush sets: the sets that are not hand-made rows of\n"
                          "// kModTerrainSets. AUTO(tileset, \"name\", stand-in, ground, {{game left group, mod left tile, mod right tile}, ...}).\n"
                          "// Do not edit by hand.\n");
    int made = 0, all = 0;
    for (uint16_t era = 0; era < 8; ++era) {
        Tileset t;
        if (!t.load(data, era, err)) continue;
        const auto terrains = tileGroupTerrains(t);
        // A tile's mean color (its pixels), to keep only the pieces that look like the game rect they stand for.
        auto mean = [&](uint16_t value) -> std::array<double, 3> {
            const int m = t.megatileFor(value);
            std::array<double, 3> c{-1, -1, -1};
            if (m < 0) return c;
            double sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (uint32_t e : t.megatiles[size_t(m)])
                if ((e >> 1) < t.minitiles.size())
                    for (uint8_t px : t.minitiles[e >> 1]) sr += t.palette[px].r, sg += t.palette[px].g, sb += t.palette[px].b, ++n;
            if (n) c = {sr / n, sg / n, sb / n};
            return c;
        };
        auto looksLike = [&](uint16_t mod, uint16_t gameGroup) {  // (within 24 of one of the game group's first 4 subtiles)
            const auto a = mean(mod);
            if (a[0] < 0) return false;
            for (uint16_t k = 0; k < 4; ++k) {
                const auto b = mean(uint16_t(gameGroup << 4 | k));
                if (b[0] < 0) continue;
                const double d = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
                if (d < 24.0 * 24.0) return true;
            }
            return false;
        };
        const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
        std::set<uint16_t> pieceGroups;
        for (const auto& m : t.modTerrains)
            if (!m.fromMaps)  // (the hand-made rows: the sets made from the maps are made again)
                for (const auto& p : m.pieces) pieceGroups.insert(uint16_t(p[1] >> 4));
        struct Set {
            std::set<std::pair<uint16_t, uint16_t>> rects;
            std::set<uint16_t> groups;
        };
        std::map<std::string, Set> sets;
        for (const TerrainStamp& s : cmbw::brushStamps(argv[1], era)) {
            std::string lower = s.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("doodad") != std::string::npos || lower.find("_trees") != std::string::npos) continue;
            Set& st = sets[s.name.substr(0, s.name.rfind('_'))];
            for (int y = 0; y < s.height; ++y)
                for (int x = 0; x + 1 < s.width; x += 2)
                    if (s.has(x, y) && s.has(x + 1, y)) {
                        const uint16_t l = s.tiles[size_t(y) * s.width + x], r = s.tiles[size_t(y) * s.width + x + 1];
                        st.rects.insert({l, r}), st.groups.insert(uint16_t(l >> 4));
                    }
        }
        for (const auto& [name, st] : sets) {
            size_t in = 0;
            for (uint16_t g : st.groups) in += pieceGroups.count(g);
            if (st.groups.empty() || in * 2 >= st.groups.size()) continue;  // (a terrain already)
            ++all;
            std::vector<std::array<uint16_t, 3>> pieces;
            std::map<std::pair<uint16_t, uint16_t>, int> borders;
            std::map<uint16_t, int> solids;
            for (const auto& [l, r] : st.rects) {
                auto it = votes.find({era, l, r});
                if (it == votes.end()) continue;
                int total = 0;
                for (auto [g, k] : it->second) total += k;
                const auto best = std::max_element(it->second.begin(), it->second.end(), [](auto& a, auto& b) { return a.second < b.second; });
                if (best->second < 2 || best->second * 2 < total || best->first >= terrains.size() || !terrains[best->first][0]) continue;
                // (a solid rect's piece only when it looks like the game's: the maps put wall pieces over solid ground too)
                if (!terrains[best->first][1] && (!looksLike(l, best->first) || !looksLike(r, uint16_t(best->first + 1)))) continue;
                pieces.push_back({best->first, l, r});
                const auto& tt = terrains[best->first];
                if (tt[1]) ++borders[std::minmax(tt[0], tt[1])];
                else ++solids[tt[0]];
            }
            if (borders.empty()) {
                std::printf("  %-44s %zu rects, %zu pieces: no border on the maps\n", name.c_str(), st.rects.size(), pieces.size());
                continue;
            }
            const auto border = std::max_element(borders.begin(), borders.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
            // (its own terrain: the border's side its solid pieces are of; none: the side named first in the set's name? the higher id)
            uint16_t own = 0;
            for (const auto& [terr, n] : solids)
                if ((terr == border.first || terr == border.second) && (!own || n > solids[own])) own = terr;
            const bool guessed = !own;
            if (!own) own = border.second;
            const uint16_t ground = own == border.first ? border.second : border.first;
            // (only the pieces of that border and of its own terrain's solid rects)
            std::vector<std::array<uint16_t, 3>> use;
            for (const auto& p : pieces) {
                const auto& tt = terrains[p[0]];
                if ((tt[1] && std::minmax(tt[0], tt[1]) == border) || (!tt[1] && tt[0] == own)) use.push_back(p);
            }
            const char* tn = table ? table->terrainName(own) : "?";
            const char* gn = table ? table->terrainName(ground) : "?";
            std::printf("  %-44s %zu rects, %zu pieces: %s on %s%s (%zu of the border's rects)\n", name.c_str(), st.rects.size(), use.size(), tn, gn,
                        guessed ? " (no fill: guessed)" : "", use.size());
            if (use.size() < 4) continue;
            ++made;
            if (out) {
                std::string label = name.substr(name.find('_') + 1);
                std::replace(label.begin(), label.end(), '-', ' ');
                std::fprintf(out, "AUTO(%u, \"%s\", %u, %u, {", unsigned(era), label.c_str(), unsigned(own), unsigned(ground));
                for (const auto& p : use) std::fprintf(out, "{%u, %u, %u},", unsigned(p[0]), unsigned(p[1]), unsigned(p[2]));
                std::fprintf(out, "})\n");
            }
        }
    }
    if (out) std::fclose(out);
    std::printf("sets: %d, made from the maps: %d\n", all, made);
    return 0;
}
