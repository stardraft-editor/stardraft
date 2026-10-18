// The floating terrain palette's views (portable, core.inc; the state and its settings: dock_prefs.inc, FloatPalette).
// FloatPaletteView is the root of the palette's own window: a row with the palette's search box, its View button and
// Dock, then the palette's list; in Rectangle mode the Tiles panel under Dock. The program gives it its palette views
// (hold): they move from the side bar into the window while it is open, and back when it closes. FloatPaletteNoteView
// takes their place in the side bar: a text and Dock.

// The root of the floating palette's window.
struct FloatPaletteView final : draw::View {
    stardraft::kit::Button dock{L"Dock", stardraft::kit::Tone::Normal, [this] {
                                    if (onDock) onDock();
                                }};
    std::function<void()> onDock;  // Dock, or the window's close button: the palette goes back to the side bar
    // The keys that no view of the window took: the main window's keys (the map's keys, the menus' shortcuts), so that
    // they work while the palette's window has the keyboard (as ScmDraft 2's shortcuts work from its terrain window).
    std::function<bool(const draw::KeyEvent&)> keys;
    bool tilesShown = false;  // Rectangle mode: the Tiles panel, not the search box and the list

    FloatPaletteView() {
        dock.setTip(L"Put the palette back in the side bar");
        add(dock, {});
    }
    ~FloatPaletteView() override {
        attach(nullptr);  // (at the program's end the host is gone before this view: its children leave no host)
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }
    // The palette's views move here (from the side bar): the search box, the View button, the list, the Tiles panel.
    void hold(draw::View& search, draw::View& viewBtn, draw::View& list, draw::View& tiles) {
        search_ = &search, viewBtn_ = &viewBtn, list_ = &list, tiles_ = &tiles;
        for (draw::View* v : {search_, viewBtn_, list_, tiles_}) add(*v, {});
        place();
    }
    // They go back (the caller adds them to the side bar): the window keeps only Dock.
    void release() {
        for (draw::View* v : {search_, viewBtn_, list_, tiles_})
            if (v && v->parent() == this) remove(*v);
        search_ = viewBtn_ = list_ = tiles_ = nullptr;
    }
    bool holds() const { return list_ != nullptr; }
    void setTilesShown(bool on) {
        if (tilesShown == on) return;
        tilesShown = on;
        place();
    }

    // The same sizes as the side bar's rows (core/layout.inc: the search row, the View button).
    void place() {
        const int pad = scale(6), rowH = scale(24), btnW = scale(64), w = width(), h = height();
        dock.setFrame({w - pad - btnW, pad, w - pad, pad + rowH});
        const int below = pad + rowH + pad;
        if (search_) search_->setFrame({pad, pad, std::max(pad, w - 3 * pad - 2 * btnW), pad + rowH});
        if (viewBtn_) viewBtn_->setFrame({w - 2 * pad - 2 * btnW, pad, w - 2 * pad - btnW, pad + rowH});
        if (list_) list_->setFrame({pad, below, w - pad, std::max(below + 1, h - pad)});
        if (tiles_) tiles_->setFrame({pad, below, w - pad, std::max(below + 1, h - pad)});
        for (draw::View* v : {search_, viewBtn_, list_})
            if (v) v->setVisible(!tilesShown);
        if (tiles_) tiles_->setVisible(tilesShown);
        invalidate();
    }
    void resized() override { place(); }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    bool key(const draw::KeyEvent& e) override { return keys && keys(e); }
    std::wstring feedbackName(draw::Point) override { return L"the floating terrain palette"; }

private:
    draw::View *search_ = nullptr, *viewBtn_ = nullptr, *list_ = nullptr, *tiles_ = nullptr;
};

// In the side bar while the palette is in its own window: a text and Dock.
struct FloatPaletteNoteView final : draw::View {
    stardraft::kit::Label text{L"The terrain palette is in its own window", stardraft::kit::LabelTone::Dim};
    stardraft::kit::Button dock{L"Dock", stardraft::kit::Tone::Normal, [this] {
                                    if (onDock) onDock();
                                }};
    std::function<void()> onDock;

    FloatPaletteNoteView() {
        text.setWrap(true);
        dock.setTip(L"Put the palette back in the side bar");
        add(text, {});
        add(dock, {});
    }
    ~FloatPaletteNoteView() override {
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }
    void resized() override {
        const int rowH = scale(24), textH = scale(40);
        text.setFrame({0, 0, width(), textH});
        dock.setFrame({0, textH + scale(6), std::min(width(), scale(64)), textH + scale(6) + rowH});
    }
    std::wstring feedbackName(draw::Point) override { return L"the side bar's note of the floating terrain palette"; }
};
