#include "canvas_imgui.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <tuple>

#include "theme.h"

namespace stardraft::kit_sdl {

using draw::Color;
using draw::Font;
using draw::FontDesc;
using draw::FontMetrics;
using draw::kNone;
using draw::LineStyle;
using draw::Point;
using draw::Rect;

namespace fs = std::filesystem;

namespace {

// The fonts made so far: a canvas font is a pointer into this list (a deque keeps the addresses).
ImFont* gUi = nullptr;
ImFont* gMono = nullptr;
ImFont* gGlyphs = nullptr;  // the icon glyph face (Segoe MDL2 Assets on Windows); the UI face when there is none
int gUiEmPx = 0;            // setUiFontHeight: the UI text's character height at scale 1 (0: the kit's own size)
float gUiSizeFactor = 1;    // the ImGui size of a UI face font for each of its pixels (loadFonts, from gUiEmPx)
std::deque<FontEntry> gFonts;
std::map<std::tuple<int, int, bool>, size_t> gFontIndex;

struct Atlas {
    int size = 0, w = 0, h = 0;
    SDL_GPUTexture* tex = nullptr;
};
std::vector<Atlas> gAtlases;
SDL_GPUDevice* gAtlasDevice = nullptr;

// The program's own pictures (loadResourceIcon): {id, 0} keeps the source pixels; {id, px} a texture made at px.
struct ResourceIcon {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;  // (the source only)
    SDL_GPUTexture* tex = nullptr;    // (the sized ones only)
};
std::map<std::pair<int, int>, ResourceIcon> gResourceIcons;

// The source made smaller (or larger) to px x px: each pixel the average of the source pixels under it, weighted by
// their alpha (premultiplied), so the edges keep their color.
std::vector<unsigned char> resampled(const ResourceIcon& src, int px) {
    std::vector<unsigned char> out(size_t(px) * size_t(px) * 4);
    const double sx = double(src.w) / px, sy = double(src.h) / px;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
            double acc[4] = {0, 0, 0, 0}, area = 0;
            for (int v = int(y0); v < int(std::ceil(y1)) && v < src.h; ++v)
                for (int u = int(x0); u < int(std::ceil(x1)) && u < src.w; ++u) {
                    const double wgt = (std::min(x1, u + 1.0) - std::max(x0, double(u))) * (std::min(y1, v + 1.0) - std::max(y0, double(v)));
                    if (wgt <= 0) continue;
                    const unsigned char* p = src.rgba.data() + (size_t(v) * size_t(src.w) + size_t(u)) * 4;
                    const double a = p[3] / 255.0;
                    acc[0] += p[0] * a * wgt, acc[1] += p[1] * a * wgt, acc[2] += p[2] * a * wgt, acc[3] += a * wgt;
                    area += wgt;
                }
            unsigned char* o = out.data() + (size_t(y) * size_t(px) + size_t(x)) * 4;
            for (int k = 0; k < 3; ++k) o[k] = acc[3] > 0 ? (unsigned char)std::lround(std::clamp(acc[k] / acc[3], 0.0, 255.0)) : 0;
            o[3] = (unsigned char)std::lround((area > 0 ? acc[3] / area : 0) * 255);
        }
    return out;
}

// A bitmap's texture: its copy of the pixels, and the revision it is of.
// Every live one is in liveBitmaps(), so their textures can go before the device does (a view that lives until the program
// ends, such as the New page, keeps its bitmaps after the device: releaseBitmapTextures, from unloadIconAtlases).
struct GpuBitmap;
// (never destroyed: a global view in another file may free its bitmaps after this file's globals are gone)
std::set<GpuBitmap*>& liveBitmaps() {
    static auto* s = new std::set<GpuBitmap*>;
    return *s;
}
struct GpuBitmap final : draw::Bitmap::Native {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTexture* tex = nullptr;
    int w = 0, h = 0;
    GpuBitmap() { liveBitmaps().insert(this); }
    ~GpuBitmap() override {
        liveBitmaps().erase(this);
        if (tex && device) SDL_ReleaseGPUTexture(device, tex);
    }
};
// The textures of all bitmaps go (each bitmap uploads its pixels again if it is drawn later).
void releaseBitmapTextures() {
    for (GpuBitmap* g : liveBitmaps()) {
        if (g->tex && g->device) SDL_ReleaseGPUTexture(g->device, g->tex);
        g->tex = nullptr, g->device = nullptr, g->w = 0, g->h = 0;
    }
}

ImU32 colorOf(Color c, unsigned alpha = 255) { return IM_COL32(int(c & 255), int((c >> 8) & 255), int((c >> 16) & 255), int(alpha)); }

ImTextureRef texRef(const void* tex) { return ImTextureRef(ImTextureID(reinterpret_cast<uintptr_t>(tex))); }

uint32_t decodeCp(const std::string& s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (n == 1) return c;
    uint32_t cp = n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    return cp;
}

bool isCjk(uint32_t c) {
    return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF);
}

