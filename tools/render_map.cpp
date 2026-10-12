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

namespace {

// Terrain from the HD tiles, box-filtered by `downscale`. Tiles that do not resolve are magenta.
Image renderHdTerrain(const Tileset& ts, const HdTiles& hd, const TerrainLayer& terrain, int downscale) {
    const int size = hd.tileSize, out = std::max(1, size / downscale), step = size / out;
    Image img;
    img.width = terrain.width * out;
    img.height = terrain.height * out;
    img.rgba.assign(size_t(img.width) * img.height * 4, 255);
    std::map<int, std::vector<uint8_t>> cache;  // megatile -> its tile at the output size
    for (int ty = 0; ty < terrain.height; ++ty)
        for (int tx = 0; tx < terrain.width; ++tx) {
            const int mega = ts.megatileFor(terrain.tiles[size_t(ty) * terrain.width + tx]);
            auto it = cache.find(mega);
            if (it == cache.end()) {
                std::vector<uint8_t> small(size_t(out) * out * 4, 255);
                if (mega < 0 || size_t(mega) >= hd.tiles.size()) {
                    for (size_t i = 0; i < small.size(); i += 4) small[i] = 255, small[i + 1] = 0, small[i + 2] = 255;
                } else {
                    DdsTexture t;
                    t.format = hd.format, t.width = t.height = size, t.data = hd.tiles[size_t(mega)];
                    std::vector<uint8_t> px;
                    decodeDds(t, 0, 0, size, size, px);
                    for (int oy = 0; oy < out; ++oy)
                        for (int ox = 0; ox < out; ++ox)
                            for (int c = 0; c < 3; ++c) {
                                int sum = 0;
                                for (int sy = 0; sy < step; ++sy)
                                    for (int sx = 0; sx < step; ++sx) sum += px[(size_t(oy * step + sy) * size + ox * step + sx) * 4 + c];
                                small[(size_t(oy) * out + ox) * 4 + c] = uint8_t(sum / (step * step));
                            }
                }
                it = cache.emplace(mega, std::move(small)).first;
            }
            for (int oy = 0; oy < out; ++oy)
                std::memcpy(&img.rgba[((size_t(ty) * out + oy) * img.width + size_t(tx) * out) * 4], &it->second[size_t(oy) * out * 4],
                            size_t(out) * 4);
        }
    return img;
}

}  // namespace

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
