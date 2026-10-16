// The Rectangle mode's Tiles panel (portable, core.inc): the filter box, the search box and the grid of every tile of
// the map's tileset (CV5), group by group and in each group by its index, null tiles too. It takes the place of the
// terrain palette while the Terrain tool strip's Rectangle is on (tiles_edit.inc has the tools on the map). The filter
// box shows all tiles, or only one terrain type's tiles (the terrain types of Isometric mode; a tile group's terrain
// types come from the tile rules: tileGroupTerrains), the favorites, the doodads' tiles or the null tiles.
// The views: TilesPanelView (the filter and the search, kit widgets) and TilesGridView (the grid, with its footer: the
// tile under the mouse). Both programs use the instances below, gTilesPanel and gTilesGrid. The Win32 program shows the
// panel in a view window and the grid in a view window of its own, with the native vertical bar (tiles_view.inc); the SDL
// program puts the grid in the panel (TilesPanelView::holdGrid) and places the panel (sdl_main_view.inc).

int tilesFooterH() { return scale(26); }
int tilesCell() { return scale(32); }
int tilesGap() { return scale(3); }
int tilesPad() { return scale(4); }

void shellFocusView();
std::wstring shellTr(const wchar_t* english);
void setMessage(const std::wstring& m);
void tilesSetFilter(int index);

// Favorites (GitHub feedback #111): tiles the user keeps for each tileset, in the program settings (not in the map).
// Right-click a tile in the list to add it or take it out; the filter box shows them.
std::vector<uint16_t>& tilesFavorites(int era) {
    static std::vector<uint16_t> fav[8];
    static bool loaded[8] = {};
    era &= 7;
    if (!loaded[era]) {
        loaded[era] = true;
        const std::wstring name = L"TileFavorites" + std::to_wstring(era);
        std::vector<uint8_t> b;
        if (prefs::bytes(name, b))  // tile values, 2 bytes each (low byte first)
            for (size_t i = 0; i + 1 < b.size(); i += 2) fav[era].push_back(uint16_t(b[i] | b[i + 1] << 8));
    }
    return fav[era];
}
void tilesSaveFavorites(int era) {
    if (gSelfTest) return;  // tests never change the user's settings
    const auto& f = tilesFavorites(era);
    const std::wstring name = L"TileFavorites" + std::to_wstring(era & 7);
    std::vector<uint8_t> b;
    for (uint16_t v : f) b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8));
    prefs::setBytes(name, b);
}

// --- The grid ---

// The tile's picture: its megatile in the art the map shows: SD, HD (HD2 resolution is plenty here) or Carbot
// (feedback #52: the grid was SD only).
draw::Bitmap& tilesPixels(uint16_t value) {
    AppState& app = appState();
    const int mode = app.carbot ? 2 : app.quality == GraphicsQuality::SD ? 0 : 1;
    if (mode != gTiles.pixelsMode) gTiles.pixels.clear(), gTiles.pixelsMode = mode;
    if (gTiles.pixels.size() > 4096 && !gTiles.pixels.count(value)) gTiles.pixels.clear();  // a long scroll: keep the memory small
    if (mode && (gTiles.hdEra != app.ed->era() || gTiles.hdMode != mode)) {
        std::string err;
        gTiles.hdEra = app.ed->era(), gTiles.hdMode = mode;
        if (!loadHdTiles(app.data, app.ed->era(), GraphicsQuality::HD2, gTiles.hd, err, mode == 2)) gTiles.hd = {};
    }
    auto [it, fresh] = gTiles.pixels.try_emplace(value);
    if (fresh) {
        const HdTiles* hd = mode && !gTiles.hd.tiles.empty() ? &gTiles.hd : nullptr;
        const Sprite pic = mapTilesPicture(app.ed->tileset(), {value}, 1, 0, 0, 1, 1, hd);
        const int size = pic.width > 0 && pic.width == pic.height ? pic.width : 32;
        draw::Bitmap& b = it->second;
        b.resize(size, size);
        b.opaque = true;
        uint8_t* px = reinterpret_cast<uint8_t*>(b.pixels());  // bytes R, G, B, A, as the picture's
        for (size_t i = 0; i < size_t(size) * size && i * 4 + 3 < pic.rgba.size(); ++i)
            px[i * 4] = pic.rgba[i * 4], px[i * 4 + 1] = pic.rgba[i * 4 + 1], px[i * 4 + 2] = pic.rgba[i * 4 + 2], px[i * 4 + 3] = 255;
        b.changed();
    }
    return it->second;
}

