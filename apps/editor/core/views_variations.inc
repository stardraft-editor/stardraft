// The side bar's Variations strip (Terrain mode with a terrain selection, under the terrain list): the selection as one
// piece, drawn once for each of its variations (core/tile_variations.inc: selectionPiece), in its own shape (a 1 x 2
// selection: pictures of 1 x 2 tiles). The one that the tiles use now has an accent frame. The mouse over a variation
// shows it on the map (the terrain preview, no change); a click puts it on the selected tiles.
// Its title row ("Variations", with a chevron) folds it; a drag on its top edge makes it taller or shorter (both kept in
// the settings); the wheel scrolls it, and a scroll bar shows at its right edge while the mouse is over it.
void shellRelayout();  // (the host's: the window laid out again)
struct VariationsView : draw::View {
    int hover = -1;  // the index in the variations under the mouse
    int scroll = 0;
    bool over = false;  // the mouse is over the strip (at overAt)
    draw::Point overAt{};
    std::vector<std::pair<int, int>> scrolledFor;  // (the tiles that the scroll is for: another selection starts at the top)
    bool sizing = false, thumbDrag = false, titleHot = false;
    int sizeFromY = 0, sizeFromH = 0, thumbFromY = 0, thumbFromScroll = 0;
    int capH = 0;  // the most the side bar gives it (the host's layout: the list keeps its least); 0: not known yet
    static int titleH() { return scale(24); }
    static int edgeH() { return scale(5); }  // (the top edge that a drag resizes)
    static int barW() { return scale(8); }   // (the scroll bar's room at the right edge)
    static int gap() { return scale(6); }
    static int pad() { return scale(6); }
    // Open (not folded), and the height the user gave it (0: as its pictures need, up to a default), from the settings.
    static bool& open() {
        static bool v = [] {
            bool on = true;
            if (!gSelfTest && !gSettingsOff) prefs::flag(L"VariationsOpen", on);
            return on;
        }();
        return v;
    }
    static int& userH() {
        static int v = [] {
            int h = 0;
            if (!gSelfTest && !gSettingsOff) prefs::number(L"VariationsHeight", h, 0, 4000);
            return h;
        }();
        return v;
    }
    // A tile's size in the pictures (pixels, with fractions) for the strip's width: two pictures in a row when they fit,
    // at most a tile's own 32 px (scaled); a wide selection gets smaller tiles, so that one picture fits the width.
    float tilePx(const SelectionPiece& p, int w) const {
        const float room = float(std::max(4, w - 2 * pad() - barW() - 2)), pw = float(std::max(1, p.w)), ph = float(std::max(1, p.h));
        float t = std::min(float(scale(32)), (room - gap()) / 2 / pw);
        if (t < 2) t = std::min(2.0f, room / pw);  // (two do not fit with tiles of 2 px: one in a row)
        return std::min(t, std::max(1.0f, float(scale(160)) / ph));  // (a tall selection: not taller than 160 px)
    }
    // The pictures' boxes (in the content: the scroll not taken off) and the content's height.
    std::vector<draw::Rect> boxes(const SelectionPiece& p, int w, int* height = nullptr) const {
        std::vector<draw::Rect> out;
        const float t = tilePx(p, w);
        const int bw = int(std::ceil(p.w * t)) + 2, bh = int(std::ceil(p.h * t)) + 2, right = w - pad() - barW();
        int x = pad(), y = titleH();
        for (size_t k = 0; k < p.subs.size(); ++k) {
            if (x + bw > right && x > pad()) x = pad(), y += bh + gap();
            out.push_back({x, y, x + bw, y + bh});
            x += bw + gap();
        }
        if (height) *height = (out.empty() ? y : out.back().bottom) + pad();
        return out;
    }
    // The height the strip needs at width `w` (0: not shown): the title row when it is folded or has no pictures; else
    // the height the user gave it (at least the title and a row of pictures), or as its pictures need up to 220 px.
    int heightFor(int w) const {
        const SelectionPiece& p = selectionPiece();
        if (p.tiles.empty() && !p.tooMany) return 0;
        if (!open() || p.subs.size() < 2) return titleH() + pad();
        int content = 0;
        const std::vector<draw::Rect> b = boxes(p, w, &content);
        const int least = std::min(content, (b.empty() ? titleH() : b[0].bottom) + pad());
        return userH() > 0 ? std::max(least, userH()) : std::min(content, scale(220));
    }
    // Its top edge resizes it: open, with pictures (a note alone has one height).
    bool resizable() const {
        const SelectionPiece& p = selectionPiece();
        return open() && !p.tiles.empty() && p.subs.size() >= 2;
    }
    int contentH(const SelectionPiece& p) const {
        int h = 0;
        boxes(p, width(), &h);
        return h;
    }
    int maxScroll(const SelectionPiece& p) const { return open() ? std::max(0, contentH(p) - height()) : 0; }
    void keepScroll(const SelectionPiece& p) {
        if (p.tiles != scrolledFor) scrolledFor = p.tiles, scroll = 0;
        scroll = std::clamp(scroll, 0, maxScroll(p));
    }
    // The scroll bar's thumb (empty: no scroll bar: all shows, or the mouse is not over the strip).
    draw::Rect thumb(const SelectionPiece& p) const {
        const int total = contentH(p) - titleH(), page = height() - titleH(), most = maxScroll(p);
        if (!(over || thumbDrag) || most <= 0 || page < scale(8) || total <= 0) return {};
        const int h = std::min(page, std::max(scale(16), page * page / total)), y = titleH() + (page - h) * scroll / most;
        return {width() - barW() + scale(1), y, width() - scale(2), y + h};
    }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kPanel);
        c.fill({0, 0, width(), 1}, kBorderStrong);
        const SelectionPiece& p = selectionPiece();
        const int flags = draw::kLeft | draw::kMiddle | draw::kEllipsis;
        // The title row: a chevron and "Variations" (a click folds or opens it); a note after it when there is no picture.
        const draw::Color tc = titleHot ? kText : kDim;
        c.icon({pad() - scale(2), 0, pad() + scale(14), titleH()}, int(open() ? UiIcon::ChevronDown : UiIcon::ChevronRight), tc, scale(14));
        const std::wstring title = shellTr(L"Variations");
        const int tx = pad() + scale(16);
        c.text({tx, 0, width() - pad(), titleH()}, title, tc, flags);
        std::wstring note;
        if (p.tooMany) {
            wchar_t m[96];
            std::swprintf(m, 96, shellTr(L"Select at most %d x %d tiles to see their variations").c_str(), kVariationW, kVariationH);
            note = m;
        } else if (!p.tiles.empty() && p.subs.size() < 2) note = shellTr(L"The selected tiles have no variations");
        if (!note.empty()) c.text({tx + c.measure(title) + scale(8), 0, width() - pad(), titleH()}, note, kDim, flags);
        if (!open() || p.tiles.empty() || p.subs.size() < 2) return;
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
        if (const draw::Rect th = thumb(p); th.right > th.left) c.fill(th, thumbDrag ? kText : kBorderStrong);
    }
    // The variation under `pt` (the view's coordinates), or -1.
    int at(const SelectionPiece& p, draw::Point pt) const {
        if (!open() || pt.y < titleH() || p.subs.size() < 2 || pt.x >= width() - barW()) return -1;
        const std::vector<draw::Rect> b = boxes(p, width());
        for (size_t k = 0; k < b.size(); ++k)
            if (b[k].contains({pt.x, pt.y + scroll})) return int(k);
        return -1;
    }
    void hoverAt(draw::Point pt) {
        const bool was = over;
        over = true, overAt = pt;
        setVariationStripHover(true);
        const SelectionPiece& p = selectionPiece();
        keepScroll(p);
        const int k = at(p, pt);
        const bool th = pt.y >= edgeH() && pt.y < titleH();
        if (k != hover || th != titleHot || !was) hover = k, titleHot = th, invalidate();
        setVariationPreview(k >= 0 ? pieceVariationCells(p, size_t(k)) : std::vector<Editor::TileAt>{});
    }
    // The preview again for the strip as it is now (its tiles changed: an undo, another selection, another map).
    void refresh() {
        if (over) hoverAt(overAt);
        else setVariationPreview({});
    }
    draw::Cursor cursor(draw::Point pt) override { return sizing || (resizable() && pt.y < edgeH()) ? draw::Cursor::SizeNS : draw::Cursor::Arrow; }
    // The capture went away during a drag (Alt+Tab, another window): the drag ends where it is.
    void captureLost() override {
        if (sizing && !gSelfTest && !gSettingsOff) prefs::setNumber(L"VariationsHeight", userH());
        if (sizing || thumbDrag) sizing = thumbDrag = false, invalidate();
    }
    bool mouse(const draw::MouseEvent& e) override {
        const SelectionPiece& p = selectionPiece();
        const int pageY = e.pt.y + frame().top;  // (in the parent's coordinates: the strip's top moves while it is resized)
        if (sizing || thumbDrag) {
            if (e.kind == draw::MouseKind::Move) {
                if (sizing) {
                    const int h = std::max(titleH() + scale(40), sizeFromH + sizeFromY - pageY);
                    userH() = capH > 0 ? std::min(h, std::max(titleH() + scale(40), capH)) : h;
                    shellRelayout();
                } else {
                    const int page = height() - titleH(), total = contentH(p) - titleH(), most = maxScroll(p);
                    const int h = std::min(page, std::max(scale(16), page * page / std::max(1, total)));
                    if (page > h) scroll = std::clamp(thumbFromScroll + (e.pt.y - thumbFromY) * most / (page - h), 0, most);
                    invalidate();
                }
                return true;
            }
            if (e.kind == draw::MouseKind::Up) {
                if (sizing && !gSelfTest && !gSettingsOff) prefs::setNumber(L"VariationsHeight", userH());
                sizing = thumbDrag = false;
                if (host()) host()->capture(*this, false);
                invalidate();
                return true;
            }
        }
        if (e.kind == draw::MouseKind::Leave) {
            over = false, hover = -1, titleHot = false;
            setVariationStripHover(false);
            setVariationPreview({});
            invalidate();
            return true;
        }
        if (e.kind == draw::MouseKind::Wheel) {
            keepScroll(p);
            const int before = scroll;
            scroll = std::clamp(scroll - int(e.wheel * scale(40)), 0, maxScroll(p));
            if (scroll != before) invalidate(), hoverAt(e.pt);
            return true;
        }
        if (e.kind == draw::MouseKind::Move) return hoverAt(e.pt), true;
        if (e.kind != draw::MouseKind::Down) return false;
        if (e.button != 0) return true;
        if (resizable() && e.pt.y < edgeH()) {  // the top edge: a drag resizes
            sizing = true, sizeFromY = pageY, sizeFromH = height();
            if (host()) host()->capture(*this, true);
            return true;
        }
        if (e.pt.y < titleH()) {  // the title row: fold or open
            open() = !open();
            if (!gSelfTest && !gSettingsOff) prefs::setNumber(L"VariationsOpen", open() ? 1 : 0);
            setVariationPreview({});
            shellRelayout();
            return true;
        }
        if (const draw::Rect th = thumb(p); th.right > th.left && e.pt.x >= width() - barW()) {  // the scroll bar
            if (th.contains(e.pt)) thumbDrag = true, thumbFromY = e.pt.y, thumbFromScroll = scroll;
            else scroll = std::clamp(scroll + (e.pt.y < th.top ? -1 : 1) * (height() - titleH()), 0, maxScroll(p)), invalidate();  // (a page)
            if (thumbDrag && host()) host()->capture(*this, true);
            return true;
        }
        const int k = at(p, e.pt);
        if (k < 0) return true;
        setVariationPreview({});
        setSelectionVariation(size_t(k));
        invalidate();
        invalidateMapView();
        return true;
    }
    bool hoverTip() const override { return true; }
    std::wstring tip(draw::Point pt) override {
        if (resizable() && pt.y < edgeH()) return shellTr(L"Drag to make Variations taller or shorter");
        if (pt.y < titleH()) return shellTr(open() ? L"Hide the variations" : L"Show the variations");
        const int k = at(selectionPiece(), pt);
        if (k < 0) return {};
        wchar_t m[160];
        std::swprintf(m, 160, shellTr(L"Variation %d. Click: put it on the selected tiles").c_str(), k + 1);
        return m;
    }
};
VariationsView gVariationsView;