// The text's width in pixels at the font's size.
float textWidth(const FontEntry& f, std::string_view s) {
    if (s.empty()) return 0;
    return f.face->CalcTextSizeA(float(f.px), FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x;
}

float lineHeight(const FontEntry& f) { return f.face->CalcTextSizeA(float(f.px), FLT_MAX, 0.0f, "Ag").y; }

// The lines a text breaks into to fit `width` (0: no limit). Breaks come between words, after a CJK character, and
// inside a word that is wider than the box; a '\n' always breaks.
std::vector<std::string> wrapLines(const FontEntry& f, const std::string& text, float width) {
    std::vector<std::string> out;
    auto trimEnd = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    auto fits = [&](const std::string& s) { return width <= 0 || textWidth(f, trimEnd(s)) <= width; };
    size_t start = 0;
    while (true) {
        const size_t nl = text.find('\n', start);
        const std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // Units: runs of spaces, words, single CJK characters.
        enum class Kind { None, Space, Word, Cjk };
        std::vector<std::string> units;
        Kind last = Kind::None;
        for (size_t i = 0; i < para.size();) {
            const size_t j = utf8Next(para, i);
            const uint32_t cp = decodeCp(para, i);
            const Kind kind = cp == ' ' ? Kind::Space : isCjk(cp) ? Kind::Cjk : Kind::Word;
            // A run of spaces or of word characters is one unit; each CJK character is one.
            if (kind == last && kind != Kind::Cjk) units.back() += para.substr(i, j - i);
            else units.push_back(para.substr(i, j - i));
            last = kind;
            i = j;
        }
        std::string cur;
        bool broke = false;  // a line ended in this paragraph: the spaces that follow it are dropped
        for (const std::string& u : units) {
            if (u[0] == ' ') {  // a run of spaces: kept inside a line and at the paragraph's start, as GDI keeps them
                if (!cur.empty() || !broke) cur += u;
                continue;
            }
            const std::string cand = cur + u;
            if (fits(cand)) {
                cur = cand;
                continue;
            }
            if (!cur.empty()) out.push_back(trimEnd(cur)), cur.clear(), broke = true;
            // The unit alone is wider than the box: break it between characters.
            for (size_t i = 0; i < u.size();) {
                const size_t j = utf8Next(u, i);
                const std::string ch = u.substr(i, j - i);
                if (!cur.empty() && !fits(cur + ch)) out.push_back(trimEnd(cur)), cur.clear(), broke = true;
                cur += ch;
                i = j;
            }
        }
        out.push_back(trimEnd(cur));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// The text cut with "..." to fit `width`: at the end, or (path) in the middle, keeping the start and the end.
std::string ellipsize(const FontEntry& f, const std::string& s, float width, bool path) {
    if (textWidth(f, s) <= width) return s;
    std::vector<size_t> cuts;  // byte index of each character, and the end
    for (size_t i = 0; i < s.size(); i = utf8Next(s, i)) cuts.push_back(i);
    cuts.push_back(s.size());
    const size_t n = cuts.size() - 1;
    const std::string dots = "...";
    for (size_t kept = n; kept-- > 0;) {
        const size_t head = path ? (kept + 1) / 2 : kept;
        const size_t tail = path ? kept / 2 : 0;
        const std::string cand = s.substr(0, cuts[head]) + dots + s.substr(cuts[n - tail]);
        if (textWidth(f, cand) <= width) return cand;
    }
    return dots;
}

// A line or outline through `pts`, solid or dashed (dash: 6 on, 4 off in the width; dot: 1 on, 2 off).
void strokePath(ImDrawList* dl, std::vector<ImVec2> pts, bool closed, ImU32 col, float w, LineStyle style) {
    if (pts.size() < 2) return;
    if (closed) pts.push_back(pts[0]);
    if (style == LineStyle::Solid) {
        dl->AddPolyline(pts.data(), int(pts.size()), col, w, 0);
        return;
    }
    const float on = style == LineStyle::Dash ? 6 * w : w;
    const float off = style == LineStyle::Dash ? 4 * w : 2 * w;
    bool drawing = true;
    float left = on;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const ImVec2 a = pts[i], b = pts[i + 1];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len <= 0) continue;
        const ImVec2 d((b.x - a.x) / len, (b.y - a.y) / len);
        float pos = 0;
        while (pos < len) {
            const float step = std::min(left, len - pos);
            if (drawing) dl->AddLine(ImVec2(a.x + d.x * pos, a.y + d.y * pos), ImVec2(a.x + d.x * (pos + step), a.y + d.y * (pos + step)), col, w);
            pos += step;
            left -= step;
            if (left <= 0) drawing = !drawing, left = drawing ? on : off;
        }
    }
}

// Triangles that cover a simple polygon (ear clipping). A polygon that cannot be cut is fanned from its first point.
std::vector<std::array<int, 3>> triangulate(const std::vector<ImVec2>& p) {
    const int n = int(p.size());
    double area = 0;
    for (int i = 0; i < n; ++i) area += double(p[size_t(i)].x) * p[size_t((i + 1) % n)].y - double(p[size_t((i + 1) % n)].x) * p[size_t(i)].y;
    const double sign = area >= 0 ? 1 : -1;
    auto cross = [&](int a, int b, int c) {
        return double(p[size_t(b)].x - p[size_t(a)].x) * (p[size_t(c)].y - p[size_t(a)].y) -
               double(p[size_t(b)].y - p[size_t(a)].y) * (p[size_t(c)].x - p[size_t(a)].x);
    };
    auto inside = [&](int q, int a, int b, int c) {
        const double d1 = cross(a, b, q), d2 = cross(b, c, q), d3 = cross(c, a, q);
        const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        return !(neg && pos);
    };
    std::vector<int> idx;  // (the vertices still in the polygon)
    idx.reserve(size_t(n));
    for (int i = 0; i < n; ++i) idx.push_back(i);
    std::vector<std::array<int, 3>> tris;
    for (int guard = 0; idx.size() > 3 && guard < n * n; ++guard) {
        bool found = false;
        const size_t m = idx.size();
        for (size_t i = 0; i < m && !found; ++i) {
            const int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
            if (cross(a, b, c) * sign <= 0) continue;  // reflex or flat: not an ear
            bool blocked = false;
            for (int q : idx)
                if (q != a && q != b && q != c && inside(q, a, b, c)) {
                    blocked = true;
                    break;
                }
            if (blocked) continue;
            tris.push_back({a, b, c});
            idx.erase(idx.begin() + std::ptrdiff_t(i));
            found = true;
        }
        if (!found) break;
    }
    for (size_t i = 1; idx.size() >= 3 && i + 1 < idx.size(); ++i) tris.push_back({idx[0], idx[i], idx[i + 1]});
    return tris;
}

// A text drawn at (x, y) (its top), with the font's underline.
void drawString(ImDrawList* dl, const FontEntry& f, float x, float y, ImU32 col, const std::string& s) {
    dl->AddText(f.face, float(f.px), ImVec2(x, y), col, s.data(), s.data() + s.size());
    if (f.underline) {
        const float uy = y + lineHeight(f) - 1;
        dl->AddLine(ImVec2(x, uy), ImVec2(x + textWidth(f, s), uy), col, 1);
    }
}

// The bitmap's pixels as RGBA with straight alpha (the Bitmap is premultiplied).
std::vector<uint8_t> straightPixels(const draw::Bitmap& b) {
    const size_t n = size_t(b.width()) * size_t(b.height());
    std::vector<uint8_t> out(n * 4);
    const uint32_t* in = b.pixels();
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = in[i];  // in memory: R, G, B, A
        const uint32_t a = p >> 24;
        const uint32_t ch[3] = {p & 255, (p >> 8) & 255, (p >> 16) & 255};
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = a ? std::min<uint32_t>(255, (ch[k] * 255 + a / 2) / a) : 0;
            out[i * 4 + size_t(k)] = uint8_t(v);
        }
        out[i * 4 + 3] = uint8_t(a);
    }
    return out;
}

EmbeddedFile gEmbedded = nullptr;  // setEmbeddedFiles

bool isFile(const std::string& utf8) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(reinterpret_cast<const char8_t*>(utf8.c_str())), ec);
}

// A file built into the program for a path that has no file: {nullptr, 0} when there is none.
std::pair<const void*, size_t> embeddedFile(const std::string& utf8) {
    if (!gEmbedded || isFile(utf8)) return {nullptr, 0};
    const auto e = gEmbedded(utf8);
    return e.first && e.second ? e : std::pair<const void*, size_t>{nullptr, 0};
}

