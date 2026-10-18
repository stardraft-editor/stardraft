// The game's pathing regions and the pathing check (stardraft/pathing_regions.h).
//
// usage: pathing_regions_test [<install dir> <maps dir> [more maps dirs...] [--dump <dir>]]
// PR_DEBUG (an environment variable): each problem's line.
// --dump: a picture of each map (16 px per tile) with the regions (each its own tint, unwalkable ground dark) and the
// problems (boxes: pockets red, gaps magenta, small units only orange, closed passages yellow, small regions cyan).
// With no arguments: the checks on made-up terrain only (no game data; CI runs this). With the game: also each map in
// the folders (the counts of regions and connected areas, the problems found, the time), and on real maps:
//   - every tile has a region; every walkable minitile but a few is in a walkable region; fewer than 2500 regions;
//   - the start locations of (2)Bottleneck are in one connected area, those of (5)Island Hop each in its own;
//   - the counts of regions of (2)Bottleneck, (4)Lost Civilization and (5)Island Hop are the counts this code gave
//     when it was written (no reference of the game's own counts is known: a change shows here);
//   - a nook made in (2)Bottleneck (one walkable minitile in an unwalkable tile) is a Pocket with a fix, and the fixed
//     terrain has no such Pocket there;
//   - a map of 256 x 256 or less takes less than 200 ms.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/pathing_regions.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace fs = std::filesystem;
using namespace stardraft;

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

// A made-up tileset: megatile 0 null, 1 unwalkable, 2 walkable (low), 3 one walkable minitile at (1, 1), 4 walkable
// high ground, 5 walkable with the minitile at (1, 1) unwalkable. Group 0: megatile 0; group 1 (type 1): 2 and 5;
// group 2 (type 2): 1 and 3 (the pocket's tile and its fix); group 3 (type 3): 4.
Tileset madeTileset() {
    Tileset ts;
    ts.megatiles.resize(6);
    ts.minitileFlags.resize(6);
    auto fill = [&](int m, uint16_t f) { ts.minitileFlags[size_t(m)].fill(f); };
    fill(0, 0), fill(1, 0), fill(2, 1), fill(3, 0), fill(4, 1 | 4), fill(5, 1);
    ts.minitileFlags[3][5] = 1;  // (1, 1)
    ts.minitileFlags[5][5] = 0;
    ts.groups.resize(4);
    for (auto& g : ts.groups) g.megatiles.fill(0);
    ts.groups[1].type = 1, ts.groups[1].megatiles[0] = 2, ts.groups[1].megatiles[1] = 5;
    ts.groups[2].type = 2, ts.groups[2].megatiles[0] = 1, ts.groups[2].megatiles[1] = 3;
    ts.groups[3].type = 3, ts.groups[3].megatiles[0] = 4;
    return ts;
}
constexpr uint16_t kWalk = 1 << 4, kWall = 2 << 4, kPocket = (2 << 4) | 1, kHigh = 3 << 4;

TerrainLayer madeTerrain(int w, int h, uint16_t fill) {
    TerrainLayer t;
    t.width = uint16_t(w), t.height = uint16_t(h);
    t.tiles.assign(size_t(w) * h, fill);
    return t;
}

Unit startAt(int tx, int ty) {
    Unit u;
    u.type = 214, u.x = uint16_t(tx * 32 + 16), u.y = uint16_t(ty * 32 + 16);
    return u;
}

int count(const PathingCheck& c, PathingProblemKind k) {
    return int(std::count_if(c.problems.begin(), c.problems.end(), [&](const PathingProblem& p) { return p.kind == k; }));
}

