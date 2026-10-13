#include <bit>

// --- Rectangle mode: tiles as they are (roadmap N.7) --------------------------------------------------------------
// The Terrain tool strip's Isometric | Rectangle choice. In Rectangle mode the terrain tools work on tiles, as they
// are, without the terrain correction, as StarEdit's rectangular terrain and SCMDraft's tile palette do
// (Editor::placeTiles): the Brush places the picked tile in the brush's size and shape (a click, or along a drag), the
// Spray places it at random in the brush, Fill fills the connected tiles of one value (Shift: every tile of that value),
// Select selects a rectangle of tiles and Ctrl+C / Ctrl+V copy and paste the tiles as they are. Ctrl+click picks the
// tile under the cursor. The panel takes the place of the terrain palette: one grid of every tile of the map's tileset
// (CV5), group by group and in each group by its index, null tiles too, with a search box. The filter box above it
// shows all tiles, or only one terrain type's tiles (the terrain types of Isometric mode; a tile group's terrain types
// come from the tile rules: tileGroupTerrains), the doodads' tiles or the null tiles. Later ISOM brushes keep
// the placed tiles unless they change the ISOM of their rects.
// main.cpp has only the hooks: the choice in the tool strip, tilesSync (layout, fillToolCombo), the map clicks, the
// preview in buildScene, copy and paste, and the datashot spec.

struct TilesView {
    HWND wnd = nullptr;     // the panel: the search box, the list
    HWND search = nullptr;
    HWND filterBox = nullptr;  // "All tiles", the terrain types, "Doodad tiles", "Null tiles"
    HWND list = nullptr;    // the tiles, scrolled
    std::vector<uint16_t> values;  // the tile values shown (group << 4 | index), in the tileset's order
    int perRow = 1;
    int scroll = 0, contentH = 0, layoutW = -1;
    int picked = -1;        // the tile value to place (group << 4 | subtile), -1: none
    std::vector<uint16_t> chosen;  // Ctrl+click and Shift+click: several tiles; Spray places one of them at random (#81)
    int hover = -1;         // the tile value under the mouse in the list
    // A box dragged over the grid selects the tiles in it (GitHub feedback #110): from the press, in content pixels
    // (the scroll added); Ctrl+drag adds to the tiles selected before.
    bool boxing = false, boxed = false;
    POINT boxFrom{}, boxTo{};
    std::vector<uint16_t> boxBase;
    const Editor* builtFor = nullptr;
    int builtEra = -1;
    std::map<uint16_t, std::vector<uint8_t>> pixels;  // 32 x 32 BGRA per tile value, for builtFor's tileset
    bool shown = false;
    // The filter box's entries: kind 0 all, 1 a terrain type (terrain), 2 doodad tiles, 3 null tiles, 4 all but null.
    struct Filter {
        std::wstring label;
        int kind = 0;
        uint16_t terrain = 0;
    };
    std::vector<Filter> filters;
    int filter = 0;  // the filters' index that the grid shows
    int filtersEra = -1;
};
TilesView gTiles;

bool tilesOn() { return app.ed && app.ed->isOpen() && gWorkspace == Workspace::Map && app.mode == Mode::Terrain && app.terrainRect; }

// "Dirt", "Dirt / High dirt"; empty when no Blizzard map uses the group.
std::wstring tilesTerrainNames(const Editor::TileGroupEntry& e) {
    const IsomTerrainTable* t = IsomTerrainTable::forEra(app.ed->era());
    if (!t || !e.terrains[0]) return L"";
    std::wstring s = widen(t->terrainName(e.terrains[0]));
    if (e.terrains[1]) s += L" / " + widen(t->terrainName(e.terrains[1]));
    return s;
}
const Editor::TileGroupEntry* tilesEntryOfGroup(int group) {
    if (!app.ed) return nullptr;
    const auto& gs = app.ed->tileGroups();
    auto it = std::lower_bound(gs.begin(), gs.end(), group, [](const Editor::TileGroupEntry& e, int g) { return int(e.group) < g; });
    return it != gs.end() && int(it->group) == group ? &*it : nullptr;
}
// What a group is: a doodad's, a terrain's, or neither.
std::wstring tilesGroupNote(int group) {
    const Editor::TileGroupEntry* e = tilesEntryOfGroup(group);
    if (!e) return L"Null tiles";
    if (e->doodad >= 0) {
        const DoodadType* d = app.ed->doodadTypes().find(uint16_t(e->doodad));
        std::wstring s = L"Doodad #" + std::to_wstring(e->doodad);
        if (d && d->groupString) s += L" (" + widen(app.ed->unitTypes().statText(d->groupString - 1u)) + L")";
        if (d && d->height > 1) s += L", row " + std::to_wstring(e->doodadRow + 1);
        return s;
    }
    if (const std::wstring t = tilesTerrainNames(*e); !t.empty()) return t;
    return L"Not in Blizzard's maps";
}
bool tilesIsNull(int value) { return app.ed && app.ed->tileset().megatileFor(uint16_t(value)) <= 0; }
std::wstring tilesValueLabel(int value) {
    std::wstring s = L"Group " + std::to_wstring(value >> 4) + L", tile " + std::to_wstring(value & 15) + L"  \x00B7  ";
    if (tilesIsNull(value)) return s + L"Null tile";
    return s + tilesGroupNote(value >> 4);
}

int tilesFooterH() { return scale(26); }
int tilesCell() { return scale(32); }
int tilesGap() { return scale(3); }
int tilesPad() { return scale(4); }

void tilesScrollTo(int pos);

