// Each mod terrain of a map's tileset, painted in small strokes on its ground, beside its ground's other neighbours and
// over other mod terrains: the time of each stroke and the corners that changed far from it (a stroke of size s changes
// corners within about s + 4 rects; more than that: the terrain broke down around the click).
// With --all: each mod terrain then gets every terrain of the tileset (the game's and the mod's) painted beside it.
// usage: cmbw_stroke_bench <Cosmonarchy install> <map> [name filter] [--all]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_stroke_bench <install> <map> [name filter]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor base(data);
    if (!base.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const bool all = std::string(argv[argc - 1]) == "--all";
    const std::string filter = argc > 3 && std::string(argv[3]) != "--all" ? argv[3] : "";
    const IsomTerrainTable* game = IsomTerrainTable::forEra(base.era());
    const auto mods = base.tileset().modTerrains;
    double worst = 0;
    int broken = 0;
    for (const Tileset::ModTerrain& m : mods) {
        if (!filter.empty() && m.name.find(filter) == std::string::npos) continue;
        Editor e(data);
        if (!e.open(argv[2], err)) return 1;
        const int cx = e.width() * 16, cy = e.height() * 16;
        e.paintBrush(cx, cy, m.ground, Brush{Brush::Diamond, 30.0f});
        // Strokes: the terrain in the middle, then along a line (each overlapping the last), then other mod terrains
        // that stand on the same ground beside it.
        struct Stroke {
            int x, y;
            uint16_t t;
            float size;
        };
        std::vector<Stroke> strokes{{cx, cy, m.id, 4}};
        for (int k = 1; k <= 6; ++k) strokes.push_back({cx + k * 64, cy + (k & 1) * 32, m.id, 2});
        for (const Tileset::ModTerrain& o : mods)
            if (o.id != m.id && o.ground == m.ground && strokes.size() < 14) strokes.push_back({cx - 4 * 64, cy + 3 * 32, o.id, 2});
        if (all) {  // (every terrain beside it, each in a fresh spot along its edge: west, then around)
            std::vector<uint16_t> others;
            for (uint16_t t = 1; game && t <= game->terrainCount(); ++t)
                if (game->isTerrain(t)) others.push_back(t);
            for (const Tileset::ModTerrain& o : mods)
                if (o.id != m.id) others.push_back(o.id);
            for (size_t k = 0; k < others.size(); ++k) {
                const int a = int(k % 4), r = 3 + int(k / 4) % 2;
                const int dx = a == 0 ? -r : a == 1 ? r : 0, dy = a == 2 ? -r : a == 3 ? r : 0;
                strokes.push_back({cx + dx * 64, cy + dy * 32, others[k], 2});
                strokes.push_back({cx, cy, m.id, 4});  // (and the terrain again, so the next one meets it)
            }
        }
        strokes.push_back({cx + 2 * 64, cy - 2 * 32, m.ground, 2});
        double slowest = 0, total = 0;
        int far = 0;
        for (const Stroke& s : strokes) {
            std::vector<uint16_t> before;
            const int cols = e.width() / 2 + 1, rows = e.height() + 1;
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x) before.push_back(((x + y) & 1) ? e.terrainAt(x * 64, y * 32) : 0);
            const auto t0 = std::chrono::steady_clock::now();
            e.paintBrush(s.x, s.y, s.t, Brush{Brush::Diamond, s.size});
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            slowest = std::max(slowest, ms), total += ms;
            const int farBefore = far;
            const int reach = int(s.size) + 6, sx = s.x / 64, sy = s.y / 32;
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x)
                    if (((x + y) & 1) && before[size_t(y) * cols + x] != e.terrainAt(x * 64, y * 32) &&
                        (std::abs(x - sx) > reach || std::abs(y - sy) > 2 * reach)) {
                        ++far;
                        if (std::getenv("FAR_DUMP") && far - farBefore <= 6)
                            std::printf("      far %d,%d: %s -> %s\n", x, y, e.terrainName(before[size_t(y) * cols + x]).c_str(),
                                        e.terrainName(e.terrainAt(x * 64, y * 32)).c_str());
                    }
            if (std::getenv("BENCH_EACH") || (all && (ms > 60 || far - farBefore > 0)))
                std::printf("    %-34s size %.0f at %d,%d: %.1f ms, far %d\n", e.terrainName(s.t).c_str(), s.size, s.x / 64, s.y / 32, ms, far - farBefore);
        }
        worst = std::max(worst, slowest);
        broken += far > 0;
        std::printf("%-44s %3zu strokes  slowest %7.1f ms  mean %6.1f ms  far corners %d\n", m.name.c_str(), strokes.size(), slowest,
                    total / double(strokes.size()), far);
    }
    std::printf("worst stroke %.1f ms, %d terrains changed corners far from a stroke\n", worst, broken);
    return 0;
}
