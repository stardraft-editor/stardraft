// The message box: questions, errors and notes in the theme (MessageBoxW is always light). Included by main.cpp
// after ui_kit.inc and view_win32.inc; main.cpp declares themedMessageBox() near the top, so the code before this file can use it.
//
// themedMessageBox() is a drop-in for MessageBoxW: the same flags (MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL,
// MB_RETRYCANCEL; MB_ICONERROR, MB_ICONWARNING, MB_ICONQUESTION, MB_ICONINFORMATION; MB_DEFBUTTON2 and 3) and the
// same return values (IDOK, IDCANCEL, IDYES, IDNO, IDRETRY). `names` gives the buttons other names (verbs: "Save",
// "Don't save", "Cancel"), from left to right; the return values do not change.
//
// The look: the kit's colors and font, 16 px padding, the text wrapped at 420 px, the icon in the theme colors, the
// kit's buttons at the bottom right. The button with the focus is Primary (first: the default button). Enter
// presses it, Esc presses Cancel (or No, or OK when it is the only button), Tab and the arrow keys move the focus,
// and the letter after "&" in a button name presses that button (with Alt, the letters are underlined while Alt is down).
//
// The box is a view (apps/common/view.h) in a view window: paint() draws the icon, the text and the buttons on the
// Canvas, and its mouse, key, text and cursor events do what the buttons do. The window is the box's own: its size,
// its place over the owner, the owner's disabling and the modal loop below.
//
// The only native message box left is the crash handler's (crash.inc): the app may be in a bad state there.

void (*gMsgBoxHook)(HWND) = nullptr;  // datashots and self-tests: called when the box shows (capture it, press a key)

namespace msgbox {

using stardraft::draw::Canvas;
using stardraft::draw::Cursor;
using stardraft::draw::KeyEvent;
using stardraft::draw::Key;
using stardraft::draw::kCenter;
using stardraft::draw::kMiddle;
using stardraft::draw::kPrefix;
using stardraft::draw::kPrefixHidden;
using stardraft::draw::kWrap;
using stardraft::draw::MouseEvent;
using stardraft::draw::MouseKind;
using stardraft::draw::Point;
using stardraft::draw::TextEvent;

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

class MessageView final : public stardraft::draw::View {
public:
    Box b;
    std::function<void()> ownerBack;  // the owner's enabled state: back on when the box ends

