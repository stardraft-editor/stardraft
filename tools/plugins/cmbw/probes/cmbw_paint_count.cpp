// Which of the game's rects of a border the Isometric brush makes: on a mod map, a big area of the ground terrain, then
// strokes of the other terrain (brushes of sizes 1 to 6, side by side and overlapping); counts each left tile group the
// strokes leave of the border (the mod's pieces off, to see the game's choice).
// usage: cmbw_paint_count <Cosmonarchy install> <map> <ground terrain> <terrain>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_paint_count <install> <map> <ground terrain> <terrain>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor ed(data);
    if (!ed.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const_cast<Tileset&>(ed.tileset()).groupPieces.clear();
    const uint16_t ground = uint16_t(std::atoi(argv[3])), terrain = uint16_t(std::atoi(argv[4]));
    const int w = ed.width() * 32, h = ed.height() * 32;
    ed.paintBrush(w / 2, h / 2, ground, Brush{Brush::Diamond, 40.0f});
    int n = 0;
    for (int size = 1; size <= 6; ++size)
        for (int i = 0; i < 8; ++i, ++n)
            ed.paintBrush(w / 2 - 900 + (n % 12) * 150 + (i & 1) * 40, h / 2 - 500 + (n / 12) * 220 + (i & 2) * 20, terrain, Brush{Brush::Diamond, float(size)});
    const auto terrains = tileGroupTerrains(ed.tileset());
    const TerrainLayer t = readTerrain(ed.chk(), "TILE");
    std::map<uint16_t, int> count;
    for (int y = 0; y < t.height; ++y)
        for (int x = 0; x + 1 < t.width; x += 2) {
            const uint16_t g = uint16_t(t.tiles[size_t(y) * t.width + x] >> 4);
            if (g < terrains.size() && terrains[g][1] && std::minmax(terrains[g][0], terrains[g][1]) == std::minmax(ground, terrain)) ++count[g];
        }
    for (const auto& [g, c] : count) std::printf("rect %4u: %d\n", unsigned(g), c);
    return 0;
}
