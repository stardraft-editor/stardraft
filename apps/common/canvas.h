#pragma once
// What the editor's UI draws on: the control kit and the panes paint through this, never through a system API, so
// the same UI code runs in a Win32 window (GdiCanvas, apps/editor/canvas_gdi.inc) and in an SDL window (later).
//
// Coordinates are pixels of the surface (already scaled for the display), right and bottom edges exclusive.
// Colors are 0x00BBGGRR (as the kit's color tokens); kNone: nothing is drawn.

#include <cstdint>
#include <string_view>

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

// How text sits in its box. Text is always one line, and '&' is not special.
enum TextFlags : unsigned {
    kLeft = 0,
    kCenter = 1,
    kRight = 2,
    kMiddle = 4,    // in the middle of the box from top to bottom (else at the top)
    kEllipsis = 8,  // "..." at the end when it does not fit
};

class Canvas {
public:
    virtual ~Canvas() = default;

    // `r` filled with `c`; `alpha` 255: opaque.
    virtual void fill(Rect r, Color c, uint8_t alpha = 255) = 0;
    // A box with rounded corners of `radius` px: filled with `fill`, with a `line` of `width` px inside `r`.
    virtual void roundBox(Rect r, Color fill, Color line, int radius, int width = 1) = 0;
    // The same outline, smoothed (antialiased).
    virtual void smoothRoundOutline(Rect r, float radius, Color line, float width = 1.0f) = 0;
    // An ellipse in `r` (as a GDI Ellipse: `r` includes the outline).
    virtual void ellipse(Rect r, Color fill, Color line) = 0;
    virtual void line(int x0, int y0, int x1, int y1, Color c, int width = 1) = 0;
    // Lines from point to point.
    virtual void polyline(const Point* pts, int n, Color c, int width = 1) = 0;

    // Text in the current font.
    virtual void text(Rect r, std::wstring_view t, Color c, unsigned flags = kMiddle) = 0;
    virtual int measure(std::wstring_view t) = 0;
    virtual FontMetrics metrics() = 0;
    // Makes `f` the current font; returns the one before.
    virtual Font setFont(Font f) = 0;

    // A UI icon (apps/editor/res/ui_icons.h) in the middle of `r`, in color `c`, at the atlas size for `px` pixels.
    virtual void icon(Rect r, int icon, Color c, int px) = 0;
    // `img` in the middle of `box`, made smaller to fit.
    virtual void fitted(const Image& img, Rect box) = 0;
};

}  // namespace stardraft::draw
