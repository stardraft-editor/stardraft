#include "stardraft/previews.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "stardraft/doodads.h"
#include "stardraft/hd_assets.h"
#include "stardraft/isom.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace stardraft {

namespace {

// Tiles (CV5 tile values, 0xFFFF = none) to a Sprite: SD megatiles, or decoded HD2 megatiles when
// `hd` is given (HD pixels then; 64 per tile).
Sprite tilesPicture(const Tileset& ts, int w, int h, const std::vector<uint16_t>& tiles, const HdTiles* hd) {
    Sprite s;
    const int tp = hd && hd->tileSize ? hd->tileSize : 32;
    s.width = w * tp, s.height = h * tp, s.originX = s.width / 2, s.originY = s.height / 2;
    s.rgba.assign(size_t(s.width) * s.height * 4, 0);
    TerrainLayer layer;
    layer.width = uint16_t(w), layer.height = uint16_t(h);
    layer.tiles.assign(tiles.begin(), tiles.end());
    for (auto& t : layer.tiles)
        if (t == 0xFFFF) t = 0;
    const Image sd = hd ? Image{} : renderTerrain(ts, layer, 1);
    std::vector<uint8_t> px;
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) {
            const uint16_t v = tiles[size_t(ty) * w + tx];
            const int mega = v == 0xFFFF ? -1 : ts.megatileFor(v);
            if (mega <= 0) continue;  // no tile here: transparent
            if (hd && size_t(mega) < hd->tiles.size()) {
                DdsTexture tex;
                tex.format = hd->format, tex.width = tex.height = tp, tex.data = hd->tiles[size_t(mega)];
                decodeDds(tex, 0, 0, tp, tp, px);
                for (int y = 0; y < tp; ++y)
                    std::memcpy(&s.rgba[((size_t(ty) * tp + y) * s.width + size_t(tx) * tp) * 4], &px[size_t(y) * tp * 4], size_t(tp) * 4);
            } else if (!hd) {
                for (int y = 0; y < 32; ++y)
                    std::memcpy(&s.rgba[((size_t(ty) * 32 + y) * s.width + size_t(tx) * 32) * 4],
                                &sd.rgba[((size_t(ty) * 32 + y) * sd.width + size_t(tx) * 32) * 4], 32 * 4);
            }
            for (int y = 0; y < tp; ++y)  // opaque
                for (int x = 0; x < tp; ++x) s.rgba[((size_t(ty) * tp + y) * s.width + size_t(tx) * tp + x) * 4 + 3] = 255;
        }
    return s;
}

}  // namespace

Sprite terrainPicture(const Tileset& ts, uint16_t terrain, const HdTiles* hd, int w, int h) {
    w = std::max(2, w / 2 * 2), h = std::max(1, h);
    // A mod's terrain (Tileset::modTerrains): its pieces for its stand-in's solid rects (its fill); a terrain of edges only:
    // its first pieces (its edges).
    if (const Tileset::ModTerrain* m = ts.modTerrain(terrain)) {
        const int fill = solidTileGroup(ts, m->standIn, 0);
        std::vector<std::array<uint16_t, 3>> use;
        for (const auto& p : m->pieces)
            if (int(p[0]) == fill) use.push_back(p);
        if (use.empty())
            for (size_t i = 0; i < m->pieces.size() && use.size() < 4; ++i) use.push_back(m->pieces[i]);
        if (use.empty()) return terrainPicture(ts, m->standIn, hd, w, h);
        std::vector<uint16_t> tiles(size_t(w) * h);
        for (int ty = 0; ty < h; ++ty)
            for (int rx = 0; rx < w / 2; ++rx) {
                const auto& p = use[size_t(rx + ty * 3) % use.size()];
                tiles[size_t(ty) * w + 2 * rx] = p[1], tiles[size_t(ty) * w + 2 * rx + 1] = p[2];
            }
        return tilesPicture(ts, w, h, tiles, hd);
    }
    // The solid group of each half of a rect (left, right), as the tile rules give them.
    int halves[2] = {solidTileGroup(ts, terrain, 0), solidTileGroup(ts, terrain, 1)};
    if (halves[0] < 0) return {};
    if (halves[1] < 0) halves[1] = halves[0];
    std::vector<uint16_t> tiles(size_t(w) * h);
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) {
            const int g = halves[tx & 1];
            // A variant the group has (megatile set), varying across the patch.
            int sub = 0;
            if (size_t(g) < ts.groups.size())
                for (int k = 0, want = (tx / 2 + ty * 3) % 16; k < 16; ++k)
                    if (ts.groups[size_t(g)].megatiles[size_t((want + k) % 16)]) {
                        sub = (want + k) % 16;
                        break;
                    }
            tiles[size_t(ty) * w + tx] = uint16_t(g << 4 | sub);
        }
    return tilesPicture(ts, w, h, tiles, hd);
}

Sprite mapTilesPicture(const Tileset& ts, const std::vector<uint16_t>& tiles, int mapWidth, int x, int y, int w, int h, const HdTiles* hd) {
    std::vector<uint16_t> part(size_t(w) * h, 0xFFFF);
    const int mapHeight = mapWidth > 0 ? int(tiles.size()) / mapWidth : 0;
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx)
            if (x + tx >= 0 && y + ty >= 0 && x + tx < mapWidth && y + ty < mapHeight)
                part[size_t(ty) * w + tx] = tiles[size_t(y + ty) * mapWidth + x + tx];
    return tilesPicture(ts, w, h, part, hd);
}

Sprite doodadPicture(const DoodadType& d, const Tileset& ts, UnitSprites& sprites, const HdTiles* hd) {
    const int w = std::max<int>(1, d.width), h = std::max<int>(1, d.height);
    std::vector<uint16_t> tiles(size_t(w) * h, 0xFFFF);
    for (int r = 0; r < d.height; ++r) {
        const size_t g = size_t(d.firstGroup) + r;
        if (g >= ts.groups.size()) break;
        for (int c = 0; c < d.width; ++c)
            if (ts.groups[g].megatiles[size_t(c)]) tiles[size_t(r) * w + c] = uint16_t(g << 4 | c);
    }
    Sprite s = tilesPicture(ts, w, h, tiles, hd);
    // The overlay sprite sits at the doodad's center (SD art; scaled up for HD tiles).
    if (d.overlaySprite) {
        const int image = sprites.imageForSprite(d.overlaySprite);
        if (image >= 0 && !hd) {
            sprites.drawImage(s, image, sprites.script(image).initFrame, false, 0, ts);
        } else if (image >= 0) {
            Sprite sd;
            sd.width = w * 32, sd.height = h * 32, sd.originX = sd.width / 2, sd.originY = sd.height / 2;
            sd.rgba.assign(size_t(sd.width) * sd.height * 4, 0);
            sprites.drawImage(sd, image, sprites.script(image).initFrame, false, 0, ts);
            const int k = s.width / std::max(1, sd.width);
            for (int y = 0; y < s.height; ++y)
                for (int x = 0; x < s.width; ++x) {
                    const uint8_t* p = &sd.rgba[(size_t(y / k) * sd.width + x / k) * 4];
                    if (p[3]) std::copy_n(p, 4, &s.rgba[(size_t(y) * s.width + x) * 4]);
                }
        }
    }
    return s;
}

Sprite tilesSprite(const Tileset& ts, int w, int h, const std::vector<uint16_t>& tiles, const HdTiles* hd) { return tilesPicture(ts, w, h, tiles, hd); }

}  // namespace stardraft
