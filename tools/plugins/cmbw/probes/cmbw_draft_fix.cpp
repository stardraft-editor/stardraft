// Repair passes over the mod's terrain drafts (the built-in rows, generated, not verified), with the template model
// (stardraft/mods/terrain_rules.h). For each tileset and each draft terrain, up to three passes:
//   1. structure: normalize (every template rect present, the engine's order); tiles that do not exist, pairs that
//      are not two subtiles side by side in one group: reported; NULL rects get the template's own tiles (borrowed:
//      the border renders like the game there instead of black).
//   2. foreign tiles: a piece whose tiles are in groups the maps never use with this terrain (Tileset::groupTerrains)
//      nor in its other pieces' groups: reported as suspects.
//   3. shape check: each mod pair's picture against the template pair's, by the structure of their edges (gradient
//      magnitude, normalized cross-correlation, 64 x 32); a pair that fits badly is reported with the best pair of the
//      terrain's own groups (not applied: the score is sure only for sets that recolor the game's, and a group's
//      variants score apart although all are right).
// The repaired rules go to one .sdterrain file (version 2; drafts stay drafts), the report to a text file, and (with an
// .inc path) the file's bytes as a C array for src/mods/cmbw.cpp (kRepairedRules: the program applies them over the
// built-in rows).
// usage: cmbw_draft_fix <Cosmonarchy install> <out .sdterrain> <report .txt> [out .inc | -] [tileset] [passes 1-3]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {

