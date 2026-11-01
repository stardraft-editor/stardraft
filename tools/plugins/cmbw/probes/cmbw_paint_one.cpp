// One mod terrain painted on its ground, for looking at one stroke: the ground's diamond of 16 in the map's middle,
// then the terrain's diamond of <size> there (and a second one <dx> pixels to the east when given). Prints the corners of
// each terrain around the strokes and writes the tiles there.
// usage: cmbw_paint_one <Cosmonarchy install> <map | new:<era>> <terrain name> <size> <out .png> [dx] [saved map]
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    // (game:<terrain>[:<ground>]: a game terrain by its id, on its ground (default 1), to compare with a mod terrain)
    const bool game = std::string(argv[3]).rfind("game:", 0) == 0;
    if (!mt && !game) return std::fprintf(stderr, "no mod terrain named %s\n", argv[3]), 1;
    const uint16_t id = game ? uint16_t(std::atoi(argv[3] + 5)) : mt->id;
    const char* colon = game ? std::strchr(argv[3] + 5, ':') : nullptr;
    const uint16_t ground = game ? uint16_t(colon ? std::atoi(colon + 1) : 1) : mt->ground;
    const int cx = e.width() * 16, cy = e.height() * 16, dx = argc > 6 ? std::atoi(argv[6]) : 0;
    auto env = [](const char* n, int d) { const char* v = std::getenv(n); return v ? std::atoi(v) : d; };
    e.paintBrush(cx, cy, ground, Brush{Brush::Diamond, float(env("PAINT_GROUND", 16))});  // (the ground's diamond size)
    e.paintBrush(cx, cy, id, Brush{Brush::Diamond, float(std::atof(argv[4]))});
    const int dy = env("PAINT_DY", 0);  // (the second one's offset down)
    if (dx || dy) e.paintBrush(cx + dx, cy + dy, id, Brush{Brush::Diamond, float(std::atof(argv[4]))});
    if (const char* s = std::getenv("PAINT_STROKES")) {  // (more strokes: "dx,dy,size;dx,dy,size")
        int sx = 0, sy = 0, n = 0;
        float size = 0;
        for (const char* p = s; *p; p += n + (p[n] == ';')) {
            if (std::sscanf(p, "%d,%d,%f%n", &sx, &sy, &size, &n) != 3) break;
            e.paintBrush(cx + sx, cy + sy, id, Brush{Brush::Diamond, size});
        }
    }
    std::map<int, int> corners;
    for (int y = cy / 32 - 16; y <= cy / 32 + 16; ++y)
        for (int x = cx / 64 - 9; x <= cx / 64 + 9; ++x)
            if ((x + y) & 1) ++corners[e.terrainAt(x * 64, y * 32)];
    for (auto [t, n] : corners) std::printf("terrain %d: %d corners\n", t, n);
    const TerrainLayer t = readTerrain(e.chk(), "TILE");
    TerrainLayer part;
    const int pw = env("PAINT_W", 36), ph = env("PAINT_H", 32);  // (the picture's size in tiles, around the middle)
    const int x0 = std::max(0, cx / 32 - pw / 2), y0 = std::max(0, cy / 32 - ph / 2);
    part.width = uint16_t(std::min<int>(t.width - x0, pw)), part.height = uint16_t(std::min<int>(t.height - y0, ph));
    for (int y = 0; y < part.height; ++y)
        for (int x = 0; x < part.width; ++x) part.tiles.push_back(t.tiles[size_t(y0 + y) * t.width + x0 + x]);
    if (const char* dump = std::getenv("PAINT_TILES")) {  // (the picture's tile values: "width height", then a line a row)
        if (FILE* f = std::fopen(dump, "w")) {
            std::fprintf(f, "%d %d\n", part.width, part.height);
            for (int y = 0; y < part.height; ++y)
                for (int x = 0; x < part.width; ++x) std::fprintf(f, "%u%c", part.tiles[size_t(y) * part.width + x], x + 1 < part.width ? ' ' : '\n');
            std::fclose(f);
        }
    }
    if (argc > 7 && !e.save(argv[7], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    return writePng(argv[5], renderTerrain(e.tileset(), part)) ? 0 : 1;
}
