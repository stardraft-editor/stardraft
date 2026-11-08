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
    kitui::ItemDrag pinDrag_;  // a pinned doodad's drag (its row), among the pins
    int pinTo_ = -1;           // ... the pinned row it goes to
    Mode mode = Mode::Units;   // the mode of the rows (their pictures are of it)
    bool second = false;       // the second palette: no pick of its own shows (its picks are its own list)
    std::vector<int>* chosen = nullptr;  // the picks it shows (the first palette: the shell's; the second: its own)
    PanelState* state = nullptr;  // its panel (panel_state.inc): its picks show, whichever panel is active
    std::wstring searchWords;  // the search's text (lower case): its words are in the accent in a list
    // The item's picture in `box` (false: none; a terrain then gets an empty swatch).
    std::function<bool(draw::Canvas&, Mode, int value, bool sprites, draw::Rect box)> picture;
    // The paint's time for new pictures: start (true), end (false; true back: pictures are left, the view paints again).
    std::function<bool(bool start)> pictureBudget;
    // A left click on an item (row index), with the keys held.
    std::function<void(int row, unsigned mods)> picked;
    // A right click (the row under it, or -1), at `pt` (the view's coordinates).
    std::function<void(int row, draw::Point pt)> context;

    PaletteGridView() {
        add(bar_, draw::Rect{});
        bar_.onScroll = [this](int p) { scrollTo(p); };
    }
    ~PaletteGridView() override {
        if (againTimer_ && againHost_) againHost_->cancelTimer(againTimer_);  // (the host the timer is on: a floating palette has left it by now)
        remove(bar_);
    }

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
        // (paletteLayout's rows go down: their tops and bottoms do not get smaller; the first row under the point, then
        // the cells of its line)
        const size_t n = std::min(rows.size(), rects_.size());
        const auto first = std::partition_point(rects_.begin(), rects_.begin() + std::ptrdiff_t(n), [&](const draw::Rect& r) { return r.bottom <= at.y; });
        for (size_t i = size_t(first - rects_.begin()); i < n && rects_[i].top <= at.y; ++i)
            if (rows[i].value >= 0 && rects_[i].contains(at)) return int(i);
        return -1;
    }
    // The content's place of row `i` (before scrolling), for the View menu's sections.
    draw::Rect rowRect(int i) const { return i >= 0 && i < int(rects_.size()) ? rects_[size_t(i)] : draw::Rect{}; }
    // The middle of the item labelled `label` in the view's coordinates (a snapshot's click: --click pal=<label>);
    // false: no such item, or it does not show.
    bool itemCenter(const std::wstring& label, draw::Point& at) const {
        const int viewH = height() - paletteFooterHeight(view());
        for (size_t i = 0; i < rows.size() && i < rects_.size(); ++i)
            if (rows[i].value >= 0 && rows[i].label == label) {
                const draw::Rect& r = rects_[i];
                at = {(r.left + r.right) / 2, (r.top + r.bottom) / 2 - scroll_};
                return at.y >= 0 && at.y < viewH && (!bar_.shown() || at.x < bar_.frame().left);
            }
        return false;
    }

    void resized() override { layout(); }
    void paint(draw::Canvas& c) override {
        if (pictureBudget) pictureBudget(true);
        struct BudgetEnd {  // (at each way out of the paint: the pictures left come in a next frame)
            PaletteGridView& v;
            ~BudgetEnd() {
                if (v.pictureBudget && v.pictureBudget(false) && v.host() && !v.againTimer_)
                    v.againHost_ = v.host(), v.againTimer_ = v.host()->timer(1, [&v = v] { v.againTimer_ = 0, v.againHost_ = nullptr, v.invalidate(); });
            }
        } budgetEnd{*this};
        const MutedAccent muted(state && state != gActivePanel);  // (no focus: gray)
        const PanelScope scope(state);  // (its panel's picks)
        const bool own = state != nullptr;
        const std::vector<int>* chosen = own ? &shellPaletteChosen() : this->chosen;
        const AppState& app = shellState();
        const PaletteView& v = view();
        const int viewH = height() - paletteFooterHeight(v);
        c.fill({0, 0, width(), height()}, kPanel);
        c.pushClip({0, 0, width(), viewH});
        const bool units = app.ed && mode != Mode::Locations;  // the picked item shows
        const int pick = second && !own ? -1 : app.palette;
        if (wordsOf_ != searchWords) {  // (the search's words: once for each search, not at each paint)
            wordsOf_ = searchWords, words_.clear();
            std::wistringstream in(searchWords);
            for (std::wstring w; in >> w;) words_.push_back(w);
        }
        const std::vector<std::wstring>& words = words_;
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
            // (a tool that does not use the pick, Bridge, Ramp, Jumble: the pick shows faded, a frame only)
            const bool faded = mode == Mode::Terrain && terrainToolIgnoresPick(app.toolByMode[int(Mode::Terrain)]) && (isPick || isChosen);
            if (faded) {
                const draw::Rect f{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
                if (int(i) == hover_) c.fill(f, kRowHot);
                c.frame(f, kBorder, scale(1));
            } else if (isPick || isChosen || int(i) == hover_) {
                const draw::Rect f{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
                if (isPick) kitpaint::drawListRow(c, f, true, false);
                else c.fill(f, isChosen ? kSel : kRowHot);
                // (several picked: each of them framed in the light accent, so that the set shows at a glance)
                if (isChosen && units && chosen->size() > 1) c.frame(f, kAccentBright, scale(2));
            }
            if (pinned && i >= 1 && i <= size_t(pinned)) {  // a pinned doodad's copy at the top: the pin at its right
                c.icon({r.right - scale(22), r.top, r.right - scale(6), r.bottom}, int(UiIcon::Pin), kDim, scale(16));
            }
            draw::Rect box = r;
            if (v.grid) box = {r.left + scale(3), r.top + scale(3), r.right - scale(3), r.bottom - scale(3)};
            else {  // a square for a unit or doodad; terrain swatches are wide, so twice as wide
                const int bw = (r.bottom - r.top) * (mode == Mode::Terrain ? 2 : 1);
                box = {r.left + scale(4), r.top + scale(2), r.left + scale(4) + bw, r.bottom - scale(2)};
            }
            if (v.grid && !(isPick || isChosen || int(i) == hover_))  // (a grid: each cell a quiet tile, so the pictures of any shape sit in a grid)
                c.roundBox({r.left + scale(2), r.top + scale(2), r.right - scale(2), r.bottom - scale(2)}, kCtl, draw::kNone, float(scale(4)));
            const bool drawn = picture && picture(c, mode, e.value, v.grid && v.sprites, box);
            if (!drawn && mode == Mode::Terrain) c.roundBox(box, draw::rgb(6, 6, 10), kBorder, float(scale(3)));  // nothing to show (Space): an empty swatch
            if (!v.grid) {
                const draw::Rect t{box.right + scale(8), r.top, r.right - scale(4), r.bottom};
                if (words.empty()) c.text(t, e.label, kText, draw::kMiddle | draw::kEllipsis);
                else drawMatched(c, t, e.label, words, kText);
            }
            if (mode == Mode::Terrain && app.ed && std::ranges::find(app.ed->terrainMask(), uint16_t(e.value)) != app.ed->terrainMask().end())
                maskMarker(c, r);  // (in the mask: paletteMaskToggle)
        }
        if (app.ed && rows.empty())  // nothing agrees with the search: only None, dim, where the first row goes
            c.text({scale(6), 0, width() - scale(6), scale(30)}, L"None", kDim, draw::kMiddle);
        if (keys_.keys && hasFocus() && isItem(keys_.at)) {  // the item with the keyboard focus
            draw::Rect r = rects_[size_t(keys_.at)];
            r.top -= scroll_, r.bottom -= scroll_;
            paintFocusOutline(c, {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1});
        }
        if (pinDrag_.dragging() && pinTo_ >= 1 && pinTo_ != pinDrag_.item) {  // a pin's drag: the line where it goes
            draw::Rect r = rowRect(pinTo_);
            r.top -= scroll_, r.bottom -= scroll_;
            kitpaint::drawDropLine(c, r.left, r.right, pinTo_ < pinDrag_.item ? r.top + scale(1) : r.bottom - scale(1));
        }
        c.popClip();
        if (const int fh = paletteFooterHeight(v)) {  // grid: the hovered (or picked) item's name
            draw::Rect f{0, viewH, width(), viewH + fh};
            c.fill(f, kCtl);
            const int show = hover_ >= 0 && hover_ < int(rows.size()) ? rows[size_t(hover_)].value : units ? pick : -1;
            f.left += scale(6);
            if (app.ed && show >= 0) c.text(f, paletteItemNameOf(mode, show), kText, draw::kMiddle | draw::kEllipsis);  // (the rows' mode: GitHub #101)
            else if (app.ed) {  // (nothing under the mouse or picked: how many the grid has, dim)
                size_t n = 0;
                for (size_t k = 0; k < rows.size(); ++k) n += rows[k].value >= 0 && !(pinned && k >= 1 && k <= size_t(pinned));
                static const wchar_t* const kinds[] = {L" terrains", L" units", L" doodads", L" locations", L" sprites", L" layers"};
                c.text(f, std::to_wstring(n) + tr(kinds[std::clamp(int(mode), 0, 5)]), kFaint, draw::kMiddle | draw::kEllipsis);
            }
        }
    }
    bool mouse(const draw::MouseEvent& e) override {
        // A press makes its panel the active one (another mode's list in the side bar: its pick does, as the side bar's
        // layout follows the mode).
        if (e.kind == draw::MouseKind::Down && state && state != gActivePanel && (state->mode == appState().mode || !isDockedPanel(state))) activatePanel(state);
        switch (e.kind) {
        case draw::MouseKind::Move: {
            const int h = rowAt(e.pt);
            if (h != hover_) hover_ = h, invalidate();
            if (pinDrag_.item >= 0 && pinDrag_.move(*this, e.pt)) {  // a pin's drag: the pinned row it goes to
                const int to = std::clamp(h >= 1 && h <= pinned ? h : e.pt.y + scroll_ < rowRect(1).top ? 1 : pinned, 1, std::max(1, pinned));
                if (to != pinTo_) pinTo_ = to, invalidate();
            }
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
            keys_.pressed();
            if (h >= 0) keys_.at = h;
            if (h >= 0 && picked) picked(h, e.mods);
            // (a pinned doodad's copy at the top: a drag moves it among the pins, as Move up and Move down)
            if (e.kind == draw::MouseKind::Down && mode == Mode::Doodads && pinned > 1 && h >= 1 && h <= pinned) pinDrag_.press(*this, h, e.pt), pinTo_ = h;
            invalidate();
            return true;
        }
        case draw::MouseKind::Up:
            if (e.button == 0 && pinDrag_.item >= 0) {
                const bool dragged = pinDrag_.dragging();
                const int from = pinDrag_.release(*this, e.pt), to = std::exchange(pinTo_, -1);
                if (dragged && from >= 1 && to >= 1 && to != from && from < int(rows.size())) {
                    const int value = rows[size_t(from)].value;
                    const std::vector<uint16_t> shown = paletteShownDoodads(rows);
                    doodadPinMoveBy(value, to - from, shown);
                }
                invalidate();
                return true;
            }
            if (e.button != 1) return false;
            if (context) context(rowAt(e.pt), e.pt);
            return true;
        default: return false;
        }
    }
    void captureLost() override {
        if (pinDrag_.armed()) pinDrag_.cancel(*this), pinTo_ = -1, invalidate();
    }
    std::wstring tip(draw::Point p) override {  // (Report feedback names the entry under the mouse)
        const int h = rowAt(p);
        return h >= 0 ? rows[size_t(h)].label : std::wstring();
    }

    // The keyboard: the list is one Tab stop. Tab puts the focus on the picked item (else the first); the arrow keys
    // move it (in a grid, Up and Down go to the item above and below), Home and End to the first and the last item, and
    // Enter and Space pick the item, as a click does.
    bool focusable() const override {
        for (const PaletteRow& r : rows)
            if (r.value >= 0) return true;
        return false;
    }
    bool focusOnClick() const override { return false; }  // (a pick leaves the keyboard on the map)
    void focusChanged(bool) override { invalidate(); }
    void tabbedTo(bool) override {
        const PanelScope scope(state);
        const AppState& app = shellState();
        const bool showsPick = !(second && !state);  // (the second palette shows no pick of its own)
        int first = -1, pick = -1;
        for (int i = 0; i < int(rows.size()); ++i)
            if (isItem(i)) {
                if (first < 0) first = i;
                if (pick < 0 && showsPick && rows[size_t(i)].value == app.palette) pick = i;
            }
        keys_.at = pick >= 0 ? pick : first, keys_.keys = true;
        showFocused();
    }
    bool key(const draw::KeyEvent& e) override {
        if (!isItem(keys_.at)) return false;
        if (pressKey(e)) {
            const int row = keys_.at;
            keys_.keys = true;
            if (picked) pressFromKeys([this, row] { picked(row, 0); });
            invalidate();
            return true;
        }
        if (!e.down || (e.mods & (draw::kCtrl | draw::kAlt | draw::kSuper))) return false;
        int next = -1;
        switch (e.key) {
        case draw::Key::Left:
        case draw::Key::Right: next = itemAfter(keys_.at, e.key == draw::Key::Left ? -1 : 1); break;
        case draw::Key::Up:
        case draw::Key::Down: next = itemBelow(keys_.at, e.key == draw::Key::Up); break;
        case draw::Key::Home: next = itemAfter(-1, 1); break;
        case draw::Key::End: next = itemAfter(int(rows.size()), -1); break;
        default: return false;
        }
        if (next >= 0) keys_.at = next, keys_.keys = true, showFocused();
        return true;
    }
    std::wstring focusedItem() const override { return isItem(keys_.at) ? rows[size_t(keys_.at)].label : std::wstring(); }

private:
    int againTimer_ = 0;  // (pictures left by a paint: the timer of the next one)
    draw::Host* againHost_ = nullptr;  // (the host that timer is on)
    int px(int v100) const { return scale(v100); }
    bool isItem(int i) const { return i >= 0 && i < int(rows.size()) && i < int(rects_.size()) && rows[size_t(i)].value >= 0; }
    // The next item after row `i` (dir -1: before it), or -1.
    int itemAfter(int i, int dir) const {
        for (int k = i + dir; k >= 0 && k < int(rows.size()); k += dir)
            if (isItem(k)) return k;
        return -1;
    }
    // The item in the line above row `i` (else below it) nearest to it from left to right, or -1.
    int itemBelow(int i, bool above) const {
        const draw::Rect at = rects_[size_t(i)];
        const int cx = (at.left + at.right) / 2;
        int best = -1, bestTop = 0, bestDx = 0;
        for (int k = 0; k < int(rows.size()); ++k) {
            if (!isItem(k)) continue;
            const draw::Rect r = rects_[size_t(k)];
            if (above ? r.top >= at.top : r.top <= at.top) continue;
            const int dx = std::abs((r.left + r.right) / 2 - cx);
            const bool nearer = best < 0 || (above ? r.top > bestTop : r.top < bestTop) || (r.top == bestTop && dx < bestDx);
            if (nearer) best = k, bestTop = r.top, bestDx = dx;
        }
        return best;
    }
    // The item with the focus scrolls into view.
    void showFocused() {
        if (isItem(keys_.at)) reveal(rows[size_t(keys_.at)].value);
        invalidate();
    }
    ItemFocus keys_;  // the item with the keyboard focus (an index into rows)
    kit::ScrollBar bar_{kit::Axis::Vertical};
    std::vector<draw::Rect> rects_;
    int scroll_ = 0, contentH_ = 0, hover_ = -1;
    std::wstring wordsOf_;  // (paint: the search that words_ has the words of)
    std::vector<std::wstring> words_;
};