// Favorites (GitHub feedback #111): tiles the user keeps for each tileset, in the program settings (not in the map).
// Right-click a tile in the list to add it or take it out; the filter box shows them.
std::vector<uint16_t>& tilesFavorites(int era) {
    static std::vector<uint16_t> fav[8];
    static bool loaded[8] = {};
    era &= 7;
    if (!loaded[era]) {
        loaded[era] = true;
        const std::wstring name = L"TileFavorites" + std::to_wstring(era);
        DWORD size = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\StarDraft", name.c_str(), RRF_RT_REG_BINARY, nullptr, nullptr, &size) == ERROR_SUCCESS && size >= 2) {
            fav[era].resize(size / 2);
            RegGetValueW(HKEY_CURRENT_USER, L"Software\\StarDraft", name.c_str(), RRF_RT_REG_BINARY, nullptr, fav[era].data(), &size);
        }
    }
    return fav[era];
}
void tilesSetFilter(int index);
void tilesSaveFavorites(int era) {
    if (gSelfTest) return;  // tests never change the user's settings
    const auto& f = tilesFavorites(era);
    const std::wstring name = L"TileFavorites" + std::to_wstring(era & 7);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\StarDraft", name.c_str(), REG_BINARY, f.data(), DWORD(f.size() * 2));
}

// The filter box for the map's tileset: all tiles, each terrain type of Isometric mode that has tiles, the doodads'
// tiles, the null tiles. The choice stays when its name is in the new list too.
void tilesFillFilters() {
    if (!gTiles.filterBox || !app.ed || !app.ed->isOpen() || gTiles.filtersEra == app.ed->era()) return;
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
    SendMessageW(gTiles.filterBox, CB_RESETCONTENT, 0, 0);
    gTiles.filter = 0;
    for (size_t i = 0; i < gTiles.filters.size(); ++i) {
        SendMessageW(gTiles.filterBox, CB_ADDSTRING, 0, LPARAM(gTiles.filters[i].label.c_str()));
        if (gTiles.filters[i].label == was) gTiles.filter = int(i);
    }
    SendMessageW(gTiles.filterBox, CB_SETCURSEL, WPARAM(gTiles.filter), 0);
}
// Is the tile value in the filter's set?
bool tilesInFilter(uint16_t v) {
    if (gTiles.filter <= 0 || gTiles.filter >= int(gTiles.filters.size())) return true;
    const auto& f = gTiles.filters[size_t(gTiles.filter)];
    if (f.kind == 3) return tilesIsNull(v);
    if (f.kind == 5) {
        const auto& fav = tilesFavorites(app.ed->era());
        return std::find(fav.begin(), fav.end(), v) != fav.end();
    }
    if (tilesIsNull(v)) return false;
    if (f.kind == 4) return true;  // every tile that is not null
    const Editor::TileGroupEntry* e = tilesEntryOfGroup(v >> 4);
    if (!e) return false;
    if (f.kind == 2) return e->doodad >= 0;
    return e->terrains[0] == f.terrain || e->terrains[1] == f.terrain;
}

// The grid for the search and the list's width: every tile of the tileset, in its order.
void tilesRebuild() {
    gTiles.values.clear();
    gTiles.hover = -1;
    if (!gTiles.list || !app.ed || !app.ed->isOpen()) return;
    RECT rc;
    GetClientRect(gTiles.list, &rc);
    const int pad = tilesPad(), cell = tilesCell(), gap = tilesGap();
    gTiles.perRow = std::max(1, (int(rc.right) - 2 * pad + gap) / (cell + gap));
    const std::wstring text = lowerText(gTiles.search);
    std::vector<std::wstring> words;
    {
        std::wistringstream in(text);
        std::wstring w;
        while (in >> w) words.push_back(w);
    }
    tilesFillFilters();
    const size_t groups = std::min<size_t>(app.ed->tileset().groups.size(), 4096);
    for (size_t g = 0; g < groups; ++g) {
        if (!words.empty()) {  // the search: the group's number and what it is
            std::wstring hay = L"group " + std::to_wstring(g) + L" " + tilesGroupNote(int(g));
            std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
            bool all = true;
            for (const auto& w : words) all = all && hay.find(w) != std::wstring::npos;
            if (!all) continue;
        }
        for (int s = 0; s < 16; ++s)
            if (tilesInFilter(uint16_t(g << 4 | s))) gTiles.values.push_back(uint16_t(g << 4 | s));
    }
    const int rows = (int(gTiles.values.size()) + gTiles.perRow - 1) / gTiles.perRow;
    gTiles.contentH = pad + rows * (cell + gap) + pad;
    gTiles.layoutW = rc.right;
    gTiles.builtFor = app.ed.get(), gTiles.builtEra = app.ed->era();
    tilesScrollTo(gTiles.scroll);
}

// The grid is for the open map's tileset (another map, or Map properties changed the tileset: build it again).
void tilesFresh() {
    if (!app.ed || !app.ed->isOpen() || (gTiles.builtFor == app.ed.get() && gTiles.builtEra == app.ed->era())) return;
    if (gTiles.builtEra != app.ed->era()) gTiles.picked = -1, gTiles.scroll = 0;
    gTiles.pixels.clear();
    tilesRebuild();
}

int tilesViewH() {
    RECT rc;
    GetClientRect(gTiles.list, &rc);
    return std::max(0, int(rc.bottom) - tilesFooterH());
}

void tilesScrollTo(int pos) {
    if (!gTiles.list) return;
    const int viewH = tilesViewH();
    gTiles.scroll = std::clamp(pos, 0, std::max(0, gTiles.contentH - viewH));
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0, si.nMax = std::max(0, gTiles.contentH - 1), si.nPage = UINT(std::max(1, viewH)), si.nPos = gTiles.scroll;
    SetScrollInfo(gTiles.list, SB_VERT, &si, TRUE);
    InvalidateRect(gTiles.list, nullptr, FALSE);
}

