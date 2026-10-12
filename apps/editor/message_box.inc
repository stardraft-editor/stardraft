// The message box: questions, errors and notes in the theme (MessageBoxW is always light). Included by main.cpp
// after ui_kit.inc; main.cpp declares themedMessageBox() near the top, so the code before this file can use it.
//
// themedMessageBox() is a drop-in for MessageBoxW: the same flags (MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL,
// MB_RETRYCANCEL; MB_ICONERROR, MB_ICONWARNING, MB_ICONQUESTION, MB_ICONINFORMATION; MB_DEFBUTTON2 and 3) and the
// same return values (IDOK, IDCANCEL, IDYES, IDNO, IDRETRY). `names` gives the buttons other names (verbs: "Save",
// "Don't save", "Cancel"), from left to right; the return values do not change.
//
// The look: the kit's colors and font, 16 px padding, the text wrapped at 420 px, the icon in the theme colors, the
// kit's buttons at the bottom right. The button with the focus is Primary (first: the default button). Enter
// presses it, Esc presses Cancel (or No, or OK when it is the only button), Tab and the arrow keys move the focus,
// and the letter after "&" in a button name (with or without Alt) presses that button.
//
// The only native message box left is the crash handler's (crash.inc): the app may be in a bad state there.

void (*gMsgBoxHook)(HWND) = nullptr;  // datashots and self-tests: called when the box shows (capture it, press a key)

namespace msgbox {

struct Button {
    std::wstring name;  // with "&" before the access key
    int id = IDOK;
    RECT rc{};
};
struct Box {
    std::wstring text;
    UINT flags = 0;
    std::vector<Button> buttons;
    int focus = 0;      // the button with the focus (Primary; Enter presses it)
    int hot = -1;       // under the mouse
    int down = -1;      // pressed with the mouse
    int cancel = -1;    // what Esc and the close box press
    int result = 0;
    bool done = false;
    RECT textRc{}, iconRc{};
};

// The icon for the MB_ICON* flags: an icon of the set (or a Segoe MDL2 glyph, for the information icon) and its color.
bool iconFor(UINT flags, ui::Ico& ico, COLORREF& c) {
    switch (flags & MB_ICONMASK) {
    case MB_ICONERROR: ico = UiIcon::Error, c = kBad; return true;
    case MB_ICONWARNING: ico = UiIcon::Warning, c = kWarn; return true;
    case MB_ICONQUESTION: ico = UiIcon::Help, c = kAccent; return true;
    case MB_ICONINFORMATION: ico = L"\xE946", c = kAccent; return true;  // Info
    }
    return false;
}

// The access key of a button name ("Do&n't save": n), lower case; 0: none.
wchar_t accessKey(const std::wstring& name) {
    for (size_t i = 0; i + 1 < name.size(); ++i)
        if (name[i] == L'&') {
            if (name[i + 1] == L'&') {
                ++i;
                continue;
            }
            return wchar_t(towlower(name[i + 1]));
        }
    return 0;
}

HFONT font() { return app.font ? app.font : HFONT(GetStockObject(DEFAULT_GUI_FONT)); }

void press(HWND hwnd, Box& b, int k) {
    if (b.done || k < 0 || k >= int(b.buttons.size())) return;
    b.result = b.buttons[size_t(k)].id, b.done = true;
    // The owner gets the activation back before the box goes (as MessageBoxW does it).
    if (HWND owner = GetWindow(hwnd, GW_OWNER)) EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
}

int hitButton(const Box& b, POINT p) {
    for (size_t k = 0; k < b.buttons.size(); ++k)
        if (PtInRect(&b.buttons[k].rc, p)) return int(k);
    return -1;
}

void paint(HWND hwnd, Box& b) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(dc, bmp);
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, font());

    ui::Ico ico = L"";
    COLORREF ic = kText;
    if (iconFor(b.flags, ico, ic)) {
        if (ico.icon >= 0) uiIcon(dc, b.iconRc, UiIcon(ico.icon), ic, scale(32));
        else ui::glyph(dc, b.iconRc, ico, ic, 26);
    }
    SetTextColor(dc, kText);
    RECT tr = b.textRc;
    DrawTextW(dc, b.text.c_str(), -1, &tr, DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);

    // The buttons: the kit's face, then the name with its access key underlined.
    for (size_t k = 0; k < b.buttons.size(); ++k) {
        const Button& bt = b.buttons[k];
        const ui::Tone tone = int(k) == b.focus ? ui::Tone::Primary : ui::Tone::Normal;
        const ui::State s = int(k) == b.down && b.down == b.hot ? ui::State::Down : int(k) == b.hot ? ui::State::Hot : ui::State::Normal;
        ui::buttonFace(dc, bt.rc, L"", tone, s, font());
        SetTextColor(dc, ui::toneText(tone));
        RECT r = bt.rc;
        // The access letters underlined only while Alt is down (as Windows shows them).
        DrawTextW(dc, bt.name.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | (GetKeyState(VK_MENU) < 0 ? 0 : DT_HIDEPREFIX));
    }
    SelectObject(dc, of);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

