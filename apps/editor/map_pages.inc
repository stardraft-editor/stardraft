// --- Map pages ---------------------------------------------------------------------------------------------------
// The map's own settings as pages in the main area, opened from the rail (under the workspaces) like the workspaces:
// Properties (map_properties.inc), Players (the players page in main.cpp), Classic triggers (classic_triggers.inc)
// and Map data (the classic settings: classic_settings.inc; classic maps only). Included into main.cpp after classic_triggers.inc.
//
// Map properties (with Players and forces under it on the same page) set each change in the map at once: a list or a
// check box at once, a typed text half a second after the last key (each an undo step). Only the size and the tileset
// wait for their own button. The other pages edit a working copy of the map's values. The bar at the bottom has Apply
// (one undo step) and Revert (the map's values again). The copy stays with its tab: leave the page (another workspace, another tab) with changes that
// are not applied and they are there when you come back, with a dot on the rail item. Closing the tab (or StarDraft)
// asks to apply them. A copy without changes reads the map again when the map changed (undo, another edit).
//
// The page: a header (the title; the Units, Upgrades and Tech tabs of Classic settings), a view that scrolls (the
// content: the native controls of the old window, made children of the view), the Apply and Revert bar.

// --- Players and forces (on the Map properties page): painted with the kit; the force names are edit boxes ---

ui::Surface gPlayersSurface;  // the pickers' and check boxes' click areas

// The page's columns and rows (the same at its making and at each paint).
struct PlayersLayout {
    int pad, rowH, pitch, c0, c1, c2, c3, c4, c5, flagX, right, tableTop, forcesTop, problemsTop, height;
    int flagW[4];
};
PlayersLayout playersLayout(HDC dc) {
    PlayersLayout l{};
    l.pad = scale(ui::kDialogPad), l.rowH = scale(ui::kFieldH), l.pitch = l.rowH + scale(8);
    l.c0 = l.pad, l.c1 = l.pad + scale(80), l.c2 = l.c1 + scale(150), l.c3 = l.c2 + scale(130), l.c4 = l.c3 + scale(170), l.c5 = l.c4 + scale(230);
    l.flagX = l.c1 + scale(120);
    int end = l.flagX;
    for (int b = 0; b < 4; ++b) l.flagW[b] = ui::textWidth(dc, kForceFlagNames[b]) + scale(40), end += l.flagW[b];
    l.right = std::max(l.c5, end);
    l.tableTop = l.pad + l.pitch;                 // under the "Players" heading
    l.forcesTop = l.tableTop + l.pitch * 9 + l.pad;  // the column names and 8 players
    l.problemsTop = l.forcesTop + l.pitch + 4 * l.pitch + l.pad;
    l.height = l.problemsTop + scale(70) + l.pad;
    return l;
}

void playersPaint(HWND hwnd, PlayersDialog& d) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    gPlayersSurface.begin(hwnd);
    const PlayersLayout l = playersLayout(mem);
    auto text = [&](const std::wstring& t, int x, int y, int w, COLORREF c) {
        SetTextColor(mem, c);
        RECT r{x, y, x + w, y + l.rowH};
        DrawTextW(mem, t.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    };
    text(L"Players", l.c0, l.pad, l.right - l.c0, kText);  // a section heading, as the map's fields have
    fillAlpha(mem, {l.c0, l.pad + l.rowH, l.right, l.pad + l.rowH + 1}, kBorder, 255);
    int y = l.tableTop;
    text(L"Player", l.c0, y, l.c1 - l.c0, kDim), text(L"Controller", l.c1, y, l.c2 - l.c1, kDim), text(L"Race", l.c2, y, l.c3 - l.c2, kDim);
    text(L"Force", l.c3, y, l.c4 - l.c3, kDim), text(L"Color", l.c4, y, l.c5 - l.c4, kDim);
    y += l.pitch;
    const int gap = scale(8);
    for (int p = 0; p < 8; ++p) {
        Editor::Player& pl = d.players[size_t(p)];
        text(L"Player " + std::to_wstring(p + 1), l.c0, y, l.c1 - l.c0, kText);
        std::wstring ctrl, race;
        for (const auto& [v, n] : kControllers)
            if (v == pl.controller) ctrl = n;
        for (const auto& [v, n] : kRaces)
            if (v == pl.race) race = n;
        ui::pickBox(mem, {l.c1, y, l.c2 - gap, y + l.rowH}, ctrl, false, [p](RECT b) {
            PlayersDialog& dd = *gPlayersDlg;
            std::vector<ListPickItem> items;
            for (size_t i = 0; i < std::size(kControllers); ++i) items.push_back({kControllers[i].second, L"", std::to_string(i)});
            std::string cur, out;
            for (size_t i = 0; i < std::size(kControllers); ++i)
                if (kControllers[i].first == dd.players[size_t(p)].controller) cur = std::to_string(i);
            if (showListPicker(L"Controller", items, cur, belowBox(b), out)) dd.players[size_t(p)].controller = kControllers[std::stoi(out)].first, playersSet(dd);
        });
        ui::pickBox(mem, {l.c2, y, l.c3 - gap, y + l.rowH}, race, false, [p](RECT b) {
            PlayersDialog& dd = *gPlayersDlg;
            std::vector<ListPickItem> items;
            for (size_t i = 0; i < std::size(kRaces); ++i) items.push_back({kRaces[i].second, L"", std::to_string(i)});
            std::string cur, out;
            for (size_t i = 0; i < std::size(kRaces); ++i)
                if (kRaces[i].first == dd.players[size_t(p)].race) cur = std::to_string(i);
            if (showListPicker(L"Race", items, cur, belowBox(b), out)) dd.players[size_t(p)].race = kRaces[std::stoi(out)].first, playersSet(dd);
        });
        auto forceName = [&d](int f) {
            const std::string& n = d.forces[size_t(f)].name;
            return n.empty() ? L"Force " + std::to_wstring(f + 1) : widen(n);
        };
        ui::pickBox(mem, {l.c3, y, l.c4 - gap, y + l.rowH}, forceName(std::clamp(int(pl.force), 0, 3)), false, [p, forceName](RECT b) {
            PlayersDialog& dd = *gPlayersDlg;
            std::vector<ListPickItem> items;
            for (int f = 0; f < 4; ++f) items.push_back({forceName(f), L"", std::to_string(f)});
            std::string out;
            if (showListPicker(L"Force", items, std::to_string(dd.players[size_t(p)].force), belowBox(b), out))
                dd.players[size_t(p)].force = uint8_t(std::stoi(out)), playersSet(dd);
        });
        // The color: a swatch in the box, then its name (Custom: the color picker; multiplayer only).
        const RECT cb{l.c4, y, l.c5 - gap, y + l.rowH};
        ui::pickBox(mem, cb, L"       " + playerColorName(pl.color), false, [p](RECT b) {
            PlayersDialog& dd = *gPlayersDlg;
            std::vector<ListPickItem> items;
            for (int i = 0; i < Editor::kColorIds; ++i) items.push_back({widen(Editor::colorIdName(i)), L"", std::to_string(i)});
            items.push_back({L"Random", L"", std::to_string(Editor::kColorIds)});
            items.push_back({L"Player's choice", L"", std::to_string(Editor::kColorIds + 1)});
            items.push_back({L"Custom color\x2026 (multiplayer only)", L"", std::to_string(Editor::kColorIds + 2)});
            Editor::PlayerColor& c = dd.players[size_t(p)].color;
            std::string out;
            if (!showListPicker(L"Color", items, std::to_string(playerColorRow(c)), belowBox(b), out)) return;
            const int row = std::stoi(out);
            if (row < Editor::kColorIds) c.kind = Editor::PlayerColor::Id, c.id = uint8_t(row);
            else if (row == Editor::kColorIds) c.kind = Editor::PlayerColor::Random;
            else if (row == Editor::kColorIds + 1) c.kind = Editor::PlayerColor::Choice;
            else {  // the color picker (a test run keeps the color it has)
                static COLORREF custom[16] = {};
                const uint32_t start = c.kind == Editor::PlayerColor::Custom ? c.rgb : app.ed->playerRgb(p);
                CHOOSECOLORW cc{sizeof cc};
                cc.hwndOwner = app.main, cc.lpCustColors = custom, cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                cc.rgbResult = RGB((start >> 16) & 255, (start >> 8) & 255, start & 255);
                if (gSelfTest || gBackground || !ChooseColorW(&cc)) return;
                c.kind = Editor::PlayerColor::Custom;
                c.rgb = uint32_t(GetRValue(cc.rgbResult)) << 16 | uint32_t(GetGValue(cc.rgbResult)) << 8 | GetBValue(cc.rgbResult);
            }
            playersSet(dd);
        });
        if (const int64_t rgb = playerColorRgb(pl.color); rgb >= 0) {
            const int s = scale(12), sx = cb.left + scale(10), sy = (cb.top + cb.bottom - s) / 2;
            fillAlpha(mem, {sx, sy, sx + s, sy + s}, RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255), 255);
        }
        y += l.pitch;
    }
    // Forces: the name (an edit box, made with the page), then the four flags.
    y = l.forcesTop;
    text(L"Forces", l.c0, y, l.right - l.c0, kText);
    fillAlpha(mem, {l.c0, y + l.rowH, l.right, y + l.rowH + 1}, kBorder, 255);
    y += l.pitch;
    for (int f = 0; f < 4; ++f) {
        int fx = l.flagX;
        for (int b = 0; b < 4; ++b) {
            const bool on = (d.forces[size_t(f)].flags >> b) & 1;
            ui::check(mem, {fx, y, fx + l.flagW[b] - gap, y + l.rowH}, kForceFlagNames[b], on, [f, b] {
                PlayersDialog& dd = *gPlayersDlg;
                dd.forces[size_t(f)].flags ^= uint8_t(1 << b);
                playersSet(dd);
            });
            fx += l.flagW[b];
        }
        y += l.pitch;
    }
    // The problems, or that StarCraft can start the map: each line with its icon.
    y = l.problemsTop;
    for (size_t a = 0; a < d.problemsText.size();) {
        size_t e = d.problemsText.find(L"\r\n", a);
        if (e == std::wstring::npos) e = d.problemsText.size();
        std::wstring line = d.problemsText.substr(a, e - a);
        a = e + 2;
        if (line.empty()) continue;
        const bool good = line[0] == L'\x2713';
        line = line.substr(std::min(line.size(), size_t(3)));
        const COLORREF c = good ? kGood : kWarn;
        uiIcon(mem, {l.c0, y, l.c0 + scale(16), y + scale(22)}, good ? UiIcon::Check : UiIcon::Warning, c, scale(16));
        SetTextColor(mem, c);
        RECT t{l.c0 + scale(24), y, l.right, y + scale(22)};
        DrawTextW(mem, line.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += scale(22);
    }
    gPlayersSurface.end(hwnd);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK playersDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PlayersDialog* d = gPlayersDlg;
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);
    LRESULT res = 0;
    if (msg != WM_PAINT && gPlayersSurface.message(hwnd, msg, wp, lp, res)) return res;
    switch (msg) {
    case WM_CREATE: {
        d->wnd = hwnd;
        HDC dc = GetDC(hwnd);
        HGDIOBJ of = SelectObject(dc, app.font);
        const PlayersLayout l = playersLayout(dc);
        SelectObject(dc, of);
        ReleaseDC(hwnd, dc);
        int y = l.forcesTop + l.pitch;
        d->filling = true;
        for (int f = 0; f < 4; ++f) {  // the only native controls: the four names
            d->forceName[f] = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, widen(d->forces[size_t(f)].name).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, l.c0, y, l.c1 + scale(110) - l.c0, l.rowH, hwnd,
                                              HMENU(INT_PTR(1700 + f)), app.inst, nullptr);
            SendMessageW(d->forceName[f], WM_SETFONT, WPARAM(app.font), TRUE);
            darkControl(d->forceName[f], L"DarkMode_CFD");
            y += l.pitch;
        }
        d->filling = false;
        playersRefresh(*d);
        SetWindowPos(hwnd, nullptr, 0, 0, l.right + l.pad, l.height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);  // as big as its content
        if (!d->baseSet) d->baseSet = true, d->basePlayers = d->players, d->baseForces = d->forces;
        return 0;
    }
    case WM_PAINT: playersPaint(hwnd, *d); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= 1700 && LOWORD(wp) < 1704 && !d->filling) playersSet(*d);
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(HDC(wp), kText);
        SetBkColor(HDC(wp), kCtl);
        return LRESULT(app.ctlBrush);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


