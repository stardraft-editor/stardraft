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
    centered(L"StarDraft 2", big, kText, scale(40));
    DeleteObject(big);
    centered(L"v" + widen(STARDRAFT_VERSION_NAME) + (buildStamp().empty() ? L"" : L"  (" + buildStamp() + L")"), app.font, kDim, scale(22));
    centered(L"A map creation tool for EUD-powered maps for StarCraft: Remastered", app.font, kDim, scale(22));
    // The name: a tribute to StarDraft by Camelot Systems (1998), which started the StarCraft modding scene.
    centered(L"Named after StarDraft by Camelot Systems (1998), which started the StarCraft modding scene.", app.font, kDim, scale(22));
    centered(L"StarDraft 2 aims to be its spiritual successor for StarCraft makers in 2026 and later.", app.font, kDim, scale(22));

    // The links: underlined when the mouse is on them. First the EUD tools, as two links in one line of text.
    gAbout.links.clear();
    {
        const std::wstring line = tr(std::wstring(L"StarDraft 2 is powered by euddraft and eudplib"));
        const std::wstring names[2] = {L"euddraft", L"eudplib"};
        const wchar_t* urls[2] = {L"https://github.com/armoha/euddraft", L"https://github.com/armoha/eudplib"};
        const size_t a = line.find(names[0]), b = line.find(names[1]);
        if (a == std::wstring::npos || b == std::wstring::npos || b < a + names[0].size()) {
            centered(line, app.font, kDim, scale(22));  // a translation without the names: the text only
        } else {
            struct Piece {
                std::wstring text;
                int link;  // -1: text
            };
            const Piece pieces[5] = {{line.substr(0, a), -1}, {names[0], 0}, {line.substr(a + names[0].size(), b - a - names[0].size()), -1},
                                     {names[1], 1}, {line.substr(b + names[1].size()), -1}};
            const size_t first = gAbout.links.size();
            HGDIOBJ of = SelectObject(mem, app.font);
            int total = 0;
            SIZE sz{};
            for (const Piece& p : pieces)
                if (!p.text.empty()) GetTextExtentPoint32W(mem, p.text.c_str(), int(p.text.size()), &sz), total += sz.cx;
            int x = (rc.right - total) / 2;
            for (const Piece& p : pieces) {
                if (p.text.empty()) continue;
                const bool hot = p.link >= 0 && gAbout.hot == int(first) + p.link;
                LOGFONTW lu = {};
                GetObjectW(app.font, sizeof lu, &lu);
                lu.lfUnderline = hot;
                HFONT f = CreateFontIndirectW(&lu);
                SelectObject(mem, f);
                GetTextExtentPoint32W(mem, p.text.c_str(), int(p.text.size()), &sz);
                RECT r{x, y, x + sz.cx, y + scale(22)};
                SetTextColor(mem, p.link >= 0 ? kAccent : kDim);
                DrawTextW(mem, p.text.c_str(), int(p.text.size()), &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                SelectObject(mem, app.font);
                DeleteObject(f);
                if (p.link >= 0) gAbout.links.push_back({r, urls[p.link]});
                x += sz.cx;
            }
            SelectObject(mem, of);
            y += scale(22);
        }
    }
    // Not a Blizzard product, and it needs the game's files.
    y += scale(10);
    centered(tr(L"StarDraft 2 is not associated with Blizzard Entertainment."), app.font, kDim, scale(22));
    centered(tr(L"It needs an installation of StarCraft to work."), app.font, kDim, scale(22));
    y += scale(14);

    // Then the repository and the Discord server.
    const size_t first = gAbout.links.size();
    gAbout.links.push_back({{}, L"https://github.com/" + std::wstring(crash::kRepo)});
    gAbout.links.push_back({{}, kDiscordInvite});
    const wchar_t* labels[2] = {L"GitHub: github.com/stardraft-editor/stardraft", L"Join the Discord server"};
    for (size_t k = 0; k < 2; ++k) {
        const size_t i = first + k;
        LOGFONTW lu = {};
        GetObjectW(app.font, sizeof lu, &lu);
        lu.lfUnderline = gAbout.hot == int(i);
        HFONT f = CreateFontIndirectW(&lu);
        HGDIOBJ of = SelectObject(mem, f);
        SIZE sz{};
        GetTextExtentPoint32W(mem, labels[k], int(wcslen(labels[k])), &sz);
        const RECT r{(rc.right - sz.cx) / 2, y, (rc.right + sz.cx) / 2, y + scale(24)};
        SetTextColor(mem, kAccent);
        RECT tr = r;
        DrawTextW(mem, labels[k], -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
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
    RECT r{0, 0, scale(600), scale(552)};  // (the lines about the name, the EUD tools and Blizzard fit)
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, UINT(app.dpi));
    RECT mr;
    GetWindowRect(app.main, &mr);
    const int w = r.right - r.left, h = r.bottom - r.top;
    gAbout.wnd = CreateWindowExW(ex, L"StarDraftAbout", L"About StarDraft 2", style, (mr.left + mr.right - w) / 2, (mr.top + mr.bottom - h) / 2, w, h,
                                 app.main, nullptr, app.inst, nullptr);
    ShowWindow(gAbout.wnd, gSelfTest || gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(gAbout.wnd);
}