void madeChecks() {
    std::printf("made-up terrain\n");
    const Tileset ts = madeTileset();
    {  // All walkable: one connected walkable area, no problem.
        const TerrainLayer t = madeTerrain(32, 32, kWalk);
        const PathingRegions r = computePathingRegions(t, ts);
        std::set<int> groups;
        int walkTiles = 0;
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 32; ++x) {
                const int ri = r.regionAt(x * 4, y * 4);
                if (ri >= 0 && r.regions[size_t(ri)].walkable()) groups.insert(r.regions[size_t(ri)].group), ++walkTiles;
            }
        std::printf("    %zu regions, %d groups\n", r.regions.size(), r.groups);
        check(walkTiles == 32 * 30 && groups.size() == 1, "open ground: one connected area");
        check(!r.regions.empty() && !r.regions[0].walkable() && r.regions[0].y0 == 31, "the console's region is the last row");
        bool sizes = true;
        for (size_t i = 1; i < r.regions.size(); ++i) {
            const PathingRegion& g = r.regions[i];
            sizes &= g.tiles > 0 && g.x1 - g.x0 <= 16 && g.y1 - g.y0 <= 16;
        }
        check(sizes && r.regions.size() > 10 && r.regions.size() < 60, "open ground: regions of a few tiles each");
        std::vector<Unit> units{startAt(5, 5), startAt(25, 20)};
        const PathingCheck c = checkPathing(r, t, ts, units);
        check(c.problems.empty(), "open ground: no problem");
    }
    {  // A wall: two areas, each with a start location; a high ground plateau: its own regions in the same area.
        TerrainLayer t = madeTerrain(32, 32, kWalk);
        for (int y = 0; y < 32; ++y) t.tiles[size_t(y) * 32 + 16] = kWall;
        for (int y = 4; y < 10; ++y)
            for (int x = 4; x < 10; ++x) t.tiles[size_t(y) * 32 + x] = kHigh;
        const PathingRegions r = computePathingRegions(t, ts);
        const int left = r.regions[size_t(r.regionAt(8, 8))].group, right = r.regions[size_t(r.regionAt(100, 8))].group;
        check(left != right, "a wall: two connected areas");
        const int high = r.regionAt(6 * 4, 6 * 4);
        check(r.regions[size_t(high)].elevation() == 2, "the plateau's region is high ground");
        std::vector<Unit> units{startAt(2, 2), startAt(28, 20)};
        const PathingCheck c = checkPathing(r, t, ts, units);
        check(count(c, PathingProblemKind::Pocket) == 0, "a wall with a start location on each side: no pocket");
        std::vector<Unit> one{startAt(2, 2)};
        const PathingCheck c1 = checkPathing(r, t, ts, one);
        check(count(c1, PathingProblemKind::Pocket) == 0, "a large area with no start location is not a pocket (an island)");
    }
    {  // A nook: a tile with one walkable minitile in a wall.
        TerrainLayer t = madeTerrain(32, 32, kWalk);
        for (int y = 10; y < 14; ++y)
            for (int x = 10; x < 14; ++x) t.tiles[size_t(y) * 32 + x] = kWall;
        t.tiles[size_t(11) * 32 + 11] = kPocket;
        const PathingRegions r = computePathingRegions(t, ts);
        std::vector<Unit> units{startAt(2, 2)};
        const PathingCheck c = checkPathing(r, t, ts, units);
        const auto it = std::find_if(c.problems.begin(), c.problems.end(), [](const PathingProblem& p) { return p.kind == PathingProblemKind::Pocket; });
        check(it != c.problems.end() && it->minitiles == 1 && !it->fitsUnit && it->x == 11 * 32 + 12 && it->y == 11 * 32 + 12,
              "a one-minitile pocket: found at its place");
        check(it != c.problems.end() && it->fixTileX == 11 && it->fixTileY == 11 && it->fixValue == kWall, "the pocket's fix: the wall tile of its group");
        TerrainLayer fixed = t;
        if (it != c.problems.end() && it->fixTileX >= 0) fixed.tiles[size_t(it->fixTileY) * 32 + it->fixTileX] = it->fixValue;
        const PathingCheck after = checkPathing(computePathingRegions(fixed, ts), fixed, ts, units);
        check(count(after, PathingProblemKind::Pocket) == 0, "after the fix: no pocket");
    }
    {  // A hole of one minitile in open ground (tile 5): no problem (units walk around it).
        TerrainLayer t = madeTerrain(32, 32, kWalk);
        t.tiles[size_t(8) * 32 + 8] = (1 << 4) | 1;
        const PathingRegions r = computePathingRegions(t, ts);
        const PathingCheck c = checkPathing(r, t, ts, std::vector<Unit>{startAt(2, 2)});
        check(c.problems.empty(), "a one-minitile hole in open ground: no problem");
    }
    {  // A passage 2 minitiles wide through a wall: small units only.
        TerrainLayer t = madeTerrain(32, 32, kWalk);
        for (int y = 0; y < 32; ++y) t.tiles[size_t(y) * 32 + 16] = kWall;
        Tileset ts2 = ts;  // megatile 6: walkable rows 1 and 2 only
        ts2.megatiles.resize(7), ts2.minitileFlags.resize(7);
        ts2.minitileFlags[6].fill(0);
        for (int k = 4; k < 12; ++k) ts2.minitileFlags[6][size_t(k)] = 1;
        ts2.groups[2].megatiles[2] = 6;
        t.tiles[size_t(12) * 32 + 16] = (2 << 4) | 2;
        const PathingRegions r = computePathingRegions(t, ts2);
        const PathingCheck c = checkPathing(r, t, ts2, std::vector<Unit>{startAt(2, 2), startAt(28, 20)});
        const auto it = std::find_if(c.problems.begin(), c.problems.end(), [](const PathingProblem& p) { return p.kind == PathingProblemKind::SmallUnitsOnly; });
        check(it != c.problems.end() && it->x / 32 >= 15 && it->x / 32 <= 17 && it->y / 32 == 12, "a passage 2 minitiles wide: small units only, at its place");
        // The game's regions do not join the two sides there (the passage's tile is unwalkable): a closed passage.
        check(count(c, PathingProblemKind::ClosedPassage) >= 1, "the passage's tile is unwalkable for the game: a closed passage");
    }
}