class TilesGridView final : public draw::View {
public:
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    void captureLost() override {
        if (gTiles.boxing) gTiles.boxing = false, invalidate();
    }
    void resized() override {
        if (width() != gTiles.layoutW) tilesRebuild();
        else tilesScrollTo(gTiles.scroll);
    }
    // The window's bar (the Win32 program's grid window): a line is 36 px, a page the visible height.
    bool vscroll(int code, int pos, int track, int page) override {
        switch (code) {
        case 0: pos -= scale(36); break;  // SB_LINEUP
        case 1: pos += scale(36); break;  // SB_LINEDOWN
        case 2: pos -= page; break;       // SB_PAGEUP
        case 3: pos += page; break;       // SB_PAGEDOWN
        case 4:                           // SB_THUMBPOSITION
        case 5: pos = track; break;       // SB_THUMBTRACK
        case 6: pos = 0; break;           // SB_TOP
        case 7: pos = gTiles.contentH; break;  // SB_BOTTOM
        default: return false;
        }
        tilesScrollTo(pos);
        return true;
    }
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, gTiles.contentH - 1), page = std::max(1, tilesViewH()), pos = gTiles.scroll;
        return true;
    }
    bool windowScrollBarDisables() const override { return true; }
    std::wstring feedbackName(draw::Point pt) override {
        const int v = hit(pt.x, pt.y);
        return v >= 0 ? tilesValueLabel(v) : L"the Tiles list";
    }

    // The tile value at a point of the grid, or -1.
    int hit(int x, int y) {
        tilesFresh();
        if (!appState().ed || y >= tilesViewH() || gTiles.values.empty()) return -1;
        const int pad = tilesPad(), step = tilesCell() + tilesGap(), cy = y + gTiles.scroll;
        if (x < pad || cy < pad) return -1;
        const int col = (x - pad) / step, row = (cy - pad) / step;
        if (col >= gTiles.perRow || (x - pad) % step >= tilesCell() || (cy - pad) % step >= tilesCell()) return -1;
        const size_t k = size_t(row) * size_t(gTiles.perRow) + size_t(col);
        return k < gTiles.values.size() ? gTiles.values[k] : -1;
    }

private:
    void boxSelect();
    void favoritesMenu(int v);
};

// The filter box: a kit dropdown that knows whether a change came from the mouse wheel over the closed box (the wheel
// steps the filter and leaves the keyboard where it is, as the native combo box's wheel did; a pick gives it to the map).
class TilesFilterBox final : public stardraft::kit::Dropdown {
public:
    bool mouse(const draw::MouseEvent& e) override {
        wheeling = e.kind == draw::MouseKind::Wheel;
        const bool used = Dropdown::mouse(e);
        wheeling = false;
        return used;
    }
    bool wheeling = false;
};