// Where the k-th tile of the grid is, in content coordinates.
RECT tilesCellRect(int k) {
    const int pad = tilesPad(), cell = tilesCell(), gap = tilesGap();
    const int x = pad + (k % gTiles.perRow) * (cell + gap), y = pad + (k / gTiles.perRow) * (cell + gap);
    return {x, y, x + cell, y + cell};
}

// The tile value at a point of the list (client coordinates), or -1.
int tilesHit(int x, int y) {
    tilesFresh();
    if (!app.ed || y >= tilesViewH() || gTiles.values.empty()) return -1;
    const int pad = tilesPad(), step = tilesCell() + tilesGap(), cy = y + gTiles.scroll;
    if (x < pad || cy < pad) return -1;
    const int col = (x - pad) / step, row = (cy - pad) / step;
    if (col >= gTiles.perRow || (x - pad) % step >= tilesCell() || (cy - pad) % step >= tilesCell()) return -1;
    const size_t k = size_t(row) * size_t(gTiles.perRow) + size_t(col);
    return k < gTiles.values.size() ? gTiles.values[k] : -1;
}

// The box: the tiles whose cells it touches, added to the ones selected before it.
void tilesBoxSelect() {
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
    InvalidateRect(gTiles.list, nullptr, FALSE);
}

// The tile's picture: its megatile, as the map shows it in SD.
const std::vector<uint8_t>& tilesPixels(uint16_t value) {
    if (gTiles.pixels.size() > 4096 && !gTiles.pixels.count(value)) gTiles.pixels.clear();  // a long scroll: keep the memory small
    auto [it, fresh] = gTiles.pixels.try_emplace(value);
    if (fresh) {
        TerrainLayer one;
        one.width = one.height = 1;
        one.tiles = {value};
        const Image img = renderTerrain(app.ed->tileset(), one, 1);
        it->second.resize(32 * 32 * 4);
        for (size_t i = 0; i < 32 * 32 && i * 4 + 3 < img.rgba.size(); ++i) {
            it->second[i * 4] = img.rgba[i * 4 + 2], it->second[i * 4 + 1] = img.rgba[i * 4 + 1], it->second[i * 4 + 2] = img.rgba[i * 4];
            it->second[i * 4 + 3] = 255;
        }
    }
    return it->second;
}

