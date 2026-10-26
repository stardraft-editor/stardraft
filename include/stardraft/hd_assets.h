#pragma once

// StarCraft: Remastered HD assets, decoded to GPU-ready (still compressed) data. GPU-free.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;
class UnitSprites;
struct Sprite;

enum class GraphicsQuality { SD, HD2, HD };
const char* qualityName(GraphicsQuality q);

// One mip level of a block-compressed DDS texture.
struct DdsTexture {
    enum Format { BC1, BC3 } format = BC1;
    int width = 0, height = 0;
    Bytes data;  // top mip level only

    bool parse(std::span<const uint8_t> dds);
    size_t blockBytes() const { return format == BC1 ? 8 : 16; }
};

// One images.dat image from an HD/HD2 .anim file.
struct AnimImage {
    struct Frame {
        uint16_t x = 0, y = 0;    // in the texture (full-HD pixels; multiply by texScale)
        int16_t xOff = 0, yOff = 0;  // inside the frame box (full-HD pixels)
        uint16_t width = 0, height = 0;  // full-HD pixels
    };
    int boxWidth = 0, boxHeight = 0;  // frame box, full-HD pixels; the image origin is its center
    bool boxGuessed = false;          // the file declares none: the box is the frames' extent
    float texScale = 1;               // texture pixels per full-HD pixel (1 for HD, 0.5 for HD2)
    DdsTexture diffuse, teamColor;    // teamColor may be empty
    std::vector<Frame> frames;
};

// Load images.dat image `image` ("anim\main_NNN.anim", or "HD2\anim\..." for HD2).
// `carbot`: the Cartooned (Carbot) skin, anim\Carbot\ and Carbot\TileSet\; images it does not
// cover fall back to the normal art.
bool loadAnimImage(const GameData& data, int image, GraphicsQuality quality, AnimImage& out, std::string& error, bool carbot = false);

// HD megatile graphics ("TileSet\<name>.dds.vr4" or "HD2\..."): one texture per megatile, indexed like
// the SD megatiles (VX4EX).
struct HdTiles {
    int tileSize = 0;  // 128 (HD) or 64 (HD2)
    DdsTexture::Format format = DdsTexture::BC1;
    std::vector<Bytes> tiles;  // compressed blocks of each megatile
};
bool loadHdTiles(const GameData& data, uint16_t era, GraphicsQuality quality, HdTiles& out, std::string& error, bool carbot = false);

// The HD or HD2 art of a GRP that no images.dat entry names (UnitSprites::extraImage: "tileset\<name>.grp", the creep
// edges): "<path>.dds.grp" (or "HD2\..."; with `carbot`, the Carbot skin's first), one DDS a frame, its frames put into
// one texture as an AnimImage. The file gives no frame offsets: each frame sits where the SD frame of `sd` (the same
// GRP) sits, kept inside the box.
struct Grp;
bool loadHdGrp(const GameData& data, const std::string& sdPath, const Grp& sd, GraphicsQuality quality, AnimImage& out,
               std::string& error, bool carbot = false);

// Previews drawn without the GPU (src/hd_preview.cpp).
// Decodes the region (x0, y0, w, h) of a block-compressed texture to RGBA (straight alpha).
void decodeDds(const DdsTexture& tex, int x0, int y0, int w, int h, std::vector<uint8_t>& rgba);
// A map's terrain from the HD tiles, box-filtered by `downscale` (1, 2, 4 ...: hd.tileSize / downscale pixels a tile),
// as the editor shows it. Tiles that do not resolve are magenta.
struct Image;
struct Tileset;
struct TerrainLayer;
Image renderHdTerrain(const Tileset& ts, const HdTiles& hd, const TerrainLayer& terrain, int downscale);
// The HD2 command-card icons of units 0..227 (HD2\unit\cmdicons\cmdicons.dds.grp, or the Carbot
// skin's), decoded; units without an icon (the game's placeholder) get an empty Sprite.
bool loadHdCommandIcons(const GameData& data, bool carbot, std::vector<Sprite>& icons, std::string& error);
// A unit in HD2 (or Carbot) art: its image and subunit in their default frames, centered on the
// largest frame box, team color applied as the renderer does. Texture pixels.
bool hdUnitPicture(const GameData& data, const UnitSprites& sprites, uint16_t unitType, uint32_t teamRgb, bool carbot, Sprite& out,
                   std::string& error);

}  // namespace stardraft
