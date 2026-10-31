// Ground pathing (R24) on real maps: walk grid, regions, paths between the start locations, town areas, timing.
//
// usage: pathing_test <install dir> <maps dir> [more maps dirs...] [--dump <dir>]
// --dump: a picture of each map (8 px per tile) with the walk grid, the regions, the paths and the town areas.
// Checks: (2)Bottleneck.scm has a ground path between its two start locations with a plausible length; (5)Island
// Hop.scm has no ground path; each start location's town area has plausible counts; each analysis of a map of
// 128x128 or less takes less than 50 ms (reported for every map).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/pathing.h"
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

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: pathing_test <install dir> <maps dir> [more maps dirs...]\n");
        return 2;
    }
    {  // (gas or minerals: a mod's Vespene Ridge is gas, Discord feedback #183)
        UnitType ridge, geyser, field;
        ridge.name = "Vespene Ridge", geyser.name = "Vespene Geyser", field.name = "Mineral Field (Type 1)";
        check(isGasResource(ridge) && isGasResource(geyser) && !isGasResource(field), "gas resources: a geyser and a ridge are gas, a mineral field is not");
    }
    std::string err;
    GameData data;
    UnitTypes types;
    if (!data.open(argv[1], err) || !types.load(data, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::string dump;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--dump") dump = argv[i + 1], argc = i;
    Tileset tilesets[8];
    bool loaded[8] = {};
    bool sawBottleneck = false, sawIsland = false;
    int maps = 0, slow = 0;
    double worst = 0;
    std::string worstName;
    for (int d = 2; d < argc; ++d) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(argv[d], ec)) {
            std::string ext = entry.path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".scm" && ext != ".scx") continue;
            Bytes raw;
            if (!readMapChk(entry.path(), raw, err)) continue;
            const Chk chk = Chk::parse(raw);
            const uint16_t era = readEra(chk);
            if (!loaded[era] && !tilesets[era].load(data, era, err)) continue;
            loaded[era] = true;
            const TerrainLayer terrain = readTerrain(chk);
            const std::vector<Unit> units = readUnits(chk);
            const std::vector<MapSprite> sprites = readSprites(chk);
            PathingInput in{&terrain, &tilesets[era], &types, &units, &sprites};
            // The first run fills the caches; the second is the one measured (as the editor runs it again on a change).
            analyzePathing(in);
            const PathingAnalysis a = analyzePathing(in);
            ++maps;
            const std::string name = entry.path().filename().string();
            int found = 0;
            for (const auto& p : a.paths) found += p.found;
            std::printf("%-40s %3dx%-3d %d starts %zu bases %zu regions, paths %d of %zu, %.1f ms (grid %.1f, regions %.1f, clearance %.1f, bases %.1f, paths %.1f)\n",
                        name.c_str(), terrain.width, terrain.height, int(std::count_if(a.bases.begin(), a.bases.end(), [](const Base& b) { return b.start; })),
                        a.bases.size(), a.regions.size.size(), found, a.paths.size(), a.totalMs, a.gridMs, a.regionsMs, a.clearanceMs, a.basesMs, a.pathsMs);
            if (a.totalMs > worst) worst = a.totalMs, worstName = name;
            if (terrain.width <= 128 && terrain.height <= 128 && a.totalMs >= 50) ++slow;
            if (!dump.empty()) {
                Image img = renderTerrain(tilesets[era], terrain, 4);  // 8 px per tile: 2 px per cell
                auto blend = [&](int x, int y, uint32_t rgb, int alpha) {
                    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
                    uint8_t* p = &img.rgba[(size_t(y) * img.width + x) * 4];
                    for (int c = 0; c < 3; ++c) p[c] = uint8_t((p[c] * (255 - alpha) + ((rgb >> (16 - 8 * c)) & 255) * alpha) / 255);
                };
                static const uint32_t tints[] = {0xE6194B, 0x3CB44B, 0xFFE119, 0x4363D8, 0xF58231, 0x911EB4, 0x46F0F0, 0xF032E6};
                for (int y = 0; y < img.height; ++y)
                    for (int x = 0; x < img.width; ++x) {
                        const int r = a.regions.id[size_t(y / 2) * a.grid.width + x / 2];
                        if (r < 0) blend(x, y, 0, 150);
                        else if (r > 0) blend(x, y, tints[r % 8], 90);
                    }
                for (const Base& b : a.bases)
                    for (int t : b.buildable)
                        for (int y = 0; y < 8; ++y)
                            for (int x = 0; x < 8; ++x)
                                if (x == 0 || y == 0) blend((t % terrain.width) * 8 + x, (t / terrain.width) * 8 + y, b.start ? 0x40FF40 : 0x40C0FF, 200);
                for (const auto& p : a.paths)
                    for (size_t k = 1; k < p.points.size(); ++k) {
                        const auto [x0, y0] = p.points[k - 1];
                        const auto [x1, y1] = p.points[k];
                        const int n = int(std::max(std::abs(x1 - x0), std::abs(y1 - y0)) / 2) + 1;
                        for (int s = 0; s <= n; ++s)
                            for (int d = -1; d <= 1; ++d)
                                blend(int((x0 + (x1 - x0) * s / n) / 4) + d, int((y0 + (y1 - y0) * s / n) / 4), 0xFFFFFF, 255);
                    }
                fs::create_directories(dump, ec);
                writePng((fs::path(dump) / (entry.path().stem().string() + ".png")).string().c_str(), img);
            }
            const bool melee = name.size() > 3 && name[0] == '(';
            if (melee)
                for (const Base& b : a.bases)
                    if (b.start)
                        std::printf("    P%d: %d buildable tiles, %d minerals (%u), %d geysers (%u)\n", b.owner + 1, int(b.buildable.size()), b.minerals,
                                    b.mineralAmount, b.geysers, b.gasAmount);
            if (melee)
                for (const Base& b : a.bases)
                    if (!b.start)
                        std::printf("    expansion at tile %d, %d: %d buildable tiles, %d minerals (%u), %d geysers (%u)\n", int(b.x) / 32, int(b.y) / 32,
                                    int(b.buildable.size()), b.minerals, b.mineralAmount, b.geysers, b.gasAmount);
            if (name == "(2)Bottleneck.scm") {
                sawBottleneck = true;
                check(a.paths.size() == 1 && a.paths[0].found, "Bottleneck: a ground path between the two start locations");
                if (!a.paths.empty()) {
                    const auto& p = a.paths[0];
                    const double straight = std::hypot(a.bases[0].x - a.bases[1].x, a.bases[0].y - a.bases[1].y) / 32;
                    std::printf("    path %.1f tiles (straight line %.1f tiles), %zu corners\n", p.lengthTiles, straight, p.points.size());
                    check(p.lengthTiles >= straight && p.lengthTiles < straight * 2.5 && p.lengthTiles > 40, "Bottleneck: the path's length is plausible");
                }
                bool counts = a.bases.size() >= 2;
                for (const Base& b : a.bases)
                    if (b.start) counts &= b.minerals >= 6 && b.minerals <= 12 && b.geysers >= 1 && b.buildable.size() >= 80 && b.buildable.size() <= 460;
                check(counts, "Bottleneck: the town area counts are plausible (6-12 minerals, a geyser, 80-460 buildable tiles)");
                check(a.bases.size() > 2, "Bottleneck: expansions found");
            }
            if (name == "(5)Island Hop.scm") {
                sawIsland = true;
                bool none = !a.paths.empty();
                for (const auto& p : a.paths) none &= !p.found;
                check(none, "Island Hop: no ground path between any start locations");
                bool counts = true;
                for (const Base& b : a.bases)
                    if (b.start) counts &= b.minerals >= 6 && b.geysers >= 1 && b.buildable.size() >= 40;
                check(counts, "Island Hop: the town area counts are plausible");
            }
        }
    }
    check(sawBottleneck && sawIsland, "the test maps (2)Bottleneck.scm and (5)Island Hop.scm were found");
    std::printf("%d maps; slowest %.1f ms (%s); maps of 128x128 or less that took 50 ms or more: %d\n", maps, worst, worstName.c_str(), slow);
    check(slow == 0, "every map of 128x128 or less in less than 50 ms");
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