// The pie palette's ring (core/palette_view.inc: pieTerrains, pieSliceAt): ringSize() px wide, the ring at the top
// (its middle ringSize() / 2 down) and a row for the name under it; the ring is wider with more terrains (radius). Its slices go clockwise from the top, each filled
// with its terrain's picture (the host's: 8 x 8 tiles), all at one size (32 px a tile), and a terrain that stands on
// another ("X (Y)") has Y as a band at the slice's outer end; the hot one has an accent rim. Each slice has its terrain's name when every
// name fits in its slice; the row under the ring has the hot slice's name (or what letting go does). The mouse over it
// picks the hot slice.
struct PieView : draw::View {
    std::vector<uint16_t> items;
    int hot = -1;
    std::function<draw::Bitmap*(uint16_t terrain, int tiles)> picture;  // tiles x tiles tiles (an even number)
    std::function<uint16_t(uint16_t terrain)> ground;  // the terrain it stands on (0: none)
    bool namesInSlices = false;  // (at the last paint: every name fit in its slice)
    int radius = 0;              // the ring's outer radius (the host sets it as the map has room; 0: pieRadius for the items)

    int ringRadius() const { return radius > 0 ? radius : pieRadius(int(items.size())); }
    int ringSize() const { return 2 * ringRadius() + scale(8); }

