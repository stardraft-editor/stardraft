// Rectangle match done by a program: for a game border (the template, any tileset's) and a range of the mod's tile
// groups, each template tile gets the mod tile that looks most like it (both in gray, each tile's brightness and
// contrast made the same: the shapes count, not the colours). Writes a terrain rules file the Mod terrain mapper opens.
// usage:
//   cmbw_auto_match dump <install> <rules .isom>
//   cmbw_auto_match match <install> <era> <base era> <base ground> <base terrain> <stand-in> <ground> <first mod group>
//                   <last mod group> <id> <name> <out .isom> [<rules .isom> to add to]
#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/mods/tile_match.h"
#include "stardraft/tileset.h"

using namespace stardraft;

namespace {

// A tile as 32x32 gray values, mean 0 and deviation 1 (a flat tile: all 0).
std::vector<float> grayOf(const Tileset& t, uint16_t v) {
    std::vector<float> g(32 * 32, 0.f);
    const int mt = t.megatileFor(v);
    if (mt <= 0 || size_t(mt) >= t.megatiles.size()) return g;
    for (int m = 0; m < 16; ++m) {
        const uint32_t e = t.megatiles[size_t(mt)][size_t(m)];
        const size_t mi = e >> 1;
        const bool flip = e & 1;
        if (mi >= t.minitiles.size()) continue;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const Rgb c = t.palette[t.minitiles[mi][size_t(y * 8 + (flip ? 7 - x : x))]];
                g[size_t(((m / 4) * 8 + y) * 32 + (m % 4) * 8 + x)] = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            }
    }
    double s = 0, q = 0;
    for (float f : g) s += f, q += double(f) * f;
    const double mean = s / g.size(), sd = std::sqrt(std::max(0.0, q / g.size() - mean * mean));
    for (float& f : g) f = sd > 1e-3 ? float((f - mean) / sd) : 0.f;
    return g;
}
// Each tile also as a 2-level shape (above or below its own middle): an edge's line counts, the texture less.
std::vector<float> smooth(const std::vector<float>& g) {
    std::vector<float> o(g.size());
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            float s = 0;
            int n = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (unsigned(x + dx) < 32 && unsigned(y + dy) < 32) s += g[size_t((y + dy) * 32 + x + dx)], ++n;
            o[size_t(y * 32 + x)] = s / n;
        }
    double s = 0, q = 0;
    for (float f : o) s += f, q += double(f) * f;
    const double mean = s / o.size(), sd = std::sqrt(std::max(0.0, q / o.size() - mean * mean));
    for (float& f : o) f = sd > 1e-3 ? float((f - mean) / sd) : 0.f;
    return o;
}
double corr(const std::vector<float>& a, const std::vector<float>& b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += double(a[i]) * b[i];
    return s / a.size();
}