class TilesPanelView final : public draw::View {
public:
    TilesPanelView() {
        search_.setPlaceholder(L"Search tiles: group number, terrain, doodad");
        search_.onChange = [] { gTiles.scroll = 0, tilesRebuild(); };
        filter_.onChange = [this](int i) {
            tilesSetFilter(i);
            if (!filter_.wheeling) shellFocusView();
        };
        add(filter_, {0, 0, 0, 0});
        add(search_, {0, 0, 0, 0});
    }
    SearchBox& search() { return search_; }
    stardraft::kit::Dropdown& filter() { return filter_; }
    // The SDL program: the grid is a child of the panel, under the search box (the Win32 program places its window there).
    void holdGrid(TilesGridView& g) { grid_ = &g, add(g, gridRect()); }
    // Where the grid goes: under the filter box and the search box, to the panel's bottom.
    draw::Rect gridRect() const {
        const int y = 2 * (scale(24) + scale(6));
        return {0, y, width(), std::max(y + 1, height())};
    }
    void resized() override {
        const int rowH = scale(24), pad = scale(6);
        filter_.setFrame({0, 0, width(), scale(kFilterH)});
        search_.setFrame({0, rowH + pad, width(), rowH + pad + rowH});
        if (grid_) grid_->setFrame(gridRect());
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    std::wstring feedbackName(draw::Point) override { return L"the Tiles list"; }

    static constexpr int kFilterH = 23;  // the native drop-down list was 23 px high (its height follows the UI font)

private:
    TilesFilterBox filter_;
    SearchBox search_;
    TilesGridView* grid_ = nullptr;
};

TilesGridView gTilesGrid;
TilesPanelView gTilesPanel;

// The panel's side of the tile logic (tiles_edit.inc calls these).
void tilesPanelRepaint() { gTilesGrid.invalidate(); }
void tilesPanelClearSearch() { gTilesPanel.search().setText(L""); }
void tilesPanelShowAllFilter() { gTilesPanel.filter().setValue(0); }
// The search box's text (its change rebuilds the grid).
void tilesSetSearch(const std::wstring& t) { gTilesPanel.search().setText(t); }

// The filter box for the map's tileset: all tiles, each terrain type of Isometric mode that has tiles, the doodads'
// tiles, the null tiles. The choice stays when its name is in the new list too.
void tilesFillFilters() {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen() || gTiles.filtersEra == app.ed->era()) return;
    gTiles.filtersEra = app.ed->era();
    const std::wstring was = gTiles.filter < int(gTiles.filters.size()) ? gTiles.filters[size_t(gTiles.filter)].label : L"";
    gTiles.filters.clear();
    gTiles.filters.push_back({L"All tiles", 0, 0});
    gTiles.filters.push_back({L"All tiles but the null tiles", 4, 0});  // (feedback #49)
    gTiles.filters.push_back({L"Favorites", 5, 0});
    const IsomTerrainTable* t = IsomTerrainTable::forEra(app.ed->era());
    const auto& gs = app.ed->tileGroups();
    if (t)
        for (uint16_t i = 1; i <= t->terrainCount(); ++i)
            if (t->isTerrain(i) && std::any_of(gs.begin(), gs.end(), [&](const Editor::TileGroupEntry& e) { return e.terrains[0] == i || e.terrains[1] == i; }))
                gTiles.filters.push_back({widen(t->terrainName(i)), 1, i});
    if (std::any_of(gs.begin(), gs.end(), [](const Editor::TileGroupEntry& e) { return e.doodad >= 0; })) gTiles.filters.push_back({L"Doodad tiles", 2, 0});
    gTiles.filters.push_back({L"Null tiles", 3, 0});
    gTiles.filter = 0;
    std::vector<stardraft::kit::DropdownItem> items;
    for (size_t i = 0; i < gTiles.filters.size(); ++i) {
        items.push_back({gTiles.filters[i].label});
        if (gTiles.filters[i].label == was) gTiles.filter = int(i);
    }
    gTilesPanel.filter().setItems(std::move(items));
    gTilesPanel.filter().setValue(gTiles.filter);
}
// Is the tile value in the filter's set?
bool tilesInFilter(uint16_t v) {
    if (gTiles.filter <= 0 || gTiles.filter >= int(gTiles.filters.size())) return true;
    const auto& f = gTiles.filters[size_t(gTiles.filter)];
    if (f.kind == 3) return tilesIsNull(v);
    if (f.kind == 5) {
        const auto& fav = tilesFavorites(appState().ed->era());
        return std::find(fav.begin(), fav.end(), v) != fav.end();
    }
    if (tilesIsNull(v)) return false;
    if (f.kind == 4) return true;  // every tile that is not null
    const Editor::TileGroupEntry* e = tilesEntryOfGroup(v >> 4);
    if (!e) return false;
    if (f.kind == 2) return e->doodad >= 0;
    return e->terrains[0] == f.terrain || e->terrains[1] == f.terrain;
}

