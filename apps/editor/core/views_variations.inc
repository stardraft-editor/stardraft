// The side bar's Variations strip (Terrain mode with a terrain selection, under the terrain list): the variations of the
// selected tile under the mouse (core/tile_variations.inc: variationsTile), a picture of each; the one in use has an
// accent frame, and the rare ones come after a gap. A click puts that variation on the tile, the other half of its
// rect and its stack; Shift+click only on the tile.
void invalidateMapView();  // (the host's: the map view is drawn again)
struct VariationsView : draw::View {
    int hover = -1;  // the index in the variations under the mouse
    static int cell() { return scale(34); }
    static int titleH() { return scale(24); }
    static int gap() { return scale(4); }
    // The cells' boxes for the width, and the height that they take.
    std::vector<draw::Rect> boxes(const TileVariations& v, int w, int* height = nullptr) const {
        std::vector<draw::Rect> out;
        const int pad = scale(6), c = cell();
        int x = pad, y = titleH();
        for (size_t k = 0; k < v.subs.size(); ++k) {
            if (int(k) == v.common && k > 0) x += gap() * 2;  // (the rare ones after a gap)
            if (x + c > w - pad && x > pad) x = pad, y += c + gap();
            out.push_back({x, y, x + c, y + c});
            x += c + gap();
        }
        if (height) *height = (out.empty() ? y : out.back().bottom) + pad;
        return out;
    }
    // The height the strip needs at width `w` (0: not shown).
    int heightFor(int w) const {
        int x, y;
        if (!variationsTile(x, y)) return 0;
        const TileVariations v = tileVariations(*appState().ed, mapTileAt(*appState().ed, x, y));
        if (v.subs.size() < 2) return titleH() + scale(6);
        int h = 0;
        boxes(v, w, &h);
        return h;
    }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kPanel);
        c.fill({0, 0, width(), 1}, kBorderStrong);
        int x, y;
        if (!variationsTile(x, y)) return;
        const Editor& ed = *appState().ed;
        const uint16_t t = mapTileAt(ed, x, y);
        const TileVariations v = tileVariations(ed, t);
        wchar_t title[96];
        if (v.subs.size() < 2) std::swprintf(title, 96, shellTr(L"Tile %d, %d has no variations").c_str(), x, y);
        else std::swprintf(title, 96, shellTr(L"Variations of tile %d, %d").c_str(), x, y);
        c.text({scale(6), 0, width() - scale(6), titleH()}, title, kDim, draw::kLeft | draw::kMiddle | draw::kEllipsis);
        if (v.subs.size() < 2) return;
        tilesPixelsTrim();
        const std::vector<draw::Rect> b = boxes(v, width());
        for (size_t k = 0; k < b.size(); ++k) {
            draw::Rect src;
            draw::Bitmap& px = tilesPixels(uint16_t((t & 0xFFF0) | v.subs[k]), src);
            const draw::Rect in{b[k].left + 1, b[k].top + 1, b[k].right - 1, b[k].bottom - 1};
            c.bitmap(px, src, in, true);
            const bool used = v.subs[k] == (t & 15);
            if (used) c.frame(b[k], kAccent, std::max(2, scale(2)));
            else c.frame(b[k], int(k) == hover ? kText : kBorderStrong, 1);
        }
    }
    int at(draw::Point p) const {
        int x, y;
        if (!variationsTile(x, y)) return -1;
        const TileVariations v = tileVariations(*appState().ed, mapTileAt(*appState().ed, x, y));
        const std::vector<draw::Rect> b = boxes(v, width());
        for (size_t k = 0; k < b.size(); ++k)
            if (b[k].contains(p)) return int(k);
        return -1;
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Leave) return hover = -1, invalidate(), true;
        const int k = at(e.pt);
        if (e.kind == draw::MouseKind::Move) {
            if (k != hover) hover = k, invalidate();
            return true;
        }
        if (e.kind != draw::MouseKind::Down || e.button != 0 || k < 0) return e.kind == draw::MouseKind::Down;
        int x, y;
        if (!variationsTile(x, y)) return true;
        const TileVariations v = tileVariations(*appState().ed, mapTileAt(*appState().ed, x, y));
        setTileVariation(x, y, v.subs[size_t(k)], (e.mods & draw::kShift) == 0);
        invalidate();
        invalidateMapView();
        return true;
    }
    bool hoverTip() const override { return true; }
    std::wstring tip(draw::Point p) override {
        const int k = at(p);
        if (k < 0) return {};
        int x, y;
        variationsTile(x, y);
        const TileVariations v = tileVariations(*appState().ed, mapTileAt(*appState().ed, x, y));
        wchar_t m[200];
        std::swprintf(m, 200, shellTr(k < v.common ? L"Variation %d (common). Click: the tile, its rect and its stack. Shift+click: only the tile"
                                                   : L"Variation %d (rare). Click: the tile, its rect and its stack. Shift+click: only the tile").c_str(),
                      k + 1);
        return m;
    }
};
VariationsView gVariationsView;
