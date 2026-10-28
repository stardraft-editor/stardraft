// Cosmonarchy BW maps (src/mods/cmbw.cpp): the mod's tile groups' terrains, the isometric brush's mod tiles, CRIG word
// lists, and an Askosi player saved and opened again. A standard tileset gets none of the mod's tables.
//
// usage: cmbw_test <Cosmonarchy install (the folder with mpq\)> <a Cosmonarchy map> <scratch dir> [Remastered install]

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", msg); \
        std::fflush(stdout);                                 \
        if (!ok_) ++failures;                                \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_test <Cosmonarchy install> <Cosmonarchy map> <scratch dir> [Remastered install]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "cannot open the Cosmonarchy data: %s\n", err.c_str()), 1;

    // The learned terrains: badlands group 1665 is High Dirt (5) and its border with 8 (src/mods/cmbw_isom.inc).
    Tileset badlands;
    CHECK(badlands.load(data, 0, err), "the mod's badlands tileset loads");
    {
        // A picture (a gradient with a red square) as an 8-bit PCX of 640 x 480: it reads back at that size, near its colors.
        const int w = 320, h = 200;
        std::vector<uint8_t> rgba(size_t(w) * h * 4, 255);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
                const bool red = x > 100 && x < 160 && y > 60 && y < 120;
                p[0] = red ? 220 : uint8_t(x * 255 / w), p[1] = red ? 20 : uint8_t(y * 255 / h), p[2] = red ? 20 : 128;
            }
        const auto pcx = cmbw::encodePcx8(rgba, w, h);
        int bw = 0, bh = 0;
        std::vector<uint8_t> back;
        const bool read = cmbw::decodePcxRgba(pcx, bw, bh, back);
        double err2 = 0;
        if (read && bw == 640 && bh == 480)
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x)
                    for (int k = 0; k < 3; ++k)
                        err2 += std::abs(int(back[(size_t(y) * 640 + x) * 4 + k]) - int(rgba[(size_t(y * h / 480) * w + x * w / 640) * 4 + k]));
        const double mean = err2 / (640.0 * 480 * 3);
        std::printf("      PCX from a picture: %zu bytes, %d x %d, mean color error %.1f\n", pcx.size(), bw, bh, mean);
        CHECK(read && bw == 640 && bh == 480 && mean < 6, "a picture becomes an 8-bit PCX of 640 x 480 near its colors");
    }
    CHECK(badlands.groupTerrains.size() > 500, "the mod's badlands has the learned terrains of its new groups");
    const auto terrains = tileGroupTerrains(badlands);
    CHECK(terrains.size() > 1665 && terrains[1665][0] == 5 && terrains[1665][1] == 8, "badlands group 1665: High Dirt and its border with 8");
    if (argc > 4) {
        GameData classic;
        Tileset standard;
        CHECK(classic.open(argv[4], err) && standard.load(classic, 0, err) && standard.groupTerrains.empty(),
              "a standard badlands tileset has no learned terrains");
    }

    // The brush: on a Cosmonarchy map, plain terrain painted with it gets some of the mod's tiles of that terrain.
    Editor ed(data);
    CHECK(ed.open(argv[2], err), "the Cosmonarchy map opens");
    {
        // The mining simulation's units: each unit as the game's unit of its role in the mod.
        auto unitRow = [](uint16_t type, uint8_t owner) {
            std::vector<uint8_t> u(36, 0);
            u[8] = uint8_t(type), u[9] = uint8_t(type >> 8), u[16] = owner;
            return u;
        };
        std::vector<uint8_t> in;
        const std::pair<uint16_t, uint8_t> rows[] = {{20, 0}, {439, 0}, {41, 1}, {429, 0}, {131, 1}, {221, 11}, {514, 11}, {457, 0}, {149, 1}, {214, 0}};
        for (auto [t, o] : rows) {
            const auto r = unitRow(t, o);
            in.insert(in.end(), r.begin(), r.end());
        }
        uint16_t other = 0;  // (a unit of the mod's own that is no worker, depot or resource)
        for (uint16_t t = 228; t < ed.unitTypes().size() && !other; ++t)
            if (!ed.unitTypes()[t].is(UnitFlag::Worker) && !ed.unitTypes()[t].is(UnitFlag::ResourceDepot) && !ed.unitTypes()[t].is(UnitFlag::ResourceContainer))
                other = t;
        const auto r = unitRow(other, 0);
        in.insert(in.end(), r.begin(), r.end());
        const auto out = cmbw::simUnits(in, ed.unitTypes());
        std::vector<int> types;
        for (size_t i = 0; i + 36 <= out.size(); i += 36) types.push_back(out[i + 8] | out[i + 9] << 8);
        const std::vector<int> want = {7, 7, 41, 106, 131, 188, 176, 110, 149, 214};
        std::printf("      simulation units:");
        for (int t : types) std::printf(" %d", t);
        std::printf("\n");
        CHECK(types == want, "the mining simulation reads the mod's workers, depots and resources as the game's of their roles");
    }
    if (!ed.isOpen()) return 1;
    Tileset ts;
    ts.load(data, ed.era(), err);
    std::set<uint16_t> modGroups;
    std::map<uint16_t, int> alone;  // terrain -> its mod groups of it alone
    for (const auto& [g, t, other] : ts.groupTerrains) modGroups.insert(g), alone[t] += !other;
    const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
    uint16_t terrain = 0;
    int most = 0;
    for (const auto& [t, n] : alone)
        if (table && table->isTerrain(t) && n > most) terrain = t, most = n;
    CHECK(terrain != 0, "the map's tileset has mod tiles of one terrain alone");
    // The mod tiles at the places that differ from `from` (the strokes' places: the map has mod tiles of its own).
    auto modTiles = [&](const std::vector<uint16_t>& from) {
        const auto now = readTerrain(ed.chk(), "TILE").tiles;
        int n = 0;
        for (size_t k = 0; k < now.size() && k < from.size(); ++k) n += now[k] != from[k] && modGroups.count(uint16_t(now[k] >> 4)) > 0;
        return n;
    };
    const auto start = readTerrain(ed.chk(), "TILE").tiles;
    const size_t doodads = ed.doodads().size();
    // (the editor's brush, and the old engine's paint)
    for (int i = 0; i < 4; ++i) ed.paintBrush(ed.width() * 8 + i * 256, ed.height() * 8, terrain, Brush{Brush::Diamond, 8.0f});
    const int byBrush = modTiles(start);
    // (another terrain first, so that the old engine's paint changes the tiles there)
    uint16_t other = 1;
    while (other == terrain || !table->isTerrain(other)) ++other;
    for (int i = 0; i < 4; ++i) ed.paintTerrain(ed.width() * 8 + i * 256, ed.height() * 16, other, 8);
    const auto mid = readTerrain(ed.chk(), "TILE").tiles;
    for (int i = 0; i < 4; ++i) ed.paintTerrain(ed.width() * 8 + i * 256, ed.height() * 16, terrain, 8);
    const int byPaint = modTiles(mid);
    std::printf("      mod tiles placed: %d by the brush, %d by the old engine's paint (terrain %u)\n", byBrush, byPaint, unsigned(terrain));
    CHECK(byBrush > 0, "the brush placed some of the mod's tiles of that terrain");
    CHECK(byPaint > 0, "the old engine's paint placed some of the mod's tiles of that terrain");
    {
        // The doodads that stay are those the same strokes keep with the mod's tiles off (a stroke that changes the
        // terrain under a doodad takes it away, as on any map; the mod's tiles take none).
        Editor plain(data);
        plain.open(argv[2], err);
        Tileset& pt = const_cast<Tileset&>(plain.tileset());
        pt.groupTerrains.clear(), pt.groupPieces.clear(), pt.modTerrains.clear();
        for (int i = 0; i < 4; ++i) plain.paintBrush(plain.width() * 8 + i * 256, plain.height() * 8, terrain, Brush{Brush::Diamond, 8.0f});
        for (int i = 0; i < 4; ++i) plain.paintTerrain(plain.width() * 8 + i * 256, plain.height() * 16, other, 8);
        for (int i = 0; i < 4; ++i) plain.paintTerrain(plain.width() * 8 + i * 256, plain.height() * 16, terrain, 8);
        std::printf("      doodads: %zu, %zu after the strokes (%zu with the mod's tiles off)\n", doodads, ed.doodads().size(), plain.doodads().size());
        CHECK(ed.doodads().size() == plain.doodads().size(), "the doodads stay (their tiles get no mod tiles)");
    }
    {
        // Each tile the strokes made one of the mod's is a tile of one terrain alone; most are of the painted terrain
        // (the strokes also make the tiles of the diamonds around them again: their terrains' tiles).
        Editor fresh(data);
        fresh.open(argv[2], err);
        const auto was = readTerrain(fresh.chk(), "TILE").tiles, now = readTerrain(ed.chk(), "TILE").tiles;
        int placed = 0, painted = 0, border = 0;
        for (size_t k = 0; k < now.size() && k < was.size(); ++k) {
            const uint16_t g = uint16_t(now[k] >> 4);
            if (now[k] == was[k] || !modGroups.count(g) || modGroups.count(uint16_t(was[k] >> 4))) continue;  // (became a mod tile)
            if (std::any_of(ts.groupPieces.begin(), ts.groupPieces.end(), [g](const auto& p) { return g == p[0] >> 4 || g == p[1] >> 4; })) continue;  // (a piece)
            if (std::any_of(ts.modTerrains.begin(), ts.modTerrains.end(), [&](const Tileset::ModTerrain& m) {
                    return std::any_of(m.pieces.begin(), m.pieces.end(), [&](const auto& p) { return now[k] == p[1] || now[k] == p[2]; });
                }))
                continue;  // (a mod terrain's piece: next to the map's own pieces of it, a terrain painted beside them keeps it)
            ++placed;
            const auto it = std::find_if(ts.groupTerrains.begin(), ts.groupTerrains.end(), [g](const auto& e) { return e[0] == g; });
            if (it == ts.groupTerrains.end() || (*it)[2] != 0) {
                ++border;
                std::printf("      tile %d,%d: %u.%u, was %u.%u\n", int(k % ed.width()), int(k / ed.width()), unsigned(now[k] >> 4), unsigned(now[k] & 15),
                            unsigned(was[k] >> 4), unsigned(was[k] & 15));
            }
            painted += it != ts.groupTerrains.end() && (*it)[1] == terrain;
        }
        std::printf("      %d mod tiles placed, %d of the painted terrain, %d of a border\n", placed, painted, border);
        // (at least half: since a mod tile goes in only with clean edges (edgeOutliers), the painted terrain has fewer looks
        // to place, and its share in this stroke is half)
        CHECK(placed > 0 && border == 0 && painted * 2 >= placed, "the mod tiles placed are of one terrain alone, at least half of the painted one");
    }
    {
        // The mod's pieces (Tileset::groupPieces): each terrain painted at its own places; the rects the brush made with a
        // game piece the mod has pieces for get the mod's pieces too, both tiles of a piece side by side in one rect.
        std::set<uint16_t> pieceLefts, pieceRights;
        std::set<std::pair<uint16_t, uint16_t>> pairs;
        for (const auto& [left, right, game] : ts.groupPieces) pieceLefts.insert(left), pieceRights.insert(right), pairs.insert({left, right});
        CHECK(!pieceLefts.empty(), "the map's tileset has pieces of the mod's");
        Editor painter(data);
        painter.open(argv[2], err);
        const auto was = readTerrain(painter.chk(), "TILE").tiles;
        int spot = 0;
        for (uint16_t t = 1; table && t <= table->terrainCount(); ++t)
            if (table->isTerrain(t))
                for (int i = 0; i < 6; ++i, ++spot)
                    painter.paintBrush(256 + (spot % 12) * (painter.width() * 32 - 512) / 12, 256 + (spot / 12) * 384, t, Brush{Brush::Diamond, 3.0f});
        const auto now = readTerrain(painter.chk(), "TILE").tiles;
        const int w = painter.width();
        int pieces = 0, broken = 0;
        for (size_t k = 0; k + 1 < now.size(); ++k) {
            if (!pieceLefts.count(now[k]) || now[k] == was[k]) continue;
            const bool even = (k % size_t(w)) % 2 == 0;
            if (!even && pieceRights.count(now[k])) continue;  // (a right tile that is a left tile of another piece too)
            ++pieces;
            if (!even || !pairs.count({now[k], now[k + 1]})) {
                ++broken;
                std::printf("      not a whole rect at %d,%d: %u.%u %u.%u (was %u.%u %u.%u)\n", int(k % size_t(w)), int(k / size_t(w)), unsigned(now[k] >> 4),
                            unsigned(now[k] & 15), unsigned(now[k + 1] >> 4), unsigned(now[k + 1] & 15), unsigned(was[k] >> 4), unsigned(was[k] & 15),
                            unsigned(was[k + 1] >> 4), unsigned(was[k + 1] & 15));
            }
        }
        std::printf("      the mod's pieces placed by the brush: %d (%d not a whole rect)\n", pieces, broken);
        // (the painted terrain as a picture in the scratch folder: brush.png)
        writePng(fs::path(argv[3]) / "brush.png", renderTerrain(ts, readTerrain(painter.chk(), "TILE")));
        {
            // The same strokes with the game's pieces only, to compare (brush-game.png).
            Editor plain(data);
            plain.open(argv[2], err);
            const_cast<Tileset&>(plain.tileset()).groupPieces.clear();
            spot = 0;
            for (uint16_t t = 1; table && t <= table->terrainCount(); ++t)
                if (table->isTerrain(t))
                    for (int i = 0; i < 6; ++i, ++spot)
                        plain.paintBrush(256 + (spot % 12) * (plain.width() * 32 - 512) / 12, 256 + (spot / 12) * 384, t, Brush{Brush::Diamond, 3.0f});
            writePng(fs::path(argv[3]) / "brush-game.png", renderTerrain(ts, readTerrain(plain.chk(), "TILE")));
        }
        // A piece goes only where it looks like the game's piece (Editor::sprinkleModTiles: the mean color within 12, the
        // contrast within a quarter) and its outer edges are near the game piece's (the mean difference of the pixels
        // within 16); a tileset with none that does gets none.
        auto look = [&](uint16_t value) {
            std::array<double, 5> s{};  // R, G, B, brightness, brightness squared
            const int m = ts.megatileFor(value);
            int n = 0;
            if (m >= 0)
                for (uint32_t e : ts.megatiles[size_t(m)])
                    if ((e >> 1) < ts.minitiles.size())
                        for (uint8_t p : ts.minitiles[e >> 1]) {
                            const Rgb& c = ts.palette[p];
                            const double l = 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
                            s[0] += c.r, s[1] += c.g, s[2] += c.b, s[3] += l, s[4] += l * l, ++n;
                        }
            for (double& v : s) v /= std::max(n, 1);
            return std::array<double, 4>{s[0], s[1], s[2], std::sqrt(std::max(0.0, s[4] - s[3] * s[3]))};
        };
        auto alike = [&](uint16_t a, uint16_t b) {
            const auto x = look(a), y = look(b);
            const double d = (x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]);
            return d < 144 && std::abs(x[3] - y[3]) < 0.25 * std::max(x[3], y[3]);
        };
        auto edges = [&](uint16_t a, uint16_t b, int sides) {  // (sides: bit 0 left, 1 right, 2 top, 3 bottom)
            auto pixel = [&](uint16_t v, int px, int py) {
                const int m = ts.megatileFor(v);
                if (m < 0) return Rgb{};
                const uint32_t e = ts.megatiles[size_t(m)][size_t(py / 8 * 4 + px / 8)];
                if ((e >> 1) >= ts.minitiles.size()) return Rgb{};
                return ts.palette[ts.minitiles[e >> 1][size_t(py % 8 * 8 + ((e & 1) ? 7 - px % 8 : px % 8))]];
            };
            int sum = 0, n = 0;
            for (int i = 0; i < 32; ++i)
                for (int side = 0; side < 4; ++side) {
                    if (!(sides >> side & 1)) continue;
                    const int px = side == 0 ? 0 : side == 1 ? 31 : i, py = side < 2 ? i : side == 2 ? 0 : 31;
                    const Rgb p = pixel(a, px, py), q = pixel(b, px, py);
                    sum += std::abs(int(p.r) - q.r) + std::abs(int(p.g) - q.g) + std::abs(int(p.b) - q.b), n += 3;
                }
            return sum < 16 * n;
        };
        bool anyFits = false;
        for (const auto& [left, right, game] : ts.groupPieces)
            for (int s = 0; s < 16 && !anyFits; ++s)
                anyFits = ts.groups[game].megatiles[size_t(s)] && alike(left, uint16_t(game << 4 | s)) && alike(right, uint16_t((game + 1) << 4 | s)) &&
                          edges(left, uint16_t(game << 4 | s), 0b1101) && edges(right, uint16_t((game + 1) << 4 | s), 0b1110);
        std::printf("      a piece of the mod's looks like its game piece: %s\n", anyFits ? "yes" : "no");
        CHECK(broken == 0 && (pieces > 0 || !anyFits), "the brush placed the mod's pieces that look like the game's, each a whole rect");
    }

    {
        // The mod's tiles cost little: a big stroke (a diamond of 24) after the first (which reads the tiles' looks).
        Editor big(data);
        big.open(argv[2], err);
        big.paintBrush(big.width() * 16, big.height() * 16, terrain, Brush{Brush::Diamond, 24.0f});
        const auto tilesBefore = readTerrain(big.chk(), "TILE").tiles;
        const auto t0 = std::chrono::steady_clock::now();
        big.paintBrush(big.width() * 16 + 64, big.height() * 16, terrain == 1 ? 2 : 1, Brush{Brush::Diamond, 24.0f});
        big.paintBrush(big.width() * 16 + 64, big.height() * 16, terrain, Brush{Brush::Diamond, 24.0f});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 2;
        std::printf("      a stroke of a diamond of 24: %.1f ms\n", ms);
        int changedTiles = 0;
        const auto tilesAfter = readTerrain(big.chk(), "TILE").tiles;
        for (size_t k = 0; k < tilesAfter.size() && k < tilesBefore.size(); ++k) changedTiles += tilesAfter[k] != tilesBefore[k];
        std::printf("      (%d tiles changed)\n", changedTiles);
        CHECK(changedTiles > 500, "the big strokes changed the terrain");
        CHECK(ms < 150, "a big stroke with the mod's tiles takes less than 150 ms");
    }
    {
        // A mod terrain's pieces stand in for the game's rects only: none for another mod terrain's own tile groups.
        // (a fill of a game terrain's tiles is the game's: not counted)
        std::set<uint16_t> modGroupsOfPieces;
        const IsomTiles* gameRects = IsomTiles::forTileset(ts);
        for (const Tileset::ModTerrain& m : ts.modTerrains)
            for (const auto& p : m.pieces)
                for (uint16_t v : {p[1], p[2]})
                    if (gameRects && gameRects->rectsOf(uint16_t((v >> 4) & ~1u)).empty()) modGroupsOfPieces.insert(uint16_t(v >> 4));
        int crossed = 0;
        for (const Tileset::ModTerrain& m : ts.modTerrains)
            for (const auto& p : m.pieces)
                if (modGroupsOfPieces.count(p[0]) && ++crossed <= 3) std::printf("      (%s: a piece for group %u)\n", m.name.c_str(), unsigned(p[0]));
        if (!ts.modTerrains.empty()) CHECK(crossed == 0, "no mod terrain takes another mod terrain's tiles as its stand-in's");
    }
    {
        // The mod terrains of all tilesets keep their ids (saved maps' ISOM has them: a new row goes after the last of its
        // tileset, and a new terrain goes into kPinned too), and each of their set's blocks gives pieces (a block that
        // gives none: its groups have no tiles, the mod renumbered them).
        struct Pinned {
            uint16_t era, id;
            const char* name;
        };
        static const Pinned kPinned[] = {
#include "cmbw_pinned_ids.inc"
        };
        int moved = 0, unpinned = 0, empty = 0;
        for (uint16_t era = 0; era < 8; ++era) {
            Tileset t;
            if (!t.load(data, era, err)) continue;
            for (const Tileset::ModTerrain& m : t.modTerrains) {
                const Pinned* p = nullptr;
                for (const Pinned& q : kPinned)
                    if (q.era == era && m.name == q.name) p = &q;
                if (!p) ++unpinned, std::printf("      not pinned: {%u, %u, \"%s\"},\n", unsigned(era), unsigned(m.id), m.name.c_str());
                else if (p->id != m.id) ++moved, std::printf("      %s: id %u, pinned %u\n", m.name.c_str(), unsigned(m.id), unsigned(p->id));
                if (m.emptyBlocks) ++empty, std::printf("      %s: %d blocks without pieces\n", m.name.c_str(), m.emptyBlocks);
            }
            for (const Pinned& q : kPinned)
                if (q.era == era && std::none_of(t.modTerrains.begin(), t.modTerrains.end(), [&](const auto& m) { return m.name == q.name; }))
                    ++moved, std::printf("      %s: pinned, but not made\n", q.name);
        }
        CHECK(moved == 0 && unpinned == 0, "the mod terrains keep their ids (tools/tests/cmbw_pinned_ids.inc)");
        CHECK(empty == 0, "each block of each mod terrain's set gives pieces");
        // The neighbor table is the one the maps give now (cmbw_neighbors --inc again after adding or changing terrains).
        const fs::path maps = fs::path(argv[1]).parent_path() / "Starcraft" / "Maps" / "CMBW-Root" / "CMBW-maps";
        if (fs::is_directory(maps)) {
            const auto learned = cmbw::learnPieceNeighbors(data, maps);
            std::array<Tileset, 8> tsets;
            size_t inTable = 0, missing = 0;
            for (uint16_t era = 0; era < 8; ++era)
                if (tsets[era].load(data, era, err)) inTable += tsets[era].pieceNeighbors.size();
            for (const auto& k : learned)
                missing += !tsets[k[0] & 7].pieceNeighbors.count(Tileset::pieceNeighborKey(k[1], k[2], k[3]));
            std::printf("      neighbor pairs: %zu in the table, %zu from the maps now (%zu not in the table)\n", inTable, learned.size(), missing);
            CHECK(inTable == learned.size() && missing == 0, "the neighbor table is the maps' (src/mods/cmbw_neighbors.inc)");
        }
    }
    for (const Tileset::ModTerrain& mt : ts.modTerrains) {
        // Each mod terrain of the tileset, painted on its ground (a big stroke, then small strokes side by side and over
        // each other): its pieces go in, and no tile of its stand-in is left (the set's missing rects: the null tile).
        // (not a variant: the brush takes it for its ground, from the terrain it is a variant of)
        if (mt.variantOf) continue;
        std::set<uint16_t> mine;
        for (const auto& [g, l, r] : mt.pieces) mine.insert(l), mine.insert(r);
        Editor e(data);
        e.open(argv[2], err);
        const int cx = e.width() * 16, cy = e.height() * 16;
        e.paintBrush(cx, cy, mt.ground, Brush{Brush::Diamond, 24.0f});
        e.paintBrush(cx - 300, cy - 150, mt.id, Brush{Brush::Diamond, 4.0f});
        for (int i = 0; i < 18; ++i)
            e.paintBrush(cx - 500 + (i % 6) * 100 + (i / 6) * 30, cy + 150 + (i / 6) * 80, mt.id, Brush{Brush::Diamond, float(1 + i % 3)});
        const auto tt = tileGroupTerrains(ts);
        const TerrainLayer lt = readTerrain(e.chk(), "TILE");
        Editor opened(data);  // (the map's own tiles: those the strokes did not change are not counted)
        opened.open(argv[2], err);
        const TerrainLayer ot = readTerrain(opened.chk(), "TILE");
        std::vector<uint8_t> underDoodad(lt.tiles.size(), 0);  // (a doodad's tiles stay as they are: not counted)
        for (const Doodad& d : e.doodads())
            if (const DoodadType* dt = e.doodadTypes().find(d.id)) {
                const int tx = (int(d.x) - dt->width * 16) / 32, ty = (int(d.y) - dt->height * 16) / 32;
                for (int r = 0; r < dt->height; ++r)
                    for (int c = 0; c < dt->width; ++c)
                        if (tx + c >= 0 && ty + r >= 0 && tx + c < lt.width && ty + r < lt.height) underDoodad[size_t(ty + r) * lt.width + tx + c] = 1;
            }
        int pieces = 0, standInLeft = 0;
        for (int y = std::max(0, cy / 32 - 14); y < cy / 32 + 14 && y < lt.height; ++y)
            for (int x = std::max(0, cx / 32 - 24); x < cx / 32 + 12 && x < lt.width; ++x) {
                const uint16_t v = lt.tiles[size_t(y) * lt.width + x], g = uint16_t(v >> 4);
                pieces += mine.count(v) != 0;
                standInLeft += !mine.count(v) && g < tt.size() && (tt[g][0] == mt.standIn || tt[g][1] == mt.standIn) &&
                               (mt.fill || tt[g][1]) &&  // (edges only: the stand-in's own tiles inside it)
                               ot.tiles[size_t(y) * lt.width + x] != v && !underDoodad[size_t(y) * lt.width + x] && [&] {
                                   const int rx = x / 2;  // (a rect with a corner of the mod terrain: its ISOM rect's corners)
                                   for (int dy = 0; dy <= 1; ++dy)
                                       for (int dx = 0; dx <= 1; ++dx)
                                           if (((rx + dx + y + dy) & 1) && e.terrainAt((rx + dx) * 64, (y + dy) * 32) == mt.id) return true;
                                   return false;
                               }();
                if (std::getenv("CMBW_OTHERS") && !mine.count(v) && !(g < tt.size() && tt[g][0] == mt.ground && !tt[g][1]))
                    std::printf("      (other tile %d,%d: %u.%u, terrains %u %u)\n", x, y, unsigned(g), unsigned(v & 15), g < tt.size() ? unsigned(tt[g][0]) : 0u,
                                g < tt.size() ? unsigned(tt[g][1]) : 0u);
            }
        if (std::getenv("CMBW_OTHERS")) {  // (the stand-in painted the same way, mod terrains off: its rects' groups)
            Editor g(data);
            g.open(argv[2], err);
            const_cast<Tileset&>(g.tileset()).modTerrains.clear();
            g.paintBrush(cx, cy, mt.ground, Brush{Brush::Diamond, 24.0f});
            g.paintBrush(cx - 300, cy - 150, mt.standIn, Brush{Brush::Diamond, 4.0f});
            for (int i = 0; i < 18; ++i)
                g.paintBrush(cx - 500 + (i % 6) * 100 + (i / 6) * 30, cy + 150 + (i / 6) * 80, mt.standIn, Brush{Brush::Diamond, float(1 + i % 3)});
            std::set<uint16_t> covered;
            for (const auto& p : mt.pieces) covered.insert(p[0]);
            std::map<uint16_t, int> uncovered;
            const TerrainLayer gt = readTerrain(g.chk(), "TILE");
            for (int y = std::max(0, cy / 32 - 14); y < cy / 32 + 14 && y < gt.height; ++y)
                for (int x = std::max(0, cx / 32 - 24) & ~1; x < cx / 32 + 12 && x < gt.width; x += 2) {
                    const uint16_t gg = uint16_t(gt.tiles[size_t(y) * gt.width + x] >> 4);
                    if (gg < tt.size() && tt[gg][1]) ++uncovered[gg];
                }
            for (const auto& [gg, n] : uncovered) std::printf("      (the stand-in's rect %u: %d, no block)\n", unsigned(gg), n);
        }
        TerrainLayer part;
        part.width = 40, part.height = 30;
        for (int y = 0; y < part.height; ++y)
            for (int x = 0; x < part.width; ++x) {
                const int sx = std::clamp(cx / 32 - 26 + x, 0, lt.width - 1), sy = std::clamp(cy / 32 - 15 + y, 0, lt.height - 1);
                part.tiles.push_back(lt.tiles[size_t(sy) * lt.width + sx]);
            }
        writePng(fs::path(argv[3]) / ("modterrain-" + std::to_string(mt.id) + ".png"), renderTerrain(ts, part));
        std::printf("      %s: %d of its tiles, %d tiles of its stand-in left\n", mt.name.c_str(), pieces, standInLeft);
        CHECK(pieces > 60 && standInLeft == 0, (mt.name + " paints on its ground with its own pieces only").c_str());
        {  // (Ctrl+click in its stroke picks it, also where mod terrains share tiles: the ISOM decides)
            const uint16_t picked = e.paletteTerrainAt(cx - 300, cy - 150);
            if (picked != mt.id) std::printf("      (Ctrl+click on %s picks terrain %u)\n", mt.name.c_str(), unsigned(picked));
            CHECK(picked == mt.id, ("Ctrl+click on " + mt.name + " picks it").c_str());
        }
        {  // (and its stand-in, painted plain beside it, picks the stand-in: the game terrain, not the mod terrain whose fill it is)
            Editor g(data);
            g.open(argv[2], err);
            const int gx = g.width() * 16, gy = g.height() * 16;
            g.paintBrush(gx, gy, mt.groundTemplate ? mt.groundTemplate : mt.ground < Tileset::kModTerrainIds ? mt.ground : 1, Brush{Brush::Diamond, 16.0f});
            g.paintBrush(gx, gy, mt.standIn, Brush{Brush::Diamond, 6.0f});
            const uint16_t picked = g.paletteTerrainAt(gx, gy);
            if (picked != mt.standIn) std::printf("      (Ctrl+click on plain %u picks terrain %u)\n", unsigned(mt.standIn), unsigned(picked));
            CHECK(picked == mt.standIn, ("Ctrl+click on plain stand-in of " + mt.name + " picks the game terrain").c_str());
        }
        {
            // Saved and opened again: the same tiles; the file's own ISOM values (past the game's shapes) only where the
            // mod terrain is (an edges-only one: not over the stand-in's own ground inside it and around).
            const fs::path saved = fs::path(argv[3]) / ("modterrain-" + std::to_string(mt.id) + ".scx");
            const IsomTiles* gameShapes = IsomTiles::forTileset(ts);
            auto savedIsom = [&](Editor& x) {  // (saved: its ISOM words)
                std::vector<uint16_t> words;
                Bytes raw2;
                if (x.save(saved, err) && readMapFile(saved, raw2, err)) {
                    const Chk sc = Chk::parse(raw2);
                    if (const Section* isom = sc.find("ISOM"))
                        for (size_t i = 0; i + 1 < isom->data.size(); i += 2) words.push_back(uint16_t(isom->data[i] | isom->data[i + 1] << 8));
                }
                return words;
            };
            int mineAll = 0, mineOpened = 0;
            for (uint16_t v : readTerrain(e.chk(), "TILE").tiles) mineAll += mine.count(v) != 0;
            for (uint16_t v : readTerrain(opened.chk(), "TILE").tiles) mineOpened += mine.count(v) != 0;
            // (the map's own mod terrains, and the ground's stroke: a mod terrain may be another one's ground)
            Editor grounded(data);
            grounded.open(argv[2], err);
            grounded.paintBrush(cx, cy, mt.ground, Brush{Brush::Diamond, 24.0f});
            // (the words that are a value past the game's shapes now and were another before the strokes)
            const std::vector<uint16_t> before = savedIsom(grounded), after = savedIsom(e);
            int own = 0;
            for (size_t i = 0; i < after.size() && i < before.size() && gameShapes; ++i)
                own += after[i] != before[i] && (after[i] >> 4) >= gameShapes->shapeCount();
            Editor back(data);
            const bool reopened = back.open(saved, err);
            int mineBack = 0;
            for (uint16_t v : readTerrain(back.chk(), "TILE").tiles) mineBack += mine.count(v) != 0;
            std::printf("      %s saved: %d ISOM values of its own more than the map's (%d of its tiles, the map's %d); opened again: %d\n",
                        mt.name.c_str(), own, mineAll, mineOpened, mineBack);
            int mineGrounded = 0;  // (its tiles after the ground's stroke: a fill of a game terrain's tiles is there before)
            for (uint16_t v : readTerrain(grounded.chk(), "TILE").tiles) mineGrounded += mine.count(v) != 0;
            CHECK(reopened && mineBack == mineAll && own > 0 && own <= 8 * std::max({1, mineAll - mineOpened, mineAll - mineGrounded}),
                  (mt.name + " is saved with its own ISOM values, near its tiles only").c_str());
            std::error_code rmEc;
            fs::remove(saved, rmEc);
        }
    }
    {
        // A mod terrain on a twin of its ground (the same inside and height, as Substructure on Jungle for Caution on
        // Substructure): painted there, the twin stays (the brush does not make it the ground the set was made on).
        auto height = [&](const Tileset::ModTerrain& m) {  // (the game terrain below it)
            uint16_t g = m.ground;
            for (int n = 0; n < 16; ++n)
                if (const Tileset::ModTerrain* o = ts.modTerrain(g)) g = o->ground;
            return g;
        };
        auto on = [&](const Tileset::ModTerrain& m, uint16_t id) {
            uint16_t g = m.ground;
            for (int n = 0; n < 16 && g; ++n) {
                if (g == id) return true;
                const Tileset::ModTerrain* o = ts.modTerrain(g);
                g = o ? o->ground : 0;
            }
            return false;
        };
        int cases = 0, broken = 0;
        for (const Tileset::ModTerrain& x : ts.modTerrains) {
            if (x.variantOf) continue;
            const Tileset::ModTerrain* g = ts.modTerrain(x.ground);
            if (!g || !g->fillKey || cases >= 4) continue;
            for (const Tileset::ModTerrain& twin : ts.modTerrains) {
                if (twin.variantOf) continue;
                if (twin.id == g->id || twin.id == x.id || twin.fillKey != g->fillKey || twin.standIn != g->standIn || height(twin) != height(*g) ||
                    (twin.groundTemplate ? twin.groundTemplate : twin.ground) != (g->groundTemplate ? g->groundTemplate : g->ground) ||
                    on(twin, g->id) || on(*g, twin.id))
                    continue;
                Editor e(data);
                if (!e.open(argv[2], err)) break;
                const int cx = e.width() * 16, cy = e.height() * 16;
                e.paintBrush(cx, cy, twin.ground, Brush{Brush::Diamond, 24.0f});
                e.paintBrush(cx, cy, twin.id, Brush{Brush::Diamond, 16.0f});
                e.paintBrush(cx, cy, x.id, Brush{Brush::Diamond, 4.0f});
                int asGround = 0, asTwin = 0;
                for (int y = cy / 32 - 12; y <= cy / 32 + 12; ++y)
                    for (int c = cx / 64 - 8; c <= cx / 64 + 8; ++c)
                        if ((c + y) & 1) asGround += e.terrainAt(c * 64, y * 32) == g->id, asTwin += e.terrainAt(c * 64, y * 32) == twin.id;
                // (and the ground painted against the twin: no third mod terrain between them, as X)
                Editor f(data);
                int others = 0;
                if (f.open(argv[2], err)) {
                    f.paintBrush(cx, cy, twin.ground, Brush{Brush::Diamond, 24.0f});
                    f.paintBrush(cx, cy, twin.id, Brush{Brush::Diamond, 12.0f});
                    f.paintBrush(cx + 12 * 32, cy, g->id, Brush{Brush::Diamond, 8.0f});
                    for (int y = cy / 32 - 12; y <= cy / 32 + 12; ++y)
                        for (int c = cx / 64 - 8; c <= cx / 64 + 16; ++c)
                            if ((c + y) & 1) {
                                const uint16_t at = f.terrainAt(c * 64, y * 32);
                                others += ts.modTerrain(at) && at != twin.id && at != g->id && !on(twin, at) && !on(*g, at);
                            }
                }
                ++cases;
                if (others) {
                    ++broken;
                    std::printf("      %s against %s: %d corners of another mod terrain between them\n", g->name.c_str(), twin.name.c_str(), others);
                }
                if (asGround || !asTwin) {
                    ++broken;
                    std::printf("      %s in %s: %d corners became %s, %d stay\n", x.name.c_str(), twin.name.c_str(), asGround, g->name.c_str(), asTwin);
                }
                break;
            }
        }
        if (cases) CHECK(broken == 0, "a mod terrain painted on a twin of its ground keeps the twin");
        // A game terrain as its ground: a mod terrain that is that terrain inside (its tiles and shapes, as High Dirt (Sand
        // Dunes) for Asphalt (High Dirt)) is a twin too.
        int gameCases = 0, gameBroken = 0;
        for (const Tileset::ModTerrain& x : ts.modTerrains) {
            if (ts.modTerrain(x.ground) || x.groundTemplate || gameCases >= 4) continue;  // (not a cliff)
            for (const Tileset::ModTerrain& twin : ts.modTerrains) {
                if (twin.variantOf) continue;
                if (twin.id == x.id || twin.standIn != x.ground || twin.fillKey != (0xFFFF0000u | x.ground) || twin.ground == x.ground || twin.groundTemplate || on(twin, x.id))
                    continue;  // (one on that terrain itself is a level higher: not a twin)
                Editor e(data);
                if (!e.open(argv[2], err)) break;
                const int cx = e.width() * 16, cy = e.height() * 16;
                e.paintBrush(cx, cy, twin.ground, Brush{Brush::Diamond, 24.0f});
                e.paintBrush(cx, cy, twin.id, Brush{Brush::Diamond, 16.0f});
                e.paintBrush(cx, cy, x.id, Brush{Brush::Diamond, 4.0f});
                int asGame = 0, asTwin = 0;
                for (int y = cy / 32 - 12; y <= cy / 32 + 12; ++y)
                    for (int c = cx / 64 - 8; c <= cx / 64 + 8; ++c)
                        if ((c + y) & 1) asGame += e.terrainAt(c * 64, y * 32) == x.ground, asTwin += e.terrainAt(c * 64, y * 32) == twin.id;
                ++gameCases;
                if (asGame || !asTwin) {
                    ++gameBroken;
                    std::printf("      %s in %s: %d corners became the game terrain, %d stay\n", x.name.c_str(), twin.name.c_str(), asGame, asTwin);
                }
                break;
            }
        }
        if (gameCases) CHECK(gameBroken == 0, "a mod terrain painted on a mod terrain that is its game ground inside keeps that terrain");
    }
    if (const Tileset::ModTerrain* asphalt = ed.era() == 3 && !ts.modTerrains.empty() ? &ts.modTerrains[0] : nullptr) {
        // Ash World's Asphalt (the mod's brush set Asphalt-1-dirt, in the terrain list): painted on Dirt, its rects are the
        // set's pieces; Dirt painted beside it keeps it Asphalt; Lava painted is Lava.
        std::set<uint16_t> mine;
        for (const auto& [g, l, r] : asphalt->pieces) mine.insert(l), mine.insert(r);
        Editor a(data);
        a.open(argv[2], err);
        const int cx = a.width() * 16, cy = a.height() * 16;
        a.paintBrush(cx, cy, 2, Brush{Brush::Diamond, 20.0f});
        a.paintBrush(cx, cy, asphalt->id, Brush{Brush::Diamond, 4.0f});
        auto count = [&](const Editor& e) {
            int n = 0;
            for (uint16_t v : readTerrain(e.chk(), "TILE").tiles) n += mine.count(v) != 0;
            return n;
        };
        // Small strokes side by side and over each other (the set's edges meeting other edges): no Lava tile is left.
        for (int i = 0; i < 24; ++i)
            a.paintBrush(cx - 700 + (i % 8) * 90 + (i / 8) * 30, cy + 500 + (i / 8) * 70, asphalt->id, Brush{Brush::Diamond, float(1 + i % 3)});
        int lavaLeft = 0;
        {
            const auto tt = tileGroupTerrains(ts);
            const TerrainLayer lt = readTerrain(a.chk(), "TILE");
            for (int y = cy / 32 + 10; y < cy / 32 + 26 && y < lt.height; ++y)
                for (int x = std::max(0, cx / 32 - 26); x < cx / 32 + 6 && x < lt.width; ++x) {
                    const uint16_t g = uint16_t(lt.tiles[size_t(y) * lt.width + x] >> 4);
                    lavaLeft += g < tt.size() && (tt[g][0] == 3 || tt[g][1] == 3);
                }
        }
        std::printf("      Lava tiles left among small Asphalt strokes: %d\n", lavaLeft);
        CHECK(lavaLeft == 0, "small Asphalt strokes leave no Lava tile (the set's missing rects take the nearest piece)");
        const int painted = count(a);
        a.paintBrush(cx + 300, cy, 2, Brush{Brush::Diamond, 3.0f});  // Dirt over its east edge
        const int afterDirt = count(a);
        a.paintBrush(cx + 900, cy, 3, Brush{Brush::Diamond, 3.0f});  // Lava away from it
        const int afterLava = count(a);
        writePng(fs::path(argv[3]) / "asphalt.png", renderTerrain(ts, readTerrain(a.chk(), "TILE")));
        std::printf("      Asphalt tiles: %d painted, %d after Dirt beside it, %d after Lava away from it\n", painted, afterDirt, afterLava);
        CHECK(asphalt->name == "Asphalt (Dirt)" && asphalt->standIn == 3, "Ash World has the mod's Asphalt, with Lava's shapes");
        CHECK(painted > 40, "the Asphalt brush puts the mod's asphalt pieces in");
        CHECK(afterDirt > painted / 2, "Dirt painted beside Asphalt keeps the rest Asphalt");
        CHECK(afterLava == afterDirt, "Lava painted away from Asphalt is Lava");
        // Saved, its ISOM has Asphalt's own values (past the game's shapes); opened again, it is Asphalt to paint beside.
        const fs::path saved = fs::path(argv[3]) / "asphalt.scx";
        CHECK(a.save(saved, err), "the map with Asphalt saves");
        Bytes raw;
        int own = 0, total = 0;
        const Chk savedChk = readMapFile(saved, raw, err) ? Chk::parse(raw) : Chk();
        if (const Section* isom = savedChk.find("ISOM"))
                for (size_t i = 0; i + 1 < isom->data.size(); i += 2) {
                    const int shape = (isom->data[i] | isom->data[i + 1] << 8) >> 4;
                    own += shape >= 125 && shape < 125 + 30, ++total;
                }
        Editor b(data);
        CHECK(b.open(saved, err) && b.canPaintTerrain(), "the map with Asphalt's own ISOM values opens, and its terrain can be painted");
        const int reopened = count(b);
        b.paintBrush(cx - 300, cy, 2, Brush{Brush::Diamond, 3.0f});  // Dirt over its west edge
        const int reopenedDirt = count(b);
        Bytes again;
        int lavaAfter = 0;
        const Chk againChk = b.save(fs::path(argv[3]) / "asphalt2.scx", err) && readMapFile(fs::path(argv[3]) / "asphalt2.scx", again, err)
                                 ? Chk::parse(again)
                                 : Chk();
        if (const Section* isom = againChk.find("ISOM"))
                for (size_t i = 0; i + 1 < isom->data.size(); i += 2) lavaAfter += ((isom->data[i] | isom->data[i + 1] << 8) >> 4) == 3;
        std::printf("      saved ISOM: %d of %d values Asphalt's own; opened again: %d Asphalt tiles, %d after Dirt beside it; Lava "
                    "values after saving again: %d\n", own, total, reopened, reopenedDirt, lavaAfter);
        CHECK(own > 40, "the saved ISOM has Asphalt's own values");
        CHECK(reopened == afterLava && reopenedDirt > reopened / 2 && reopenedDirt < reopened, "opened again, Asphalt stays Asphalt beside new Dirt");
        // Replace: Asphalt with Lava (all), and Lava with Asphalt in a box: the ISOM terrain changes (Asphalt is a terrain of
        // its own). The tiles left are the map's own Asphalt pieces, placed over other terrain.
        auto corners = [&](const Editor& x, uint16_t t) {
            int n = 0;
            for (int y = 0; y <= x.height(); ++y)
                for (int c = (y + 1) & 1; c * 2 <= x.width(); c += 2) n += x.terrainAt(c * 64, y * 32) == t;
            return n;
        };
        Editor o(data);
        const int mapOwn = o.open(argv[2], err) ? count(o) : 0;
        const Rect box{cx - 900, cy - 400, cx + 900, cy + 1100};
        const int toLava = b.replaceTerrain(asphalt->id, 3);
        const int noneLeft = count(b), cornersLeft = corners(b, asphalt->id);
        const int toAsphalt = b.replaceTerrain(3, asphalt->id, &box);  // (in a box: not all the map's lava)
        std::printf("      Replace: %d corners Asphalt to Lava (%d Asphalt corners, %d Asphalt tiles left, the map's own %d), %d corners Lava to "
                    "Asphalt (%d Asphalt tiles)\n", toLava, cornersLeft, noneLeft, mapOwn, toAsphalt, count(b));
        CHECK(toLava > 0 && cornersLeft == 0 && noneLeft <= mapOwn, "Replace makes Asphalt Lava");
        CHECK(toAsphalt > 0 && corners(b, asphalt->id) >= toAsphalt && count(b) > noneLeft + toAsphalt, "Replace makes Lava Asphalt");
        // Lava painted over Asphalt is Lava; Replace Asphalt with Dirt keeps the real Lava; Ctrl+click on Asphalt picks it.
        {
            Editor e(data);
            e.open(argv[2], err);
            const auto tt = tileGroupTerrains(ts);
            auto lavaTiles = [&](const Editor& x) {
                int n = 0;
                for (uint16_t v : readTerrain(x.chk(), "TILE").tiles) n += (v >> 4) < tt.size() && (tt[v >> 4][0] == 3 || tt[v >> 4][1] == 3) && !mine.count(v);
                return n;
            };
            e.paintBrush(cx, cy, 2, Brush{Brush::Diamond, 20.0f});
            e.paintBrush(cx, cy, asphalt->id, Brush{Brush::Diamond, 4.0f});
            int ax = -1, ay = -1;  // (an Asphalt tile's pixel)
            const TerrainLayer at = readTerrain(e.chk(), "TILE");
            for (size_t k = 0; k < at.tiles.size() && ax < 0; ++k)
                if (mine.count(at.tiles[k])) ax = int(k % at.width) * 32 + 16, ay = int(k / at.width) * 32 + 16;
            CHECK(ax >= 0 && e.paletteTerrainAt(ax, ay) == asphalt->id, "Ctrl+click on Asphalt picks Asphalt (not Lava)");
            {  // A tile placed in Rectangle mode inside Asphalt: the Asphalt brush over it gives Asphalt's piece again (GitHub #143).
                Editor g(data);
                g.open(argv[2], err);
                g.paintBrush(cx, cy, 2, Brush{Brush::Diamond, 20.0f});
                g.paintBrush(cx, cy, asphalt->id, Brush{Brush::Diamond, 4.0f});
                const int tx = cx / 32, ty = cy / 32;
                const uint16_t was = readTerrain(g.chk(), "TILE").tiles[size_t(ty) * g.width() + tx];
                g.placeTileValues({{tx, ty, 0}});
                const uint16_t placed = readTerrain(g.chk(), "TILE").tiles[size_t(ty) * g.width() + tx];
                g.paintBrush(tx * 32 + 16, ty * 32 + 16, asphalt->id, Brush{Brush::Diamond, 1.0f});
                const uint16_t after = readTerrain(g.chk(), "TILE").tiles[size_t(ty) * g.width() + tx];
                std::printf("      placed tile in Asphalt: %u -> %u, after the brush %u\n", unsigned(was), unsigned(placed), unsigned(after));
                CHECK(mine.count(was) && placed == 0 && mine.count(after), "the Asphalt brush over a placed tile in Asphalt gives Asphalt's piece");
            }
            const int asphaltBefore = count(e), lavaBefore = lavaTiles(e);
            e.paintBrush(cx, cy, 3, Brush{Brush::Diamond, 2.0f});  // Lava over its middle
            std::printf("      Lava over Asphalt: Asphalt tiles %d -> %d, Lava tiles %d -> %d\n", asphaltBefore, count(e), lavaBefore, lavaTiles(e));
            CHECK(count(e) < asphaltBefore && lavaTiles(e) > lavaBefore, "Lava painted over Asphalt is Lava");
            // Replace Asphalt with Dirt (all the map): the painted Asphalt goes, the map's own (placed on Dirt) stays, and the
            // map's real Lava stays (a fresh copy: Asphalt away from any Lava).
            Editor f(data);
            f.open(argv[2], err);
            const int own = count(f), lavaOwn = lavaTiles(f);
            f.paintBrush(cx, cy, 2, Brush{Brush::Diamond, 20.0f});
            const int lavaDirt = lavaTiles(f), ownLeft = count(f);  // (the map's own Asphalt after the Dirt stroke)
            f.paintBrush(cx, cy, asphalt->id, Brush{Brush::Diamond, 4.0f});
            const int painted2 = count(f);
            const int replaced = f.replaceTerrain(asphalt->id, 2);
            std::printf("      Replace Asphalt with Dirt: %d corners; Asphalt tiles %d (the map's own %d, painted %d), Lava tiles %d -> %d (the map's %d)\n",
                        replaced, count(f), own, painted2 - own, lavaDirt, lavaTiles(f), lavaOwn);
            CHECK(replaced > 0 && count(f) == ownLeft && lavaTiles(f) == lavaDirt, "Replace Asphalt with Dirt takes the painted Asphalt and keeps the real Lava");
        }
    }

    {
        // The mod's height advantage (Cosmonarchy-GPTP get_cliff_advantage): a tile of high ground is higher than one of
        // low ground; a ground unit's ground weapon reaches 32 px further per level, an air unit's does not.
        Editor h(data);
        h.open(argv[2], err);
        const int cx = h.width() * 16, cy = h.height() * 16;
        const IsomTerrainTable* tt = IsomTerrainTable::forEra(h.era());
        auto named = [&](const char* n) {
            for (uint16_t i = 1; tt && i <= tt->terrainCount(); ++i)
                if (tt->isTerrain(i) && (std::string(tt->terrainName(i)) == n || std::string(tt->terrainName(i)).rfind(std::string(n) + " (", 0) == 0))
                    return i;  // (in a mod's table a game terrain is "X (its ground)")
            return uint16_t(0);
        };
        const uint16_t dirt = named("Dirt"), highDirt = named(h.era() == 6 ? "High Snow" : h.era() == 1 ? "High Platform" : h.era() == 2 ? "Floor" : "High Dirt");
        h.paintBrush(cx, cy, h.era() == 6 ? named("Snow") : h.era() == 1 ? named("Platform") : h.era() == 2 ? named("Substructure") : dirt, Brush{Brush::Diamond, 12.0f});  // (the low ground)
        const uint16_t low = readTerrain(h.chk(), "MTXM").tiles[size_t(cy / 32) * h.width() + cx / 32];
        h.paintBrush(cx, cy, highDirt, Brush{Brush::Diamond, 6.0f});  // (the high ground)
        const uint16_t high = readTerrain(h.chk(), "MTXM").tiles[size_t(cy / 32) * h.width() + cx / 32];
        const int hl = cmbw::tileHeight(h.tileset(), low), hh = cmbw::tileHeight(h.tileset(), high);
        int ground = -1, flyer = -1;  // (a mod unit past the game's 228 with a ranged ground weapon, and an air unit)
        for (uint16_t u = 229; u < 3000 && (ground < 0 || flyer < 0); ++u) {
            cmbw::UnitRanges r;
            if (!cmbw::unitRanges(data, u, r)) continue;
            if (ground < 0 && r.cliff && r.ground > 64) ground = u;
            if (flyer < 0 && r.flyer && r.ground > 64) flyer = u;
        }
        cmbw::UnitRanges g, f;
        const bool got = ground >= 0 && flyer >= 0 && cmbw::unitRanges(data, uint16_t(ground), g) && cmbw::unitRanges(data, uint16_t(flyer), f);
        std::printf("      heights: low ground %d, high ground %d; unit %d: %d px -> %d px on high ground; air unit %d: %d px -> %d px\n", hl, hh, ground,
                    g.ground, cmbw::cliffRange(g.ground, hh, g.cliff), flyer, f.ground, cmbw::cliffRange(f.ground, hh, f.cliff));
        CHECK(hh > hl, "high ground's tiles are higher than low ground's (the mod's tile height)");
        CHECK(got && cmbw::cliffRange(g.ground, hh, g.cliff) == g.ground + 32 * hh && hh > 0,
              "a ground unit past the game's units reaches 32 px further per level of its ground");
        CHECK(got && cmbw::cliffRange(f.ground, hh, f.cliff) == f.ground, "an air unit gets no height advantage");
    }

    // An Askosi player (race 69: the mod's lobby and AI read it) is saved and opened again.
    Editor::Player p = ed.player(0);
    p.race = Editor::Race(69);
    ed.setPlayer(0, p);
    const fs::path saved = fs::path(argv[3]) / "askosi.scx";
    const bool savedOk = ed.save(saved, err);
    if (!savedOk) std::printf("      (%s)\n", err.c_str());
    CHECK(savedOk, "the map with an Askosi player saves");
    Editor reopened(data);
    CHECK(reopened.open(saved, err) && int(reopened.player(0).race) == 69, "player 1 is Askosi after the map opens again");
    CHECK(readTerrain(reopened.chk(), "TILE").tiles == readTerrain(ed.chk(), "TILE").tiles, "the brush's mod tiles are saved");

    // CRIG word lists: the words the mod takes, and a line with them reads and writes the same.
    const auto& races = cmbw::crigWordChoices("set_race", "race");
    CHECK(std::find(races.begin(), races.end(), "askosi") != races.end(), "set_race's race words have askosi");
    CHECK(!cmbw::crigWordChoices("set_faction", "faction").empty() && !cmbw::crigWordChoices("spawn", "type").empty(),
          "set_faction's factions and spawn's types are lists");
    CHECK(cmbw::crigWordChoices("kill", "area").empty(), "an argument that is not a word has no word list");
    const cmbw::CrigLine line = cmbw::parseCrigLine("set_race 1 askosi");
    CHECK(line.command && line.command->names.front() == "set_race" && line.values.size() == 2 && line.values[1] == "askosi",
          "set_race 1 askosi reads as set_race, player 1, askosi");
    CHECK(cmbw::crigLineText(line) == "set_race 1 askosi", "set_race 1 askosi writes the same");

    {
        // CRIGs as condition and action rows: the map's triggers give the same TRIG back; a new command condition in a
        // trigger with no commands makes a Comment of its own at the top, which reads back as that condition's row.
        Bytes raw;
        const Chk chk = readMapFile(argv[2], raw, err) ? Chk::parse(raw) : Chk();
        const ClassicTriggers t = readClassicTriggers(chk);
        const StringTable strings = StringTable::read(chk);
        const ClassicTriggers rows = cmbw::crigCollapse(t, strings);
        StringTable after = strings;
        int n = 0;
        for (const ClassicTrigger& tr : rows.triggers)
            for (int i = 0; i < tr.actionCount(); ++i) n += tr.actions[size_t(i)].type == kClassicCrigType;
        std::printf("      %d command rows in the map's triggers\n", n);
        CHECK(encodeClassicTriggers(cmbw::crigExpand(rows, after)) == encodeClassicTriggers(t) && after.size() == strings.size(),
              "the map's triggers as command rows give the same TRIG back");
        ClassicTriggers one;
        one.triggers.push_back(ClassicTrigger::make());  // (Always)
        ClassicCondition c;
        c.type = kClassicCrigType;
        auto line2 = std::make_shared<ClassicCrig>();
        line2->line = "condition var ready == true";
        c.crig.p = line2;
        one.triggers[0].addCondition(c);
        StringTable s2;
        const ClassicTriggers written = cmbw::crigExpand(one, s2);
        const ClassicAction& first = written.triggers[0].actions[0];
        CHECK(first.type == 47 && s2.get(first.text) == "condition var ready == true" && written.triggers[0].conditionCount() == 1,
              "a new command condition is a Comment of its own at the top");
        const ClassicTriggers back = cmbw::crigCollapse(written, s2);
        CHECK(back.triggers[0].conditionCount() == 2 && back.triggers[0].conditions[1].type == kClassicCrigType &&
                  back.triggers[0].conditions[1].crig->line == "condition var ready == true" && back.triggers[0].actionCount() == 0,
              "that Comment reads back as the condition's row");
        // A command row turned off: a turned-off Comment of its own (a Comment's line cannot be off alone).
        ClassicTriggers two;
        two.triggers.push_back(ClassicTrigger::make());
        for (const char* l : {"set_race 1 askosi", "set_race 2 zerg"}) {
            ClassicAction a;
            a.type = kClassicCrigType;
            auto r = std::make_shared<ClassicCrig>();
            r->line = l;
            a.crig.p = r;
            two.triggers[0].addAction(a);
        }
        two.triggers[0].actions[1].flags = 2;
        StringTable s3;
        const ClassicTriggers off = cmbw::crigExpand(two, s3);
        CHECK(off.triggers[0].actionCount() == 2 && s3.get(off.triggers[0].actions[0].text) == "set_race 1 askosi" &&
                  !(off.triggers[0].actions[0].flags & 2) && s3.get(off.triggers[0].actions[1].text) == "set_race 2 zerg" &&
                  (off.triggers[0].actions[1].flags & 2),
              "a command row turned off is a turned-off Comment of its own");
        // Rows moved: the Comment's lines in the rows' order. Rows of another depth: a Comment of their own.
        auto crigAction = [](const char* l, int group, int index, uint8_t depth) {
            ClassicAction a;
            a.type = kClassicCrigType, a.depth = depth;
            auto r = std::make_shared<ClassicCrig>();
            r->line = l, r->group = group, r->index = index;
            a.crig.p = r;
            return a;
        };
        ClassicTriggers moved;
        moved.triggers.push_back(ClassicTrigger::make());
        moved.triggers[0].addAction(crigAction("set_race 3 protoss", 0, 2, 0));  // (C, moved up)
        moved.triggers[0].addAction(crigAction("set_race 1 askosi", 0, 0, 0));
        moved.triggers[0].addAction(crigAction("set_race 2 zerg", 0, 1, 0));
        StringTable s4;
        const ClassicTriggers mv = cmbw::crigExpand(moved, s4);
        CHECK(mv.triggers[0].actionCount() == 1 && s4.get(mv.triggers[0].actions[0].text) == "set_race 3 protoss\r\nset_race 1 askosi\r\nset_race 2 zerg",
              "moved command rows are written in their new order");
        ClassicTriggers deep = moved;
        deep.triggers[0].actions[2].depth = 1;
        StringTable s5;
        const ClassicTriggers dp = cmbw::crigExpand(deep, s5);
        CHECK(dp.triggers[0].actionCount() == 2 && dp.triggers[0].actions[1].depth == 1, "command rows of another depth are a Comment of their own");
        // 64 actions and a new command condition: a new Comment would make 65 (the page does not set the triggers).
        ClassicTriggers full;
        full.triggers.push_back(ClassicTrigger::make());
        for (int i = 0; i < 64; ++i) full.triggers[0].addAction(makeClassicAction(4));  // (Wait)
        ClassicCondition cc;
        cc.type = kClassicCrigType;
        auto cl = std::make_shared<ClassicCrig>();
        cl->line = "condition var ready == true";
        cc.crig.p = cl;
        full.triggers[0].addCondition(cc);
        CHECK(!cmbw::crigFits(full, StringTable{}) && cmbw::crigFits(moved, StringTable{}), "a trigger that would need 65 actions does not fit");
    }

    {
        // The Town Center Marker (UnitType::blocksNothing) goes on top of a building; another building does not.
        Editor pe(data);
        CHECK(pe.open(argv[2], err), "the Cosmonarchy map opens again");
        int marker = -1;
        for (size_t i = 0; i < pe.unitTypes().size(); ++i)
            if (pe.unitTypes()[uint16_t(i)].blocksNothing && pe.unitTypes()[uint16_t(i)].name.find("Town Center") != std::string::npos) marker = int(i);
        const PlaceResult b = pe.placeUnit(106, 0, pe.width() * 16, pe.height() * 16);  // (a Terran Ministry, moved to where it fits)
        const PlaceResult m = marker >= 0 && b.placed ? pe.placeUnit(uint16_t(marker), 0, b.unit.x, b.unit.y, false) : PlaceResult{};
        const PlaceResult o = b.placed ? pe.placeUnit(106, 0, b.unit.x, b.unit.y, false) : PlaceResult{};
        CHECK(marker >= 0 && b.placed && m.placed && !o.placed, "a Town Center Marker goes on top of a building (a building does not)");
        // (the palette: a unit whose name ends as a standard unit's is not that unit: Crashed Minotaur, Volatile Egg)
        auto section = [&](const char* name) {
            const auto id = pe.unitTypes().findByName(name);
            return id ? pe.unitTypes()[*id].paletteSection : std::string();
        };
        CHECK(section("Terran Minotaur") == "Terran \xC2\xB7 Air units" && section("Crashed Minotaur") == "Terran \xC2\xB7 Special units" &&
                  section("Volatile Egg") == "Zerg \xC2\xB7 Special units",
              "the unit palette: Crashed Minotaur and Volatile Egg are special units, not the Minotaur and the Egg");
        CHECK(section("Zerg Begrothilisk") == "Zerg \xC2\xB7 Unimplemented units" && section("Zerg Rakzilisk") == "Zerg \xC2\xB7 Ground units",
              "the unit palette: a unit the website marks unimplemented (Begrothilisk) is in its race's Unimplemented units");
    }

    {
        // The mod's player colors: 111 by number (its tunit.pcx rows, named as Cosmonarchy-GPTP names them); a color past
        // the game's 12 goes into COLR (the 1.16.1 game reads COLR only), and the player's units take that row's ramp.
        Editor pe(data);
        CHECK(pe.open(argv[2], err), "the Cosmonarchy map opens for its colors");
        std::vector<Editor::Player> players;
        std::vector<Editor::Force> forces;
        for (int p = 0; p < 8; ++p) players.push_back(pe.player(p));
        for (int f = 0; f < 4; ++f) forces.push_back(pe.force(f));
        players[0].color = {Editor::PlayerColor::Id, 30, 0};  // (Pumpkin)
        pe.setPlayersAndForces(players, forces);
        const Section* colr = pe.chk().find("COLR");
        UnitSprites& us = pe.unitSprites();
        std::printf("      colors: %d, 30 is %s (#%06X), COLR[0] %d\n", pe.colorCount(), pe.colorName(30).c_str(), unsigned(pe.colorRgb(30)),
                    colr && !colr->data.empty() ? colr->data[0] : -1);
        CHECK(pe.colorCount() == 111 && pe.colorName(30) == "Pumpkin" && colr && colr->data[0] == 30, "a mod color by number goes into COLR");
        CHECK(us.colorRowCount() >= 111 && us.remap(8, 0) == us.remapRow(8, 30) && us.remap(8, 1) == us.remapRow(8, pe.player(1).color.id),
              "a player's units take its color's row of the mod's tunit.pcx");
    }

    {
        // The establishing shot as the mod's example file has it (scenario setup page, pastebin k14M3iDt): each page's
        // display time, picture and font before its place; read and written back the same.
        const std::string example =
            "</FADESPEED 100>\r\n\r\n</COMMENT ####>\r\n</DISPLAYTIME 6500>\r\n</BACKGROUND est\\blank.pcx>\r\n</FONTCOLOR est\\tfont.pcx>\r\n"
            "</SCREENCENTER>\r\nNever truly awake.\r\n</PAGE>\r\n\r\n</COMMENT image credit>\r\n</DISPLAYTIME 6500>\r\n</BACKGROUND est\\m4.pcx>\r\n"
            "</FONTCOLOR est\\tfont.pcx>\r\n</SCREENLOWERLEFT>\r\nLONGEST NIGHT\r\n\r\nHecaton command post, Concatenoi IV\r\nPhase two of Operation Stardance\r\n</PAGE>";
        const auto shot = cmbw::parseEstablishingShot(example);
        CHECK(shot.fadeMs == 100 && shot.pages.size() == 2 && shot.pages[0].background == "est\\blank.pcx" && shot.pages[1].background == "est\\m4.pcx" &&
                  shot.pages[1].displayMs == 6500 && shot.pages[1].font == "est\\tfont.pcx" && shot.pages[1].place == "SCREENLOWERLEFT" &&
                  shot.pages[1].lines.size() == 4 && shot.pages[1].lines[0] == "LONGEST NIGHT" && shot.pages[1].comments.size() == 1,
              "the mod's example establishing shot: two pages, each with its own picture, font and display time");
        const std::string again = cmbw::establishingShotText(shot);
        CHECK(cmbw::establishingShotText(cmbw::parseEstablishingShot(again)) == again && again.find("DISPLAYTIME 6500>\r\n</BACKGROUND est\\m4.pcx") != std::string::npos,
              "an establishing shot is written back as it was read");
    }
    {  // A location's new name goes into the CRIG lines that use it (one undo step), not into other words or plain text
        CHECK(cmbw::crigRenameLocation("move_unit \"Old Zone\" * 13 men 10 true\r\nkill \"old zone\" 1 men 5 false\r\nThe Old Zone is safe", "Old Zone", "North Gate") ==
                  "move_unit \"North Gate\" * 13 men 10 true\r\nkill \"North Gate\" 1 men 5 false\r\nThe Old Zone is safe",
              "a renamed location: its CRIG lines (any case, quoted) take the new name; a note keeps the old one");
        CHECK(cmbw::crigRenameLocation("  condition local_camera   Base  visible\nexit_if camera Base\nkill Base 1 men 5 false", "Base", "Gate") ==
                  "  condition local_camera   Gate  visible\nexit_if camera Gate\nkill Gate 1 men 5 false",
              "a renamed location: only its words change (an alias, the spaces kept), also in the condition after exit_if");
        CHECK(cmbw::crigRenameLocation("kill Base 1 men 5 false", "Base", "Gate \"A\"") == "kill Base 1 men 5 false" &&
                  cmbw::crigRenameLocation("kill Base 1 men 5 false", "Base", "North\tGate") == "kill \"North\tGate\" 1 men 5 false",
              "a renamed location: a new name with a quote leaves the lines; one with a tab is quoted");
        Editor ed(data);
        std::string e;
        if (ed.open(argv[2], e)) {
            const int loc = ed.createLocation({64, 64, 192, 192}, false, 3, "Old Zone");
            StringTable strings = ed.strings();
            ClassicTrigger t;
            ClassicAction c;
            c.type = 47, c.text = uint32_t(strings.add("move_unit \"Old Zone\" * 13 men 10 true"));
            t.actions[0] = c;
            ed.addClassicTrigger(t, -1, &strings);
            auto comment = [&] { return ed.strings().get(ed.classicTriggers().triggers.back().actions[0].text); };
            ed.renameLocation(loc, "North Gate");
            const bool renamed = comment() == "move_unit \"North Gate\" * 13 men 10 true" && ed.locationName(loc) == "North Gate";
            ed.undo();
            CHECK(renamed && comment() == "move_unit \"Old Zone\" * 13 men 10 true" && ed.locationName(loc) == "Old Zone",
                  "Rename location changes the CRIG line that uses the location, and one undo takes both back");
            const int twin = ed.createLocation({256, 256, 320, 320}, false, 3, "Old Zone");
            ed.renameLocation(loc, "North Gate");
            CHECK(comment() == "move_unit \"Old Zone\" * 13 men 10 true" && twin >= 0,
                  "Rename location leaves the CRIG lines when another location has the old name (either could be the one)");
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
