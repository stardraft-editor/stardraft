// The Custom list (portable; views_tiles.inc includes it before the Tiles panel, which shows it in place of its grid
// while its panel's Custom is on): the custom terrain of the map's tileset (core/stamps.inc), a picture of each, under
// headings made from the names ("Jungle_Bridges_Left": Left under Bridges under Jungle); a click on a heading closes or
// opens it. The box at the top sorts by name (A to Z) or the newest first; the search under it shows only the custom
// terrain whose name has the typed text (any case; GitHub #175), its closed headings open. A click on an item makes it the Brush's
// stamp; a right click: Rename, Delete, Show in folder. With no custom terrain for the tileset, two dim lines say how
// to make it.

// The picture of custom terrain in `box` (its shape kept, in the middle): its tiles as the Tiles list draws them. A
// large one draws every n-th tile only, each over the n x n tiles it stands for.
void stampsDrawPicture(draw::Canvas& c, const TerrainStamp& s, draw::Rect box) {
    if (s.width <= 0 || s.height <= 0 || !appState().ed) return;
    const int bw = box.right - box.left, bh = box.bottom - box.top;
    const double cell = std::min(double(bw) / s.width, double(bh) / s.height);
    if (cell <= 0) return;
    const int step = std::max(1, int(std::ceil(2.0 / cell)));  // (2 px a drawn tile at least)
    const double ox = box.left + (bw - cell * s.width) / 2, oy = box.top + (bh - cell * s.height) / 2;
    for (int y = 0; y < s.height; y += step)
        for (int x = 0; x < s.width; x += step) {
            if (!s.has(x, y)) continue;
            const draw::Rect d{int(ox + x * cell), int(oy + y * cell), int(ox + std::min(x + step, s.width) * cell), int(oy + std::min(y + step, s.height) * cell)};
            if (d.right <= d.left || d.bottom <= d.top) continue;
            draw::Rect src;
            draw::Bitmap& b = tilesPixels(s.tiles[size_t(y) * size_t(s.width) + size_t(x)], src);
            c.bitmap(b, src, d, true);
        }
}

// The list's rows: the headings (a group, a group's sub-group) and the items, in the order of the sort.
struct StampRow {
    int depth = 0;        // a heading: 0 a group, 1 a sub-group; an item: how many headings are over it
    bool heading = false;
    std::wstring label;   // a heading's name, or the item's (its name's part after its groups)
    std::wstring key;     // a heading's: its groups ("Jungle", "Jungle_Bridges"), for closing it
    int index = -1;       // an item's: its index in stampsList()
};
std::set<std::wstring> gStampsClosed;  // the headings closed (by key), for this run

std::vector<StampRow> stampsRows(std::wstring search = {}) {
    const auto& list = stampsList();
    auto lower = [](const std::string& n) {
        std::wstring w = widen(n);
        std::transform(w.begin(), w.end(), w.begin(), ::towlower);
        return w;
    };
    std::transform(search.begin(), search.end(), search.begin(), ::towlower);
    std::vector<int> order;
    for (size_t i = 0; i < list.size(); ++i)
        if (search.empty() || lower(list[i].name).find(search) != std::wstring::npos) order.push_back(int(i));
    if (stampsSort() == 1)
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return list[size_t(a)].created > list[size_t(b)].created; });
    else
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return lower(list[size_t(a)].name) < lower(list[size_t(b)].name); });
    // The groups in the order of their first item (by name: A to Z; the newest first: the group with the newest item
    // first); the custom terrain with no group first.
    struct Item {
        int index;
        std::wstring group, sub, label;
    };
    std::vector<Item> items;
    for (int i : order) {
        const std::vector<std::string> parts = terrainStampNameParts(list[size_t(i)].name);
        Item it{i, L"", L"", L""};
        size_t used = 0;
        if (parts.size() >= 2) it.group = widen(parts[0]), used = 1;
        if (parts.size() >= 3) it.sub = widen(parts[1]), used = 2;
        for (size_t k = used; k < parts.size(); ++k) it.label += (k > used ? L"_" : L"") + widen(parts[k]);
        items.push_back(std::move(it));
    }
    std::stable_partition(items.begin(), items.end(), [](const Item& it) { return it.group.empty(); });
    std::vector<std::wstring> groups;
    for (const Item& it : items)
        if (!it.group.empty() && std::find(groups.begin(), groups.end(), it.group) == groups.end()) groups.push_back(it.group);
    std::vector<StampRow> rows;
    for (const Item& it : items)
        if (it.group.empty()) rows.push_back({0, false, it.label, L"", it.index});
    for (const std::wstring& g : groups) {
        rows.push_back({0, true, g, g, -1});
        if (gStampsClosed.count(g) && search.empty()) continue;
        for (const Item& it : items)  // its own items first, then its sub-groups
            if (it.group == g && it.sub.empty()) rows.push_back({1, false, it.label, L"", it.index});
        std::vector<std::wstring> subs;
        for (const Item& it : items)
            if (it.group == g && !it.sub.empty() && std::find(subs.begin(), subs.end(), it.sub) == subs.end()) subs.push_back(it.sub);
        for (const std::wstring& sub : subs) {
            const std::wstring key = g + L"_" + sub;
            rows.push_back({1, true, sub, key, -1});
            if (gStampsClosed.count(key) && search.empty()) continue;
            for (const Item& it : items)
                if (it.group == g && it.sub == sub) rows.push_back({2, false, it.label, L"", it.index});
        }
    }
    return rows;
}

