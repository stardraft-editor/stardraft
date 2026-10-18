// The floating palettes' views (portable, core.inc; the state and its settings: dock_prefs.inc, FloatPalette).
// FloatPaletteView is the root of a palette's own window: the palette's rows from the side bar (an owner box, a section
// box, the search box with its View button, a filter), Dock at the right end of the first row, then the palette's list;
// in Rectangle mode the Tiles panel (`alt`) under Dock. The program gives it the views of the active mode's palette
// (hold): they move from the side bar into the window while it is open, and back when it closes. FloatPaletteNoteView
// takes their place in the side bar: a text and Dock.

// The views of a palette in the window, from the top: rows (a view that fills the row, and a button at its right or
// none), then the list that fills the rest; `alt` shows in place of the rows and the list while altShown.
struct FloatPaletteParts {
    std::vector<std::pair<draw::View*, draw::View*>> rows;
    draw::View* list = nullptr;
    draw::View* alt = nullptr;
};

// A palette window's own slim title strip at the top, in place of the system's title bar (macOS: its buttons are
// hidden, macSetupToolWindow; Windows and Linux: the window has no frame of the system's, and the program draws a line
// around it, drawFrame): a close box at the left, the window's name in the middle, and at the right a collapse box (the
// window becomes the strip alone, and back; a double click on the strip does it too) and a duplicate box (a new panel
// like this one). A drag on the rest moves the window (dragWindow: the program moves it, kept in the main window; on
// Wayland the system moves it: dragAt, the window's hit test). titleH 0: none (the system's title bar). Its height: an
// even strip (24 px at 100%) and the line under it. The base of the floating palette's window and of the palette panels.
struct PanelStripView : draw::View {
    std::wstring name = L"palette";  // (the strip's text and the feedback's name: the window's title)
    int titleH = 0;
    bool drawFrame = false;   // a line around the window (it has no frame of the system's)
    bool collapsed = false;   // the window is the strip alone
    std::function<void()> onClose;      // the close box
    std::function<void()> onDuplicate;  // the duplicate box (none: no box)
    std::function<void()> onCollapse;   // the collapse box, a double click on the strip (none: no box)
    // A drag on the strip: 0 it starts, 1 the mouse moved, 2 it ends (the program moves the window to follow the mouse).
    std::function<void(int phase)> dragWindow;
    // The keys that no view of the window took: the main window's keys (the map's keys, the menus' shortcuts), so that
    // they work while the window has the keyboard (as ScmDraft 2's shortcuts work from its terrain window).
    std::function<bool(const draw::KeyEvent&)> keys;

