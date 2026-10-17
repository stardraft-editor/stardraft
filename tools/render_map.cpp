// Render a map's terrain and units to PNG.
//
// usage: render_map <install dir> <map.scm|.scx|.chk> <out.png> [downscale=1] [MTXM|TILE] [SD|HD2|HD|Carbot|Carbot2]
//
// SD draws the classic tiles and the units (32 px per tile). HD2, HD and the Carbot skins (Carbot = HD,
// Carbot2 = HD2) draw the terrain only, from the HD tiles (64 or 128 px per tile), as the editor shows it.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/map_file.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/units.h"

using namespace stardraft;


int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: render_map <install dir> <map.scm|.scx|.chk> <out.png> [downscale] [MTXM|TILE] [SD|HD2|HD|Carbot|Carbot2]\n");
        return 2;
    }
    std::string err;
    std::filesystem::path mapPath = argv[2];
    Bytes raw;
    if (mapPath.extension() == ".chk") {
        std::ifstream in(mapPath, std::ios::binary);
        raw.assign(std::istreambuf_iterator<char>(in), {});
    } else if (!readMapChk(mapPath, raw, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Chk chk = Chk::parse(raw);

    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Tileset ts;
    if (!ts.load(data, readEra(chk), err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    int downscale = argc > 4 ? std::atoi(argv[4]) : 1;
    SectionName layer = "MTXM";
    if (argc > 5 && std::string(argv[5]) == "TILE") layer = "TILE";
    const std::string graphics = argc > 6 ? argv[6] : "SD";

    TerrainLayer terrain = readTerrain(chk, layer);
    Image img;
    if (graphics == "SD") {
        img = renderTerrain(ts, terrain, downscale);
        UnitSprites sprites;
        std::vector<Unit> units = readUnits(chk);
        if (sprites.load(data, err)) drawUnits(img, units, sprites, ts, downscale);
        else std::fprintf(stderr, "units not drawn: %s\n", err.c_str());
    } else {
        const bool carbot = graphics.rfind("Carbot", 0) == 0;
        const GraphicsQuality q = graphics == "HD2" || graphics == "Carbot2" ? GraphicsQuality::HD2 : GraphicsQuality::HD;
        HdTiles hd;
        if (!loadHdTiles(data, readEra(chk), q, hd, err, carbot)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        img = renderHdTerrain(ts, hd, terrain, downscale);
    }
    if (!writePng(argv[3], img)) {
        std::fprintf(stderr, "failed to write %s\n", argv[3]);
        return 1;
    }
    std::printf("%s: %ux%u tiles, tileset %s -> %dx%d px\n", mapPath.filename().string().c_str(), terrain.width,
                terrain.height, tilesetName(readEra(chk)), img.width, img.height);
    return 0;
}
