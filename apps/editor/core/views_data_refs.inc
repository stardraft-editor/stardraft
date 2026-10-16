// The Data workspace's References panel (portable, core.inc): the entries of any .dat whose fields point at the open
// entry, grouped by their .dat, each a link to it. The panel is a view in a view window (apps/common/view.h); its window's
// vertical bar takes the scroll. The program's parts (the .dat tables, the entries' names and icons, the go-to) come in
// through DataRefsHost (main.cpp: DataRefsProgram; sdl_data.inc: SdlDataRefsHost); the rows are dataRefRows' (below,
// from dataRefs in core/data_model.inc).

// One reference: the entry that points at the open one, with what the panel shows for it.
struct DataRefRow {
    std::string dat;                // the entry's .dat ("units", "buttons", "strings", ...)
    int entry = 0;                  // its index in that .dat (strings count from 1)
    std::wstring group;             // the .dat's title, as the group heading shows it
    std::optional<UiIcon> groupIcon;  // the group's icon before its title (none: no icon)
    std::wstring name;              // the entry's name; "#n" when it has none
    std::wstring feedback;          // the name Report feedback gives it
    draw::Image icon;               // the entry's icon (empty: none)
};

class DataRefsHost {
public:
    virtual ~DataRefsHost() = default;
    // The references of the open entry, in the order the panel shows them; `shown`: false when the panel has nothing to
    // say (no entry of the Data workspace is open).
    virtual std::vector<DataRefRow> refs(bool& shown) = 0;
    virtual void goTo(const std::string& dat, int entry) = 0;  // a link is clicked: the Data workspace goes there
    virtual bool travel(int button) = 0;                       // the mouse's back (1) or forward (2) button: the Data workspace's history
};

class DataRefsPage final : public draw::View {
public:
    explicit DataRefsPage(DataRefsHost& host) : host_(host) {}

    // A new entry starts at the top of its references (as the Data page's selection does).
    void resetScroll() {
        scroll_ = 0;
        invalidate();
    }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override { return linkAt(pt) ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring feedbackName(draw::Point pt) override {
        const Link* l = linkAt(pt);
        return l && !l->feedback.empty() ? l->feedback : L"References";
    }
    bool vscroll(int code, int pos, int track, int page) override;
    // The window's vertical bar: the content's last line, the panel's height and the position.
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, contentH_ - 1), page = height(), pos = scroll_;
        return true;
    }
    bool extraButton(int button) override { return host_.travel(button); }

private:
    struct Link {
        draw::Rect r;
        std::string dat;
        int entry = 0;
        std::wstring feedback;
    };

    static draw::Font smallFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}); }
    static int sideHeader(draw::Canvas& c, int x, int y, int w, const std::wstring& title, size_t count) {
        const draw::Rect h{x, y, x + w, y + scale(22)};
        c.text(h, title, kText, draw::kMiddle);
        c.text(h, std::to_wstring(count), kDim, draw::kMiddle | draw::kRight);
        y += scale(30);
        if (!count) {
            c.text({x, y, x + w, y + scale(20)}, L"No other data uses this entry", kDim, draw::kMiddle | draw::kEllipsis);
            y += scale(22);
        }
        return y;
    }
    // A group's heading: its icon and its .dat's title, small and dim; the first group has no gap above it.
    static int sideGroup(draw::Canvas& c, int x, int y, int w, const std::wstring& name, bool first, std::optional<UiIcon> icon) {
        if (!first) y += scale(8);
        const draw::Font old = c.setFont(smallFont(c));
        if (icon) {
            c.icon({x, y - scale(1), x + scale(16), y + scale(16)}, int(*icon), kDim, scale(16));
            x += scale(22), w -= scale(22);
        }
        c.text({x, y, x + w, y + scale(15)}, name, kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(old);
        return y + scale(18);
    }
    // One reference: its icon and name in the accent; underlined while the pointer is over it.
    static void sideLink(draw::Canvas& c, int x, int y, int w, const std::wstring& text, const draw::Image& icon, bool over) {
        const int h = scale(20);
        int tx = x;
        if (icon) {
            c.fitted(icon, {x, y + scale(1), x + scale(18), y + h - scale(1)});
            tx += scale(22);
        }
        const draw::Rect nr{tx, y, x + w, y + h};
        c.text(nr, text, over ? kAccentBright : kAccent, draw::kMiddle | draw::kEllipsis);
        if (over) {
            const int tw = std::min<int>(c.measure(text), nr.width());
            c.line(tx, nr.bottom - scale(3), tx + tw, nr.bottom - scale(3), kAccentBright);
        }
    }

    const Link* linkAt(draw::Point pt) const {
        for (const auto& l : links_)
            if (l.r.contains(pt)) return &l;
        return nullptr;
    }
    int maxScroll() const { return std::max(0, contentH_ - height()); }
    void setScroll(int s) {
        const int n = std::clamp(s, 0, maxScroll());
        if (n != scroll_) scroll_ = n, invalidate();
    }

    DataRefsHost& host_;
    int scroll_ = 0;
    int contentH_ = 0;
    draw::Point pointer_{};
    bool hasPointer_ = false;
    std::vector<Link> links_;  // the link rects of the last paint (the mouse works from these)
    int hover_ = -1;           // the link the pointer is over, as the last move drew it
};

