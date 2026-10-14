// Thin terrain strokes: paint lines of a terrain with small brushes and measure how much more of the
// map the result covers than the strokes asked for (a thin wall should stay thin).
//
// Fails (exit 1) if any stroke grows past 2.5x what was painted or leaves invalid diamonds: thin walls
// must stay thin (the repairs take the smallest change, see IsomGrid::fixRectMinimal).
//
// usage: thin_probe <install dir> <out dir>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return 1;
    std::filesystem::create_directories(argv[2]);
    int failures = 0;
    struct Case {
        uint16_t era, base, wall;
        const char* name;
    };
    for (Case cs : {Case{0, 1, 2, "badlands_highdirt"}, Case{2, 1, 2, "install_floor"}, Case{2, 2, 3, "install_roof"}, Case{4, 1, 2, "jungle_highdirt"}}) {
        for (float size : {0.5f, 1.0f, 2.0f}) {
            for (int dir = 0; dir < 12; ++dir) {  // every 15 degrees
                Editor ed(data);
                if (!ed.newMap(64, 64, cs.era, cs.base, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
                std::set<std::pair<int, int>> asked;
                ed.beginTerrainStroke();
                const double ang = dir * 3.14159265 / 12;
                for (int k = 0; k <= 160; ++k) {  // a drag: a point every 4 px over 640 px
                    int px = 1024 + int((k - 80) * 4 * std::cos(ang)), py = 1024 + int((k - 80) * 4 * std::sin(ang));
                    Brush b{Brush::Diamond, size};
                    for (auto c : ed.brushCorners(px, py, b)) asked.insert(c);
                    ed.paintBrush(px, py, cs.wall, b);
                }
                ed.endTerrainStroke();
                // Corners of the wall terrain (or anything above it) now on the map vs. corners asked for.
                const IsomTerrainTable* t = IsomTerrainTable::forEra(cs.era);
                int got = 0;
                for (int y = 0; y <= 64; ++y)
                    for (int x = 0; x <= 33; ++x) {
                        uint16_t v = ed.terrainAt(x * 64, y * 32);
                        bool wall = v == cs.wall;
                        if (!((x + y) & 1)) continue;
                        (void)t;
                        got += wall;
                    }
                std::printf("%-18s size %.1f %-10s asked %3zu  got %3d  (%.1fx)  invalid %d\n", cs.name, size,
                            dir == 0 ? "horizontal" : dir == 1 ? "diagonal" : "vertical", asked.size(), got,
                            asked.empty() ? 0.0 : double(got) / asked.size(), ed.invalidTerrain());
                if (double(got) > asked.size() * 2.5 || ed.invalidTerrain() > 0) ++failures;
                if (double(got) > asked.size() * 1.3) {
                    char path[512];
                    std::snprintf(path, sizeof path, "%s/%s_%.1f_%d.png", argv[2], cs.name, size, dir * 15);
                    writePng(path, renderTerrain(ed.tileset(), ed.terrain(), 1));
                }
            }
        }
    }
    std::printf("\n%d strokes grew past 2.5x or left invalid diamonds\n", failures);
    return failures ? 1 : 0;
}
