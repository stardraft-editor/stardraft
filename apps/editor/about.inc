// Help > About StarDraft: the app icon large, the version, and links to the GitHub repository and the Discord server.
// Included by main.cpp after ui_kit.inc. The content is a view (apps/common/view.h) in a view window (ui::kViewClass).

const wchar_t* const kDiscordInvite = L"https://discord.gg/JBMCwSRJBm";

// The About box's content: painted on the Canvas; the links open in the browser, the Close button and Esc or Enter
// close the window.
struct AboutBox final : stardraft::draw::View {
    struct Link {
        stardraft::draw::Rect rc;
        std::wstring url;
    };
    std::vector<Link> links;
    stardraft::draw::Rect close;
    int hot = -1;  // link index, or -2 for Close

    void paint(stardraft::draw::Canvas& c) override {
        using stardraft::draw::Color;
        using stardraft::draw::Font;
        using stardraft::draw::FontDesc;
        using stardraft::draw::Rect;
        using stardraft::draw::kCenter;
        using stardraft::draw::kMiddle;
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);

        // The icon, as large as the window allows (the .ico has a 256 px image).
        const int px = scale(160);
        int y = scale(28);
        c.resourceIcon({(w - px) / 2, y, (w - px) / 2 + px, y + px}, 1, px);
        y += px + scale(16);

        auto centered = [&](const std::wstring& t, Font f, Color col, int hgt) {
            const Font of = c.setFont(f);
            c.text({0, y, w, y + hgt}, t, col, kCenter | kMiddle);
            c.setFont(of);
            y += hgt;
        };
        LOGFONTW lf{};
        GetObjectW(app.font, sizeof lf, &lf);
        FontDesc title;  // the UI font at twice its height, semibold
        title.px = -lf.lfHeight * 2, title.weight = FW_SEMIBOLD;
        centered(L"StarDraft 2", c.font(title), kText, scale(40));
        const Font smallText = ui::fontOf(app.font);
        centered(L"v" + widen(STARDRAFT_VERSION_NAME) + (buildStamp().empty() ? L"" : L"  (" + buildStamp() + L")"), smallText, kDim, scale(22));
        centered(L"A map creation tool for EUD-powered maps for StarCraft: Remastered", smallText, kDim, scale(22));
        // The name: a tribute to StarDraft by Camelot Systems (1998), which started the StarCraft modding scene.
        centered(L"Named after StarDraft by Camelot Systems (1998), which started the StarCraft modding scene.", smallText, kDim, scale(22));
        centered(L"StarDraft 2 aims to be its spiritual successor for StarCraft makers in 2026 and later.", smallText, kDim, scale(22));
        // The links: underlined when the mouse is on them. First the EUD tools, as two links in one line of text.
        links.clear();
        {
            const std::wstring line = tr(std::wstring(L"StarDraft 2 is powered by euddraft and eudplib"));
            const std::wstring names[2] = {L"euddraft", L"eudplib"};
            const wchar_t* urls[2] = {L"https://github.com/armoha/euddraft", L"https://github.com/armoha/eudplib"};
            const size_t a = line.find(names[0]), b = line.find(names[1]);
            if (a == std::wstring::npos || b == std::wstring::npos || b < a + names[0].size()) {
                centered(line, smallText, kDim, scale(22));  // a translation without the names: the text only
            } else {
                struct Piece {
                    std::wstring text;
                    int link;  // -1: text
                };
                const Piece pieces[5] = {{line.substr(0, a), -1}, {names[0], 0}, {line.substr(a + names[0].size(), b - a - names[0].size()), -1},
                                         {names[1], 1}, {line.substr(b + names[1].size()), -1}};
                const size_t first = links.size();
                int total = 0;
                for (const Piece& p : pieces) {
                    if (p.text.empty()) continue;
                    const Font of = c.setFont(smallText);
                    total += c.measure(p.text);
                    c.setFont(of);
                }
                int x = (w - total) / 2;
                for (const Piece& p : pieces) {
                    if (p.text.empty()) continue;
                    FontDesc ud;
                    ud.underline = p.link >= 0 && hot == int(first) + p.link;
                    const Font of = c.setFont(c.font(ud));
                    const int tw = c.measure(p.text);
                    const Rect r{x, y, x + tw, y + scale(22)};
                    c.text(r, p.text, p.link >= 0 ? kAccent : kDim, kMiddle);
                    c.setFont(of);
                    if (p.link >= 0) links.push_back({r, urls[p.link]});
                    x += tw;
                }
                y += scale(22);
            }
        }
        y += scale(14);

