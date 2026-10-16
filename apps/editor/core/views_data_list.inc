// The Data workspace's entry list (portable, core.inc): the rows of the open .dat (the number, the icon, the name, a pin
// and the changed dot), the selection and the hover, the scroll (the window's own bar), and the keys and mouse of the list.
// The rows and their order come from the model (data_list.inc, dataRebuildList); the program gives the tab, the names,
// the icons and the changed flags, and what a click does (select, the right button's menu, back and forward) through
// DataListViewHost. The view is a view in a view window (apps/common/view.h), like the References panel.

// The program's side of the list.
class DataListViewHost {
public:
    virtual ~DataListViewHost() = default;
    virtual int tab() = 0;                         // the .dat (or extra tab) the list shows
    virtual const std::vector<int>& rows() = 0;    // the shown entries, in order
    virtual int pinnedShown() = 0;                 // how many of the first rows are pinned (a line under the last)
    virtual int entry() = 0;                       // the open entry: its row is selected
    virtual int& scroll() = 0;                     // the list's scroll, in pixels (kept by the program: a tab switch resets it)
    virtual bool changed(int e) = 0;               // the entry differs from the game's (a green dot)
    virtual std::wstring name(int e) = 0;          // the entry's name; empty when it has none
    virtual draw::Image icon(int e) = 0;           // the entry's icon (empty: none)
    virtual void select(int e) = 0;                // a row is clicked or the keys move the selection
    virtual void menu(int e, draw::Point pt) = 0;  // the right button is released over an entry (pt: the list's coordinates)
    virtual bool travel(int button) = 0;           // the mouse's back (1) or forward (2) button
};

class DataListView final : public draw::View {
public:
    explicit DataListView(DataListViewHost& host) : host_(host) {}

    // The row height in pixels (as the Windows list always had it).
    static int rowH() { return scale(26); }

    // Keeps the scroll inside the list after its rows changed (a shorter list: no empty rows below its end).
    void clampScroll() { dataListClampScroll(host_.scroll(), host_.rows().size(), rowH(), height()); }
    // Scrolls the list so that entry `e`'s row shows (the selection's reveal).
    void reveal(int e) {
        const auto& rows = host_.rows();
        const auto it = std::find(rows.begin(), rows.end(), e);
        if (it == rows.end()) return;
        const int y = int(it - rows.begin()) * rowH();
        int& s = host_.scroll();
        if (y < s) s = y;
        if (y + rowH() > s + height()) s = y + rowH() - height();
        invalidate();
    }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    bool vscroll(int code, int pos, int track, int page) override;
    // The window's vertical bar: the list's last line, the list's height and the position. (The bar is disabled, not hidden,
    // when the rows fit: as the Windows list had it.)
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, int(host_.rows().size()) * rowH() - 1), page = height(), pos = host_.scroll();
        return true;
    }
    bool windowScrollBarDisables() const override { return true; }
    bool extraButton(int button) override { return (button == 1 || button == 2) && host_.travel(button); }
    bool hoverTip() const override { return true; }  // the full name of a cut row
    bool focusable() const override { return true; }
    std::wstring tip(draw::Point pt) override {
        for (const auto& c : cut_)
            if (c.first.contains(pt)) return c.second;
        return {};
    }

private:
    int rowAt(draw::Point pt) const {
        const int y = pt.y + host_.scroll();
        if (y < 0) return -1;
        const auto& rows = host_.rows();
        const int i = y / rowH();
        return i < int(rows.size()) ? rows[size_t(i)] : -1;
    }
    void setScroll(int s) {
        const int most = std::max(0, int(host_.rows().size()) * rowH() - height());
        const int n = std::clamp(s, 0, most);
        if (n != host_.scroll()) host_.scroll() = n, invalidate();
    }

    DataListViewHost& host_;
    int hover_ = -1;                                        // the entry under the mouse (its row is hot)
    std::vector<std::pair<draw::Rect, std::wstring>> cut_;  // the rows whose names the last paint cut, with the full names
};

