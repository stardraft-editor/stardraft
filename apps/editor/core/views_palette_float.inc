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

// The root of a floating palette's window.
struct FloatPaletteView final : draw::View {
    stardraft::kit::Button dock{L"Dock", stardraft::kit::Tone::Normal, [this] {
                                    if (onDock) onDock();
                                }};
    std::function<void()> onDock;  // Dock, or the window's close button: the palette goes back to the side bar
    // The keys that no view of the window took: the main window's keys (the map's keys, the menus' shortcuts), so that
    // they work while the palette's window has the keyboard (as ScmDraft 2's shortcuts work from its terrain window).
    std::function<bool(const draw::KeyEvent&)> keys;
    bool altShown = false;  // Rectangle mode: the Tiles panel, not the rows and the list
    std::wstring name = L"palette";  // (the feedback's name: the window's title)
    // macOS: the window's own slim title strip at the top (the system's title bar shows nothing there): the name, and a
    // close box at the left that docks the palette; a drag on the rest moves the window (dragAt: the window's hit test).
    // 0: none (the system's title bar). Its height: an even strip (24 px at 100%) and the line under it.
    int titleH = 0;
    draw::Rect closeBox() const {
        // The strip less its bottom line, less 3 px on each side; as far from the left edge as from the top.
        const int m = scale(3), s = titleH - 1 - 2 * m;
        return {m, m, m + s, m + s};
    }
    bool dragAt(draw::Point p) const { return titleH > 0 && p.y >= 0 && p.y < titleH && !closeBox().contains(p); }

    FloatPaletteView() { add(dock, {}); }
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
        invalidate();
    }
    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        if (titleH <= 0) return;
        c.fill({0, 0, width(), titleH}, kPanel);
        c.line(0, titleH - 1, width(), titleH - 1, kBorder);
        const draw::Rect x = closeBox();  // the close box: an x as the tabs' (core/views_tabs.inc), on kHot under the mouse
        if (closeHot) c.fill(x, kHot);
        // The name's capitals in the middle of the strip: kMiddle centers the font's whole line (with its descenders'
        // room), which puts them 1 px low, at 100% and at 200% alike (measured; the strip is macOS's only).
        auto middle = [](draw::Rect r) { return draw::Rect(r.left, r.top - 1, r.right, r.bottom - 1); };
        // The x: two lines from the box's middle, the size and weight of the tabs' x glyph (7 px, 1 px lines at 100%),
        // exactly in the middle at every scale (a glyph's ink is not where its line's middle is).
        // (The smooth lines land half a pixel right and down of their points, and the x one more pixel right: measured
        // in snapshots at 100% and 200%; the points are moved back by that.)
        const float cx = (x.left + x.right) / 2.0f - 1.5f, cy = (x.top + x.bottom) / 2.0f - 0.5f, r = scale(7) / 2.0f,
                    lw = std::max(1.0f, scale(10) / 10.0f);
        const draw::Color mark = closeHot ? kText : kDim;
        c.smoothLine({cx - r, cy - r}, {cx + r, cy + r}, mark, lw);
        c.smoothLine({cx + r, cy - r}, {cx - r, cy + r}, mark, lw);
        const draw::Font of = c.setFont(c.font(kitpaint::smallFontDesc()));
        // The name in the middle of the strip (the close box's width kept free on both sides).
        c.text(middle({x.right + scale(6), 0, width() - x.right - scale(6), titleH - 1}), shellTr(name.c_str()), kDim,
               draw::kCenter | draw::kMiddle);
        c.setFont(of);
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (titleH <= 0) return false;
        const bool hot = closeBox().contains(e.pt);
        if (hot != closeHot && e.kind != draw::MouseKind::Wheel) closeHot = hot, invalidate();
        if (e.kind == draw::MouseKind::Leave && closeHot) closeHot = false, invalidate();
        if (e.kind == draw::MouseKind::Down && e.button == 0 && hot) return onDock ? (onDock(), true) : true;
        return e.pt.y < titleH;
    }
    bool closeHot = false;
    bool key(const draw::KeyEvent& e) override { return keys && keys(e); }
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
