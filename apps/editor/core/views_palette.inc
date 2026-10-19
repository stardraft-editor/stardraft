// The palette's view and the pie palette on the Canvas (portable, core.inc): the SDL program's palettes (sdl_panels.inc)
// and its pie (sdl_main_view.inc). The model is core/palette_view.inc (the views, the View menu, the rows, the layout);
// the Windows palette (main.cpp: paintUnitPalette) draws the same rows with its own window and scroll bar.

// The palette: the rows as a list or a grid (the mode's view: gViewByMode), the headings with a line after their name,
// the pick, the picks of a spray, the second terrain (a dashed frame and "Shift"), the hovered row, the pin of a pinned
// doodad's copy at the top, and in a grid the footer with the hovered (or picked) item's name. The host gives the pictures
// (picture), the picks (picked) and the menu (context). The second palette is the same view with its own rows.
struct PaletteGridView : draw::View {
    std::vector<PaletteRow> rows;
    int pinned = 0;            // rows 1 to pinned are the pinned doodads' copies
    Mode mode = Mode::Units;   // the mode of the rows (their pictures are of it)
    bool second = false;       // the second palette: no pick of its own shows (its picks are its own list)
    std::vector<int>* chosen = nullptr;  // the picks it shows (the first palette: the shell's; the second: its own)
    std::wstring searchWords;  // the search's text (lower case): its words are in the accent in a list
    // The item's picture in `box` (false: none; a terrain then gets an empty swatch).
    std::function<bool(draw::Canvas&, Mode, int value, bool sprites, draw::Rect box)> picture;
    // A left click on an item (row index), with the keys held.
    std::function<void(int row, unsigned mods)> picked;
    // A right click (the row under it, or -1), at `pt` (the view's coordinates).
    std::function<void(int row, draw::Point pt)> context;

    PaletteGridView() {
        add(bar_, draw::Rect{});
        bar_.onScroll = [this](int p) { scrollTo(p); };
    }
    ~PaletteGridView() override { remove(bar_); }

    const PaletteView* ownView = nullptr;  // a palette panel's own view (else the mode's: gViewByMode)
    const PaletteView& view() const { return ownView ? *ownView : gViewByMode[int(mode)]; }
    // The rows changed (or the view): their places again, the scroll kept where it can be.
    void setRows(std::vector<PaletteRow> r, Mode m, int pins) {
        rows = std::move(r), mode = m, pinned = pins;
        hover_ = -1;
        layout();
    }
    void layout() {
        const int bw = px(kit::kScrollBarW), viewH = std::max(0, height() - paletteFooterHeight(view()));
        contentH_ = paletteLayout(rows, view(), std::max(0, width() - bw), rects_);
        bar_.setFrame({width() - bw, 0, width(), viewH});
        bar_.setRange(contentH_, viewH);
        bar_.setVisible(contentH_ > viewH);
        scrollTo(scroll_);
    }
    void scrollTo(int pos) {
        const int viewH = std::max(0, height() - paletteFooterHeight(view()));
        scroll_ = std::clamp(pos, 0, std::max(0, contentH_ - viewH));
        bar_.setPos(scroll_);
        invalidate();
    }
    int scrollTop() const { return scroll_; }
    // Scrolls as little as it takes for the row of `value` to show whole (no such row: the scroll stays).
    void reveal(int value) {
        const int viewH = std::max(0, height() - paletteFooterHeight(view()));
        for (size_t i = 0; i < rows.size() && i < rects_.size(); ++i)
            if (rows[i].value == value) {
                const draw::Rect& r = rects_[i];
                if (r.top < scroll_) scrollTo(r.top);
                else if (r.bottom > scroll_ + viewH) scrollTo(r.bottom - viewH);
                return;
            }
    }
    bool sameItem(draw::Point a, draw::Point b) const override { return rowAt(a) == rowAt(b); }  // (a double click: one row)
    // The row at `p` (the view's coordinates): an item's index, -1 for a heading, the footer or nothing.
    int rowAt(draw::Point p) const {
        if (p.y >= height() - paletteFooterHeight(view()) || (bar_.shown() && p.x >= bar_.frame().left)) return -1;
        const draw::Point at{p.x, p.y + scroll_};
        for (size_t i = 0; i < rows.size() && i < rects_.size(); ++i)
            if (rows[i].value >= 0 && rects_[i].contains(at)) return int(i);
        return -1;
    }
    // The content's place of row `i` (before scrolling), for the View menu's sections.
    draw::Rect rowRect(int i) const { return i >= 0 && i < int(rects_.size()) ? rects_[size_t(i)] : draw::Rect{}; }