std::vector<unsigned char> readFile(const std::string& utf8) {
    if (const auto e = embeddedFile(utf8); e.first) {
        const auto* b = static_cast<const unsigned char*>(e.first);
        return std::vector<unsigned char>(b, b + e.second);
    }
    std::ifstream in(fs::path(reinterpret_cast<const char8_t*>(utf8.c_str())), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The first of the files that is there (UTF-8 paths), as a file or built into the program.
std::string firstFile(std::initializer_list<std::string> paths) {
    for (const std::string& p : paths)
        if (isFile(p) || embeddedFile(p).first) return p;
    return {};
}

#if !defined(_WIN32) && !defined(__APPLE__)
// The first line that a command writes ("" when it does not run).
std::string commandOutput(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    char buf[1024];
    std::string out;
    if (std::fgets(buf, sizeof buf, p)) out = buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// A font that stb_truetype (ImGui's rasterizer) reads: TrueType outlines, or CFF ones (OTTO) that are not CFF2 (the
// variable CFF fonts, as Cantarell's newer files): the first font of a collection is looked at.
bool rasterizerReads(const std::string& path) {
    std::ifstream in(fs::path(reinterpret_cast<const char8_t*>(path.c_str())), std::ios::binary);
    unsigned char head[12] = {};
    if (!in.read(reinterpret_cast<char*>(head), 12)) return false;
    auto u32 = [](const unsigned char* b) { return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]; };
    uint32_t at = 0;
    if (std::memcmp(head, "ttcf", 4) == 0) {
        unsigned char off[4] = {};  // (the first font's offset, after the tag, the version and the count)
        in.seekg(12);
        if (!in.read(reinterpret_cast<char*>(off), 4)) return false;
        at = u32(off);
        in.seekg(at);
        if (!in.read(reinterpret_cast<char*>(head), 12)) return false;
    }
    const uint32_t tag = u32(head);
    if (tag != 0x00010000 && std::memcmp(head, "true", 4) != 0 && std::memcmp(head, "OTTO", 4) != 0) return false;
    const int tables = head[4] << 8 | head[5];
    for (int i = 0; i < tables; ++i) {
        unsigned char rec[16];
        if (!in.read(reinterpret_cast<char*>(rec), 16)) return false;
        if (std::memcmp(rec, "CFF2", 4) == 0) return false;
    }
    return true;
}

// The desktop's UI font as a file (fontconfig, fc-match): KDE's font (kdeglobals: [General] font=), GNOME's and the
// desktops' that use its settings (gsettings: org.gnome.desktop.interface font-name), else fontconfig's sans-serif. ""
// when there is no fontconfig or no font that the rasterizer reads.
std::string desktopUiFont() {
    std::string family;
    const char* desk = std::getenv("XDG_CURRENT_DESKTOP");
    const std::string desktop = desk ? desk : "";
    if (desktop.find("KDE") != std::string::npos) {
        const char* cfg = std::getenv("XDG_CONFIG_HOME");
        const char* home = std::getenv("HOME");
        const fs::path file = cfg && *cfg == '/' ? fs::path(cfg) / "kdeglobals" : home ? fs::path(home) / ".config" / "kdeglobals" : fs::path();
        std::ifstream in(file);
        bool general = false;
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line[0] == '[') general = line.rfind("[General]", 0) == 0;
            else if (general && line.rfind("font=", 0) == 0) family = line.substr(5, line.find(',') - 5);  // "Noto Sans,10,-1,5,50,..."
        }
    } else if (!desktop.empty()) {
        std::string f = commandOutput("gsettings get org.gnome.desktop.interface font-name 2>/dev/null");  // "'Cantarell 11'"
        if (f.size() >= 2 && f.front() == '\'' && f.back() == '\'') f = f.substr(1, f.size() - 2);
        if (const size_t sp = f.find_last_of(' '); sp != std::string::npos && f.find_first_not_of("0123456789.", sp + 1) == std::string::npos)
            f.resize(sp);  // (the size)
        family = f;
    }
    // (only letters, digits, spaces and - _ go to the shell)
    if (family.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_") != std::string::npos) family.clear();
    for (const std::string& pattern : {family, std::string("sans-serif")}) {
        if (pattern.empty()) continue;
        const std::string file = commandOutput("fc-match -f '%{file}' '" + pattern + ":style=Regular' 2>/dev/null");
        if (!file.empty() && isFile(file) && rasterizerReads(file)) return file;
    }
    return {};
}
#endif

// Emoji and the other characters past U+FFFF (two UTF-16 units) are the fonts' glyphs, not the replacement mark.
static_assert(sizeof(ImWchar) == 4, "ImGui needs IMGUI_USE_WCHAR32 (CMakeLists.txt) for the characters past U+FFFF");

// A font from a file, or from the program's own bytes (not copied: ImGui only reads them).
ImFont* addFont(const std::string& path, float size, const ImFontConfig* cfg = nullptr, const ImWchar* ranges = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    if (const auto e = embeddedFile(path); e.first) {
        ImFontConfig c = cfg ? *cfg : ImFontConfig();
        c.FontDataOwnedByAtlas = false;
        return io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(e.first), int(e.second), size, &c, ranges);
    }
    return io.Fonts->AddFontFromFileTTF(path.c_str(), size, cfg, ranges);
}

std::string fileName(const std::string& path) { return fs::path(reinterpret_cast<const char8_t*>(path.c_str())).filename().string(); }

const FontEntry* fontEntry(const draw::FontDesc& d) {
    const int face = d.face == draw::Face::Mono ? 1 : d.face == draw::Face::Glyphs ? 2 : 0;
    ImFont* base = face == 1 && gMono ? gMono : face == 2 && gGlyphs ? gGlyphs : gUi;
    int px = d.px > 0 ? d.px : kit::px(15);
    if (face == 0 && gUiSizeFactor != 1) px = std::max(1, int(std::lround(float(px) * gUiSizeFactor)));
    const auto key = std::make_tuple(face, px, d.underline);
    if (auto it = gFontIndex.find(key); it != gFontIndex.end()) return &gFonts[it->second];
    gFonts.push_back(FontEntry{base, px, d.underline});
    gFontIndex[key] = gFonts.size() - 1;
    return &gFonts.back();
}

TextTranslator gTranslator = nullptr;  // setTextTranslator
GlyphIcons gGlyphIcons = nullptr;      // setGlyphIcons

// The UI icon that stands for the text `t` (one glyph of the program's table), or -1.
int glyphIconOf(std::wstring_view t) { return gGlyphIcons && t.size() == 1 ? gGlyphIcons(t[0]) : -1; }

// The atlas that a glyph of `px` pixels is drawn with: the smallest that is as large (else the largest); null: none.
// Only the atlases of 32 px and less (the text's glyph icons keep their sizes; the large atlases are for Canvas::icon).
const Atlas* glyphAtlas(int px) {
    const Atlas* best = nullptr;
    for (const Atlas& a : gAtlases)
        if (a.size <= 32 && a.size >= px && (!best || a.size < best->size)) best = &a;
    if (!best)
        for (const Atlas& a : gAtlases)
            if (a.size <= 32 && (!best || a.size > best->size)) best = &a;
    return best;
}

// The size that Canvas::icon draws an icon of `px` pixels at: up to 32 px, the Win32 editor's (its uiIconSize: 32 from
// 32 px, 24 from 28, else 16); larger, px itself.
int iconDrawnPx(int px) { return px >= 33 ? px : px >= 32 ? 32 : px >= 28 ? 24 : 16; }
// The atlas that an icon drawn at `want` pixels comes from: the smallest that has the icon and is as large, else the
// largest that has it (an icon newer than the large atlases); null: none.
const Atlas* iconAtlas(int icon, int want) {
    const Atlas* a = nullptr;
    for (const Atlas& cand : gAtlases)
        if (icon < cand.w / cand.size && cand.size >= want && (!a || cand.size < a->size)) a = &cand;
    if (!a)
        for (const Atlas& cand : gAtlases)
            if (icon < cand.w / cand.size && (!a || cand.size > a->size)) a = &cand;
    return a;
}

// A private use character (an icon font's glyph) that the font does not have: not drawn (no box).
bool missingGlyph(const FontEntry& f, std::wstring_view t) {
    for (wchar_t ch : t)
        if (ch >= 0xE000 && ch <= 0xF8FF && !f.face->IsGlyphInFont(ImWchar(ch))) return true;
    return false;
}

}  // namespace

void setTextTranslator(TextTranslator translator) { gTranslator = translator; }
void setGlyphIcons(GlyphIcons icons) { gGlyphIcons = icons; }

std::wstring_view uiText(std::wstring_view t, std::wstring& buf) {
    if (!gTranslator || t.empty() || !gTranslator(t, buf)) return t;
    return buf;
}