    // The strip less its bottom line, less 6 px on each side: the close box at the left as far from the edge as from the
    // top; the duplicate box the same at the right, the collapse box left of it (2 px between).
    int boxMargin() const { return scale(6); }
    draw::Rect closeBox() const {
        const int m = boxMargin(), s = titleH - 1 - 2 * m;
        return {m, m, m + s, m + s};
    }
    draw::Rect duplicateBox() const {
        const int m = boxMargin(), s = titleH - 1 - 2 * m;
        return {width() - m - s, m, width() - m, m + s};
    }
    draw::Rect collapseBox() const {
        const int m = boxMargin(), s = titleH - 1 - 2 * m;
        const int right = onDuplicate ? duplicateBox().left - scale(2) : width() - m;
        return {right - s, m, right, m + s};
    }
    // The part of the strip that moves the window (not its boxes).
    bool dragAt(draw::Point p) const {
        if (titleH <= 0 || p.y < 0 || p.y >= titleH || closeBox().contains(p)) return false;
        return !(onDuplicate && duplicateBox().contains(p)) && !(onCollapse && collapseBox().contains(p));
    }
    void paintStrip(draw::Canvas& c) {
        if (titleH <= 0) return;
        c.fill({0, 0, width(), titleH}, kPanel);
        c.line(0, titleH - 1, width(), titleH - 1, kBorder);
        const draw::Rect x = closeBox();  // the close box: an x as the tabs' (core/views_tabs.inc), on kHot under the mouse
        if (hot_ == 1) c.fill(x, kHot);
        // The x: two lines from the box's middle, the size and weight of the tabs' x glyph (7 px, 1 px lines at 100%),
        // exactly in the middle at every scale (a glyph's ink is not where its line's middle is).
        // (The smooth lines land half a pixel right and down of their points, and the x a little more right: measured
        // in snapshots at 100% and 200%; the points are moved back by that.)
        const float cx = (x.left + x.right) / 2.0f - 1.0f, cy = (x.top + x.bottom) / 2.0f - 0.5f, r = scale(7) / 2.0f,
                    lw = std::max(1.0f, scale(10) / 10.0f);
        const draw::Color mark = hot_ == 1 ? kText : kDim;
        c.smoothLine({cx - r, cy - r}, {cx + r, cy + r}, mark, lw);
        c.smoothLine({cx + r, cy - r}, {cx - r, cy + r}, mark, lw);
        // Frames of filled lines (whole pixels at every scale: an outline's line between two pixels smears).
        auto frame = [&](draw::Rect r, draw::Color col) {
            const int t = scale(1);
            c.fill({r.left, r.top, r.right, r.top + t}, col), c.fill({r.left, r.bottom - t, r.right, r.bottom}, col);
            c.fill({r.left, r.top, r.left + t, r.bottom}, col), c.fill({r.right - t, r.top, r.right, r.bottom}, col);
        };
        if (onDuplicate) {  // two squares, the front one over the back one (12 px at 100%), in the middle of the box
            const draw::Rect d = duplicateBox();
            if (hot_ == 2) c.fill(d, kHot);
            const draw::Color dm = hot_ == 2 ? kText : kDim;
            const int side = scale(9), off = scale(3), ox = d.left + (d.width() - side - off) / 2, oy = d.top + (d.height() - side - off) / 2;
            frame({ox + off, oy, ox + off + side, oy + side}, dm);
            const draw::Rect front(ox, oy + off, ox + side, oy + off + side);
            c.fill(front, hot_ == 2 ? kHot : kPanel);
            frame(front, dm);
        }
        if (onCollapse) {  // a bar (collapse), or a square (the window back) while collapsed: 12 px at 100%
            const draw::Rect k = collapseBox();
            if (hot_ == 3) c.fill(k, kHot);
            const draw::Color km = hot_ == 3 ? kText : kDim;
            const int side = scale(12), ox = k.left + (k.width() - side) / 2, oy = k.top + (k.height() - side) / 2;
            if (collapsed) frame({ox + scale(1), oy + scale(1), ox + side - scale(1), oy + side - scale(1)}, km);
            else c.fill({ox, oy + (side - scale(1)) / 2, ox + side, oy + (side - scale(1)) / 2 + scale(1)}, km);
        }
        // The name (the UI's text size) in the middle of the window, as far from the boxes on each side (the wider side's);
        // its capitals in the middle of the strip (kMiddle centers the font's whole line: half a point low; measured at
        // 100% and 200%).
        const int inset = std::max(x.right, width() - (onCollapse ? collapseBox().left : onDuplicate ? duplicateBox().left : width() - x.right)) + scale(6);
        const int up = scale(1) / 2;
        c.text({inset, -up, std::max(inset, width() - inset), titleH - 1 - up}, shellTr(name.c_str()), kText, draw::kCenter | draw::kMiddle | draw::kEllipsis);
    }
    void paintFrame(draw::Canvas& c) {
        if (!drawFrame) return;
        const int t = scale(1);
        c.fill({0, 0, width(), t}, kBorder), c.fill({0, height() - t, width(), height()}, kBorder);
        c.fill({0, 0, t, height()}, kBorder), c.fill({width() - t, 0, width(), height()}, kBorder);
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (titleH <= 0) return false;
        if (dragging_) {
            if (e.kind == draw::MouseKind::Move && dragWindow) dragWindow(1);
            if (e.kind == draw::MouseKind::Up && e.button == 0) {
                dragging_ = false;
                if (draw::Host* h = host()) h->capture(*this, false);
                if (dragWindow) dragWindow(2);
            }
            return true;
        }
        const int hot = closeBox().contains(e.pt) ? 1 : onDuplicate && duplicateBox().contains(e.pt) ? 2 : onCollapse && collapseBox().contains(e.pt) ? 3 : 0;
        if (e.kind == draw::MouseKind::Leave) {
            if (hot_) hot_ = 0, invalidate();
            return false;
        }
        if (hot != hot_ && e.kind != draw::MouseKind::Wheel) hot_ = hot, invalidate();
        if (e.kind == draw::MouseKind::DoubleClick && e.button == 0 && dragAt(e.pt) && onCollapse) return onCollapse(), true;
        if (e.kind == draw::MouseKind::Down && e.button == 0) {
            if (hot == 1) return onClose ? (onClose(), true) : true;
            if (hot == 2) return onDuplicate ? (onDuplicate(), true) : true;
            if (hot == 3) return onCollapse ? (onCollapse(), true) : true;
            if (dragAt(e.pt) && dragWindow) {
                dragging_ = true;
                if (draw::Host* h = host()) h->capture(*this, true);
                dragWindow(0);
                return true;
            }
        }
        return e.pt.y < titleH;
    }
    bool key(const draw::KeyEvent& e) override { return keys && keys(e); }

private:
    int hot_ = 0;  // the box under the mouse: 1 close, 2 duplicate, 3 collapse
    bool dragging_ = false;
};

