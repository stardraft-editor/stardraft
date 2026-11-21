// Copies of the game's isometric tiles among the mod's tiles: the base of the copy-based rules for the mod's terrains.
//
// The game's borders are valid isometric terrain (their tiles, rects and roles come from the tilesets' CV5). The mod
// makes most of its terrains by copying those tiles, in the same tileset (Higher Dirt from High Dirt) or into another
// one, and redrawing parts (another ground at the top, a recolour). So each mod tile that keeps most of the pixels of a
// game border's tile belongs in that tile's rect role and half.
//
// Sources: every tile (each variant, both halves) of every rect of every game border of all 8 tilesets. Targets: every
// tile of the mod's groups of all 8 tilesets. A target matches a source when 60 % or more of its pixels agree: the
// same colour (within 12 levels), or the same after a linear recolour of the gray levels (within 10). Candidates come
// from 16-bit hashes of each 16 x 16 quarter (which blocks are brighter than the quarter's mean), so a tile with a
// redrawn quarter still finds its source.
//
// Output, a line per matched target: target era, target tile, source era, source ground, source terrain, source rect,
// role, half, variant, source tile, score (share of pixels that agree), raw share, recoloured share, sources tied (within
// 0.02 of the best: the same picture in several rects), the source border's rect count, the tied sources' slots of that
// family (role:half, commas).
// usage: cmbw_isom_copies <Cosmonarchy install> <out .tsv> [min score, default 0.6]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {

const char* kEraName[8] = {"Badlands", "Platform", "Install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
const std::array<unsigned, 8> kStock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};

struct Pic {
    std::array<uint8_t, 3072> rgb{};
    std::array<float, 1024> gray{};
    std::array<int32_t, 4> hash{};  // each quarter's hash; -1: the quarter is flat
    std::array<uint64_t, 4> exact{};  // each quarter's exact pixels (FNV-1a): an unchanged quarter finds its source even when
                                      // it is too even for the brightness hash (asphalt, plating)
    bool ok = false;
};

bool picOf(const Tileset& ts, uint16_t tile, Pic& p) {
    p.ok = false;
    if (ts.megatileFor(tile) <= 0) return false;
    const Sprite sp = mapTilesPicture(ts, {tile}, 1, 0, 0, 1, 1);
    if (sp.width != 32 || sp.height != 32) return false;
    double sum = 0, sq = 0;
    for (int i = 0; i < 1024; ++i) {
        for (int c = 0; c < 3; ++c) p.rgb[size_t(i * 3 + c)] = sp.rgba[size_t(i * 4 + c)];
        const float g = 0.299f * sp.rgba[size_t(i * 4)] + 0.587f * sp.rgba[size_t(i * 4 + 1)] + 0.114f * sp.rgba[size_t(i * 4 + 2)];
        p.gray[size_t(i)] = g, sum += g, sq += double(g) * g;
    }
    const double mean = sum / 1024, sd = std::sqrt(std::max(0.0, sq / 1024 - mean * mean));
    if (sd < 4) return false;  // (a flat tile: nothing to match)
    for (int q = 0; q < 4; ++q) {
        const int qx = (q & 1) * 16, qy = (q >> 1) * 16;
        float block[16];
        double qs = 0, qq = 0;
        for (int b = 0; b < 16; ++b) {
            float a = 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) a += p.gray[size_t((qy + (b >> 2) * 4 + y) * 32 + qx + (b & 3) * 4 + x)];
            block[b] = a / 16, qs += block[b], qq += double(block[b]) * block[b];
        }
        const double qm = qs / 16, qsd = std::sqrt(std::max(0.0, qq / 16 - qm * qm));
        if (qsd < 3) {
            p.hash[size_t(q)] = -1;
            continue;
        }
        int h = 0;
        for (int b = 0; b < 16; ++b) h |= (block[b] > qm ? 1 : 0) << b;
        p.hash[size_t(q)] = h;
    }
    for (int q = 0; q < 4; ++q) {
        uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                for (int c = 0; c < 3; ++c) h = (h ^ p.rgb[size_t((((q >> 1) * 16 + y) * 32 + (q & 1) * 16 + x) * 3 + c)]) * 1099511628211ull;
        p.exact[size_t(q)] = h ^ uint64_t(q);
    }
    p.ok = true;
    return true;
}

