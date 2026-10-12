// Load the HD assets used by the renderer and report what was found.
//
// usage: hd_test <install dir>

#include <chrono>
#include <cstdio>

#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/sprites.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: hd_test <install dir>\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    int failures = 0;
    for (auto q : {GraphicsQuality::HD2, GraphicsQuality::HD}) {
        for (uint16_t era = 0; era < 8; ++era) {
            auto t0 = std::chrono::steady_clock::now();
            HdTiles tiles;
            bool ok = loadHdTiles(data, era, q, tiles, err);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("%-3s tiles era %u: %s %zu tiles of %d px (%.0f ms)\n", qualityName(q), era, ok ? "ok" : err.c_str(),
                        tiles.tiles.size(), tiles.tileSize, ms);
            failures += !ok;
        }
    }
    UnitSprites sprites;
    UnitTypes types;
    if (!sprites.load(data, err) || !types.load(data, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    int animOk = 0, animFail = 0;
    for (uint16_t t = 0; t < types.size(); ++t) {
        if (!(types[t].availability & UnitAvail::Palette)) continue;
        int img = sprites.imageFor(t);
        AnimImage a;
        if (loadAnimImage(data, img, GraphicsQuality::HD2, a, err)) ++animOk;
        else ++animFail, std::printf("anim fail %s (image %d): %s\n", types[t].name.c_str(), img, err.c_str());
    }
    AnimImage marine;
    if (loadAnimImage(data, sprites.imageFor(0), GraphicsQuality::HD, marine, err))
        std::printf("marine HD: box %dx%d, %zu frames, diffuse %dx%d %s, teamcolor %dx%d\n", marine.boxWidth, marine.boxHeight,
                    marine.frames.size(), marine.diffuse.width, marine.diffuse.height,
                    marine.diffuse.format == DdsTexture::BC3 ? "BC3" : "BC1", marine.teamColor.width, marine.teamColor.height);
    std::printf("palette unit images (HD2): %d ok, %d failed\n", animOk, animFail);
    return failures || animFail ? 1 : 0;
}