        // Then the repository and the Discord server.
        const size_t first = links.size();
        links.push_back({{}, L"https://github.com/" + std::wstring(crash::kRepo)});
        links.push_back({{}, kDiscordInvite});
        const wchar_t* labels[2] = {L"GitHub: github.com/stardraft-editor/stardraft", L"Join the Discord server"};
        for (size_t k = 0; k < 2; ++k) {
            const size_t i = first + k;
            FontDesc ud;
            ud.underline = hot == int(i);
            const Font of = c.setFont(c.font(ud));
            const int tw = c.measure(labels[k]);
            const Rect r{(w - tw) / 2, y, (w + tw) / 2, y + scale(24)};
            c.text(r, labels[k], kAccent, kCenter | kMiddle);
            c.setFont(of);
            links[i].rc = r;
            y += scale(26);
        }

        // Close.
        const int bw = scale(96), bh = scale(ui::kButtonH), pad = scale(ui::kDialogPad);
        close = {(w - bw) / 2, h - bh - pad, (w + bw) / 2, h - pad};
        ui::buttonFace(c, ui::toRECT(close), L"Close", ui::Tone::Primary, hot == -2 ? ui::State::Hot : ui::State::Normal, app.font);
    }

    int hitAt(stardraft::draw::Point p) const {
        for (size_t i = 0; i < links.size(); ++i)
            if (links[i].rc.contains(p)) return int(i);
        return close.contains(p) ? -2 : -1;
    }
    void setHot(int h) {
        if (h == hot) return;
        hot = h;
        invalidate();
    }
    void closeBox() {
        if (HWND w = ui::Win32Host::windowOf(*this)) DestroyWindow(w);
    }

    bool mouse(const stardraft::draw::MouseEvent& e) override {
        using stardraft::draw::MouseKind;
        switch (e.kind) {
        case MouseKind::Move: setHot(hitAt(e.pt)); return true;
        case MouseKind::Leave: setHot(-1); return true;
        case MouseKind::Up: {
            if (e.button != 0) return false;
            const int h = hitAt(e.pt);
            if (h >= 0) aboutOpen(links[size_t(h)].url);
            else if (h == -2) closeBox();
            return true;
        }
        default: return false;
        }
    }
    bool key(const stardraft::draw::KeyEvent& e) override {
        using stardraft::draw::Key;
        if (!e.down || (e.mods & stardraft::draw::kAlt)) return false;
        if (e.key != Key::Escape && e.key != Key::Enter) return false;
        closeBox();
        return true;
    }
    stardraft::draw::Cursor cursor(stardraft::draw::Point) override { return hot != -1 ? stardraft::draw::Cursor::Hand : stardraft::draw::Cursor::Arrow; }
};

// The About window and its content (one window at a time).
struct AboutWindow {
    HWND wnd = nullptr;
    AboutBox view;
} gAbout;

void aboutOpen(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

// Help > About StarDraft (one window; a second click brings it to the front).
void showAbout() {
    if (gAbout.wnd && IsWindow(gAbout.wnd)) {
        SetForegroundWindow(gAbout.wnd);
        return;
    }
    gAbout.view.hot = -1;
    // The same window as the other dialogs (a dialog frame; a tool window in test runs).
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP, ex = WS_EX_DLGMODALFRAME | (gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0);
    RECT r{0, 0, scale(600), scale(518)};  // (the lines about the name and the EUD tools fit)
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, UINT(app.dpi));
    RECT mr;
    GetWindowRect(app.main, &mr);
    const int w = r.right - r.left, h = r.bottom - r.top;
    gAbout.wnd = CreateWindowExW(ex, ui::kViewClass, L"About StarDraft 2", style, (mr.left + mr.right - w) / 2, (mr.top + mr.bottom - h) / 2, w, h,
                                 app.main, nullptr, app.inst, &gAbout.view);
    if (!gAbout.wnd) return;
    const BOOL dark = !gLightTheme;
    DwmSetWindowAttribute(gAbout.wnd, 20, &dark, sizeof dark), titleColors(gAbout.wnd);
    // The program's icon in the title bar (the view class has none).
    static HICON smallIcon = nullptr;
    if (!smallIcon) smallIcon = HICON(LoadImageW(app.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    SendMessageW(gAbout.wnd, WM_SETICON, ICON_BIG, LPARAM(LoadIconW(app.inst, MAKEINTRESOURCEW(1))));
    SendMessageW(gAbout.wnd, WM_SETICON, ICON_SMALL, LPARAM(smallIcon));
    ShowWindow(gAbout.wnd, gSelfTest || gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(gAbout.wnd);
}
