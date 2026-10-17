// Paint random drag strokes (like the UI does) onto a map through the Editor and save the result
// as .chk for glitch analysis.
//
// usage: paint_stress <install dir> <map|--new[:era]> <out.chk> [strokes=60] [seed=1] [terrains, e.g. 8,12: only these] [reach=20] [brush=0: 1 to 4]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/isom.h"
#include "stardraft/game_data.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: paint_stress <install dir> <map> <out.chk> [strokes] [seed]\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor ed(data);
    const std::string src = argv[2];
    const bool fresh = src.rfind("--new", 0) == 0;
    const uint16_t era = fresh && src.size() > 6 ? uint16_t(std::atoi(src.c_str() + 6)) : 0;
    bool ok = fresh ? ed.newMap(128, 128, era, 1, err) : ed.open(argv[2], err);
    if (!ok || !ed.canPaintTerrain()) return std::fprintf(stderr, "cannot paint: %s\n", err.c_str()), 1;
    int strokes = argc > 4 ? std::atoi(argv[4]) : 60;
    std::mt19937 rng(argc > 5 ? unsigned(std::atoi(argv[5])) : 1u);
    std::vector<uint16_t> only;
    for (const char* p = argc > 6 ? argv[6] : ""; *p;) {
        only.push_back(uint16_t(std::strtoul(p, const_cast<char**>(&p), 10)));
        if (*p == ',') ++p;
    }
    double total = 0, worst = 0, strokeEnd = 0;
    int calls = 0;
    for (int s = 0; s < strokes; ++s) {
        // Every brush terrain of the tileset (jungle has 13: Temple, High Temple and the raised ones too).
        const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
        uint16_t terrain = 0;
        if (!only.empty()) terrain = only[rng() % only.size()];
        while (!table->isTerrain(terrain)) terrain = uint16_t(1 + rng() % table->terrainCount());
        const int fixedBrush = argc > 8 ? std::atoi(argv[8]) : 0;
        int brush = fixedBrush > 0 ? fixedBrush : 1 + int(rng() % 4);
        float x = float(rng() % (ed.width() * 32)), y = float(rng() % (ed.height() * 32));
        const int reach = argc > 7 ? std::atoi(argv[7]) : 20;  // how far the mouse moves between two calls (a fast mouse: 80)
        float dx = float(int(rng() % (2 * reach + 1)) - reach), dy = float(int(rng() % (2 * reach + 1)) - reach);
        ed.beginTerrainStroke();
        for (int i = 0; i < 40; ++i) {  // as the UI's brush does it, timed
            const auto t0 = std::chrono::steady_clock::now();
            ed.paintBrush(int(x + dx * i), int(y + dy * i), terrain, Brush{Brush::Diamond, float(brush)});
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            total += ms, worst = std::max(worst, ms), ++calls;
            if (std::getenv("PS_TRACE")) {
                static int last = 0;
                std::vector<std::pair<int, int>> wh;
                const int now = countTileMisfits(ed.chk(), ed.tileset(), &wh);
                if (now > last) std::printf("stroke %d step %d at (%d,%d) terrain %d brush %d: misfits %d -> %d, first (%d,%d)\n", s, i, int(x + dx * i) / 32 * 2, int(y + dy * i) / 32, terrain, brush, last, now, wh[0].first, wh[0].second);
                last = now;
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        ed.endTerrainStroke();
        strokeEnd = std::max(strokeEnd, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
    }
    std::printf("invalid diamonds %d\n", ed.invalidTerrain());
    {
        std::vector<std::pair<int, int>> where;
        const int bad = countTileMisfits(ed.chk(), ed.tileset(), &where);
        std::printf("misfits: %d", bad);
        for (size_t k = 0; k < where.size() && k < 8; ++k) std::printf(" (%d,%d)", where[k].first, where[k].second);
        std::printf("\n");
    }
    std::printf("paintBrush: %d calls, %.2f ms each on average, %.2f ms at most; the end of a stroke: %.2f ms at most\n", calls, total / std::max(1, calls), worst, strokeEnd);
    if (!ed.save(argv[3], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::printf("painted %d strokes -> %s\n", strokes, argv[3]);
    return 0;
}
