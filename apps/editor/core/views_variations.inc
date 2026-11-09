// The side bar's Variations strip (Terrain mode with a terrain selection, under the terrain list): the selection as one
// piece, drawn once for each of its variations (core/tile_variations.inc: selectionPiece), in its own shape (a 1 x 2
// selection: pictures of 1 x 2 tiles). The one that the tiles use now has an accent frame. The mouse over a variation
// shows it on the map (the terrain preview, no change); a click puts it on the selected tiles. The wheel scrolls the
// strip when its pictures need more room than the side bar gives.
struct VariationsView : draw::View {
    int hover = -1;  // the index in the variations under the mouse
    int scroll = 0;
    bool over = false;  // the mouse is over the strip (at overAt)
    draw::Point overAt{};
    std::vector<std::pair<int, int>> scrolledFor;  // (the tiles that the scroll is for: another selection starts at the top)
    static int titleH() { return scale(24); }
    static int gap() { return scale(6); }
    static int pad() { return scale(6); }
    // A tile's size in the pictures (pixels, with fractions) for the strip's width: two pictures in a row when they fit,
    // at most a tile's own 32 px (scaled); a wide selection gets smaller tiles, so that one picture fits the width.
    float tilePx(const SelectionPiece& p, int w) const {
        const float room = float(std::max(4, w - 2 * pad() - 2)), pw = float(std::max(1, p.w)), ph = float(std::max(1, p.h));
        float t = std::min(float(scale(32)), (room - gap()) / 2 / pw);
        if (t < 2) t = std::min(2.0f, room / pw);  // (two do not fit with tiles of 2 px: one in a row)
        return std::min(t, std::max(1.0f, float(scale(160)) / ph));  // (a tall selection: not taller than 160 px)
    }
    // The pictures' boxes (in the content: the scroll not taken off) and the content's height.
    std::vector<draw::Rect> boxes(const SelectionPiece& p, int w, int* height = nullptr) const {
        std::vector<draw::Rect> out;
        const float t = tilePx(p, w);
        const int bw = int(std::ceil(p.w * t)) + 2, bh = int(std::ceil(p.h * t)) + 2;
        int x = pad(), y = titleH();
        for (size_t k = 0; k < p.subs.size(); ++k) {
            if (x + bw > w - pad() && x > pad()) x = pad(), y += bh + gap();
            out.push_back({x, y, x + bw, y + bh});
            x += bw + gap();
        }
        if (height) *height = (out.empty() ? y : out.back().bottom) + pad();
        return out;
    }
    // The height the strip needs at width `w` (0: not shown).
    int heightFor(int w) const {
        const SelectionPiece& p = selectionPiece();
        if (p.tiles.empty() && !p.tooMany) return 0;
        if (p.subs.size() < 2) return titleH() + pad();
        int h = 0;
        boxes(p, w, &h);
        return h;
    }
    int maxScroll(const SelectionPiece& p) const {
        int h = 0;
        boxes(p, width(), &h);
        return std::max(0, h - height());
    }
    void keepScroll(const SelectionPiece& p) {
        if (p.tiles != scrolledFor) scrolledFor = p.tiles, scroll = 0;
        scroll = std::clamp(scroll, 0, maxScroll(p));
    }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kPanel);
        c.fill({0, 0, width(), 1}, kBorderStrong);
        const SelectionPiece& p = selectionPiece();
        const draw::Rect titleBox{pad(), 0, width() - pad(), titleH()};
        const int flags = draw::kLeft | draw::kMiddle | draw::kEllipsis;
        if (p.tooMany) {
            wchar_t m[96];
            std::swprintf(m, 96, shellTr(L"Select at most %d x %d tiles to see their variations").c_str(), kVariationW, kVariationH);
            return c.text(titleBox, m, kDim, flags);
        }
        if (p.tiles.empty()) return;
        if (p.subs.size() < 2) return c.text(titleBox, shellTr(L"The selected tiles have no variations"), kDim, flags);
        keepScroll(p);
        const float t = tilePx(p, width());
        const std::vector<draw::Rect> b = boxes(p, width());
        c.pushClip({0, titleH(), width(), height()});
        tilesPixelsTrim();
        for (size_t k = 0; k < b.size(); ++k) {
            const draw::Rect box{b[k].left, b[k].top - scroll, b[k].right, b[k].bottom - scroll};
            if (box.bottom < titleH() || box.top > height()) continue;
            c.fill(box, kBg);
            for (size_t i = 0; i < p.tiles.size(); ++i) {
                draw::Rect src;
                draw::Bitmap& px = tilesPixels(p.values[k][i], src);
                const float fx = box.left + 1 + (p.tiles[i].first - p.x0) * t, fy = box.top + 1 + (p.tiles[i].second - p.y0) * t;
                const int l = int(std::lround(fx)), tp = int(std::lround(fy));
                c.bitmap(px, src, {l, tp, std::max(l + 1, int(std::lround(fx + t))), std::max(tp + 1, int(std::lround(fy + t)))}, true);
            }
            const bool hot = int(k) == hover;
            if (p.now[k]) c.frame(box, kAccent, std::max(2, scale(2)));
            else c.frame(box, hot ? kText : kBorderStrong, hot ? std::max(2, scale(2)) : 1);
        }
        c.popClip();
        wchar_t title[96];
        std::swprintf(title, 96, shellTr(L"Variations of the selection (%d x %d tiles)").c_str(), p.w, p.h);
        c.text(titleBox, title, kDim, flags);
    }
    // The variation under `pt` (the view's coordinates), or -1.
    int at(const SelectionPiece& p, draw::Point pt) const {
        if (pt.y < titleH() || p.subs.size() < 2) return -1;
        const std::vector<draw::Rect> b = boxes(p, width());
        for (size_t k = 0; k < b.size(); ++k)
            if (b[k].contains({pt.x, pt.y + scroll})) return int(k);
        return -1;
    }
    void hoverAt(draw::Point pt) {
        over = true, overAt = pt;
        setVariationStripHover(true);
        const SelectionPiece& p = selectionPiece();
        keepScroll(p);
        const int k = at(p, pt);
        if (k != hover) hover = k, invalidate();
        setVariationPreview(k >= 0 ? pieceVariationCells(p, size_t(k)) : std::vector<Editor::TileAt>{});
    }
    // The preview again for the strip as it is now (its tiles changed: an undo, another selection, another map).
    void refresh() {
        if (over) hoverAt(overAt);
        else setVariationPreview({});
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Leave) {
            over = false, hover = -1;
            setVariationStripHover(false);
            setVariationPreview({});
            invalidate();
            return true;
        }
        if (e.kind == draw::MouseKind::Wheel) {
            const SelectionPiece& p = selectionPiece();
            keepScroll(p);
            const int before = scroll;
            scroll = std::clamp(scroll - int(e.wheel * scale(40)), 0, maxScroll(p));
            if (scroll != before) invalidate(), hoverAt(e.pt);
            return true;
        }
        if (e.kind == draw::MouseKind::Move) return hoverAt(e.pt), true;
        if (e.kind != draw::MouseKind::Down) return false;
        const int k = at(selectionPiece(), e.pt);
        if (e.button != 0 || k < 0) return true;
        setVariationPreview({});
        setSelectionVariation(size_t(k));
        invalidate();
        invalidateMapView();
        return true;
    }
    bool hoverTip() const override { return true; }
    std::wstring tip(draw::Point pt) override {
        const int k = at(selectionPiece(), pt);
        if (k < 0) return {};
        wchar_t m[160];
        std::swprintf(m, 160, shellTr(L"Variation %d. Click: put it on the selected tiles").c_str(), k + 1);
        return m;
    }
};
VariationsView gVariationsView;