// The edge structure of a pair of tiles (64 x 32): gradient magnitudes of the gray picture, zero mean, unit variance.
std::vector<float> edges(const Tileset& ts, uint16_t l, uint16_t r) {
    const Sprite s = mapTilesPicture(ts, {l, r}, 2, 0, 0, 2, 1);
    std::vector<float> out;
    if (s.width != 64 || s.height != 32) return out;
    std::vector<float> gray(size_t(64 * 32));
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = 0.299f * s.rgba[i * 4] + 0.587f * s.rgba[i * 4 + 1] + 0.114f * s.rgba[i * 4 + 2];
    out.assign(gray.size(), 0.0f);
    for (int y = 1; y < 31; ++y)
        for (int x = 1; x < 63; ++x) {
            const float gx = gray[size_t(y * 64 + x + 1)] - gray[size_t(y * 64 + x - 1)], gy = gray[size_t((y + 1) * 64 + x)] - gray[size_t((y - 1) * 64 + x)];
            out[size_t(y * 64 + x)] = std::sqrt(gx * gx + gy * gy);
        }
    double mean = 0;
    for (float v : out) mean += v;
    mean /= double(out.size());
    double var = 0;
    for (float& v : out) v = float(v - mean), var += double(v) * v;
    const float sd = float(std::sqrt(var / double(out.size())));
    if (sd > 1e-6f)
        for (float& v : out) v /= sd;
    return out;
}
float ncc(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0;
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += double(a[i]) * b[i];
    return float(s / double(a.size()));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_draft_fix <install> <out .sdterrain> <report .txt> [tileset] [passes]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const std::string incPath = argc > 4 && std::string(argv[4]) != "-" ? argv[4] : "";
    const int only = argc > 5 ? std::atoi(argv[5]) : -1, passes = argc > 6 ? std::clamp(std::atoi(argv[6]), 1, 3) : 3;
    FILE* rep = std::fopen(argv[3], "w");
    if (!rep) return std::fprintf(stderr, "cannot write %s\n", argv[3]), 1;
    auto out = [&](const char* fmt, auto... args) { std::fprintf(rep, fmt, args...), std::printf(fmt, args...); };
    tr::Rules rules;
    int totalBorrowed = 0, totalReplaced = 0, totalSuspects = 0, totalBroken = 0;
    for (int era = 0; era < 8; ++era) {
        if (only >= 0 && era != only) continue;
        Tileset ts;
        if (!ts.load(data, uint16_t(era), err)) {
            out("== tileset %d: %s\n", era, err.c_str());
            continue;
        }
        const IsomTerrainTable* table = IsomTerrainTable::forEra(ts.era);
        tr::TilesetRules tsr = tr::rulesOf(ts);
        out("== tileset %d: %zu terrains\n", era, tsr.terrains.size());
        std::map<uint16_t, std::vector<float>> edgeCache;
        auto edgesOf = [&](uint16_t l, uint16_t r) -> const std::vector<float>& {
            auto& e = edgeCache[uint16_t(l)];  // (keyed by the left tile: pairs are k, k+1 of one group)
            if (e.empty()) e = edges(ts, l, r);
            return e;
        };
        for (tr::Terrain& x : tsr.terrains) {
            Tileset::ModTerrain& m = x.t;
            if (m.variantOf || !m.draft) continue;
            const auto [a, b] = tr::templateOf(ts, m);
            const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, a, b);
            std::map<uint16_t, const tr::TemplateRect*> byGroup;
            for (const auto& r : rects) byGroup[r.group] = &r;
            const auto before = tr::completeness(ts, m);
            out("-- %s  (%s, template %s (%s), %d of %d rects)\n", m.name.c_str(), tr::familyName(tr::familyOf(rects, ts)), table->terrainName(b), table->terrainName(a),
                before.first, before.second);
            // Pass 1: structure.
            tr::normalize(ts, m);
            int borrowed = 0, broken = 0;
            auto isTemplatePiece = [&](size_t k) { return k < m.setPieces || k + m.gapPieces >= m.pieces.size(); };
            for (size_t k = 0; k < m.pieces.size(); ++k) {
                auto& p = m.pieces[k];
                if (!isTemplatePiece(k)) continue;
                for (int h = 1; h <= 2; ++h)
                    if (!tr::nullHalf(p[size_t(h)]) && ts.megatileFor(p[size_t(h)]) <= 0) {
                        out("   pass 1  %-24s rect %u: tile %u.%u does not exist -> NULL\n", byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other", unsigned(p[0]),
                            unsigned(p[size_t(h)] >> 4), unsigned(p[size_t(h)] & 15));
                        p[size_t(h)] = uint16_t(h - 1), ++broken;
                    }
                if (tr::stateOf(p) == tr::PieceState::Own && ((p[1] >> 4) != (p[2] >> 4) || (p[1] & 15) + 1 != (p[2] & 15))) {
                    out("   pass 1  %-24s rect %u: pair %u.%u %u.%u is not two subtiles side by side (kept)\n", byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other",
                        unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15), unsigned(p[2] >> 4), unsigned(p[2] & 15));
                    ++broken;
                }
                if (tr::nullHalf(p[1]) || tr::nullHalf(p[2])) {
                    const uint16_t l = tr::gameTile(ts, p[0]), r = tr::gameTile(ts, uint32_t(p[0]) + 1);
                    if (l && r) {
                        if (tr::nullHalf(p[1])) p[1] = l;
                        if (tr::nullHalf(p[2])) p[2] = r;
                        ++borrowed;
                    }
                }
            }
            tr::normalize(ts, m);
            if (borrowed) out("   pass 1  %d rects borrowed the template's tiles\n", borrowed);
            totalBorrowed += borrowed, totalBroken += broken;
            if (passes < 2) continue;
            // Pass 2: the terrain's own groups; foreign tiles.
            std::set<uint16_t> mine;
            for (const auto& [g, t, other] : ts.groupTerrains)
                if (t == m.id || other == m.id) mine.insert(g);
            std::map<uint16_t, int> groupUse;
            for (size_t k = 0; k < m.pieces.size(); ++k)
                if (isTemplatePiece(k) && tr::stateOf(m.pieces[k]) == tr::PieceState::Own) ++groupUse[uint16_t(m.pieces[k][1] >> 4)];
            for (const auto& [g, n] : groupUse)
                if (n >= 2) mine.insert(g);
            int suspects = 0;
            for (size_t k = 0; k < m.pieces.size(); ++k) {
                const auto& p = m.pieces[k];
                if (!isTemplatePiece(k) || tr::stateOf(p) != tr::PieceState::Own) continue;
                if (!mine.count(uint16_t(p[1] >> 4)))
                    out("   pass 2  %-24s rect %u: tiles %u.%u %u.%u are from a group the terrain does not use elsewhere (suspect)\n",
                        byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other", unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15), unsigned(p[2] >> 4),
                        unsigned(p[2] & 15)),
                        ++suspects;
            }
            totalSuspects += suspects;
            if (passes < 3) continue;
            // Pass 3: the shape of each own pair against the template's.
            std::vector<std::pair<uint16_t, uint16_t>> candidates;
            for (uint16_t g : mine)
                for (uint16_t s = 0; s + 1 < 16; ++s)
                    if (ts.megatileFor(uint16_t(g << 4 | s)) > 0 && ts.megatileFor(uint16_t(g << 4 | (s + 1))) > 0) candidates.push_back({uint16_t(g << 4 | s), uint16_t(g << 4 | (s + 1))});
            int replaced = 0, weak = 0;
            double sum = 0;
            int scored = 0;
            for (size_t k = 0; k < m.pieces.size(); ++k) {
                auto& p = m.pieces[k];
                if (!isTemplatePiece(k) || tr::stateOf(p) != tr::PieceState::Own) continue;
                const uint16_t gl = tr::gameTile(ts, p[0]), gr = tr::gameTile(ts, uint32_t(p[0]) + 1);
                if (!gl || !gr) continue;
                const std::vector<float>& want = edgesOf(gl, gr);
                const float cur = ncc(want, edgesOf(p[1], p[2]));
                sum += cur, ++scored;
                if (cur >= 0.45f) continue;
                std::pair<uint16_t, uint16_t> best{0, 0};
                float bestScore = cur;
                for (const auto& c : candidates)
                    if (const float sc = ncc(want, edgesOf(c.first, c.second)); sc > bestScore) bestScore = sc, best = c;
                const char* role = byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other";
                if (best.first && bestScore >= 0.55f && bestScore >= cur + 0.2f) {  // (reported, not applied: the score is sure only for recolored
                    // sets, and a group's variants score apart although all are right)
                    out("   pass 3  %-24s rect %u: %u.%u fits %.2f, %u.%u would fit %.2f: check by hand\n", role, unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15),
                        double(cur), unsigned(best.first >> 4), unsigned(best.first & 15), double(bestScore));
                    ++replaced;
                } else {
                    out("   pass 3  %-24s rect %u: %u.%u fits %.2f (best other %.2f): check by hand\n", role, unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15), double(cur),
                        double(bestScore));
                    ++weak;
                }
            }
            totalReplaced += replaced;
            const auto after = tr::completeness(ts, m);
            out("   result  %d of %d rects; shape fit %.2f over %d pairs; %d with a better-fitting pair, %d weak\n", after.first, after.second, scored ? sum / scored : 0.0, scored,
                replaced, weak);
        }
        rules.tilesets.push_back(std::move(tsr));
    }
    out("== totals: %d rects borrowed, %d pairs with a better-fitting pair, %d suspects, %d broken pairs\n", totalBorrowed, totalReplaced, totalSuspects, totalBroken);
    std::fclose(rep);
    if (!tr::save(argv[2], rules, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::printf("wrote %s\n", argv[2]);
    if (!incPath.empty()) {  // (the same bytes as a C array: src/mods/cmbw_repaired_rules.inc)
        const std::vector<uint8_t> bytes = tr::write(rules);
        FILE* inc = std::fopen(incPath.c_str(), "w");
        if (!inc) return std::fprintf(stderr, "cannot write %s\n", incPath.c_str()), 1;
        std::fprintf(inc, "// Made by tools/plugins/cmbw/probes/cmbw_draft_fix.cpp: the built-in terrain rows repaired (every template rect present,\n"
                          "// the template's tiles where a row had none), as a terrain rules file (version 2) the program applies over the rows.\n"
                          "// Do not edit by hand. (Its probe builds with -Dcmbw_probes=ON.)\n");
        for (size_t i = 0; i < bytes.size(); ++i) std::fprintf(inc, "%s%u,", i % 32 ? "" : "\n", unsigned(bytes[i]));
        std::fprintf(inc, "\n");
        std::fclose(inc);
        std::printf("wrote %s (%zu bytes)\n", incPath.c_str(), bytes.size());
    }
    return 0;
}