void paintTilesList(HWND hwnd) {
    tilesFresh();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    const int viewH = tilesViewH();
    if (app.ed && app.ed->isOpen()) {
        HRGN clip = CreateRectRgn(0, 0, rc.right, viewH);
        SelectClipRgn(mem, clip);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), 32, -32, 1, 32, BI_RGB};
        SetStretchBltMode(mem, COLORONCOLOR);
        const int step = tilesCell() + tilesGap();
        const int firstRow = std::max(0, (gTiles.scroll - tilesPad()) / step), lastRow = (gTiles.scroll + viewH) / step + 1;
        for (int row = firstRow; row <= lastRow; ++row)
            for (int col = 0; col < gTiles.perRow; ++col) {
                const size_t k = size_t(row) * size_t(gTiles.perRow) + size_t(col);
                if (k >= gTiles.values.size()) break;
                const uint16_t v = gTiles.values[k];
                RECT c = tilesCellRect(int(k));
                OffsetRect(&c, 0, -gTiles.scroll);
                StretchDIBits(mem, c.left, c.top, c.right - c.left, c.bottom - c.top, 0, 0, 32, 32, tilesPixels(v).data(), &bi, DIB_RGB_COLORS,
                              SRCCOPY);
                if (v == gTiles.picked || (gTiles.chosen.size() > 1 && std::find(gTiles.chosen.begin(), gTiles.chosen.end(), v) != gTiles.chosen.end())) {
                    RECT o = c;
                    InflateRect(&o, scale(2), scale(2));
                    outline(mem, o, kAccent, scale(2));
                } else if (v == gTiles.hover) {
                    RECT o = c;
                    InflateRect(&o, 1, 1);
                    outline(mem, o, kBorderStrong, 1);
                } else if (tilesIsNull(v)) {  // a null tile: its frame, so it shows on the dark background
                    outline(mem, c, kBorder, 1);
                }
            }
        SelectClipRgn(mem, nullptr);
        DeleteObject(clip);
        if (gTiles.values.empty()) {
            RECT t{scale(8), scale(8), rc.right - scale(8), viewH};
            SetTextColor(mem, kDim);
            DrawTextW(mem, L"No tiles agree with the search and the filter", -1, &t, DT_WORDBREAK | DT_NOPREFIX);
        }
    }
    if (gTiles.boxing && gTiles.boxed) {  // the box being dragged
        RECT b{std::min(gTiles.boxFrom.x, gTiles.boxTo.x), std::min(gTiles.boxFrom.y, gTiles.boxTo.y) - gTiles.scroll,
               std::max(gTiles.boxFrom.x, gTiles.boxTo.x), std::max(gTiles.boxFrom.y, gTiles.boxTo.y) - gTiles.scroll};
        ui::box(mem, b, CLR_NONE, kAccent);
    }
    // The tile under the mouse (or the picked one).
    RECT f{0, viewH, rc.right, rc.bottom};
    FillRect(mem, &f, app.ctlBrush);
    const int show = gTiles.hover >= 0 ? gTiles.hover : gTiles.picked;
    SetTextColor(mem, show >= 0 ? kText : kDim);
    f.left += scale(6), f.right -= scale(4);
    const std::wstring name = !app.ed || !app.ed->isOpen() ? L"" : show >= 0 ? tilesValueLabel(show) : L"Click a tile to place it";
    DrawTextW(mem, name.c_str(), -1, &f, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void tilesPick(int value, bool fromMap) {
    gTiles.picked = value;
    gTiles.chosen.clear();
    if (value >= 0) gTiles.chosen.push_back(uint16_t(value));
    if (value >= 0)
        setMessage((fromMap ? L"Selected " : L"Placing ") + tilesValueLabel(value) + L": click or drag to place it (the brush's size and shape)");
    if (gTiles.list) InvalidateRect(gTiles.list, nullptr, FALSE);
    app.dirty = true;
}

// Scrolls the grid so the tile shows (clears the search and the filter if they hide it).
void tilesReveal(int value) {
    if (!gTiles.list || value < 0) return;
    auto find = [&] { return int(std::find(gTiles.values.begin(), gTiles.values.end(), uint16_t(value)) - gTiles.values.begin()); };
    int k = find();
    if (k >= int(gTiles.values.size())) {
        SetWindowTextW(gTiles.search, L"");  // EN_CHANGE rebuilds
        gTiles.filter = 0;
        if (gTiles.filterBox) SendMessageW(gTiles.filterBox, CB_SETCURSEL, 0, 0);
        tilesRebuild();
        k = find();
    }
    if (k >= int(gTiles.values.size())) return;
    const RECT c = tilesCellRect(k);
    if (c.top < gTiles.scroll || c.bottom > gTiles.scroll + tilesViewH()) tilesScrollTo(c.top - scale(8));
}

LRESULT CALLBACK tilesListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_FEEDBACK_NAME: {
        auto& q = *reinterpret_cast<FeedbackQuery*>(lp);
        const int v = tilesHit(q.pt.x, q.pt.y);
        q.name = v >= 0 ? tilesValueLabel(v) : L"the Tiles list";
        return 0;
    }
    case WM_PAINT: paintTilesList(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right != gTiles.layoutW) tilesRebuild();
        else tilesScrollTo(gTiles.scroll);
        return 0;
    }
    case WM_MOUSEWHEEL: tilesScrollTo(gTiles.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(90) / WHEEL_DELTA); return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof si, SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= scale(36); break;
        case SB_LINEDOWN: pos += scale(36); break;
        case SB_PAGEUP: pos -= int(si.nPage); break;
        case SB_PAGEDOWN: pos += int(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = gTiles.contentH; break;
        }
        tilesScrollTo(pos);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (gTiles.boxing && (wp & MK_LBUTTON)) {  // the box: the tiles in it, as it grows
            gTiles.boxTo = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp) + gTiles.scroll};
            if (std::abs(gTiles.boxTo.x - gTiles.boxFrom.x) + std::abs(gTiles.boxTo.y - gTiles.boxFrom.y) > scale(6)) gTiles.boxed = true;
            if (gTiles.boxed) tilesBoxSelect();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        const int v = tilesHit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (v != gTiles.hover) gTiles.hover = v, InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (gTiles.hover >= 0) gTiles.hover = -1, InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        const int v = tilesHit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (v >= 0 && (wp & (MK_CONTROL | MK_SHIFT)) && gTiles.picked >= 0) {  // several tiles, for Spray (#81)
            if (wp & MK_SHIFT) {  // the tiles from the last pick to this one, in the grid's order
                auto at = [](int value) { return int(std::find(gTiles.values.begin(), gTiles.values.end(), uint16_t(value)) - gTiles.values.begin()); };
                const int a = std::min(at(gTiles.picked), at(v)), b = std::max(at(gTiles.picked), at(v));
                if (!(wp & MK_CONTROL)) gTiles.chosen.clear();
                for (int k = a; k <= b && k < int(gTiles.values.size()); ++k)
                    if (std::find(gTiles.chosen.begin(), gTiles.chosen.end(), gTiles.values[size_t(k)]) == gTiles.chosen.end())
                        gTiles.chosen.push_back(gTiles.values[size_t(k)]);
            } else if (auto it = std::find(gTiles.chosen.begin(), gTiles.chosen.end(), uint16_t(v)); it != gTiles.chosen.end()) {
                if (gTiles.chosen.size() > 1) gTiles.chosen.erase(it);
            } else {
                gTiles.chosen.push_back(uint16_t(v));
            }
            gTiles.picked = gTiles.chosen.back();
            setMessage(L"Selected " + std::to_wstring(gTiles.chosen.size()) + L" tiles: Spray places one of them at random");
            InvalidateRect(gTiles.list, nullptr, FALSE);
            app.dirty = true;
        } else if (v >= 0) {
            tilesPick(v, false);
        }
        if (v >= 0 && !(wp & MK_SHIFT)) {  // a drag from here: a box (Ctrl: added to the tiles selected before)
            gTiles.boxing = true, gTiles.boxed = false;
            gTiles.boxFrom = gTiles.boxTo = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp) + gTiles.scroll};
            gTiles.boxBase = (wp & MK_CONTROL) ? gTiles.chosen : std::vector<uint16_t>{};
            SetCapture(hwnd);
        }
        SetFocus(app.view);
        return 0;
    }
    case WM_RBUTTONUP: {  // a tile: add it (or the selected tiles, when it is one of them) to the favorites, or take it out
        const int v = tilesHit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (v < 0 || !app.ed) break;
        std::vector<uint16_t> which{uint16_t(v)};
        if (gTiles.chosen.size() > 1 && std::find(gTiles.chosen.begin(), gTiles.chosen.end(), uint16_t(v)) != gTiles.chosen.end()) which = gTiles.chosen;
        auto& fav = tilesFavorites(app.ed->era());
        const bool in = std::find(fav.begin(), fav.end(), uint16_t(v)) != fav.end();
        HMENU m = CreatePopupMenu();
        const std::wstring n = which.size() > 1 ? L" (" + std::to_wstring(which.size()) + L" tiles)" : L"";
        AppendMenuW(m, MF_STRING, 1, ((in ? L"Remove from favorites" : L"Add to favorites") + n).c_str());
        AppendMenuW(m, MF_STRING, 2, L"Show the favorites");
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &pt);
        gMenuShown = true;
        const int pick = popupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, hwnd);
        DestroyMenu(m);
        if (pick == 1) {
            for (uint16_t t : which) {
                const auto it = std::find(fav.begin(), fav.end(), t);
                if (in && it != fav.end()) fav.erase(it);
                else if (!in && it == fav.end()) fav.push_back(t);
            }
            tilesSaveFavorites(app.ed->era());
            setMessage(std::wstring(in ? L"Removed from the favorites: " : L"Added to the favorites: ") + std::to_wstring(which.size()) +
                       (which.size() == 1 ? L" tile" : L" tiles") + L". The Favorites filter shows them.");
            if (gTiles.filter < int(gTiles.filters.size()) && gTiles.filters[size_t(gTiles.filter)].kind == 5) tilesRebuild();
        } else if (pick == 2) {
            for (size_t i = 0; i < gTiles.filters.size(); ++i)
                if (gTiles.filters[i].kind == 5) tilesSetFilter(int(i));
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (gTiles.boxing) {
            gTiles.boxing = false;
            ReleaseCapture();
            if (gTiles.boxed && gTiles.chosen.size() > 1)
                setMessage(L"Selected " + std::to_wstring(gTiles.chosen.size()) + L" tiles: Spray and Fill place one of them at random");
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The filter box's choice (clamped): the grid shows those tiles.
void tilesSetFilter(int index) {
    if (!gTiles.filterBox || gTiles.filters.empty()) return;
    gTiles.filter = std::clamp(index, 0, int(gTiles.filters.size()) - 1);
    SendMessageW(gTiles.filterBox, CB_SETCURSEL, WPARAM(gTiles.filter), 0);
    gTiles.scroll = 0;
    tilesRebuild();
}
// The mouse wheel over the closed filter box goes to the next or the previous filter (as the palette's section box).
LRESULT CALLBACK tilesFilterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEWHEEL && !SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0)) {
        tilesSetFilter(gTiles.filter + (GET_WHEEL_DELTA_WPARAM(wp) < 0 ? 1 : -1));
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// The panel: the filter box, the search box, then the grid.
void tilesPanelLayout() {
    if (!gTiles.wnd) return;
    RECT rc;
    GetClientRect(gTiles.wnd, &rc);
    const int rowH = scale(24), pad = scale(6);
    MoveWindow(gTiles.filterBox, 0, 0, rc.right, scale(400), TRUE);
    MoveWindow(gTiles.search, 0, rowH + pad, rc.right, rowH, TRUE);
    const int y = 2 * (rowH + pad);
    MoveWindow(gTiles.list, 0, y, rc.right, std::max(1, int(rc.bottom) - y), TRUE);
}
LRESULT CALLBACK tilesPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_FEEDBACK_NAME: reinterpret_cast<FeedbackQuery*>(lp)->name = L"the Tiles list"; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, app.bgBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: tilesPanelLayout(); return 0;
    case WM_COMMAND:
        if (HWND(lp) == gTiles.search && HIWORD(wp) == EN_CHANGE) gTiles.scroll = 0, tilesRebuild();
        if (HWND(lp) == gTiles.filterBox && HIWORD(wp) == CBN_SELCHANGE) {
            tilesSetFilter(int(SendMessageW(gTiles.filterBox, CB_GETCURSEL, 0, 0)));
            SetFocus(app.view);
        }
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(HDC(wp), kText);
        SetBkColor(HDC(wp), kCtl);
        return LRESULT(app.ctlBrush);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void tilesCreate() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW c{};
        c.hInstance = app.inst, c.hCursor = LoadCursor(nullptr, IDC_ARROW);
        c.lpfnWndProc = tilesPanelProc, c.lpszClassName = L"StarDraftTilesPanel";
        RegisterClassW(&c);
        c.lpfnWndProc = tilesListProc, c.lpszClassName = L"StarDraftTilesList";
        RegisterClassW(&c);
        registered = true;
    }
    gTiles.wnd = CreateWindowExW(0, L"StarDraftTilesPanel", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, nullptr);
    gTiles.filterBox = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 1, 1, gTiles.wnd,
                                       nullptr, app.inst, nullptr);
    SendMessageW(gTiles.filterBox, WM_SETFONT, WPARAM(app.font), TRUE);
    SetWindowSubclass(gTiles.filterBox, tilesFilterProc, 1, 0);
    darkControl(gTiles.filterBox, L"DarkMode_CFD");
    gTiles.search = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 1, 1, gTiles.wnd, nullptr,
                                    app.inst, nullptr);
    SendMessageW(gTiles.search, WM_SETFONT, WPARAM(app.font), TRUE);
    SendMessageW(gTiles.search, EM_SETCUEBANNER, TRUE, LPARAM(L"Search tiles: group number, terrain, doodad")); addSearchClear(gTiles.search);  // an x clears it
    darkControl(gTiles.search, L"DarkMode_CFD");
    gTiles.list = CreateWindowExW(0, L"StarDraftTilesList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 1, 1, gTiles.wnd, nullptr, app.inst, nullptr);
    darkControl(gTiles.list, L"DarkMode_Explorer");
}

