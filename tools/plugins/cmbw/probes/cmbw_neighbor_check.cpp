// The neighbour check: are the mod's isometric rules (each mod tile stands for a rect of a game border, its template)
// right, by the mod's maps? The mappers put the tiles down by hand in arrangements that are right; the template says
// which rects may be side by side and stacked. So each pair of neighbouring tiles of one mod terrain on the maps, read
// through the rules as (rect, half) and (rect, half), must be a pair the game border itself makes. A pair it never
// makes: a piece in the wrong rect, or art laid out another way than the template.
// The legal pairs of a template: the game border painted with many random strokes (the engine's own tiles).
// Output: a line a terrain; with an out folder, pictures of the terrains with the lowest scores (and a few of the
// highest): the map place that uses the terrain most, the same with the illegal seams red, and the engine's painting.
// usage: cmbw_neighbor_check <Cosmonarchy install> <maps folder> [out folder] [pictures: lowest N] [era]
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {

const char* kEraName[8] = {"Badlands", "Platform", "Install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
const std::array<unsigned, 8> kStock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};

struct MapTiles {
    std::string name;
    int w = 0, h = 0;
    std::vector<uint16_t> tiles;
};
struct Legal {
    std::unordered_set<uint32_t> pairs[2];  // 0: side by side, 1: stacked
};
uint32_t slot(uint16_t rect, int half) { return uint32_t(rect) << 1 | uint32_t(half); }
uint32_t pairKey(uint32_t a, uint32_t b) { return a << 13 | b; }

// The pairs the game border (terrain on ground) makes: 300 random strokes on a 192 x 192 map, two in three the
// terrain's and one the ground's (carving), every pair of neighbouring tiles as (rect, half) slots.
Legal legalOf(GameData& data, int era, uint16_t ground, uint16_t terrain) {
    Legal out;
    std::string err;
    Editor ed(data);
    if (!ed.newMap(192, 192, uint16_t(era), 1, err)) return out;
    const int c = 96 * 32;
    ed.paintBrush(c, c, ground, Brush{Brush::Diamond, 30});
    std::mt19937 rng(uint32_t(era * 1000 + ground * 40 + terrain));
    for (int i = 0; i < 300; ++i) {
        const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
        ed.paintBrush(x, y, i % 3 == 2 ? ground : terrain, Brush{Brush::Diamond, float(1 + rng() % 4)});
    }
    const TerrainLayer t = readTerrain(ed.chk(), "TILE");
    const int w = t.width, h = t.height;
    auto s = [&](size_t i) {
        const uint16_t g = uint16_t(t.tiles[i] >> 4);
        return slot(uint16_t(g & ~1u), g & 1);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            if (x + 1 < w) out.pairs[0].insert(pairKey(s(i), s(i + 1)));
            if (y + 1 < h) out.pairs[1].insert(pairKey(s(i), s(i + size_t(w))));
        }
    return out;
}

// The rects of a terrain's template border (not its fill).
std::set<uint16_t> edgeRectsOf(const Tileset& ts, const Tileset::ModTerrain& m) {
    std::set<uint16_t> out;
    const auto tpl = tr::templateOf(ts, m);
    for (const tr::TemplateRect& r : tr::templateRects(ts, tpl.first, tpl.second)) out.insert(r.group);
    return out;
}
// The mod tiles that are some mod terrain's fill (a piece for a rect that is not of its border).
std::vector<uint8_t> fillTilesOf(const Tileset& ts, int era) {
    std::vector<uint8_t> out(65536, 0);
    for (const Tileset::ModTerrain& m : ts.modTerrains) {
        const std::set<uint16_t> edges = edgeRectsOf(ts, m);
        for (const auto& p : m.pieces)
            for (int half = 0; half < 2; ++half) {
                const uint16_t v = p[size_t(1 + half)];
                if (!tr::nullHalf(v) && unsigned(v >> 4) >= kStock[size_t(era)] && !edges.count(p[0])) out[v] = 1;
            }
    }
    return out;
}
// Each of a terrain's mod tiles: the slots (rect, half) it stands in. A fill tile stands in both halves of its rect
// (the mod's fill tiles are texture tiles with no left or right half). A tile that is a fill somewhere (this terrain's
// or another's: a ground tile that fills a hole of the set) is no piece of the border: no slot in a border rect.
std::vector<std::vector<uint32_t>> slotsOf(const Tileset& ts, int era, const Tileset::ModTerrain& m, const std::vector<uint8_t>& fillTile) {
    std::vector<std::vector<uint32_t>> slots(65536);
    const std::set<uint16_t> edges = edgeRectsOf(ts, m);
    auto add = [&](uint16_t v, uint32_t k) {
        auto& s = slots[v];
        if (std::find(s.begin(), s.end(), k) == s.end()) s.push_back(k);
    };
    for (const auto& p : m.pieces)
        for (int half = 0; half < 2; ++half) {
            const uint16_t v = p[size_t(1 + half)];
            if (tr::nullHalf(v) || unsigned(v >> 4) < kStock[size_t(era)]) continue;
            if (!edges.count(p[0])) add(v, slot(p[0], 0)), add(v, slot(p[0], 1));
            else if (!fillTile[v]) add(v, slot(p[0], half));
        }
    return slots;
}

struct Result {
    int era = 0;
    size_t terrain = 0;  // index in the tileset's modTerrains
    std::string name;
    long long uses = 0, legal = 0;
    int distinct = 0, distinctLegal = 0, maps = 0;
    int bestMap = -1, cropX = 0, cropY = 0;
    size_t setTiles = 0;
    long long bad[4] = {};  // the illegal pairs by where they are: fill + fill, fill + edge, wall rows, flat edge
};

void redLine(Image& img, int x0, int y0, bool vertical) {
    for (int t = 0; t < 32; ++t)
        for (int k = -1; k <= 1; ++k) {
            const int px = vertical ? x0 + k : x0 + t, py = vertical ? y0 + t : y0 + k;
            if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
            uint8_t* p = &img.rgba[size_t(py * img.width + px) * 4];
            p[0] = 255, p[1] = 0, p[2] = 0, p[3] = 255;
        }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_neighbor_check <install> <maps folder> [out folder] [pictures: lowest N] [era]\n"), 2;
    const char* outDir = argc > 3 ? argv[3] : nullptr;
    const int lowest = argc > 4 ? std::atoi(argv[4]) : 10;
    const int only = argc > 5 ? std::atoi(argv[5]) : -1;
    const int cw = 42, ch = 28;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::array<std::vector<MapTiles>, 8> maps;
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
        if (only >= 0 && e != only) continue;
        const TerrainLayer t = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
        if (t.width == 0 || t.height == 0 || t.tiles.size() < size_t(t.width) * t.height) continue;
        maps[size_t(e)].push_back({p.stem().string(), t.width, t.height, t.tiles});
    }
    std::vector<Result> results;
    std::array<std::unique_ptr<Editor>, 8> eds;
    std::array<std::vector<uint8_t>, 8> fillTiles;
    std::printf("%-9s %-44s %5s %8s %7s | %6s %7s\n", "tileset", "terrain", "maps", "pairs", "legal", "kinds", "legal");
    for (int e = 0; e < 8; ++e) {
        if (only >= 0 && e != only) continue;
        eds[size_t(e)] = std::make_unique<Editor>(data);
        if (!eds[size_t(e)]->newMap(64, 64, uint16_t(e), 1, err)) {
            eds[size_t(e)].reset();
            continue;
        }
        const Tileset& ts = eds[size_t(e)]->tileset();
        fillTiles[size_t(e)] = fillTilesOf(ts, e);
        std::map<std::pair<uint16_t, uint16_t>, Legal> legalCache;
        for (size_t ti = 0; ti < ts.modTerrains.size(); ++ti) {
            const Tileset::ModTerrain& m = ts.modTerrains[ti];
            const auto tpl = tr::templateOf(ts, m);
            if (tpl.first == tpl.second || tr::templateRects(ts, tpl.first, tpl.second).empty()) continue;
            if (!legalCache.count(tpl)) legalCache[tpl] = legalOf(data, e, tpl.first, tpl.second);
            const Legal& legal = legalCache[tpl];
            // Each of the terrain's mod tiles: the slots (rect, half) it stands in.
            const std::vector<std::vector<uint32_t>> slots = slotsOf(ts, e, m, fillTiles[size_t(e)]);
            size_t setTiles = 0;
            for (const auto& s : slots) setTiles += !s.empty();
            std::map<uint16_t, int> rectRow;  // the template's rects (the border's own: not the fill) and their stack rows
            for (const tr::TemplateRect& rect : tr::templateRects(ts, tpl.first, tpl.second)) rectRow[rect.group] = rect.row;
            Result r;
            r.era = e, r.terrain = ti, r.name = m.name, r.setTiles = setTiles;
            std::map<uint64_t, bool> kinds;  // (dir, tile a, tile b) -> legal
            int bestCount = 0;
            for (size_t mi = 0; mi < maps[size_t(e)].size(); ++mi) {
                const MapTiles& mp = maps[size_t(e)][mi];
                const int w = mp.w, h = mp.h;
                std::vector<int> mine(size_t(w + 1) * (h + 1), 0);  // (summed) the terrain's tiles
                long long uses = 0;
                auto check = [&](size_t i, size_t j, int dir) {
                    const auto& a = slots[mp.tiles[i]];
                    const auto& b = slots[mp.tiles[j]];
                    if (a.empty() || b.empty()) return;
                    bool ok = false;
                    for (uint32_t sa : a)
                        for (uint32_t sb : b) ok = ok || legal.pairs[dir].count(pairKey(sa, sb));
                    ++r.uses, ++uses, r.legal += ok;
                    if (!ok) {
                        const auto ra = rectRow.find(uint16_t(a.front() >> 1)), rb = rectRow.find(uint16_t(b.front() >> 1));
                        const bool fa = ra == rectRow.end(), fb = rb == rectRow.end();
                        ++r.bad[fa && fb ? 0 : fa || fb ? 1 : ra->second > 0 || rb->second > 0 ? 2 : 3];
                    }
                    kinds[uint64_t(dir) << 32 | uint64_t(mp.tiles[i]) << 16 | mp.tiles[j]] = ok;
                };
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const size_t i = size_t(y) * w + x;
                        if (x + 1 < w) check(i, i + 1, 0);
                        if (y + 1 < h) check(i, i + size_t(w), 1);
                        mine[size_t(y + 1) * (w + 1) + x + 1] =
                            int(!slots[mp.tiles[i]].empty()) + mine[size_t(y) * (w + 1) + x + 1] + mine[size_t(y + 1) * (w + 1) + x] - mine[size_t(y) * (w + 1) + x];
                    }
                if (!uses) continue;
                ++r.maps;
                // The window of the picture's size with the most of the terrain's tiles.
                for (int y = 0; y + ch <= h; y += 4)
                    for (int x = 0; x + cw <= w; x += 4) {
                        const int n = mine[size_t(y + ch) * (w + 1) + x + cw] - mine[size_t(y) * (w + 1) + x + cw] - mine[size_t(y + ch) * (w + 1) + x] +
                                      mine[size_t(y) * (w + 1) + x];
                        if (n > bestCount) bestCount = n, r.bestMap = int(mi), r.cropX = x, r.cropY = y;
                    }
            }
            for (const auto& [k, ok] : kinds) ++r.distinct, r.distinctLegal += ok;
            results.push_back(r);
            auto pct = [](long long n, long long d) { return d ? 100.0 * double(n) / double(d) : 0.0; };
            std::printf("%-9s %-44.44s %5d %8lld %6.1f%% | %6d %6.1f%%\n", kEraName[e], m.name.c_str(), r.maps, r.uses, pct(r.legal, r.uses), r.distinct,
                        pct(r.distinctLegal, r.distinct));
            std::fflush(stdout);
        }
    }
    long long uses = 0, legal = 0;
    int evidence = 0, b95 = 0, b80 = 0, low = 0;
    for (const Result& r : results) {
        uses += r.uses, legal += r.legal;
        if (r.uses < 300) continue;
        ++evidence;
        const double s = double(r.legal) / double(r.uses);
        (s >= 0.95 ? b95 : s >= 0.8 ? b80 : low)++;
    }
    std::printf("\nall: %zu terrains; pairs on the maps %lld, legal %.1f%%; terrains with 300+ pairs: %d (95%%+ legal: %d, 80-95%%: %d, under 80%%: %d)\n",
                results.size(), uses, uses ? 100.0 * double(legal) / double(uses) : 0.0, evidence, b95, b80, low);
    if (!outDir) return 0;

    // Pictures: the lowest scores among the terrains with 300+ pairs (one for each set of tiles), and the 3 highest.
    std::vector<const Result*> ranked;
    for (const Result& r : results)
        if (r.uses >= 300 && r.bestMap >= 0) ranked.push_back(&r);
    std::sort(ranked.begin(), ranked.end(), [](const Result* a, const Result* b) { return a->legal * b->uses < b->legal * a->uses; });
    std::vector<const Result*> chosen;
    std::set<std::string> seenSets;
    auto setKey = [&](const Result* r) {
        const Tileset::ModTerrain& m = eds[size_t(r->era)]->tileset().modTerrains[r->terrain];
        std::string k = std::to_string(r->era);
        for (size_t i = 0; i < m.pieces.size() && i < 6; ++i) k += "," + std::to_string(m.pieces[i][1]);
        return k;
    };
    // (lowest 0: the catalogue, every terrain under 95 %)
    for (const Result* r : ranked) {
        if (lowest > 0 ? int(chosen.size()) >= lowest : r->legal * 100 >= r->uses * 95) continue;
        if (seenSets.insert(setKey(r)).second) chosen.push_back(r);
    }
    for (size_t i = ranked.size(), n = 0; lowest > 0 && i-- > 0 && n < 3;)
        if (seenSets.insert(setKey(ranked[i])).second) chosen.push_back(ranked[i]), ++n;
    FILE* list = std::fopen((std::string(outDir) + "/list.txt").c_str(), "w");
    int n = 0;
    for (const Result* r : chosen) {
        ++n;
        const Tileset& ts = eds[size_t(r->era)]->tileset();
        const Tileset::ModTerrain& m = ts.modTerrains[r->terrain];
        const auto tpl = tr::templateOf(ts, m);
        const Legal legalSet = legalOf(data, r->era, tpl.first, tpl.second);
        const std::vector<std::vector<uint32_t>> slots = slotsOf(ts, r->era, m, fillTiles[size_t(r->era)]);
        const MapTiles& mp = maps[size_t(r->era)][size_t(r->bestMap)];
        TerrainLayer part;
        part.width = uint16_t(std::min(cw, mp.w)), part.height = uint16_t(std::min(ch, mp.h));
        for (int y = 0; y < part.height; ++y)
            for (int x = 0; x < part.width; ++x) part.tiles.push_back(mp.tiles[size_t(r->cropY + y) * mp.w + r->cropX + x]);
        Image img = renderTerrain(ts, part);
        const std::string base = std::string(outDir) + "/" + std::to_string(n);
        writePng((base + "-map.png").c_str(), img);
        auto bad = [&](int x, int y, int dx, int dy) {
            if (x + dx >= part.width || y + dy >= part.height) return false;
            const auto& a = slots[part.tiles[size_t(y) * part.width + x]];
            const auto& b = slots[part.tiles[size_t(y + dy) * part.width + x + dx]];
            if (a.empty() || b.empty()) return false;
            for (uint32_t sa : a)
                for (uint32_t sb : b)
                    if (legalSet.pairs[dy].count(pairKey(sa, sb))) return false;
            return true;
        };
        for (int y = 0; y < part.height; ++y)
            for (int x = 0; x < part.width; ++x) {
                if (bad(x, y, 1, 0)) redLine(img, x * 32 + 32, y * 32, true);
                if (bad(x, y, 0, 1)) redLine(img, x * 32, y * 32 + 32, false);
            }
        writePng((base + "-marked.png").c_str(), img);
        // The engine's painting of the terrain: 5 strokes on its ground.
        Editor ed(data);
        if (ed.newMap(128, 128, uint16_t(r->era), 1, err)) {
            const int cx = 2048, cy = 2048;
            ed.paintBrush(cx, cy, m.ground, Brush{Brush::Diamond, 16});
            const int strokes[5][3] = {{0, 0, 6}, {320, 96, 4}, {-448, -160, 1}, {-448, 160, 2}, {0, -288, 3}};
            for (const auto& s : strokes) ed.paintBrush(cx + s[0], cy + s[1], m.id, Brush{Brush::Diamond, float(s[2])});
            const TerrainLayer t = readTerrain(ed.chk(), "TILE");
            TerrainLayer p2;
            p2.width = uint16_t(cw), p2.height = uint16_t(ch);
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x) p2.tiles.push_back(t.tiles[size_t(64 - 15 + y) * t.width + 64 - 20 + x]);
            writePng((base + "-engine.png").c_str(), renderTerrain(ed.tileset(), p2));
        }
        if (list)
            std::fprintf(list, "%d\t%s\t%s\t%.1f\t%lld\t%d\t%s\t%lld\t%lld\t%lld\t%lld\n", n, kEraName[r->era], r->name.c_str(),
                         100.0 * double(r->legal) / double(r->uses), r->uses, r->maps, mp.name.c_str(), r->bad[0], r->bad[1], r->bad[2], r->bad[3]);
    }
    if (list) std::fclose(list);
    return 0;
}
