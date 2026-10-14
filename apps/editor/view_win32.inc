// --- Views in Win32 windows -------------------------------------------------------------------------------------
// apps/common/view.h's Host for a Win32 window: the window's messages become view events, and its paint draws the
// view on a GdiCanvas (into a buffer, then onto the window). One host per window; a pane made a View keeps its
// window as its host until the program has an SDL window.

#pragma comment(lib, "imm32.lib")  // (the input method: ImmGetContext)

namespace ui {

using stardraft::draw::CompositionEvent;
using stardraft::draw::Cursor;
using stardraft::draw::Host;
using stardraft::draw::Key;
using stardraft::draw::KeyEvent;
using stardraft::draw::MouseEvent;
using stardraft::draw::MouseKind;
using stardraft::draw::TextEvent;
using stardraft::draw::View;

inline unsigned keyMods() {
    unsigned m = 0;
    if (GetKeyState(VK_SHIFT) < 0) m |= stardraft::draw::kShift;
    if (GetKeyState(VK_CONTROL) < 0) m |= stardraft::draw::kCtrl;
    if (GetKeyState(VK_MENU) < 0) m |= stardraft::draw::kAlt;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) m |= stardraft::draw::kSuper;
    return m;
}

inline Key keyFromVk(WPARAM vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return Key(int(vk));
    if (vk >= VK_F1 && vk <= VK_F12) return Key(int(Key::F1) + int(vk - VK_F1));
    switch (vk) {
    case VK_BACK: return Key::Backspace;
    case VK_TAB: return Key::Tab;
    case VK_RETURN: return Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_SPACE: return Key::Space;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_INSERT: return Key::Insert;
    case VK_DELETE: return Key::Delete;
    case VK_OEM_PLUS: case VK_ADD: return Key::Plus;
    case VK_OEM_MINUS: case VK_SUBTRACT: return Key::Minus;
    case VK_OEM_COMMA: return Key::Comma;
    case VK_OEM_PERIOD: return Key::Period;
    case VK_OEM_2: return Key::Slash;
    case VK_OEM_5: return Key::Backslash;
    case VK_OEM_1: return Key::Semicolon;
    case VK_OEM_7: return Key::Quote;
    case VK_OEM_3: return Key::Grave;
    case VK_OEM_4: return Key::LeftBracket;
    case VK_OEM_6: return Key::RightBracket;
    }
    return Key::None;
}

inline LPCWSTR cursorId(Cursor c) {
    switch (c) {
    case Cursor::Hand: return IDC_HAND;
    case Cursor::IBeam: return IDC_IBEAM;
    case Cursor::SizeAll: return IDC_SIZEALL;
    case Cursor::SizeWE: return IDC_SIZEWE;
    case Cursor::SizeNS: return IDC_SIZENS;
    case Cursor::No: return IDC_NO;
    case Cursor::Wait: return IDC_WAIT;
    default: return IDC_ARROW;
    }
}

class Win32Host final : public Host {
public:
    explicit Win32Host(View& v) : view_(v) { v.attach(this); }
    ~Win32Host() override {
        for (auto& [id, t] : timers_) KillTimer(wnd_, id);
        view_.attach(nullptr);
    }
    HWND window() const { return wnd_; }