void dumpPicture(const fs::path& file, const Tileset& ts, const TerrainLayer& terrain, const PathingRegions& r, const PathingCheck& c) {
    Image img = renderTerrain(ts, terrain, 2);  // 16 px per tile: 4 px per minitile
    auto blend = [&](int x, int y, uint32_t rgb, int alpha) {
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
        uint8_t* p = &img.rgba[(size_t(y) * img.width + x) * 4];
        for (int k = 0; k < 3; ++k) p[k] = uint8_t((p[k] * (255 - alpha) + ((rgb >> (16 - 8 * k)) & 255) * alpha) / 255);
    };
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const int mx = x / 4, my = y / 4, ri = r.regionAt(mx, my);
            if (!r.walkable(mx, my)) blend(x, y, 0, 150);
            else if (ri >= 0) blend(x, y, 0x404040 + uint32_t((ri * 2654435761u) & 0x7F7F7F), 90);
            // The borders of the regions.
            if ((x % 4 == 0 && r.regionAt(mx - 1, my) != ri) || (y % 4 == 0 && r.regionAt(mx, my - 1) != ri)) blend(x, y, 0xFFFFFF, 120);
        }
    for (const PathingProblem& p : c.problems) {
        const uint32_t color = p.kind == PathingProblemKind::Pocket ? 0xFF2020 : p.kind == PathingProblemKind::Gap ? 0xFF20FF
                             : p.kind == PathingProblemKind::SmallUnitsOnly ? 0xFF9020 : p.kind == PathingProblemKind::ClosedPassage ? 0xFFFF20 : 0x20FFFF;
        const int x0 = p.x0 * 4 - 3, y0 = p.y0 * 4 - 3, x1 = p.x1 * 4 + 2, y1 = p.y1 * 4 + 2;
        for (int x = x0; x <= x1; ++x) blend(x, y0, color, 255), blend(x, y1, color, 255), blend(x, y0 + 1, color, 255), blend(x, y1 - 1, color, 255);
        for (int y = y0; y <= y1; ++y) blend(x0, y, color, 255), blend(x1, y, color, 255), blend(x0 + 1, y, color, 255), blend(x1 - 1, y, color, 255);
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    writePng(file.string().c_str(), img);
}

