#include "map_preview.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef __APPLE__
#include <pwd.h>
#include <unistd.h>
#endif

#include "stardraft/map_file.h"
#include "stardraft/strings.h"
#include "stardraft/units.h"
#include "text_codec.h"

namespace fs = std::filesystem;

namespace stardraft::preview {

namespace {

constexpr char kMagic[4] = {'S', 'D', 'T', 'C'};  // StarDraft tile colors
constexpr uint32_t kVersion = 1;
constexpr uint16_t kStartLocation = 214;

// The tilesets' base colors (a tile group is a little lighter or darker), for a map whose tileset has no table yet.
constexpr uint32_t kBase[8] = {0x6E5A3C, 0x30303E, 0x4A4A4E, 0x4E2E22, 0x2E5A26, 0x9C7E4C, 0xA8B4C2, 0x3E3252};

// The minimap's player colors (core/views_minimap.inc), players 1 to 12 (12: neutral, as minerals).
constexpr uint32_t kPlayer[12] = {0xF40404, 0x0C48CC, 0x2CB494, 0x88409C, 0xF88C14, 0x703014,
                                  0xCCE0D0, 0xFCFC38, 0x088008, 0xFCFC7C, 0xECC4B0, 0x4068D4};

uint16_t u16(const Bytes& b, size_t at) { return at + 1 < b.size() ? uint16_t(b[at] | b[at + 1] << 8) : 0; }

void put(Image& img, int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
    uint8_t* p = &img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
    p[0] = uint8_t(rgb >> 16), p[1] = uint8_t(rgb >> 8), p[2] = uint8_t(rgb), p[3] = 255;
}

void fill(Image& img, int x0, int y0, int x1, int y1, uint32_t rgb) {
    for (int y = std::max(0, y0); y < std::min(img.height, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(img.width, x1); ++x) put(img, x, y, rgb);
}

uint32_t shade(uint32_t rgb, int d) {
    auto c = [&](int shift) { return uint32_t(std::clamp(int(rgb >> shift & 0xFF) + d, 0, 255)) << shift; };
    return c(16) | c(8) | c(0);
}

}  // namespace

std::vector<uint32_t> tileColors(const Tileset& ts) {
    std::vector<uint32_t> mega(ts.megatiles.size(), 0);
    for (size_t m = 0; m < ts.megatiles.size(); ++m) {  // (as rebuildMinimapTerrain in core/views_minimap.inc)
        uint32_t sum[3] = {0, 0, 0};
        for (uint32_t ref : ts.megatiles[m]) {
            const size_t mini = ref >> 1;
            if (mini >= ts.minitiles.size()) continue;
            for (uint8_t idx : ts.minitiles[mini]) sum[0] += ts.palette[idx].r, sum[1] += ts.palette[idx].g, sum[2] += ts.palette[idx].b;
        }
        mega[m] = (sum[0] / 1024) << 16 | (sum[1] / 1024) << 8 | (sum[2] / 1024);
    }
    std::vector<uint32_t> out(ts.groups.size() * 16, 0);
    for (size_t v = 0; v < out.size(); ++v) {
        const int m = ts.megatileFor(uint16_t(v));
        out[v] = m >= 0 && size_t(m) < mega.size() ? mega[size_t(m)] : 0;
    }
    return out;
}

fs::path tileColorsFile(const fs::path& dir, uint16_t era) { return dir / ("tile-colors-" + std::to_string(era & 7) + ".bin"); }

bool writeTileColors(const fs::path& file, const std::vector<uint32_t>& colors, std::string& error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path part = file;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return error = "cannot write " + part.string(), false;
        const uint32_t head[2] = {kVersion, uint32_t(colors.size())};
        out.write(kMagic, 4);
        for (uint32_t h : head)
            for (int i = 0; i < 4; ++i) out.put(char(h >> (8 * i)));
        for (uint32_t c : colors) out.put(char(c >> 16)), out.put(char(c >> 8)), out.put(char(c));
        if (!out) return error = "cannot write " + part.string(), false;
    }
    fs::rename(part, file, ec);
    if (ec) return error = "cannot replace " + file.string() + ": " + ec.message(), false;
    return true;
}

bool readTileColors(const fs::path& file, std::vector<uint32_t>& colors) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    char head[12];
    if (!in.read(head, 12) || !std::equal(head, head + 4, kMagic)) return false;
    auto word = [&](int at) { return uint32_t(uint8_t(head[at])) | uint32_t(uint8_t(head[at + 1])) << 8 | uint32_t(uint8_t(head[at + 2])) << 16 | uint32_t(uint8_t(head[at + 3])) << 24; };
    if (word(4) != kVersion) return false;
    const uint32_t count = word(8);
    if (count > 65536) return false;  // (a tile value has 16 bits)
    std::vector<char> raw(size_t(count) * 3);
    if (!in.read(raw.data(), std::streamsize(raw.size()))) return false;
    colors.resize(count);
    for (size_t i = 0; i < count; ++i)
        colors[i] = uint32_t(uint8_t(raw[i * 3])) << 16 | uint32_t(uint8_t(raw[i * 3 + 1])) << 8 | uint8_t(raw[i * 3 + 2]);
    return true;
}

