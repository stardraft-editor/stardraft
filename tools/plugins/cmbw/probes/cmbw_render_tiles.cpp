// Draws a grid of tile values with the mod's tileset: the file is "width height", then a line of values a row (as
// cmbw_paint_one's PAINT_TILES writes it), each value group * 16 + subtile.
// usage: cmbw_render_tiles <Cosmonarchy install> <tileset> <tiles .txt> <out .png>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_render_tiles <install> <tileset> <tiles .txt> <out .png>\n"), 2;
    std::string err;
    GameData data;
    Tileset ts;
    if (!cmbw::open(data, argv[1], err) || !ts.load(data, uint16_t(std::atoi(argv[2])), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::ifstream in(argv[3]);
    int w = 0, h = 0;
    if (!(in >> w >> h) || w <= 0 || h <= 0) return std::fprintf(stderr, "bad tiles file\n"), 1;
    TerrainLayer layer;
    layer.width = uint16_t(w), layer.height = uint16_t(h);
    for (unsigned v; layer.tiles.size() < size_t(w) * h && in >> v;) layer.tiles.push_back(uint16_t(v));
    layer.tiles.resize(size_t(w) * h, 0);
    return writePng(argv[4], renderTerrain(ts, layer)) ? 0 : 1;
}
