// Options > Program options (Ctrl+,): the options of the program in one window, painted with the kit (the snap and
// placement options are in the Options menu). A change applies at once, through the same command as the menu
// (command(ID_OPT_...), so the shortcuts and the tests that call the commands get the same result). Close closes the
// window. Included by main.cpp after about.inc.

int scrollFromMessage(HWND hwnd, WPARAM wp, int current, int line);
void cleanScroll(HWND list);

struct OptionsWindow {
    HWND wnd = nullptr;
    ui::Surface surface;
    int scroll = 0, height = 0;  // the content's scroll position and full height (px)
    int shownLight = -1;          // the theme the title bar has (it follows a theme change)
    std::map<std::string, RECT> parts;  // where each option is (client px), for the tests
} gOptions;

void optionsRefresh() {
    if (gOptions.wnd) InvalidateRect(gOptions.wnd, nullptr, FALSE);
}

void optionsPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (gOptions.shownLight != int(gLightTheme)) {  // a theme change: the title bar and scroll bar too
        gOptions.shownLight = int(gLightTheme);
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark), titleColors(hwnd);
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    fillAlpha(mem, rc, kBg, 255);
    SetBkMode(mem, TRANSPARENT);
    gOptions.surface.begin(hwnd);
    gOptions.parts.clear();

    const int pad = scale(ui::kDialogPad), rowH = scale(ui::kFormRowH), fieldH = scale(ui::kFieldH);
    const int footerH = pad + scale(ui::kButtonH) + pad, contentBottom = std::max<int>(0, rc.bottom - footerH);
    FormLayout l{};
    l.x0 = pad, l.labelW = scale(150), l.rowH = rowH, l.colRight = rc.right - pad, l.colW = l.colRight - l.x0;
    const int fx = l.x0 + l.labelW;  // the field column
    int y = pad - gOptions.scroll;
    IntersectClipRect(mem, 0, 0, rc.right, contentBottom);

    auto fieldTop = [&](int rowY) { return rowY + (rowH - fieldH) / 2; };
    auto note = [&](int x, int noteY, const std::wstring& text) {
        HGDIOBJ o = SelectObject(mem, ui::smallFont());
        SetTextColor(mem, kDim);
        RECT r{x, noteY, l.colRight, noteY + scale(20)};
        DrawTextW(mem, text.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(mem, o);
    };
    // A segmented choice: one button for each choice, the chosen one Primary, 4 px apart.
    auto segmented = [&](int rowY, const std::vector<std::pair<std::wstring, std::wstring>>& choices, int chosen, std::function<void(int)> pick,
                         const std::string& part) {
        int x = fx;
        for (size_t i = 0; i < choices.size(); ++i) {
            const int w = ui::buttonWidth(mem, choices[i].first);
            const RECT r{x, fieldTop(rowY), x + w, fieldTop(rowY) + fieldH};
            ui::button(mem, r, choices[i].first, int(i) == chosen ? ui::Tone::Primary : ui::Tone::Normal, [pick, i] { pick(int(i)); }, choices[i].second);
            gOptions.parts[part + "." + std::to_string(i)] = r;
            x += w + scale(4);
        }
    };
    auto checkRow = [&](int rowY, const std::wstring& label, bool on, int id, const std::string& part, const std::wstring& tip = L"", bool enabled = true) {
        const RECT r{l.x0, rowY, l.x0 + scale(24) + ui::textWidth(mem, label), rowY + rowH};
        ui::check(mem, r, label, on, [id] { command(id); }, tip, false, enabled);
        gOptions.parts[part] = r;
    };

    // Appearance.
    y = ui::sectionHeader(mem, l, y, L"Appearance");
    ui::fieldLabel(mem, l, y, L"Theme", false);
    segmented(y, {{L"Dark", L"Dark theme (default)"}, {L"Light", L"Light theme"}}, gLightTheme ? 1 : 0,
              [](int i) { command(i ? ID_THEME_LIGHT : ID_THEME_DARK); }, "theme");
    y += rowH;
    // The language: a choice that applies at the next start (the names of the languages are in their own language).
    ui::fieldLabel(mem, l, y, L"Language", false);
    {
        const std::wstring& lang = gLanguage;
        const int chosen = lang == L"en" ? 1 : lang == L"ko" ? 2 : lang == L"es" ? 3 : 0;
        const std::vector<std::pair<std::wstring, std::wstring>> choices = {
            {L"Automatic", L"Follow the language of Windows"}, {L"English", L"English"}, {L"한국어", L"Korean"}, {L"Español", L"Spanish"}};
        segmented(y, choices, chosen, [](int i) { command(ID_OPT_LANG + i); }, "language");
    }
    y += rowH;
    note(fx, y - scale(4), L"Applies at the next start");
    y += scale(20);
    ui::fieldLabel(mem, l, y, L"Accent color", false);
    {
        const int s = scale(20), top = y + (rowH - s) / 2;
        for (int i = 0; i < theme::kAccentCount; ++i) {
            const int x = fx + i * (s + scale(8));
            const RECT sw{x, top, x + s, top + s};
            const bool chosen = i == gAccentIndex, over = ui::hot(sw);
            if (chosen || over) {  // a ring around the chosen one (and the one under the mouse)
                RECT ring = sw;
                InflateRect(&ring, scale(3), scale(3));
                ui::box(mem, ring, CLR_NONE, chosen ? kText : kBorderHot);
            }
            ui::box(mem, sw, theme::swatch(i), CLR_NONE);
            hitsOut().push_back({sw, nullptr, [i] { applyTheme(gLightTheme, i), updateMenus(); },
                                 std::wstring(theme::kAccents[i].name) + (i == 0 ? L" (default)" : L""), IDC_HAND, {}});
            gOptions.parts["accent." + std::to_string(i)] = sw;
        }
        note(fx + theme::kAccentCount * (s + scale(8)) + scale(4), y + (rowH - scale(20)) / 2, theme::kAccents[gAccentIndex].name);
    }
    y += rowH;
    checkRow(y, L"Animate units", app.animate, ID_OPT_ANIMATE, "animate", L"Units on the map play their animations");
    y += rowH;
    y += volumeRow(mem, l.x0, y, l.labelW, std::min<int>(l.colRight, fx + scale(260)), hitsOut()) + pad;  // (an SFX entry has it too)

    // Unit names: a unit with a custom name (the map's unit settings), in every list that names it.
    y = ui::sectionHeader(mem, l, y, L"Unit names");
    {
        static const wchar_t* const kNames[4] = {L"Terran Marine", L"Terran Marine (Custom Name)", L"Custom Name (Terran Marine)", L"Custom Name"};
        ui::fieldLabel(mem, l, y, L"Custom names", false);
        ui::pickBox(mem, {fx, fieldTop(y), std::min<int>(l.colRight, fx + scale(300)), fieldTop(y) + fieldH}, kNames[gDock.unitNames], false,
                    [](RECT b) { pickUnitNames(b); }, L"How lists name a unit that has a custom name in the map");
        gOptions.parts["unitnames"] = {fx, fieldTop(y), fx + scale(300), fieldTop(y) + fieldH};
        y += rowH + pad;
    }

    // Map unit limit: placing, spraying and pasting units stop there (GitHub feedback #103). pickUnitLimit: main.cpp.
    y = ui::sectionHeader(mem, l, y, L"Map unit limit");
    for (int k = 0; k < 2; ++k) {
        int& limit = k == 0 ? gUnitLimitClassic : gUnitLimitEud;
        ui::fieldLabel(mem, l, y, k == 0 ? L"Standard maps" : L"EUD projects", false);
        const RECT box{fx, fieldTop(y), std::min<int>(l.colRight, fx + scale(120)), fieldTop(y) + fieldH};
        ui::pickBox(mem, box, std::to_wstring(limit) + L" units", false,
                    [hwnd, k](RECT b) { pickUnitLimit(k, b, hwnd); },
                    k == 0 ? L"Default value: 3400" : L"Default value: 1700");
        gOptions.parts[k == 0 ? "unitlimit.classic" : "unitlimit.eud"] = box;
        y += rowH;
    }
    note(l.x0, y - scale(4), L"StarCraft without mods takes at most 3400 units in a map.");
    y += scale(20) + pad;

    // Start.
    y = ui::sectionHeader(mem, l, y, L"Start");
    checkRow(y, L"Open the last project on start", gOpenLastProject, ID_OPT_LASTPROJECT, "lastproject");
    y += rowH;
    checkRow(y, L"Look for updates on start", gCheckUpdates, ID_OPT_UPDATECHECK, "updatecheck", L"Release builds only");
    {
        const int x = gOptions.parts["updatecheck"].right + scale(16);
        ui::smallButton(mem, x, y, rowH, L"Check now", [] { command(ID_HELP_UPDATES); }, L"Look for a newer release now");
    }
    y += rowH;
    checkRow(y, L"Send anonymous crash reports", gSendCrashReports, ID_OPT_CRASHREPORTS, "crashreports",
             L"The version, Windows, the video card, where it crashed and the last log lines; no maps");
    y += rowH + pad;

    // The working folder: where new projects go.
    y = ui::sectionHeader(mem, l, y, L"Working folder");
    {
        const std::wstring browse = L"Browse 26", def = L"Default";
        const int bw = ui::buttonWidth(mem, browse), dw = ui::buttonWidth(mem, def), gap = scale(ui::kButtonGap);
        const RECT field{l.x0, fieldTop(y), l.colRight - bw - dw - 2 * gap, fieldTop(y) + fieldH};
        ui::fieldBox(mem, field, ui::Field::ReadOnly);
        RECT t = field;
        InflateRect(&t, -scale(8), 0);
        SetTextColor(mem, kText);
        DrawTextW(mem, defaultProjectsFolder().make_preferred().wstring().c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_PATH_ELLIPSIS);
        const int by = y + (rowH - scale(ui::kButtonH)) / 2;
        ui::buttonsRight(mem, l.colRight, by,
                         {{browse, ui::Tone::Normal,
                           [hwnd] {
                               const std::wstring f = pickFolderTitled(L"The working folder: where new projects go");
                               if (f.empty()) return;
                               gProjectsFolder = fs::path(f).make_preferred().wstring();
                               saveRecent();
                               setMessage(L"New projects go to " + gProjectsFolder);
                               InvalidateRect(hwnd, nullptr, FALSE);
                           },
                           L"Select the folder where new projects go"},
                          {def, ui::Tone::Normal,
                           [hwnd] {
                               gProjectsFolder.clear();
                               saveRecent();
                               setMessage(L"New projects go to " + defaultProjectsFolder().make_preferred().wstring());
                               InvalidateRect(hwnd, nullptr, FALSE);
                           },
                           L"Documents\\StarDraft"}});
        gOptions.parts["workfolder"] = field;
        y += rowH;
        note(l.x0, y - scale(4), L"New projects go here. A project that you open stays where it is.");
        y += scale(20) + pad;
    }

    // StarCraft folder.
    y = ui::sectionHeader(mem, l, y, L"StarCraft folder");
    {
        const std::wstring browse = L"Browse\x2026";
        const int bw = ui::buttonWidth(mem, browse);
        const RECT field{l.x0, fieldTop(y), l.colRight - bw - scale(ui::kButtonGap), fieldTop(y) + fieldH};
        ui::fieldBox(mem, field, ui::Field::ReadOnly);
        RECT t = field;
        InflateRect(&t, -scale(8), 0);
        SetTextColor(mem, kText);
        DrawTextW(mem, (gInstallInUse.empty() ? std::wstring(L"\x2014") : gInstallInUse.wstring()).c_str(), -1, &t,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_PATH_ELLIPSIS);
        const int by = y + (rowH - scale(ui::kButtonH)) / 2;
        const RECT b{l.colRight - bw, by, l.colRight, by + scale(ui::kButtonH)};
        ui::button(mem, b, browse, ui::Tone::Normal, [] { command(ID_OPT_INSTALL); }, L"Select the StarCraft: Remastered folder");
        gOptions.parts["install"] = b;
        y += rowH;
        note(l.x0, y - scale(4),
             !gInstallNext.empty() && gInstallNext != gInstallInUse ? L"From the next start: " + gInstallNext.wstring()
                                                                    : std::wstring(L"A new folder is used from the next start"));
        y += scale(20) + pad;
    }

    gOptions.height = y + gOptions.scroll;
    // The parts under the footer cannot be clicked.
    std::erase_if(gOptions.surface.hits, [&](const DataWidgetHit& h) { return h.r.top >= contentBottom; });
    for (auto& h : gOptions.surface.hits) h.r.bottom = std::min<LONG>(h.r.bottom, contentBottom);

    // The footer: Close.
    SelectClipRgn(mem, nullptr);
    if (gOptions.height > contentBottom) fillAlpha(mem, {0, contentBottom, rc.right, contentBottom + 1}, kBorder, 255);  // the content scrolls under it
    ui::buttonsRight(mem, rc.right - pad, rc.bottom - pad - scale(ui::kButtonH), {{L"Close", ui::Tone::Primary, [hwnd] { DestroyWindow(hwnd); }}});
    if (!gOptions.surface.hits.empty()) gOptions.parts["close"] = gOptions.surface.hits.back().r;

    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, gOptions.height - 1), UINT(std::max(0, contentBottom)),
                  gOptions.scroll};
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gOptions.surface.end(hwnd);
}