// Shows the Tiles panel in the place of the palette while the Tiles tool is on (and the palette again after).
void tilesSync() {
    const bool on = tilesOn();
    if (on && !gTiles.wnd) tilesCreate();
    if (!gTiles.wnd) return;
    if (on) {
        // The palette's place: from its search box down to its end.
        RECT a{}, b{};
        GetWindowRect(gPal.search, &a);
        GetWindowRect(gPal.wnd, &b);
        MapWindowPoints(nullptr, app.main, reinterpret_cast<POINT*>(&a), 2);
        MapWindowPoints(nullptr, app.main, reinterpret_cast<POINT*>(&b), 2);
        for (HWND h : {gPal.search, gPal.viewBtn, gPal.wnd})
            if (h) ShowWindow(h, SW_HIDE);
        MoveWindow(gTiles.wnd, a.left, a.top, std::max(1, int(b.right - a.left)), std::max(1, int(b.bottom - a.top)), TRUE);
        ShowWindow(gTiles.wnd, SW_SHOW);
        tilesFresh();
        InvalidateRect(gTiles.wnd, nullptr, FALSE);
        InvalidateRect(gTiles.list, nullptr, FALSE);
    } else if (gTiles.shown) {
        ShowWindow(gTiles.wnd, SW_HIDE);
        const bool show = gWorkspace == Workspace::Map && app.mode != Mode::Locations;
        for (HWND h : {gPal.search, gPal.viewBtn, gPal.wnd})
            if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    }
    gTiles.shown = on;
}

