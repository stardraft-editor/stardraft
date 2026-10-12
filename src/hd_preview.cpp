// CPU decoding of HD art for previews drawn without the GPU (the editor's unit palette).

#include <algorithm>
#include <cstring>
#include <map>

#include "byteio.h"
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/sprites.h"

namespace stardraft {

namespace {

void rgb565(uint16_t c, uint8_t* o) {
    o[0] = uint8_t((c >> 11) * 255 / 31), o[1] = uint8_t(((c >> 5) & 63) * 255 / 63), o[2] = uint8_t((c & 31) * 255 / 31);
}

// One 4x4 block to 16 RGBA pixels.
void decodeBlock(const DdsTexture& t, const uint8_t* b, uint8_t px[16][4]) {
    uint8_t alpha[16];
    std::fill(std::begin(alpha), std::end(alpha), uint8_t(255));
    if (t.format == DdsTexture::BC3) {
        uint8_t a[8] = {b[0], b[1]};
        if (a[0] > a[1]) {
            for (int i = 1; i < 7; ++i) a[1 + i] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
        } else {
            for (int i = 1; i < 5; ++i) a[1 + i] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5);
            a[6] = 0, a[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) alpha[i] = a[(bits >> (3 * i)) & 7];
        b += 8;
    }
    const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    uint8_t c[4][4] = {};
    rgb565(c0, c[0]), rgb565(c1, c[1]);
    c[0][3] = c[1][3] = c[2][3] = c[3][3] = 255;
    if (c0 > c1 || t.format == DdsTexture::BC3) {
        for (int k = 0; k < 3; ++k) c[2][k] = uint8_t((2 * c[0][k] + c[1][k]) / 3), c[3][k] = uint8_t((c[0][k] + 2 * c[1][k]) / 3);
    } else {
        for (int k = 0; k < 3; ++k) c[2][k] = uint8_t((c[0][k] + c[1][k]) / 2);
        c[3][3] = 0;
    }
    const uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const uint8_t* s = c[(idx >> (2 * i)) & 3];
        px[i][0] = s[0], px[i][1] = s[1], px[i][2] = s[2], px[i][3] = uint8_t(s[3] * alpha[i] / 255);
    }
}

}  // namespace

void decodeDds(const DdsTexture& t, int x0, int y0, int w, int h, std::vector<uint8_t>& out) {
    out.assign(size_t(std::max(0, w)) * size_t(std::max(0, h)) * 4, 0);
    const int bw = (t.width + 3) / 4;
    uint8_t px[16][4];
    for (int by = std::max(0, y0) / 4; by * 4 < std::min(t.height, y0 + h); ++by)
        for (int bx = std::max(0, x0) / 4; bx * 4 < std::min(t.width, x0 + w); ++bx) {
            const size_t off = (size_t(by) * bw + bx) * t.blockBytes();
            if (off + t.blockBytes() > t.data.size()) continue;
            decodeBlock(t, t.data.data() + off, px);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + i % 4 - x0, y = by * 4 + i / 4 - y0;
                if (x >= 0 && y >= 0 && x < w && y < h) std::memcpy(&out[(size_t(y) * w + x) * 4], px[i], 4);
            }
        }
}

bool loadHdCommandIcons(const GameData& data, bool carbot, std::vector<Sprite>& out, std::string& error) {
    // cmdicons.dds.grp: u32 file size, u16 frame count, u16 version, then per frame u32 0, u16 width,
    // u16 height, u32 size and a DDS file.
    const std::string name = std::string("HD2\\") + (carbot ? "Carbot\\" : "") + "unit\\cmdicons\\cmdicons.dds.grp";
    Bytes d;
    if (!data.readFile(name, d, error)) return false;
    if (d.size() < 8) {
        error = name + ": too small";
        return false;
    }
    const int count = readU16(d, 4);  // every icon: the units first, then weapons, upgrades, tech and orders
    std::vector<DdsTexture> tex(size_t(std::max(0, count)));
    size_t p = 8;
    for (int i = 0; i < count && p + 12 <= d.size(); ++i) {
        const size_t size = readU32(d, p + 8);
        if (p + 12 + size > d.size()) break;
        tex[size_t(i)].parse(std::span(d).subspan(p + 12, size));
        p += 12 + size;
    }
    // Units without an icon share one placeholder ("BLANK"): the frame repeated most often.
    std::map<Bytes, int> seen;
    int most = 0;
    for (const auto& t : tex) most = std::max(most, ++seen[t.data]);
    out.assign(tex.size(), {});
    for (size_t i = 0; i < tex.size(); ++i) {
        const DdsTexture& t = tex[i];
        if (t.data.empty() || (most > 1 && seen[t.data] == most)) continue;
        Sprite& s = out[i];
        s.width = t.width, s.height = t.height, s.originX = t.width / 2, s.originY = t.height / 2;
        decodeDds(t, 0, 0, t.width, t.height, s.rgba);
    }
    return true;
}

