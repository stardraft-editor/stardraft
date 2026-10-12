// Two single high-dirt corners (brush 0.5) two steps apart along a diagonal, then the corner between
// them: the result should be exactly the three painted corners (a thin diagonal ridge), nothing more.
//
// usage: connect_probe <install dir>

#include <cstdio>
#include <set>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return 1;
    int failures = 0;
    // Corner (x, y) sits at pixel (64x, 32y); x + y odd. Directions along diamond edges.
    const int dirs[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    const char* names[4] = {"south-east", "south-west", "north-east", "north-west"};
    for (uint16_t era : {uint16_t(0), uint16_t(4)}) {
        for (int d = 0; d < 4; ++d) {
            Editor ed(data);
            if (!ed.newMap(64, 64, era, 1, err)) return 1;
            const int x0 = 16, y0 = 31;  // x + y odd
            const int dx = dirs[d][0], dy = dirs[d][1];
            auto paint = [&](int x, int y) { ed.paintBrush(x * 64, y * 32, 2, Brush{Brush::Diamond, 0.5f}); };
            auto high = [&] {
                std::set<std::pair<int, int>> s;
                for (int y = 0; y <= 64; ++y)
                    for (int x = 0; x <= 33; ++x)
                        if (((x + y) & 1) && ed.terrainAt(x * 64, y * 32) == 2) s.insert({x, y});
                return s;
            };
            paint(x0, y0);
            paint(x0 + 2 * dx, y0 + 2 * dy);
            auto before = high();
            paint(x0 + dx, y0 + dy);
            auto after = high();
            std::set<std::pair<int, int>> want{{x0, y0}, {x0 + dx, y0 + dy}, {x0 + 2 * dx, y0 + 2 * dy}};
            bool ok = after == want;
            failures += !ok;
            std::printf("%s %s: before connecting %zu corners, after %zu %s", tilesetName(era), names[d], before.size(), after.size(), ok ? "ok" : "EXTRA:");
            if (!ok)
                for (auto c : after)
                    if (!want.count(c)) std::printf(" (%d,%d)", c.first - x0, c.second - y0);
            std::printf("\n");
        }
    }
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