// The grid for the search and the grid's width: every tile of the tileset, in its order.
void tilesRebuild() {
    AppState& app = appState();
    gTiles.values.clear();
    gTiles.hover = -1;
    if (!app.ed || !app.ed->isOpen()) return;
    const int pad = tilesPad(), cell = tilesCell(), gap = tilesGap(), w = gTilesGrid.width();
    gTiles.perRow = std::max(1, (w - 2 * pad + gap) / (cell + gap));
    std::wstring text = gTilesPanel.search().text();
    std::transform(text.begin(), text.end(), text.begin(), ::towlower);
    std::vector<std::wstring> words;
    auto space = [](wchar_t ch) { return ch == L' ' || (ch >= 9 && ch <= 13) || ch == 0x3000; };
    for (size_t i = 0; i < text.size();) {  // the words, split at white space
        while (i < text.size() && space(text[i])) ++i;
        const size_t s = i;
        while (i < text.size() && !space(text[i])) ++i;
        if (i > s) words.push_back(text.substr(s, i - s));
    }
    tilesFillFilters();
    const size_t groups = std::min<size_t>(app.ed->tileset().groups.size(), 4096);
    for (size_t g = 0; g < groups; ++g) {
        if (!words.empty()) {  // the search: the group's number and what it is
            std::wstring hay = L"group " + std::to_wstring(g) + L" " + tilesGroupNote(int(g));
            std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
            bool all = true;
            for (const auto& wd : words) all = all && hay.find(wd) != std::wstring::npos;
            if (!all) continue;
        }
        for (int s = 0; s < 16; ++s)
            if (tilesInFilter(uint16_t(g << 4 | s))) gTiles.values.push_back(uint16_t(g << 4 | s));
    }
    const int rows = (int(gTiles.values.size()) + gTiles.perRow - 1) / gTiles.perRow;
    gTiles.contentH = pad + rows * (cell + gap) + pad;
    gTiles.layoutW = w;
    gTiles.builtFor = app.ed.get(), gTiles.builtEra = app.ed->era();
    tilesScrollTo(gTiles.scroll);
}

int tilesViewH() { return std::max(0, gTilesGrid.height() - tilesFooterH()); }

void tilesScrollTo(int pos) {
    gTiles.scroll = std::clamp(pos, 0, std::max(0, gTiles.contentH - tilesViewH()));
    gTilesGrid.invalidate();  // (the window's bar follows: its host asks windowScrollBar after the paint)
}

// Where the k-th tile of the grid is, in content coordinates.
RECT tilesCellRect(int k) {
    const int pad = tilesPad(), cell = tilesCell(), gap = tilesGap();
    const int x = pad + (k % gTiles.perRow) * (cell + gap), y = pad + (k / gTiles.perRow) * (cell + gap);
    return {x, y, x + cell, y + cell};
}

// The filter box's choice (clamped): the grid shows those tiles.
void tilesSetFilter(int index) {
    if (gTiles.filters.empty()) return;
    gTiles.filter = std::clamp(index, 0, int(gTiles.filters.size()) - 1);
    gTilesPanel.filter().setValue(gTiles.filter);
    gTiles.scroll = 0;
    tilesRebuild();
}

// The box: the tiles whose cells it touches, added to the ones selected before it.
void TilesGridView::boxSelect() {
    const int pad = tilesPad(), step = tilesCell() + tilesGap();
    const int x0 = std::min(gTiles.boxFrom.x, gTiles.boxTo.x), x1 = std::max(gTiles.boxFrom.x, gTiles.boxTo.x);
    const int y0 = std::min(gTiles.boxFrom.y, gTiles.boxTo.y), y1 = std::max(gTiles.boxFrom.y, gTiles.boxTo.y);
    const int c0 = std::max(0, (x0 - pad) / step), c1 = std::min(gTiles.perRow - 1, (x1 - pad) / step);
    const int r0 = std::max(0, (y0 - pad) / step), r1 = std::max(0, (y1 - pad) / step);
    std::vector<uint16_t> chosen = gTiles.boxBase;
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            const size_t k = size_t(r) * size_t(gTiles.perRow) + size_t(c);
            if (k < gTiles.values.size() && std::find(chosen.begin(), chosen.end(), gTiles.values[k]) == chosen.end()) chosen.push_back(gTiles.values[k]);
        }
    if (chosen.empty()) return;
    gTiles.chosen = chosen;
    gTiles.picked = chosen.back();
    invalidate();
}