// The share of pixels that agree: raw colours, and gray levels after a linear recolour (least squares).
void agree(const Pic& t, const Pic& s, float& raw, float& fit) {
    int nr = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < 1024; ++i) {
        const int d = (std::abs(int(t.rgb[size_t(i * 3)]) - s.rgb[size_t(i * 3)]) + std::abs(int(t.rgb[size_t(i * 3 + 1)]) - s.rgb[size_t(i * 3 + 1)]) +
                       std::abs(int(t.rgb[size_t(i * 3 + 2)]) - s.rgb[size_t(i * 3 + 2)])) / 3;
        nr += d <= 12;
        const double x = s.gray[size_t(i)], y = t.gray[size_t(i)];
        sx += x, sy += y, sxx += x * x, sxy += x * y;
    }
    raw = float(nr) / 1024;
    const double den = 1024 * sxx - sx * sx;
    const double a = den > 1e-6 ? (1024 * sxy - sx * sy) / den : 1.0, b = (sy - a * sx) / 1024;
    int nf = 0;
    for (int i = 0; i < 1024; ++i) nf += std::abs(t.gray[size_t(i)] - (a * s.gray[size_t(i)] + b)) <= 10;
    fit = a > 0.3 ? float(nf) / 1024 : 0.0f;  // (an inverted or flattened picture is no copy)
}

struct Src {
    int era;
    uint16_t ground, terrain, rect, tile;
    std::string role;
    int half, variant;
    Pic pic;
    int nrects = 0;  // the border's rect count (its family)
    std::vector<uint16_t> terrainPx;  // the pixels that are the terrain's, not the ground's
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_isom_copies <install> <out .tsv> [min score]\n"), 2;
    const float minScore = argc > 3 ? float(std::atof(argv[3])) : 0.6f;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;

