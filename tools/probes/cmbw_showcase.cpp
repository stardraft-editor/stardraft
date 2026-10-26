// A picture of a tileset's mod terrains together (Tileset::modTerrains): on a mod map, each terrain painted on its
// ground side by side (a big diamond and smaller strokes over each other), in the list's order from the left.
// usage: cmbw_showcase <Cosmonarchy install> <map> <out .png>   (CMBW_STANDIN=1: each terrain's stand-in, for comparing)
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
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_showcase <install> <map> <out .png>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const auto& mts = e.tileset().modTerrains;
    if (mts.empty()) return std::fprintf(stderr, "no mod terrains in this tileset\n"), 1;
    const int n = int(mts.size()), step = 14 * 32;  // (each terrain's place: 14 tiles wide)
    const int w = e.width() * 32, h = e.height() * 32;
    const int left = std::max(256, w / 2 - n * step / 2), cy = h / 2;
    std::string order;
    for (int i = 0; i < n; ++i)  // (the grounds first: a later one does not paint over an earlier terrain)
        e.paintBrush(left + i * step + step / 2, cy, mts[size_t(i)].ground, Brush{Brush::Diamond, 7.0f});
    for (int i = 0; i < n; ++i) {
        const auto& m = mts[size_t(i)];
        const int cx = left + i * step + step / 2;
        const uint16_t t = std::getenv("CMBW_STANDIN") ? m.standIn : m.id;
        e.paintBrush(cx, cy - 64, t, Brush{Brush::Diamond, 3.0f});
        e.paintBrush(cx - 40, cy + 70, t, Brush{Brush::Diamond, 1.0f});
        e.paintBrush(cx + 40, cy + 86, t, Brush{Brush::Diamond, 1.0f});
        order += (i ? ", " : "") + m.name;
    }
    const TerrainLayer t = readTerrain(e.chk(), "TILE");
    TerrainLayer part;
    const int x0 = std::max(0, left / 32 - 1), y0 = std::max(0, cy / 32 - 9);
    part.width = uint16_t(std::min<int>(t.width - x0, n * 14 + 2)), part.height = uint16_t(std::min<int>(t.height - y0, 18));
    for (int y = 0; y < part.height; ++y)
        for (int x = 0; x < part.width; ++x) part.tiles.push_back(t.tiles[size_t(y0 + y) * t.width + x0 + x]);
    std::printf("%s\n", order.c_str());
    return writePng(argv[3], renderTerrain(e.tileset(), part)) ? 0 : 1;
}