    void invalidate(View&) override {
        if (wnd_) InvalidateRect(wnd_, nullptr, FALSE);
    }
    void focus(View&) override {
        if (wnd_) SetFocus(wnd_);
    }
    bool hasFocus(const View&) const override { return wnd_ && GetFocus() == wnd_; }
    void capture(View&, bool on) override {
        if (on) SetCapture(wnd_);
        else if (GetCapture() == wnd_) ReleaseCapture();
    }
    stardraft::draw::Point mouse(const View&) const override {
        POINT m;
        cursorPos(m);
        ScreenToClient(wnd_, &m);
        return {m.x, m.y};
    }
    void setCursor(Cursor c) override { SetCursor(LoadCursor(nullptr, cursorId(c))); }
    std::string clipboardText() override {
        std::string out;
        if (!OpenClipboard(wnd_)) return out;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (auto* t = static_cast<const wchar_t*>(GlobalLock(h))) {
                out = narrow(t);
                GlobalUnlock(h);
            }
        CloseClipboard();
        return out;
    }
    void setClipboardText(std::string_view utf8) override {
        const std::wstring w = widen(std::string(utf8));
        if (!OpenClipboard(wnd_)) return;
        EmptyClipboard();
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
            memcpy(GlobalLock(g), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        }
        CloseClipboard();
    }
    int timer(int ms, std::function<void()> run) override {
        const int id = nextTimer_++;
        timers_[UINT_PTR(id)] = std::move(run);
        SetTimer(wnd_, UINT_PTR(id), UINT(std::max(1, ms)), nullptr);
        return id;
    }
    void cancelTimer(int id) override {
        KillTimer(wnd_, UINT_PTR(id));
        timers_.erase(UINT_PTR(id));
    }
    void textInput(View&, bool on, stardraft::draw::Rect caret) override {
        imeOn_ = on;
        if (HIMC imc = ImmGetContext(wnd_)) {
            COMPOSITIONFORM cf{CFS_POINT, {caret.left, caret.top}, {}};
            ImmSetCompositionWindow(imc, &cf);
            CANDIDATEFORM cand{0, CFS_EXCLUDE, {caret.left, caret.bottom}, {caret.left, caret.top, caret.right, caret.bottom}};
            ImmSetCandidateWindow(imc, &cand);
            ImmReleaseContext(wnd_, imc);
        }
    }
    float scale() const override { return float(app.dpi) / 96.0f; }

    void openWindow(View& from, View& content, const stardraft::draw::WindowSpec& spec, std::function<void()> closed) override;
    void closeWindow(View& content) override;
    void runModal(View& from, View& content, const stardraft::draw::WindowSpec& spec) override;

    // The window of a view shown by a Win32Host (nullptr: none).
    static HWND windowOf(const View& v) {
        auto* h = dynamic_cast<Win32Host*>(v.host());
        return h ? h->wnd_ : nullptr;
    }
    std::function<void()> onClosed;  // (a Popup or Menu that closes itself)
    stardraft::draw::WindowKind kind = stardraft::draw::WindowKind::Dialog;
    bool isWindow = false;  // made by openWindow (else a pane's window that its owner made)

