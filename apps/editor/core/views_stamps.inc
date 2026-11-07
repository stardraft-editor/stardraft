// The Custom list (portable; views_tiles.inc includes it before the Tiles panel, which shows it in place of its grid
// while its panel's Custom is on): the custom terrain of the map's tileset (core/stamps.inc), a picture of each, under
// a heading for each of its folders (GitHub #252: right-click > New folder; drag custom terrain onto a folder to put it
// there, or below the folders to take it out); a click on a heading closes or opens it. The row at the top, as the other lists have it (GitHub #258): the search, which shows only the custom terrain
// whose name has the typed text (any case; GitHub #175), its closed headings open; the Sort button (by name, A to Z, or
// the newest first); the View button (List or Grid; Normal, Large, Extra large). A click on an item makes it the Brush's
// stamp; Alt+click makes its tiles the mask (GitHub #247: painting in Tile and Custom changes only the tiles of those
// values; Alt+click on it again clears the mask). A right click: the View menu as the other palettes have it (List or Grid; Normal, Large, Extra large), on an item
// Rename, Delete, Show in folder first. With no custom terrain for the tileset, two dim lines say how to make it.

// The picture of custom terrain in `box` (its shape kept, in the middle, on a dark area of its own size: the rest of
// the box stays the panel's color, GitHub #259): its tiles as the Tiles list draws them. A large one draws every n-th
// tile only, each over the n x n tiles it stands for.
void stampsDrawPicture(draw::Canvas& c, const TerrainStamp& s, draw::Rect box) {
    if (s.width <= 0 || s.height <= 0 || !appState().ed) return;
    const int bw = box.right - box.left, bh = box.bottom - box.top;
    const double cell = std::min(double(bw) / s.width, double(bh) / s.height);
    if (cell <= 0) return;
    const int step = std::max(1, int(std::ceil(2.0 / cell)));  // (2 px a drawn tile at least)
    const double ox = box.left + (bw - cell * s.width) / 2, oy = box.top + (bh - cell * s.height) / 2;
    c.fill({int(ox), int(oy), int(ox + cell * s.width), int(oy + cell * s.height)}, draw::rgb(6, 6, 10));  // (its holes: dark)
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

// The list's rows: the headings (the folders) and the items, in the order of the sort.
struct StampRow {
    int depth = 0;        // a heading: how many folders it is in; an item: how many headings are over it
    bool heading = false;
    std::wstring label;   // a heading's name (its folder's last name), or the item's name
    std::wstring key;     // a heading's: its folder ("Jungle/Bridges"), for closing it
    int index = -1;       // an item's: its index in stampsList()
};

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
    // The custom terrain in no folder first, then each folder (by name), its items in the order, then its folders. A
    // search shows only the folders with a match, open.
    std::vector<std::string> folders = stampsFolders();
    for (int i : order)
        if (const std::string& f = list[size_t(i)].folder; !f.empty() && std::find(folders.begin(), folders.end(), f) == folders.end()) folders.push_back(f);
    std::sort(folders.begin(), folders.end(), [&](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    std::vector<StampRow> rows;
    for (int i : order)
        if (list[size_t(i)].folder.empty()) rows.push_back({0, false, widen(list[size_t(i)].name), L"", i});
    const auto parentOf = [](const std::string& f) {
        const size_t cut = f.rfind('/');
        return cut == std::string::npos ? std::string() : f.substr(0, cut);
    };
    const auto hasMatch = [&](const std::string& f) {
        for (int i : order)
            if (const std::string& g = list[size_t(i)].folder; g == f || g.rfind(f + "/", 0) == 0) return true;
        return false;
    };
    std::function<void(const std::string&, int)> folderRows = [&](const std::string& parent, int depth) {
        for (const std::string& f : folders) {
            if (parentOf(f) != parent || (!search.empty() && !hasMatch(f))) continue;
            const std::wstring key = widen(f);
            rows.push_back({depth, true, widen(f.substr(parent.empty() ? 0 : parent.size() + 1)), key, -1});
            if (gStampsClosed.count(key) && search.empty()) continue;
            for (int i : order)
                if (list[size_t(i)].folder == f) rows.push_back({depth + 1, false, widen(list[size_t(i)].name), L"", i});
            folderRows(f, depth + 1);
        }
    };
    folderRows({}, 0);
    return rows;
}

class StampsListView;
// The right click's menu at `at` of `from` (the host: sdl_panels.inc): on an item (`s`) Rename, Delete, Show in folder;
// New folder (in `folder`: the folder clicked, or the item's); on a folder (`onFolder`) Rename folder, Delete folder;
// then the View items (List, Grid; Normal, Large, Extra large).
void shellStampsMenu(draw::View& from, draw::Point at, const TerrainStamp* s, const std::string& folder = {}, bool onFolder = false);
// The Sort button's menu (`sort`) or the View button's menu under `from` (the host: sdl_panels.inc).
void shellStampsButtonMenu(draw::View& from, bool sort);
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
        search_.setPlaceholder(tr(L"Search"));
        search_.onChange = [this] { scroll_ = 0, layout(); };
        add(search_, {0, 0, 0, 0});
        sort_.setTip(tr(L"Sort the custom terrain: by name or by date"));
        add(sort_, {0, 0, 0, 0});
        add(view_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
        bar_.onScroll = [this](int pos) { scrollTo(pos); };
    }
    ~StampsListView() override { std::erase(gStampsViews, this); }
    StampsListView(const StampsListView&) = delete;
    StampsListView& operator=(const StampsListView&) = delete;

    TilesScope here() { return {data, grid, panel}; }
    static int topH() { return scale(24) + scale(6); }  // (the row: the search, Sort, View)
    // An item's height in the list, and a cell's side in the grid, by the size (normal, large, extra large).
    static int rowH(bool heading) {
        static const int kRow[] = {44, 72, 112};
        return heading ? scale(24) : scale(kRow[stampsSize()]);
    }
    static int cellW() {
        static const int kCell[] = {64, 96, 144};
        return scale(kCell[stampsSize()]);
    }
    const std::vector<StampRow>& rows() const { return rows_; }

    void resized() override {
        // (as the side bar's search row, core/layout.inc: the View button 64 wide, the Sort button a square before it)
        const int rowH = scale(24), pad = scale(4), btnW = scale(64);
        view_.setFrame({width() - btnW, 0, width(), rowH});
        sort_.setFrame({width() - btnW - pad - rowH, 0, width() - btnW - pad, rowH});
        search_.setFrame({0, 0, std::max(0, width() - btnW - 2 * pad - rowH), rowH});
        layout();
    }
    // The rows again (the files, the sort, a heading closed): their places, the scroll kept where it can be.
    void layout() {
        rows_ = stampsRows(search_.text());
        tops_.clear(), rects_.clear();
        // (a list: each row under the one before; a grid: the items in rows of cells, each heading on its own row)
        const int w = std::max(1, width() - barW()), cell = cellW();
        const int cols = std::max(1, w / cell);
        int y = 0, col = 0;
        for (const StampRow& r : rows_) {
            if (!stampsGrid() || r.heading) {
                if (col) y += cell, col = 0;
                tops_.push_back(y), rects_.push_back({0, y, w, y + rowH(r.heading)}), y += rowH(r.heading);
                continue;
            }
            tops_.push_back(y), rects_.push_back({col * cell, y, col * cell + cell, y + cell});
            if (++col == cols) col = 0, y += cell;
        }
        if (col) y += cell;
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
        for (size_t i = 0; i < rows_.size() && i < rects_.size(); ++i)
            if (y >= rects_[i].top && y < rects_[i].bottom && p.x >= rects_[i].left && p.x < rects_[i].right) return int(i);
        return -1;
    }

    void paint(draw::Canvas& c) override {
        const TilesScope scope = here();
        if (viewShown_ != stampsGrid() * 3 + stampsSize()) viewShown_ = stampsGrid() * 3 + stampsSize(), stale_ = true;
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
        if (dragging_ && dropShown_) {  // the drop's place: its folder's heading with an outline, or (no folder) the list's top edge
            bool shownOn = false;
            for (size_t i = 0; i < rows_.size() && i < rects_.size(); ++i)
                if (rows_[i].heading && narrow(rows_[i].key) == drop_) {
                    const int top = topH() + rects_[i].top - scroll_;
                    c.roundBox({1, top + 1, w - 1, topH() + rects_[i].bottom - scroll_ - 1}, kSel, kAccent, float(scale(3)));
                    shownOn = true;
                }
            if (!shownOn && drop_.empty()) c.fill({0, topH(), w, topH() + scale(2)}, kAccent);
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            const StampRow& r = rows_[i];
            const int top = topH() + rects_[i].top - scroll_, bottom = topH() + rects_[i].bottom - scroll_;
            if (bottom < topH() || top > h) continue;
            const int indent = scale(4) + r.depth * scale(14);
            if (r.heading) {  // a chevron (open: down, closed: right), the name, then a line to the right edge
                const bool closed = gStampsClosed.count(r.key) != 0;
                c.icon({indent, top, indent + scale(14), bottom}, int(closed ? UiIcon::ChevronRight : UiIcon::ChevronDown), int(i) == hover_ ? kText : kDim, scale(16));
                const draw::Rect t{indent + scale(16), top, w - scale(6), bottom};
                c.text(t, r.label, kText, draw::kMiddle | draw::kEllipsis);
                const int mid = (top + bottom) / 2, lineL = t.left + c.measure(r.label) + scale(8);
                if (lineL < w - scale(6)) c.fill({lineL, mid, w - scale(6), mid + 1}, kBorder);
                continue;
            }
            if (r.index < 0 || r.index >= int(list.size())) continue;
            const TerrainStamp& s = list[size_t(r.index)];
            const bool picked = !data->stampName.empty() && widen(s.name) == data->stampName;
            if (stampsGrid()) {  // a cell: the picture, the name under it (medium and large)
                const draw::Rect f{rects_[i].left + 1, top + 1, rects_[i].right - 1, bottom - 1};
                if (picked) kitpaint::drawListRow(c, f, true, false);
                else if (int(i) == hover_) c.fill(f, kRowHot);
                const int nameH = stampsSize() ? scale(16) : 0;
                const draw::Rect pic{f.left + scale(3), f.top + scale(3), f.right - scale(3), f.bottom - scale(3) - nameH};
                stampsDrawPicture(c, s, pic);
                if (nameH) c.text({f.left + scale(3), pic.bottom, f.right - scale(3), f.bottom}, r.label, kText, draw::kCenter | draw::kMiddle | draw::kEllipsis);
                continue;
            }
            const draw::Rect f{1, top + 1, w - 1, bottom - 1};
            if (picked) kitpaint::drawListRow(c, f, true, false);
            else if (int(i) == hover_) c.fill(f, kRowHot);
            const draw::Rect pic{indent + scale(2), top + scale(3), indent + scale(2) + (bottom - top) - scale(6), bottom - scale(3)};
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
            if (press_ >= 0 && !dragging_ && std::abs(e.pt.x - pressAt_.x) + std::abs(e.pt.y - pressAt_.y) > scale(6)) {
                dragging_ = true;
                if (draw::Host* h = host()) h->capture(*this, true);  // (the release comes here, also outside the list)
            }
            if (dragging_) {  // (the folder it goes into: under the mouse)
                const std::string to = dropFolder(e.pt);
                if (!dropShown_ || to != drop_) drop_ = to, dropShown_ = true, invalidate();
            }
            return true;
        }
        case draw::MouseKind::Leave:
            if (hover_ >= 0) hover_ = -1, invalidate();
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick: {
            if (e.button != 0) return e.button == 1;
            const int r = rowAt(e.pt);
            press_ = -1, dragging_ = dropShown_ = false;
            if (r < 0) return true;
            if (!rows_[size_t(r)].heading && rows_[size_t(r)].index >= 0 && rows_[size_t(r)].index < int(stampsList().size()) &&
                !stampsList()[size_t(rows_[size_t(r)].index)].file.empty())
                press_ = rows_[size_t(r)].index, pressAt_ = e.pt;  // (the user's own: a drag moves it to a folder)
            const StampRow row = rows_[size_t(r)];
            if (row.heading) {  // close it, or open it again
                if (!gStampsClosed.erase(row.key)) gStampsClosed.insert(row.key);
                stampsPanelsRepaint();  // (each panel's list shows it so)
                return true;
            }
            const auto& list = stampsList();
            if (row.index < 0 || row.index >= int(list.size())) return true;
            if ((e.mods & draw::kAlt) && appState().ed) {  // its tiles: the mask (each value once), or no mask (the same again)
                const TerrainStamp& s = list[size_t(row.index)];
                std::vector<uint16_t> mask;
                for (size_t i = 0; i < s.tiles.size() && i < s.mask.size(); ++i)  // (its holes are not in it)
                    if (const uint16_t v = s.tiles[i]; s.mask[i] && std::find(mask.begin(), mask.end(), v) == mask.end()) mask.push_back(v);
                std::vector<uint16_t> now = appState().ed->tileMask(), sorted = mask;
                std::sort(now.begin(), now.end()), std::sort(sorted.begin(), sorted.end());
                if (now == sorted) mask.clear();
                setMessage(mask.empty() ? std::wstring(tr(L"No mask: painting changes all tiles"))
                                        : tr(L"Mask: painting in Tile and Custom changes only the tiles of ") + widen(s.name) + L" (" +
                                              std::to_wstring(mask.size()) + tr(L" tiles). Alt+click it again: no mask"));
                appState().ed->setTileMask(std::move(mask));
                tilesPanelRepaint();
                return true;
            }
            if (appState().mode != Mode::Terrain) {  // (another mode: the pick goes to Terrain, as a palette's)
                if (state) activatePanel(state);
                else setMode(Mode::Terrain);
            }
            stampsUse(list[size_t(row.index)]);
            shellFocusView();
            return true;
        }
        case draw::MouseKind::Up: {
            if (e.button == 0) {
                const int index = std::exchange(press_, -1);
                const bool dropped = std::exchange(dragging_, false);
                dropShown_ = false;
                if (dropped)
                    if (draw::Host* h = host()) h->capture(*this, false);
                invalidate();
                if (dropped && (e.pt.x < 0 || e.pt.y < topH() || e.pt.x >= width() || e.pt.y >= height())) return true;  // (out of the list: no move)
                const auto& list = stampsList();
                if (dropped && index >= 0 && index < int(list.size())) {
                    const TerrainStamp s = list[size_t(index)];
                    const std::string to = dropFolder(e.pt);
                    shellPost([s, to] { stampsMoveTo(s, to); });
                }
                return true;
            }
            if (e.button != 1) return false;
            const int r = rowAt(e.pt);
            const auto& list = stampsList();
            const bool item = r >= 0 && !rows_[size_t(r)].heading && rows_[size_t(r)].index >= 0 && rows_[size_t(r)].index < int(list.size());
            const bool folder = r >= 0 && rows_[size_t(r)].heading;
            const std::string in = folder ? narrow(rows_[size_t(r)].key) : item ? list[size_t(rows_[size_t(r)].index)].folder : std::string();
            shellStampsMenu(*this, e.pt, item ? &list[size_t(rows_[size_t(r)].index)] : nullptr, in, folder);
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
    // The folder that a drop at `p` puts custom terrain into: a heading's folder, an item's folder, else none (out of the
    // folders).
    std::string dropFolder(draw::Point p) const {
        int r = rowAt(p);
        if (r < 0 && p.y >= topH()) {  // (between a grid's cells, or after a folder's last cell: the row before it; under all rows: none)
            const int y = p.y - topH() + scroll_;
            if (y >= contentH_) return {};
            for (size_t i = 0; i < rows_.size() && i < rects_.size(); ++i)
                if (rects_[i].top <= y && (rects_[i].top < y || rects_[i].left <= p.x)) r = int(i);
        }
        if (r < 0) return {};
        const StampRow& row = rows_[size_t(r)];
        if (row.heading) return narrow(row.key);
        const auto& list = stampsList();
        return row.index >= 0 && row.index < int(list.size()) ? list[size_t(row.index)].folder : std::string();
    }
    void captureLost() override {
        if (dragging_) press_ = -1, dragging_ = dropShown_ = false, invalidate();
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
            x = o.x + (rects_[i].left + rects_[i].right) / 2, y = o.y + topH() + (rects_[i].top + rects_[i].bottom) / 2 - scroll_;
            return shown() && y - o.y >= topH() && y - o.y < height();
        }
        return false;
    }

private:
    stardraft::kit::IconButton sort_{stardraft::kit::IconGlyph::ofIcon(int(UiIcon::Sort)), stardraft::kit::IconStyle::Boxed, L"",
                                     [this] { shellStampsButtonMenu(sort_, true); }};
    stardraft::kit::Button view_{L"View", stardraft::kit::Tone::Normal, [this] { shellStampsButtonMenu(view_, false); }};
    stardraft::kit::TextField search_;
    stardraft::kit::ScrollBar bar_;
    std::vector<StampRow> rows_;
    std::vector<int> tops_;
    std::vector<draw::Rect> rects_;  // (each row's place in the content: a list's row, a grid's cell)
    int contentH_ = 0, scroll_ = 0, hover_ = -1, viewShown_ = -1, builtFor_ = -2;
    int press_ = -1;              // the item pressed (its index in stampsList()): a drag moves it to a folder
    draw::Point pressAt_{};
    bool dragging_ = false, dropShown_ = false;
    std::string drop_;            // the folder that the drop goes into (shown while dragging_)
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
