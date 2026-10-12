// Palette pictures for one tileset: every terrain, then every doodad, on a grid (SD, or HD2 with --hd).
//
// usage: preview_gallery <install dir> <era> <out.png> [--hd]

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "stardraft/doodads.h"
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/isom.h"
#include "stardraft/previews.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: preview_gallery <install dir> <era> <out.png> [--hd]\n"), 2;
    std::string err;
    GameData data;
    Tileset ts;
    UnitSprites sprites;
    DoodadTypes doodads;
    const uint16_t era = uint16_t(std::atoi(argv[2]));
    if (!data.open(argv[1], err) || !ts.load(data, era, err) || !sprites.load(data, err) || !doodads.load(data, ts, era, err))
        return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    HdTiles hd;
    const bool useHd = argc > 4 && !std::strcmp(argv[4], "--hd") && loadHdTiles(data, era, GraphicsQuality::HD2, hd, err);
    std::vector<Sprite> pics;
    int terrains = 0, empty = 0;
    if (const IsomTerrainTable* t = IsomTerrainTable::forEra(era))
        for (uint16_t i = 1; i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) {
                pics.push_back(terrainPicture(ts, i, useHd ? &hd : nullptr));
                ++terrains, empty += pics.back().width == 0;
            }
    for (const auto& d : doodads.all())
        if (d.inPalette) pics.push_back(doodadPicture(d, ts, sprites, useHd ? &hd : nullptr));
    // Each picture fit into a 160 px cell.
    const int cell = 160, cols = 12, rows = int((pics.size() + cols - 1) / cols);
    Image img;
    img.width = cols * cell, img.height = rows * cell;
    img.rgba.assign(size_t(img.width) * img.height * 4, 200);  // light gray: transparency shows
    for (size_t i = 0; i < pics.size(); ++i) {
        const Sprite& p = pics[i];
        if (!p.width) continue;
        const double k = std::min(1.0, std::min(double(cell - 8) / p.width, double(cell - 8) / p.height));
        const int w = int(p.width * k), h = int(p.height * k), ox = int(i % cols) * cell + (cell - w) / 2, oy = int(i / cols) * cell + (cell - h) / 2;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint8_t* s = &p.rgba[(size_t(int(y / k)) * p.width + int(x / k)) * 4];
                uint8_t* d = &img.rgba[(size_t(oy + y) * img.width + ox + x) * 4];
                for (int c = 0; c < 3; ++c) d[c] = uint8_t((s[c] * s[3] + d[c] * (255 - s[3])) / 255);
                d[3] = 255;
            }
    }
    writePng(argv[3], img);
    std::printf("%s: %d terrains (%d without a picture), %zu doodads -> %s\n", tilesetName(era), terrains, empty,
                doodads.all().size(), argv[3]);
    return empty ? 1 : 0;
}
