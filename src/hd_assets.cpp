#include "stardraft/hd_assets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "byteio.h"
#include "stardraft/game_data.h"
#include "stardraft/sprites.h"
#include "stardraft/tileset.h"

namespace stardraft {

const char* qualityName(GraphicsQuality q) {
    switch (q) {
    case GraphicsQuality::SD: return "SD";
    case GraphicsQuality::HD2: return "HD2";
    case GraphicsQuality::HD: return "HD";
    }
    return "?";
}

bool DdsTexture::parse(std::span<const uint8_t> d) {
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) return false;
    height = int(readU32(d, 12));
    width = int(readU32(d, 16));
    if (std::memcmp(d.data() + 84, "DXT1", 4) == 0) format = BC1;
    else if (std::memcmp(d.data() + 84, "DXT5", 4) == 0) format = BC3;
    else return false;
    size_t bytes = size_t((width + 3) / 4) * ((height + 3) / 4) * blockBytes();
    if (d.size() < 128 + bytes) return false;
    data.assign(d.begin() + 128, d.begin() + 128 + bytes);
    return true;
}

namespace {

// Images without their own graphics are zero-frame entries in SD\mainSD.anim whose next u16 names
// the image they reuse. Read once per GameData.
int animReference(const GameData& data, int image) {
    static const GameData* cachedFor = nullptr;
    static std::vector<int> refs;
    if (cachedFor != &data) {
        cachedFor = &data;
        refs.assign(999, -1);
        Bytes d;
        std::string err;
        if (data.readFile("SD\\mainSD.anim", d, err) && d.size() >= 332 + 999 * 4)
            for (int i = 0; i < 999; ++i) {
                size_t o = readU32(d, 332 + i * 4);
                if (o + 4 <= d.size() && readU16(d, o) == 0) refs[i] = readU16(d, o + 2);
            }
    }
    return image >= 0 && image < int(refs.size()) ? refs[image] : -1;
}

}  // namespace

bool loadAnimImage(const GameData& data, int image, GraphicsQuality quality, AnimImage& out, std::string& error, bool carbot) {
    for (int hops = 0; hops < 4; ++hops) {
        int ref = animReference(data, image);
        if (ref < 0 || ref == image) break;
        image = ref;
    }
    char name[64];
    const char* hd2 = quality == GraphicsQuality::HD2 ? "HD2\\" : "";
    Bytes d;
    std::snprintf(name, sizeof name, "%sanim\\Carbot\\main_%03d.anim", hd2, image);
    if (!carbot || !data.readFile(name, d, error)) {
        std::snprintf(name, sizeof name, "%sanim\\main_%03d.anim", hd2, image);
        if (!data.readFile(name, d, error)) return false;
    }
    if (d.size() < 332 + 12 || std::memcmp(d.data(), "ANIM", 4) != 0) {
        error = std::string(name) + ": not an ANIM file";
        return false;
    }
    uint16_t layers = readU16(d, 8);
    size_t e = 12 + 320;
    uint16_t frames = readU16(d, e);
    if (!frames) {
        error = std::string(name) + ": entry without frames";
        return false;
    }
    out.boxWidth = readU16(d, e + 4);
    out.boxHeight = readU16(d, e + 6);
    size_t frameTable = readU32(d, e + 8);
    // Layer order: diffuse, bright, teamcolor, emissive, normal, specular, ao_depth.
    auto layer = [&](int i, DdsTexture& tex) {
        if (i >= layers) return false;
        size_t off = readU32(d, e + 12 + 12 * i), size = readU32(d, e + 16 + 12 * i);
        if (!off || off + size > d.size()) return false;
        return tex.parse(std::span(d).subspan(off, size));
    };
    if (!layer(0, out.diffuse)) {
        error = std::string(name) + ": no diffuse layer";
        return false;
    }
    layer(2, out.teamColor);
    out.texScale = quality == GraphicsQuality::HD2 ? 0.5f : 1.0f;
    out.frames.resize(frames);
    if (frameTable + size_t(frames) * 16 > d.size()) {
        error = std::string(name) + ": frame table out of range";
        return false;
    }
    for (uint16_t f = 0; f < frames; ++f) {
        size_t o = frameTable + size_t(f) * 16;
        auto& fr = out.frames[f];
        fr.x = readU16(d, o);
        fr.y = readU16(d, o + 2);
        fr.xOff = int16_t(readU16(d, o + 4));
        fr.yOff = int16_t(readU16(d, o + 6));
        fr.width = readU16(d, o + 8);
        fr.height = readU16(d, o + 10);
    }
    // No frame box declared (the start location): the smallest box the frames sit centered in.
    if (!out.boxWidth || !out.boxHeight)
        out.boxGuessed = true;
    if (out.boxGuessed)
        for (const auto& fr : out.frames)
            out.boxWidth = std::max(out.boxWidth, 2 * fr.xOff + fr.width), out.boxHeight = std::max(out.boxHeight, 2 * fr.yOff + fr.height);
    return true;
}

