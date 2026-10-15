// The Data workspace's top part (portable, core.inc): the strip of .dat tabs with their counts, the changed dots, the back
// and forward arrows, the search box and the Changed toggle above the entry list, and the edge between the list and the
// form. The container is one view (DataWorkspaceView) in a view window; the entry list, the form and the References panel
// stay windows of their own, placed by the container (placeLists). The program (main.cpp: DataWorkspaceProgram) gives the
// tabs, the sizes and what a click does through DataWorkspaceHost. The search text and the Changed toggle are the filter
// (DataListFilter, data_list.inc): the rows are made from it.

// The program's side of the workspace: the tabs, the lists' sizes and places, and what a click does.
class DataWorkspaceHost {
public:
    virtual ~DataWorkspaceHost() = default;
    virtual int tabCount() = 0;                    // the tabs: the .dat files, then the extra ones
    virtual std::wstring tabTitle(int tab) = 0;
    virtual int tabEntryCount(int tab) = 0;        // the entries the tab lists (its count)
    virtual int tabChangedCount(int tab) = 0;      // the changed entries (a green dot when any)
    virtual int tabIcon(int tab) = 0;              // the tab's UI icon (apps/editor/res/ui_icons.h)
    virtual draw::FontDesc countFont() = 0;        // the font of the counts
    virtual int currentTab() = 0;
    virtual bool canBack() = 0;                    // back and forward have a place to go
    virtual bool canForward() = 0;
    virtual void tabClicked(int tab) = 0;          // a tab is clicked: it opens
    virtual void travel(bool back) = 0;            // back (true) or forward: the arrows and the mouse's side buttons
    virtual int listWidth() = 0;                   // the entry list's width in px (the edge is at its right)
    virtual int dragList(int dx) = 0;              // the edge moved by dx px: returns the px it really moved (its clamps)
    virtual void splitDone() = 0;                  // the edge was let go (the dock is saved)
    virtual int refsWidth() = 0;                   // the References panel's width in px (0: none)
    virtual void placeLists(draw::Rect list, draw::Rect form, draw::Rect refs) = 0;  // the windows the program keeps (refs empty: none)
    virtual void filterChanged() = 0;              // the search text or the Changed toggle changed: the rows are made again
    virtual void status(const std::wstring& text) = 0;
};

// The tab strip: one view, painted on the canvas. Its height is the strip's (scale(34)).
class DataTabsView final : public draw::View {
public:
    explicit DataTabsView(DataWorkspaceHost& host) : host_(host) {}

    static int stripH() { return scale(34); }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;

private:
    DataWorkspaceHost& host_;
    int hot_ = -1;                                // the tab under the mouse
    std::vector<draw::Rect> boxes_;               // the tabs as the last paint drew them (the clicks use them)
    draw::Rect nav_[2];                           // the back and forward arrows, as the last paint drew them
};