    // The window's procedure calls this first; true: handled (`result` is the answer).
    bool message(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        wnd_ = w;
        auto mouseEvent = [&](MouseKind k, int button) {
            MouseEvent e;
            e.kind = k, e.button = button, e.pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, e.mods = keyMods();
            return e;
        };
        switch (msg) {
        case WM_SIZE: view_.setSize(LOWORD(lp), HIWORD(lp)); return false;
        case WM_ERASEBKGND: result = 1; return true;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            RECT rc;
            GetClientRect(w, &rc);
            view_.setSize(rc.right, rc.bottom);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ ob = SelectObject(mem, bmp), of = SelectObject(mem, app.font);
            GdiCanvas gdi(mem);
            Canvas& c = gdi;
            c.fill(rc, kBg);
            view_.paint(c);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, of), SelectObject(mem, ob);
            DeleteObject(bmp), DeleteDC(mem);
            EndPaint(w, &ps);
            result = 0;
            return true;
        }
        case WM_MOUSEMOVE:
            if (!tracking_) {
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, w, 0};
                tracking_ = TrackMouseEvent(&tme) != 0;
            }
            return view_.mouse(mouseEvent(MouseKind::Move, 0)) ? (result = 0, true) : false;
        case WM_MOUSELEAVE: {
            tracking_ = false;
            MouseEvent e;
            e.kind = MouseKind::Leave;
            return view_.mouse(e) ? (result = 0, true) : false;
        }
        case WM_LBUTTONDOWN: return view_.mouse(mouseEvent(MouseKind::Down, 0)) ? (result = 0, true) : false;
        case WM_RBUTTONDOWN: return view_.mouse(mouseEvent(MouseKind::Down, 1)) ? (result = 0, true) : false;
        case WM_MBUTTONDOWN: return view_.mouse(mouseEvent(MouseKind::Down, 2)) ? (result = 0, true) : false;
        case WM_LBUTTONUP: return view_.mouse(mouseEvent(MouseKind::Up, 0)) ? (result = 0, true) : false;
        case WM_RBUTTONUP: return view_.mouse(mouseEvent(MouseKind::Up, 1)) ? (result = 0, true) : false;
        case WM_MBUTTONUP: return view_.mouse(mouseEvent(MouseKind::Up, 2)) ? (result = 0, true) : false;
        case WM_LBUTTONDBLCLK: return view_.mouse(mouseEvent(MouseKind::DoubleClick, 0)) ? (result = 0, true) : false;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(w, &pt);
            MouseEvent e;
            e.kind = MouseKind::Wheel, e.pt = {pt.x, pt.y}, e.mods = keyMods();
            e.wheel = float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA, e.horizontal = msg == WM_MOUSEHWHEEL;
            return view_.mouse(e) ? (result = 0, true) : false;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lp) != HTCLIENT) return false;
            setCursor(view_.cursor(mouse(view_)));
            result = TRUE;
            return true;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            KeyEvent e;
            e.key = keyFromVk(wp), e.down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, e.repeat = (lp & (1 << 30)) != 0, e.mods = keyMods();
            if (e.key == Key::None) return false;
            return view_.key(e) ? (result = 0, true) : false;
        }
        case WM_CHAR: {
            if (wp < 32 || wp == 127) return false;  // (control keys come as key events)
            if (IS_HIGH_SURROGATE(wchar_t(wp))) {
                high_ = wchar_t(wp);
                result = 0;
                return true;
            }
            std::wstring t;
            if (high_ && IS_LOW_SURROGATE(wchar_t(wp))) t.push_back(high_);
            high_ = 0;
            t.push_back(wchar_t(wp));
            return view_.text({narrow(t)}) ? (result = 0, true) : false;
        }
        case WM_IME_STARTCOMPOSITION:
            if (!imeOn_) return false;
            result = 0;
            return true;  // the view draws the composition
        case WM_IME_COMPOSITION: {
            if (!imeOn_) return false;
            HIMC imc = ImmGetContext(w);
            if (!imc) return false;
            if (lp & GCS_RESULTSTR) {
                const LONG n = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
                std::wstring s(size_t(std::max(0L, n)) / sizeof(wchar_t), L'\0');
                if (n > 0) ImmGetCompositionStringW(imc, GCS_RESULTSTR, s.data(), DWORD(n));
                view_.compose({});
                if (!s.empty()) view_.text({narrow(s)});
            }
            if (lp & GCS_COMPSTR) {
                const LONG n = ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
                std::wstring s(size_t(std::max(0L, n)) / sizeof(wchar_t), L'\0');
                if (n > 0) ImmGetCompositionStringW(imc, GCS_COMPSTR, s.data(), DWORD(n));
                const LONG cur = ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0);
                view_.compose({narrow(s), int(narrow(s.substr(0, size_t(std::clamp<LONG>(cur, 0, LONG(s.size()))))).size())});
            }
            ImmReleaseContext(w, imc);
            result = 0;
            return true;
        }
        case WM_IME_ENDCOMPOSITION:
            if (!imeOn_) return false;
            view_.compose({});
            result = 0;
            return true;
        case WM_IME_CHAR:
            if (!imeOn_) return false;
            result = 0;
            return true;  // (the result string came with WM_IME_COMPOSITION)
        case WM_SETFOCUS: view_.focusChanged(true); return false;
        case WM_KILLFOCUS: view_.focusChanged(false); return false;
        case WM_TIMER: {
            auto it = timers_.find(wp);
            if (it == timers_.end()) return false;
            KillTimer(w, wp);
            auto run = std::move(it->second);
            timers_.erase(it);
            run();
            result = 0;
            return true;
        }
        case WM_GETDLGCODE: result = DLGC_WANTALLKEYS | DLGC_WANTCHARS; return true;
        case WM_ACTIVATE:  // a Popup or Menu closes when another window takes the focus
            if (isWindow && LOWORD(wp) == WA_INACTIVE && (kind == stardraft::draw::WindowKind::Popup || kind == stardraft::draw::WindowKind::Menu)) {
                PostMessageW(w, WM_CLOSE, 0, 0);
                return false;
            }
            return false;
        case WM_CLOSE:
            if (!isWindow) return false;
            if (auto c = std::move(onClosed)) c();
            DestroyWindow(w);
            result = 0;
            return true;
        case WM_MOUSEACTIVATE:
            if (kind == stardraft::draw::WindowKind::Tooltip) return result = MA_NOACTIVATE, true;
            return false;
        case WM_NCHITTEST:
            if (kind == stardraft::draw::WindowKind::Tooltip) return result = HTTRANSPARENT, true;
            return false;
        }
        return false;
    }