    void resized() override { layout(); }
    void paint(draw::Canvas& c) override {
        const AppState& app = shellState();
        const PaletteView& v = view();
        const int viewH = height() - paletteFooterHeight(v);
        c.fill({0, 0, width(), height()}, kPanel);
        c.pushClip({0, 0, width(), viewH});
        const bool units = app.ed && mode != Mode::Locations;  // the picked item shows
        const int pick = second ? -1 : app.palette;
        std::vector<std::wstring> words;
        {
            std::wistringstream in(searchWords);
            for (std::wstring w; in >> w;) words.push_back(w);
        }
        for (size_t i = 0; app.ed && i < rows.size() && i < rects_.size(); ++i) {
            const PaletteRow& e = rows[i];
            draw::Rect r = rects_[i];
            r.top -= scroll_, r.bottom -= scroll_;
            if (r.bottom < 0 || r.top > viewH) continue;
            if (e.value < 0) {  // a heading: its name, then a line to the right edge
                draw::Rect t = r;
                t.left += scale(6);
                c.text(t, e.label, kText, draw::kMiddle);
                const int mid = (r.top + r.bottom) / 2;
                c.fill({t.left + c.measure(tr(e.label)) + scale(8), mid, r.right - scale(6), mid + 1}, kBorder);
                continue;
            }
            const bool locs = mode == Mode::Locations;  // (a side's list of the locations: the selected ones)
            const bool isPick = (units && e.value == pick) || (locs && e.value == app.ed->selectedLocation());
            const bool isChosen = (units && chosen && std::find(chosen->begin(), chosen->end(), e.value) != chosen->end()) ||
                                  (locs && app.ed->locationSelected(e.value));
            const bool isSecond = units && !second && mode == Mode::Terrain && !app.terrainRect && e.value == app.palette2;  // Shift's
            if (isPick || isChosen || isSecond || int(i) == hover_) {
                const draw::Rect f{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
                if (isPick) kitpaint::drawListRow(c, f, true, false);
                else c.fill(f, isChosen || isSecond ? kSel : kRowHot);
                if (isSecond) {  // a dashed accent frame, and "Shift" at its right end
                    c.frame(f, kAccent, 1, draw::LineStyle::Dot);
                    c.text({f.right - scale(60), f.top, f.right - scale(4), f.bottom}, L"Shift", kAccent, draw::kRight | draw::kMiddle);
                }
            }
            if (pinned && i >= 1 && i <= size_t(pinned)) {  // a pinned doodad's copy at the top: the pin at its right
                draw::FontDesc glyphs;
                glyphs.face = draw::Face::Glyphs, glyphs.px = scale(11);
                const draw::Font of = c.setFont(c.font(glyphs));
                c.text({r.right - scale(22), r.top, r.right - scale(6), r.bottom}, L"\xE718", kDim, draw::kRight | draw::kMiddle);
                c.setFont(of);
            }
            draw::Rect box = r;
            if (v.grid) box = {r.left + scale(3), r.top + scale(3), r.right - scale(3), r.bottom - scale(3)};
            else {  // a square for a unit or doodad; terrain swatches are wide, so twice as wide
                const int bw = (r.bottom - r.top) * (mode == Mode::Terrain ? 2 : 1);
                box = {r.left + scale(4), r.top + scale(2), r.left + scale(4) + bw, r.bottom - scale(2)};
            }
            const bool drawn = picture && picture(c, mode, e.value, v.grid && v.sprites, box);
            if (!drawn && mode == Mode::Terrain) c.roundBox(box, draw::rgb(6, 6, 10), kBorder, float(scale(3)));  // nothing to show (Space): an empty swatch
            if (!v.grid) {
                const draw::Rect t{box.right + scale(8), r.top, r.right - scale(4), r.bottom};
                if (words.empty()) c.text(t, e.label, kText, draw::kMiddle | draw::kEllipsis);
                else drawMatched(c, t, e.label, words, kText);
            }
        }
        if (app.ed && rows.empty())  // nothing agrees with the search: only None, dim, where the first row goes
            c.text({scale(6), 0, width() - scale(6), scale(30)}, L"None", kDim, draw::kMiddle);
        c.popClip();
        if (const int fh = paletteFooterHeight(v)) {  // grid: the hovered (or picked) item's name
            draw::Rect f{0, viewH, width(), viewH + fh};
            c.fill(f, kCtl);
            const int show = hover_ >= 0 && hover_ < int(rows.size()) ? rows[size_t(hover_)].value : units ? pick : -1;
            f.left += scale(6);
            if (app.ed && show >= 0) c.text(f, paletteItemNameOf(mode, show), kText, draw::kMiddle | draw::kEllipsis);  // (the rows' mode: GitHub #101)
        }
    }
    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move: {
            const int h = rowAt(e.pt);
            if (h != hover_) hover_ = h, invalidate();
            return true;
        }
        case draw::MouseKind::Leave:
            if (hover_ >= 0) hover_ = -1, invalidate();
            return true;
        case draw::MouseKind::Wheel:
            if (e.horizontal) return false;
            scrollTo(scroll_ - int(std::lround(draw::wheelDistance(e, float(scale(90))))));
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick: {
            if (e.button != 0) return e.button == 1;
            const int h = rowAt(e.pt);
            if (h >= 0 && picked) picked(h, e.mods);
            invalidate();
            return true;
        }
        case draw::MouseKind::Up:
            if (e.button != 1) return false;
            if (context) context(rowAt(e.pt), e.pt);
            return true;
        default: return false;
        }
    }
    std::wstring tip(draw::Point p) override {  // (Report feedback names the entry under the mouse)
        const int h = rowAt(p);
        return h >= 0 ? rows[size_t(h)].label : std::wstring();
    }