// The root of a floating palette's window.
struct FloatPaletteView final : PanelStripView {
    stardraft::kit::Button dock{L"Dock", stardraft::kit::Tone::Normal, [this] {
                                    if (onDock) onDock();
                                }};
    std::function<void()> onDock;  // Dock, or the window's close button: the palette goes back to the side bar
    bool altShown = false;  // Rectangle mode: the Tiles panel, not the rows and the list

    FloatPaletteView() {
        add(dock, {});
        onClose = [this] {
            if (onDock) onDock();
        };
    }
    ~FloatPaletteView() override {
        attach(nullptr);  // (at the program's end the host is gone before this view: its children leave no host)
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }
    // The palette's views move here (from the side bar).
    void hold(const FloatPaletteParts& parts) {
        parts_ = parts;
        for (draw::View* v : views()) add(*v, {});
        place();
    }
    // They go back (the caller adds them to the side bar): the window keeps only Dock.
    void release() {
        for (draw::View* v : views())
            if (v->parent() == this) remove(*v);
        parts_ = {};
    }
    bool holds() const { return parts_.list != nullptr; }
    // The view is one of the palette's views in the window.
    bool holds(const draw::View& v) const {
        for (const draw::View* p : views())
            if (p == &v) return p->parent() == this;
        return false;
    }
    const FloatPaletteParts& parts() const { return parts_; }
    void setAltShown(bool on) {
        if (altShown == on) return;
        altShown = on;
        place();
    }

    // The same sizes as the side bar's rows (core/layout.inc: the search row, the View button).
    void place() {
        const int pad = scale(6), rowH = scale(24), btnW = scale(64), w = width(), h = height(), top = titleH;
        dock.setFrame({w - pad - btnW, top + pad, w - pad, top + pad + rowH});
        dock.setVisible(titleH <= 0);  // (with the strip, its close box docks: no Dock button)
        const bool alt = altShown && parts_.alt;
        int y = top + pad;
        for (size_t i = 0; i < parts_.rows.size(); ++i) {
            auto [fill, button] = parts_.rows[i];
            int right = w - pad - (i == 0 && titleH <= 0 ? btnW + pad : 0);  // (Dock is at the right end of the first row)
            if (button) {
                button->setFrame({right - btnW, y, right, y + rowH});
                right -= btnW + pad;
                button->setVisible(!alt);
            }
            if (fill) fill->setFrame({pad, y, std::max(pad, right), y + rowH}), fill->setVisible(!alt);
            y += rowH + pad;
        }
        if (parts_.rows.empty()) y = top + pad + rowH + pad;
        if (parts_.list) parts_.list->setFrame({pad, y, w - pad, std::max(y + 1, h - pad)}), parts_.list->setVisible(!alt);
        const int below = top + pad + rowH + pad;
        if (parts_.alt) parts_.alt->setFrame({pad, below, w - pad, std::max(below + 1, h - pad)}), parts_.alt->setVisible(alt);
        if (collapsed)
            for (draw::View* v : views()) v->setVisible(false);
        invalidate();
    }
    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        paintStrip(c);
    }
    void paintOver(draw::Canvas& c) override { paintFrame(c); }
    std::wstring feedbackName(draw::Point) override { return L"the floating window of the " + name; }

private:
    FloatPaletteParts parts_;
    std::vector<draw::View*> views() const {
        std::vector<draw::View*> out;
        for (auto [fill, button] : parts_.rows) {
            if (fill) out.push_back(fill);
            if (button) out.push_back(button);
        }
        if (parts_.list) out.push_back(parts_.list);
        if (parts_.alt) out.push_back(parts_.alt);
        return out;
    }
};

