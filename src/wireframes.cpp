// Unit wireframes (roadmap R13): the status panel's pictures of a selected unit, decoded for previews.
// The files and their colors are as the game draws them.

#include "stardraft/wireframes.h"

#include <algorithm>
#include <cstring>

#include "byteio.h"
#include "stardraft/game_data.h"

namespace stardraft {

namespace {

// game\twire.pcx: 8-bit RLE PCX, 24 x 1 pixels, with a 256-color palette at its end.
bool readTwire(const Bytes& pcx, uint8_t (&colors)[Wireframes::kTwireColors], uint8_t (&palette)[768]) {
    if (pcx.size() < 128 + 769 || pcx[pcx.size() - 769] != 12) return false;
    const int w = readU16(pcx, 8) - readU16(pcx, 4) + 1, bytesPerLine = readU16(pcx, 66);
    if (w < Wireframes::kTwireColors || bytesPerLine < w) return false;
    std::vector<uint8_t> row;
    for (size_t p = 128; row.size() < size_t(w) && p < pcx.size() - 769;) {
        const uint8_t c = pcx[p++];
        if ((c & 0xC0) == 0xC0 && p < pcx.size()) row.insert(row.end(), c & 0x3F, pcx[p++]);
        else row.push_back(c);
    }
    if (row.size() < size_t(Wireframes::kTwireColors)) return false;
    std::copy_n(row.begin(), Wireframes::kTwireColors, colors);
    std::memcpy(palette, pcx.data() + pcx.size() - 768, 768);
    return true;
}

void put(uint8_t* o, const uint8_t* rgb, uint8_t a = 255) { o[0] = rgb[0], o[1] = rgb[1], o[2] = rgb[2], o[3] = a; }

}  // namespace

const char* Wireframes::fileName(WireKind kind) {
    return kind == WireKind::Single ? "wirefram" : kind == WireKind::Group ? "grpwire" : "tranwire";
}

bool Wireframes::load(const GameData& data, WireKind kind, bool hd, bool carbot, std::string& error) {
    *this = {};
    hd_ = hd;
    Bytes pcx;
    if (!data.readFile("game\\twire.pcx", pcx, error)) return false;
    if (!readTwire(pcx, twire_, palette_)) {
        error = "game\\twire.pcx: not a 24-color palette strip";
        return false;
    }
    const std::string name = fileName(kind);
    Bytes d;
    if (!hd) {
        const std::string file = "unit\\wirefram\\" + name + ".grp";
        if (!data.readFile(file, d, error)) return false;
        if (!grp_.parse(d) || grp_.frames.empty()) {
            error = file + ": not a GRP";
            return false;
        }
        return true;
    }
    // <name>.dds.grp: u32 file size, u16 frame count, u16 version, then each frame: u32 0, u16 width, u16 height,
    // u32 size and a DDS file of that size.
    const std::string file = std::string("HD2\\") + (carbot ? "Carbot\\" : "") + "unit\\wirefram\\" + name + ".dds.grp";
    if (!data.readFile(file, d, error)) return false;
    if (d.size() < 8) {
        error = file + ": too small";
        return false;
    }
    const int n = readU16(d, 4);
    tex_.resize(size_t(n));
    size_t p = 8;
    for (int i = 0; i < n && p + 12 <= d.size(); ++i) {
        const size_t size = readU32(d, p + 8);
        if (p + 12 + size > d.size()) break;
        tex_[size_t(i)].parse(std::span(d).subspan(p + 12, size));
        p += 12 + size;
    }
    return true;
}

const Sprite* Wireframes::picture(int f, bool shields) {
    if (f < 0 || f >= count()) return nullptr;
    auto& slot = pictures_[f * 2 + (shields ? 1 : 0)];
    if (!slot) slot = std::make_unique<Sprite>(hd_ ? decodeHd(f, shields) : decodeSd(f, shields));
    return slot->width ? slot.get() : nullptr;
}

// SD: palette indices. 208-211 (or 216-219) are the four parts of the unit, each in the color of its health (one
// stray pixel has 215); 192 and 193 are the outlines. The other indices show in the palette's colors.
Sprite Wireframes::decodeSd(int f, bool shields) const {
    Sprite s;
    const Grp::Frame& fr = grp_.frames[size_t(f)];
    if (fr.pixels.empty()) return s;
    s.width = grp_.width, s.height = grp_.height, s.originX = s.width / 2, s.originY = s.height / 2;
    s.rgba.assign(size_t(s.width) * s.height * 4, 0);
    const uint8_t* green = &palette_[twire_[kFullHealth] * 3];
    for (int y = 0; y < fr.height; ++y)
        for (int x = 0; x < fr.width; ++x) {
            const uint8_t v = fr.pixels[size_t(y) * fr.width + x];
            const int px = fr.x + x, py = fr.y + y;
            if (!v || px >= s.width || py >= s.height) continue;
            uint8_t* o = &s.rgba[(size_t(py) * s.width + px) * 4];
            if (v >= 208 && v <= 219) put(o, green);
            else if (shields && (v == 192 || v == 193)) put(o, &palette_[twire_[kFullShields + (v - 192)] * 3]);
            else put(o, &palette_[v * 3]);
        }
    return s;
}

// HD2. The single wireframes are a strip of square cells: cells 0-3 the four parts of the unit (white, to take
// the color of their health), cell 4 the outline that shows shields, cell 5 the dark outline; the cells after
// those are not drawn. The group and transport wireframes are one picture in the SD colors: a pixel nearest to
// palette color 192 or 193 is an outline, the others are parts of the unit.
Sprite Wireframes::decodeHd(int f, bool shields) const {
    Sprite s;
    const DdsTexture& t = tex_[size_t(f)];
    if (t.data.empty() || !t.height) return s;
    std::vector<uint8_t> rgba;
    decodeDds(t, 0, 0, t.width, t.height, rgba);
    const uint8_t* green = &palette_[twire_[kFullHealth] * 3];
    const uint8_t* blue = &palette_[twire_[kFullShields] * 3];
    auto over = [](uint8_t* o, const uint8_t* c, int a) {  // c over o, straight alpha
        if (a <= 0) return;
        const int oa = o[3], na = a + oa * (255 - a) / 255;
        for (int k = 0; k < 3; ++k) o[k] = uint8_t((c[k] * a + o[k] * oa * (255 - a) / 255) / std::max(1, na));
        o[3] = uint8_t(na);
    };
    const int cells = t.width / t.height;
    if (cells >= 6 && t.width % t.height == 0) {
        const int c = t.height;
        s.width = s.height = c, s.originX = s.originY = c / 2;
        s.rgba.assign(size_t(c) * c * 4, 0);
        for (int cell : {5, 4, 0, 1, 2, 3})
            for (int y = 0; y < c; ++y)
                for (int x = 0; x < c; ++x) {
                    const uint8_t* p = &rgba[(size_t(y) * t.width + size_t(cell) * c + x) * 4];
                    if (!p[3]) continue;
                    uint8_t* o = &s.rgba[(size_t(y) * c + x) * 4];
                    const int gray = std::max({p[0], p[1], p[2]});
                    uint8_t col[3] = {p[0], p[1], p[2]};
                    const uint8_t* tint = cell < 4 ? green : cell == 4 && shields ? blue : nullptr;
                    if (tint)
                        for (int k = 0; k < 3; ++k) col[k] = uint8_t(tint[k] * gray / 255);
                    over(o, col, p[3]);
                }
        return s;
    }
    s.width = t.width, s.height = t.height, s.originX = s.width / 2, s.originY = s.height / 2;
    s.rgba = rgba;
    const uint8_t* outline[2] = {&palette_[192 * 3], &palette_[193 * 3]};
    const uint8_t gray[3] = {60, 62, 70};  // the parts' colors are grays near this one
    auto dist = [](const uint8_t* a, const uint8_t* b) {
        int d = 0;
        for (int k = 0; k < 3; ++k) d += (a[k] - b[k]) * (a[k] - b[k]);
        return d;
    };
    for (size_t i = 0; i + 3 < s.rgba.size(); i += 4) {
        uint8_t* o = &s.rgba[i];
        if (!o[3]) continue;
        const int d0 = dist(o, outline[0]), d1 = dist(o, outline[1]), dg = dist(o, gray);
        if (dg <= d0 && dg <= d1) put(o, green, o[3]);
        else if (shields) put(o, blue, o[3]);
    }
    return s;
}

}  // namespace stardraft