bool hdUnitPicture(const GameData& data, const UnitSprites& sprites, uint16_t unitType, uint32_t teamRgb, bool carbot, Sprite& out,
                   std::string& error) {
    struct Layer {
        AnimImage a;
        int frame = 0;
        bool flip = false;
    };
    std::vector<Layer> layers;
    const int baseImage = sprites.imageFor(unitType);
    for (int image : {baseImage, sprites.subunitImage(unitType)}) {
        if (image < 0) continue;
        Layer l;
        if (!loadAnimImage(data, image, GraphicsQuality::HD2, l.a, error, carbot) || l.a.frames.empty()) {
            if (layers.empty()) return false;
            continue;
        }
        l.frame = sprites.defaultFrame(unitType, image, l.flip);
        if (l.frame < 0 || l.frame >= int(l.a.frames.size())) l.frame = 0;
        layers.push_back(std::move(l));
    }
    if (layers.empty()) {
        error = "no graphics";
        return false;
    }
    // Canvas: the largest frame box, in texture pixels; every image is centered on it, as in the game.
    int W = 0, H = 0;
    for (const auto& l : layers)
        W = std::max(W, int(l.a.boxWidth * l.a.texScale)), H = std::max(H, int(l.a.boxHeight * l.a.texScale));
    out = {};
    out.width = W, out.height = H, out.originX = W / 2, out.originY = H / 2;
    out.rgba.assign(size_t(W) * H * 4, 0);
    const float tr = float((teamRgb >> 16) & 255), tg = float((teamRgb >> 8) & 255), tb = float(teamRgb & 255);
    std::vector<uint8_t> diff, mask;
    for (const auto& l : layers) {
        const float ts = l.a.texScale;
        const auto& f = l.a.frames[size_t(l.frame)];
        const int fx = int(f.x * ts), fy = int(f.y * ts), fw = int(f.width * ts), fh = int(f.height * ts);
        decodeDds(l.a.diffuse, fx, fy, fw, fh, diff);
        if (!l.a.teamColor.data.empty()) decodeDds(l.a.teamColor, fx, fy, fw, fh, mask);
        else mask.assign(diff.size(), 0);
        // A turret sits at the base's special-overlay offset for the base's frame (map pixels; 4 full-HD px each).
        int offX = 0, offY = 0;
        if (&l != &layers.front()) {
            const Layer& b = layers.front();
            auto [tx, ty] = sprites.specialOffset(baseImage, b.frame);
            offX = int((b.flip ? -tx : tx) * 4 * ts), offY = int(ty * 4 * ts);
        }
        const int boxW = int(l.a.boxWidth * ts), left = W / 2 - boxW / 2 + offX, top = H / 2 - int(l.a.boxHeight * ts) / 2 + offY;
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                const uint8_t* c = &diff[(size_t(y) * fw + x) * 4];
                if (c[3] < 102) continue;  // the renderer's alpha test (0.4)
                int bx = int(f.xOff * ts) + x;
                if (l.flip) bx = boxW - 1 - bx;
                const int ox = left + bx, oy = top + int(f.yOff * ts) + y;
                if (ox < 0 || oy < 0 || ox >= W || oy >= H) continue;
                // Team color as the sprite shader does it: the masked (grayscale) art times the player color.
                const float m = mask[(size_t(y) * fw + x) * 4] / 255.0f;
                uint8_t* o = &out.rgba[(size_t(oy) * W + ox) * 4];
                o[0] = uint8_t(c[0] + (c[0] * tr / 255.0f - c[0]) * m), o[1] = uint8_t(c[1] + (c[1] * tg / 255.0f - c[1]) * m);
                o[2] = uint8_t(c[2] + (c[2] * tb / 255.0f - c[2]) * m), o[3] = 255;
            }
    }
    return true;
}

}  // namespace stardraft