    static int labelHeight() { return scale(34); }
    // The ring's middle, in the view's coordinates.
    draw::Point middle() const { return {width() / 2, ringSize() / 2}; }
    // The slice under `p` (the view's coordinates).
    int sliceAt(draw::Point p) const { return pieSliceAt(p.x - middle().x, p.y - middle().y, int(items.size())); }
    void track(draw::Point p) {
        const int h = sliceAt(p);
        if (h != hot) hot = h, invalidate();
    }
    std::function<void()> onClick;  // a left click: the slice under it is picked (the ring opened from the menu)
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && e.button == 0 && onClick) return track(e.pt), onClick(), true;
        if (e.kind != draw::MouseKind::Move) return false;
        track(e.pt);
        return true;
    }
    std::wstring terrainName(uint16_t t) const {
        const AppState& app = shellState();
        return app.ed ? widen(app.ed->terrainName(t)) : std::wstring();
    }
    void paint(draw::Canvas& c) override {
        constexpr double kTurn = 2 * 3.14159265358979323846;
        using PointF = draw::Canvas::PointF;
        const int n = int(items.size());
        const float R = float(ringRadius()), r0 = float(pieHole()), cx = float(middle().x), cy = float(middle().y);
        const double step = n ? kTurn / n : kTurn;
        auto at = [&](double a, float rad) { return PointF{cx + float(std::sin(a)) * rad, cy - float(std::cos(a)) * rad}; };
        c.smoothEllipse({cx - R, cy - R}, 2 * R, 2 * R, kPanel);
        // Each slice: its terrain's picture from the hole to the band (or the rim), the ground's in the band, in thin
        // pieces along the arc. Every picture has one size on the screen (8 tiles of 32 px at 100 %), its middle at the
        // middle of its part's box.
        const int parts = std::max(2, int(96 / std::max(1, n)));
        const float band = std::max(float(scale(28)), (R - r0) / 4), tilePx = float(scale(32));  // (the ground's band: a quarter of the slice)
        struct Box {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        };
        auto boxOf = [&](double a0, float ra, float rb) {
            Box r;
            for (int j = 0; j <= parts; ++j)
                for (const float rad : {ra, rb}) {
                    const PointF q = at(a0 + step * j / parts, rad);
                    r.x0 = std::min(r.x0, q.x), r.y0 = std::min(r.y0, q.y), r.x1 = std::max(r.x1, q.x), r.y1 = std::max(r.y1, q.y);
                }
            return r;
        };
        // The pictures' size in tiles: the biggest part's box with a tile to spare (the parts of one slice are alike, so
        // the first slice's tell; 8 at least), even (terrainPicture's pairs of halves).
        int tiles = 8;
        if (n > 0) {
            const Box b = boxOf(-step / 2, r0, R);
            tiles = std::max(tiles, int(std::ceil(std::max(b.x1 - b.x0, b.y1 - b.y0) / tilePx)) + 1);
            const Box s = boxOf(-step / 2 + kTurn / 4, r0, R);  // (a slice at the side: its box is the other way round)
            tiles = std::max(tiles, int(std::ceil(std::max(s.x1 - s.x0, s.y1 - s.y0) / tilePx)) + 1);
            tiles = (tiles + 1) / 2 * 2;
        }
        auto fillPart = [&](uint16_t terrain, double a0, float ra, float rb) {
            draw::Bitmap* b = picture ? picture(terrain, tiles) : nullptr;
            if (!b || b->width() <= 0 || b->height() <= 0 || rb <= ra) return;
            const Box box = boxOf(a0, ra, rb);
            const float span = tilePx * float(tiles), mx = (box.x0 + box.x1) / 2, my = (box.y0 + box.y1) / 2;
            auto uv = [&](PointF q) { return PointF{0.5f + (q.x - mx) / span, 0.5f + (q.y - my) / span}; };
            for (int j = 0; j < parts; ++j) {
                const double a = a0 + step * j / parts, e = a0 + step * (j + 1) / parts + (j + 1 < parts ? 0.002 : 0);
                const PointF q[4] = {at(a, ra), at(a, rb), at(e, rb), at(e, ra)};
                const PointF t[4] = {uv(q[0]), uv(q[1]), uv(q[2]), uv(q[3])};
                c.bitmapQuad(*b, q, t);
            }
        };
        for (int k = 0; k < n; ++k) {
            const double a0 = k * step - step / 2;
            const uint16_t g = ground ? ground(items[size_t(k)]) : 0;
            fillPart(items[size_t(k)], a0, r0, g ? R - band : R);
            if (!g) continue;
            fillPart(g, a0, R - band, R);
            c.smoothArc({cx - (R - band), cy - (R - band)}, 2 * (R - band), 2 * (R - band), float(a0 * 360 / kTurn) - 90,
                        float(step * 360 / kTurn), kBorderStrong, 1.5f);  // (the border between the two)
        }
        for (int k = 0; k < n; ++k) {  // the lines between the slices
            const double a = k * step - step / 2;
            c.smoothLine(at(a, r0), at(a, R), kBorderStrong, 1.5f);
        }
        c.smoothArc({cx - R, cy - R}, 2 * R, 2 * R, 0, 360, kBorderStrong, 1.5f);
        c.smoothEllipse({cx - r0 + 1, cy - r0 + 1}, 2 * r0 - 2, 2 * r0 - 2, kBg);
        c.smoothArc({cx - r0, cy - r0}, 2 * r0, 2 * r0, 0, 360, kBorderStrong, 1.5f);
        if (hot >= 0 && hot < n) {  // the hot slice: an accent rim (over the hole's border)
            const double a0 = hot * step - step / 2, a1 = a0 + step;
            const float w = float(scale(3));
            c.smoothLine(at(a0, r0), at(a0, R), kAccent, w);
            c.smoothLine(at(a1, r0), at(a1, R), kAccent, w);
            const float deg0 = float(a0 * 360 / kTurn) - 90, sweep = float(step * 360 / kTurn);
            c.smoothArc({cx - R, cy - R}, 2 * R, 2 * R, deg0, sweep, kAccent, w);
            c.smoothArc({cx - r0, cy - r0}, 2 * r0, 2 * r0, deg0, sweep, kAccent, w);
        }
        // The names in the slices, each on a plate at the middle of its slice, when no plate touches the next one and
        // each is inside the ring.
        const float rm = (r0 + R) / 2 + scale(10);
        const int pad = scale(6), lineH = scale(20), gap = scale(2);
        std::vector<draw::Rect> plates;
        for (int k = 0; k < n; ++k) {
            const PointF q = at(k * step, rm);
            const int w = c.measure(terrainName(items[size_t(k)])) + 2 * pad, x = int(q.x) - w / 2, y = int(q.y) - lineH / 2;
            plates.push_back({x, y, x + w, y + lineH});
        }
        auto inRing = [&](const draw::Rect& r) {
            for (const float x : {float(r.left), float(r.right)})
                for (const float y : {float(r.top), float(r.bottom)})
                    if (std::hypot(x - cx, y - cy) > R - gap) return false;
            return true;
        };
        auto apart = [&](const draw::Rect& a, const draw::Rect& b) {
            return a.right + gap <= b.left || b.right + gap <= a.left || a.bottom + gap <= b.top || b.bottom + gap <= a.top;
        };
        namesInSlices = n > 0;
        for (int k = 0; k < n && namesInSlices; ++k)
            if (!inRing(plates[size_t(k)]) || (n > 1 && !apart(plates[size_t(k)], plates[size_t((k + 1) % n)]))) namesInSlices = false;
        if (namesInSlices)
            for (int k = 0; k < n; ++k) {
                c.fill(plates[size_t(k)], kBg, 210);
                c.text(plates[size_t(k)], terrainName(items[size_t(k)]), k == hot ? kText : kDim, draw::kCenter | draw::kMiddle);
            }
        // The row under the ring: the hot slice's name, or what letting go does.
        const std::wstring label = hot >= 0 && hot < n ? terrainName(items[size_t(hot)]) : std::wstring(L"Let go: no change");
        const int lw = std::min(width(), c.measure(label) + 4 * pad), lx = (width() - lw) / 2, ly = ringSize() + (labelHeight() - scale(26)) / 2;
        c.roundBox({lx, ly, lx + lw, ly + scale(26)}, kPanel, kBorderStrong, float(scale(stardraft::kit::kRadius)));
        c.text({lx + pad, ly, lx + lw - pad, ly + scale(26)}, label, hot >= 0 ? kText : kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
    }
};
