// --- The Canvas on GDI ------------------------------------------------------------------------------------------
// apps/common/canvas.h drawn into a DC with the same GDI calls the kit made before it had the Canvas (same pixels).
// A Paint is what the kit's functions take: a Canvas, or a DC (a GdiCanvas on it), so the panes that still paint
// with GDI call the kit as before, and each pane moves to the Canvas on its own.

namespace ui {

using stardraft::draw::Canvas;
using stardraft::draw::Color;
using stardraft::draw::Font;
using stardraft::draw::Image;
using stardraft::draw::kNone;
using stardraft::draw::Point;
using stardraft::draw::Rect;
using stardraft::draw::kLeft, stardraft::draw::kCenter, stardraft::draw::kRight, stardraft::draw::kMiddle, stardraft::draw::kEllipsis;

inline RECT toRECT(Rect r) { return {r.left, r.top, r.right, r.bottom}; }
inline Font fontOf(HFONT f) { return Font{f}; }
inline Image imageOf(const UnitPalette::Bitmap* b) { return b && b->bmp ? Image{b->bmp, b->w, b->h} : Image{}; }

class GdiCanvas final : public Canvas {
public:
    explicit GdiCanvas(HDC dc) : dc_(dc) {}
    HDC dc() const { return dc_; }

    void fill(Rect r, Color c, uint8_t alpha) override { fillAlpha(dc_, toRECT(r), c, alpha); }
    void roundBox(Rect r, Color fill, Color line, int radius, int width) override {
        HBRUSH b = fill == kNone ? nullptr : CreateSolidBrush(fill);
        HPEN p = line == kNone ? nullptr : CreatePen(PS_SOLID, width, line);
        HGDIOBJ ob = SelectObject(dc_, b ? HGDIOBJ(b) : GetStockObject(NULL_BRUSH)), op = SelectObject(dc_, p ? HGDIOBJ(p) : GetStockObject(NULL_PEN));
        // GDI: without a pen the fill stops 1 px short of the right and bottom; a wide pen is centered on the edge.
        const int in = p ? width / 2 : 0, out = p ? 0 : 1;
        RoundRect(dc_, r.left + in, r.top + in, r.right + out, r.bottom + out, 2 * radius, 2 * radius);
        SelectObject(dc_, ob), SelectObject(dc_, op);
        if (b) DeleteObject(b);
        if (p) DeleteObject(p);
    }
    void smoothRoundOutline(Rect r, float radius, Color line, float width) override { smoothRoundBox(dc_, toRECT(r), radius, line, width); }
    void ellipse(Rect r, Color fill, Color line) override {
        HBRUSH b = fill == kNone ? nullptr : CreateSolidBrush(fill);
        HPEN p = line == kNone ? nullptr : CreatePen(PS_SOLID, 1, line);
        HGDIOBJ ob = SelectObject(dc_, b ? HGDIOBJ(b) : GetStockObject(NULL_BRUSH)), op = SelectObject(dc_, p ? HGDIOBJ(p) : GetStockObject(NULL_PEN));
        Ellipse(dc_, r.left, r.top, r.right, r.bottom);
        SelectObject(dc_, ob), SelectObject(dc_, op);
        if (b) DeleteObject(b);
        if (p) DeleteObject(p);
    }
    void line(int x0, int y0, int x1, int y1, Color c, int width) override { ::line(dc_, x0, y0, x1, y1, c, width); }
    void polyline(const Point* pts, int n, Color c, int width) override {
        if (n < 2) return;
        HPEN p = CreatePen(PS_SOLID, width, c);
        HGDIOBJ op = SelectObject(dc_, p);
        MoveToEx(dc_, pts[0].x, pts[0].y, nullptr);
        for (int i = 1; i < n; ++i) LineTo(dc_, pts[i].x, pts[i].y);
        SelectObject(dc_, op);
        DeleteObject(p);
    }

    void text(Rect r, std::wstring_view t, Color c, unsigned flags) override {
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, c);
        UINT f = DT_SINGLELINE | DT_NOPREFIX;
        if (flags & kCenter) f |= DT_CENTER;
        if (flags & kRight) f |= DT_RIGHT;
        if (flags & kMiddle) f |= DT_VCENTER;
        if (flags & kEllipsis) f |= DT_END_ELLIPSIS;
        RECT rc = toRECT(r);
        DrawTextW(dc_, t.data(), int(t.size()), &rc, f);
    }
    int measure(std::wstring_view t) override {
        SIZE sz{};
        GetTextExtentPoint32W(dc_, t.data(), int(t.size()), &sz);
        return sz.cx;
    }
    stardraft::draw::FontMetrics metrics() override {
        TEXTMETRICW tm{};
        GetTextMetricsW(dc_, &tm);
        return {tm.tmHeight, tm.tmDescent};
    }
    Font setFont(Font f) override { return Font{SelectObject(dc_, HGDIOBJ(const_cast<void*>(f.handle)))}; }

    void icon(Rect r, int icon, Color c, int px) override { uiIcon(dc_, toRECT(r), UiIcon(icon), c, px); }
    void fitted(const Image& img, Rect box) override {
        UnitPalette::Bitmap b;
        b.bmp = HBITMAP(const_cast<void*>(img.handle)), b.w = img.w, b.h = img.h;
        drawFitted(dc_, b, toRECT(box));
    }

private:
    HDC dc_;
};

// What a kit function draws on. Made from a Canvas, or from a DC (then it holds a GdiCanvas on it for the call).
class Paint {
public:
    Paint(Canvas& c) : c_(&c) {}
    Paint(HDC dc) : gdi_(dc), c_(&gdi_) {}
    Paint(const Paint&) = delete;
    Paint& operator=(const Paint&) = delete;
    Canvas& operator*() const { return *c_; }
    Canvas* operator->() const { return c_; }

private:
    GdiCanvas gdi_{nullptr};
    Canvas* c_;
};

}  // namespace ui