    // Places the text, the icon and the buttons for the Canvas's font; returns the client size.
    SIZE layout(Canvas& c) {
        const int pad = scale(ui::kDialogPad), wrap = scale(420), bh = scale(ui::kButtonH), gap = scale(ui::kButtonGap);
        const stardraft::draw::Font of = c.setFont(c.font({}));
        const Point t = c.measureBox(b.text, wrap, kWrap);
        int bw = 0;
        std::vector<int> widths;
        for (const Button& bt : b.buttons) {
            widths.push_back(std::max(scale(ui::kButtonMinW), c.measureBox(bt.name, 0, kPrefixHidden).x + scale(28)));
            bw += widths.back() + (bw ? gap : 0);
        }
        c.setFont(of);

        ui::Ico ico = L"";
        COLORREF ic = kText;
        const bool icon = iconFor(b.flags, ico, ic);
        const int iconW = icon ? scale(32) : 0, iconGap = icon ? scale(12) : 0;
        const int textW = std::min<int>(wrap, t.x), textH = t.y;
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

    void paint(Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        const bool alt = host() && (host()->modifiers() & stardraft::draw::kAlt) != 0;
        ui::Ico ico = L"";
        COLORREF ic = kText;
        if (iconFor(b.flags, ico, ic)) {
            if (ico.icon >= 0) c.icon(b.iconRc, ico.icon, ic, scale(32));
            else ui::glyph(c, b.iconRc, ico, ic, 26);
        }
        c.text(b.textRc, b.text, kText, kWrap);
        // The buttons: the kit's face, then the name with its access key underlined while Alt is down.
        for (size_t k = 0; k < b.buttons.size(); ++k) {
            const Button& bt = b.buttons[k];
            const ui::Tone tone = int(k) == b.focus ? ui::Tone::Primary : ui::Tone::Normal;
            const ui::State s = int(k) == b.down && b.down == b.hot ? ui::State::Down : int(k) == b.hot ? ui::State::Hot : ui::State::Normal;
            ui::buttonFace(c, bt.rc, L"", tone, s);
            c.text(bt.rc, bt.name, ui::toneText(tone), kCenter | kMiddle | (alt ? kPrefix : kPrefixHidden));
        }
    }

    bool mouse(const MouseEvent& e) override {
        switch (e.kind) {
        case MouseKind::Move: {
            const int h = hitButton(e.pt);
            if (h != b.hot) b.hot = h, invalidate();
            return true;
        }
        case MouseKind::Leave:
            if (b.hot != -1) b.hot = -1, invalidate();
            return true;
        case MouseKind::Down:
        case MouseKind::DoubleClick:  // (the window class has CS_DBLCLKS: a second click is this)
            if (e.button != 0) return false;
            b.down = hitButton(e.pt);
            if (b.down >= 0) host()->capture(*this, true), b.focus = b.down, invalidate();
            return true;
        case MouseKind::Up: {
            if (e.button != 0) return false;
            const int d = b.down;
            b.down = -1;
            if (host()) host()->capture(*this, false);
            if (d >= 0 && hitButton(e.pt) == d) press(d);
            else invalidate();
            return true;
        }
        default: return false;
        }
    }

    bool key(const KeyEvent& e) override {
        if (!e.down || b.done) return false;
        if (e.mods & stardraft::draw::kAlt) {  // Alt+letter: the access key (Windows sends it as a system key)
            const int k = int(e.key);
            if (!((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'))) return false;
            return pressAccess(wchar_t(towlower(wchar_t(k))));
        }
        const int n = int(b.buttons.size());
        switch (e.key) {
        case Key::Enter:
        case Key::Space: press(b.focus); return true;
        case Key::Escape: press(b.cancel); return true;
        case Key::Tab:
        case Key::Left:
        case Key::Right:
        case Key::Up:
        case Key::Down: {
            const bool back = e.key == Key::Left || e.key == Key::Up || (e.key == Key::Tab && (e.mods & stardraft::draw::kShift) != 0);
            b.focus = (b.focus + (back ? n - 1 : 1)) % n;
            invalidate();
            return true;
        }
        default: return false;  // (letters come as text)
        }
    }

    bool text(const TextEvent& e) override {
        const std::wstring w = widen(e.utf8);
        if (w.size() == 1) pressAccess(wchar_t(towlower(w[0])));
        return true;
    }

    Cursor cursor(Point) override { return b.hot != -1 ? Cursor::Hand : Cursor::Arrow; }

    // The system ends the box (its close box, Alt+F4): Cancel. The window goes after this.
    void closedBySystem() { choose(b.cancel); }

private:
    int hitButton(Point p) const {
        for (size_t k = 0; k < b.buttons.size(); ++k)
            if (PtInRect(&b.buttons[k].rc, POINT{p.x, p.y})) return int(k);
        return -1;
    }
    // Ends the box with button k; false when it is ended already or k is no button.
    bool choose(int k) {
        if (b.done || k < 0 || k >= int(b.buttons.size())) return false;
        b.result = b.buttons[size_t(k)].id, b.done = true;
        if (ownerBack) ownerBack();  // the owner gets the activation back before the box goes (as MessageBoxW does it)
        return true;
    }
    void press(int k) {
        if (choose(k) && host()) host()->closeWindow(*this);
    }
    bool pressAccess(wchar_t c) {
        if (!c || b.done) return false;
        for (size_t k = 0; k < b.buttons.size(); ++k)
            if (accessKey(b.buttons[k].name) == c) {
                press(int(k));
                return true;
            }
        return false;
    }
};

}  // namespace msgbox

int themedMessageBox(HWND owner, const std::wstring& text, const std::wstring& caption, UINT flags, const std::vector<std::wstring>& names) {
    msgbox::MessageView view;
    msgbox::Box& b = view.b;
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

    // The owner is the top window (a box from a child window disables its top window, as MessageBoxW does).
    if (owner) owner = GetAncestor(owner, GA_ROOT);
    view.ownerBack = [owner] {
        if (owner) EnableWindow(owner, TRUE);
    };
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP, ex = WS_EX_DLGMODALFRAME | (gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0);
    HWND wnd = CreateWindowExW(ex, ui::kViewClass, caption.c_str(), style, 0, 0, 100, 100, owner, nullptr, app.inst, &view);
    if (!wnd) return MessageBoxW(owner, text.c_str(), caption.c_str(), flags);  // no window: the native one
    const BOOL dark = !gLightTheme;
    DwmSetWindowAttribute(wnd, 20, &dark, sizeof dark), titleColors(wnd);
    // The close box is Cancel: the host's close for a window (it destroys the window after this).
    if (auto* h = dynamic_cast<ui::Win32Host*>(view.host())) h->isWindow = true, h->onClosed = [&view] { view.closedBySystem(); };
    // The size for the text and the buttons, centered on the owner (on the screen when the owner does not show).
    SIZE sz{};
    if (HDC dc = GetDC(wnd)) {
        ui::Paint p(dc);
        sz = view.layout(*p);
        ReleaseDC(wnd, dc);
    }
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
