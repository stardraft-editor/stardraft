// Help > About StarDraft: the app icon large, the version, and links to the GitHub repository and the Discord server.
// Included by main.cpp after ui_kit.inc.

const wchar_t* const kDiscordInvite = L"https://discord.gg/JBMCwSRJBm";

struct AboutWindow {
    HWND wnd = nullptr;
    HICON icon = nullptr;
    int iconPx = 0;
    struct Link {
        RECT rc;
        std::wstring url;
    };
    std::vector<Link> links;
    RECT close{};
    int hot = -1;  // link index, or -2 for Close
} gAbout;

void aboutOpen(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void aboutPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(kPanel);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    // The icon, as large as the window allows (the .ico has a 256 px image).
    const int px = scale(160);
    if (!gAbout.icon || gAbout.iconPx != px) {
        if (gAbout.icon) DestroyIcon(gAbout.icon);
        gAbout.icon = HICON(LoadImageW(app.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, px, px, LR_DEFAULTCOLOR));
        gAbout.iconPx = px;
    }
    int y = scale(28);
    if (gAbout.icon) DrawIconEx(mem, (rc.right - px) / 2, y, gAbout.icon, px, px, 0, nullptr, DI_NORMAL);
    y += px + scale(16);

    auto centered = [&](const std::wstring& t, HFONT f, COLORREF c, int h) {
        HGDIOBJ of = SelectObject(mem, f);
        SetTextColor(mem, c);
        RECT r{0, y, rc.right, y + h};
        DrawTextW(mem, t.c_str(), -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(mem, of);
        y += h;
    };
    LOGFONTW lf{};
    GetObjectW(app.font, sizeof lf, &lf);
    lf.lfHeight = lf.lfHeight * 2, lf.lfWeight = FW_SEMIBOLD;
    HFONT big = CreateFontIndirectW(&lf);
    centered(L"StarDraft", big, kText, scale(40));
    DeleteObject(big);
    centered(widen(STARDRAFT_VERSION_NAME) + (buildStamp().empty() ? L"" : L"  (" + buildStamp() + L")"), app.font, kDim, scale(22));
    centered(L"A map creation tool for EUD-powered maps for StarCraft: Remastered", app.font, kDim, scale(22));
    centered(L"Named in tribute to the original StarDraft by Camelot Systems.", app.font, kDim, scale(22));
    y += scale(14);

    // The links: underlined when the mouse is on them.
    gAbout.links = {{{}, L"https://github.com/" + std::wstring(crash::kRepo)}, {{}, kDiscordInvite}};
    const wchar_t* labels[2] = {L"GitHub: github.com/stardraft-editor/stardraft", L"Join the Discord server"};
    for (size_t i = 0; i < gAbout.links.size(); ++i) {
        LOGFONTW lu = {};
        GetObjectW(app.font, sizeof lu, &lu);
        lu.lfUnderline = gAbout.hot == int(i);
        HFONT f = CreateFontIndirectW(&lu);
        HGDIOBJ of = SelectObject(mem, f);
        SIZE sz{};
        GetTextExtentPoint32W(mem, labels[i], int(wcslen(labels[i])), &sz);
        const RECT r{(rc.right - sz.cx) / 2, y, (rc.right + sz.cx) / 2, y + scale(24)};
        SetTextColor(mem, kAccent);
        RECT tr = r;
        DrawTextW(mem, labels[i], -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(mem, of);
        DeleteObject(f);
        gAbout.links[i].rc = r;
        y += scale(26);
    }

    // Close.
    const int bw = scale(96), bh = scale(ui::kButtonH), pad = scale(ui::kDialogPad);
    gAbout.close = {(rc.right - bw) / 2, rc.bottom - bh - pad, (rc.right + bw) / 2, rc.bottom - pad};
    ui::buttonFace(mem, gAbout.close, L"Close", ui::Tone::Primary, gAbout.hot == -2 ? ui::State::Hot : ui::State::Normal, app.font);

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

int aboutHit(POINT p) {
    for (size_t i = 0; i < gAbout.links.size(); ++i)
        if (PtInRect(&gAbout.links[i].rc, p)) return int(i);
    return PtInRect(&gAbout.close, p) ? -2 : -1;
}

LRESULT CALLBACK aboutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark), titleColors(hwnd);
        return 0;
    }
    case WM_PAINT: aboutPaint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        const int h = aboutHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (h != gAbout.hot) gAbout.hot = h, InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (gAbout.hot != -1) gAbout.hot = -1, InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && gAbout.hot != -1) return SetCursor(LoadCursor(nullptr, IDC_HAND)), TRUE;
        break;
    case WM_LBUTTONUP: {
        const int h = aboutHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (h >= 0) aboutOpen(gAbout.links[size_t(h)].url);
        else if (h == -2) DestroyWindow(hwnd);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        gAbout.wnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Help > About StarDraft (one window; a second click brings it to the front).
void showAbout() {
    if (gAbout.wnd) {
        SetForegroundWindow(gAbout.wnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = aboutProc, wc.hInstance = app.inst, wc.lpszClassName = L"StarDraftAbout";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(app.inst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        registered = true;
    }
    // The same window as the other dialogs (a dialog frame; a tool window in test runs).
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP, ex = WS_EX_DLGMODALFRAME | (gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0);
    RECT r{0, 0, scale(440), scale(452)};
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, UINT(app.dpi));
    RECT mr;
    GetWindowRect(app.main, &mr);
    const int w = r.right - r.left, h = r.bottom - r.top;
    gAbout.wnd = CreateWindowExW(ex, L"StarDraftAbout", L"About StarDraft", style, (mr.left + mr.right - w) / 2, (mr.top + mr.bottom - h) / 2, w, h,
                                 app.main, nullptr, app.inst, nullptr);
    ShowWindow(gAbout.wnd, gSelfTest || gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(gAbout.wnd);
}
