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

int tilesFooterH() { return scale(26); }
int tilesCell() { return scale(32); }
int tilesGap() { return scale(3); }
int tilesPad() { return scale(4); }

// The panel's side of the tile logic (tiles_edit.inc calls these).
void tilesPanelRepaint() {
    if (gTiles.list) InvalidateRect(gTiles.list, nullptr, FALSE);
}
void tilesPanelClearSearch() { SetWindowTextW(gTiles.search, L""); }
void tilesPanelShowAllFilter() {
    if (gTiles.filterBox) SendMessageW(gTiles.filterBox, CB_SETCURSEL, 0, 0);
}

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
        std::vector<uint8_t> b;
        if (prefs::bytes(name, b))  // tile values, 2 bytes each (low byte first)
            for (size_t i = 0; i + 1 < b.size(); i += 2) fav[era].push_back(uint16_t(b[i] | b[i + 1] << 8));
    }
    return fav[era];
}
void tilesSetFilter(int index);
void tilesSaveFavorites(int era) {
    if (gSelfTest) return;  // tests never change the user's settings
    const auto& f = tilesFavorites(era);
    const std::wstring name = L"TileFavorites" + std::to_wstring(era & 7);
    std::vector<uint8_t> b;
    for (uint16_t v : f) b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8));
    prefs::setBytes(name, b);
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

// The tile's picture: its megatile in the art the map shows: SD, HD (HD2 resolution is plenty here) or Carbot
// (feedback #52: the grid was SD only).
const TilesView::Pixels& tilesPixels(uint16_t value) {
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
        it->second.size = size;
        it->second.bgra.assign(size_t(size) * size * 4, 0);
        for (size_t i = 0; i < size_t(size) * size && i * 4 + 3 < pic.rgba.size(); ++i) {
            it->second.bgra[i * 4] = pic.rgba[i * 4 + 2], it->second.bgra[i * 4 + 1] = pic.rgba[i * 4 + 1], it->second.bgra[i * 4 + 2] = pic.rgba[i * 4];
            it->second.bgra[i * 4 + 3] = 255;
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
                const TilesView::Pixels& px = tilesPixels(v);
                bi.bmiHeader.biWidth = px.size, bi.bmiHeader.biHeight = -px.size;
                StretchDIBits(mem, c.left, c.top, c.right - c.left, c.bottom - c.top, 0, 0, px.size, px.size, px.bgra.data(), &bi, DIB_RGB_COLORS,
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
            DrawTextW(mem, tr(L"None"), -1, &t, DT_SINGLELINE | DT_NOPREFIX);
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
            setMessage(L"Selected " + std::to_wstring(gTiles.chosen.size()) + L" tiles: Brush, Spray and Fill place one of them at random");
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
                setMessage(L"Selected " + std::to_wstring(gTiles.chosen.size()) + L" tiles: Brush, Spray and Fill place one of them at random");
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
