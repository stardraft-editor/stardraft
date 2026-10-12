#include "stardraft/terrain.h"

#include <algorithm>

#include "byteio.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_FALLBACK
#include <stb_image_write.h>

namespace stardraft {

bool readDimensions(const Chk& chk, uint16_t& width, uint16_t& height) {
    const Section* dim = chk.find("DIM ");
    if (!dim || dim->data.size() < 4) return false;
    width = readU16(dim->data, 0);
    height = readU16(dim->data, 2);
    return true;
}

uint16_t readEra(const Chk& chk) {
    const Section* era = chk.find("ERA ");
    return era && era->data.size() >= 2 ? readU16(era->data, 0) & 7 : 0;
}

TerrainLayer readTerrain(const Chk& chk, SectionName section) {
    TerrainLayer t;
    readDimensions(chk, t.width, t.height);
    t.tiles.assign(static_cast<size_t>(t.width) * t.height, 0);
    for (const auto& s : chk.sections) {
        if (s.name != section) continue;
        size_t n = std::min(t.tiles.size(), s.data.size() / 2);
        for (size_t i = 0; i < n; ++i) t.tiles[i] = readU16(s.data, i * 2);
    }
    return t;
}

Image renderTerrain(const Tileset& ts, const TerrainLayer& terrain, int downscale) {
    downscale = std::clamp(downscale, 1, 32);
    Image img;
    img.width = terrain.width * 32 / downscale;
    img.height = terrain.height * 32 / downscale;
    img.rgba.assign(static_cast<size_t>(img.width) * img.height * 4, 0);

    // Render one megatile to 32x32 RGB, then box-filter into the output.
    uint8_t tile[32 * 32 * 3];
    const int area = downscale * downscale;
    const int outPerTile = 32 / downscale;
    for (int ty = 0; ty < terrain.height; ++ty) {
        for (int tx = 0; tx < terrain.width; ++tx) {
            int mega = ts.megatileFor(terrain.tiles[static_cast<size_t>(ty) * terrain.width + tx]);
            if (mega < 0) {
                for (int p = 0; p < 32 * 32; ++p) tile[p * 3] = 255, tile[p * 3 + 1] = 0, tile[p * 3 + 2] = 255;
            } else {
                const auto& refs = ts.megatiles[mega];
                for (int m = 0; m < 16; ++m) {
                    uint32_t ref = refs[m];
                    bool flip = ref & 1;
                    size_t mini = ref >> 1;
                    int mx = (m % 4) * 8, my = (m / 4) * 8;
                    for (int py = 0; py < 8; ++py)
                        for (int px = 0; px < 8; ++px) {
                            uint8_t idx = mini < ts.minitiles.size() ? ts.minitiles[mini][py * 8 + (flip ? 7 - px : px)] : 0;
                            const Rgb& c = ts.palette[idx];
                            uint8_t* d = tile + ((my + py) * 32 + mx + px) * 3;
                            d[0] = c.r, d[1] = c.g, d[2] = c.b;
                        }
                }
            }
            for (int oy = 0; oy < outPerTile; ++oy)
                for (int ox = 0; ox < outPerTile; ++ox) {
                    int sum[3] = {0, 0, 0};
                    for (int sy = 0; sy < downscale; ++sy)
                        for (int sx = 0; sx < downscale; ++sx) {
                            const uint8_t* s = tile + ((oy * downscale + sy) * 32 + ox * downscale + sx) * 3;
                            sum[0] += s[0], sum[1] += s[1], sum[2] += s[2];
                        }
                    uint8_t* d = img.rgba.data() +
                                 ((static_cast<size_t>(ty) * outPerTile + oy) * img.width + tx * outPerTile + ox) * 4;
                    d[0] = static_cast<uint8_t>(sum[0] / area);
                    d[1] = static_cast<uint8_t>(sum[1] / area);
                    d[2] = static_cast<uint8_t>(sum[2] / area);
                    d[3] = 255;
                }
        }
    }
    return img;
}

namespace {
void appendToVector(void* ctx, void* data, int size) {
    auto* out = static_cast<Bytes*>(ctx);
    auto* p = static_cast<uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}
}  // namespace

bool writePng(const char* path, const Image& image) {
    Bytes png;
    if (!stbi_write_png_to_func(appendToVector, &png, image.width, image.height, 4, image.rgba.data(), image.width * 4))
        return false;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace stardraft