    // The sources: every game border of every tileset, from the original game data when VANILLA=<StarCraft folder> is
    // given (the mod keeps the game's groups but has redrawn some of their art).
    GameData vanilla;
    const char* vdir = std::getenv("VANILLA");
    if (vdir && !cmbw::openGame(vanilla, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    GameData& srcData = vdir ? vanilla : data;
    std::vector<Src> srcs;
    std::array<const IsomTerrainTable*, 8> tables{};
    for (int e = 0; e < 8; ++e) {
        Tileset loaded;
        if (!loaded.load(srcData, uint16_t(e), err)) {
            std::fprintf(stderr, "%s: %s\n", kEraName[e], err.c_str());
            continue;
        }
        const Tileset& ts = loaded;
        tables[size_t(e)] = IsomTerrainTable::forEra(uint16_t(e));
        // (how many of the game's border tiles the mod has redrawn: the same tile value, another picture)
        int same = 0, redrawn = 0;
        if (vdir) {
            Editor med(data);
            if (med.newMap(64, 64, uint16_t(e), 1, err))
                for (const IsomTerrainTable::Pair& pr : tables[size_t(e)]->pairs()) {
                    if (pr.ta || pr.tag || pr.twin) continue;
                    for (const tr::TemplateRect& r : tr::templateRects(ts, pr.a, pr.b))
                        for (int half = 0; half < 2; ++half)
                            for (int k = 0; k < 16; ++k) {
                                const uint16_t t = uint16_t((r.group + half) * 16 + k);
                                Pic a, b;
                                if (!picOf(ts, t, a) || !picOf(med.tileset(), t, b)) continue;
                                float raw, fit;
                                agree(b, a, raw, fit);
                                (raw >= 0.99f ? same : redrawn)++;
                            }
                }
            std::printf("%s: game border tiles in the mod: %d the same, %d redrawn\n", kEraName[e], same, redrawn);
        }
        if (!tables[size_t(e)]) continue;
        int borders = 0;
        for (const IsomTerrainTable::Pair& pr : tables[size_t(e)]->pairs()) {
            if (pr.ta || pr.tag || pr.twin) continue;  // (game borders only)
            const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, pr.a, pr.b);
            if (rects.empty()) continue;
            ++borders;
            std::vector<Pic> ground;  // the ground's solid tiles (both halves, their first variants)
            for (int half = 0; half < 2; ++half)
                if (const int g = solidTileGroup(ts, pr.a, half); g >= 0)
                    for (uint16_t k = 0; k < 8; ++k) {
                        Pic gp;
                        if (picOf(ts, uint16_t(g << 4 | k), gp)) ground.push_back(gp);
                    }
            for (const tr::TemplateRect& r : rects)
                for (int half = 0; half < 2; ++half)
                    for (int k = 0; k < 16; ++k) {
                        Src s{e, pr.a, pr.b, r.group, uint16_t((r.group + half) * 16 + k), r.role, half, k, {}, int(rects.size()), {}};
                        if (!picOf(ts, s.tile, s.pic)) continue;
                        for (uint16_t i = 0; i < 1024; ++i) {
                            bool groundPx = false;
                            for (const Pic& gp : ground) {
                                int d = 0;
                                for (int c = 0; c < 3; ++c) d += std::abs(int(s.pic.rgb[size_t(i * 3 + c)]) - gp.rgb[size_t(i * 3 + c)]);
                                if (d <= 36) { groundPx = true; break; }
                            }
                            if (!groundPx) s.terrainPx.push_back(i);
                        }
                        srcs.push_back(std::move(s));
                    }
        }
        std::printf("%s: %d game borders\n", kEraName[e], borders);
    }
    std::printf("source tiles: %zu\n", srcs.size());
    // The index: (quarter, hash) -> sources. A bucket of more than 400 sources is too common to tell anything.
    std::unordered_map<int64_t, std::vector<int>> index;
    for (size_t i = 0; i < srcs.size(); ++i)
        for (int q = 0; q < 4; ++q)
            if (srcs[i].pic.hash[size_t(q)] >= 0) index[int64_t(q) << 16 | srcs[i].pic.hash[size_t(q)]].push_back(int(i));
    std::unordered_map<uint64_t, std::vector<int>> exactIndex;
    for (size_t i = 0; i < srcs.size(); ++i)
        for (int q = 0; q < 4; ++q) exactIndex[srcs[i].pic.exact[size_t(q)]].push_back(int(i));

    // (DEBUG_TILE=<era>:<tile>: that tile against every source, best by the terrain-pixel share, then stop)
    if (const char* dt = std::getenv("DEBUG_TILE")) {
        const int de = std::atoi(dt), dtile = std::atoi(std::strchr(dt, ':') + 1);
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(de), 1, err)) return 1;
        Pic p;
        if (!picOf(ed.tileset(), uint16_t(dtile), p)) return std::printf("no picture\n"), 0;
        std::vector<std::tuple<float, float, float, int>> rows;
        for (size_t si = 0; si < srcs.size(); ++si) {
            float raw, fit;
            agree(p, srcs[si].pic, raw, fit);
            const auto& tp = srcs[si].terrainPx;
            int n = 0;
            for (uint16_t i : tp) {
                int d = 0;
                for (int c = 0; c < 3; ++c) d += std::abs(int(p.rgb[size_t(i * 3 + c)]) - srcs[si].pic.rgb[size_t(i * 3 + c)]);
                n += d <= 36;
            }
            rows.push_back({tp.empty() ? 0.0f : float(n) / float(tp.size()), raw, fit, int(si)});
        }
        std::sort(rows.rbegin(), rows.rend());
        for (int k = 0; k < 8 && k < int(rows.size()); ++k) {
            const auto& [m, raw, fit, si] = rows[size_t(k)];
            const Src& s = srcs[size_t(si)];
            int hashHit = 0;
            for (int q = 0; q < 4; ++q) hashHit += p.hash[size_t(q)] >= 0 && p.hash[size_t(q)] == s.pic.hash[size_t(q)];
            std::printf("masked %.2f (of %zu px) raw %.2f fit %.2f  %s %s/%s %s:%d tile %u  quarter hashes equal %d\n", m, s.terrainPx.size(), raw, fit, kEraName[s.era],
                        IsomTerrainTable::forEra(uint16_t(s.era))->terrainName(s.ground), IsomTerrainTable::forEra(uint16_t(s.era))->terrainName(s.terrain), s.role.c_str(),
                        s.half, unsigned(s.tile), hashHit);
        }
        return 0;
    }
    FILE* out = std::fopen(argv[2], "w");
    if (!out) return 1;
    std::vector<int> seen(srcs.size(), -1);
    for (int e = 0; e < 8; ++e) {
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
        const Tileset& ts = ed.tileset();
        int targets = 0, matched = 0, tick = 0;
        Pic p;
        for (size_t g = kStock[size_t(e)]; g < ts.groups.size(); ++g)
            for (unsigned k = 0; k < 16; ++k) {
                const uint16_t tile = uint16_t(g * 16 + k);
                if (!picOf(ts, tile, p)) continue;
                ++targets, ++tick;
                float best = 0, bestRaw = 0, bestFit = 0;
                int bi = -1;
                std::vector<std::pair<float, int>> scores;
                for (int q = 0; q < 8; ++q) {
                    const std::vector<int>* bucket = nullptr;
                    if (q < 4) {
                        if (p.hash[size_t(q)] < 0) continue;
                        const auto it = index.find(int64_t(q) << 16 | p.hash[size_t(q)]);
                        if (it != index.end()) bucket = &it->second;
                    } else {
                        const auto it = exactIndex.find(p.exact[size_t(q - 4)]);
                        if (it != exactIndex.end()) bucket = &it->second;
                    }
                    if (!bucket || bucket->size() > 400) continue;
                    for (int si : *bucket) {
                        if (seen[size_t(si)] == tick) continue;
                        seen[size_t(si)] = tick;
                        float raw, fit;
                        agree(p, srcs[size_t(si)].pic, raw, fit);
                        float sc = std::max(raw, fit);
                        // (on the terrain's pixels only: a redrawn ground)
                        const auto& tp = srcs[size_t(si)].terrainPx;
                        if (sc < 0.8f && tp.size() >= 160 && tp.size() < 1000) {
                            int n = 0;
                            for (uint16_t i : tp) {
                                int d = 0;
                                for (int c = 0; c < 3; ++c) d += std::abs(int(p.rgb[size_t(i * 3 + c)]) - srcs[size_t(si)].pic.rgb[size_t(i * 3 + c)]);
                                n += d <= 36;
                            }
                            const float masked = float(n) / float(tp.size());
                            if (masked >= 0.8f) sc = std::max(sc, masked);  // (the share of the terrain's pixels that agree)
                        }
                        scores.push_back({sc, si});
                        if (sc > best) best = sc, bestRaw = raw, bestFit = fit, bi = si;
                    }
                }
                if (bi < 0 || best < minScore) continue;
                int ties = 0;
                std::string tied;  // the tied sources' slots of the best's family: "role:half" with commas
                for (const auto& [sc, si] : scores)
                    if (sc >= best - 0.02f) {
                        ++ties;
                        const Src& o = srcs[size_t(si)];
                        if (o.nrects != srcs[size_t(bi)].nrects) continue;
                        const std::string slot = o.role + ":" + std::to_string(o.half);
                        if (("," + tied + ",").find("," + slot + ",") == std::string::npos) tied += (tied.empty() ? "" : ",") + slot;
                    }
                const Src& s = srcs[size_t(bi)];
                const IsomTerrainTable* t = tables[size_t(s.era)];
                std::fprintf(out, "%s\t%u\t%s\t%s\t%s\t%u\t%s\t%d\t%d\t%u\t%.3f\t%.3f\t%.3f\t%d\t%d\t%s\n", kEraName[e], unsigned(tile), kEraName[s.era],
                             t->terrainName(s.ground), t->terrainName(s.terrain), unsigned(s.rect), s.role.c_str(), s.half, s.variant, unsigned(s.tile), best,
                             bestRaw, bestFit, ties, s.nrects, tied.c_str());
                ++matched;
            }
        std::printf("%s: %d mod tiles with a picture, %d copies of a game border tile\n", kEraName[e], targets, matched);
        std::fflush(stdout);
    }
    std::fclose(out);
    return 0;
}