void TilesGridView::paint(draw::Canvas& c) {
    tilesFresh();
    AppState& app = appState();
    const int w = width(), h = height();
    c.fill({0, 0, w, h}, kBg);
    const int viewH = tilesViewH();
    if (app.ed && app.ed->isOpen()) {
        c.pushClip({0, 0, w, viewH});
        const int step = tilesCell() + tilesGap();
        const int firstRow = std::max(0, (gTiles.scroll - tilesPad()) / step), lastRow = (gTiles.scroll + viewH) / step + 1;
        for (int row = firstRow; row <= lastRow; ++row)
            for (int col = 0; col < gTiles.perRow; ++col) {
                const size_t k = size_t(row) * size_t(gTiles.perRow) + size_t(col);
                if (k >= gTiles.values.size()) break;
                const uint16_t v = gTiles.values[k];
                const RECT cr = tilesCellRect(int(k));
                const draw::Rect cell{cr.left, cr.top - gTiles.scroll, cr.right, cr.bottom - gTiles.scroll};
                draw::Bitmap& px = tilesPixels(v);
                c.bitmap(px, {0, 0, px.width(), px.height()}, cell);
                if (v == gTiles.picked || (gTiles.chosen.size() > 1 && std::find(gTiles.chosen.begin(), gTiles.chosen.end(), v) != gTiles.chosen.end())) {
                    const int g = scale(2);
                    c.frame({cell.left - g, cell.top - g, cell.right + g, cell.bottom + g}, kAccent, scale(2));
                } else if (v == gTiles.hover) {
                    c.frame({cell.left - 1, cell.top - 1, cell.right + 1, cell.bottom + 1}, kBorderStrong, 1);
                } else if (tilesIsNull(v)) {  // a null tile: its frame, so it shows on the dark background
                    c.frame(cell, kBorder, 1);
                }
            }
        c.popClip();
        if (gTiles.values.empty()) c.text({scale(8), scale(8), w - scale(8), viewH}, shellTr(L"None"), kDim, draw::kLeft);
    }
    if (gTiles.boxing && gTiles.boxed) {  // the box being dragged
        const draw::Rect b{std::min(gTiles.boxFrom.x, gTiles.boxTo.x), std::min(gTiles.boxFrom.y, gTiles.boxTo.y) - gTiles.scroll,
                           std::max(gTiles.boxFrom.x, gTiles.boxTo.x), std::max(gTiles.boxFrom.y, gTiles.boxTo.y) - gTiles.scroll};
        c.roundBox(b, draw::kNone, kAccent, float(scale(stardraft::kit::kRadius)));
    }
    // The tile under the mouse (or the picked one).
    c.fill({0, viewH, w, h}, kCtl);
    const int show = gTiles.hover >= 0 ? gTiles.hover : gTiles.picked;
    const std::wstring name = !app.ed || !app.ed->isOpen() ? L"" : show >= 0 ? tilesValueLabel(show) : L"Click a tile to place it";
    c.text({scale(6), viewH, w - scale(4), h}, name, show >= 0 ? kText : kDim, draw::kMiddle | draw::kEllipsis);
}

