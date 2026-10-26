// Each mod terrain of a map's tileset in the shapes its edges must take: a concave L (inner corners), a thin line one
// diamond wide (half-width slices) and a single diamond, painted on its ground; a sheet of them (a row a terrain, its
// name in the file's list) for looking at the edges. Prints how many of each shape's rects have no piece of the terrain
// (the game's tiles there: "gaps").
// usage: cmbw_edge_sheet <Cosmonarchy install> <map> <out .png> [name filter]
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_edge_sheet <install> <map> <out.png> [filter]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor base(data);
    if (!base.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const std::string filter = argc > 4 ? argv[4] : "";
    const int partW = 40, partH = 22;  // tiles of each terrain's part of the sheet
    std::vector<const Tileset::ModTerrain*> list;
    for (const auto& m : base.tileset().modTerrains)
        if (filter.empty() || m.name.find(filter) != std::string::npos) list.push_back(&m);
    TerrainLayer sheet;
    sheet.width = uint16_t(partW), sheet.height = uint16_t(partH * list.size());
    sheet.tiles.assign(size_t(sheet.width) * sheet.height, 0);
    int row = 0;
    for (const Tileset::ModTerrain* m : list) {
        Editor e(data);
        if (!e.open(argv[2], err)) return 1;
        const int cx = e.width() * 16, cy = e.height() * 16;
        e.paintBrush(cx, cy, m->ground, Brush{Brush::Diamond, 40.0f});
        // The L: two strokes of 4 meeting at a right angle (its inside corner a concave edge), at the left.
        const int lx = cx - 12 * 32, ly = cy;
        e.paintBrush(lx, ly - 3 * 32, m->id, Brush{Brush::Diamond, 4.0f});
        e.paintBrush(lx + 2 * 64, ly - 1 * 32, m->id, Brush{Brush::Diamond, 4.0f});
        e.paintBrush(lx, ly + 1 * 32, m->id, Brush{Brush::Diamond, 4.0f});
        // The thin line: diamonds of 1 along a diagonal, in the middle.
        for (int k = 0; k < 5; ++k) e.paintBrush(cx + 2 * 32 + k * 64, cy - 4 * 32 + k * 32, m->id, Brush{Brush::Diamond, 1.0f});
        // One diamond, at the right.
        e.paintBrush(cx + 15 * 32, cy - 2 * 32, m->id, Brush{Brush::Diamond, 1.0f});
        const TerrainLayer t = readTerrain(e.chk(), "TILE");
        const int x0 = cx / 32 - partW / 2, y0 = cy / 32 - partH / 2;
        // Gaps: rects with a corner of the terrain whose tiles are not its pieces (nor its fill).
        std::set<uint16_t> mine;
        for (const auto& p : m->pieces) mine.insert(p[1]), mine.insert(p[2]);
        int gaps = 0;
        for (int y = 0; y < partH; ++y)
            for (int x = 0; x < partW; ++x) {
                const int tx = x0 + x, ty = y0 + y;
                const uint16_t v = tx >= 0 && ty >= 0 && tx < t.width && ty < t.height ? t.tiles[size_t(ty) * t.width + tx] : 0;
                sheet.tiles[size_t(row * partH + y) * sheet.width + x] = v;
                if (!(tx & 1)) {
                    bool touches = false;
                    for (int dy = 0; dy <= 1 && !touches; ++dy)
                        for (int dx = 0; dx <= 1 && !touches; ++dx) {
                            const int c = tx / 2 + dx, r = ty + dy;
                            if ((c + r) & 1) touches = e.terrainAt(c * 64, r * 32) == m->id;
                        }
                    if (touches && !mine.count(v)) ++gaps;
                }
            }
        std::printf("%3d %-40s gaps %d  (id %d on %d %s)\n", row, m->name.c_str(), gaps, int(m->id), int(m->ground),
                    e.terrainName(m->ground).c_str());
        if (std::getenv("EDGE_DUMP"))  // (the tiles and corners of rows 6-11, columns 26-37 of the part; * a piece of it)
            for (int y = 6 + (std::getenv("EDGE_Y") ? std::atoi(std::getenv("EDGE_Y")) : 0), y1 = y + 5; y <= y1; ++y) {
                for (int x = 26 + (std::getenv("EDGE_X") ? std::atoi(std::getenv("EDGE_X")) : 0), x1 = x + 11; x <= x1; ++x) {
                    const uint16_t v = t.tiles[size_t(y0 + y) * t.width + x0 + x];
                    std::printf(" %4d.%-2d%c", v >> 4, v & 15, mine.count(v) ? '*' : ' ');
                }
                std::printf("  |");
                for (int c = (x0 + 26 + (std::getenv("EDGE_X") ? std::atoi(std::getenv("EDGE_X")) : 0)) / 2, c1 = c + 6; c <= c1; ++c)
                    std::printf(" %d", ((c + y0 + y) & 1) ? int(e.terrainAt(c * 64, (y0 + y) * 32)) : -1);
                std::printf("\n");
            }
        ++row;
    }
    return writePng(argv[3], renderTerrain(base.tileset(), sheet, 2)) ? 0 : 1;
}