void DataTabsView::paint(draw::Canvas& c) {
    const int w = width(), top = height();
    c.fill({0, 0, w, top}, kBg);
    // The .dat tabs: name and entry count; the open one underlined in the accent color. The layout (core/views_data.inc):
    // what fits before the back and forward arrows; this paint measures the text.
    const draw::Font uiFont = c.font({draw::Face::Ui, 0}), smallFont = c.font(host_.countFont());
    const draw::Font outer = c.setFont(uiFont);
    const int n = host_.tabCount();
    std::vector<DataTabText> texts;
    for (int i = 0; i < n; ++i) texts.push_back({c.measure(host_.tabTitle(i)), c.measure(std::to_wstring(host_.tabEntryCount(i)))});
    const DataTabMetrics metrics{scale(8), scale(84), scale(22), scale(16) + scale(6), scale(26), scale(2)};
    const DataTabLayout layout = dataTabLayout(texts, w, metrics);
    const int cur = host_.currentTab();
    boxes_.clear();
    for (int i = 0; i < n; ++i) {
        const std::wstring name = host_.tabTitle(i);
        const std::wstring count = layout.showCounts ? std::to_wstring(host_.tabEntryCount(i)) : L"";
        const int nameW = texts[size_t(i)].nameW;
        const int iw = layout.showIcons ? scale(16) + scale(6) : 0;  // the tab's icon and the gap after it
        const auto [boxL, boxR] = layout.boxes[size_t(i)];
        const draw::Rect r{boxL, 0, boxR, top};
        const bool on = i == cur, hot = i == hot_;
        if (on || hot) c.fill(r, on ? kBg : kCtl);
        if (on) c.fill({r.left, r.bottom - scale(2), r.right, r.bottom}, kAccent);
        if (host_.tabChangedCount(i) > 0) {  // edited: a green dot
            const int rr = scale(3), cx = r.right - scale(6), cy = r.top + scale(9);
            c.ellipse({cx - rr, cy - rr, cx + rr, cy + rr}, kGood, draw::kNone);
        }
        if (layout.showIcons) c.icon({r.left + scale(10), r.top, r.left + scale(10) + scale(16), r.bottom}, host_.tabIcon(i), on ? kAccent : kDim, scale(16));
        c.text({r.left + scale(10) + iw, r.top, r.right, r.bottom}, name, on ? kAccent : kText, draw::kMiddle);
        c.setFont(smallFont);
        c.text({r.left + scale(14) + iw + nameW, r.top + scale(1), r.right, r.bottom}, count, kDim, draw::kMiddle);
        c.setFont(uiFont);
        boxes_.push_back(r);
    }
    for (int k = 0; k < 2; ++k) {
        nav_[k] = {w - scale(68) + k * scale(32), scale(3), w - scale(40) + k * scale(32), top - scale(3)};
        const bool can = k == 0 ? host_.canBack() : host_.canForward();
        c.icon(nav_[k], int(k == 0 ? UiIcon::Back : UiIcon::Forward), can ? kText : kFaint, scale(16));
    }
    c.setFont(outer);
}

bool DataTabsView::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: {
        int hot = -1;
        for (size_t i = 0; i < boxes_.size(); ++i)
            if (boxes_[i].contains(e.pt)) hot = int(i);
        if (hot != hot_) hot_ = hot, invalidate();
        return true;
    }
    case MouseKind::Leave:
        if (hot_ != -1) hot_ = -1, invalidate();
        return true;
    case MouseKind::Down:
        if (e.button != 0) return false;
        for (int k = 0; k < 2; ++k)
            if (nav_[k].contains(e.pt)) {
                host_.travel(k == 0);
                return true;
            }
        for (size_t i = 0; i < boxes_.size(); ++i)
            if (boxes_[i].contains(e.pt)) {
                host_.tabClicked(int(i));
                return true;
            }
        return true;
    default: return false;
    }
}

// The Data workspace's container: the tab strip (above), the search box and the Changed toggle (above the list), the
// edge between the list and the form (drag it: the list's width), and the lines between them.
class DataWorkspaceView final : public draw::View {
public:
    explicit DataWorkspaceView(DataWorkspaceHost& host) : host_(host), tabs_(host) {
        search_.setPlaceholder(L"Search units");
        search_.onChange = [this] {
            filter_.search = search_.text();
            host_.filterChanged();
        };
        changed_.setGlyph(stardraft::kit::IconGlyph::ofIcon(int(UiIcon::Filter)));
        changed_.setStyle(stardraft::kit::IconStyle::Face);
        changed_.setTip(L"Changed: show only the entries that you changed (click again to show all)");
        changed_.onClick = [this] { toggleChanged(); };
        add(tabs_, {0, 0, 0, 0});
        add(search_, {0, 0, 0, 0});
        add(changed_, {0, 0, 0, 0});
    }