// --- On the map ---

void tilesAt(float wx, float wy, int& tx, int& ty) {
    tx = std::clamp(int(std::floor(wx / 32)), 0, std::max(0, int(app.ed->width()) - 1));
    ty = std::clamp(int(std::floor(wy / 32)), 0, std::max(0, int(app.ed->height()) - 1));
}

// The tiles a brush of the tool strip's size and shape covers around tile (cx, cy): size 0.5 and 1 one tile, then a
// "radius" of size - 1 tiles; diamond, square or circle.
std::vector<std::pair<int, int>> tilesFootprint(int cx, int cy) {
    std::vector<std::pair<int, int>> out;
    const int k = std::max(0, int(std::lround(app.brushSize)) - 1);
    for (int dy = -k; dy <= k; ++dy)
        for (int dx = -k; dx <= k; ++dx) {
            const bool in = app.brushShape == Brush::Square ? true
                          : app.brushShape == Brush::Circle ? dx * dx + dy * dy <= k * k + k
                                                            : std::abs(dx) + std::abs(dy) <= k;
            if (in) out.push_back({cx + dx, cy + dy});
        }
    return out;
}
int gTilesStrokePlaced = 0;

// The message after tiles went down: how many, and the doodads and units that went with them.
void tilesPlacedMessage(int n, const wchar_t* verb) {
    Editor& ed = *app.ed;
    const int doodads = ed.takeDoodadsRemovedByTerrain(), units = ed.takeUnitsRemovedByTerrain();
    std::wstring m = std::wstring(verb) + (n == 1 ? L" 1 tile" : L" " + std::to_wstring(n) + L" tiles");
    if (!n) m = L"The tiles are the same already";
    if (doodads) m += L". Removed " + std::to_wstring(doodads) + (doodads == 1 ? L" doodad under them" : L" doodads under them");
    if (units) m += L". Removed " + std::to_wstring(units) + (units == 1 ? L" unit that cannot stand there" : L" units that cannot stand there");
    if (doodads || units) m += L".";
    setMessage(m);
}

// One spray in Rectangle mode: the picked tile on random tiles of the brush's footprint (as many as the density's
// dabs). Part of the spray's stroke.
void tilesSprayAt(float wx, float wy) {
    if (!app.ed || gTiles.picked < 0) return;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    const auto foot = tilesFootprint(tx, ty);
    std::mt19937 rng(app.spraySeed++);
    // Each dab: one of the selected tiles, at random (Ctrl+click or Shift+click in the grid; one tile: that tile).
    std::vector<Editor::TileAt> cells;
    for (int i = 0; i < sprayDabs(); ++i) {
        const uint16_t v = gTiles.chosen.size() > 1 ? gTiles.chosen[rng() % gTiles.chosen.size()] : uint16_t(gTiles.picked);
        const auto [x, y] = foot[rng() % foot.size()];
        cells.push_back({x, y, v});
    }
    gTilesStrokePlaced += app.ed->placeTileValues(cells);
}

// The picked tile in `cells` (one undo step); with several tiles picked (Shift+click in the list), each cell gets one of
// them at random.
int tilesPlaceRandom(const std::vector<std::pair<int, int>>& cells) {
    Editor& ed = *app.ed;
    if (gTiles.chosen.size() < 2) return ed.placeTileCells(cells, uint16_t(gTiles.picked));
    std::mt19937 rng(app.spraySeed++);
    std::vector<Editor::TileAt> values;
    for (const auto& [x, y] : cells) values.push_back({x, y, gTiles.chosen[rng() % gTiles.chosen.size()]});
    ed.beginStroke();
    const int n = ed.placeTileValues(values);
    ed.endStroke();
    return n;
}