std::string utf8Text(std::wstring_view w) {
    std::string o;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = uint32_t(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) {
            const uint32_t d = uint32_t(w[i + 1]);
            if (d >= 0xDC00 && d < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00), ++i;
        }
        // A control code (StarCraft's text colors, 01-1F, other than a tab or a line end) is not drawn and has no
        // width, as GDI's DrawTextW does (the font has no glyph for it: a box).
        if (c < 0x20 && c != L'\t' && c != L'\n' && c != L'\r') continue;
        if (c < 0x80) o += char(c);
        else if (c < 0x800) o += char(0xC0 | (c >> 6)), o += char(0x80 | (c & 63));
        else if (c < 0x10000) o += char(0xE0 | (c >> 12)), o += char(0x80 | ((c >> 6) & 63)), o += char(0x80 | (c & 63));
        else o += char(0xF0 | (c >> 18)), o += char(0x80 | ((c >> 12) & 63)), o += char(0x80 | ((c >> 6) & 63)), o += char(0x80 | (c & 63));
    }
    return o;
}

size_t utf8Next(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    return std::min(s.size(), i + n);
}

void setEmbeddedFiles(EmbeddedFile files) { gEmbedded = files; }

void setUiFontHeight(int emPx) { gUiEmPx = emPx; }

namespace {

// The height that ImGui gives a font of size 1 (its ascent minus its descent, from the 'hhea' table) in em units: the
// size at which ImGui draws the font with characters of a GDI font's height (a LOGFONT's negative lfHeight) is that
// height times this. 0 when the file cannot be read. `face`: the font in a collection (.ttc).
float ascentDescentPerEm(const std::string& path, int face = 0) {
    const std::vector<unsigned char> b = readFile(path);
    auto u16 = [&](size_t o) { return o + 2 <= b.size() ? unsigned(b[o] << 8 | b[o + 1]) : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= b.size() ? uint32_t(u16(o)) << 16 | u16(o + 2) : 0u; };
    size_t dir = 0;
    if (u32(0) == 0x74746366u) dir = u32(12 + 4 * size_t(face));  // 'ttcf': each face's table directory
    unsigned upem = 0;
    int ascent = 0, descent = 0;
    for (unsigned i = 0, n = u16(dir + 4); i < n; ++i) {
        const size_t rec = dir + 12 + 16 * size_t(i);
        const uint32_t tag = u32(rec), off = u32(rec + 8);
        if (tag == 0x68656164u) upem = u16(off + 18);  // 'head': unitsPerEm
        if (tag == 0x68686561u) ascent = int16_t(u16(off + 4)), descent = int16_t(u16(off + 6));  // 'hhea'
    }
    return upem && ascent > descent ? float(ascent - descent) / float(upem) : 0.0f;
}

#if defined(_WIN32)
// The system's font folder (%WINDIR%\Fonts\), as UTF-8.
std::string windowsFonts() {
    const wchar_t* w = _wgetenv(L"WINDIR");
    const std::u8string u = (fs::path(w && *w ? w : L"C:\\Windows") / L"Fonts").u8string();
    return std::string(u.begin(), u.end()) + "\\";
}
#endif

}  // namespace

std::string loadFonts(const std::string& fontDir) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    gFonts.clear(), gFontIndex.clear();
    const std::string d = fontDir;
    // The program's own fonts first (Noto Sans, KR, SC: the build puts them beside the program), so every system shows
    // the same text; else the system's. (Windows: the other way round, below.)
    const std::string ownLatin = firstFile({d + "NotoSans-Regular.ttf"});
    const std::string ownKorean = firstFile({d + "NotoSansKR-Regular.otf"});
    const std::string ownChinese = firstFile({d + "NotoSansSC-Regular.otf"});
#if defined(_WIN32)
    // Windows: the system's fonts, as the Win32 program had them (Segoe UI, Malgun Gothic for Korean, Microsoft YaHei for
    // Chinese, Cascadia Mono or Consolas for code); the program's own Noto Sans only when a system font is not there.
    const std::string w = windowsFonts();
    std::string latin = firstFile({w + "segoeui.ttf", ownLatin});
    std::string korean = firstFile({w + "malgun.ttf", ownKorean});
    std::string chinese = firstFile({w + "msyh.ttc", w + "simsun.ttc", ownChinese});
    const std::string mono = firstFile({w + "CascadiaMono.ttf", w + "consola.ttf"});
#elif defined(__APPLE__)
    std::string latin = firstFile({"/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc"});
    std::string korean = firstFile({"/System/Library/Fonts/AppleSDGothicNeo.ttc", "/System/Library/Fonts/Supplemental/AppleGothic.ttf"});
    std::string chinese = firstFile({"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Medium.ttc"});
    const std::string mono = firstFile({"/System/Library/Fonts/SFNSMono.ttf", "/System/Library/Fonts/Menlo.ttc"});
#else
    std::string latin = firstFile({"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"});
    std::string korean = firstFile({"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                    "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"});
    std::string chinese = korean;  // (Noto Sans CJK has both)
    const std::string mono = firstFile({"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"});
#endif
#if !defined(_WIN32)
    if (!ownLatin.empty()) latin = ownLatin;
    if (!ownKorean.empty()) korean = ownKorean;
    if (!ownChinese.empty()) chinese = ownChinese;
#endif
    // macOS: the system's UI font (SF Pro, SFNS.ttf) for the UI text, as the system's own programs; the program's Noto
    // Sans is merged after it for the characters that SF Pro does not have, then Korean and Chinese as on the others.
    std::string fill;  // (a font merged after the UI font, before Korean)
#if defined(__APPLE__)
    if (const std::string sf = firstFile({"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/SFNSText.ttf"}); !sf.empty())
        fill = latin, latin = sf;
#elif !defined(_WIN32)
    // Linux and the BSDs: the desktop's UI font (fontconfig), as the desktop's own programs; Noto Sans after it, as above.
    // STARDRAFT_FONT=noto keeps Noto Sans (the same text on every computer).
    const char* only = std::getenv("STARDRAFT_FONT");
    if (!(only && std::strcmp(only, "noto") == 0))
        if (const std::string ui = desktopUiFont(); !ui.empty() && fileName(ui) != fileName(latin)) fill = latin, latin = ui;
#endif

    const float size = 15.0f;  // the size the font file is read at; each canvas font is drawn at its own size
    std::string loaded;
    ImFontConfig cfg;
#if defined(__APPLE__)
    // As macOS draws text: no hinting (stb_truetype has none), each glyph at its sub-pixel place (three positions in a
    // pixel, no snap to whole pixels), and the strokes a little heavier, as macOS's text gamma makes them.
    cfg.OversampleH = 3, cfg.PixelSnapH = false, cfg.RasterizerMultiply = 1.15f;
    const std::string rasterizer = "; rasterizer: macOS (no hinting, sub-pixel places, x1.15)";
#else
    const std::string rasterizer;
#endif
    if (!latin.empty() && (gUi = addFont(latin, size, &cfg))) loaded = fileName(latin);
    else if (!fill.empty() && (gUi = addFont(fill, size, &cfg))) loaded = fileName(fill), latin = fill;  // (the UI font did not load)
    else gUi = io.Fonts->AddFontDefault(), loaded = "ImGui's own (no system font found)";
    if (!fill.empty() && fill != latin) {
        ImFontConfig merge = cfg;
        merge.MergeMode = true;
        if (addFont(fill, size, &merge)) loaded += " + " + fileName(fill);
    }
    // The symbols that the text fonts do not have (a game text's line end mark U+21B5, the pencil U+270E): the
    // system's symbol font, as Windows's font linking gives them to GDI (else they show as boxes).
#if defined(_WIN32)
    std::string symbols = firstFile({w + "seguisym.ttf"});
#elif defined(__APPLE__)
    std::string symbols = firstFile({"/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"});
#else
    std::string symbols = firstFile({"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf"});
#endif
#if defined(_WIN32)
    const std::string emoji = firstFile({w + "seguiemj.ttf"});  // (Segoe UI Emoji: its glyphs in one color)
#else
    const std::string emoji;
#endif
    for (const std::string* f : std::initializer_list<const std::string*>{&korean, &chinese, &symbols, &emoji}) {
        if (f->empty() || (f == &chinese && *f == korean) || (f == &symbols && *f == latin)) continue;
        ImFontConfig merge = cfg;
        merge.MergeMode = true;
        if (addFont(*f, size, &merge)) loaded += " + " + fileName(*f);
    }
    gMono = mono.empty() ? nullptr : addFont(mono, size, &cfg);
    if (gMono) {
        loaded += "; code: " + fileName(mono);
        // Korean and Chinese in code (a script's comments and texts), as the Windows RichEdit's font linking gives them.
        for (const std::string* f : {&korean, &chinese}) {
            if (f->empty() || (f == &chinese && *f == korean)) continue;
            ImFontConfig merge = cfg;
            merge.MergeMode = true;
            addFont(*f, size, &merge);
        }
    } else gMono = gUi;
    // The icon glyphs (the message box's info icon is U+E946 in Segoe MDL2 Assets): only their range is loaded.
    static const ImWchar kGlyphRanges[] = {0xE000, 0xF8FF, 0};
#if defined(_WIN32)
    const std::string glyphs = firstFile({w + "segmdl2.ttf"});
#else
    const std::string glyphs;
#endif
    gGlyphs = glyphs.empty() ? nullptr : addFont(glyphs, size, nullptr, kGlyphRanges);
    if (gGlyphs) loaded += "; glyphs: " + fileName(glyphs);
    else gGlyphs = gUi;
    // The UI text at the system's size (setUiFontHeight): the kit's UI size (15 px) draws characters of that height.
    gUiSizeFactor = 1;
    if (gUiEmPx > 0 && !latin.empty())
        if (const float r = ascentDescentPerEm(latin); r > 0) gUiSizeFactor = float(gUiEmPx) * r / 15.0f;
    // Only a small change: the row heights (kRowH, kMenuRowH, kFormRowH) change with the DPI only, and a larger Windows
    // text size (Settings > Accessibility > Text size, which also makes the message font larger) would make the text
    // overflow its rows. The default message font (12 px) gives about 1.06.
    gUiSizeFactor = std::clamp(gUiSizeFactor, 0.9f, 1.1f);
    if (gUiSizeFactor != 1) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "; text size x%.3f", double(gUiSizeFactor));
        loaded += buf;
    }
    return loaded + rasterizer;
}