    // The filter the rows are made from (the program reads it).
    const DataListFilter& filter() const { return filter_; }
    // The Changed toggle's state: the list shows only the changed entries (the button is on).
    void setChangedOn(bool on) {
        filter_.changedOnly = on;
        changed_.setOn(on);
    }
    // A tab switch: the search box goes back to empty and gets its cue for the tab.
    void resetSearch(const std::wstring& cue) {
        search_.setPlaceholder(cue);
        search_.setText(L"");
    }
    bool searchFocused() { return search_.field().hasFocus(); }
    // The layout from the container's size (and the list's width): the tabs, the search box, the Changed toggle, and the
    // windows the program keeps (placeLists).
    void relayout() {
        const int w = width(), h = height(), pad = scale(8), rowH = scale(26), lw = host_.listWidth(), top = DataTabsView::stripH();
        auto at = [](int x, int y, int cw, int ch) { return draw::Rect{x, y, x + std::max(0, cw), y + std::max(0, ch)}; };
        tabs_.setFrame(at(0, 0, w, top));
        search_.setFrame(at(pad, top + pad, lw - pad - rowH - scale(6) - pad, scale(stardraft::kit::kFieldH)));
        changed_.setFrame(at(lw - pad - rowH, top + pad, rowH, rowH));
        const int gap = scale(5), rw = host_.refsWidth();
        const draw::Rect list = at(0, top + rowH + 2 * pad, lw, std::max(1, h - top - rowH - 2 * pad));
        const draw::Rect form = at(lw + gap, top, std::max(1, w - lw - gap - rw), std::max(1, h - top));
        const draw::Rect refs = rw > 0 ? at(w - rw, top, rw, std::max(1, h - top)) : draw::Rect{};
        host_.placeLists(list, form, refs);
        invalidate();
    }

    void resized() override { relayout(); }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    void paintOver(draw::Canvas& c) override {
        // The line under the tabs, the gap by the list's edge and the edge's line (after the children, as the Win32 paint had them).
        const int w = width(), h = height(), top = DataTabsView::stripH(), lw = host_.listWidth();
        c.fill({0, top - 1, w, top}, kBorder);
        c.fill({lw, top, lw + scale(5), h}, kBg);
        c.fill({lw + scale(2), top, lw + scale(3), h}, kBorder);
    }
    bool hoverTip() const override { return true; }  // the Changed toggle's tip (a view tip shows over the children too)
    bool mouse(const draw::MouseEvent& e) override {
        using draw::MouseKind;
        const int top = DataTabsView::stripH(), lw = host_.listWidth(), gap = scale(5);
        switch (e.kind) {
        case MouseKind::Down:  // the edge (between the list and the form): drag it
            if (e.button != 0 || e.pt.y <= top || e.pt.x < lw || e.pt.x >= lw + gap) return false;
            grab_ = e.pt.x;
            if (auto* h = host()) h->capture(*this, true);
            return true;
        case MouseKind::Move:
            if (grab_ < 0) return false;
            grab_ += host_.dragList(e.pt.x - grab_);  // the edge moves with the mouse; the grab follows the edge
            return true;
        case MouseKind::Up:
            if (e.button != 0 || grab_ < 0) return false;
            grab_ = -1;
            if (auto* h = host()) h->capture(*this, false);
            host_.splitDone();
            return true;
        default: return false;
        }
    }
    draw::Cursor cursor(draw::Point pt) override {
        const int top = DataTabsView::stripH(), lw = host_.listWidth(), gap = scale(5);
        return pt.y > top && pt.x >= lw && pt.x < lw + gap ? draw::Cursor::SizeWE : draw::Cursor::Arrow;
    }
    void captureLost() override { grab_ = -1; }
    bool extraButton(int button) override {  // the mouse's back (1) and forward (2) buttons
        if (button != 1 && button != 2) return false;
        host_.travel(button == 1);
        return true;
    }

private:
    void toggleChanged() {
        setChangedOn(!filter_.changedOnly);
        host_.filterChanged();
        host_.status(dataChangedMessage(host_.tabTitle(host_.currentTab()), filter_.changedOnly));
    }

    DataWorkspaceHost& host_;
    DataListFilter filter_;
    DataTabsView tabs_;
    SearchBox search_;
    stardraft::kit::IconButton changed_;
    int grab_ = -1;  // the edge's grab (its x) while it is dragged
};
