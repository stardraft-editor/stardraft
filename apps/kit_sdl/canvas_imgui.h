#pragma once
// The kit's Canvas (apps/common/canvas.h) on Dear ImGui's draw lists. The SDL host (sdl_host.h) paints each window into
// an ImDrawList of its own and draws it with ImGui's SDL_GPU backend. The canvas works in the window's pixels, so the
// fonts are sized in pixels too. Bitmaps and images are GPU textures; the canvas makes them and keeps them.
//
// Limits: bold and italic use the regular face; the glyph face (Face::Glyphs) is the UI face; a smooth or nearest
// bitmap is drawn with the linear filter.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "canvas.h"
#include "imgui.h"
#include <SDL3/SDL_gpu.h>

namespace stardraft::kit_sdl {

// What a font is in the canvas: an ImGui font, a size in pixels and the underline. Made by ImguiCanvas::font().
struct FontEntry {
    ImFont* face = nullptr;
    int px = 15;
    bool underline = false;
};

// The UI fonts (fontDir first, then the system's: Latin, then Korean and Chinese merged in) and the monospace font,
// into the current ImGui context. Call it before the first frame. Returns what was loaded, for the log.
std::string loadFonts(const std::string& fontDir);

// The UI icon atlases (atlas-16.png, atlas-20.png, atlas-24.png, atlas-32.png in dir) uploaded to `device`.
// False when none is found. unloadIconAtlases() before the device goes.
bool loadIconAtlases(SDL_GPUDevice* device, const std::string& dir);
void unloadIconAtlases();

// RGBA8 pixels (straight alpha, rows from the top) as a texture the GPU samples. Null on failure.
SDL_GPUTexture* uploadRgba(SDL_GPUDevice* device, int w, int h, const uint8_t* rgba);

// Text helpers: UTF-8 of a wide text, and the byte index after the code point at `i`.
std::string utf8Text(std::wstring_view w);
size_t utf8Next(const std::string& s, size_t i);

class ImguiCanvas final : public draw::Canvas {
public:
    // Draws into `list` (made with ImGui::GetDrawListSharedData(), reset for the frame). `device` makes the textures.
    ImguiCanvas(ImDrawList& list, SDL_GPUDevice* device);

    void fill(draw::Rect r, draw::Color c, uint8_t alpha = 255) override;
    void roundBox(draw::Rect r, draw::Color fill, draw::Color line, float radius, int width = 1) override;
    void smoothRoundOutline(draw::Rect r, float radius, draw::Color line, float width = 1.0f) override;
    void smoothLine(PointF a, PointF b, draw::Color c, float width = 1.0f, uint8_t alpha = 255) override;
    void smoothFrame(PointF topLeft, float w, float h, draw::Color c, float width = 1.0f, uint8_t alpha = 255) override;
    void smoothArc(PointF topLeft, float w, float h, float startDeg, float sweepDeg, draw::Color c, float width = 1.0f) override;
    void smoothPolygon(const PointF* pts, int n, draw::Color fill) override;
    void smoothEllipse(PointF topLeft, float w, float h, draw::Color fill) override;
    void ellipse(draw::Rect r, draw::Color fill, draw::Color line) override;
    void ring(draw::Rect r, draw::Color line, int width = 1, draw::LineStyle style = draw::LineStyle::Solid) override;
    void frame(draw::Rect r, draw::Color line, int width = 1, draw::LineStyle style = draw::LineStyle::Solid) override;
    void line(int x0, int y0, int x1, int y1, draw::Color c, int width = 1, draw::LineStyle style = draw::LineStyle::Solid) override;
    void polyline(const draw::Point* pts, int n, draw::Color c, int width = 1, draw::LineStyle style = draw::LineStyle::Solid) override;
    void polygon(const draw::Point* pts, int n, draw::Color fill, draw::Color line = draw::kNone) override;

    void pushClip(draw::Rect r) override;
    void popClip() override;
    void pushView(draw::Rect r) override;
    void popView() override;

    void text(draw::Rect r, std::wstring_view t, draw::Color c, unsigned flags = draw::kMiddle) override;
    int measure(std::wstring_view t) override;
    // Each character at its own measured advance (measure() rounds each piece up: a run drawn whole would be narrower
    // than the rich field's units, and a gap would open after it).
    void textAt(draw::Point at, std::wstring_view t, draw::Color c) override;
    draw::Point measureBox(std::wstring_view t, int width, unsigned flags) override;
    draw::FontMetrics metrics() override;
    draw::Font setFont(draw::Font f) override;
    draw::Font font(const draw::FontDesc& d) override;

    void bitmap(draw::Bitmap& b, draw::Rect src, draw::Rect dst, bool smooth = false, uint8_t alpha = 255) override;

    void icon(draw::Rect r, int icon, draw::Color c, int px) override;
    void fitted(const draw::Image& img, draw::Rect box) override;
    void externalImage(const void* gpuTexture, draw::Rect dst) override;

private:
    struct Saved {
        ImVec2 origin;
    };
    ImVec2 at(float x, float y) const { return ImVec2(origin_.x + x, origin_.y + y); }

    ImDrawList* dl_;
    SDL_GPUDevice* dev_;
    const FontEntry* cur_;
    ImVec2 origin_{0, 0};
    std::vector<Saved> stack_;  // what popClip and popView restore
};

}  // namespace stardraft::kit_sdl