SDL_GPUTexture* uploadRgba(SDL_GPUDevice* device, int w, int h, const uint8_t* rgba) {
    if (!device || w <= 0 || h <= 0 || !rgba) return nullptr;
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = uint32_t(w), ti.height = uint32_t(h), ti.layer_count_or_depth = 1, ti.num_levels = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
    if (!tex) return nullptr;
    const size_t bytes = size_t(w) * size_t(h) * 4;
    SDL_GPUTransferBufferCreateInfo tb{};
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = uint32_t(bytes);
    SDL_GPUTransferBuffer* buf = SDL_CreateGPUTransferBuffer(device, &tb);
    if (!buf) {
        SDL_ReleaseGPUTexture(device, tex);
        return nullptr;
    }
    void* p = SDL_MapGPUTransferBuffer(device, buf, false);
    std::memcpy(p, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(device, buf);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = buf;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex, dst.w = uint32_t(w), dst.h = uint32_t(h), dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    SDL_ReleaseGPUTransferBuffer(device, buf);
    return tex;
}

bool loadIconAtlases(SDL_GPUDevice* device, const std::string& dir) {
    unloadIconAtlases();
    gAtlasDevice = device;
    for (int size : {16, 20, 24, 32, 40, 48, 64}) {
        const std::vector<unsigned char> file = readFile(dir + "atlas-" + std::to_string(size) + ".png");
        if (file.empty()) continue;
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4);
        if (!px) continue;
        SDL_GPUTexture* tex = uploadRgba(device, w, h, px);
        stbi_image_free(px);
        if (tex) gAtlases.push_back(Atlas{size, w, h, tex});
    }
    return !gAtlases.empty();
}

void unloadIconAtlases() {
    releaseBitmapTextures();  // (the bitmaps' textures too: the device goes next)
    for (const Atlas& a : gAtlases)
        if (gAtlasDevice && a.tex) SDL_ReleaseGPUTexture(gAtlasDevice, a.tex);
    gAtlases.clear();
    for (auto& [key, r] : gResourceIcons)
        if (gAtlasDevice && r.tex) SDL_ReleaseGPUTexture(gAtlasDevice, r.tex);
    gResourceIcons.clear();
}

bool loadResourceIcon(int id, const std::string& file) {
    const std::vector<unsigned char> f = readFile(file);
    // An .ico: its largest image that is a PNG (a 0 in the size byte is 256); else the file is a PNG.
    const unsigned char* png = f.data();
    size_t pngSize = f.size();
    if (f.size() >= 6 && f[0] == 0 && f[1] == 0 && f[2] == 1 && f[3] == 0) {
        const size_t n = size_t(f[4] | (f[5] << 8));
        int best = 0;
        pngSize = 0;
        for (size_t k = 0; k < n && 6 + 16 * (k + 1) <= f.size(); ++k) {
            const unsigned char* e = f.data() + 6 + 16 * k;
            const int size = e[0] ? e[0] : 256;
            const size_t len = size_t(e[8]) | size_t(e[9]) << 8 | size_t(e[10]) << 16 | size_t(e[11]) << 24;
            const size_t off = size_t(e[12]) | size_t(e[13]) << 8 | size_t(e[14]) << 16 | size_t(e[15]) << 24;
            if (off + len > f.size() || len < 8 || f[off] != 0x89 || f[off + 1] != 'P') continue;
            if (size > best) best = size, png = f.data() + off, pngSize = len;
        }
    }
    if (!pngSize) return false;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(png, int(pngSize), &w, &h, &n, 4);
    if (!px) return false;
    ResourceIcon& r = gResourceIcons[{id, 0}];
    r.w = w, r.h = h;
    r.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return true;
}

bool setWindowIcon(SDL_Window* window, int id) {
    const auto it = gResourceIcons.find({id, 0});
    if (!window || it == gResourceIcons.end() || it->second.rgba.empty()) return false;
    ResourceIcon& r = it->second;
    SDL_Surface* s = SDL_CreateSurfaceFrom(r.w, r.h, SDL_PIXELFORMAT_RGBA32, r.rgba.data(), r.w * 4);
    if (!s) return false;
    const bool ok = SDL_SetWindowIcon(window, s);  // (SDL copies the pixels)
    SDL_DestroySurface(s);
    return ok;
}

draw::Canvas& measuringCanvas() {
    // Never destroyed: a draw list takes itself out of the ImGui context's shared data when it goes, and at exit that
    // context is gone before the statics (the program crashed at exit on Windows after a message box or a tooltip).
    static ImDrawList* list = new ImDrawList(ImGui::GetDrawListSharedData());
    static ImguiCanvas* canvas = new ImguiCanvas(*list, nullptr);
    return *canvas;
}

ImguiCanvas::ImguiCanvas(ImDrawList& list, SDL_GPUDevice* device) : dl_(&list), dev_(device), cur_(fontEntry(FontDesc{})) {}

void ImguiCanvas::fill(Rect r, Color c, uint8_t alpha) {
    if (c == kNone || r.empty() || !alpha) return;
    dl_->AddRectFilled(at(float(r.left), float(r.top)), at(float(r.right), float(r.bottom)), colorOf(c, alpha));
}

