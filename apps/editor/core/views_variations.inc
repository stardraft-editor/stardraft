// The side bar's Variations strip (Terrain mode with a terrain selection, under the terrain list): for each selected
// tile (core/tile_variations.inc: variationsTiles), a picture of each of its variations; the one in use has an accent
// frame, and the rare ones come after a gap. The mouse over a variation shows it on the map (the terrain preview, no
// change); a click puts it on the tile, the other half of its rect and its stack, Shift+click only on the tile. The
// wheel scrolls the strip when its rows need more room than the side bar gives.
struct VariationsView : draw::View {
    int hoverRow = -1, hoverK = -1;
    int scroll = 0;
    bool over = false, overShift = false;  // the mouse is over the strip (at overAt), with Shift held or not
    draw::Point overAt{};
    int shiftTimer = -1;
    static int cell() { return scale(34); }
    static int titleH() { return scale(24); }
    static int labelH() { return scale(18); }
    static int gap() { return scale(4); }
    static int pad() { return scale(6); }
    struct Cell {
        int row = -1, k = -1;
        draw::Rect box;  // (in the content: the scroll not taken off)
    };
    struct Label {
        draw::Rect box;
        std::wstring text;
    };
    // The cells and labels for the width, and the content's height.
    struct Layout {
        std::vector<Cell> cells;
        std::vector<Label> labels;
        int height = 0;
    };
    Layout layout(int w) const {
        Layout l;
        const std::vector<std::pair<int, int>> tiles = variationsTiles();
        if (tiles.empty()) return l;
        const Editor& ed = *appState().ed;
        const int c = cell();
        int y = titleH();
        for (size_t r = 0; r < tiles.size(); ++r) {
            const auto [tx, ty] = tiles[r];
            const TileVariations v = tileVariations(ed, mapTileAt(ed, tx, ty));
            if (tiles.size() > 1) {  // (one tile: the title names it)
                wchar_t t[96];
                if (v.subs.size() < 2) std::swprintf(t, 96, shellTr(L"Tile %d, %d: no variations").c_str(), tx, ty);
                else std::swprintf(t, 96, shellTr(L"Tile %d, %d").c_str(), tx, ty);
                l.labels.push_back({{pad(), y, w - pad(), y + labelH()}, t});
                y += labelH();
            }
            if (v.subs.size() < 2) continue;
            int x = pad();
            for (size_t k = 0; k < v.subs.size(); ++k) {
                if (int(k) == v.common && k > 0) x += gap() * 2;  // (the rare ones after a gap)
                if (x + c > w - pad() && x > pad()) x = pad(), y += c + gap();
                l.cells.push_back({int(r), int(k), {x, y, x + c, y + c}});
                x += c + gap();
            }
            y += c + gap();
        }
        l.height = y + pad();
        return l;
    }
    // The height the strip needs at width `w` (0: not shown).
    int heightFor(int w) const { return variationsTiles().empty() ? 0 : layout(w).height; }
    int maxScroll() const { return std::max(0, layout(width()).height - height()); }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kPanel);
        c.fill({0, 0, width(), 1}, kBorderStrong);
        const std::vector<std::pair<int, int>> tiles = variationsTiles();
        if (tiles.empty()) return;
        const Editor& ed = *appState().ed;
        scroll = std::clamp(scroll, 0, maxScroll());
        const Layout l = layout(width());
        wchar_t title[96];
        const TileVariations first = tileVariations(ed, mapTileAt(ed, tiles[0].first, tiles[0].second));
        if (tiles.size() > 1) std::swprintf(title, 96, shellTr(L"Variations of %d tiles").c_str(), int(tiles.size()));
        else if (first.subs.size() < 2) std::swprintf(title, 96, shellTr(L"Tile %d, %d has no variations").c_str(), tiles[0].first, tiles[0].second);
        else std::swprintf(title, 96, shellTr(L"Variations of tile %d, %d").c_str(), tiles[0].first, tiles[0].second);
        c.pushClip({0, titleH(), width(), height()});
        for (const Label& lb : l.labels) {
            const draw::Rect r{lb.box.left, lb.box.top - scroll, lb.box.right, lb.box.bottom - scroll};
            c.text(r, lb.text, kDim, draw::kLeft | draw::kMiddle | draw::kEllipsis);
        }
        tilesPixelsTrim();
        for (const Cell& e : l.cells) {
            const auto [tx, ty] = tiles[size_t(e.row)];
            const uint16_t t = mapTileAt(ed, tx, ty);
            const TileVariations v = tileVariations(ed, t);
            if (size_t(e.k) >= v.subs.size()) continue;
            const draw::Rect b{e.box.left, e.box.top - scroll, e.box.right, e.box.bottom - scroll};
            if (b.bottom < titleH() || b.top > height()) continue;
            draw::Rect src;
            draw::Bitmap& px = tilesPixels(uint16_t((t & 0xFFF0) | v.subs[size_t(e.k)]), src);
            c.bitmap(px, src, {b.left + 1, b.top + 1, b.right - 1, b.bottom - 1}, true);
            const bool hot = e.row == hoverRow && e.k == hoverK;
            if (v.subs[size_t(e.k)] == (t & 15)) c.frame(b, kAccent, std::max(2, scale(2)));
            else c.frame(b, hot ? kText : kBorderStrong, hot ? std::max(2, scale(2)) : 1);
        }
        c.popClip();
        c.text({pad(), 0, width() - pad(), titleH()}, title, kDim, draw::kLeft | draw::kMiddle | draw::kEllipsis);
    }
    // The cell under `p` (the view's coordinates), or none (row -1).
    Cell at(draw::Point p) const {
        if (p.y < titleH()) return {};
        for (const Cell& e : layout(width()).cells)
            if (e.box.contains({p.x, p.y + scroll})) return e;
        return {};
    }
    // The tile and the variation of a cell: false when it is not one now.
    static bool cellTile(const Cell& e, int& tx, int& ty, int& sub) {
        const std::vector<std::pair<int, int>> tiles = variationsTiles();
        if (e.row < 0 || size_t(e.row) >= tiles.size()) return false;
        const Editor& ed = *appState().ed;
        tx = tiles[size_t(e.row)].first, ty = tiles[size_t(e.row)].second;
        const TileVariations v = tileVariations(ed, mapTileAt(ed, tx, ty));
        if (e.k < 0 || size_t(e.k) >= v.subs.size()) return false;
        sub = v.subs[size_t(e.k)];
        return true;
    }
    void hoverAt(draw::Point p, unsigned mods) {
        over = true, overAt = p, overShift = (mods & draw::kShift) != 0;
        watchShift();
        const Cell e = at(p);
        if (e.row != hoverRow || e.k != hoverK) hoverRow = e.row, hoverK = e.k, invalidate();
        int tx, ty, sub;
        setVariationPreview(cellTile(e, tx, ty, sub) ? tileVariationCells(*appState().ed, tx, ty, sub, (mods & draw::kShift) == 0)
                                                     : std::vector<Editor::TileAt>{});
    }
    // The preview again for the strip as it is now (its tiles changed: an undo, another selection, another map).
    void refresh() {
        if (over) hoverAt(overAt, overShift ? unsigned(draw::kShift) : 0u);
        else setVariationPreview({});
    }
    // While the mouse is over the strip: Shift held or let go changes the preview (only the tile, or its rect and stack)
    // with no mouse move (a modifier key alone is no key event here).
    void watchShift() {
        draw::Host* h = host();
        if (!h || shiftTimer >= 0) return;
        shiftTimer = h->timer(100, [this] {
            shiftTimer = -1;
            if (!over || !host()) return;
            const bool shift = (host()->modifiers() & draw::kShift) != 0;
            if (shift != overShift) hoverAt(overAt, shift ? unsigned(draw::kShift) : 0u);
            else watchShift();
        });
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Leave) {
            over = false;
            if (shiftTimer >= 0 && host()) host()->cancelTimer(shiftTimer);
            shiftTimer = -1;
            hoverRow = hoverK = -1;
            setVariationPreview({});
            invalidate();
            return true;
        }
        if (e.kind == draw::MouseKind::Wheel) {
            const int before = scroll;
            scroll = std::clamp(scroll - int(e.wheel * cell()), 0, maxScroll());
            if (scroll != before) invalidate(), hoverAt(e.pt, e.mods);
            return true;
        }
        if (e.kind == draw::MouseKind::Move) return hoverAt(e.pt, e.mods), true;
        if (e.kind != draw::MouseKind::Down) return false;
        int tx, ty, sub;
        if (e.button != 0 || !cellTile(at(e.pt), tx, ty, sub)) return true;
        setVariationPreview({});
        setTileVariation(tx, ty, sub, (e.mods & draw::kShift) == 0);
        invalidate();
        invalidateMapView();
        return true;
    }
    bool hoverTip() const override { return true; }
    std::wstring tip(draw::Point p) override {
        const Cell e = at(p);
        int tx, ty, sub;
        if (!cellTile(e, tx, ty, sub)) return {};
        const TileVariations v = tileVariations(*appState().ed, mapTileAt(*appState().ed, tx, ty));
        wchar_t m[200];
        std::swprintf(m, 200,
                      shellTr(e.k < v.common ? L"Variation %d (common). Click: the tile, its rect and its stack. Shift+click: only the tile"
                                             : L"Variation %d (rare). Click: the tile, its rect and its stack. Shift+click: only the tile")
                          .c_str(),
                      e.k + 1);
        return m;
    }
};
VariationsView gVariationsView;
