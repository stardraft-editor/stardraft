// One mod terrain painted on its ground, for looking at one stroke: the ground's diamond of 16 in the map's middle,
// then the terrain's diamond of <size> there (and a second one <dx> pixels to the east when given). Prints the corners of
// each terrain around the strokes and writes the tiles there.
// usage: cmbw_paint_one <Cosmonarchy install> <map | new:<era>> <terrain name> <size> <out .png> [dx] [saved map]
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 6) return std::fprintf(stderr, "usage: cmbw_paint_one <install> <map> <terrain name> <size> <out .png> [dx]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (std::string(argv[2]).rfind("new:", 0) == 0) {  // (new:<era>: a new 128 x 128 map of that tileset)
        if (!e.newMap(128, 128, uint16_t(std::atoi(argv[2] + 4)), 1, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    } else if (!e.open(argv[2], err)) {
        return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    }
    const Tileset::ModTerrain* mt = nullptr;
    for (const auto& m : e.tileset().modTerrains)
        if (m.name == argv[3]) mt = &m;
    if (!mt) return std::fprintf(stderr, "no mod terrain named %s\n", argv[3]), 1;
    const int cx = e.width() * 16, cy = e.height() * 16, dx = argc > 6 ? std::atoi(argv[6]) : 0;
    e.paintBrush(cx, cy, mt->ground, Brush{Brush::Diamond, 16.0f});
    e.paintBrush(cx, cy, mt->id, Brush{Brush::Diamond, float(std::atof(argv[4]))});
    if (dx) e.paintBrush(cx + dx, cy, mt->id, Brush{Brush::Diamond, float(std::atof(argv[4]))});
    std::map<int, int> corners;
    for (int y = cy / 32 - 16; y <= cy / 32 + 16; ++y)
        for (int x = cx / 64 - 9; x <= cx / 64 + 9; ++x)
            if ((x + y) & 1) ++corners[e.terrainAt(x * 64, y * 32)];
    for (auto [t, n] : corners) std::printf("terrain %d: %d corners\n", t, n);
    const TerrainLayer t = readTerrain(e.chk(), "TILE");
    TerrainLayer part;
    const int x0 = std::max(0, cx / 32 - 18), y0 = std::max(0, cy / 32 - 16);
    part.width = uint16_t(std::min<int>(t.width - x0, 36)), part.height = uint16_t(std::min<int>(t.height - y0, 32));
    for (int y = 0; y < part.height; ++y)
        for (int x = 0; x < part.width; ++x) part.tiles.push_back(t.tiles[size_t(y0 + y) * t.width + x0 + x]);
    if (argc > 7 && !e.save(argv[7], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    return writePng(argv[5], renderTerrain(e.tileset(), part)) ? 0 : 1;
}