// A palette panel (New panel like this: a palette's View menu, the duplicate box): a palette of its own in a window of
// its own, of one map tab, shown in the Map workspace while that tab is active. It has the side bar's rows for its type:
// its type and the owner (units and sprites: the players' choice, as the side bar's owner box), the section (units,
// doodads, sprites), the search with the View button (its view, order and sections); then its list. Its type, view,
// order, section and search are its own, and so is its scroll; a pick in it picks in its mode (the mode changes to it).
// The program (sdl_palette_panels.inc) keeps the panels and their windows.
struct PalettePanelView final : PanelStripView {
    stardraft::kit::Dropdown type;     // Terrain, Units, Doodads, Sprites
    stardraft::kit::Dropdown owner;    // the player (units, sprites)
    stardraft::kit::Dropdown sections; // all, then the sections (units, doodads, sprites)
    SearchBox search;
    stardraft::kit::Button viewButton{L"View", stardraft::kit::Tone::Normal, [this] {
                                          if (onView) onView();
                                      }};
    PaletteGridView list;
    Mode mode = Mode::Units;
    PaletteView view;
    PaletteSort sort = PaletteSort::Category;
    std::wstring section;          // the section it shows ("": all)
    std::function<void()> onView;  // the View button: the panel's View menu
    // A mode's floating palette (View > Floating palettes, Make floating): its type is the mode's (no type box), its view
    // and order are the mode's (gViewByMode, gSortByMode), the close box docks it; it shows in every mode. Terrain's holds
    // Rectangle mode's Tiles panel (`alt`) in Terrain mode while Rectangle mode is on.
    bool floating = false;
    draw::View* alt = nullptr;
    bool altShown = false;
    void holdAlt(draw::View* v, bool shown) {
        if (alt != v) {
            if (alt && alt->parent() == this) remove(*alt);
            alt = v;
            if (alt) add(*alt, {});
        }
        if (altShown != shown || alt) altShown = shown && alt, place();
    }

    PalettePanelView() {
        list.ownView = &view;
        for (draw::View* v : std::initializer_list<draw::View*>{&type, &owner, &sections, &search, &viewButton, &list}) add(*v, {});
        search.setPlaceholder(L"Search");
    }
    ~PalettePanelView() override {
        attach(nullptr);
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }
    std::vector<draw::View*> ownViews() { return {&type, &owner, &sections, &search, &viewButton, &list}; }
    bool hasOwner() const { return mode == Mode::Units || mode == Mode::Sprites; }
    bool hasSections() const { return mode != Mode::Terrain; }
    // The same sizes as the side bar's rows (core/layout.inc).
    void place() {
        const int pad = scale(6), rowH = scale(24), btnW = scale(64), w = width(), h = height();
        int y = titleH + pad;
        const int half = std::max(0, w - 3 * pad) / 2;
        type.setVisible(!floating);
        owner.setVisible(hasOwner());
        if (floating) {  // (the mode's palette: the owner alone on its row, as in the side bar)
            if (hasOwner()) owner.setFrame({pad, y, w - pad, y + rowH}), y += rowH + pad;
        } else {
            type.setFrame({pad, y, hasOwner() ? pad + half : w - pad, y + rowH});
            owner.setFrame({pad + half + pad, y, w - pad, y + rowH});
            y += rowH + pad;
        }
        sections.setVisible(hasSections());
        if (hasSections()) sections.setFrame({pad, y, w - pad, y + rowH}), y += rowH + pad;
        search.setFrame({pad, y, std::max(pad, w - pad - btnW - pad), y + rowH});
        viewButton.setFrame({w - pad - btnW, y, w - pad, y + rowH});
        y += rowH + pad;
        list.setFrame({pad, y, w - pad, std::max(y + 1, h - pad)});
        for (draw::View* v : std::initializer_list<draw::View*>{&search, &viewButton, &list}) v->setVisible(!collapsed && !altShown);
        if (!floating) type.setVisible(!collapsed);
        if (collapsed || altShown) owner.setVisible(false), sections.setVisible(false);
        if (alt) {  // (Rectangle mode's Tiles panel in place of the rows and the list)
            const int top = titleH + pad;
            alt->setFrame({pad, top, w - pad, std::max(top + 1, h - pad)});
            alt->setVisible(altShown && !collapsed);
        }
        invalidate();
    }
    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        paintStrip(c);
    }
    void paintOver(draw::Canvas& c) override { paintFrame(c); }
    std::wstring feedbackName(draw::Point) override { return L"a palette panel (" + name + L")"; }
};

// In the side bar while the active mode's palette is in its own window: a text and Dock.
struct FloatPaletteNoteView final : draw::View {
    stardraft::kit::Label text{L"", stardraft::kit::LabelTone::Dim};
    stardraft::kit::Button dock{L"Dock", stardraft::kit::Tone::Normal, [this] {
                                    if (onDock) onDock();
                                }};
    std::function<void()> onDock;

    FloatPaletteNoteView() {
        text.setWrap(true);
        add(text, {});
        add(dock, {});
    }
    ~FloatPaletteNoteView() override {
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }
    // The texts of the palette of `m` (kFloatPalNames).
    void setPalette(Mode m) {
        text.setText(kFloatPalNames[int(m)].note);
        dock.setTip(kFloatPalNames[int(m)].tip);
    }
    void resized() override {
        const int rowH = scale(24), textH = scale(40);
        text.setFrame({0, 0, width(), textH});
        dock.setFrame({0, textH + scale(6), std::min(width(), scale(64)), textH + scale(6) + rowH});
    }
    std::wstring feedbackName(draw::Point) override { return L"the side bar's note of a floating palette"; }
};