fs::path tileColorsFolder() {
#ifdef __APPLE__
    // (in a sandbox HOME is the container's folder: the user's own home is the password database's)
    const passwd* pw = getpwuid(getuid());
    const char* home = pw && pw->pw_dir ? pw->pw_dir : std::getenv("HOME");
    if (!home || !*home) return {};
    return fs::path(home) / "Library" / "Application Support" / "StarDraft" / "quicklook";
#else
    return {};
#endif
}

std::string plainText(const std::string& gameText) {
    std::string kept;
    kept.reserve(gameText.size());
    for (char ch : gameText) {
        const unsigned char b = static_cast<unsigned char>(ch);
        if (b == '\r') continue;
        if (b < 0x20 && b != '\n' && b != '\t') continue;  // the game's color and format codes
        if (b == 0x7F) continue;
        kept.push_back(ch);
    }
    std::string out = text::narrow(text::widenLegacy(kept));
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    size_t start = 0;
    while (start < out.size() && (out[start] == '\n' || out[start] == ' ')) ++start;
    return out.substr(start);
}

MapInfo readInfo(const Chk& chk) {
    MapInfo info;
    uint16_t w = 0, h = 0;
    readDimensions(chk, w, h);
    info.width = w, info.height = h;
    info.era = readEra(chk) & 7;
    const StringTable strings = StringTable::read(chk);
    if (const Section* s = chk.find("SPRP"); s && s->data.size() >= 4) {
        const uint16_t name = u16(s->data, 0), desc = u16(s->data, 2);
        if (name) info.name = plainText(strings.get(name));
        if (desc) info.description = plainText(strings.get(desc));
    }
    if (const Section* s = chk.find("OWNR"))
        for (size_t i = 0; i < 8 && i < s->data.size(); ++i)
            if (s->data[i] == 5 || s->data[i] == 6) ++info.players;  // (5 computer, 6 human)
    for (const Unit& u : readUnits(chk))
        if (u.type == kStartLocation) ++info.startLocations;
    return info;
}

Image render(const Chk& chk, const std::vector<uint32_t>* colors, int maxSide) {
    Image img;
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h) || !w || !h) return img;
    const int scale = std::max(1, maxSide / std::max<int>(w, h));
    img.width = w * scale, img.height = h * scale;
    img.rgba.assign(size_t(img.width) * size_t(img.height) * 4, 0);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    const TerrainLayer t = readTerrain(chk);
    const uint32_t base = kBase[readEra(chk) & 7];
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) {
            const size_t i = size_t(ty) * w + size_t(tx);
            const uint16_t v = i < t.tiles.size() ? t.tiles[i] : 0;
            uint32_t c;
            if (colors && !colors->empty()) c = v < colors->size() ? (*colors)[v] : 0;
            else c = shade(base, int((uint32_t(v >> 4) * 2654435761u) >> 28) * 3 - 22);  // (-22 to +23, the same for a group)
            fill(img, tx * scale, ty * scale, (tx + 1) * scale, (ty + 1) * scale, c);
        }
    // The units as dots (about a tile and a half), the start locations as frames of their box (4 x 3 tiles).
    const float px = float(scale) / 32.0f;  // map pixels -> picture pixels
    const int dot = std::clamp(scale * 3 / 2, 2, 10);
    std::vector<Unit> units = readUnits(chk);
    for (const Unit& u : units) {
        if (u.type == kStartLocation) continue;
        const int x = int(u.x * px), y = int(u.y * px);
        fill(img, x - dot / 2, y - dot / 2, x - dot / 2 + dot, y - dot / 2 + dot, kPlayer[std::min<int>(u.owner, 11)]);
    }
    const int line = std::max(1, scale / 3);
    for (const Unit& u : units) {
        if (u.type != kStartLocation) continue;
        const uint32_t c = kPlayer[std::min<int>(u.owner, 11)];
        const int x0 = int((u.x - 64) * px), y0 = int((u.y - 48) * px), x1 = std::max(x0 + 3, int((u.x + 64) * px)),
                  y1 = std::max(y0 + 3, int((u.y + 48) * px));
        fill(img, x0, y0, x1, y0 + line, c), fill(img, x0, y1 - line, x1, y1, c);
        fill(img, x0, y0, x0 + line, y1, c), fill(img, x1 - line, y0, x1, y1, c);
    }
    return img;
}

bool renderFile(const fs::path& map, const fs::path& colorsDir, int maxSide, Image& out, MapInfo& info, bool* usedTable,
                std::string& error) {
    Bytes bytes;
    if (!readMapChk(map, bytes, error)) return false;
    const Chk chk = Chk::parse(bytes);
    info = readInfo(chk);
    if (!info.width || !info.height) return error = "the map has no size (DIM)", false;
    std::vector<uint32_t> colors;
    const bool table = !colorsDir.empty() && readTileColors(tileColorsFile(colorsDir, info.era), colors);
    if (usedTable) *usedTable = table;
    out = render(chk, table ? &colors : nullptr, maxSide);
    return !out.rgba.empty() || (error = "the map has no picture", false);
}

}  // namespace stardraft::preview