// --- Map properties' fields (on the Map properties page): painted with the kit; the title and description are edit boxes

ui::Surface gPropsSurface;  // the dropdowns' and buttons' click areas

// The page's rows (the same at its making and at each paint).
struct PropsLayout {
    int pad, rowH, pitch, labelW, boxW, x1, nameY, descY, descH, terrainY, tilesetY, sizeY, anchorY, fillY, infoY, infoH, buttonsY, height;
};
PropsLayout propsLayout() {
    PropsLayout l{};
    l.pad = scale(ui::kDialogPad), l.rowH = scale(ui::kFieldH), l.pitch = l.rowH + scale(8), l.labelW = scale(120), l.boxW = scale(440);
    l.x1 = l.pad + l.labelW;
    l.nameY = l.pad + l.pitch;  // under the "Map" heading
    l.descY = l.nameY + l.pitch, l.descH = scale(84);
    l.terrainY = l.descY + l.descH + l.pad;
    l.tilesetY = l.terrainY + l.pitch;
    l.sizeY = l.tilesetY + l.pitch, l.anchorY = l.sizeY + l.pitch, l.fillY = l.anchorY + l.pitch;
    // The buttons under the fields; what they do under them (the text shows when the size or the tileset differs).
    l.buttonsY = l.fillY + l.pitch + scale(4);
    l.infoY = l.buttonsY + scale(ui::kButtonH) + scale(8), l.infoH = scale(20);
    if (gMapProps && !gMapProps->infoText.empty()) {  // as tall as the text
        HDC dc = GetDC(nullptr);
        HGDIOBJ of = SelectObject(dc, app.font);
        RECT r{0, 0, l.boxW, 0};
        DrawTextW(dc, gMapProps->infoText.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        SelectObject(dc, of);
        ReleaseDC(nullptr, dc);
        l.infoH = std::max<int>(l.infoH, r.bottom + scale(4));
    }
    l.height = l.infoY + l.infoH + l.pad;
    return l;
}

void propsFitHeight(HWND hwnd);
void propsPaint(HWND hwnd, MapPropsDialog& d) {
    propsFitHeight(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    gPropsSurface.begin(hwnd);
    const PropsLayout l = propsLayout();
    const int right = l.x1 + l.boxW;
    auto heading = [&](const wchar_t* t, int y) {
        SetTextColor(mem, kText);
        RECT r{l.pad, y, right, y + l.rowH};
        DrawTextW(mem, t, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        fillAlpha(mem, {l.pad, y + l.rowH, right, y + l.rowH + 1}, kBorder, 255);
    };
    auto label = [&](const wchar_t* t, int y) {
        SetTextColor(mem, kDim);
        RECT r{l.pad, y, l.x1 - scale(8), y + l.rowH};
        DrawTextW(mem, t, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    };
    heading(L"Map", l.pad);
    label(L"Title", l.nameY), label(L"Description", l.descY);
    heading(L"Terrain", l.terrainY);
    label(L"Tileset", l.tilesetY), label(L"Size (tiles)", l.sizeY), label(L"Map content at", l.anchorY), label(L"New ground", l.fillY);
    auto pick = [](const wchar_t* prompt, std::vector<ListPickItem> items, const std::string& cur, RECT b, auto set) {
        std::string out;
        if (showListPicker(prompt, std::move(items), cur, belowBox(b), out)) set(out), mapPropsInfo(*gMapProps), mapPageChanged();
    };
    ui::pickBox(mem, {l.x1, l.tilesetY, right, l.tilesetY + l.rowH}, kTilesetNames[d.era & 7], d.era != d.eraBase, [pick](RECT b) {
        std::vector<ListPickItem> items;
        for (int e = 0; e < 8; ++e) items.push_back({kTilesetNames[e], L"", std::to_string(e)});
        pick(L"Tileset", items, std::to_string(gMapProps->era), b, [](const std::string& v) {
            gMapProps->era = std::stoi(v);
            gMapProps->fillTerrain = mapPropsFirstTerrain(gMapProps->era);  // the new tileset's terrains
        });
    });
    const int sw = scale(96);
    auto sizeBox = [&](int x, int& value, bool changed) {
        ui::pickBox(mem, {x, l.sizeY, x + sw, l.sizeY + l.rowH}, std::to_wstring(value), changed, [pick, &value](RECT b) {
            std::vector<ListPickItem> items;
            bool odd = true;
            for (int s : kMapSizes) items.push_back({std::to_wstring(s), L"", std::to_string(s)}), odd &= s != value;
            if (odd) items.push_back({std::to_wstring(value), L"", std::to_string(value)});  // an odd size: kept as a choice
            pick(L"Size", items, std::to_string(value), b, [&value](const std::string& v) { value = std::stoi(v); });
        });
    };
    sizeBox(l.x1, d.w, d.w != d.wBase);
    SetTextColor(mem, kDim);
    RECT xr{l.x1 + sw, l.sizeY, l.x1 + sw + scale(24), l.sizeY + l.rowH};
    DrawTextW(mem, L"x", -1, &xr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    sizeBox(l.x1 + sw + scale(24), d.h, d.h != d.hBase);
    ui::pickBox(mem, {l.x1, l.anchorY, right, l.anchorY + l.rowH}, kAnchorNames[std::clamp(d.anchorIndex, 0, 8)], false, [pick](RECT b) {
        std::vector<ListPickItem> items;
        for (int k = 0; k < 9; ++k) items.push_back({kAnchorNames[k], L"", std::to_string(k)});
        pick(L"Map content at", items, std::to_string(gMapProps->anchorIndex), b, [](const std::string& v) { gMapProps->anchorIndex = std::stoi(v); });
    });
    std::wstring fillName;
    const IsomTerrainTable* table = IsomTerrainTable::forEra(uint16_t(d.era));
    if (table && table->isTerrain(d.fillTerrain)) fillName = widen(table->terrainName(d.fillTerrain));
    ui::pickBox(mem, {l.x1, l.fillY, right, l.fillY + l.rowH}, fillName, false, [pick](RECT b) {
        std::vector<ListPickItem> items;
        if (const IsomTerrainTable* t = IsomTerrainTable::forEra(uint16_t(gMapProps->era)))
            for (uint16_t i = 1; i <= t->terrainCount(); ++i)
                if (t->isTerrain(i)) items.push_back({widen(t->terrainName(i)), L"", std::to_string(i)});
        pick(L"New ground", items, std::to_string(gMapProps->fillTerrain), b, [](const std::string& v) { gMapProps->fillTerrain = uint16_t(std::stoi(v)); });
    });
    // The buttons, then what they do (only the size and the tileset wait for them).
    SetTextColor(mem, kDim);
    RECT ir{l.x1, l.infoY, right, l.infoY + l.infoH};
    DrawTextW(mem, d.infoText.c_str(), -1, &ir, DT_WORDBREAK | DT_NOPREFIX);
    const bool differs = !d.infoText.empty();
    const int bh = scale(ui::kButtonH), cw = ui::buttonWidth(mem, L"Change the size and tileset"), rw = ui::buttonWidth(mem, L"Reset");
    d.changeRect = {l.x1, l.buttonsY, l.x1 + cw, l.buttonsY + bh};
    d.resetRect = {d.changeRect.right + scale(ui::kButtonGap), l.buttonsY, d.changeRect.right + scale(ui::kButtonGap) + rw, l.buttonsY + bh};
    ui::button(mem, d.changeRect, L"Change the size and tileset", ui::Tone::Primary, [] { mapPageApplyTerrain(); },
               L"Set the new size and tileset in the map (one undo step)", differs);
    ui::button(mem, d.resetRect, L"Reset", ui::Tone::Normal, [] { mapPageReload(); }, L"The map's size and tileset again", differs);
    gPropsSurface.end(hwnd);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK mapPropsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MapPropsDialog* d = gMapProps;
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);
    LRESULT res = 0;
    if (msg != WM_PAINT && gPropsSurface.message(hwnd, msg, wp, lp, res)) return res;
    switch (msg) {
    case WM_CREATE: {
        d->wnd = hwnd;
        const PropsLayout l = propsLayout();
        auto edit = [&](int id, int y, int hgt, DWORD extra, const std::wstring& text) {  // the only native controls: the texts
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra, l.x1, y, l.boxW, hgt, hwnd,
                                     HMENU(INT_PTR(id)), app.inst, nullptr);
            SendMessageW(h, WM_SETFONT, WPARAM(app.font), TRUE);
            darkControl(h, L"DarkMode_CFD");
            return h;
        };
        d->name = edit(1601, l.nameY, l.rowH, ES_AUTOHSCROLL, d->nameText);
        d->desc = edit(1602, l.descY, l.descH, ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL, d->descText);
        mapPropsInfo(*d);
        SetWindowPos(hwnd, nullptr, 0, 0, l.x1 + l.boxW + l.pad, l.height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);  // as big as its content
        if (!d->baseSet) {  // the map's values as the page shows them: "no change" is these
            d->baseSet = true;
            d->nameBase = windowText(d->name), d->descBase = windowText(d->desc);
            d->eraBase = d->era, d->wBase = d->w, d->hBase = d->h;
        }
        return 0;
    }
    case WM_PAINT: propsPaint(hwnd, *d); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE) mapPageChanged();
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(HDC(wp), kText);
        SetBkColor(HDC(wp), kCtl);
        return LRESULT(app.ctlBrush);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

enum MapPageKind { kMpProps, kMpPlayers, kMpClassicTrig, kMpClassicSettings, kMpCount };
const wchar_t* const kMpTitles[kMpCount] = {L"Map properties", L"Players and forces", L"Classic triggers", L"Data"};
const wchar_t* const kMpRailNames[kMpCount] = {L"Properties", L"Players", L"Classic triggers", L"Data"};

struct MapPageCopy {
    std::shared_ptr<void> data;  // MapPropsDialog, PlayersDialog, ClassicTrigDialog or ClassicDialog
    uint64_t revision = 0;       // the map's revision when the copy read it
    bool changed = false;        // changes that are not applied (the rail's dot)
};
struct MapPageCopies {
    const Editor* owner = nullptr;  // the map the copies are of
    MapPageCopy kinds[kMpCount];
};

struct MapPageHost {
    HWND wnd = nullptr, view = nullptr;
    HWND content[kClassicTabCount] = {};  // one; three for Classic settings (its tabs)
    int kind = -1;
    const Editor* ed = nullptr;
    bool ready = false;  // the content is made (until then, no reading back from its controls)
    int scrollX = 0, scrollY = 0;
    ui::Surface surface;  // the header's tabs, the bar's buttons
    RECT applyRect{}, revertRect{};
    bool typing = false;  // Map properties: a text is being typed (it goes into the map when the typing stops)
} gMp;

int mapPageKind(Workspace w) {
    switch (w) {
    case Workspace::Properties: return kMpProps;
    case Workspace::Players: return kMpPlayers;
    case Workspace::ClassicTriggers: return kMpClassicTrig;
    case Workspace::ClassicSettings: return kMpClassicSettings;
    default: return -1;
    }
}
int mapPageCommand(int kind) { return kind == kMpProps ? ID_SCEN_PROPS : kind == kMpPlayers ? ID_SCEN_PLAYERS : kind == kMpClassicTrig ? ID_SCEN_CLASSICTRIG : ID_SCEN_CLASSIC; }
int mapPageKindOfCommand(int id) {
    for (int k = 0; k < kMpCount; ++k)
        if (mapPageCommand(k) == id) return k;
    return -1;
}

// The active tab's copies (`make`: made when there are none). A tab that has another map now has none.
MapPageCopies* mapPageCopies(bool make) {
    if (gActive < 0 || gActive >= int(gDocs.size()) || !app.ed) return nullptr;
    auto& sp = gDocs[size_t(gActive)].mapPages;
    if (sp && sp->owner != app.ed.get()) sp.reset();
    if (!sp && make) sp = std::make_shared<MapPageCopies>(), sp->owner = app.ed.get();
    return sp.get();
}

// The rail's dot: the active tab's page `kind` has changes that are not applied.
bool mapPageHasChanges(int kind) {
    const MapPageCopies* c = kind >= 0 ? mapPageCopies(false) : nullptr;
    return c && c->kinds[kind].data && c->kinds[kind].changed;
}

bool mapPageChangesFor(int id) { return mapPageHasChanges(mapPageKindOfCommand(id)); }

// The copy's struct to the globals the page's code uses (nullptr: none).
void mapPageSetGlobals(int kind, void* data, void* players = nullptr) {
    gMapProps = kind == kMpProps ? static_cast<MapPropsDialog*>(data) : nullptr;
    gPlayersDlg = kind == kMpPlayers ? static_cast<PlayersDialog*>(data) : kind == kMpProps ? static_cast<PlayersDialog*>(players) : nullptr;
    gCt = kind == kMpClassicTrig ? static_cast<ClassicTrigDialog*>(data) : nullptr;
    gClassic = kind == kMpClassicSettings ? static_cast<ClassicDialog*>(data) : nullptr;
}

// A copy from the map. False: there is none to make (Classic settings without the game's .dat files).
bool mapPageLoad(int kind, MapPageCopy& c) {
    switch (kind) {
    case kMpProps: {
        auto d = std::make_shared<MapPropsDialog>();
        mapPropsLoad(*d);
        c.data = d;
        break;
    }
    case kMpPlayers: {
        auto d = std::make_shared<PlayersDialog>();
        playersLoad(*d);
        c.data = d;
        break;
    }
    case kMpClassicTrig: {
        auto d = std::make_shared<ClassicTrigDialog>();
        ctLoad(*d);
        c.data = d;
        break;
    }
    case kMpClassicSettings: {
        auto d = std::make_shared<ClassicDialog>();
        if (!classicLoad(*d)) return false;
        c.data = d;
        break;
    }
    default: return false;
    }
    c.revision = app.ed->revision(), c.changed = false;
    return true;
}

MapPageCopy* mapPageOpenCopy() {
    MapPageCopies* c = gMp.wnd && gMp.kind >= 0 ? mapPageCopies(false) : nullptr;
    return c ? &c->kinds[gMp.kind] : nullptr;
}

// The open page's fields into its copy (the Properties and Players pages keep some values in their controls).
void mapPageReadBack() {
    if (!gMp.ready) return;
    if (gMp.kind == kMpProps && gMapProps && IsWindow(gMapProps->wnd)) mapPropsRead(*gMapProps);
    if ((gMp.kind == kMpPlayers || gMp.kind == kMpProps) && gPlayersDlg && IsWindow(gPlayersDlg->wnd)) playersRead(*gPlayersDlg);
}

bool mapPageCopyChanged(int kind, const MapPageCopy& c) {
    if (!c.data) return false;
    switch (kind) {
    case kMpProps: return mapPropsChanged(*static_cast<const MapPropsDialog*>(c.data.get()));
    case kMpPlayers: return playersChanged(*static_cast<const PlayersDialog*>(c.data.get()));
    case kMpClassicTrig: return ctCopyChanged(*static_cast<const ClassicTrigDialog*>(c.data.get()));
    case kMpClassicSettings: return classicChanged(*static_cast<const ClassicDialog*>(c.data.get()));
    }
    return false;
}

int mapPageHeaderH() { return scale(56) + (gMp.kind == kMpClassicSettings ? scale(40) : 0); }
int mapPageFooterH() { return 0; }  // no Apply bar: every change goes into the map at once (GitHub feedback #102)

// The bar's part of the page, for a repaint.
void mapPageInvalidateBar() {
    if (!gMp.wnd) return;
    RECT rc;
    GetClientRect(gMp.wnd, &rc);
    rc.top = rc.bottom - mapPageFooterH();
    InvalidateRect(gMp.wnd, &rc, FALSE);
}

void mapPageAutoApply();
void mapPageCommit();
void mapPageFlush();
// After an edit on the page: the change goes into the map at once, one undo step (a typed text a moment after the last
// key). GitHub feedback #102: no Apply.
void mapPageChanged() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c) return;
    wchar_t cls[16] = L"";
    GetClassNameW(GetFocus(), cls, 16);
    if (_wcsicmp(cls, WC_EDITW) == 0 && IsChild(gMp.wnd, GetFocus())) {  // typing: when it stops
        gMp.typing = true, SetTimer(gMp.wnd, 2, 500, nullptr);
        return;
    }
    if (gMp.kind == kMpProps) mapPageAutoApply();
    else mapPageCommit();
}

// --- The view: the content, scrolled when the page is smaller than it -------------------------------------------

// Map properties: its parts one under the other (the map's fields, then players and forces); the other pages: one
// part (Map data: the tab that shows).
bool mapPageStacked() { return gMp.kind == kMpProps; }
int mapPagePartTop(int k) {
    int y = 0;
    for (int i = 0; i < k && mapPageStacked(); ++i)
        if (HWND c = gMp.content[i]) {
            RECT r;
            GetWindowRect(c, &r);
            y += r.bottom - r.top;
        }
    return y;
}
SIZE mapPageContentSize() {
    SIZE s{0, 0};
    for (int k = 0; k < kClassicTabCount; ++k)
        if (HWND c = gMp.content[k]) {
            RECT r;
            GetWindowRect(c, &r);
            s.cx = std::max<LONG>(s.cx, r.right - r.left);
            s.cy = std::max<LONG>(s.cy, mapPagePartTop(k) + r.bottom - r.top);
        }
    return s;
}

void mapPageScrollTo(int x, int y) {
    if (!gMp.view) return;
    RECT rc;
    GetClientRect(gMp.view, &rc);
    const SIZE s = mapPageContentSize();
    gMp.scrollX = std::clamp(x, 0, std::max(0, int(s.cx) - int(rc.right)));
    gMp.scrollY = std::clamp(y, 0, std::max(0, int(s.cy) - int(rc.bottom)));
    for (int k = 0; k < kClassicTabCount; ++k)
        if (HWND c = gMp.content[k]) SetWindowPos(c, nullptr, -gMp.scrollX, mapPagePartTop(k) - gMp.scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMin = 0, si.nMax = std::max(0, int(s.cy) - 1), si.nPage = UINT(rc.bottom), si.nPos = gMp.scrollY;
    SetScrollInfo(gMp.view, SB_VERT, &si, TRUE);
    si.nMax = std::max(0, int(s.cx) - 1), si.nPage = UINT(rc.right), si.nPos = gMp.scrollX;
    SetScrollInfo(gMp.view, SB_HORZ, &si, TRUE);
}

// The text under the size and tileset buttons got taller or shorter: the part's new height, the players under it.
void propsFitHeight(HWND hwnd) {
    const PropsLayout l = propsLayout();
    RECT wr;
    GetClientRect(hwnd, &wr);
    if (wr.bottom == l.height) return;
    SetWindowPos(hwnd, nullptr, 0, 0, wr.right, l.height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    mapPageScrollTo(gMp.scrollX, gMp.scrollY);
}

LRESULT CALLBACK mapPageViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE: mapPageScrollTo(gMp.scrollX, gMp.scrollY); return 0;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(HDC(wp), &rc, app.bgBrush);
        return 1;
    }
    case WM_VSCROLL:
    case WM_HSCROLL: {
        const int bar = msg == WM_VSCROLL ? SB_VERT : SB_HORZ;
        SCROLLINFO si{sizeof si, SIF_ALL};
        GetScrollInfo(hwnd, bar, &si);
        int pos = bar == SB_VERT ? gMp.scrollY : gMp.scrollX;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= scale(32); break;
        case SB_LINEDOWN: pos += scale(32); break;
        case SB_PAGEUP: pos -= int(si.nPage); break;
        case SB_PAGEDOWN: pos += int(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = 1 << 30; break;
        }
        bar == SB_VERT ? mapPageScrollTo(gMp.scrollX, pos) : mapPageScrollTo(pos, gMp.scrollY);
        return 0;
    }
    case WM_MOUSEWHEEL: mapPageScrollTo(gMp.scrollX, gMp.scrollY - GET_WHEEL_DELTA_WPARAM(wp) * scale(32) * 3 / WHEEL_DELTA); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- The page ------------------------------------------------------------------------------------------------------

void mapPageShowTab(int tab) {
    if (gMp.kind != kMpClassicSettings || !gClassic) return;
    gClassic->tab = std::clamp(tab, 0, kClassicTabCount - 1);
    for (int k = 0; k < kClassicTabCount; ++k) ShowWindow(gMp.content[k], k == gClassic->tab ? SW_SHOW : SW_HIDE);
    InvalidateRect(gMp.wnd, nullptr, FALSE);
}

void mapPageApply();
void mapPageRevert();

void mapPagePaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    gMp.surface.begin(hwnd);
    const int pad = scale(ui::kDialogPad), headH = mapPageHeaderH(), footH = mapPageFooterH();
    // The header: the page's title; Classic settings' tabs under it.
    if (gMp.kind >= 0) {
        SelectObject(mem, ui::titleFont());
        SetTextColor(mem, kText);
        RECT t{pad, 0, rc.right - pad, scale(56)};
        DrawTextW(mem, kMpTitles[gMp.kind], -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(mem, app.font);
    }
    if (gMp.kind == kMpClassicSettings && gClassic) {
        int x = pad - scale(12);
        const int icon = scale(16), top = scale(56);
        for (int k = 0; k < kClassicTabCount; ++k) {
            const RECT t{x, top, x + scale(14) + icon + scale(8) + ui::textWidth(mem, kClassicTabNames[k]) + scale(14), headH};
            const bool on = gClassic->tab == k, hot = ui::hot(t);
            if (on || hot) fillAlpha(mem, t, on ? kSel : kRowHot, 255);
            if (on) fillAlpha(mem, {t.left, t.bottom - scale(2), t.right, t.bottom}, kAccent, 255);
            const COLORREF c = on ? kAccent : hot ? kText : kDim;
            uiIcon(mem, {t.left + scale(14), t.top, t.left + scale(14) + icon, t.bottom}, kClassicTabIcons[k], c, icon);
            SetTextColor(mem, c);
            RECT tt{t.left + scale(14) + icon + scale(8), t.top, t.right, t.bottom};
            DrawTextW(mem, kClassicTabNames[k], -1, &tt, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            gMp.surface.hits.push_back({t, nullptr, [k] { mapPageShowTab(k); }, L"", IDC_HAND, {}});
            x = t.right + scale(4);
        }
    }
    fillAlpha(mem, {0, headH - 1, rc.right, headH}, kBorder, 255);
    // The bar: a line over it; what the page has; Apply and Revert at the right (Map properties: none).
    if (!footH) {
        gMp.applyRect = gMp.revertRect = RECT{};
        gMp.surface.end(hwnd);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, of), SelectObject(mem, ob);
        DeleteObject(back);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }
    const int barTop = rc.bottom - footH;
    fillAlpha(mem, {0, barTop, rc.right, barTop + 1}, kBorder, 255);
    const MapPageCopy* c = mapPageOpenCopy();
    const bool changed = c && c->changed;
    const int by = barTop + (footH - scale(ui::kButtonH)) / 2;
    if (changed) {
        ui::changedDot(mem, pad + scale(4), by + scale(ui::kButtonH) / 2);
        SetTextColor(mem, kDim);
        RECT t{pad + scale(16), by, rc.right / 2, by + scale(ui::kButtonH)};
        DrawTextW(mem, L"Changes that are not applied", -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    const std::wstring apply = L"Apply", revert = L"Revert";
    const int rw = ui::buttonWidth(mem, revert), aw = ui::buttonWidth(mem, apply);
    gMp.revertRect = {int(rc.right) - pad - rw, by, int(rc.right) - pad, by + scale(ui::kButtonH)};
    gMp.applyRect = {gMp.revertRect.left - scale(ui::kButtonGap) - aw, by, gMp.revertRect.left - scale(ui::kButtonGap), by + scale(ui::kButtonH)};
    ui::button(mem, gMp.applyRect, apply, ui::Tone::Primary, [] { mapPageApply(); }, L"Set these values in the map (one undo step)", changed);
    ui::button(mem, gMp.revertRect, revert, ui::Tone::Normal, [] { mapPageRevert(); }, L"Show the map's values again", changed);
    gMp.surface.end(hwnd);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void mapPageReloadIfStale();

LRESULT CALLBACK mapPageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT res = 0;
    if (msg != WM_PAINT && gMp.surface.message(hwnd, msg, wp, lp, res)) return res;
    switch (msg) {
    case WM_PAINT: mapPagePaint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if (wp == 1) mapPageReloadIfStale();
        if (wp == 2) KillTimer(hwnd, 2), gMp.typing = false, gMp.kind == kMpProps ? mapPageAutoApply() : mapPageCommit();  // the typing stopped
        return 0;
    case WM_COMMAND: return 0;  // Enter and Esc (the dialog keys): nothing; Apply is a click
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void mapPageRegister() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = app.inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    const std::pair<WNDPROC, const wchar_t*> classes[] = {{mapPageProc, L"StarDraftMapPage"},
                                                          {mapPageViewProc, L"StarDraftMapPageView"},
                                                          {mapPropsProc, L"StarDraftMapProps"},
                                                          {playersDialogProc, L"StarDraftPlayers"},
                                                          {classicPageProc, L"StarDraftClassicPage"},
                                                          {classicTrigProc, L"StarDraftClassicTriggers"},
                                                          {ctListProc, L"StarDraftClassicTrigList"}};  // the painted lists
    for (const auto& [proc, name] : classes) wc.lpfnWndProc = proc, wc.lpszClassName = name, RegisterClassExW(&wc);
}

// The content of the open page from its copy (made again after Apply, Revert, a reload, a theme change).
void mapPageMakeContent() {
    gMp.ready = false;
    for (HWND& c : gMp.content)
        if (c) DestroyWindow(c), c = nullptr;
    if (gClassic && gClassic->readOnlyBrush) DeleteObject(gClassic->readOnlyBrush), gClassic->readOnlyBrush = nullptr;
    gMp.scrollX = gMp.scrollY = 0;
    auto make = [](const wchar_t* cls, void* param) {
        return CreateWindowExW(WS_EX_CONTROLPARENT, cls, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, gMp.view, nullptr, app.inst, param);
    };
    switch (gMp.kind) {
    case kMpProps:  // the map's fields, then players and forces
        gMp.content[0] = make(L"StarDraftMapProps", nullptr);
        if (gPlayersDlg) gMp.content[1] = make(L"StarDraftPlayers", nullptr);
        break;
    case kMpPlayers: gMp.content[0] = make(L"StarDraftPlayers", nullptr); break;
    case kMpClassicTrig: gMp.content[0] = make(L"StarDraftClassicTriggers", nullptr); break;
    case kMpClassicSettings:
        gClassic->readOnlyBrush = CreateSolidBrush(kReadOnly);
        for (int k = 0; k < kClassicTabCount; ++k) gMp.content[k] = make(L"StarDraftClassicPage", reinterpret_cast<void*>(INT_PTR(k)));
        break;
    }
    if (gMp.kind == kMpClassicSettings) mapPageShowTab(gClassic->tab);
    else
        for (HWND c : gMp.content)
            if (c) ShowWindow(c, SW_SHOW);
    gMp.ready = true;
    mapPageScrollTo(0, 0);
    if (gMp.kind != kMpProps) mapPageChanged();
    InvalidateRect(gMp.wnd, nullptr, FALSE);
}

// Closes the open page (its window goes; its copy stays with the tab).
void mapPageClose() {
    if (!gMp.wnd) return;
    KillTimer(gMp.wnd, 2), gMp.typing = false;  // a text being typed goes into the map
    if (gMp.kind == kMpProps) mapPageAutoApply();
    else mapPageCommit();
    gMp.ready = false;
    KillTimer(gMp.wnd, 1);
    DestroyWindow(gMp.wnd);
    if (gClassic && gClassic->readOnlyBrush) DeleteObject(gClassic->readOnlyBrush), gClassic->readOnlyBrush = nullptr;
    mapPageSetGlobals(-1, nullptr);
    gMp = MapPageHost{};
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
}

// Opens page `kind` of the active tab's map (hidden: setWorkspace shows it after the layout). False: no page.
bool mapPageOpen(int kind) {
    if (!app.ed || !app.ed->isOpen() || activePage()) return false;
    if (gMp.wnd && gMp.kind == kind && gMp.ed == app.ed.get()) return true;
    mapPageClose();
    MapPageCopies* copies = mapPageCopies(true);
    if (!copies) return false;
    MapPageCopy& c = copies->kinds[kind];
    if (c.data && !c.changed && c.revision != app.ed->revision()) c.data.reset();  // the map changed: read it again
    if (!c.data && !mapPageLoad(kind, c)) {
        setMessage(L"Map data: cannot read the game's units.dat, weapons.dat, upgrades.dat and techdata.dat");
        return false;
    }
    void* players = nullptr;
    if (kind == kMpProps) {  // players and forces are on this page too
        MapPageCopy& pc = copies->kinds[kMpPlayers];
        if (pc.data && pc.revision != app.ed->revision()) pc.data.reset();
        if (pc.data || mapPageLoad(kMpPlayers, pc)) players = pc.data.get();
    }
    mapPageRegister();
    mapPageSetGlobals(kind, c.data.get(), players);
    gMp.kind = kind, gMp.ed = app.ed.get();
    gMp.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"StarDraftMapPage", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, nullptr);
    gMp.view = CreateWindowExW(WS_EX_CONTROLPARENT, L"StarDraftMapPageView", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL, 0, 0, 1,
                               1, gMp.wnd, nullptr, app.inst, nullptr);
    darkControl(gMp.view, L"DarkMode_Explorer");  // the scroll bars in the theme
    mapPageMakeContent();
    SetTimer(gMp.wnd, 1, 300, nullptr);  // the map changed from outside: a copy without changes reads it again
    return true;
}

// The copy again from the map, and the content again (Revert, after Apply, a reload).
void mapPageReload() {
    MapPageCopy* c = mapPageOpenCopy();
    if (!c) return;
    MapPageCopy fresh;
    if (!mapPageLoad(gMp.kind, fresh)) return;
    if (gMp.kind == kMpClassicSettings) {  // the same tab and entries show
        auto* was = static_cast<ClassicDialog*>(c->data.get());
        auto* now = static_cast<ClassicDialog*>(fresh.data.get());
        now->tab = was->tab;
        for (int k = 0; k < kClassicTabCount; ++k) now->pages[k].entry = was->pages[k].entry;
        if (was->readOnlyBrush) DeleteObject(was->readOnlyBrush), was->readOnlyBrush = nullptr;
    }
    gMp.ready = false;
    for (HWND& w : gMp.content)  // the old content goes before its copy does
        if (w) DestroyWindow(w), w = nullptr;
    *c = fresh;
    void* players = nullptr;
    if (gMp.kind == kMpProps)
        if (MapPageCopies* all = mapPageCopies(false); all && mapPageLoad(kMpPlayers, all->kinds[kMpPlayers])) players = all->kinds[kMpPlayers].data.get();
    mapPageSetGlobals(gMp.kind, c->data.get(), players);
    mapPageMakeContent();
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
}

void mapPageReloadIfStale() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c || !app.ed || gMp.ed != app.ed.get()) return;
    // Set at once: the map changed elsewhere (an undo) when the copy's revision is old.
    if (gMp.typing) return;  // a text being typed goes in first
    if (gMp.kind != kMpProps) {  // a change the page did not report yet (not while a text has the focus: its typing)
        wchar_t cls[16] = L"";
        GetClassNameW(GetFocus(), cls, 16);
        if (_wcsicmp(cls, WC_EDITW) != 0 || !IsChild(gMp.wnd, GetFocus())) mapPageCommit();
    }
    if (c->revision != app.ed->revision() && !c->changed) mapPageReload();
}

// Map properties: what changed goes into the map now (each an undo step): the title and the description, players and
// forces. The size and the tileset wait for their button (the copy keeps them: the rail's dot).
void mapPageAutoApply() {
    if (!gMp.ready || gMp.kind != kMpProps || !app.ed) return;
    MapPageCopies* all = mapPageCopies(false);
    if (!all) return;
    mapPageReadBack();
    MapPageCopy& pc = all->kinds[kMpProps];
    MapPageCopy& pl = all->kinds[kMpPlayers];
    if (gMapProps && mapPropsTextChanged(*gMapProps)) mapPropsApplyText(*gMapProps);
    if (gPlayersDlg && playersChanged(*gPlayersDlg)) {
        playersApply(*gPlayersDlg);
        gPlayersDlg->basePlayers = gPlayersDlg->players, gPlayersDlg->baseForces = gPlayersDlg->forces;
    }
    pc.revision = pl.revision = app.ed->revision();  // the copies have the map's values (no reload)
    pl.changed = false;
    const bool terrain = gMapProps && mapPropsChanged(*gMapProps);
    if (terrain != pc.changed) {
        pc.changed = terrain;
        if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
    }
}

// Ctrl+P (Players and forces): the Map properties page, scrolled to them.
void mapPageShowPlayers() {
    if (gMp.wnd && gMp.kind == kMpProps) mapPageScrollTo(0, mapPagePartTop(1));
}

// Map properties: the size and tileset (their button), as one undo step; then the page shows the map again.
void mapPageApplyTerrain() {
    if (!gMp.ready || gMp.kind != kMpProps || !gMapProps) return;
    KillTimer(gMp.wnd, 2), gMp.typing = false;
    mapPageAutoApply();
    if (!mapPropsApplyTerrain(*gMapProps)) return;
    if (gMp.kind == kMpProps) mapPageReload();
}

// Applies one copy of the active tab (the page need not show). False: the map did not take it (the copy stays).
bool mapPageApplyCopy(int kind, MapPageCopy& c) {
    switch (kind) {
    case kMpProps: return mapPropsApply(*static_cast<MapPropsDialog*>(c.data.get()));
    case kMpPlayers: return playersApply(*static_cast<PlayersDialog*>(c.data.get()));
    case kMpClassicTrig: return ctApply(*static_cast<ClassicTrigDialog*>(c.data.get()));
    case kMpClassicSettings: return classicApply(*static_cast<ClassicDialog*>(c.data.get()));
    }
    return false;
}

// The copy's values are the map's now (after they went into it): what the copy compares with for a change.
void mapPageRebase(int kind, MapPageCopy& c) {
    switch (kind) {
    case kMpPlayers: {
        auto& d = *static_cast<PlayersDialog*>(c.data.get());
        d.basePlayers = d.players, d.baseForces = d.forces;
        break;
    }
    case kMpClassicTrig: {
        auto& d = *static_cast<ClassicTrigDialog*>(c.data.get());
        d.before = d.trig.triggers;
        break;
    }
    case kMpClassicSettings: {
        auto& d = *static_cast<ClassicDialog*>(c.data.get());
        d.namesBefore = d.names, d.unitsBefore = d.units, d.upgradesBefore = d.upgrades, d.techsBefore = d.techs;
        break;
    }
    }
}

// The page's change into the map now (one undo step), without reading the page again (it keeps its place and focus).
void mapPageCommit() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c || !app.ed || gMp.kind == kMpProps) return;
    mapPageReadBack();
    if (!mapPageCopyChanged(gMp.kind, *c)) return;
    // The map changed elsewhere (an undo) after the copy's last commit: the copy is old. The page shows the map again
    // (the change made since is lost, so that an old copy never writes over the undo).
    if (c->revision != app.ed->revision()) return mapPageReload();
    if (mapPageApplyCopy(gMp.kind, *c)) mapPageRebase(gMp.kind, *c), c->revision = app.ed->revision();
    const bool now = mapPageCopyChanged(gMp.kind, *c);
    if (now != c->changed) {
        c->changed = now;
        if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
    }
}

// A text being typed goes into the map now (before a save, a build, or the map closes).
void mapPageFlush() {
    if (!gMp.ready || gMp.ed != app.ed.get()) return;
    KillTimer(gMp.wnd, 2), gMp.typing = false;
    if (gMp.kind == kMpProps) mapPageAutoApply();
    else mapPageCommit();
}

// Apply (the bar's button): the copy into the map as one undo step, then the map's values again.
void mapPageApply() {
    MapPageCopy* c = gMp.ready ? mapPageOpenCopy() : nullptr;
    if (!c) return;
    mapPageChanged();
    if (!c->changed) return;
    const int kind = gMp.kind;
    if (!mapPageApplyCopy(kind, *c)) return mapPageChanged();
    // Applying the size can lay out the window again; the page is still this one.
    if (gMp.kind == kind && mapPageOpenCopy() == c) mapPageReload();
}

// Revert (the bar's button): the changes go; the map's values again.
void mapPageRevert() {
    if (gMp.ready) mapPageReload();
}

// Leaving the map (closing its tab, or StarDraft) with changes that are not applied: apply them, or not, or stay.
bool mapPagesConfirm() {
    if (gMp.wnd && gMp.ed == app.ed.get()) mapPageFlush();
    MapPageCopies* c = mapPageCopies(false);
    if (!c) return true;
    std::wstring names;
    for (int k = 0; k < kMpCount; ++k)
        if (c->kinds[k].data && c->kinds[k].changed) names += (names.empty() ? L"" : L", ") + std::wstring(kMpRailNames[k]);
    if (names.empty()) return true;
    const std::wstring q = L"These pages have changes that are not applied: " + names + L".\n\nApply them to the map?";
    const int r = themedMessageBox(app.main, q.c_str(), L"Changes that are not applied", MB_YESNOCANCEL | MB_ICONQUESTION, {L"&Apply", L"Do &not apply", L"Cancel"});
    if (r == IDCANCEL) return false;
    for (int k = 0; k < kMpCount; ++k) {
        MapPageCopy& one = c->kinds[k];
        if (!one.data || !one.changed) continue;
        if (r == IDYES && !mapPageApplyCopy(k, one)) return false;  // the map did not take it: stay
        if (gMp.wnd && gMp.kind == k && gMp.ed == app.ed.get()) mapPageReload();  // the page shows the map's values
        else one = MapPageCopy{};
    }
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
    return true;
}

// The page's place: the main area (as the workspaces have it).
void mapPageLayout(int x, int y, int w, int h) {
    if (!gMp.wnd) return;
    MoveWindow(gMp.wnd, x, y, w, h, TRUE);
    const int top = mapPageHeaderH(), bottom = std::max(top + 1, h - mapPageFooterH());
    MoveWindow(gMp.view, 0, top, w, bottom - top, TRUE);
    mapPageScrollTo(gMp.scrollX, gMp.scrollY);
    InvalidateRect(gMp.wnd, nullptr, FALSE);
}

// A theme change: the content again in the theme's colors (the copy keeps what the fields had).
void mapPageThemeChanged() {
    if (!gMp.ready) return;
    mapPageReadBack();
    mapPageMakeContent();
}

// Tests: the page that shows; Apply (or Revert) clicked through the click area the bar's paint made.
HWND mapPageWindow() { return gMp.wnd; }
bool mapPageClickButton(bool apply) {
    if (gMp.ready) {  // no bar: changes go in at once; "Revert" shows the map's values again
        KillTimer(gMp.wnd, 2), gMp.typing = false;
        if (gMp.kind == kMpProps && apply) mapPageAutoApply(), mapPageApplyTerrain();
        else if (apply) mapPageCommit();
        else mapPageReload();
        return true;
    }
    if (!gMp.wnd) return false;
    RedrawWindow(gMp.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    const RECT r = apply ? gMp.applyRect : gMp.revertRect;
    const DataWidgetHit* h = gMp.surface.at({(r.left + r.right) / 2, (r.top + r.bottom) / 2});
    if (!h || !h->click) return false;
    const auto click = h->click;  // the click paints the page again (the hits change)
    click();
    return true;
}

// Datashot mappages: each map page of the tab's mode in the main area (mappage_<page>[_light].png), and through the
// page's fields and the bar's buttons: a change turns on Apply and the rail's dot; leaving the page keeps the change
// and the map as it was; back on the page the change shows (mappage_<page>_changed[_light].png); Revert gives the
// map's values back; Apply sets the change as one undo step; after one undo the page reads the map again.
// mappages.txt says what holds.
void mapPagesDatashot(const fs::path& dir) {
    std::ofstream log(dir / "mappages.txt", std::ios::app);
    if (!app.ed || !app.ed->isOpen()) return;
    const std::string light = gLightTheme ? "_light" : "";
    auto yes = [](bool b) { return b ? "yes" : "NO"; };
    auto settle = [] {
        RedrawWindow(app.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        for (int i = 0; i < 15; ++i) pump(), Sleep(30);  // the new controls reach the compositor
    };
    const char* const names[kMpCount] = {"props", "players", "classic_triggers", "classic_settings"};
    {  // Map properties with players and forces: each change goes into the map at once; the size waits for its button.
        command(ID_WS_MAP);
        const uint64_t revBefore = app.ed->revision();
        const auto t0 = std::chrono::steady_clock::now();
        command(ID_SCEN_PROPS);
        const double openMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        settle();
        for (int i = 0; i < 20; ++i) pump(), Sleep(30), mapPageReloadIfStale();  // its timer (a translated name must not go in)
        const bool unchanged = app.ed->revision() == revBefore;
        const bool open = gMp.wnd && gMp.kind == kMpProps && gMapProps && gPlayersDlg && gMp.content[1];
        capture(dir / ("mappage_props" + light + ".png"), app.main);
        bool title = false, titleUndo = false, player = false, sizeWaits = false, sizeSet = false, ctrlP = false;
        if (open) {
            const std::string before = app.ed->scenarioName();
            SetWindowTextW(gMapProps->name, L"Changed by the test");  // (the focus is not in it: at once)
            title = app.ed->scenarioName() == "Changed by the test";
            app.ed->undo();
            mapPageReloadIfStale();
            titleUndo = app.ed->scenarioName() == before && gMapProps && windowText(gMapProps->name) == widen(before);
            int cur = 0;  // the next controller for player 2, as its picker sets it
            for (size_t i = 0; i < std::size(kControllers); ++i)
                if (kControllers[i].first == gPlayersDlg->players[1].controller) cur = int(i);
            const int next = (cur + 1) % int(std::size(kControllers));
            gPlayersDlg->players[1].controller = kControllers[next].first;
            playersSet(*gPlayersDlg);
            player = app.ed->player(1).controller == kControllers[next].first;
            app.ed->undo();
            mapPageReloadIfStale();
            const int w = app.ed->width(), other = w == 128 ? 0 : 2;  // 64 or 128
            gMapProps->w = kMapSizes[other];  // as the Width picker sets it
            mapPropsInfo(*gMapProps), mapPageChanged();
            sizeWaits = app.ed->width() == w && mapPageHasChanges(kMpProps) && !gMapProps->infoText.empty();
            capture(dir / ("mappage_props_size" + light + ".png"), app.main);
            {  // the size and the tileset: the longest text, the players part moved down under it
                const int era = gMapProps->era;
                gMapProps->era = (era + 1) % 8;
                mapPropsInfo(*gMapProps), mapPageChanged();
                for (int i = 0; i < 6; ++i) pump();
                capture(dir / ("mappage_props_tileset" + light + ".png"), app.main);
                gMapProps->era = era;
                mapPropsInfo(*gMapProps), mapPageChanged();
                for (int i = 0; i < 6; ++i) pump();
            }
            mapPageApplyTerrain();  // Change the size and tileset
            sizeSet = app.ed->width() != w && !mapPageHasChanges(kMpProps);
            app.ed->undo();
            mapPageReloadIfStale();
            command(ID_SCEN_PLAYERS);
            ctrlP = gMp.kind == kMpProps && gMp.scrollY > 0;
        }
        log << "props: one page with players and forces " << yes(open) << " (opened in " << int(openMs) << " ms), the title goes into the map at once "
            << yes(title) << ", undo gives it back on the page " << yes(titleUndo) << ", a player's change at once " << yes(player)
            << ", the size waits for its button (the dot) " << yes(sizeWaits) << ", the button sets it " << yes(sizeSet)
            << ", Players and forces scrolls to them " << yes(ctrlP) << ", opening it leaves the map as it was " << yes(unchanged) << "\n";
        command(ID_WS_MAP);
    }
    for (int k = kMpClassicTrig; k < kMpCount; ++k) {
        const int id = mapPageCommand(k);
        if (!commandInMode(id)) {
            log << names[k] << ": not in this tab's mode\n";
            continue;
        }
        command(id);
        settle();
        const bool open = gMp.wnd && gMp.kind == k && IsWindowVisible(gMp.wnd);
        RedrawWindow(gWsBar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        bool railOn = false;
        for (const auto& h : gWsHits) railOn |= h.id == id;
        railOn &= mapPageCommandOf(gWorkspace) == id;
        capture(dir / ("mappage_" + std::string(names[k]) + light + ".png"), app.main);
        log << names[k] << ": the page shows in the main area " << yes(open) << ", its rail item on " << yes(railOn) << "\n";
        if (!open) continue;
        const uint64_t rev = app.ed->revision();
        // One change through the page's fields, and where it shows.
        int target = 0;  // players: the controller of player 2; classic settings: the marine's "Use default"
        const size_t trigCount = gCt ? gCt->trig.triggers.size() : 0;
        auto change = [&] {
            if (k == kMpClassicTrig) {
                ctAddTrigger();
            } else {
                mapPageShowTab(kClassicUnits);
                classicSelect(kClassicUnits, 0);  // the marine
                ClassicPage& pg = gClassic->pages[kClassicUnits];
                target = !classicChecked(pg.useDefault);
                SendMessageW(pg.useDefault, BM_SETCHECK, target ? BST_CHECKED : BST_UNCHECKED, 0);
                SendMessageW(pg.wnd, WM_COMMAND, MAKEWPARAM(kCsUseDefault, BN_CLICKED), LPARAM(pg.useDefault));
            }
            mapPageChanged();
        };
        auto shows = [&] {
            if (k == kMpClassicTrig) return gCt && gCt->trig.triggers.size() == trigCount + 1;
            return gClassic && int(gClassic->units.units[0].useDefault) == target;
        };
        auto inMap = [&] {
            if (k == kMpClassicTrig) return app.ed->classicTriggers().triggers.size() == trigCount + 1;
            return int(app.ed->unitSettings().units[0].useDefault) == target;
        };
        change();
        const bool atOnce = !mapPageHasChanges(k) && shows() && inMap() && app.ed->revision() != rev;
        command(ID_WS_MAP);  // leave the page, and back: the change is the map's
        command(id);
        settle();
        const bool back = shows() && inMap();
        capture(dir / ("mappage_" + std::string(names[k]) + "_changed" + light + ".png"), app.main);
        app.ed->undo();
        mapPageReloadIfStale();  // as the page's timer does
        const bool undone = !inMap() && !shows() && !mapPageHasChanges(k);
        log << names[k] << ": a change goes into the map at once (no Apply) " << yes(atOnce) << ", there again on the page " << yes(back)
            << ", one undo gives the map's value back and the page shows it " << yes(undone) << "\n";
        if (k == kMpClassicSettings) {  // the unit's rename button: the name box in place of the title; Enter sets the name (#91)
            mapPageShowTab(kClassicUnits);
            classicSelect(kClassicUnits, 3);  // the goliath
            ClassicPage& pg = gClassic->pages[kClassicUnits];
            const bool button = IsWindowVisible(pg.rename) != FALSE;
            SendMessageW(pg.wnd, WM_COMMAND, MAKEWPARAM(kCsRename, BN_CLICKED), LPARAM(pg.rename));
            const bool box = IsWindowVisible(pg.name) && !IsWindowVisible(pg.title);
            SetWindowTextW(pg.name, L"Walker");
            settle();
            capture(dir / ("mappage_rename" + light + ".png"), app.main);
            SendMessageW(pg.name, WM_KEYDOWN, VK_RETURN, 0);
            settle();
            const bool set = app.ed->unitSettingName(3) == "Walker" && IsWindowVisible(pg.title) && windowText(pg.title).find(L"Walker") != std::wstring::npos;
            capture(dir / ("mappage_renamed" + light + ".png"), app.main);
            app.ed->undo();
            mapPageReloadIfStale();
            log << names[k] << ": the rename button " << yes(button) << ", the name box in place of the title " << yes(box)
                << ", Enter sets the name, the title shows it " << yes(set) << "\n";
        }
        command(ID_WS_MAP);
    }
}

// The main loop: keys while a control of the page has the focus. Tab moves between the fields; the shortcuts that
// are not the page's own (Ctrl+C, Ctrl+V and the arrow keys are) still work.
bool mapPageHasFocus() {
    const HWND f = GetFocus();
    return gMp.wnd && f && (f == gMp.wnd || IsChild(gMp.wnd, f));
}
bool mapPageKeepsKey(const MSG& m) {
    if (m.message != WM_KEYDOWN && m.message != WM_SYSKEYDOWN) return false;
    if (!(GetKeyState(VK_CONTROL) & 0x8000)) return m.message == WM_KEYDOWN && (m.wParam < VK_F1 || m.wParam > VK_F24);
    switch (m.wParam) {
    case 'A': case 'C': case 'V': case 'X':
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_HOME: case VK_END: case VK_DELETE: case VK_INSERT: return true;
    }
    return false;
}