// Places the text, the icon and the buttons; returns the client size.
SIZE layout(HWND hwnd, Box& b) {
    const int pad = scale(ui::kDialogPad), wrap = scale(420), bh = scale(ui::kButtonH), gap = scale(ui::kButtonGap);
    HDC dc = GetDC(hwnd);
    HGDIOBJ of = SelectObject(dc, font());
    RECT t{0, 0, wrap, 0};
    DrawTextW(dc, b.text.c_str(), -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    int bw = 0;
    std::vector<int> widths;
    for (const Button& bt : b.buttons) {
        RECT r{};
        DrawTextW(dc, bt.name.c_str(), -1, &r, DT_CALCRECT | DT_SINGLELINE);
        widths.push_back(std::max(scale(ui::kButtonMinW), int(r.right - r.left) + scale(28)));
        bw += widths.back() + (bw ? gap : 0);
    }
    SelectObject(dc, of);
    ReleaseDC(hwnd, dc);

    ui::Ico ico = L"";
    COLORREF ic;
    const bool icon = iconFor(b.flags, ico, ic);
    const int iconW = icon ? scale(32) : 0, iconGap = icon ? scale(12) : 0;
    const int textW = std::min<int>(wrap, t.right - t.left), textH = t.bottom - t.top;
    const int contentH = std::max(textH, iconW);
    const int w = std::max(pad + iconW + iconGap + textW + pad, pad + bw + pad);
    // The text is beside the icon, centered on it when it is one or two lines.
    const int ty = pad + (contentH - textH) / 2;
    b.iconRc = {pad, pad, pad + iconW, pad + iconW};
    b.textRc = {pad + iconW + iconGap, ty, pad + iconW + iconGap + textW, ty + textH};
    const int by = pad + contentH + scale(24);
    int x = w - pad;
    for (size_t k = b.buttons.size(); k-- > 0;) {
        b.buttons[k].rc = {x - widths[k], by, x, by + bh};
        x -= widths[k] + gap;
    }
    return {w, by + bh + pad};
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Box* b = reinterpret_cast<Box*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_CREATE: {
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark), titleColors(hwnd);
        return 0;
    }
    case WM_PAINT:
        if (!b) break;
        paint(hwnd, *b);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        if (!b) break;
        const int h = hitButton(*b, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (h != b->hot) b->hot = h, InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (b && b->hot != -1) b->hot = -1, InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (b && LOWORD(lp) == HTCLIENT && b->hot != -1) return SetCursor(LoadCursor(nullptr, IDC_HAND)), TRUE;
        break;
    case WM_LBUTTONDOWN:
        if (!b) break;
        b->down = hitButton(*b, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (b->down >= 0) SetCapture(hwnd), b->focus = b->down, InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        if (!b) break;
        const int d = b->down;
        b->down = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (d >= 0 && hitButton(*b, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == d) press(hwnd, *b, d);
        else InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYUP:
        if (wp == VK_MENU) InvalidateRect(hwnd, nullptr, FALSE);  // the access letters show or go
        break;
    case WM_KEYDOWN: {
        if (!b) break;
        const int n = int(b->buttons.size());
        if (wp == VK_RETURN || wp == VK_SPACE) press(hwnd, *b, b->focus);
        else if (wp == VK_ESCAPE) press(hwnd, *b, b->cancel);
        else if (wp == VK_TAB || wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN) {
            const bool back = wp == VK_LEFT || wp == VK_UP || (wp == VK_TAB && GetKeyState(VK_SHIFT) < 0);
            b->focus = (b->focus + (back ? n - 1 : 1)) % n;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_CHAR:
    case WM_SYSCHAR: {
        if (!b) break;
        const wchar_t c = wchar_t(towlower(wchar_t(wp)));
        for (size_t k = 0; k < b->buttons.size(); ++k)
            if (c && accessKey(b->buttons[k].name) == c) return press(hwnd, *b, int(k)), 0;
        if (msg == WM_SYSCHAR) break;  // Alt+Space: the window menu
        return 0;
    }
    case WM_CLOSE:
        if (b) press(hwnd, *b, b->cancel);
        return 0;
    case WM_DPICHANGED:
    case WM_THEMECHANGED: InvalidateRect(hwnd, nullptr, FALSE); break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace msgbox

int themedMessageBox(HWND owner, const std::wstring& text, const std::wstring& caption, UINT flags, const std::vector<std::wstring>& names) {
    using msgbox::Box;
    Box b;
    b.text = text, b.flags = flags;
    switch (flags & MB_TYPEMASK) {
    case MB_OKCANCEL: b.buttons = {{L"OK", IDOK}, {L"Cancel", IDCANCEL}}, b.cancel = 1; break;
    case MB_YESNO: b.buttons = {{L"&Yes", IDYES}, {L"&No", IDNO}}, b.cancel = 1; break;
    case MB_YESNOCANCEL: b.buttons = {{L"&Yes", IDYES}, {L"&No", IDNO}, {L"Cancel", IDCANCEL}}, b.cancel = 2; break;
    case MB_RETRYCANCEL: b.buttons = {{L"&Retry", IDRETRY}, {L"Cancel", IDCANCEL}}, b.cancel = 1; break;
    default: b.buttons = {{L"OK", IDOK}}, b.cancel = 0; break;
    }
    for (size_t k = 0; k < names.size() && k < b.buttons.size(); ++k)
        if (!names[k].empty()) b.buttons[k].name = names[k];
    const UINT def = flags & MB_DEFMASK;
    b.focus = std::min<int>(int(b.buttons.size()) - 1, def == MB_DEFBUTTON2 ? 1 : def == MB_DEFBUTTON3 ? 2 : def == MB_DEFBUTTON4 ? 3 : 0);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = msgbox::proc, wc.hInstance = app.inst, wc.lpszClassName = L"StarDraftMessageBox";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }
    // The owner is the top window (a box from a child window disables its top window, as MessageBoxW does).
    if (owner) owner = GetAncestor(owner, GA_ROOT);
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP, ex = WS_EX_DLGMODALFRAME | (gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0);
    HWND wnd = CreateWindowExW(ex, L"StarDraftMessageBox", caption.c_str(), style, 0, 0, 100, 100, owner, nullptr, app.inst, &b);
    if (!wnd) return MessageBoxW(owner, text.c_str(), caption.c_str(), flags);  // no window: the native one
    // The size for the text and the buttons, centered on the owner (on the screen when the owner does not show).
    const SIZE sz = msgbox::layout(wnd, b);
    RECT r{0, 0, sz.cx, sz.cy};
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, UINT(app.dpi ? app.dpi : 96));
    const int w = r.right - r.left, h = r.bottom - r.top;
    RECT on{};
    if (!owner || !IsWindowVisible(owner) || IsIconic(owner) || !GetWindowRect(owner, &on)) {
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &mi);
        on = mi.rcWork;
    }
    SetWindowPos(wnd, nullptr, (on.left + on.right - w) / 2, (on.top + on.bottom - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    const bool ownerWasEnabled = owner && IsWindowEnabled(owner);
    if (ownerWasEnabled) EnableWindow(owner, FALSE);
    ShowWindow(wnd, gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(wnd);
    if (!gBackground) SetForegroundWindow(wnd), SetFocus(wnd);
    if (gMsgBoxHook) gMsgBoxHook(wnd);
    MSG msg;
    bool quit = false;
    while (IsWindow(wnd) && !b.done) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {
            quit = true;
            break;
        }
        if (got < 0) break;
        TranslateMessage(&msg), DispatchMessageW(&msg);
    }
    if (IsWindow(wnd)) {
        if (ownerWasEnabled) EnableWindow(owner, TRUE);
        DestroyWindow(wnd);
    }
    if (ownerWasEnabled) {
        EnableWindow(owner, TRUE);
        if (!gBackground) SetForegroundWindow(owner);
    }
    if (quit) PostQuitMessage(int(msg.wParam));  // the app closes: the main loop gets it
    return b.done ? b.result : b.buttons[size_t(b.cancel)].id;
}