void DataRefsPage::paint(draw::Canvas& c) {
    const int w = width(), h = height();
    c.fill({0, 0, w, h}, kPanel);
    c.fill({0, 0, 1, h}, kBorder);
    const int x = scale(14), sw = w - scale(14) - scale(10);
    int y = scale(12) - scroll_;
    links_.clear();
    bool shown = false;
    const auto refs = host_.refs(shown);
    if (shown) {
        y = sideHeader(c, x, y, sw, L"References", refs.size());
        std::string group;
        for (const auto& r : refs) {
            if (r.dat != group) y = sideGroup(c, x, y, sw, r.group, group.empty(), r.groupIcon), group = r.dat;
            const draw::Rect row{x, y, x + sw, y + scale(20)};
            const bool over = hasPointer_ && row.contains(pointer_);
            sideLink(c, x, y, sw, r.name, r.icon, over);
            links_.push_back({row, r.dat, r.entry, r.feedback});
            y += scale(22);
        }
    }
    contentH_ = y + scroll_ + scale(12);
    if (scroll_ > maxScroll()) scroll_ = maxScroll(), invalidate();
}

bool DataRefsPage::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: {  // a link under the mouse lights up
        pointer_ = e.pt, hasPointer_ = true;
        const Link* l = linkAt(e.pt);
        const int now = l ? int(l - links_.data()) : -1;
        if (now != hover_) hover_ = now, invalidate();
        return false;
    }
    case MouseKind::Leave:
        hasPointer_ = false, hover_ = -1;
        invalidate();
        return false;
    case MouseKind::Wheel:
        if (e.horizontal) return false;  // the old panel had no sideways wheel
        setScroll(scroll_ - int(std::lround(draw::wheelDistance(e, float(scale(20) * 3)))));  // three rows a notch
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick: {  // (the class has CS_DBLCLKS: a quick second click is a double click, and it opens the link too)
        if (e.button != 0) return false;
        if (const Link* l = linkAt(e.pt)) {
            const std::string dat = l->dat;
            const int entry = l->entry;
            host_.goTo(dat, entry);
            return true;
        }
        return false;
    }
    default: return false;
    }
}

bool DataRefsPage::vscroll(int code, int pos, int track, int page) {
    // The window's bar (the SB_* requests): a line is a row, a page is the visible height.
    int s = scroll_;
    switch (code) {
    case 0: s -= scale(20); break;   // SB_LINEUP
    case 1: s += scale(20); break;   // SB_LINEDOWN
    case 2: s -= page; break;        // SB_PAGEUP
    case 3: s += page; break;        // SB_PAGEDOWN
    case 4:                          // SB_THUMBPOSITION
    case 5: s = track; break;        // SB_THUMBTRACK
    case 6: s = 0; break;            // SB_TOP
    case 7: s = 1 << 30; break;      // SB_BOTTOM
    default: (void)pos; return false;
    }
    setScroll(s);
    return true;
}

// The panel's rows for the open entry of the Data workspace (dataRefs, core/data_model.inc). `shown`: false when no
// entry is open (no table, and not the Strings or Buttons tab of a project). `icon` gives an entry's picture (the
// program's: tab, entry).
std::vector<DataRefRow> dataRefRows(bool& shown, const std::function<draw::Image(int, int)>& icon) {
    const DataModel& d = dataState();
    const DatTable* t = curTable();
    shown = t || (extraTab(d.datTab) && d.project);
    std::vector<DataRefRow> out;
    if (!shown) return out;
    const auto& list = dataRefs(t ? t->schema().name : d.datTab == stringsTab() ? "strings" : "buttons", d.entry);
    out.reserve(list.size());
    for (const auto& r : list) {
        DataRefRow row;
        row.dat = r.dat, row.entry = r.entry;
        row.group = tabTitle(datTabOf(r.dat)), row.groupIcon = tabIcon(datTabOf(r.dat));
        row.name = tabEntryName(datTabOf(r.dat), r.entry);
        row.feedback = entryName(r.dat, r.entry);
        if (row.name.empty()) row.name = L"#" + std::to_wstring(r.entry);
        row.icon = icon(datTabOf(r.dat), r.entry);
        out.push_back(std::move(row));
    }
    return out;
}