void ImguiCanvas::roundBox(Rect r, Color fill, Color line, float radius, int width) {
    if (r.empty()) return;
    if (fill != kNone) dl_->AddRectFilled(at(float(r.left), float(r.top)), at(float(r.right), float(r.bottom)), colorOf(fill), radius);
    if (line != kNone) {
        const float h = float(std::max(1, width)) / 2;
        dl_->AddRect(at(r.left + h, r.top + h), at(r.right - h, r.bottom - h), colorOf(line), radius, 0, float(std::max(1, width)));
    }
}

void ImguiCanvas::smoothRoundOutline(Rect r, float radius, Color line, float width) {
    if (line == kNone || r.empty()) return;
    const float h = std::max(1.0f, width) / 2;
    dl_->AddRect(at(r.left + h, r.top + h), at(r.right - h, r.bottom - h), colorOf(line), radius, 0, std::max(1.0f, width));
}

void ImguiCanvas::smoothLine(PointF a, PointF b, Color c, float width, uint8_t alpha) {
    if (c == kNone) return;
    dl_->AddLine(at(a.x, a.y), at(b.x, b.y), colorOf(c, alpha), width);
}

void ImguiCanvas::smoothFrame(PointF p, float w, float h, Color c, float width, uint8_t alpha) {
    if (c == kNone) return;
    dl_->AddRect(at(p.x, p.y), at(p.x + w, p.y + h), colorOf(c, alpha), 0, 0, width);
}

void ImguiCanvas::smoothArc(PointF p, float w, float h, float startDeg, float sweepDeg, Color c, float width) {
    if (c == kNone || w <= 0 || h <= 0) return;
    const float k = 3.14159265f / 180.0f;
    const ImVec2 mid = at(p.x + w / 2, p.y + h / 2);
    if (w == h) {
        dl_->PathArcTo(mid, w / 2, startDeg * k, (startDeg + sweepDeg) * k, 0);
    } else {
        const int n = std::max(8, int(std::fabs(sweepDeg) / 6));
        for (int i = 0; i <= n; ++i) {
            const float a = (startDeg + sweepDeg * float(i) / float(n)) * k;
            dl_->PathLineTo(ImVec2(mid.x + w / 2 * std::cos(a), mid.y + h / 2 * std::sin(a)));
        }
    }
    dl_->PathStroke(colorOf(c), 0, width);
}

void ImguiCanvas::smoothPolygon(const PointF* pts, int n, Color fill) {
    if (fill == kNone || n < 3) return;
    std::vector<ImVec2> ps;
    for (int i = 0; i < n; ++i) ps.push_back(at(pts[i].x, pts[i].y));
    dl_->AddConvexPolyFilled(ps.data(), n, colorOf(fill));
}

void ImguiCanvas::smoothEllipse(PointF p, float w, float h, Color fill) {
    if (fill == kNone) return;
    dl_->AddEllipseFilled(at(p.x + w / 2, p.y + h / 2), ImVec2(w / 2, h / 2), colorOf(fill), 0, 0);
}

void ImguiCanvas::ellipse(Rect r, Color fill, Color line) {
    if (r.empty()) return;
    // As GDI's Ellipse: `r` includes the outline, which is 1 px wide inside it.
    const ImVec2 c = at(r.left + r.width() / 2.0f, r.top + r.height() / 2.0f);
    const ImVec2 rad(r.width() / 2.0f - 0.5f, r.height() / 2.0f - 0.5f);
    if (fill != kNone) dl_->AddEllipseFilled(c, rad, colorOf(fill), 0, 0);
    if (line != kNone) dl_->AddEllipse(c, rad, colorOf(line), 0, 0, 1.0f);
}

void ImguiCanvas::ring(Rect r, Color line, int width, LineStyle style) {
    if (line == kNone || r.empty()) return;
    const float w = float(std::max(1, width));
    const float cx = origin_.x + r.left + r.width() / 2.0f, cy = origin_.y + r.top + r.height() / 2.0f;
    const float rx = r.width() / 2.0f - w / 2, ry = r.height() / 2.0f - w / 2;
    if (rx <= 0 || ry <= 0) return;
    if (style == LineStyle::Solid) {
        dl_->AddEllipse(ImVec2(cx, cy), ImVec2(rx, ry), colorOf(line), 0, 0, w);
        return;
    }
    std::vector<ImVec2> pts;
    for (int i = 0; i < 96; ++i) {
        const float a = 6.28318530718f * float(i) / 96;
        pts.push_back(ImVec2(cx + rx * std::cos(a), cy + ry * std::sin(a)));
    }
    strokePath(dl_, pts, true, colorOf(line), w, style);
}

void ImguiCanvas::frame(Rect r, Color line, int width, LineStyle style) {
    if (line == kNone || r.empty()) return;
    const float w = float(std::max(1, width)), h = w / 2;
    const float l = r.left + h, t = r.top + h, rr = r.right - h, b = r.bottom - h;
    if (style == LineStyle::Solid) {
        dl_->AddRect(at(l, t), at(rr, b), colorOf(line), 0, 0, w);
        return;
    }
    strokePath(dl_, {at(l, t), at(rr, t), at(rr, b), at(l, b)}, true, colorOf(line), w, style);
}

void ImguiCanvas::line(int x0, int y0, int x1, int y1, Color c, int width, LineStyle style) {
    const Point pts[] = {{x0, y0}, {x1, y1}};
    polyline(pts, 2, c, width, style);
}

void ImguiCanvas::polyline(const Point* pts, int n, Color c, int width, LineStyle style) {
    if (c == kNone || n < 2) return;
    const int w = std::max(1, width);
    const float o = (w & 1) ? 0.5f : 0.0f;  // odd widths: on the pixel's middle
    std::vector<ImVec2> v;
    v.reserve(size_t(n));
    for (int i = 0; i < n; ++i) v.push_back(at(float(pts[i].x) + o, float(pts[i].y) + o));
    strokePath(dl_, v, false, colorOf(c), float(w), style);
}

void ImguiCanvas::polygon(const Point* pts, int n, Color fill, Color line) {
    if (n < 3 || (fill == kNone && line == kNone)) return;
    std::vector<ImVec2> v;
    v.reserve(size_t(n));
    for (int i = 0; i < n; ++i) v.push_back(at(float(pts[i].x), float(pts[i].y)));
    if (fill != kNone)
        for (const auto& t : triangulate(v)) dl_->AddTriangleFilled(v[size_t(t[0])], v[size_t(t[1])], v[size_t(t[2])], colorOf(fill));
    if (line != kNone) dl_->AddPolyline(v.data(), n, colorOf(line), 1.0f, ImDrawFlags_Closed);
}

void ImguiCanvas::pushClip(Rect r) {
    dl_->PushClipRect(at(float(r.left), float(r.top)), at(float(r.right), float(r.bottom)), true);
    stack_.push_back(Saved{origin_});
}

void ImguiCanvas::popClip() {
    if (stack_.empty()) return;
    dl_->PopClipRect();
    origin_ = stack_.back().origin;
    stack_.pop_back();
}

void ImguiCanvas::pushView(Rect r) {
    dl_->PushClipRect(at(float(r.left), float(r.top)), at(float(r.right), float(r.bottom)), true);
    stack_.push_back(Saved{origin_});
    origin_ = at(float(r.left), float(r.top));
}

void ImguiCanvas::popView() { popClip(); }

