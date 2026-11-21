// Two measurements for a rebuild of the mod's isometric terrain rules, from the tiles' pixels and the mod's maps:
//   A. the seam check: can the pixels on two tiles' touching edges tell tiles the mappers put side by side (the maps'
//      pairs with a mod tile) from pairs they did not (a random tile; a tile of a group near the real one)? AUC, and
//      how many wrong pairs pass a limit that 95 % of the real pairs pass.
//   B. today's rules against the maps: each mod terrain painted with a few strokes; of the tile pairs the engine makes,
//      how many the maps have too (or pass the seam limit), and of the maps' pairs between the tiles the engine used,
//      how many the engine made (few: the mappers lay these tiles out another way).
// usage: cmbw_seam_measure <Cosmonarchy install> <maps folder> [era]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

const std::array<unsigned, 8> kStock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};
const char* kEra[8] = {"Badlands", "Platform", "Install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};

// A megatile's four edges (left column, right column, top row, bottom row: 32 pixels of RGB each) and how rough the
// picture is at each (the mean difference between the edge and the line next to it).
struct Prof {
    uint8_t e[4][96] = {};
    float rough[4] = {};
    bool ok = false;
};
struct PairUse {
    int count = 0, maps = 0, lastMap = -1;
};
struct EraData {
    std::unique_ptr<Editor> ed;
    std::unordered_map<int, Prof> prof;
    std::unordered_map<int, uint16_t> tileOf;  // megatile -> a tile value that shows it
    std::unordered_map<uint64_t, PairUse> pairs[2];  // 0: side by side (left, right), 1: stacked (top, bottom)
    float limit[2] = {1e9f, 1e9f};
    int maps = 0;
};

uint64_t key(int a, int b) { return uint64_t(uint32_t(a)) << 32 | uint32_t(b); }

const Prof& profOf(EraData& era, int mega) {
    Prof& p = era.prof[mega];
    if (p.ok) return p;
    p.ok = true;
    const auto it = era.tileOf.find(mega);
    if (it == era.tileOf.end()) return p;
    const Sprite s = mapTilesPicture(era.ed->tileset(), {it->second}, 1, 0, 0, 1, 1);
    if (s.width != 32 || s.height != 32) return p;
    auto px = [&](int x, int y, int c) { return s.rgba[size_t((y * 32 + x) * 4 + c)]; };
    double r[4] = {};
    for (int i = 0; i < 32; ++i)
        for (int c = 0; c < 3; ++c) {
            p.e[0][i * 3 + c] = px(0, i, c), p.e[1][i * 3 + c] = px(31, i, c);
            p.e[2][i * 3 + c] = px(i, 0, c), p.e[3][i * 3 + c] = px(i, 31, c);
            r[0] += std::abs(px(0, i, c) - px(1, i, c)), r[1] += std::abs(px(31, i, c) - px(30, i, c));
            r[2] += std::abs(px(i, 0, c) - px(i, 1, c)), r[3] += std::abs(px(i, 31, c) - px(i, 30, c));
        }
    for (int k = 0; k < 4; ++k) p.rough[k] = float(r[k] / 96.0);
    return p;
}

// The seam of two megatiles (dir 0: a left of b; 1: a over b): the mean difference of the touching pixels, plain and
// divided by the tiles' own roughness there.
std::pair<float, float> seam(EraData& era, int a, int b, int dir) {
    const Prof& pa = profOf(era, a);
    const Prof& pb = profOf(era, b);
    const int ea = dir ? 3 : 1, eb = dir ? 2 : 0;
    double d = 0;
    for (int i = 0; i < 96; ++i) d += std::abs(int(pa.e[ea][i]) - int(pb.e[eb][i]));
    const float raw = float(d / 96.0);
    return {raw, raw / (2.0f + 0.5f * (pa.rough[ea] + pb.rough[eb]))};
}

double auc(std::vector<float> pos, std::vector<float> neg) {
    if (pos.empty() || neg.empty()) return 0;
    // (the chance that a real pair scores lower than a wrong one)
    std::sort(neg.begin(), neg.end());
    double s = 0;
    for (float p : pos) {
        const auto lo = std::lower_bound(neg.begin(), neg.end(), p), hi = std::upper_bound(neg.begin(), neg.end(), p);
        s += double(neg.end() - hi) + 0.5 * double(hi - lo);
    }
    return s / (double(pos.size()) * double(neg.size()));
}
float quantile(std::vector<float> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}
double passRate(const std::vector<float>& v, float limit) {
    if (v.empty()) return 0;
    size_t n = 0;
    for (float x : v) n += x <= limit;
    return double(n) / double(v.size());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_seam_measure <install> <maps folder> [era]\n"), 2;
    const int only = argc > 3 ? std::atoi(argv[3]) : -1;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::array<EraData, 8> eras;
    for (int e = 0; e < 8; ++e) {
        if (only >= 0 && e != only) continue;
        eras[size_t(e)].ed = std::make_unique<Editor>(data);
        if (!eras[size_t(e)].ed->newMap(64, 64, uint16_t(e), 1, err)) eras[size_t(e)].ed.reset();
    }
    // The maps' pairs.
    int mapIdx = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[2], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* es = chk.find("ERA ");
        if (!es || es->data.size() < 2) continue;
        const int e = (es->data[0] | es->data[1] << 8) & 7;
        EraData& era = eras[size_t(e)];
        if (!era.ed) continue;
        const Tileset& ts = era.ed->tileset();
        const TerrainLayer t = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
        const int w = t.width, h = t.height;
        if (w == 0 || h == 0 || t.tiles.size() < size_t(w) * h) continue;
        ++mapIdx, ++era.maps;
        std::vector<int> mega(size_t(w) * h);
        for (size_t i = 0; i < mega.size(); ++i) {
            const uint16_t v = t.tiles[i];
            const int m = (v >> 4) == 0 ? -1 : ts.megatileFor(v);
            mega[i] = m > 0 ? m : -1;
            if (m > 0) era.tileOf.emplace(m, v);
        }
        auto mod = [&](size_t i) { return unsigned(t.tiles[i] >> 4) >= kStock[size_t(e)]; };
        auto add = [&](size_t i, size_t j, int dir) {
            if (mega[i] < 0 || mega[j] < 0 || (!mod(i) && !mod(j))) return;
            PairUse& u = era.pairs[dir][key(mega[i], mega[j])];
            ++u.count;
            if (u.lastMap != mapIdx) u.lastMap = mapIdx, ++u.maps;
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                if (x + 1 < w) add(i, i + 1, 0);
                if (y + 1 < h) add(i, i + size_t(w), 1);
            }
    }

    // A. The seam check.
    std::printf("A. Seam check (real pairs: on 2+ maps, 3+ uses; a mod tile in each)\n");
    std::printf("%-9s %4s %7s | %-27s | %-27s\n", "tileset", "maps", "pairs", "plain: AUC rnd, near, pass", "rough-scaled: AUC rnd, near, pass");
    std::mt19937 rng(12345);
    std::vector<float> allPos[2], allRnd[2], allNear[2];
    for (int e = 0; e < 8; ++e) {
        EraData& era = eras[size_t(e)];
        if (!era.ed) continue;
        const Tileset& ts = era.ed->tileset();
        std::vector<int> pool;
        for (const auto& [m, v] : era.tileOf) pool.push_back(m);
        std::sort(pool.begin(), pool.end());
        std::vector<float> pos[2], rnd[2], near[2];  // [0 plain, 1 scaled]
        std::vector<float> posDir[2];                // scaled, by direction (the limit)
        size_t real = 0;
        for (int dir = 0; dir < 2; ++dir)
            for (const auto& [k, u] : era.pairs[dir]) {
                if (u.count < 3 || u.maps < 2) continue;
                ++real;
                const int a = int(k >> 32), b = int(uint32_t(k));
                const auto s = seam(era, a, b, dir);
                pos[0].push_back(s.first), pos[1].push_back(s.second), posDir[dir].push_back(s.second);
                // A random tile of the maps in b's place.
                for (int tries = 0; tries < 10 && !pool.empty(); ++tries) {
                    const int c = pool[rng() % pool.size()];
                    if (c == b || era.pairs[dir].count(key(a, c))) continue;
                    const auto n = seam(era, a, c, dir);
                    rnd[0].push_back(n.first), rnd[1].push_back(n.second);
                    break;
                }
                // A tile of a group near b's (the same set, most likely).
                const uint16_t bt = era.tileOf[b];
                for (int tries = 0; tries < 20; ++tries) {
                    const int g = int(bt >> 4) + int(rng() % 9) - 4;
                    if (g < 1 || g >= int(ts.groups.size())) continue;
                    const uint16_t v = uint16_t(g << 4 | (rng() % 16));
                    const int c = ts.megatileFor(v);
                    if (c <= 0 || c == b || era.pairs[dir].count(key(a, c))) continue;
                    era.tileOf.emplace(c, v);
                    const auto n = seam(era, a, c, dir);
                    near[0].push_back(n.first), near[1].push_back(n.second);
                    break;
                }
            }
        for (int dir = 0; dir < 2; ++dir) era.limit[dir] = quantile(posDir[dir], 0.95);
        std::printf("%-9s %4d %7zu |", kEra[e], era.maps, real);
        for (int k = 0; k < 2; ++k) {
            const float lim = quantile(pos[k], 0.95);
            std::printf(" %.3f %.3f  rnd %4.1f%% near %4.1f%% |", auc(pos[k], rnd[k]), auc(pos[k], near[k]), 100 * passRate(rnd[k], lim),
                        100 * passRate(near[k], lim));
            if (k == 1) {
                allPos[0].insert(allPos[0].end(), pos[0].begin(), pos[0].end()), allPos[1].insert(allPos[1].end(), pos[1].begin(), pos[1].end());
                allRnd[0].insert(allRnd[0].end(), rnd[0].begin(), rnd[0].end()), allRnd[1].insert(allRnd[1].end(), rnd[1].begin(), rnd[1].end());
                allNear[0].insert(allNear[0].end(), near[0].begin(), near[0].end()), allNear[1].insert(allNear[1].end(), near[1].begin(), near[1].end());
            }
        }
        std::printf("\n");
    }
    std::printf("%-9s %4s %7zu |", "all", "", allPos[0].size());
    for (int k = 0; k < 2; ++k) {
        const float lim = quantile(allPos[k], 0.95);
        std::printf(" %.3f %.3f  rnd %4.1f%% near %4.1f%% |", auc(allPos[k], allRnd[k]), auc(allPos[k], allNear[k]), 100 * passRate(allRnd[k], lim),
                    100 * passRate(allNear[k], lim));
    }
    std::printf("\n(pass: the wrong pairs under the limit that 95 %% of the real pairs are under)\n\n");

    // B. Today's rules against the maps.
    std::printf("B. Today's rules against the maps (each mod terrain: 5 strokes on its ground)\n");
    std::printf("%-9s %-44s %5s %5s %5s | %6s %6s %6s | %s\n", "tileset", "terrain", "tiles", "NULL", "pairs", "onmaps", "seamOK", "bad", "maps' pairs made");
    struct Row {
        int e;
        std::string name;
        int tiles, nulls, pairs, on, ok, bad, recallN, recallD;
    };
    std::vector<Row> rows;
    for (int e = 0; e < 8; ++e) {
        EraData& era = eras[size_t(e)];
        if (!era.ed) continue;
        const std::vector<Tileset::ModTerrain> terrains = era.ed->tileset().modTerrains;
        for (const Tileset::ModTerrain& mt : terrains) {
            Editor ed(data);
            if (!ed.newMap(128, 128, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            const int cx = 2048, cy = 2048;
            ed.paintBrush(cx, cy, mt.ground, Brush{Brush::Diamond, 16});
            const TerrainLayer before = readTerrain(ed.chk(), "TILE");
            const int strokes[5][3] = {{0, 0, 6}, {320, 96, 4}, {-448, -160, 1}, {-448, 160, 2}, {0, -288, 3}};
            for (const auto& s : strokes) ed.paintBrush(cx + s[0], cy + s[1], mt.id, Brush{Brush::Diamond, float(s[2])});
            const TerrainLayer after = readTerrain(ed.chk(), "TILE");
            const int w = after.width, h = after.height;
            if (before.tiles.size() != after.tiles.size()) continue;
            Row r{e, mt.name, 0, 0, 0, 0, 0, 0, 0, 0};
            std::vector<int> mega(after.tiles.size(), -1);
            std::vector<uint8_t> changed(after.tiles.size(), 0);
            std::set<int> own;
            for (size_t i = 0; i < mega.size(); ++i) {
                const uint16_t v = after.tiles[i];
                changed[i] = v != before.tiles[i];
                if ((v >> 4) == 0) {
                    r.nulls += changed[i];
                    continue;
                }
                const int m = ts.megatileFor(v);
                if (m <= 0) continue;
                mega[i] = m;
                era.tileOf.emplace(m, v);
                r.tiles += changed[i];
                if (changed[i] && unsigned(v >> 4) >= kStock[size_t(e)]) own.insert(m);
            }
            std::set<uint64_t> made[2];
            auto mod = [&](size_t i) { return unsigned(after.tiles[i] >> 4) >= kStock[size_t(e)]; };
            auto add = [&](size_t i, size_t j, int dir) {
                if (!changed[i] && !changed[j]) return;
                if (mega[i] < 0 || mega[j] < 0 || (!mod(i) && !mod(j))) return;
                made[dir].insert(key(mega[i], mega[j]));
            };
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const size_t i = size_t(y) * w + x;
                    if (x + 1 < w) add(i, i + 1, 0);
                    if (y + 1 < h) add(i, i + size_t(w), 1);
                }
            for (int dir = 0; dir < 2; ++dir) {
                for (uint64_t k : made[dir]) {
                    ++r.pairs;
                    if (era.pairs[dir].count(k)) ++r.on;
                    else if (seam(era, int(k >> 32), int(uint32_t(k)), dir).second <= era.limit[dir]) ++r.ok;
                    else ++r.bad;
                }
                for (const auto& [k, u] : era.pairs[dir]) {
                    if (u.count < 3 || u.maps < 2) continue;
                    if (!own.count(int(k >> 32)) || !own.count(int(uint32_t(k)))) continue;
                    ++r.recallD;
                    r.recallN += int(made[dir].count(k));
                }
            }
            rows.push_back(r);
            // SEAM_OUT=<folder>: the strokes of each terrain with 7.8 % or more bad pairs, as <n>-plain.png and
            // <n>-marked.png (a red line on each bad seam) and a line in list.txt.
            if (const char* outDir = std::getenv("SEAM_OUT"); outDir && r.pairs && r.bad * 1000 >= r.pairs * 78) {
                const int x0 = 64 - 20, y0 = 64 - 15, pw = 42, ph = 28;
                TerrainLayer part;
                part.width = uint16_t(pw), part.height = uint16_t(ph);
                for (int y = 0; y < ph; ++y)
                    for (int x = 0; x < pw; ++x) part.tiles.push_back(after.tiles[size_t(y0 + y) * w + x0 + x]);
                Image img = renderTerrain(ts, part);
                const std::string base = std::string(outDir) + "/" + std::to_string(rows.size());
                writePng((base + "-plain.png").c_str(), img);
                auto red = [&](int px, int py) {
                    if (px < 0 || py < 0 || px >= img.width || py >= img.height) return;
                    uint8_t* p = &img.rgba[size_t(py * img.width + px) * 4];
                    p[0] = 255, p[1] = 0, p[2] = 0, p[3] = 255;
                };
                auto bad = [&](size_t i, size_t j, int dir) {
                    if ((!changed[i] && !changed[j]) || mega[i] < 0 || mega[j] < 0 || (!mod(i) && !mod(j))) return false;
                    const uint64_t k = key(mega[i], mega[j]);
                    return !era.pairs[dir].count(k) && seam(era, mega[i], mega[j], dir).second > era.limit[dir];
                };
                for (int y = 0; y < ph; ++y)
                    for (int x = 0; x < pw; ++x) {
                        const size_t i = size_t(y0 + y) * w + x0 + x;
                        if (bad(i, i + 1, 0))
                            for (int t = 0; t < 32; ++t) red(x * 32 + 31, y * 32 + t), red(x * 32 + 32, y * 32 + t);
                        if (bad(i, i + size_t(w), 1))
                            for (int t = 0; t < 32; ++t) red(x * 32 + t, y * 32 + 31), red(x * 32 + t, y * 32 + 32);
                    }
                writePng((base + "-marked.png").c_str(), img);
                if (FILE* f = std::fopen((std::string(outDir) + "/list.txt").c_str(), "a")) {
                    std::fprintf(f, "%zu\t%s\t%s\t%.1f\t%d\t%d\n", rows.size(), kEra[e], mt.name.c_str(), 100.0 * r.bad / r.pairs, r.nulls, r.tiles);
                    std::fclose(f);
                }
            }
            auto pct =[](int n, int d) { return d ? 100.0 * n / d : 0.0; };
            std::printf("%-9s %-44.44s %5d %5d %5d | %5.1f%% %5.1f%% %5.1f%% | %4d of %4d (%.0f%%)\n", kEra[e], mt.name.c_str(), r.tiles, r.nulls, r.pairs,
                        pct(r.on, r.pairs), pct(r.ok, r.pairs), pct(r.bad, r.pairs), r.recallN, r.recallD, pct(r.recallN, r.recallD));
            std::fflush(stdout);
        }
    }
    std::printf("\nBy tileset: terrains, NULL share of the painted tiles, the engine's pairs on the maps / seam OK / bad, the maps' pairs made\n");
    Row all{-1, "all", 0, 0, 0, 0, 0, 0, 0, 0};
    int allN = 0;
    for (int e = 0; e <= 8; ++e) {
        Row s{e, "", 0, 0, 0, 0, 0, 0, 0, 0};
        int n = 0;
        if (e < 8) {
            for (const Row& r : rows)
                if (r.e == e) ++n, s.tiles += r.tiles, s.nulls += r.nulls, s.pairs += r.pairs, s.on += r.on, s.ok += r.ok, s.bad += r.bad, s.recallN += r.recallN, s.recallD += r.recallD;
            if (!n) continue;
            ++allN, all.tiles += s.tiles, all.nulls += s.nulls, all.pairs += s.pairs, all.on += s.on, all.ok += s.ok, all.bad += s.bad, all.recallN += s.recallN,
                all.recallD += s.recallD;
        } else {
            s = all, n = int(rows.size());
        }
        auto pct = [](int a, int d) { return d ? 100.0 * a / d : 0.0; };
        std::printf("%-9s %3d  NULL %4.1f%%  on maps %4.1f%%  seam OK %4.1f%%  bad %4.1f%%  maps' pairs made %4.1f%% (%d of %d)\n", e < 8 ? kEra[e] : "all", n,
                    pct(s.nulls, s.tiles + s.nulls), pct(s.on, s.pairs), pct(s.ok, s.pairs), pct(s.bad, s.pairs), pct(s.recallN, s.recallD), s.recallN, s.recallD);
    }
    return 0;
}