class StampsListView;
std::vector<StampsListView*> gStampsViews;  // (each panel's list: stampsPanelsRepaint)
void stampsPanelsRepaint();

class StampsListView final : public draw::View {
public:
    TilesView* data = &gTilesDocked;  // its panel's tiles (the Brush's stamp, the Custom choice)
    TilesGridView* grid = nullptr;
    TilesPanelView* panel = nullptr;
    PanelState* state = nullptr;  // its Terrain panel (a press makes it the active panel)

    StampsListView() {
        gStampsViews.push_back(this);
        sort_.setItems({{L"Sort by name (A to Z)"}, {L"Sort by date (newest first)"}});
        sort_.onChange = [](int i) {
            setStampsSort(i);
            shellFocusView();
        };
        add(sort_, {0, 0, 0, 0});
        search_.setPlaceholder(tr(L"Search"));
        search_.onChange = [this] { scroll_ = 0, layout(); };
        add(search_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
        bar_.onScroll = [this](int pos) { scrollTo(pos); };
    }
    ~StampsListView() override { std::erase(gStampsViews, this); }
    StampsListView(const StampsListView&) = delete;
    StampsListView& operator=(const StampsListView&) = delete;

    TilesScope here() { return {data, grid, panel}; }
    static int topH() { return 2 * scale(24) + 2 * scale(6); }  // (the sort box, the search)
    static int rowH(bool heading) { return heading ? scale(24) : scale(44); }
    const std::vector<StampRow>& rows() const { return rows_; }

    void resized() override {
        sort_.setFrame({0, 0, width(), scale(24)});
        search_.setFrame({0, scale(24) + scale(6), width(), 2 * scale(24) + scale(6)});
        layout();
    }
    // The rows again (the files, the sort, a heading closed): their places, the scroll kept where it can be.
    void layout() {
        rows_ = stampsRows(search_.text());
        tops_.clear();
        int y = 0;
        for (const StampRow& r : rows_) tops_.push_back(y), y += rowH(r.heading);
        contentH_ = y;
        const int viewH = std::max(0, height() - topH());
        bar_.setFrame({width() - barW(), topH(), width(), height()});
        bar_.setRange(contentH_, std::max(1, viewH));
        bar_.setVisible(contentH_ > viewH);
        scrollTo(scroll_);
    }
    void scrollTo(int pos) {
        scroll_ = std::clamp(pos, 0, std::max(0, contentH_ - (height() - topH())));
        bar_.setPos(scroll_);
        invalidate();
    }
    static int barW() { return scale(stardraft::kit::kScrollBarW); }
    // The row at a point of the view (-1: none).
    int rowAt(draw::Point p) const {
        if (p.y < topH() || (bar_.shown() && p.x >= width() - barW())) return -1;
        const int y = p.y - topH() + scroll_;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (y >= tops_[i] && y < tops_[i] + rowH(rows_[i].heading)) return int(i);
        return -1;
    }

    void paint(draw::Canvas& c) override {
        const TilesScope scope = here();
        if (sortShown_ != stampsSort()) sortShown_ = stampsSort(), sort_.setValue(sortShown_);
        const auto& list = stampsList();
        if (listShown_ != &list || builtFor_ != gStamps.era || listSize_ != list.size() || stale_) {
            listShown_ = &list, builtFor_ = gStamps.era, listSize_ = list.size(), stale_ = false;
            layout();
        }
        tilesPixelsTrim();
        const int w = width() - (bar_.shown() ? barW() : 0), h = height();
        c.fill({0, 0, width(), h}, kBg);
        if (!appState().ed || !appState().ed->isOpen()) return;
        if (rows_.empty() && !search_.text().empty()) {
            c.text({scale(8), topH(), w - scale(8), topH() + scale(28)}, tr(L"No match"), kDim, draw::kCenter | draw::kMiddle);
            return;
        }
        if (rows_.empty()) {  // how to make it: as short as it can be
            const int mid = topH() + (h - topH()) / 2, lh = scale(20);
            c.text({scale(8), mid - lh, w - scale(8), mid}, tr(L"Select tiles on the map."), kDim, draw::kCenter | draw::kMiddle);
            c.text({scale(8), mid, w - scale(8), mid + lh}, tr(L"Right-click > Save as custom terrain."), kDim, draw::kCenter | draw::kMiddle);
            return;
        }
        c.pushClip({0, topH(), w, h});
        draw::FontDesc glyphs;
        glyphs.face = draw::Face::Glyphs, glyphs.px = scale(9);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const StampRow& r = rows_[i];
            const int top = topH() + tops_[i] - scroll_, bottom = top + rowH(r.heading);
            if (bottom < topH() || top > h) continue;
            const int indent = scale(4) + r.depth * scale(14);
            if (r.heading) {  // a chevron (open: down, closed: right), the name, then a line to the right edge
                const bool closed = gStampsClosed.count(r.key) != 0;
                const draw::Font of = c.setFont(c.font(glyphs));
                c.text({indent, top, indent + scale(14), bottom}, closed ? L"\xE76C" : L"\xE70D", int(i) == hover_ ? kText : kDim, draw::kCenter | draw::kMiddle);
                c.setFont(of);
                const draw::Rect t{indent + scale(16), top, w - scale(6), bottom};
                c.text(t, r.label, kText, draw::kMiddle | draw::kEllipsis);
                const int mid = (top + bottom) / 2, lineL = t.left + c.measure(r.label) + scale(8);
                if (lineL < w - scale(6)) c.fill({lineL, mid, w - scale(6), mid + 1}, kBorder);
                continue;
            }
            if (r.index < 0 || r.index >= int(list.size())) continue;
            const TerrainStamp& s = list[size_t(r.index)];
            const bool picked = !data->stampName.empty() && widen(s.name) == data->stampName;
            const draw::Rect f{1, top + 1, w - 1, bottom - 1};
            if (picked) kitpaint::drawListRow(c, f, true, false);
            else if (int(i) == hover_) c.fill(f, kRowHot);
            const draw::Rect pic{indent + scale(2), top + scale(3), indent + scale(2) + (bottom - top) - scale(6), bottom - scale(3)};
            c.fill(pic, draw::rgb(6, 6, 10));
            stampsDrawPicture(c, s, pic);
            const std::wstring size = std::to_wstring(s.width) + L" x " + std::to_wstring(s.height);
            const int sw = c.measure(size) + scale(8);
            c.text({pic.right + scale(8), top, w - sw - scale(4), bottom}, r.label, kText, draw::kMiddle | draw::kEllipsis);
            c.text({w - sw, top, w - scale(6), bottom}, size, kDim, draw::kRight | draw::kMiddle);
        }
        c.popClip();
    }

    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && state && state != gActivePanel && (state->mode == appState().mode || !isDockedPanel(state))) activatePanel(state);
        const TilesScope scope = here();
        switch (e.kind) {
        case draw::MouseKind::Wheel:
            if (e.horizontal) return false;
            scrollTo(scroll_ - int(draw::wheelDistance(e, float(scale(90)))));
            return true;
        case draw::MouseKind::Move: {
            const int r = rowAt(e.pt);
            if (r != hover_) hover_ = r, invalidate();
            return true;
        }
        case draw::MouseKind::Leave:
            if (hover_ >= 0) hover_ = -1, invalidate();
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick: {
            if (e.button != 0) return e.button == 1;
            const int r = rowAt(e.pt);
            if (r < 0) return true;
            const StampRow row = rows_[size_t(r)];
            if (row.heading) {  // close it, or open it again
                if (!gStampsClosed.erase(row.key)) gStampsClosed.insert(row.key);
                stampsPanelsRepaint();  // (each panel's list shows it so)
                return true;
            }
            const auto& list = stampsList();
            if (row.index < 0 || row.index >= int(list.size())) return true;
            if (appState().mode != Mode::Terrain) {  // (another mode: the pick goes to Terrain, as a palette's)
                if (state) activatePanel(state);
                else setMode(Mode::Terrain);
            }
            stampsUse(list[size_t(row.index)]);
            shellFocusView();
            return true;
        }
        case draw::MouseKind::Up: {
            if (e.button != 1) return false;
            const int r = rowAt(e.pt);
            const auto& list = stampsList();
            if (r < 0 || rows_[size_t(r)].heading || rows_[size_t(r)].index >= int(list.size())) return false;
            stampsItemMenu(list[size_t(rows_[size_t(r)].index)]);
            return true;
        }
        default: return false;
        }
    }
    std::wstring feedbackName(draw::Point pt) override {
        const int r = rowAt(pt);
        const auto& list = stampsList();
        if (r >= 0 && !rows_[size_t(r)].heading && rows_[size_t(r)].index < int(list.size())) return widen(list[size_t(rows_[size_t(r)].index)].name);
        return L"the Custom list";
    }
    // The files changed: the rows again at the next paint.
    void changed() {
        stale_ = true;
        invalidate();
    }
    // The middle of the item row of `name` in the window (a snapshot's click: --click custom:<name>); false: not shown.
    bool itemCenter(const std::wstring& name, int& x, int& y) {
        const TilesScope scope = here();
        if (stale_ || listShown_ != &stampsList()) layout(), stale_ = false;
        const auto& list = stampsList();
        for (size_t i = 0; i < rows_.size(); ++i) {
            const StampRow& r = rows_[i];
            if (r.heading ? r.key != name : (r.index < 0 || r.index >= int(list.size()) || widen(list[size_t(r.index)].name) != name)) continue;
            const draw::Point o = origin();
            x = o.x + width() / 2, y = o.y + topH() + tops_[i] - scroll_ + rowH(r.heading) / 2;
            return shown() && y - o.y >= topH() && y - o.y < height();
        }
        return false;
    }

private:
    stardraft::kit::Dropdown sort_;
    stardraft::kit::TextField search_;
    stardraft::kit::ScrollBar bar_;
    std::vector<StampRow> rows_;
    std::vector<int> tops_;
    int contentH_ = 0, scroll_ = 0, hover_ = -1, sortShown_ = -1, builtFor_ = -2;
    const std::vector<TerrainStamp>* listShown_ = nullptr;
    size_t listSize_ = 0;
    bool stale_ = true;
};

void stampsPanelsRepaint() {
    for (StampsListView* v : gStampsViews) v->changed();
}
void stampsRenamedInPanels(int era, const std::wstring& was, const std::wstring& now) {
    std::vector<TilesView*> all{gTiles};
    for (StampsListView* v : gStampsViews) all.push_back(v->data);
    for (auto& [doc, d] : gDockedPanels) all.push_back(&d.tiles);
    for (TilesView* t : all) {
        if (!t || t->stampName != was || t->stampEra != (era & 7)) continue;
        if (!now.empty()) {
            t->stampName = now;
            continue;
        }
        t->stamp.clear(), t->stampW = t->stampH = 0, t->stampName.clear();
        t->chosen.clear(), t->picked = t->anchor = -1;
    }
}
