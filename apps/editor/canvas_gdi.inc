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
using stardraft::draw::LineStyle;
using stardraft::draw::kLeft, stardraft::draw::kCenter, stardraft::draw::kRight, stardraft::draw::kMiddle, stardraft::draw::kEllipsis;
using stardraft::draw::kWrap, stardraft::draw::kPathEllipsis, stardraft::draw::kBottom;

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
    void ring(Rect r, Color line, int width, LineStyle style) override {
        HPEN pen = makePen(line, width, style);
        HGDIOBJ op = SelectObject(dc_, pen), ob = SelectObject(dc_, GetStockObject(NULL_BRUSH));
        Ellipse(dc_, r.left, r.top, r.right, r.bottom);
        SelectObject(dc_, op), SelectObject(dc_, ob);
        DeleteObject(pen);
    }
    void frame(Rect r, Color line, int width, LineStyle style) override { outline(dc_, toRECT(r), line, width, penStyle(style)); }
    void line(int x0, int y0, int x1, int y1, Color c, int width, LineStyle style) override {
        if (style == LineStyle::Solid) return ::line(dc_, x0, y0, x1, y1, c, width);
        const Point pts[] = {{x0, y0}, {x1, y1}};
        polyline(pts, 2, c, width, style);
    }
    void polygon(const Point* pts, int n, Color fill, Color line) override {
        if (n < 3) return;
        std::vector<POINT> ps(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) ps[size_t(i)] = {pts[i].x, pts[i].y};
        HBRUSH b = fill == kNone ? nullptr : CreateSolidBrush(fill);
        HPEN p = line == kNone ? nullptr : CreatePen(PS_SOLID, 1, line);
        HGDIOBJ ob = SelectObject(dc_, b ? HGDIOBJ(b) : GetStockObject(NULL_BRUSH)), op = SelectObject(dc_, p ? HGDIOBJ(p) : GetStockObject(NULL_PEN));
        Polygon(dc_, ps.data(), n);
        SelectObject(dc_, ob), SelectObject(dc_, op);
        if (b) DeleteObject(b);
        if (p) DeleteObject(p);
    }
    void pushClip(Rect r) override {
        clips_.push_back(SaveDC(dc_));
        IntersectClipRect(dc_, r.left, r.top, r.right, r.bottom);
    }
    void popClip() override {
        if (clips_.empty()) return;
        RestoreDC(dc_, clips_.back());
        clips_.pop_back();
    }
    void polyline(const Point* pts, int n, Color c, int width, LineStyle style) override {
        if (n < 2) return;
        HPEN p = makePen(c, width, style);
        HGDIOBJ op = SelectObject(dc_, p);
        MoveToEx(dc_, pts[0].x, pts[0].y, nullptr);
        for (int i = 1; i < n; ++i) LineTo(dc_, pts[i].x, pts[i].y);
        SelectObject(dc_, op);
        DeleteObject(p);
    }

    static UINT dtFlags(unsigned flags) {
        UINT f = DT_NOPREFIX | (flags & kWrap ? DT_WORDBREAK : DT_SINGLELINE);
        if (flags & kCenter) f |= DT_CENTER;
        if (flags & kRight) f |= DT_RIGHT;
        if (flags & kMiddle) f |= DT_VCENTER;
        if (flags & kBottom) f |= DT_BOTTOM;
        if (flags & kEllipsis) f |= DT_END_ELLIPSIS;
        if (flags & kPathEllipsis) f |= DT_PATH_ELLIPSIS;
        return f;
    }
    void text(Rect r, std::wstring_view t, Color c, unsigned flags) override {
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, c);
        RECT rc = toRECT(r);
        DrawTextW(dc_, t.data(), int(t.size()), &rc, dtFlags(flags));
    }
    Point measureBox(std::wstring_view t, int width, unsigned flags) override {
        RECT rc{0, 0, width, 0};
        DrawTextW(dc_, t.data(), int(t.size()), &rc, dtFlags(flags) | DT_CALCRECT);
        return {rc.right, rc.bottom};
    }
    Font font(const stardraft::draw::FontDesc& d) override { return Font{gdiFont(d)}; }
    void bitmap(stardraft::draw::Bitmap& b, Rect src, Rect dst, bool smooth, uint8_t alpha) override {
        struct Dib : stardraft::draw::Bitmap::Native {
            HBITMAP bmp = nullptr;
            void* bits = nullptr;
            int w = 0, h = 0;
            ~Dib() override {
                if (bmp) DeleteObject(bmp);
            }
        };
        if (b.width() <= 0 || b.height() <= 0) return;
        auto* d = dynamic_cast<Dib*>(b.native.get());
        if (!d || d->w != b.width() || d->h != b.height()) {
            auto nd = std::make_unique<Dib>();
            BITMAPINFO bi{};
            bi.bmiHeader = {sizeof(BITMAPINFOHEADER), b.width(), -b.height(), 1, 32, BI_RGB};
            nd->bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &nd->bits, nullptr, 0);
            if (!nd->bmp) return;
            nd->w = b.width(), nd->h = b.height(), nd->revision = 0;
            d = nd.get();
            b.native = std::move(nd);
        }
        if (d->revision != b.revision()) {  // RGBA to GDI's BGRA
            auto* o = static_cast<uint32_t*>(d->bits);
            const uint32_t* in = b.pixels();
            for (size_t i = 0, n = size_t(b.width()) * size_t(b.height()); i < n; ++i) {
                const uint32_t p = in[i];
                o[i] = (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
            }
            GdiFlush();
            d->revision = b.revision();
        }
        HDC mem = CreateCompatibleDC(dc_);
        HGDIOBJ old = SelectObject(mem, d->bmp);
        const int mode = SetStretchBltMode(dc_, smooth ? HALFTONE : COLORONCOLOR);
        if (smooth) SetBrushOrgEx(dc_, 0, 0, nullptr);
        const BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        AlphaBlend(dc_, dst.left, dst.top, dst.width(), dst.height(), mem, src.left, src.top, src.width(), src.height(), bf);
        SetStretchBltMode(dc_, mode);
        SelectObject(mem, old);
        DeleteDC(mem);
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
    static int penStyle(LineStyle s) { return s == LineStyle::Dash ? PS_DASH : s == LineStyle::Dot ? PS_DOT : PS_SOLID; }
    // A pen: wide dashed lines need a geometric pen (CreatePen draws wide dashes solid).
    static HPEN makePen(Color c, int width, LineStyle style) {
        if (style == LineStyle::Solid || width <= 1) return CreatePen(penStyle(style), width, c);
        LOGBRUSH lb{BS_SOLID, c, 0};
        return ExtCreatePen(PS_GEOMETRIC | penStyle(style) | PS_ENDCAP_FLAT, DWORD(width), &lb, 0, nullptr);
    }
    static HFONT gdiFont(const stardraft::draw::FontDesc& d) {
        static std::map<std::tuple<int, int, int, bool, bool, bool>, HFONT> fonts;
        HFONT& f = fonts[{int(d.face), d.px, scale(100), d.bold, d.italic, d.underline}];
        if (!f) {
            LOGFONTW lf{};
            if (d.face == stardraft::draw::Face::Glyphs) {
                wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
                lf.lfHeight = -(d.px ? d.px : scale(kGlyph));
            } else {
                GetObjectW(d.face == stardraft::draw::Face::Mono ? monoFont() : app.font, sizeof lf, &lf);
                if (d.px) lf.lfHeight = -d.px;
            }
            if (d.bold) lf.lfWeight = FW_BOLD;
            lf.lfItalic = d.italic, lf.lfUnderline = d.underline;
            f = CreateFontIndirectW(&lf);
        }
        return f;
    }
    HDC dc_;
    std::vector<int> clips_;
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