bool loadHdTiles(const GameData& data, uint16_t era, GraphicsQuality quality, HdTiles& out, std::string& error, bool carbot) {
    const std::string hd2 = quality == GraphicsQuality::HD2 ? "HD2\\" : "";
    std::string name = hd2 + "Carbot\\TileSet\\" + tilesetName(era) + ".dds.vr4";
    Bytes d;
    if (!carbot || !data.readFile(name, d, error)) {
        name = hd2 + "TileSet\\" + tilesetName(era) + ".dds.vr4";
        if (!data.readFile(name, d, error)) return false;
    }
    if (d.size() < 8) {
        error = name + ": too small";
        return false;
    }
    uint16_t count = readU16(d, 4);
    out.tiles.clear();
    out.tiles.reserve(count);
    size_t p = 8;
    for (uint16_t i = 0; i < count && p + 12 <= d.size(); ++i) {
        size_t size = readU32(d, p + 8);
        DdsTexture tex;
        if (p + 12 + size > d.size() || !tex.parse(std::span(d).subspan(p + 12, size))) {
            error = name + ": bad entry " + std::to_string(i);
            return false;
        }
        if (i == 0) out.tileSize = tex.width, out.format = tex.format;
        if (tex.width != out.tileSize || tex.format != out.format) {
            error = name + ": mixed tile sizes/formats";
            return false;
        }
        out.tiles.push_back(std::move(tex.data));
        p += 12 + size;
    }
    return true;
}

bool loadHdGrp(const GameData& data, const std::string& sdPath, const Grp& sd, GraphicsQuality quality, AnimImage& out,
               std::string& error, bool carbot) {
    if (sdPath.size() < 4 || sd.frames.empty()) return error = sdPath + ": no SD frames", false;
    const std::string base = sdPath.substr(0, sdPath.size() - 4) + ".dds.grp";  // ("tileset\badlands.grp")
    const std::string hd2 = quality == GraphicsQuality::HD2 ? "HD2\\" : "";
    std::string name = hd2 + "Carbot\\" + base;
    Bytes d;
    if (!carbot || !data.readFile(name, d, error)) {
        name = hd2 + base;
        if (!data.readFile(name, d, error)) return false;
    }
    // u32 file size, u16 frame count, u16 version, then each frame: u32 0, u16 width, u16 height, u32 size, a DDS file.
    if (d.size() < 8) return error = name + ": too small", false;
    const uint16_t count = readU16(d, 4);
    std::vector<DdsTexture> frames;
    frames.reserve(count);
    for (size_t i = 0, p = 8; i < count; ++i) {
        const size_t size = p + 12 <= d.size() ? readU32(d, p + 8) : 0;
        DdsTexture tex;
        if (!size || p + 12 + size > d.size() || !tex.parse(std::span(d).subspan(p + 12, size)) ||
            (!frames.empty() && tex.format != frames.front().format) || tex.width % 4 || tex.height % 4)
            return error = name + ": bad frame " + std::to_string(i), false;
        frames.push_back(std::move(tex));
        p += 12 + size;
    }
    if (frames.size() < sd.frames.size()) return error = name + ": fewer frames than the SD GRP", false;  // (SD then)
    // One texture: a cell for each frame, in rows; the blocks copied as they are (no decoding).
    int cell = 4;
    for (const DdsTexture& f : frames) cell = std::max({cell, f.width, f.height});
    const int cols = int(std::ceil(std::sqrt(double(frames.size())))), rows = int((frames.size() + size_t(cols) - 1) / size_t(cols));
    DdsTexture& tex = out.diffuse;
    tex.format = frames.front().format;
    tex.width = cols * cell, tex.height = rows * cell;
    const size_t blockBytes = tex.blockBytes(), rowBytes = size_t(tex.width / 4) * blockBytes;
    tex.data.assign(rowBytes * size_t(tex.height / 4), 0);  // (BC3: zero blocks are transparent)
    if (tex.format == DdsTexture::BC1)  // (BC1: transparent blocks, two equal colors and every texel 3)
        for (size_t b = 0; b < tex.data.size(); b += blockBytes) std::memset(&tex.data[b + 4], 0xFF, 4);
    out.texScale = quality == GraphicsQuality::HD2 ? 0.5f : 1.0f;
    const float full = 1.0f / out.texScale;  // full-HD pixels per texel
    out.boxWidth = sd.width * 4, out.boxHeight = sd.height * 4;
    out.boxGuessed = false;
    out.teamColor = {};
    out.frames.clear();
    for (size_t i = 0; i < frames.size(); ++i) {
        const DdsTexture& f = frames[i];
        const int cx = int(i % size_t(cols)) * cell, cy = int(i / size_t(cols)) * cell;
        const size_t frameRow = size_t(f.width / 4) * blockBytes;
        for (int r = 0; r < f.height / 4; ++r)
            std::memcpy(&tex.data[size_t(cy / 4 + r) * rowBytes + size_t(cx / 4) * blockBytes], &f.data[size_t(r) * frameRow], frameRow);
        // A frame has no offset in the file: it sits where the SD frame sits, kept inside the box (an edge at the
        // bottom or the right of a tile is drawn there).
        AnimImage::Frame fr;
        fr.x = uint16_t(cx * full), fr.y = uint16_t(cy * full);
        fr.width = uint16_t(f.width * full), fr.height = uint16_t(f.height * full);
        const Grp::Frame* s = i < sd.frames.size() ? &sd.frames[i] : nullptr;
        fr.xOff = int16_t(std::clamp(s ? s->x * 4 : 0, 0, std::max(0, out.boxWidth - fr.width)));
        fr.yOff = int16_t(std::clamp(s ? s->y * 4 : 0, 0, std::max(0, out.boxHeight - fr.height)));
        out.frames.push_back(fr);
    }
    return true;
}

}  // namespace stardraft