// kPrefix, kPrefixHidden: the text without its "&" marks ("&&" is one "&"), and the index of the access key in it (the
// letter after a single "&"; npos: none).
std::wstring withoutPrefix(std::wstring_view t, size_t* key) {
    std::wstring o;
    if (key) *key = std::wstring::npos;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'&' && i + 1 < t.size()) {
            ++i;
            if (t[i] != L'&' && key && *key == std::wstring::npos) *key = o.size();
        }
        o += t[i];
    }
    return o;
}

void ImguiCanvas::text(Rect r, std::wstring_view t, Color c, unsigned flags) {
    if (flags & (draw::kPrefix | draw::kPrefixHidden)) {
        size_t key = std::wstring::npos;
        const std::wstring plain = withoutPrefix(t, &key);
        text(r, plain, c, flags & ~(draw::kPrefix | draw::kPrefixHidden));
        // kPrefix: the access key underlined (one line of text only), on the baseline as GDI draws it.
        if ((flags & draw::kPrefix) && key != std::wstring::npos && !(flags & (draw::kWrap | draw::kEllipsis | draw::kPathEllipsis))) {
            const FontEntry& f = *cur_;
            const float lh = lineHeight(f), w = textWidth(f, utf8Text(plain));
            float x = float(r.left);
            if (flags & draw::kCenter) x += (float(r.width()) - w) / 2;
            else if (flags & draw::kRight) x += float(r.width()) - w;
            float y = float(r.top);
            if (flags & draw::kMiddle) y += (float(r.height()) - lh) / 2;
            else if (flags & draw::kBottom) y += float(r.height()) - lh;
            const float x0 = x + textWidth(f, utf8Text(std::wstring_view(plain).substr(0, key)));
            const float x1 = x + textWidth(f, utf8Text(std::wstring_view(plain).substr(0, key + 1)));
            const ImFontBaked* baked = f.face->GetFontBaked(float(f.px));
            const float base = std::floor(y + (baked ? baked->Ascent : float(f.px)) + 1);  // (one pixel under the baseline)
            dl_->AddRectFilled(at(std::floor(x0), base), at(std::ceil(x1), base + 1), colorOf(c));
        }
        return;
    }
    if (c == kNone || r.empty()) return;
    if (const int gi = glyphIconOf(t); gi >= 0) {  // a glyph that has a UI icon: the icon, placed as the glyph
        const Atlas* a = glyphAtlas(cur_->px);
        if (!a || gi >= a->w / a->size) return;
        const float n = float(a->size);
        float x = float(r.left), y = float(r.top);
        if (flags & draw::kCenter) x += (float(r.width()) - n) / 2;
        else if (flags & draw::kRight) x += float(r.width()) - n;
        if (flags & draw::kMiddle) y += (float(r.height()) - n) / 2;
        else if (flags & draw::kBottom) y += float(r.height()) - n;
        const ImVec2 p0 = at(std::floor(x), std::floor(y));
        const float u0 = float(gi * a->size) / float(a->w), u1 = float((gi + 1) * a->size) / float(a->w);
        dl_->AddImage(texRef(a->tex), p0, ImVec2(p0.x + n, p0.y + n), ImVec2(u0, 0), ImVec2(u1, 1), colorOf(c));
        return;
    }
    if (missingGlyph(*cur_, t)) return;
    std::wstring tb;  // (the translation, uiText)
    const std::string s = utf8Text(uiText(t, tb));
    if (s.empty()) return;
    const FontEntry& f = *cur_;
    const float lh = lineHeight(f);
    const ImU32 col = colorOf(c);
    // The x of a line `w` wide in the box (kCenter, kRight; else at the left).
    auto alignX = [&](float w) {
        float x = float(r.left);
        if (flags & draw::kCenter) x += (float(r.width()) - w) / 2;
        else if (flags & draw::kRight) x += float(r.width()) - w;
        return origin_.x + x;
    };
    dl_->PushClipRect(at(float(r.left), float(r.top)), at(float(r.right), float(r.bottom)), true);
    if (flags & draw::kWrap) {
        const std::vector<std::string> lines = wrapLines(f, s, float(r.width()));
        const float block = lh * float(lines.size());
        float y = origin_.y + float(r.top);
        if (flags & draw::kMiddle) y += (float(r.height()) - block) / 2;
        else if (flags & draw::kBottom) y += float(r.height()) - block;
        for (const std::string& line : lines) {
            drawString(dl_, f, alignX(textWidth(f, line)), y, col, line);
            y += lh;
        }
    } else {
        const bool cut = flags & (draw::kEllipsis | draw::kPathEllipsis);
        const std::string line = cut ? ellipsize(f, s, float(r.width()), (flags & draw::kPathEllipsis) != 0) : s;
        float y = origin_.y + float(r.top);
        if (flags & draw::kMiddle) y += (float(r.height()) - lh) / 2;
        else if (flags & draw::kBottom) y += float(r.height()) - lh;
        drawString(dl_, f, alignX(textWidth(f, line)), y, col, line);
    }
    dl_->PopClipRect();
}

int ImguiCanvas::measure(std::wstring_view t) {
    if (glyphIconOf(t) >= 0)
        if (const Atlas* a = glyphAtlas(cur_->px)) return a->size;
    std::wstring tb;
    return int(std::ceil(textWidth(*cur_, utf8Text(uiText(t, tb)))));
}

void ImguiCanvas::textAt(Point at, std::wstring_view t, Color c) {
    if (t.empty() || c == kNone) return;
    const FontEntry& f = *cur_;
    const ImU32 col = colorOf(c);
    float x = origin_.x + float(at.x);
    const float y = origin_.y + float(at.y);
    for (size_t i = 0; i < t.size();) {
        const size_t n = (t[i] >= 0xD800 && t[i] < 0xDC00 && i + 1 < t.size()) ? 2 : 1;  // (a surrogate pair is one character)
        const std::string ch = utf8Text(t.substr(i, n));
        drawString(dl_, f, x, y, col, ch);
        x += std::ceil(textWidth(f, ch));
        i += n;
    }
}

int ImguiCanvas::measureAt(std::wstring_view t) {
    float w = 0;
    for (size_t i = 0; i < t.size();) {
        const size_t n = (t[i] >= 0xD800 && t[i] < 0xDC00 && i + 1 < t.size()) ? 2 : 1;
        w += std::ceil(textWidth(*cur_, utf8Text(t.substr(i, n))));
        i += n;
    }
    return int(w);
}

Point ImguiCanvas::measureBox(std::wstring_view t, int width, unsigned flags) {
    if (flags & (draw::kPrefix | draw::kPrefixHidden)) return measureBox(withoutPrefix(t, nullptr), width, flags & ~(draw::kPrefix | draw::kPrefixHidden));
    if (glyphIconOf(t) >= 0)
        if (const Atlas* a = glyphAtlas(cur_->px)) return {a->size, std::max(a->size, int(std::ceil(lineHeight(*cur_))))};
    std::wstring tb;
    const std::string s = utf8Text(uiText(t, tb));
    const float lh = lineHeight(*cur_);
    if (!(flags & draw::kWrap)) return {int(std::ceil(textWidth(*cur_, s))), int(std::ceil(lh))};
    const std::vector<std::string> lines = wrapLines(*cur_, s, float(width));
    float widest = 0;
    for (const std::string& line : lines) widest = std::max(widest, textWidth(*cur_, line));
    return {int(std::ceil(widest)), int(std::ceil(lh * float(lines.size())))};
}

FontMetrics ImguiCanvas::metrics() {
    const float lh = lineHeight(*cur_);
    return {int(std::ceil(lh)), int(std::ceil(lh * 0.2f))};
}

Font ImguiCanvas::setFont(Font f) {
    const Font old{cur_};
    if (f.handle) cur_ = static_cast<const FontEntry*>(f.handle);
    return old;
}