private:
    View& view_;
    HWND wnd_ = nullptr;
    bool tracking_ = false, imeOn_ = false;
    wchar_t high_ = 0;
    std::map<UINT_PTR, std::function<void()>> timers_;
    int nextTimer_ = 0x5D00;  // (away from the ids the panes use with SetTimer)
};

// A window class whose windows show a View: CreateWindowExW(..., kViewClass, ..., lpParam = the View*).
inline const wchar_t* kViewClass = L"StarDraftView";
LRESULT CALLBACK viewWindowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* host = reinterpret_cast<Win32Host*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* v = static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        host = new Win32Host(*v);
        SetWindowLongPtrW(w, GWLP_USERDATA, LONG_PTR(host));
    }
    LRESULT r = 0;
    if (host && host->message(w, msg, wp, lp, r)) return r;
    if (msg == WM_NCDESTROY && host) {
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        delete host;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
inline HWND openViewWindow(HWND owner, View& content, const stardraft::draw::WindowSpec& spec) {
    using stardraft::draw::WindowKind;
    RECT r{spec.at.left, spec.at.top, spec.at.right, spec.at.bottom};
    DWORD style = WS_POPUP, ex = 0;
    if (spec.kind == WindowKind::Dialog) {
        style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        ex = WS_EX_DLGMODALFRAME;
        RECT o{};
        GetWindowRect(owner, &o);
        const int w = r.right - r.left, h = r.bottom - r.top;
        r = {(o.left + o.right - w) / 2, (o.top + o.bottom - h) / 2, 0, 0};
        r.right = r.left + w, r.bottom = r.top + h;
        AdjustWindowRectEx(&r, style, FALSE, ex);
    } else {
        POINT a{r.left, r.top};
        ClientToScreen(owner, &a);
        OffsetRect(&r, a.x - r.left, a.y - r.top);
        ex = WS_EX_TOOLWINDOW | (spec.kind == WindowKind::Tooltip ? WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST : 0);
    }
    if (gBackground) OffsetRect(&r, GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 100, 0);  // (tests: off the screen)
    HWND w = CreateWindowExW(ex, kViewClass, spec.title.c_str(), style, r.left, r.top, r.right - r.left, r.bottom - r.top, GetAncestor(owner, GA_ROOT),
                             nullptr, app.inst, &content);
    if (!w) return nullptr;
    if (auto* h = dynamic_cast<Win32Host*>(content.host())) h->kind = spec.kind, h->isWindow = true;
    if (spec.kind == WindowKind::Dialog) {
        const BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(w, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark), titleColors(w);
    }
    popupFrame(w);
    ShowWindow(w, spec.kind == WindowKind::Tooltip || gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    return w;
}
inline void Win32Host::openWindow(View&, View& content, const stardraft::draw::WindowSpec& spec, std::function<void()> closed) {
    if (HWND w = openViewWindow(wnd_, content, spec))
        if (auto* h = dynamic_cast<Win32Host*>(content.host())) h->onClosed = std::move(closed), (void)w;
}
inline void Win32Host::closeWindow(View& content) {
    if (HWND w = windowOf(content)) {
        if (auto* h = dynamic_cast<Win32Host*>(content.host())) h->onClosed = nullptr;
        DestroyWindow(w);
    }
}
inline void Win32Host::runModal(View& from, View& content, const stardraft::draw::WindowSpec& spec) {
    HWND root = GetAncestor(wnd_, GA_ROOT);
    const bool wasEnabled = IsWindowEnabled(root) != 0;
    openWindow(from, content, spec, nullptr);
    HWND w = windowOf(content);
    if (!w) return;
    EnableWindow(root, FALSE);
    MSG m;
    while (IsWindow(w) && GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (wasEnabled) EnableWindow(root, TRUE);
    if (!gBackground) SetActiveWindow(root);
}

inline void registerViewClass(HINSTANCE inst) {
    WNDCLASSEXW vc{sizeof vc};
    vc.style = CS_DBLCLKS;
    vc.lpfnWndProc = viewWindowProc;
    vc.hInstance = inst;
    vc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    vc.lpszClassName = kViewClass;
    RegisterClassExW(&vc);
}

}  // namespace ui
