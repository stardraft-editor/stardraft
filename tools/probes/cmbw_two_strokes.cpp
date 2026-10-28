// Two mod terrains painted beside each other on the first one's ground; writes the tiles there as a PNG.
// usage: cmbw_two_strokes <Cosmonarchy install> <map> <terrain A> <terrain B> <out .png> [dx rects] [dy rects]
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
    if (argc < 6) return std::fprintf(stderr, "usage: cmbw_two_strokes <install> <map> <A> <B> <out.png> [dx] [dy]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const Tileset::ModTerrain *a = nullptr, *b = nullptr;
    for (const auto& m : e.tileset().modTerrains) {
        if (m.name == argv[3]) a = &m;
        if (m.name == argv[4]) b = &m;
    }
    if (!a || !b) return std::fprintf(stderr, "no such terrain\n"), 1;
    const int cx = e.width() * 16, cy = e.height() * 16, dx = argc > 6 ? std::atoi(argv[6]) : -4, dy = argc > 7 ? std::atoi(argv[7]) : 3;
    e.paintBrush(cx, cy, a->ground, Brush{Brush::Diamond, 30.0f});
    e.paintBrush(cx, cy, a->id, Brush{Brush::Diamond, 4.0f});
    e.paintBrush(cx + dx * 64, cy + dy * 32, b->id, Brush{Brush::Diamond, 2.0f});
    const TerrainLayer t = readTerrain(e.chk(), "TILE");
    TerrainLayer part;
    const int x0 = std::max(0, cx / 32 - 18), y0 = std::max(0, cy / 32 - 12);
    part.width = 36, part.height = 24;
    for (int y = 0; y < part.height; ++y)
        for (int x = 0; x < part.width; ++x) part.tiles.push_back(t.tiles[size_t(y0 + y) * t.width + x0 + x]);
    return writePng(argv[5], renderTerrain(e.tileset(), part)) ? 0 : 1;
}