bool TilesGridView::mouse(const draw::MouseEvent& e) {
    switch (e.kind) {
    case draw::MouseKind::Pinch: return false;
    case draw::MouseKind::Wheel:
        if (e.horizontal) return false;
        tilesScrollTo(gTiles.scroll - int(e.wheel * float(scale(90))));
        return true;
    case draw::MouseKind::Move: {
        if (gTiles.boxing) {  // the box: the tiles in it, as it grows
            gTiles.boxTo = {e.pt.x, e.pt.y + gTiles.scroll};
            if (std::abs(gTiles.boxTo.x - gTiles.boxFrom.x) + std::abs(gTiles.boxTo.y - gTiles.boxFrom.y) > scale(6)) gTiles.boxed = true;
            if (gTiles.boxed) boxSelect();
            invalidate();
        }
        const int v = hit(e.pt.x, e.pt.y);
        if (v != gTiles.hover) gTiles.hover = v, invalidate();
        return true;
    }
    case draw::MouseKind::Leave:
        if (gTiles.hover >= 0) gTiles.hover = -1, invalidate();
        return true;
    case draw::MouseKind::Down:
    case draw::MouseKind::DoubleClick: {
        if (e.button != 0) return e.button == 1;
        const bool ctrl = (e.mods & draw::kCtrl) != 0, shift = (e.mods & draw::kShift) != 0;
        const int v = hit(e.pt.x, e.pt.y);
        if (v >= 0 && (ctrl || shift) && gTiles.picked >= 0) {  // several tiles, for Spray (#81)
            if (shift) {  // the tiles from the last pick to this one, in the grid's order
                auto at = [](int value) { return int(std::find(gTiles.values.begin(), gTiles.values.end(), uint16_t(value)) - gTiles.values.begin()); };
                const int a = std::min(at(gTiles.picked), at(v)), b = std::max(at(gTiles.picked), at(v));
                if (!ctrl) gTiles.chosen.clear();
                for (int k = a; k <= b && k < int(gTiles.values.size()); ++k)
                    if (std::find(gTiles.chosen.begin(), gTiles.chosen.end(), gTiles.values[size_t(k)]) == gTiles.chosen.end())
                        gTiles.chosen.push_back(gTiles.values[size_t(k)]);
            } else if (auto it = std::find(gTiles.chosen.begin(), gTiles.chosen.end(), uint16_t(v)); it != gTiles.chosen.end()) {
                if (gTiles.chosen.size() > 1) gTiles.chosen.erase(it);
            } else {
                gTiles.chosen.push_back(uint16_t(v));
            }
            gTiles.picked = gTiles.chosen.back();
            setMessage(L"Selected " + std::to_wstring(gTiles.chosen.size()) + L" tiles: Brush, Spray and Fill place one of them at random");
            invalidate();
            appState().dirty = true;
        } else if (v >= 0) {
            tilesPick(v, false);
        }
        if (v >= 0 && !shift) {  // a drag from here: a box (Ctrl: added to the tiles selected before)
            gTiles.boxing = true, gTiles.boxed = false;
            gTiles.boxFrom = gTiles.boxTo = {e.pt.x, e.pt.y + gTiles.scroll};
            gTiles.boxBase = ctrl ? gTiles.chosen : std::vector<uint16_t>{};
            if (draw::Host* h = host()) h->capture(*this, true);
        }
        shellFocusView();
        return true;
    }
    case draw::MouseKind::Up:
        if (e.button == 1) {  // a tile: add it (or the selected tiles, when it is one of them) to the favorites, or take it out
            const int v = hit(e.pt.x, e.pt.y);
            if (v < 0 || !appState().ed) return false;
            favoritesMenu(v);
            return true;
        }
        if (e.button == 0 && gTiles.boxing) {
            gTiles.boxing = false;
            if (draw::Host* h = host()) h->capture(*this, false);
            if (gTiles.boxed && gTiles.chosen.size() > 1)
                setMessage(L"Selected " + std::to_wstring(gTiles.chosen.size()) + L" tiles: Brush, Spray and Fill place one of them at random");
            invalidate();
        }
        return true;
    }
    return false;
}

void TilesGridView::favoritesMenu(int v) {
    std::vector<uint16_t> which{uint16_t(v)};
    if (gTiles.chosen.size() > 1 && std::find(gTiles.chosen.begin(), gTiles.chosen.end(), uint16_t(v)) != gTiles.chosen.end()) which = gTiles.chosen;
    const int era = appState().ed->era();
    auto& fav = tilesFavorites(era);
    const bool in = std::find(fav.begin(), fav.end(), uint16_t(v)) != fav.end();
    const std::wstring n = which.size() > 1 ? L" (" + std::to_wstring(which.size()) + L" tiles)" : L"";
    const int pick = shellContextMenu({(in ? L"Remove from favorites" : L"Add to favorites") + n, L"Show the favorites"});
    if (pick == 1) {
        for (uint16_t t : which) {
            const auto it = std::find(fav.begin(), fav.end(), t);
            if (in && it != fav.end()) fav.erase(it);
            else if (!in && it == fav.end()) fav.push_back(t);
        }
        tilesSaveFavorites(era);
        setMessage(std::wstring(in ? L"Removed from the favorites: " : L"Added to the favorites: ") + std::to_wstring(which.size()) +
                   (which.size() == 1 ? L" tile" : L" tiles") + L". The Favorites filter shows them.");
        if (gTiles.filter < int(gTiles.filters.size()) && gTiles.filters[size_t(gTiles.filter)].kind == 5) tilesRebuild();
    } else if (pick == 2) {
        for (size_t i = 0; i < gTiles.filters.size(); ++i)
            if (gTiles.filters[i].kind == 5) tilesSetFilter(int(i));
    }
}
