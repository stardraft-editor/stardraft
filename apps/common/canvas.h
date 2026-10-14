#pragma once
// What the editor's UI draws on: the control kit and the panes paint through this, never through a system API, so
// the same UI code runs in a Win32 window (GdiCanvas, apps/editor/canvas_gdi.inc) and in an SDL window (later).
//
// Coordinates are pixels of the surface (already scaled for the display), right and bottom edges exclusive.
// Colors are 0x00BBGGRR (as the kit's color tokens); kNone: nothing is drawn.

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace stardraft::draw {

using Color = uint32_t;
constexpr Color kNone = 0xFFFFFFFF;
constexpr Color rgb(int r, int g, int b) { return Color(r & 255) | Color(g & 255) << 8 | Color(b & 255) << 16; }

struct Point {
    int x = 0, y = 0;
};
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    Rect() = default;
    Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
    // From any rectangle type with the same four fields (the Win32 RECT, while the panes still use it).
    template <class R, class = decltype(R{}.left + R{}.top + R{}.right + R{}.bottom)>
    Rect(const R& r) : left(int(r.left)), top(int(r.top)), right(int(r.right)), bottom(int(r.bottom)) {}
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }
    bool contains(Point p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
};

// A font of the surface (the canvas makes and keeps them). Empty: none.
struct Font {
    const void* handle = nullptr;
    explicit operator bool() const { return handle != nullptr; }
};
struct FontMetrics {
    int height = 0, descent = 0;
};

// An image the canvas can draw (a unit's icon, a portrait). Its pixels are the canvas's own (a GDI bitmap, a texture).
struct Image {
    const void* handle = nullptr;
    int w = 0, h = 0;
    explicit operator bool() const { return handle != nullptr; }
};

// Pixels the program makes (the minimap, a palette's pictures): RGBA, premultiplied alpha, rows from the top. Each
// canvas makes its own copy (a GDI bitmap, a texture) when it first draws it, and again after `changed()`.
class Bitmap {
public:
    Bitmap() = default;
    Bitmap(int w, int h) { resize(w, h); }
    void resize(int w, int h) {
        w_ = w, h_ = h;
        px_.assign(size_t(w > 0 ? w : 0) * size_t(h > 0 ? h : 0), 0);
        changed();
    }
    int width() const { return w_; }
    int height() const { return h_; }
    uint32_t* pixels() { return px_.data(); }  // 0xAABBGGRR (bytes R, G, B, A)
    const uint32_t* pixels() const { return px_.data(); }
    // After a change of the pixels.
    void changed() { ++revision_; }
    // Every pixel is opaque (alpha 255): a canvas may copy it without blending (and keep a scaled copy).
    bool opaque = false;
    uint64_t revision() const { return revision_; }
    // (For canvases: their copy of the pixels and the revision it is of.)
    struct Native {
        virtual ~Native() = default;
        uint64_t revision = 0;
    };
    std::unique_ptr<Native> native;

private:
    int w_ = 0, h_ = 0;
    std::vector<uint32_t> px_;
    uint64_t revision_ = 1;
};

// How text sits in its box. '&' is not special.
enum TextFlags : unsigned {
    kLeft = 0,
    kCenter = 1,
    kRight = 2,
    kMiddle = 4,          // in the middle of the box from top to bottom (else at the top; one line only)
    kEllipsis = 8,        // "..." at the end when it does not fit
    kWrap = 16,           // lines broken between words to fit the width (else one line)
    kPathEllipsis = 32,   // "..." in the middle of a file path when it does not fit
    kBottom = 64,         // at the bottom of the box (one line only)
};

enum class LineStyle { Solid, Dash, Dot };

// A font by what it is for; the canvas makes it and keeps it.
enum class Face { Ui, Mono, Glyphs /*the icon glyph font (Segoe MDL2 Assets on Windows)*/ };
struct FontDesc {
    Face face = Face::Ui;
    int px = 0;  // the height of the letters in pixels; 0: the UI's size
    bool bold = false, italic = false, underline = false;
};

class Canvas {
public:
    virtual ~Canvas() = default;

    // `r` filled with `c`; `alpha` 255: opaque.
    virtual void fill(Rect r, Color c, uint8_t alpha = 255) = 0;
    // A box with rounded corners of `radius` px: filled with `fill`, with a `line` of `width` px inside `r`.
    virtual void roundBox(Rect r, Color fill, Color line, float radius, int width = 1) = 0;
    // The same outline, smoothed (antialiased).
    virtual void smoothRoundOutline(Rect r, float radius, Color line, float width = 1.0f) = 0;
    // An ellipse in `r` (as a GDI Ellipse: `r` includes the outline).
    virtual void ellipse(Rect r, Color fill, Color line) = 0;
    // An ellipse's outline in `r`, `width` px wide.
    virtual void ring(Rect r, Color line, int width = 1, LineStyle style = LineStyle::Solid) = 0;
    // A rectangle's outline inside `r`.
    virtual void frame(Rect r, Color line, int width = 1, LineStyle style = LineStyle::Solid) = 0;
    virtual void line(int x0, int y0, int x1, int y1, Color c, int width = 1, LineStyle style = LineStyle::Solid) = 0;
    // Lines from point to point.
    virtual void polyline(const Point* pts, int n, Color c, int width = 1, LineStyle style = LineStyle::Solid) = 0;
    // A filled polygon.
    virtual void polygon(const Point* pts, int n, Color fill, Color line = kNone) = 0;

    // Drawing only inside `r` (and inside the clip before it) until popClip().
    virtual void pushClip(Rect r) = 0;
    virtual void popClip() = 0;
    // A child view: drawing clipped to `r`, and (0, 0) at r's top left corner, until popView().
    virtual void pushView(Rect r) = 0;
    virtual void popView() = 0;

    // Text in the current font.
    virtual void text(Rect r, std::wstring_view t, Color c, unsigned flags = kMiddle) = 0;
    virtual int measure(std::wstring_view t) = 0;
    // The size of `t` drawn with `flags` in a box `width` wide (kWrap: its lines' height).
    virtual Point measureBox(std::wstring_view t, int width, unsigned flags) = 0;
    virtual FontMetrics metrics() = 0;
    // Makes `f` the current font; returns the one before.
    virtual Font setFont(Font f) = 0;
    virtual Font font(const FontDesc& d) = 0;

    // Part `src` of `b` into `dst` (scaled; `smooth`: filtered, else nearest pixels), with `alpha` (255: as it is).
    virtual void bitmap(Bitmap& b, Rect src, Rect dst, bool smooth = false, uint8_t alpha = 255) = 0;

    // A UI icon (apps/editor/res/ui_icons.h) in the middle of `r`, in color `c`, at the atlas size for `px` pixels.
    virtual void icon(Rect r, int icon, Color c, int px) = 0;
    // `img` in the middle of `box`, made smaller to fit.
    virtual void fitted(const Image& img, Rect box) = 0;
};

}  // namespace stardraft::draw