int dump(const GameData& data, const char* file) {
    terrainrules::Rules r;
    std::string err;
    if (!terrainrules::load(file, r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    for (const auto& ts : r.tilesets) {
        std::printf("tileset era %d groups %u\n", ts.era, ts.groups);
        for (const auto& t : ts.terrains) {
            std::printf(" terrain %u '%s' standIn %u ground %u groundTemplate %u fill %d base %d/%u/%u pieces %zu set %zu gap %zu\n", t.t.id, t.t.name.c_str(), t.t.standIn,
                        t.t.ground, t.t.groundTemplate, int(t.t.fill), t.baseEra, t.baseGround, t.baseTerrain, t.t.pieces.size(), t.t.setPieces, t.t.gapPieces);
            for (const auto& p : t.t.pieces) {
                const auto it = t.variantOf.find(p);
                std::printf("   g%u  %u:%u  %u:%u  k%d\n", p[0], p[1] >> 4, p[1] & 15, p[2] >> 4, p[2] & 15, it != t.variantOf.end() ? it->second : -1);
            }
        }
    }
    (void)data;
    return 0;
}

// (the editor's baseRectsOf: each own rect -> the base border's rect of the same shape, then role, then order)
std::map<uint16_t, uint16_t> baseRectsOf(const std::vector<terrainrules::TemplateRect>& own, const std::vector<terrainrules::TemplateRect>& base) {
    std::map<uint16_t, uint16_t> out;
    std::vector<bool> used(base.size(), false);
    for (const auto& r : own)
        for (size_t k = 0; k < base.size(); ++k)
            if (!used[k] && !r.shape.empty() && base[k].shape == r.shape) {
                out[r.group] = base[k].group, used[k] = true;
                break;
            }
    for (const auto& r : own)
        for (size_t k = 0; k < base.size() && !out.count(r.group); ++k)
            if (!used[k] && base[k].role == r.role) {
                out[r.group] = base[k].group, used[k] = true;
                break;
            }
    size_t k = 0;
    for (const auto& r : own) {
        if (out.count(r.group)) continue;
        while (k < base.size() && used[k]) ++k;
        if (k < base.size()) out[r.group] = base[k].group, used[k] = true;
    }
    return out;
}

struct Target {
    uint16_t g;  // (own rect)
    int k, h;    // (variant, half)
    uint16_t tv; // (the template's tile)
};

// The targets: as the mapper's matchLayout makes them.
std::vector<Target> targetsOf(const Tileset& ts, const Tileset& bts, const Tileset::ModTerrain& m, uint16_t bg0, uint16_t bt0) {
    const auto [g, s] = terrainrules::templateOf(ts, m);
    const auto own = terrainrules::templateRects(ts, g, s);
    const auto base = baseRectsOf(own, terrainrules::templateRects(bts, bg0, bt0));
    std::vector<Target> out;
    auto addRect = [&](int bg, int rg, uint16_t og) {
        int k = 0;
        for (int sub = 0; sub < 16; ++sub) {
            if (bts.megatileFor(uint16_t(bg * 16 + sub)) <= 0) continue;
            out.push_back({og, k, 0, uint16_t(bg * 16 + sub)});
            if (bts.megatileFor(uint16_t(rg * 16 + sub)) > 0) out.push_back({og, k, 1, uint16_t(rg * 16 + sub)});
            ++k;
        }
    };
    for (const auto& r : own)
        if (const auto it = base.find(r.group); it != base.end()) addRect(int(it->second), int(it->second) + 1, r.group);
    if (const int sg = solidTileGroup(ts, m.standIn), bs = solidTileGroup(bts, bt0); sg >= 0 && bs >= 0)
        addRect(bs, bs + 1, uint16_t(sg)), addRect(bs + 1, bs, uint16_t(sg + 1));
    return out;
}

// Each template tile -> a mod tile of the groups [g0, g1] (each mod tile once, the best scores first).
std::map<uint16_t, uint16_t> assign(const Tileset& ts, const Tileset& bts, const std::vector<Target>& tg, int g0, int g1, bool verbose) {
    std::set<uint16_t> tmpl;
    for (const auto& t : tg) tmpl.insert(t.tv);
    std::vector<uint16_t> mods;
    for (int g = g0; g <= g1; ++g)
        for (int s = 0; s < 16; ++s)
            if (ts.megatileFor(uint16_t(g * 16 + s)) > 0) mods.push_back(uint16_t(g * 16 + s));
    std::map<uint16_t, std::pair<std::vector<float>, std::vector<float>>> fa, fb;
    for (uint16_t v : tmpl) { auto g = grayOf(bts, v); fa[v] = {g, smooth(g)}; }
    for (uint16_t v : mods) { auto g = grayOf(ts, v); fb[v] = {g, smooth(g)}; }
    static const double pol = std::getenv("MATCH_INVERT") ? -1.0 : 1.0, wraw = std::getenv("MATCH_RAW") ? std::atof(std::getenv("MATCH_RAW")) : 0.3;  // (the template's terrain lighter than its ground: -1)
    auto score = [&](uint16_t a, uint16_t b) { return pol * (wraw * corr(fa[a].first, fb[b].first) + corr(fa[a].second, fb[b].second)); };
    std::map<uint16_t, uint16_t> out;
    std::set<uint16_t> used;
    // First by pairs: a rect's two halves to two mod tiles that stand together (the next subtile, or the same subtile
    // of the next group), the best sums first.
    {
        std::map<std::pair<uint16_t, int>, std::array<uint16_t, 2>> halves;
        for (const auto& t : tg) halves[{t.g, t.k}][size_t(t.h)] = t.tv;
        std::set<std::pair<uint16_t, uint16_t>> tp;
        for (const auto& [key, h] : halves)
            if (h[0] && h[1] && !tp.count({h[1], h[0]})) tp.insert({h[0], h[1]});
        std::set<uint16_t> modSet(mods.begin(), mods.end());
        std::vector<std::pair<uint16_t, uint16_t>> mp;
        for (uint16_t v : mods) {
            if ((v & 15) < 15 && modSet.count(uint16_t(v + 1))) mp.push_back({v, uint16_t(v + 1)});
            if (modSet.count(uint16_t(v + 16))) mp.push_back({v, uint16_t(v + 16)});
        }
        struct P { double s; std::pair<uint16_t, uint16_t> a, b; };
        std::vector<P> ps;
        for (const auto& a : tp)
            for (const auto& b : mp) ps.push_back({score(a.first, b.first) + score(a.second, b.second), a, b});
        std::sort(ps.begin(), ps.end(), [](const P& x, const P& y) { return x.s > y.s; });
        for (const P& p : ps)
            if (!out.count(p.a.first) && !out.count(p.a.second) && !used.count(p.b.first) && !used.count(p.b.second)) {
                out[p.a.first] = p.b.first, out[p.a.second] = p.b.second, used.insert(p.b.first), used.insert(p.b.second);
                if (verbose) std::printf("  %u:%u+%u:%u -> %u:%u+%u:%u  %.3f\n", p.a.first >> 4, p.a.first & 15, p.a.second >> 4, p.a.second & 15, p.b.first >> 4, p.b.first & 15, p.b.second >> 4, p.b.second & 15, p.s);
            }
    }
    struct S { double s; uint16_t a, b; };
    std::vector<S> all;
    for (uint16_t a : tmpl)
        for (uint16_t b : mods) all.push_back({score(a, b), a, b});
    std::sort(all.begin(), all.end(), [](const S& x, const S& y) { return x.s > y.s; });
    for (const S& x : all)
        if (!out.count(x.a) && !used.count(x.b)) {
            out[x.a] = x.b, used.insert(x.b);
            if (verbose) std::printf("  %u:%u -> %u:%u  %.3f\n", x.a >> 4, x.a & 15, x.b >> 4, x.b & 15, x.s);
        }
    return out;
}

Tileset::ModTerrain modOf(uint16_t id, const char* name, uint16_t standIn, uint16_t ground, uint16_t groundTemplate) {
    Tileset::ModTerrain m;
    m.id = id, m.name = name, m.standIn = standIn, m.ground = ground, m.groundTemplate = groundTemplate, m.fill = true;
    return m;
}

terrainrules::Terrain build(const Tileset& ts, Tileset::ModTerrain m, const std::vector<Target>& tg, const std::map<uint16_t, uint16_t>& a, int baseEra,
                            uint16_t bg0, uint16_t bt0) {
    terrainrules::Terrain t;
    std::map<std::pair<uint16_t, int>, std::array<uint16_t, 3>> pc;
    std::vector<std::pair<uint16_t, int>> order;
    for (const auto& x : tg) {
        auto key = std::make_pair(x.g, x.k);
        if (!pc.count(key)) pc[key] = {x.g, 0, 1}, order.push_back(key);
        if (const auto it = a.find(x.tv); it != a.end()) pc[key][size_t(1 + x.h)] = it->second;
    }
    const int sg = solidTileGroup(ts, m.standIn);
    std::vector<std::array<uint16_t, 3>> set, fill;
    for (const auto& key : order) {
        const auto& p = pc[key];
        (key.first == sg || key.first == sg + 1 ? fill : set).push_back(p);
        t.variantOf[p] = uint8_t(key.second);
    }
    m.pieces = set;
    m.pieces.insert(m.pieces.end(), fill.begin(), fill.end());
    m.setPieces = set.size(), m.gapPieces = 0;
    terrainrules::normalize(ts, m);
    t.t = m, t.baseEra = baseEra, t.baseGround = bg0, t.baseTerrain = bt0;
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: see the file\n"), 2;
    const std::string mode = argv[1];
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (mode == "dump") return dump(data, argv[3]);
    if (mode == "solids") {  // solids <install> <era>: each terrain's inside group
        Tileset ts;
        if (!ts.load(data, uint16_t(std::atoi(argv[3])), err)) return 1;
        const auto* tb = IsomTerrainTable::forEra(uint16_t(std::atoi(argv[3])));
        for (uint16_t i = 1; tb && i <= tb->terrainCount(); ++i) {
            const int g = solidTileGroup(ts, i);
            const int mt = g >= 0 ? ts.megatileFor(uint16_t(g << 4)) : -1;
            const unsigned f = mt > 0 && size_t(mt) < ts.minitileFlags.size() ? ts.minitileFlags[size_t(mt)][5] : 0;
            std::printf("%u %s %d cv5h %u vf4 %x\n", i, tb->terrainName(i), g, g >= 0 ? unsigned(ts.groups[size_t(g)].groundHeight) : 0u, f);
        }
        return 0;
    }
    if (mode == "groups") {  // groups <install> <era> <g0> <g1>: each group's tiles and mean colour
        Tileset ts;
        if (!ts.load(data, uint16_t(std::atoi(argv[3])), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        for (int g = std::atoi(argv[4]); g <= std::atoi(argv[5]); ++g) {
            int n = 0;
            double r = 0, gg = 0, b = 0;
            std::string subs;  // (the subtiles with art: x, else .)
            for (int s = 0; s < 16; ++s) subs += ts.megatileFor(uint16_t(g * 16 + s)) > 0 ? 'x' : '.';
            std::printf("%s ", subs.c_str());
            for (int s = 0; s < 16; ++s) {
                const int mt = ts.megatileFor(uint16_t(g * 16 + s));
                if (mt <= 0) continue;
                ++n;
                for (int m = 0; m < 16; ++m)
                    for (uint8_t px : ts.minitiles[ts.megatiles[size_t(mt)][size_t(m)] >> 1]) r += ts.palette[px].r, gg += ts.palette[px].g, b += ts.palette[px].b;
            }
            const double d = n ? n * 1024.0 : 1;
            const auto& tg = ts.groups[size_t(g)];
            std::printf("%d n%2d  %3.0f %3.0f %3.0f  type %u edges %u %u %u %u  extra %u %u %u %u\n", g, n, r / d, gg / d, b / d, tg.type, tg.edges[0], tg.edges[1], tg.edges[2],
                        tg.edges[3], tg.extra[0], tg.extra[1], tg.extra[2], tg.extra[3]);
        }
        return 0;
    }
    // dup <install> <rules .isom> <terrain id> <new id> <name> <ground> <ground template> <g0> <g1> <inside g0> <inside g1> <out .isom>:
    // the terrain again with the mod tiles that look most like its own (the same art on another ground)
    if (mode == "dup" && argc >= 14) {
        terrainrules::Rules r;
        if (!terrainrules::load(argv[3], r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const terrainrules::Terrain* src = nullptr;
        for (const auto& x : r.tilesets[0].terrains)
            if (x.t.id == std::atoi(argv[4])) src = &x;
        if (!src) return std::fprintf(stderr, "no terrain\n"), 1;
        Tileset ts;
        if (!ts.load(data, r.tilesets[0].era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const int sg = solidTileGroup(ts, src->t.standIn);
        std::vector<Target> edge, inside;
        for (const auto& p : src->t.pieces) {
            const auto it = src->variantOf.find(p);
            const int k = it != src->variantOf.end() ? it->second : -1;
            for (int h = 0; h < 2; ++h)
                if (!terrainrules::nullHalf(p[size_t(1 + h)])) (p[0] == sg || p[0] == sg + 1 ? inside : edge).push_back({p[0], k, h, p[size_t(1 + h)]});
        }
        auto a = assign(ts, ts, edge, std::atoi(argv[9]), std::atoi(argv[10]), true);
        for (const auto& [x, y] : assign(ts, ts, inside, std::atoi(argv[11]), std::atoi(argv[12]), true)) a[x] = y;
        terrainrules::Terrain t = *src;
        t.t.id = uint16_t(std::atoi(argv[5])), t.t.name = argv[6], t.t.ground = uint16_t(std::atoi(argv[7])), t.t.groundTemplate = uint16_t(std::atoi(argv[8]));
        t.variantOf.clear();
        int missing = 0;
        for (auto& p : t.t.pieces) {
            const auto old = p;
            for (int h = 0; h < 2; ++h)
                if (!terrainrules::nullHalf(p[size_t(1 + h)])) {
                    if (const auto it = a.find(p[size_t(1 + h)]); it != a.end()) p[size_t(1 + h)] = it->second;
                    else p[size_t(1 + h)] = uint16_t(h), ++missing;
                }
            if (const auto it = src->variantOf.find(old); it != src->variantOf.end()) t.variantOf[p] = it->second;
        }
        auto& list = r.tilesets[0].terrains;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& x) { return x.t.id == t.t.id; }), list.end());
        list.push_back(t);
        std::printf("pieces %zu, halves with no match %d\n", t.t.pieces.size(), missing);
        return terrainrules::save(argv[13], r, err) ? 0 : (std::fprintf(stderr, "%s\n", err.c_str()), 1);
    }
    if (mode == "names") {  // names <install> <era>
        const auto* tb = IsomTerrainTable::forEra(uint16_t(std::atoi(argv[3])));
        for (uint16_t i = 1; tb && i <= tb->terrainCount(); ++i) std::printf("%u %s\n", i, tb->terrainName(i));
        return 0;
    }
    // seed <install> <rules .isom> <terrain id> [window]: the suggestions after 1, 2, 3 of the hand's edge rects are
    // matched (each rect, each pair, ... as the seeds), against the hand's tiles for the rest
    if (mode == "seed") {
        terrainrules::Rules r;
        if (!terrainrules::load(argv[3], r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const terrainrules::Terrain* t = nullptr;
        for (const auto& x : r.tilesets[0].terrains)
            if (x.t.id == std::atoi(argv[4])) t = &x;
        if (!t) return std::fprintf(stderr, "no terrain\n"), 1;
        Tileset ts, bts;
        if (!ts.load(data, r.tilesets[0].era, err) || !bts.load(data, uint16_t(t->baseEra), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const auto tg = targetsOf(ts, bts, t->t, t->baseGround, t->baseTerrain);
        const int sg = solidTileGroup(ts, t->t.standIn);
        std::map<uint16_t, uint16_t> hand;  // (template tile -> the hand's mod tile)
        std::map<uint16_t, std::pair<uint16_t, int>> halfOf;            // (template tile -> its rect, half)
        std::map<std::pair<uint16_t, int>, std::set<uint16_t>> handHalf;  // (a rect's half -> the hand's tiles in it, any variant)
        std::vector<uint16_t> rects;
        std::map<uint16_t, std::vector<uint16_t>> tilesOf;
        for (const auto& x : tg) {
            if (x.g == sg || x.g == sg + 1) continue;  // (edges only)
            for (const auto& p : t->t.pieces)
                if (const auto it = t->variantOf.find(p); p[0] == x.g && it != t->variantOf.end() && it->second == x.k && !terrainrules::nullHalf(p[size_t(1 + x.h)]))
                    hand[x.tv] = p[size_t(1 + x.h)];
            if (!hand.count(x.tv)) continue;  // (only the tiles the hand has matched)
            halfOf[x.tv] = {x.g, x.h}, handHalf[{x.g, x.h}].insert(hand[x.tv]);
            if (std::find(rects.begin(), rects.end(), x.g) == rects.end()) rects.push_back(x.g);
            tilesOf[x.g].push_back(x.tv);
        }
        if (std::getenv("DIAG")) {  // (the hand's pairs: the seam between their halves, and against the sibling set's)
            for (const auto& x : tg) {
                if (x.h != 0 || x.g == sg || x.g == sg + 1) continue;
                uint16_t l = 0, r = 0;
                for (const auto& p : t->t.pieces)
                    if (const auto it = t->variantOf.find(p); p[0] == x.g && it != t->variantOf.end() && it->second == x.k) l = p[1], r = p[2];
                if (l <= 1 || r <= 1) continue;
                auto sm = [&](uint16_t a, uint16_t b) {
                    const auto ga = grayOf(ts, a), gb = grayOf(ts, b);  // (normalized: use raw instead)
                    (void)ga, (void)gb;
                    double s = 0;
                    const int ma = ts.megatileFor(a), mb = ts.megatileFor(b);
                    for (int y = 0; y < 32; ++y) {
                        auto px = [&](int mt, int xx, int yy) {
                            const uint32_t e = ts.megatiles[size_t(mt)][size_t((yy / 8) * 4 + xx / 8)];
                            const int lx = (e & 1) ? 7 - xx % 8 : xx % 8;
                            const Rgb c = ts.palette[ts.minitiles[e >> 1][size_t((yy % 8) * 8 + lx)]];
                            return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
                        };
                        s += std::abs(px(ma, 31, y) - px(mb, 0, y));
                    }
                    return s / 32;
                };
                std::printf("g%u k%d  %u:%u|%u:%u seam %.1f   reversed %.1f   L|R+1 %.1f\n", x.g, x.k, l >> 4, l & 15, r >> 4, r & 15, sm(l, r), sm(r, l),
                            ts.megatileFor(uint16_t(r + 1)) > 0 ? sm(l, uint16_t(r + 1)) : -1.0);
            }
            return 0;
        }
        const int window = argc >= 6 ? std::atoi(argv[5]) : 8;
        std::vector<std::array<uint16_t, 2>> pairs;  // (each rect variant's template halves)
        {
            std::map<std::pair<uint16_t, int>, std::array<uint16_t, 2>> h;
            for (const auto& x : tg)
                if (x.g != sg && x.g != sg + 1) h[{x.g, x.k}][size_t(x.h)] = x.tv;
            for (const auto& [k, v] : h)
                if (v[0] && v[1]) pairs.push_back(v);
        }
        for (int nSeeds = 1; nSeeds <= (std::getenv("SEEDS") ? std::atoi(std::getenv("SEEDS")) : 3); ++nSeeds) {
            int right = 0, all = 0, runs = 0, confRight = 0, confAll = 0, rectRight = 0;
            for (size_t first = 0; first < rects.size(); ++first) {
                tilematch::Input in;
                in.tmplTs = &bts, in.modTs = &ts, in.window = window;
                if (const char* e = std::getenv("W_SEAM")) in.wSeam = float(std::atof(e));
                if (const char* e = std::getenv("W_LAYOUT")) in.wLayout = float(std::atof(e));
                if (const char* e = std::getenv("W_COLOR")) in.wColor = float(std::atof(e));
                if (const char* e = std::getenv("W_MASK")) in.wMask = float(std::atof(e));
                if (const char* e = std::getenv("W_LIKE")) in.wLike = float(std::atof(e));
                if (const char* e = std::getenv("LEARN")) in.learn = std::atoi(e) != 0;
                in.pairs = pairs;
                std::set<uint16_t> seedRects;
                for (int k = 0; k < nSeeds; ++k) seedRects.insert(rects[(first + size_t(k) * 5) % rects.size()]);  // (seeds spread out)
                for (const auto& [g, vs] : tilesOf)
                    for (uint16_t v : vs)
                        if (seedRects.count(g) && hand.count(v)) in.known[v] = hand[v];
                        else in.slots.push_back(v);
                const auto sug = tilematch::suggest(in);
                for (const auto& s : sug) {
                    // (right: the hand's tile, or one with the same picture)
                    auto samePic = [&](uint16_t a, uint16_t b) {
                        if (a == b) return true;
                        const auto ga = grayOf(ts, a), gb = grayOf(ts, b);
                        const int ma = ts.megatileFor(a), mb = ts.megatileFor(b);
                        if (ma == mb) return true;
                        int diff = 0;
                        for (int m = 0; m < 16; ++m)
                            for (int k = 0; k < 64; ++k) {
                                const auto ea = ts.megatiles[size_t(ma)][size_t(m)], eb = ts.megatiles[size_t(mb)][size_t(m)];
                                const int xa = (ea & 1) ? 7 - k % 8 : k % 8, xb = (eb & 1) ? 7 - k % 8 : k % 8;
                                diff += ts.minitiles[ea >> 1][size_t(k / 8 * 8 + xa)] != ts.minitiles[eb >> 1][size_t(k / 8 * 8 + xb)];
                            }
                        (void)ga, (void)gb;
                        return diff == 0;
                    };
                    const bool ok = hand.count(s.tmpl) && samePic(hand[s.tmpl], s.mod);
                    right += ok, ++all;
                    rectRight += ok || (halfOf.count(s.tmpl) && handHalf[halfOf[s.tmpl]].count(s.mod));
                    if (std::getenv("SHOW") && first == 0) std::printf("    %s %u:%u -> %u:%u (hand %u:%u) score %.2f conf %.2f\n", ok ? "ok " : "BAD", s.tmpl >> 4, s.tmpl & 15, s.mod >> 4, s.mod & 15, hand[s.tmpl] >> 4, hand[s.tmpl] & 15, s.score, s.confidence);
                    if (s.confidence >= 0.5f) confRight += ok, ++confAll;
                }
                ++runs;
            }
            std::printf("seeds %d rects (%d runs): %d of %d right (%.0f%%), right rect any variant %.0f%%; confident: %d of %d right\n", nSeeds, runs, right, all,
                        all ? 100.0 * right / all : 0.0, all ? 100.0 * rectRight / all : 0.0, confRight, confAll);
        }
        return 0;
    }
    // live <install> <rules .isom> <terrain id>: the suggestions as the editor makes them on the file's terrain (its
    // matched tiles known, every other edge tile a slot), each with its mod tile's group and the two tiles' mean colours
    if (mode == "live") {
        if (argc < 5) return std::fprintf(stderr, "usage: live <install> <rules .isom> <terrain id>\n"), 2;
        terrainrules::Rules r;
        if (!terrainrules::load(argv[3], r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        if (r.tilesets.empty()) return std::fprintf(stderr, "no tilesets\n"), 1;
        const terrainrules::Terrain* t = nullptr;
        for (const auto& x : r.tilesets[0].terrains)
            if (x.t.id == std::atoi(argv[4])) t = &x;
        if (!t) return std::fprintf(stderr, "no terrain\n"), 1;
        Tileset ts, bts;
        if (!ts.load(data, r.tilesets[0].era, err) || !bts.load(data, uint16_t(t->baseEra), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const auto tg = targetsOf(ts, bts, t->t, t->baseGround, t->baseTerrain);
        const int sg = solidTileGroup(ts, t->t.standIn);
        tilematch::Input in;
        in.tmplTs = &bts, in.modTs = &ts;
        std::map<std::pair<uint16_t, int>, std::array<uint16_t, 2>> h;
        for (const auto& x : tg) {
            if (x.g == sg || x.g == sg + 1) continue;
            h[{x.g, x.k}][size_t(x.h)] = x.tv;
            uint16_t v = 0;
            for (const auto& p : t->t.pieces)
                if (const auto it = t->variantOf.find(p); p[0] == x.g && it != t->variantOf.end() && it->second == x.k && !terrainrules::nullHalf(p[size_t(1 + x.h)]))
                    v = p[size_t(1 + x.h)];
            if (v) in.known[x.tv] = v;
            in.slots.push_back(x.tv);
        }
        for (const auto& [k, v] : h)
            if (v[0] && v[1]) in.pairs.push_back(v);
        auto mean = [](const Tileset& s, uint16_t v) {
            const int mt = s.megatileFor(v);
            double c[3] = {0, 0, 0};
            if (mt > 0)
                for (int m = 0; m < 16; ++m)
                    for (int k = 0; k < 64; ++k) {
                        const Rgb p = s.palette[s.minitiles[s.megatiles[size_t(mt)][size_t(m)] >> 1][size_t(k)]];
                        c[0] += p.r, c[1] += p.g, c[2] += p.b;
                    }
            char buf[32];
            std::snprintf(buf, sizeof buf, "%3.0f %3.0f %3.0f", c[0] / 1024, c[1] / 1024, c[2] / 1024);
            return std::string(buf);
        };
        std::printf("known %zu, slots %zu\n", in.known.size(), in.slots.size());
        for (const auto& [a, b] : in.known) std::printf("  known %u:%u (%s) -> %u:%u (%s)\n", a >> 4, a & 15, mean(bts, a).c_str(), b >> 4, b & 15, mean(ts, b).c_str());
        const auto t0 = std::chrono::steady_clock::now();
        const auto sugs = tilematch::suggest(in);
        std::printf("suggest: %lld ms, %zu suggestions\n", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), sugs.size());
        for (const auto& s : sugs)
            std::printf("  %s %u:%u (%s) -> %u:%u (%s) score %.2f conf %.2f\n", s.confidence >= 0.5f ? "SURE" : "    ", s.tmpl >> 4, s.tmpl & 15, mean(bts, s.tmpl).c_str(), s.mod >> 4,
                        s.mod & 15, mean(ts, s.mod).c_str(), s.score, s.confidence);
        return 0;
    }
    // bench <install> <era> [seeds] [window]: every built-in mod terrain of the tileset as an answer key: its template rects
    // (the tileset's own game border), a few of its rects as the matches, the suggestions for the rest; right: a mod
    // tile the terrain has in that rect's half (any variant)
    if (mode == "bench") {
        const uint16_t era = uint16_t(std::atoi(argv[3]));
        const int nSeeds = argc >= 5 ? std::atoi(argv[4]) : 2, window = argc >= 6 ? std::atoi(argv[5]) : 8;
        cmbw::skipTerrainOverride(true);
        Tileset ts;
        if (!ts.load(data, era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        int allRight = 0, all = 0, sureRight = 0, sure = 0, terrains = 0;
        for (const auto& m : ts.modTerrains) {
            if (m.variantOf) continue;
            const auto [g, s] = terrainrules::templateOf(ts, m);
            const auto rects = terrainrules::templateRects(ts, g, s);
            std::map<uint16_t, std::array<std::set<uint16_t>, 2>> truth;  // (rect -> its own tiles, left and right)
            std::map<uint16_t, std::array<std::vector<uint16_t>, 2>> order;
            for (const auto& p : m.pieces)
                if (terrainrules::stateOf(p) == terrainrules::PieceState::Own)
                    for (int h = 0; h < 2; ++h)
                        if (truth[p[0]][size_t(h)].insert(p[size_t(1 + h)]).second) order[p[0]][size_t(h)].push_back(p[size_t(1 + h)]);
            tilematch::Input in;
            in.tmplTs = &ts, in.modTs = &ts, in.window = window;
            if (const char* e = std::getenv("W_COLOR")) in.wColor = float(std::atof(e));
            if (const char* e = std::getenv("W_SEAM")) in.wSeam = float(std::atof(e));
            if (const char* e = std::getenv("W_MASK")) in.wMask = float(std::atof(e));
            if (const char* e = std::getenv("W_LIKE")) in.wLike = float(std::atof(e));
            if (const char* e = std::getenv("LEARN")) in.learn = std::atoi(e) != 0;
            std::map<uint16_t, std::pair<uint16_t, int>> rectOf;  // (template tile -> its rect, half)
            std::vector<uint16_t> withTruth;
            for (const auto& r : rects) {
                if (!truth.count(r.group)) continue;
                withTruth.push_back(r.group);
                for (int sub = 0; sub < 16; ++sub) {
                    const uint16_t L = uint16_t(r.group * 16 + sub), R = uint16_t((r.group + 1) * 16 + sub);
                    if (ts.megatileFor(L) <= 0) continue;
                    in.slots.push_back(L), rectOf[L] = {r.group, 0};
                    if (ts.megatileFor(R) > 0) in.slots.push_back(R), rectOf[R] = {r.group, 1}, in.pairs.push_back({L, R});
                }
            }
            if (withTruth.size() < size_t(nSeeds) + 2) continue;
            // (the seeds: rects spread over the list, each template variant k its k-th own tile, while there are)
            for (int k = 0; k < nSeeds; ++k) {
                const uint16_t rg = withTruth[size_t(k) * withTruth.size() / size_t(nSeeds)];
                int n = 0;
                for (int sub = 0; sub < 16; ++sub)
                    if (ts.megatileFor(uint16_t(rg * 16 + sub)) > 0) {
                        for (int h = 0; h < 2; ++h)
                            if (const auto& o = order[rg][size_t(h)]; size_t(n) < o.size()) in.known[uint16_t((rg + h) * 16 + sub)] = o[size_t(n)];
                        ++n;
                    }
            }
            const auto sug = tilematch::suggest(in);
            int right = 0, sr = 0, sn = 0;
            for (const auto& x : sug) {
                const auto [rg, h] = rectOf[x.tmpl];
                const bool ok = truth[rg][size_t(h)].count(x.mod) > 0;
                right += ok;
                if (const char* d = std::getenv("DEBUG_T"); d && m.name == d) {
                    std::string want;
                    for (uint16_t v : truth[rg][size_t(h)]) want += std::to_string(v >> 4) + ":" + std::to_string(v & 15) + " ";
                    std::printf("    %s %u:%u (rect %u half %d) -> %u:%u conf %.2f   want %s\n", ok ? "ok " : "BAD", x.tmpl >> 4, x.tmpl & 15, rg, h, x.mod >> 4, x.mod & 15, x.confidence, want.c_str());
                }
                if (x.confidence >= 0.5f) sr += ok, ++sn;
            }
            allRight += right, all += int(sug.size()), sureRight += sr, sure += sn, ++terrains;
            std::set<int> tgs;
            for (const auto& [rg, hs] : truth)
                for (const auto& hh : hs)
                    for (uint16_t v : hh) tgs.insert(v >> 4);
            std::string groups;
            for (int x : tgs) groups += std::to_string(x) + " ";
            if (std::getenv("SHOW"))
                std::printf("  %-34s %3d of %3d right, sure %3d of %3d   seeds %zu, rects %zu, mod groups %s\n", m.name.c_str(), right, int(sug.size()), sr, sn, in.known.size(), withTruth.size(),
                            groups.substr(0, 110).c_str());
        }
        std::printf("era %u, %d terrains, seeds %d: %d of %d right (%.0f%%); sure: %d of %d right (%.0f%%)\n", era, terrains, nSeeds, allRight, all, all ? 100.0 * allRight / all : 0.0, sureRight,
                    sure, sure ? 100.0 * sureRight / sure : 0.0);
        return 0;
    }
    // strip <install> <rules .isom> <terrain id> <rects to keep> <out .isom>: the terrain with only its first edge rects'
    // tiles (the seeds of a suggestions test), the others and the inside NULL
    // seedfile <install> <era> <terrain name> <seed rects> <out .isom>: a rules file for the mapper with the mod's built-in
    // terrain of that name, its template the game border it is made from, only a few of its edge rects (spread over
    // them, each variant in its place) and its inside kept: a demo of the suggestions on a terrain the mod has
    if (mode == "seedfile" && argc >= 7) {
        const uint16_t era = uint16_t(std::atoi(argv[3]));
        cmbw::skipTerrainOverride(true);
        Tileset ts;
        if (!ts.load(data, era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        terrainrules::TilesetRules all = terrainrules::rulesOf(ts);
        terrainrules::Terrain x;
        bool found = false;
        for (const auto& t : all.terrains)
            if (t.t.name == argv[4] && !t.t.variantOf) x = t, found = true;
        if (!found) return std::fprintf(stderr, "no terrain %s\n", argv[4]), 1;
        const auto [g, s] = terrainrules::templateOf(ts, x.t);
        x.baseEra = era, x.baseGround = g, x.baseTerrain = s, x.t.draft = true;
        const int sg = solidTileGroup(ts, x.t.standIn);
        std::vector<uint16_t> edges;
        for (const auto& p : x.t.pieces)
            if (p[0] != sg && p[0] != sg + 1 && terrainrules::stateOf(p) == terrainrules::PieceState::Own && std::find(edges.begin(), edges.end(), p[0]) == edges.end())
                edges.push_back(p[0]);
        const int n = std::max(1, std::atoi(argv[5]));
        std::set<uint16_t> seeds;
        for (int k = 0; k < n && !edges.empty(); ++k) seeds.insert(edges[size_t(k) * edges.size() / size_t(n)]);
        std::map<uint16_t, uint8_t> nth;  // (a rect's pieces in order: variant 0, 1, ...)
        for (auto& p : x.t.pieces) {
            const bool keep = p[0] == sg || p[0] == sg + 1 || seeds.count(p[0]);
            if (keep) x.variantOf[p] = nth[p[0]]++;
            else p[1] = 0, p[2] = 1;
        }
        terrainrules::normalize(ts, x.t);
        terrainrules::Rules r;
        all.terrains = {x};
        r.tilesets.push_back(all);
        std::printf("%s: id %u, template %u/%u, %zu edge rects, seeds", x.t.name.c_str(), x.t.id, g, s, edges.size());
        for (uint16_t q : seeds) std::printf(" %u", q);
        std::printf("\n");
        return terrainrules::save(argv[6], r, err) ? 0 : (std::fprintf(stderr, "%s\n", err.c_str()), 1);
    }
    if (mode == "strip" && argc >= 7) {
        terrainrules::Rules r;
        if (!terrainrules::load(argv[3], r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        Tileset ts;
        if (!ts.load(data, r.tilesets[0].era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        for (auto& t : r.tilesets[0].terrains) {
            if (t.t.id != std::atoi(argv[4])) continue;
            const int sg = solidTileGroup(ts, t.t.standIn);
            std::vector<uint16_t> kept;
            terrainrules::Terrain x = t;
            x.variantOf.clear();
            for (auto& p : x.t.pieces) {
                const auto old = p;
                const bool inside = p[0] == sg || p[0] == sg + 1;
                if (!inside && std::find(kept.begin(), kept.end(), p[0]) == kept.end() && int(kept.size()) < std::atoi(argv[5])) kept.push_back(p[0]);
                if (inside && std::getenv("KEEP_INSIDE")) {  // (the inside kept too: the canvas shows the edges on it)
                    if (const auto it = t.variantOf.find(old); it != t.variantOf.end()) x.variantOf[p] = it->second;
                } else if (inside || std::find(kept.begin(), kept.end(), p[0]) == kept.end()) p[1] = 0, p[2] = 1;
                else if (const auto it = t.variantOf.find(old); it != t.variantOf.end()) x.variantOf[p] = it->second;
            }
            terrainrules::normalize(ts, x.t);
            t = x;
        }
        return terrainrules::save(argv[6], r, err) ? 0 : (std::fprintf(stderr, "%s\n", err.c_str()), 1);
    }
    if (mode == "check") {  // check <install> <rules .isom> <terrain id> <g0> <g1>: the program's choice against the file's
        terrainrules::Rules r;
        if (!terrainrules::load(argv[3], r, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const auto* t = [&]() -> const terrainrules::Terrain* {
            for (const auto& s : r.tilesets)
                for (const auto& x : s.terrains)
                    if (x.t.id == std::atoi(argv[4])) return &x;
            return nullptr;
        }();
        if (!t) return std::fprintf(stderr, "no terrain\n"), 1;
        Tileset ts, bts;
        if (!ts.load(data, r.tilesets[0].era, err) || !bts.load(data, uint16_t(t->baseEra), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        const auto tg = targetsOf(ts, bts, t->t, t->baseGround, t->baseTerrain);
        const auto a = assign(ts, bts, tg, std::atoi(argv[5]), std::atoi(argv[6]), false);
        int same = 0, diff = 0, none = 0;
        for (const auto& x : tg) {
            uint16_t want = 0;
            for (const auto& p : t->t.pieces)
                if (const auto it = t->variantOf.find(p); p[0] == x.g && it != t->variantOf.end() && it->second == x.k) want = p[size_t(1 + x.h)];
            const auto it = a.find(x.tv);
            const uint16_t got = it != a.end() ? it->second : 0;
            if (want <= 1) ++none;
            else if (got == want) ++same;
            else ++diff, std::printf("  g%u k%d h%d: tmpl %u:%u  hand %u:%u  program %u:%u\n", x.g, x.k, x.h, x.tv >> 4, x.tv & 15, want >> 4, want & 15, got >> 4, got & 15);
        }
        std::printf("targets %zu: same %d, different %d, hand has none %d\n", tg.size(), same, diff, none);
        return 0;
    }
    if (mode == "match" && argc >= 14) {
        const uint16_t era = uint16_t(std::atoi(argv[3]));
        const int baseEra = std::atoi(argv[4]);
        const uint16_t bg0 = uint16_t(std::atoi(argv[5])), bt0 = uint16_t(std::atoi(argv[6]));
        Tileset ts, bts;
        if (!ts.load(data, era, err) || !bts.load(data, uint16_t(baseEra), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        auto m = modOf(uint16_t(std::atoi(argv[11])), argv[12], uint16_t(std::atoi(argv[7])), uint16_t(std::atoi(argv[8])), 0);
        if (argc >= 15 && std::atoi(argv[14]) > 0) m.groundTemplate = uint16_t(std::atoi(argv[14]));
        const auto tg = targetsOf(ts, bts, m, bg0, bt0);
        const auto a = assign(ts, bts, tg, std::atoi(argv[9]), std::atoi(argv[10]), true);
        auto t = build(ts, m, tg, a, baseEra, bg0, bt0);
        terrainrules::Rules r;
        if (argc >= 16) terrainrules::load(argv[15], r, err);
        if (r.tilesets.empty()) r.tilesets.push_back({uint8_t(era), uint32_t(ts.groups.size()), terrainrules::fingerprint(ts), {}});
        auto& list = r.tilesets[0].terrains;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& x) { return x.t.id == t.t.id; }), list.end());
        list.push_back(t);
        const auto [done, all] = terrainrules::completeness(ts, t.t);
        std::printf("targets %zu, pieces %zu, complete %d of %d\n", tg.size(), t.t.pieces.size(), done, all);
        return terrainrules::save(argv[13], r, err) ? 0 : (std::fprintf(stderr, "%s\n", err.c_str()), 1);
    }
    return 2;
}