void optionsScrollTo(HWND hwnd, int s) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int view = int(rc.bottom) - scale(2 * ui::kDialogPad + ui::kButtonH);
    s = std::clamp(s, 0, std::max(0, gOptions.height - view));
    if (s != gOptions.scroll) gOptions.scroll = s, InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK optionsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; gOptions.surface.message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_PAINT: optionsPaint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: optionsScrollTo(hwnd, gOptions.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(40) * 3 / WHEEL_DELTA); return 0;
    case WM_VSCROLL: optionsScrollTo(hwnd, scrollFromMessage(hwnd, wp, gOptions.scroll, scale(40))); return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        gOptions.wnd = nullptr, gOptions.scroll = 0, gOptions.shownLight = -1;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Options > Program options (one window; a second Ctrl+, brings it to the front).
void showOptions() {
    if (gOptions.wnd) {
        if (!gSelfTest && !gBackground) SetForegroundWindow(gOptions.wnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = optionsProc, wc.hInstance = app.inst, wc.lpszClassName = L"StarDraftOptions";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(app.inst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        registered = true;
    }
    // A dialog frame (a tool window in test runs); as high as the options need, or as the screen has room for.
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VSCROLL, ex = WS_EX_DLGMODALFRAME | (gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0);
    RECT mr{0, 0, 1280, 900};
    if (app.main) GetWindowRect(app.main, &mr);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromRect(&mr, MONITOR_DEFAULTTONEAREST), &mi);
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    RECT r{0, 0, scale(600), std::min(scale(460), workH - scale(80))};
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, UINT(app.dpi));
    const int w = r.right - r.left, h = r.bottom - r.top;
    int x = (mr.left + mr.right - w) / 2, y = std::max<int>(mi.rcWork.top, (mr.top + mr.bottom - h) / 2);
    if (gBackground) x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 100, y = 0;
    gOptions.wnd = CreateWindowExW(ex, L"StarDraftOptions", L"Program options", style, x, y, w, h, app.main, nullptr, app.inst, nullptr);
    if (!gOptions.wnd) return;
    cleanScroll(gOptions.wnd);
    darkControl(gOptions.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    ShowWindow(gOptions.wnd, gSelfTest || gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(gOptions.wnd);
}