private:
    int px(int v100) const { return scale(v100); }
    kit::ScrollBar bar_{kit::Axis::Vertical};
    std::vector<draw::Rect> rects_;
    int scroll_ = 0, contentH_ = 0, hover_ = -1;
};

// The pie palette's ring (core/palette_view.inc: pieTerrains, pieSliceAt): pieSize() px square, the ring's middle at its
// middle. Its slices go clockwise from the top; the hot one is in the accent; each has its terrain's picture (the host's)
// and name; the middle says what letting go of Space does. The mouse over it picks the hot slice.
struct PieView : draw::View {
    std::vector<uint16_t> items;
    int hot = -1;
    std::function<void(draw::Canvas&, uint16_t terrain, draw::Rect box)> picture;

    // The slice under `p` (the view's coordinates).
    int sliceAt(draw::Point p) const { return pieSliceAt(p.x - width() / 2.0, p.y - height() / 2.0, int(items.size())); }
    void track(draw::Point p) {
        const int h = sliceAt(p);
        if (h != hot) hot = h, invalidate();
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind != draw::MouseKind::Move) return false;
        track(e.pt);
        return true;
    }
    std::wstring terrainName(uint16_t t) const {
        const AppState& app = shellState();
        const IsomTerrainTable* tt = app.ed ? IsomTerrainTable::forEra(app.ed->era()) : nullptr;
        return tt ? widen(tt->terrainName(t)) : std::wstring();
    }
    void paint(draw::Canvas& c) override {
        constexpr double kTurn = 2 * 3.14159265358979323846;
        const int n = int(items.size());
        const float R = float(pieRadius()), r0 = float(pieHole()), cx = width() / 2.0f, cy = height() / 2.0f;
        const double step = n ? kTurn / n : kTurn;
        auto at = [&](double a, float rad) { return draw::Canvas::PointF{cx + float(std::sin(a)) * rad, cy - float(std::cos(a)) * rad}; };
        c.smoothEllipse({cx - R, cy - R}, 2 * R, 2 * R, kPanel);
        if (hot >= 0 && hot < n) {  // the hot slice: an accent tint (the ring's color and the accent, mixed), in thin pieces
            auto mixed = [](draw::Color a, draw::Color b, int k) {  // b over a at k / 255
                draw::Color out = 0;
                for (int s = 0; s < 24; s += 8) out |= draw::Color(((a >> s & 255) * (255 - k) + (b >> s & 255) * k) / 255) << s;
                return out;
            };
            const draw::Color tint = mixed(kPanel, kAccent, 120);
            const double a0 = hot * step - step / 2;
            const int parts = std::max(4, int(step / kTurn * 96));
            for (int k = 0; k < parts; ++k) {
                const double a = a0 + step * k / parts, b = a0 + step * (k + 1) / parts + 0.002;
                const draw::Canvas::PointF q[4] = {at(a, r0), at(a, R), at(b, R), at(b, r0)};
                c.smoothPolygon(q, 4, tint);
            }
        }
        for (int k = 0; k < n; ++k) {  // the lines between the slices
            const double a = k * step - step / 2;
            c.smoothLine(at(a, r0), at(a, R), kBorder);
        }
        c.smoothArc({cx - R, cy - R}, 2 * R, 2 * R, 0, 360, kBorderStrong);
        c.smoothEllipse({cx - r0, cy - r0}, 2 * r0, 2 * r0, kBg);
        c.smoothArc({cx - r0, cy - r0}, 2 * r0, 2 * r0, 0, 360, kBorderStrong);
        // Each slice: its terrain's picture, and its name under it.
        const int box = scale(44);
        for (int k = 0; k < n; ++k) {
            const double a = k * step;
            const float rm = (r0 + R) / 2 + scale(10);
            const draw::Canvas::PointF p = at(a, rm);
            const int x = int(p.x), y = int(p.y) - scale(8);
            if (picture) picture(c, items[size_t(k)], {x - box / 2, y - box / 2, x + box / 2, y + box / 2});
            const int ty = int(p.y) + scale(22);
            c.text({x - scale(60), ty - scale(10), x + scale(60), ty + scale(10)}, terrainName(items[size_t(k)]), k == hot ? kText : kDim,
                   draw::kCenter | draw::kMiddle | draw::kEllipsis);
        }
        const std::wstring mid = hot >= 0 && hot < n ? terrainName(items[size_t(hot)]) : std::wstring(L"Let go: no change");
        c.text({int(cx - r0) + 2, int(cy - r0), int(cx + r0) - 2, int(cy + r0)}, mid, kText, draw::kCenter | draw::kMiddle | draw::kWrap);
    }
};