void DataListView::paint(draw::Canvas& c) {
    const int w = width(), h = height(), rh = rowH();
    const auto& rows = host_.rows();
    const int n = int(rows.size()), scroll = host_.scroll(), pinned = host_.pinnedShown(), open = host_.entry();
    cut_.clear();
    const int first = scroll / rh;
    for (int i = first; i < n && (i * rh - scroll) < h; ++i) {
        const int u = rows[size_t(i)];
        const draw::Rect r{0, i * rh - scroll, w, (i + 1) * rh - scroll};
        const bool on = u == open, hot = u == hover_;
        if (on) {  // a selected row: its fill and a bar at the left (the list's accent)
            c.fill(r, kSel);
            c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
        } else if (hot) {
            c.fill(r, kRowHot);
        }
        c.text({scale(6), r.top, scale(36), r.bottom}, std::to_wstring(u), kDim, draw::kRight | draw::kMiddle);
        const draw::Rect box{scale(42), r.top + scale(2), scale(42) + rh - scale(4), r.bottom - scale(2)};
        if (const draw::Image img = host_.icon(u)) c.fitted(img, box);
        const bool isPinned = i < pinned;
        const draw::Rect t{box.right + scale(8), r.top, w - scale(isPinned ? 34 : 16), r.bottom};
        if (isPinned) {  // a pin at the right; a line under the last pinned row
            const draw::Font old = c.setFont(c.font({draw::Face::Glyphs, scale(11)}));
            c.text({w - scale(33), r.top, w - scale(18), r.bottom}, L"\xE718", kDim, draw::kCenter | draw::kMiddle);
            c.setFont(old);
            if (i == pinned - 1 && i + 1 < n) c.fill({scale(6), r.bottom - 1, w - scale(6), r.bottom}, kBorder);
        }
        std::wstring name = host_.name(u);
        const draw::Color color = on ? kAccent : name.empty() ? kDim : kText;
        if (name.empty()) name = L"(unnamed)";
        c.text(t, name, color, draw::kMiddle | draw::kEllipsis);
        if (c.measure(name) > t.width()) cut_.push_back({r, name});
        if (host_.changed(u)) {  // changed: a green dot
            const int cy = (r.top + r.bottom) / 2, rr = scale(3);
            c.ellipse({w - scale(12) - rr, cy - rr, w - scale(12) + rr, cy + rr}, kGood, draw::kNone);
        }
    }
}

bool DataListView::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: {  // the row under the mouse is hot
        const int u = rowAt(e.pt);
        if (u != hover_) hover_ = u, invalidate();
        return true;
    }
    case MouseKind::Leave:
        if (hover_ != -1) hover_ = -1, invalidate();
        return true;
    case MouseKind::Wheel:
        if (e.horizontal) return false;
        setScroll(host_.scroll() - int(std::lround(draw::wheelDistance(e, float(rowH() * 3)))));  // three rows a notch
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
        if (e.button == 0) {
            const int u = rowAt(e.pt);
            if (u >= 0) host_.select(u);
        }
        return true;
    case MouseKind::Up:  // an entry: copy, paste or reset its values (the menu)
        if (e.button != 1) return false;
        if (const int u = rowAt(e.pt); u >= 0) {
            host_.select(u);
            host_.menu(u, e.pt);
        }
        return true;
    default: return false;
    }
}

bool DataListView::key(const draw::KeyEvent& e) {
    if (!e.down) return false;
    int step = 0;
    switch (e.key) {
    case draw::Key::Down: step = 1; break;
    case draw::Key::Up: step = -1; break;
    case draw::Key::PageDown: step = 15; break;
    case draw::Key::PageUp: step = -15; break;
    default: return false;
    }
    const auto& rows = host_.rows();
    if (rows.empty()) return true;
    const auto it = std::find(rows.begin(), rows.end(), host_.entry());
    const int i = it == rows.end() ? 0 : int(it - rows.begin());
    host_.select(rows[size_t(std::clamp(i + step, 0, int(rows.size()) - 1))]);
    return true;
}

bool DataListView::vscroll(int code, int pos, int track, int page) {
    // The window's bar (the SB_* requests): a line is a row, a page is the visible height.
    int s = host_.scroll();
    switch (code) {
    case 0: s -= rowH(); break;  // SB_LINEUP
    case 1: s += rowH(); break;  // SB_LINEDOWN
    case 2: s -= page; break;    // SB_PAGEUP
    case 3: s += page; break;    // SB_PAGEDOWN
    case 4:                      // SB_THUMBPOSITION
    case 5: s = track; break;    // SB_THUMBTRACK
    case 6: s = 0; break;        // SB_TOP
    case 7: s = 1 << 30; break;  // SB_BOTTOM
    default: (void)pos; return false;
    }
    setScroll(s);
    return true;
}