int main(int argc, char** argv) {
    madeChecks();
    if (argc < 3) {
        std::printf("%s\n", failures ? "FAILED" : "all passed (made-up terrain only: no game data given)");
        return failures ? 1 : 0;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::string dump;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--dump") dump = argv[i + 1], argc = i;
    Tileset tilesets[8];
    bool loaded[8] = {};
    // The counts of regions (and connected areas) when this test was written.
    const std::map<std::string, std::pair<size_t, int>> known = {
        {"(2)Bottleneck.scm", {506, 69}}, {"(4)Lost Civilization.scm", {512, 74}}, {"(5)Island Hop.scm", {344, 51}}};
    int maps = 0, seen = 0;
    double worst = 0;
    for (int d = 2; d < argc; ++d) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(argv[d], ec)) files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& path : files) {
            std::string ext = path.extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".scm" && ext != ".scx") continue;
            Bytes raw;
            if (!readMapChk(path, raw, err)) continue;
            const Chk chk = Chk::parse(raw);
            const uint16_t era = readEra(chk);
            if (!loaded[era] && !tilesets[era].load(data, era, err)) continue;
            loaded[era] = true;
            const Tileset& ts = tilesets[era];
            const TerrainLayer terrain = readTerrain(chk);
            const std::vector<Unit> units = readUnits(chk);
            const PathingRegions r = computePathingRegions(terrain, ts);
            const PathingCheck c = checkPathing(r, terrain, ts, units);
            ++maps;
            const std::string name = path.filename().string();
            worst = std::max(worst, r.ms);
            std::printf("%-40s %3dx%-3d %4zu regions (%4d first) %3d areas, %.1f ms; pockets %d, gaps %d, small units only %d, closed %d, small regions %d\n",
                        name.c_str(), terrain.width, terrain.height, r.regions.size(), r.firstRegions, r.groups, r.ms, c.pockets, c.gaps, c.narrow, c.closed,
                        c.smallRegions);
            if (std::getenv("PR_DEBUG"))
                for (const PathingProblem& p : c.problems)
                    std::printf("    %s at %d, %d (tile %d, %d): %d minitiles, region %d group %d%s\n", pathingProblemName(p.kind), p.x, p.y, p.x / 32, p.y / 32, p.minitiles,
                                r.regionAt(p.x / 8, p.y / 8), r.regions[size_t(r.regionAt(p.x / 8, p.y / 8))].group, p.fixTileX >= 0 ? ", with a fix" : "");
            if (!dump.empty()) dumpPicture(fs::path(dump) / (path.stem().string() + ".png"), ts, terrain, r, c);
            // Every tile has a region; walkable minitiles are in walkable regions (but for the split tiles' fallback).
            bool allTiles = true;
            for (int32_t t : r.tile) allTiles &= t >= 0 ? size_t(t) < r.regions.size() : size_t(-1 - t) < r.splits.size();
            int walk = 0, inWalkable = 0;
            for (int my = 0; my < r.walkH(); ++my)
                for (int mx = 0; mx < r.walkW(); ++mx)
                    if (r.walkable(mx, my)) ++walk, inWalkable += r.regions[size_t(r.regionAt(mx, my))].walkable();
            if (!allTiles || r.regions.size() >= 2500 || r.tooMany || inWalkable < walk * 0.97)
                check(false, name + ": every tile has a region, walkable ground is in walkable regions, fewer than 2500 regions (" +
                                 std::to_string(inWalkable) + " of " + std::to_string(walk) + ")");
            if (terrain.width <= 256 && terrain.height <= 256 && r.ms >= 200) check(false, name + ": less than 200 ms");
            auto startGroups = [&]() {
                std::vector<int> g;
                for (const Unit& u : units)
                    if (u.type == 214) g.push_back(r.regions[size_t(r.regionAt(u.x / 8, u.y / 8))].group);
                return g;
            };
            if (name == "(2)Bottleneck.scm") {
                ++seen;
                const auto g = startGroups();
                check(g.size() == 2 && g[0] == g[1], "Bottleneck: the start locations are in one connected area");
                // A nook: an unwalkable tile far from the start locations gets a walkable minitile.
                int tx = -1, ty = -1;
                for (int y = 4; y < terrain.height - 4 && tx < 0; ++y)
                    for (int x = 4; x < terrain.width - 4 && tx < 0; ++x) {
                        bool wall = true;  // a 3 x 3 of unwalkable tiles
                        for (int k = 0; k < 9; ++k)
                            for (int m = 0; m < 16; ++m) wall &= !r.walkable((x - 1 + k % 3) * 4 + m % 4, (y - 1 + k / 3) * 4 + m / 4);
                        if (wall) tx = x, ty = y;
                    }
                check(tx >= 0, "Bottleneck: an unwalkable place for the nook");
                if (tx >= 0) {
                    // A tile of the tileset with exactly one walkable minitile.
                    int value = -1;
                    for (size_t g2 = 0; g2 < ts.groups.size() && value < 0; ++g2)
                        for (int s = 0; s < 16 && value < 0; ++s) {
                            const int m = ts.groups[g2].megatiles[size_t(s)];
                            if (m <= 0 || size_t(m) >= ts.minitileFlags.size()) continue;
                            int n = 0;
                            for (uint16_t f : ts.minitileFlags[size_t(m)]) n += f & 1;
                            if (n == 1) value = int((g2 << 4) | size_t(s));
                        }
                    std::printf("    the nook at tile %d, %d: tile value 0x%04X\n", tx, ty, value);
                    check(value >= 0, "Bottleneck: the tileset has a tile with one walkable minitile");
                    if (value >= 0) {
                        TerrainLayer nook = terrain;
                        nook.tiles[size_t(ty) * terrain.width + tx] = uint16_t(value);
                        const PathingRegions nr = computePathingRegions(nook, ts);
                        const PathingCheck nc = checkPathing(nr, nook, ts, units);
                        const auto it = std::find_if(nc.problems.begin(), nc.problems.end(), [&](const PathingProblem& p) {
                            return p.kind == PathingProblemKind::Pocket && p.x / 32 == tx && p.y / 32 == ty;
                        });
                        check(it != nc.problems.end() && it->minitiles == 1 && !it->fitsUnit, "Bottleneck with a nook: the nook is a pocket");
                        check(nc.pockets == c.pockets + 1, "Bottleneck with a nook: one pocket more");
                        if (it != nc.problems.end()) {
                            std::printf("    fix: tile %d, %d: 0x%04X\n", it->fixTileX, it->fixTileY, it->fixValue);
                            check(it->fixTileX == tx && it->fixTileY == ty, "Bottleneck with a nook: the pocket has a fix");
                            if (it->fixTileX >= 0) {
                                nook.tiles[size_t(ty) * terrain.width + tx] = it->fixValue;
                                const PathingCheck fixed = checkPathing(computePathingRegions(nook, ts), nook, ts, units);
                                check(fixed.pockets == c.pockets, "Bottleneck with a nook, fixed: the pockets of the map as it was");
                            }
                        }
                    }
                }
            }
            if (name == "(5)Island Hop.scm") {
                ++seen;
                auto g = startGroups();
                std::sort(g.begin(), g.end());
                check(g.size() == 5 && std::unique(g.begin(), g.end()) == g.end(), "Island Hop: each start location in its own connected area");
            }
            if (const auto k = known.find(name); k != known.end() && k->second.first) {
                check(r.regions.size() == k->second.first && r.groups == k->second.second,
                      name + ": " + std::to_string(r.regions.size()) + " regions and " + std::to_string(r.groups) + " connected areas, as when the test was written");
            }
            // The same input, the same regions.
            const PathingRegions again = computePathingRegions(terrain, ts);
            if (again.tile != r.tile || again.regions.size() != r.regions.size()) check(false, name + ": the same regions again");
        }
    }
    check(seen == 2, "the maps (2)Bottleneck and (5)Island Hop were there");
    std::printf("%d maps, the slowest %.1f ms\n%s\n", maps, worst, failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