// Fill in Rectangle mode: the connected tiles (4 ways) of the value under the cursor; with Shift (or Replace) every
// tile of that value, in the selection if there is one. One undo step.
void tilesFill(int tx, int ty, bool every) {
    Editor& ed = *app.ed;
    const int w = ed.width(), h = ed.height();
    const std::vector<uint16_t>& t = ed.terrain().tiles;
    const uint16_t from = t[size_t(ty) * w + tx];
    std::vector<std::pair<int, int>> cells;
    if (every) {
        int x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
        if (app.hasTerrainSel)
            x0 = std::max(0, app.terrainSel.left / 32), y0 = std::max(0, app.terrainSel.top / 32),
            x1 = std::min(w - 1, (app.terrainSel.right - 1) / 32), y1 = std::min(h - 1, (app.terrainSel.bottom - 1) / 32);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (t[size_t(y) * w + x] == from && (!app.hasTerrainSel || Editor::inSelectShape(app.terrainSel, selectShape(), x * 32 + 16, y * 32 + 16)))
                    cells.push_back({x, y});  // (in the selection's shape)
    } else {
        std::vector<uint8_t> seen(size_t(w) * h, 0);
        std::vector<std::pair<int, int>> todo{{tx, ty}};
        seen[size_t(ty) * w + tx] = 1;
        while (!todo.empty()) {
            const auto [x, y] = todo.back();
            todo.pop_back();
            cells.push_back({x, y});
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                const size_t i = size_t(ny[k]) * w + nx[k];
                if (!seen[i] && t[i] == from) seen[i] = 1, todo.push_back({nx[k], ny[k]});
            }
        }
    }
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
    tilesPlacedMessage(tilesPlaceRandom(cells), every ? L"Replaced" : L"Filled");
}

// Fill selection (Rectangle mode; GitHub feedback #114): every tile in the selection (in its shape), one undo step.
void tilesFillSelection() {
    if (!app.ed || !app.hasTerrainSel || gTiles.picked < 0) return;
    Editor& ed = *app.ed;
    const int w = ed.width(), h = ed.height();
    std::vector<std::pair<int, int>> cells;
    for (int y = std::max(0, app.terrainSel.top / 32); y <= std::min(h - 1, (app.terrainSel.bottom - 1) / 32); ++y)
        for (int x = std::max(0, app.terrainSel.left / 32); x <= std::min(w - 1, (app.terrainSel.right - 1) / 32); ++x)
            if (Editor::inSelectShape(app.terrainSel, selectShape(), x * 32 + 16, y * 32 + 16)) cells.push_back({x, y});
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
    tilesPlacedMessage(tilesPlaceRandom(cells), L"Filled the selection:");
}

// A click on the map in Rectangle mode. Select: a rectangle of tiles, or a paste of the copied tiles. Ctrl: pick the
// tile under the cursor. Brush: the picked tile in the brush's footprint, and on along the drag (one undo step for the
// stroke). Spray: random tiles of the footprint, again while the button is held. Fill: the connected tiles.
bool tilesMouseDown(float wx, float wy, WPARAM keys) {
    if (!app.ed || !app.ed->isOpen()) return false;
    tilesFresh();
    Editor& ed = *app.ed;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    const int tool = currentTool();
    if (tool == TSelect) {
        if (app.terrainPasting && ed.hasTileClipboard()) {
            ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
            tilesPlacedMessage(ed.pasteTiles(tx, ty), L"Pasted");
            // The copy stays on the mouse, as a brush: click again to paste again; right-click or Esc stops (GitHub feedback #123).
            app.drag = Drag::None;
        } else {
            app.drag = Drag::TerrainSelect;
        }
        return true;
    }
    if (keys & MK_CONTROL) {
        const uint16_t v = ed.terrain().tiles[size_t(ty) * ed.width() + tx];
        tilesPick(v, true);
        tilesReveal(v);
        app.drag = Drag::None;
        return true;
    }
    if (gTiles.picked < 0) {
        setMessage(L"Select a tile in the Tiles list first (or Ctrl+click a tile on the map)");
        app.drag = Drag::None;
        return true;
    }
    if (tool == TFill && !(keys & MK_SHIFT) && app.hasTerrainSel &&
        Editor::inSelectShape(app.terrainSel, selectShape(), int(wx), int(wy))) {  // in the selection: all of it
        tilesFillSelection();
        app.drag = Drag::None;
        return true;
    }
    if (tool == TFill || tool == TReplace) {
        tilesFill(tx, ty, tool == TReplace || (keys & MK_SHIFT));
        app.drag = Drag::None;
        return true;
    }
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
    ed.beginStroke();
    gTilesStrokePlaced = 0;
    if (tool == TSpray) {
        tilesSprayAt(wx, wy);
        app.drag = Drag::Spray;
        return true;
    }
    gTilesStrokePlaced = ed.placeTileCells(tilesFootprint(tx, ty), uint16_t(gTiles.picked));
    app.drag = Drag::Tiles;
    return true;
}

// The drag: the footprint at the mouse.
void tilesMouseMove(float wx, float wy) {
    if (!app.ed || gTiles.picked < 0) return;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    gTilesStrokePlaced += app.ed->placeTileCells(tilesFootprint(tx, ty), uint16_t(gTiles.picked));
}

// The end of a Brush or Spray stroke in Rectangle mode.
void tilesMouseUp(float, float) {
    if (!app.ed) return;
    app.ed->endStroke();
    tilesPlacedMessage(gTilesStrokePlaced, L"Placed");
}

