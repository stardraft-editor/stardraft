// A picture of each mod terrain of a map's tileset, painted as cmbw_paint_one does (its ground's diamond, then two
// strokes of it side by side), from the n-th terrain on, in a grid with their names below: for looking at many at once.
// usage: cmbw_terrain_sheet <Cosmonarchy install> <map> <first index> <out .png> [count]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_terrain_sheet <install> <map> <first index> <out .png> [count]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor probe(data);
    if (!probe.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const int first = std::atoi(argv[3]), count = argc > 5 ? std::atoi(argv[5]) : 99;
    const int n = std::min<int>(int(probe.tileset().modTerrains.size()) - first, count);
    if (n <= 0) return std::fprintf(stderr, "no terrains from %d\n", first), 1;
    const int cw = 24, ch = 14, cols = 4, rows = (n + cols - 1) / cols;  // (each cell: tiles)
    TerrainLayer sheet;
    sheet.width = uint16_t(cw * cols), sheet.height = uint16_t(ch * rows);
    sheet.tiles.assign(size_t(sheet.width) * sheet.height, 0);
    for (int i = 0; i < n; ++i) {
        Editor e(data);
        e.open(argv[2], err);
        const Tileset::ModTerrain& mt = e.tileset().modTerrains[size_t(first + i)];
        const int cx = e.width() * 16, cy = e.height() * 16;
        e.paintBrush(cx, cy, mt.ground, Brush{Brush::Diamond, 14.0f});
        e.paintBrush(cx - 96, cy, mt.id, Brush{Brush::Diamond, 4.0f});
        e.paintBrush(cx + 96, cy, mt.id, Brush{Brush::Diamond, 4.0f});
        const TerrainLayer t = readTerrain(e.chk(), "TILE");
        const int x0 = cx / 32 - cw / 2, y0 = cy / 32 - ch / 2;
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                if (x0 + x >= 0 && y0 + y >= 0 && x0 + x < t.width && y0 + y < t.height)
                    sheet.tiles[size_t((i / cols) * ch + y) * sheet.width + (i % cols) * cw + x] = t.tiles[size_t(y0 + y) * t.width + x0 + x];
        std::printf("%d: %s (id %u)\n", first + i, mt.name.c_str(), unsigned(mt.id));
    }
    return writePng(argv[4], renderTerrain(probe.tileset(), sheet, 2)) ? 0 : 1;
}