Font ImguiCanvas::font(const FontDesc& d) { return Font{fontEntry(d)}; }

// Copies new pixels into a texture of the same size. No wait: the GPU runs the copy after the frames already
// submitted, and SDL keeps the transfer buffer until the copy is done.
static void updateRgba(SDL_GPUDevice* device, SDL_GPUTexture* tex, int w, int h, const uint8_t* rgba) {
    const size_t bytes = size_t(w) * size_t(h) * 4;
    SDL_GPUTransferBufferCreateInfo tb{};
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = uint32_t(bytes);
    SDL_GPUTransferBuffer* buf = SDL_CreateGPUTransferBuffer(device, &tb);
    if (!buf) return;
    void* p = SDL_MapGPUTransferBuffer(device, buf, false);
    std::memcpy(p, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(device, buf);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = buf;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex, dst.w = uint32_t(w), dst.h = uint32_t(h), dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, buf);
}

// The bitmap's texture, made or brought up to date (null when it cannot be made).
static GpuBitmap* gpuBitmapOf(SDL_GPUDevice* dev, draw::Bitmap& b) {
    auto* g = dynamic_cast<GpuBitmap*>(b.native.get());
    if (!g) {
        auto ng = std::make_unique<GpuBitmap>();
        ng->device = dev;
        g = ng.get();
        b.native = std::move(ng);
    }
    if (!g->tex || g->w != b.width() || g->h != b.height()) {
        if (g->tex) SDL_ReleaseGPUTexture(dev, g->tex);
        const std::vector<uint8_t> px = straightPixels(b);
        g->device = dev;  // (again after releaseBitmapTextures)
        g->tex = uploadRgba(dev, b.width(), b.height(), px.data());
        g->w = b.width(), g->h = b.height(), g->revision = b.revision();
        if (!g->tex) return nullptr;
    } else if (g->revision != b.revision()) {
        const std::vector<uint8_t> px = straightPixels(b);
        updateRgba(dev, g->tex, b.width(), b.height(), px.data());  // (same size: the texture is reused)
        g->revision = b.revision();
    }
    return g;
}

void bitmapTextureStats(int& count, size_t& bytes) {
    count = 0, bytes = 0;
    for (const GpuBitmap* g : liveBitmaps())
        if (g->tex) ++count, bytes += size_t(g->w) * size_t(g->h) * 4;
}

draw::Image imageOfBitmap(SDL_GPUDevice* device, draw::Bitmap& b) {
    if (!device || b.width() <= 0 || b.height() <= 0) return {};
    GpuBitmap* g = gpuBitmapOf(device, b);
    return g ? draw::Image{g->tex, b.width(), b.height()} : draw::Image{};
}

void ImguiCanvas::bitmap(draw::Bitmap& b, Rect src, Rect dst, bool, uint8_t alpha) {
    if (!dev_ || b.width() <= 0 || b.height() <= 0 || dst.empty() || alpha == 0) return;
    GpuBitmap* g = gpuBitmapOf(dev_, b);
    if (!g) return;
    // The part of the bitmap that is src (inside the bitmap).
    const float W = float(b.width()), H = float(b.height());
    const float sl = float(std::clamp(src.left, 0, b.width())), sr = float(std::clamp(src.right, 0, b.width()));
    const float st = float(std::clamp(src.top, 0, b.height())), sb = float(std::clamp(src.bottom, 0, b.height()));
    if (sr <= sl || sb <= st) return;
    dl_->AddImage(texRef(g->tex), at(float(dst.left), float(dst.top)), at(float(dst.right), float(dst.bottom)), ImVec2(sl / W, st / H),
                  ImVec2(sr / W, sb / H), colorOf(draw::rgb(255, 255, 255), alpha));
}

void ImguiCanvas::icon(Rect r, int icon, Color c, int px) {
    if (c == kNone || icon < 0 || gAtlases.empty()) return;
    // Up to 32 px, the atlas of the size the Win32 editor draws at px (its uiIconSize): 32 from 32 px, 24 from 28, else
    // 16; each at its own size (a 16 px icon at scale 2 is the 32 px atlas: sharp on a Retina display). Larger (a card's
    // icon at scale 2, 64 px): the smallest atlas that is as large (40, 48, 64), drawn at px; a larger px, the largest.
    // An icon that an atlas does not have (it is newer than the atlas): the largest atlas that has it, at that size.
    const int want = iconDrawnPx(px);
    const Atlas* a = iconAtlas(icon, want);
    if (!a) return;
    const int cell = a->size;
    const float drawn = float(want > 32 ? want : std::min(cell, want));  // (an atlas that has no larger cell: scaled up)
    const float cx = origin_.x + r.left + r.width() / 2.0f, cy = origin_.y + r.top + r.height() / 2.0f;
    const ImVec2 p0(std::floor(cx - drawn / 2.0f), std::floor(cy - drawn / 2.0f));
    const ImVec2 p1(p0.x + drawn, p0.y + drawn);
    const float u0 = float(icon * cell) / float(a->w), u1 = float((icon + 1) * cell) / float(a->w);
    dl_->AddImage(texRef(a->tex), p0, p1, ImVec2(u0, 0), ImVec2(u1, 1), colorOf(c));
}

int iconAtlasCell(int icon, int px) {
    const Atlas* a = icon >= 0 ? iconAtlas(icon, iconDrawnPx(px)) : nullptr;
    return a ? a->size : 0;
}

void ImguiCanvas::resourceIcon(Rect r, int resourceId, int px) {
    if (!dev_ || px <= 0) return;
    const auto src = gResourceIcons.find({resourceId, 0});
    if (src == gResourceIcons.end()) return;
    ResourceIcon& sized = gResourceIcons[{resourceId, px}];  // (a map: the source's reference stays good)
    if (!sized.tex) {
        if (!gAtlasDevice) gAtlasDevice = dev_;
        const std::vector<unsigned char> pixels = resampled(src->second, px);
        sized.w = sized.h = px;
        sized.tex = uploadRgba(dev_, px, px, pixels.data());
        if (!sized.tex) return;
    }
    // Centered in r at its own size, as DrawIconEx draws the icon of that size.
    const float x = std::floor(float(r.left) + float(r.width() - px) / 2), y = std::floor(float(r.top) + float(r.height() - px) / 2);
    dl_->AddImage(texRef(sized.tex), at(x, y), at(x + float(px), y + float(px)), ImVec2(0, 0), ImVec2(1, 1), colorOf(draw::rgb(255, 255, 255)));
}

void ImguiCanvas::fitted(const draw::Image& img, Rect box) {
    if (!img.handle || img.w <= 0 || img.h <= 0 || box.empty()) return;
    // Smaller to fit the box, never larger; centered.
    const float s = std::min(1.0f, std::min(float(box.width()) / float(img.w), float(box.height()) / float(img.h)));
    const float w = float(img.w) * s, h = float(img.h) * s;
    const float x = float(box.left) + (float(box.width()) - w) / 2, y = float(box.top) + (float(box.height()) - h) / 2;
    dl_->AddImage(texRef(img.handle), at(x, y), at(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), colorOf(draw::rgb(255, 255, 255)));
}

void ImguiCanvas::externalImage(const void* gpuTexture, Rect dst) {
    if (!gpuTexture || dst.empty()) return;
    dl_->AddImage(texRef(gpuTexture), at(float(dst.left), float(dst.top)), at(float(dst.right), float(dst.bottom)), ImVec2(0, 0),
                  ImVec2(1, 1), colorOf(draw::rgb(255, 255, 255)));
}

}  // namespace stardraft::kit_sdl