// The preview: the picked tile in the brush's footprint at the mouse, with its outline; a paste: the copied tiles.
void tilesScene(render::Scene& s, float wx, float wy) {
    if (!app.ed || !app.ed->isOpen()) return;
    tilesFresh();
    Editor& ed = *app.ed;
    int cx, cy;
    tilesAt(wx, wy, cx, cy);
    std::vector<std::pair<int, uint16_t>> cells;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    auto add = [&](int x, int y, uint16_t v) {
        if (x < 0 || y < 0 || x >= ed.width() || y >= ed.height()) return;
        cells.push_back({y * ed.width() + x, v});
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    };
    const int tool = currentTool();
    if (tool == TSelect) {
        if (!app.terrainPasting || !ed.hasTileClipboard()) return;
        const int cw = ed.tileClipboardWidth();
        for (int y = 0; y < ed.tileClipboardHeight(); ++y)
            for (int x = 0; x < cw; ++x) add(cx + x, cy + y, ed.tileClipboard()[size_t(y) * cw + x]);
    } else if (tool == TBrush || tool == TSpray) {
        if (gTiles.picked < 0 || !ed.tileset().groupFor(uint16_t(gTiles.picked))) return;
        for (auto [x, y] : tilesFootprint(cx, cy))
            if (tool == TBrush) add(x, y, uint16_t(gTiles.picked));
            else x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);  // the spray: its area only
    } else {
        return;
    }
    ghostTiles = std::move(cells);
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, ed.width() - 1), y1 = std::min(y1, ed.height() - 1);
    if (x0 <= x1) outline(s, Rect{x0 * 32, y0 * 32, (x1 + 1) * 32, (y1 + 1) * 32}, 0x40FF40FFu, 1.5f);
}

// --- Datashots ---

void pump();
bool capture(const fs::path& file, HWND wnd);

// tiles:[<search>]: the Map workspace in Terrain mode, Rectangle, the search set, the first tile with a picture picked:
// tiles.png and tiles_panel.png.
// tilesplace:[<tile value>]: places the picked tile (or that value) on one tile and as a 6 x 4 rectangle at the middle
// of the view, then the same rectangle again with a brush beside it: tiles_placed.png, tiles_brushed.png.
bool tilesDatashot(const std::string& spec, const fs::path& dir) {
    if (spec.rfind("tiles:", 0) == 0) {
        setWorkspace(Workspace::Map);
        setMode(Mode::Terrain);
        app.terrainRect = true;
        app.toolByMode[int(Mode::Terrain)] = TBrush;
        fillToolCombo();
        std::string search = spec.substr(6);
        SetWindowTextW(gTiles.search, widen(search).c_str());
        gTiles.scroll = 0;
        tilesRebuild();
        // The first tile that has a picture.
        for (uint16_t v : gTiles.values)
            if (!tilesIsNull(v)) {
                gTiles.picked = v;
                break;
            }
        size_t nulls = 0;
        for (uint16_t v : gTiles.values) nulls += tilesIsNull(v);
        InvalidateRect(gTiles.wnd, nullptr, FALSE);
        for (int i = 0; i < 8; ++i) pump();
        capture(dir / "tiles.png", nullptr);
        capture(dir / "tiles_panel.png", gTiles.wnd);
        std::ofstream log(dir / "datashots.txt", std::ios::app);
        log << "tiles: " << (app.ed ? tilesetName(app.ed->era()) : "-") << ", " << gTiles.values.size() << " tiles shown ("
            << nulls << " null tiles) of " << (app.ed ? app.ed->tileset().groups.size() * 16 : 0) << ", " << gTiles.perRow
            << " in a row, picked " << gTiles.picked << "\n";
        return true;
    }
    if (spec.rfind("tilesplace:", 0) == 0) {
        if (!app.ed || !app.ed->isOpen()) return true;
        if (spec.size() > 11) gTiles.picked = std::atoi(spec.c_str() + 11);
        if (gTiles.picked < 0) return true;
        Editor& ed = *app.ed;
        // The middle of the map, at 100%.
        const int cx = ed.width() / 2, cy = ed.height() / 2;
        int vw, vh;
        viewSize(vw, vh);
        app.zoom = 1;
        app.camX = cx * 32.0f - vw / 2.0f, app.camY = cy * 32.0f - vh / 2.0f;
        const int n1 = ed.placeTiles(cx + 2, cy - 4, cx + 2, cy - 4, uint16_t(gTiles.picked));
        const int n2 = ed.placeTiles(cx - 3, cy - 2, cx + 2, cy + 1, uint16_t(gTiles.picked));
        app.dirty = true;
        for (int i = 0; i < 8; ++i) pump();
        capture(dir / "tiles_placed.png", nullptr);
        // A brush beside the rectangle: the tiles whose rects it does not change stay.
        int brushed = 0;
        if (ed.canPaintTerrain()) {
            const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era());
            const uint16_t at = ed.terrainAt((cx - 6) * 32, cy * 32);
            uint16_t other = 1;
            while (t && other < 64 && (!t->isTerrain(other) || other == at || !t->linked(at, other))) ++other;
            if (other < 64) ed.paintTerrain((cx - 6) * 32, cy * 32, other, 1);
            for (int y = cy - 2; y <= cy + 1; ++y)
                for (int x = cx - 3; x <= cx + 2; ++x) brushed += ed.terrain().tiles[size_t(y) * ed.width() + x] == gTiles.picked;
            app.dirty = true;
            for (int i = 0; i < 8; ++i) pump();
            capture(dir / "tiles_brushed.png", nullptr);
        }
        std::ofstream log(dir / "datashots.txt", std::ios::app);
        log << "tilesplace: tile " << gTiles.picked << " at " << cx << "," << cy << ": " << n1 << " + " << n2 << " tiles placed, " << brushed
            << " of 24 still there after the brush, invalid ISOM diamonds " << ed.invalidTerrain() << "\n";
        return true;
    }
    return false;
}
