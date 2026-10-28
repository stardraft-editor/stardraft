// For the mod's team: how each mod terrain of a tileset is made, and which pieces its set lacks. Each terrain is painted
// on the map in an L (inner corners), a thin line one diamond wide and one diamond, on its ground; then:
//   - its picture as painted (<out>/<era>_<n>_paint.png), and the same place with each of its pieces as the game's tile it
//     stands for (<era>_<n>_template.png: the game's border that the ISOM gives there);
//   - the pieces it has: for each game tile group, the game's tile and the set's first piece for it (<era>_<n>_game_<g>.png,
//     <era>_<n>_map_<g>.png);
//   - the pieces it lacks: the game's border it is made as (every rect the ISOM can give between its stand-in and its
//     ground's template terrain: the game's tile groups for them) less the groups it has pieces for (<era>_<n>_miss_<g>.png:
//     the game's tile, the one the set would need a piece for). The editor puts the null tile there (black): the gap
//     shows, for the mod's makers to fill;
//   - painted border rects that kept the game's tile all the same ("kept").
// All of it in <out>/report_<era>.json, for tools/cmbw_report.py.
// usage: cmbw_report <Cosmonarchy install> <map> <out folder>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

namespace {
const char kNl = 10;  // (a line's end in the JSON)
std::string jsonText(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == 92) o += char(92);
        o += c;
    }
    return o + "\"";
}
// A pair of tiles (a rect) as a picture.
bool pairPicture(const Tileset& ts, uint16_t l, uint16_t r, const fs::path& path) {
    TerrainLayer t;
    t.width = 2, t.height = 1, t.tiles = {l, r};
    return writePng(path, renderTerrain(ts, t, 1));
}
std::string list(const std::vector<std::string>& items) {
    std::string o;
    for (size_t i = 0; i < items.size(); ++i) o += (i ? ", " : "") + items[i];
    return "[" + o + "]";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_report <install> <map> <out>%c", kNl), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s%c", err.c_str(), kNl), 1;
    Editor base(data);
    if (!base.open(argv[2], err)) return std::fprintf(stderr, "%s%c", err.c_str(), kNl), 1;
    const fs::path out = argv[3];
    fs::create_directories(out);
    const Tileset& ts = base.tileset();
    const int era = int(ts.era & 7);
    const IsomTerrainTable* game = IsomTerrainTable::forEra(uint16_t(era));
    const IsomTiles* tiles = IsomTiles::forTileset(ts);
    if (!game || !tiles) return std::fprintf(stderr, "no table for the tileset%c", kNl), 1;
    // The game's border of two game terrains: every rect the ISOM gives there, as the tile groups the game may take for it
    // (its left groups; a rect wholly in one terrain, its solid tiles, left out).
    auto borderRects = [&](uint16_t a, uint16_t b) {
        std::set<uint16_t> shapes, edge;
        for (int k = 0; k < 16; ++k) {
            const std::array<uint16_t, 4> c{uint16_t(k & 1 ? b : a), uint16_t(k & 2 ? b : a), uint16_t(k & 4 ? b : a), uint16_t(k & 8 ? b : a)};
            if (const uint16_t sh = game->shapeFor(c)) {
                shapes.insert(sh);
                if (k != 0 && k != 15) edge.insert(sh);
            }
        }
        std::set<uint16_t> solid;
        // (each rect: its groups -> its parity and its two diamonds' corners, a bit each: 1 the stand-in, 0 the ground;
        // left, top, right, bottom)
        std::map<std::vector<uint16_t>, std::array<int, 3>> rects;
        auto bits = [&](uint16_t shape) {
            const auto c = game->corners(shape);
            int b = 0;
            for (int i = 0; i < 4; ++i) b |= (c[size_t(i)] == a ? 1 : 0) << i;
            return b;
        };
        for (int par = 0; par < 2; ++par)
            for (uint16_t s : shapes)
                if (!edge.count(s))
                    for (uint16_t g : tiles->groups(par, s, s)) solid.insert(uint16_t(g & ~1u));
        for (int par = 0; par < 2; ++par)
            for (uint16_t l : shapes)
                for (uint16_t r : shapes)
                    if (edge.count(l) || edge.count(r)) {
                        std::set<uint16_t> options;
                        for (uint16_t g : tiles->groups(par, l, r))
                            if (!solid.count(uint16_t(g & ~1u))) options.insert(uint16_t(g & ~1u));
                        if (!options.empty()) rects.try_emplace({options.begin(), options.end()}, std::array<int, 3>{par, bits(l), bits(r)});
                    }
        return rects;
    };
    // The reference for a rect's pattern: the game's Badlands Asphalt (5) on Dirt (1), whose edges show clearly
    // (<out>/ref_<parity>_<left bits>_<right bits>.png, once).
    Tileset badlands;
    const bool haveRef = badlands.load(data, 0, err);
    const IsomTerrainTable* refTable = IsomTerrainTable::forEra(0);
    const IsomTiles* refTiles = haveRef ? IsomTiles::forTileset(badlands) : nullptr;
    auto reference = [&](const std::array<int, 3>& p) {
        const std::string name = "ref_" + std::to_string(p[0]) + "_" + std::to_string(p[1]) + "_" + std::to_string(p[2]) + ".png";
        if (!refTiles || !refTable || fs::exists(out / name)) return;
        auto shape = [&](int b) {
            std::array<uint16_t, 4> c{};
            for (int i = 0; i < 4; ++i) c[size_t(i)] = (b >> i & 1) ? 5 : 1;
            return refTable->shapeFor(c);
        };
        const auto& gs = refTiles->groups(p[0], shape(p[1]), shape(p[2]));
        if (!gs.empty()) pairPicture(badlands, uint16_t(gs.front() << 4), uint16_t((gs.front() + 1) << 4), out / name);
    };
    // Where each part sits: `terrain` painted on `ground` in a big diamond, notches turned four ways, thin lines both ways,
    // one diamond and pairs touching or one apart; for each part's corners the first rect that has them: a crop of 10 x 7
    // tiles around it (<out>/<prefix>_<parity>_<left bits>_<right bits>.png; the rect at tile 4, 3 of it), for
    // tools/cmbw_report.py. On Badlands the game's own Asphalt (5, a flat edge: "ctx_e") and High Dirt (2, a cliff: "ctx_c")
    // on Dirt; for each terrain its own ("own_<era>_<n>") and its template's on its tileset ("tpl_<era>_<stand-in>_<ground>").
    auto examples = [&](uint16_t terrain, uint16_t ground, const std::string& prefix) {
        Editor e(data);
        if (!e.open(argv[2], err)) return;
        const int cx = e.width() * 16, cy = e.height() * 16;
        e.paintBrush(cx, cy, ground, Brush{Brush::Diamond, 48.0f});
        e.paintBrush(cx - 14 * 64, cy, terrain, Brush{Brush::Diamond, 6.0f});  // (a big diamond)
        for (int k : {0, 1, 2}) e.paintBrush(cx - 6 * 64 + k * 64, cy - 4 * 32 + k * 32, terrain, Brush{Brush::Diamond, 3.0f});  // (an L:
        e.paintBrush(cx - 6 * 64, cy + 2 * 32, terrain, Brush{Brush::Diamond, 3.0f});                                          //  notches)
        for (int k : {0, 1, 2}) e.paintBrush(cx + 2 * 64 - k * 64, cy + 6 * 32 + k * 32, terrain, Brush{Brush::Diamond, 3.0f});
        e.paintBrush(cx + 2 * 64, cy + 12 * 32, terrain, Brush{Brush::Diamond, 3.0f});
        for (int k = 0; k < 5; ++k) e.paintBrush(cx + 4 * 64 + k * 64, cy - 8 * 32 + k * 32, terrain, Brush{Brush::Diamond, 1.0f});  // (thin)
        e.paintBrush(cx + 12 * 64, cy + 2 * 32, terrain, Brush{Brush::Diamond, 1.0f});  // (one diamond)
        e.paintBrush(cx + 6 * 64, cy + 8 * 32, terrain, Brush{Brush::Diamond, 1.0f});   // (two touching at a corner)
        e.paintBrush(cx + 7 * 64, cy + 9 * 32, terrain, Brush{Brush::Diamond, 1.0f});
        // (more: a thin line the other way, L notches turned, pairs touching or one apart each way)
        for (int k = 0; k < 5; ++k) e.paintBrush(cx - 12 * 64 + k * 64, cy - 6 * 32 - k * 32, terrain, Brush{Brush::Diamond, 1.0f});
        for (int k : {0, 1, 2}) e.paintBrush(cx - 14 * 64 + k * 64, cy + 10 * 32 - k * 32, terrain, Brush{Brush::Diamond, 3.0f});
        e.paintBrush(cx - 14 * 64, cy + 6 * 32, terrain, Brush{Brush::Diamond, 3.0f});
        for (int k : {0, 1, 2}) e.paintBrush(cx + 10 * 64 - k * 64, cy - 10 * 32 - k * 32, terrain, Brush{Brush::Diamond, 3.0f});
        e.paintBrush(cx + 10 * 64, cy - 16 * 32, terrain, Brush{Brush::Diamond, 3.0f});
        const std::pair<int, int> pairs[] = {{1, -1}, {2, 0}, {0, 2}, {2, 2}, {2, -2}, {3, 1}, {1, 3}, {3, -1}};
        for (size_t k = 0; k < std::size(pairs); ++k) {
            const int bx0 = cx - 16 * 64 + int(k % 4) * 6 * 64, by0 = cy + (k < 4 ? 14 : 18) * 32;
            e.paintBrush(bx0, by0, terrain, Brush{Brush::Diamond, 1.0f});
            e.paintBrush(bx0 + pairs[k].first * 64, by0 + pairs[k].second * 32, terrain, Brush{Brush::Diamond, 1.0f});
        }
        // (half-size brush: lone dots, lines both ways and across, and Xs, of half diamonds)
        const Brush half{Brush::Diamond, 0.5f};
        const int hy = cy + 30 * 32;
        for (int k = 0; k < 4; ++k) e.paintBrush(cx - 18 * 64 + k * 2 * 64, hy + (k & 1) * 64, terrain, half);
        for (int k = 0; k < 8; ++k) e.paintBrush(cx - 9 * 64 + k * 32, hy - 3 * 32 + k * 16, terrain, half);
        for (int k = 0; k < 8; ++k) e.paintBrush(cx - 4 * 64 + k * 32, hy + 3 * 32 - k * 16, terrain, half);
        for (int k = 0; k < 7; ++k) e.paintBrush(cx + k * 32, hy, terrain, half);
        for (int x0 : {7, 11})
            for (int k = -3; k <= 3; ++k) {
                e.paintBrush(cx + x0 * 64 + k * 32, hy + k * 16, terrain, half);
                e.paintBrush(cx + x0 * 64 + k * 32, hy - k * 16, terrain, half);
            }
        const TerrainLayer t = readTerrain(e.chk(), "TILE");
        if (prefix.rfind("own_", 0) == 0 || prefix.rfind("orig_", 0) == 0) {  // (the whole painting too, its overview: <prefix>_all.png)
            TerrainLayer all;
            const int ax0 = 2 * (cx / 64 - 21), ay0 = cy / 32 - 21;
            all.width = 70, all.height = 58, all.tiles.assign(size_t(all.width) * all.height, 0);
            for (int y = 0; y < all.height; ++y)
                for (int x = 0; x < all.width; ++x) {
                    const int tx = ax0 + x, ty = ay0 + y;
                    if (tx >= 0 && ty >= 0 && tx < t.width && ty < t.height) all.tiles[size_t(y) * all.width + x] = t.tiles[size_t(ty) * t.width + tx];
                }
            writePng(out / (prefix + "_all.png"), renderTerrain(e.tileset(), all, 1));
        }
        std::set<std::array<int, 3>> seen;
        for (int ry = cy / 32 - 24; ry <= cy / 32 + 24; ++ry)
            for (int rx = cx / 64 - 22; rx <= cx / 64 + 22; ++rx) {
                const int par = (rx + ry) & 1;
                const int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
                auto bitsAt = [&](int x, int y) {  // (a diamond's corners of the terrain: west, north, east, south)
                    const std::pair<int, int> cs[4] = {{x - 1, y}, {x, y - 1}, {x + 1, y}, {x, y + 1}};
                    int b = 0;
                    for (int i = 0; i < 4; ++i) b |= (e.terrainAt(cs[i].first * 64, cs[i].second * 32) == terrain ? 1 : 0) << i;
                    return b;
                };
                const std::array<int, 3> pat{par, bitsAt(ax, ay), bitsAt(bx, by)};
                if ((pat[1] == 0 && pat[2] == 0) || (pat[1] == 15 && pat[2] == 15) || !seen.insert(pat).second) continue;
                TerrainLayer crop;
                crop.width = 10, crop.height = 7, crop.tiles.assign(70, 0);
                for (int y = 0; y < 7; ++y)
                    for (int x = 0; x < 10; ++x) {
                        const int tx = 2 * rx - 4 + x, ty = ry - 3 + y;
                        if (tx >= 0 && ty >= 0 && tx < t.width && ty < t.height) crop.tiles[size_t(y) * 10 + x] = t.tiles[size_t(ty) * t.width + tx];
                    }
                writePng(out / (prefix + "_" + std::to_string(pat[0]) + "_" + std::to_string(pat[1]) + "_" + std::to_string(pat[2]) + ".png"),
                         renderTerrain(e.tileset(), crop, 1));
            }
    };
    if (era == 0 && !fs::exists(out / "ctx_done")) {
        examples(5, 1, "ctx_e"), examples(2, 1, "ctx_c");
        std::FILE* done = std::fopen((out / "ctx_done").string().c_str(), "wb");
        if (done) std::fclose(done);
    }
    std::set<std::pair<uint16_t, uint16_t>> templatesShown;
    // (a terrain's height: of its solid tiles)
    auto height = [&](uint16_t terrain) {
        const uint16_t s = game->shapeFor({terrain, terrain, terrain, terrain});
        const auto& gs = s ? tiles->groups(0, s, s) : std::vector<uint16_t>{};
        return gs.empty() ? 0 : cmbw::tileHeight(ts, uint16_t(gs.front() << 4));
    };
    const int partW = 40, partH = 22;
    std::string json = "{\"era\": " + std::to_string(era) + ", \"tileset\": " + jsonText(tilesetName(uint16_t(era))) + ", \"terrains\": [";
    json += kNl;
    int n = 0;
    for (const Tileset::ModTerrain& m : ts.modTerrains) {
        if (m.variantOf) continue;  // (a variant: its set on another ground, the same pieces)
        Editor e(data);
        if (!e.open(argv[2], err)) return 1;
        const int cx = e.width() * 16, cy = e.height() * 16;
        e.paintBrush(cx, cy, m.ground, Brush{Brush::Diamond, 40.0f});
        const int lx = cx - 12 * 32, ly = cy;
        e.paintBrush(lx, ly - 3 * 32, m.id, Brush{Brush::Diamond, 4.0f});
        e.paintBrush(lx + 2 * 64, ly - 1 * 32, m.id, Brush{Brush::Diamond, 4.0f});
        e.paintBrush(lx, ly + 1 * 32, m.id, Brush{Brush::Diamond, 4.0f});
        for (int k = 0; k < 5; ++k) e.paintBrush(cx + 2 * 32 + k * 64, cy - 4 * 32 + k * 32, m.id, Brush{Brush::Diamond, 1.0f});
        e.paintBrush(cx + 15 * 32, cy - 2 * 32, m.id, Brush{Brush::Diamond, 1.0f});
        const TerrainLayer t = readTerrain(e.chk(), "TILE");
        const int x0 = cx / 32 - partW / 2, y0 = cy / 32 - partH / 2;
        // Its pieces: a mod tile -> the game's tile group it stands for; the game groups it has pieces for.
        std::map<uint16_t, uint16_t> gameOf;
        std::map<uint16_t, std::pair<uint16_t, uint16_t>> firstPiece;
        std::map<uint16_t, int> variants;
        for (size_t i = 0; i < m.setPieces && i < m.pieces.size(); ++i) {  // (the set's own pieces: the others are borrowed)
            const auto& p = m.pieces[i];
            gameOf[p[1]] = p[0], gameOf[p[2]] = uint16_t(p[0] + 1);
            firstPiece.try_emplace(p[0], std::pair{p[1], p[2]});
            ++variants[p[0]];
        }
        TerrainLayer paint, templ;
        paint.width = templ.width = uint16_t(partW), paint.height = templ.height = uint16_t(partH);
        paint.tiles.assign(size_t(partW) * partH, 0), templ.tiles.assign(size_t(partW) * partH, 0);
        std::map<uint16_t, int> kept;  // game group (left) -> painted border rects that kept the game's tile
        for (int y = 0; y < partH; ++y)
            for (int x = 0; x < partW; ++x) {
                const int tx = x0 + x, ty = y0 + y;
                const uint16_t v = tx >= 0 && ty >= 0 && tx < t.width && ty < t.height ? t.tiles[size_t(ty) * t.width + tx] : 0;
                paint.tiles[size_t(y) * partW + x] = v;
                const auto g = gameOf.find(v);
                templ.tiles[size_t(y) * partW + x] = g != gameOf.end() ? uint16_t(g->second << 4) : v;
                if (tx & 1) continue;
                bool mine = false, other = false;  // (a border rect: corners of this terrain and of another)
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dx = 0; dx <= 1; ++dx) {
                        const int c = tx / 2 + dx, r = ty + dy;
                        if ((c + r) & 1) (e.terrainAt(c * 64, r * 32) == m.id ? mine : other) = true;
                    }
                if (mine && other && !gameOf.count(v)) ++kept[uint16_t(v >> 4)];
            }
        const std::string stem = std::to_string(era) + "_" + std::to_string(n);
        writePng(out / (stem + "_paint.png"), renderTerrain(ts, paint, 1));
        writePng(out / (stem + "_template.png"), renderTerrain(ts, templ, 1));
        // (the template: its stand-in against its ground's game terrain in the template)
        uint16_t against = m.groundTemplate ? m.groundTemplate : m.ground;
        for (int i = 0; i < 8 && against >= Tileset::kModTerrainIds; ++i)
            if (const Tileset::ModTerrain* g = ts.modTerrain(against)) against = g->groundTemplate ? g->groundTemplate : g->standIn;
        const auto needed = borderRects(m.standIn, against);
        // (its examples: its own parts in it, the parts it lacks in its template, once a template)
        examples(m.id, m.ground, "own_" + std::to_string(era) + "_" + std::to_string(n));
        if (templatesShown.insert({m.standIn, against}).second)
            examples(m.standIn, against, "tpl_" + std::to_string(era) + "_" + std::to_string(m.standIn) + "_" + std::to_string(against));
        // (its kind: a cliff when its two terrains' solid tiles stand at different heights, the higher one's side; else
        // a flat edge, as a path or grass on the ground)
        const int hStand = height(m.standIn), hAgainst = height(against);
        const char* kind = hStand > hAgainst ? "cliff up" : hStand < hAgainst ? "cliff down" : "edge";
        std::vector<std::string> pieces, missing, keptList;
        size_t alternates = 0;  // (groups of covered rects the set has no piece for: more variety, not a gap)
        for (const auto& [g, p] : firstPiece) {
            pairPicture(ts, uint16_t(g << 4), uint16_t((g + 1) << 4), out / (stem + "_game_" + std::to_string(g) + ".png"));
            pairPicture(ts, p.first, p.second, out / (stem + "_map_" + std::to_string(g) + ".png"));
            pieces.push_back("{\"game\": " + std::to_string(g) + ", \"variants\": " + std::to_string(variants[g]) + ", \"tiles\": [" +
                             std::to_string(p.first) + ", " + std::to_string(p.second) + "], \"mega\": [" + std::to_string(ts.megatileFor(p.first)) +
                             ", " + std::to_string(ts.megatileFor(p.second)) + "]}");
        }
        for (const auto& [options, pattern] : needed) {
            const bool covered = std::any_of(options.begin(), options.end(), [&](uint16_t g) { return firstPiece.count(g) != 0; });
            if (covered) {
                for (uint16_t g : options) alternates += !firstPiece.count(g);
                continue;
            }
            const uint16_t g = options.front();  // (a rect with no piece: the game's first tile for it)
            pairPicture(ts, uint16_t(g << 4), uint16_t((g + 1) << 4), out / (stem + "_miss_" + std::to_string(g) + ".png"));
            missing.push_back(std::to_string(g));
        }
        for (const auto& [g, count] : kept) keptList.push_back("{\"game\": " + std::to_string(g) + ", \"rects\": " + std::to_string(count) + "}");
        std::vector<std::string> rectList;  // (each rect of the border: the game's groups for it)
        for (const auto& [options, pattern] : needed) {
            std::vector<std::string> o;
            for (uint16_t g : options) o.push_back(std::to_string(g));
            reference(pattern);
            rectList.push_back("{\"o\": " + list(o) + ", \"p\": " + std::to_string(pattern[0]) + ", \"l\": " + std::to_string(pattern[1]) +
                               ", \"r\": " + std::to_string(pattern[2]) + "}");
        }
        json += std::string(n ? "," : "") + kNl;
        json += "  {\"n\": " + std::to_string(n) + ", \"id\": " + std::to_string(m.id) + ", \"name\": " + jsonText(m.name) + ", \"standIn\": " +
                jsonText(e.terrainName(m.standIn)) + ", \"ground\": " + jsonText(e.terrainName(m.ground)) + ", \"template\": " +
                jsonText(e.terrainName(m.standIn) + " / " + e.terrainName(against)) + ", \"kind\": " + jsonText(kind) + ", \"templateIds\": [" + std::to_string(m.standIn) +
                ", " + std::to_string(against) + "], \"fill\": " + (m.fill ? "true" : "false") +
                ", \"fromMaps\": " + (m.fromMaps ? "true" : "false") + ", \"emptyBlocks\": " + std::to_string(m.emptyBlocks) +
                ", \"rects\": " + list(rectList) + ", \"alternates\": " + std::to_string(alternates) + ", \"pieces\": " + list(pieces) + ", \"missing\": " + list(missing) +
                ", \"kept\": " + list(keptList) + "}";
        std::printf("%3d %-40s template %s / %s: %zu rects, pieces for %zu groups, rects with none %zu (%zu other groups); kept %zu%c", n, m.name.c_str(),
                    e.terrainName(m.standIn).c_str(), e.terrainName(against).c_str(), needed.size(), firstPiece.size(), missing.size(),
                    alternates, kept.size(), kNl);
        ++n;
    }
    json += kNl;
    // The game's own terrains of the tileset, each on the terrain next to it toward the first one, painted like the mod's
    // (<out>/orig_<era>_<id>_...): a part a mod terrain lacks shows in the game terrain most like it (tools/cmbw_report.py
    // picks it by name and kind).
    json += "], \"game\": [";
    bool firstGame = true;
    for (uint16_t g = 1; g <= game->terrainCount(); ++g) {
        const auto toFirst = game->isTerrain(g) ? game->path(g, 1) : std::vector<uint16_t>{};
        if (toFirst.size() < 2) continue;
        const uint16_t under = toFirst[1];
        examples(g, under, "orig_" + std::to_string(era) + "_" + std::to_string(g));
        // (its parts as a mod terrain's: each rect's groups and pattern; its tile for each: origtile_<era>_<id>_<pattern>.png
        // and <era>_g<id>_map_<group>.png, the standard terrains the report shows to compare with)
        std::string rects;
        for (const auto& [groups, pat] : borderRects(g, under)) {
            const std::string patName = std::to_string(pat[0]) + "_" + std::to_string(pat[1]) + "_" + std::to_string(pat[2]);
            pairPicture(ts, uint16_t(groups.front() << 4), uint16_t((groups.front() + 1) << 4),
                        out / ("origtile_" + std::to_string(era) + "_" + std::to_string(g) + "_" + patName + ".png"));
            pairPicture(ts, uint16_t(groups.front() << 4), uint16_t((groups.front() + 1) << 4),
                        out / (std::to_string(era) + "_g" + std::to_string(g) + "_map_" + std::to_string(groups.front()) + ".png"));
            std::string o;
            for (uint16_t gr : groups) o += (o.empty() ? "" : ", ") + std::to_string(gr);
            rects += std::string(rects.empty() ? "" : ", ") + "{\"o\": [" + o + "], \"p\": " + std::to_string(pat[0]) + ", \"l\": " +
                     std::to_string(pat[1]) + ", \"r\": " + std::to_string(pat[2]) + "}";
        }
        const int hg = height(g), hu = height(under);
        json += std::string(firstGame ? "" : ",") + kNl + "  {\"id\": " + std::to_string(g) + ", \"name\": " + jsonText(game->terrainName(g)) +
                ", \"groundName\": " + jsonText(game->terrainName(under)) + ", \"ground\": " + std::to_string(under) + ", \"kind\": " +
                jsonText(hg > hu ? "cliff up" : hg < hu ? "cliff down" : "edge") + ", \"rects\": [" + rects + "]}";
        firstGame = false;
    }
    json += kNl;
    json += "]}";
    json += kNl;
    std::FILE* f = std::fopen((out / ("report_" + std::to_string(era) + ".json")).string().c_str(), "wb");
    if (!f) return 1;
    std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    return 0;
}
