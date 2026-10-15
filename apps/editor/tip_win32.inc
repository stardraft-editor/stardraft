// The hover tooltips on Windows: the core's TipView (core/views_tip.inc) in a Tooltip window, for a view window's
// hoverTip (Win32Host), for the painted parts of a ui::Surface and for the Data form's widgets. Included by main.cpp
// after view_win32.inc.
//
// A window takes part with hoverTipsAttach(w, textAt): `textAt` gives the tip at a point of its client area (empty: none).
// The tip shows as the native tooltips did (TTS_ALWAYSTIP): after the double-click time with the mouse on a part with a
// tip (a tenth of it when a tip showed a moment ago), under the mouse pointer; it stays while the mouse is on a part with
// the same text, goes when the text under the mouse changes, on a press, a wheel turn or a key, when the mouse leaves the
// window, and after ten times the double-click time.

namespace ui {

void gHoverTipsTick(UINT_PTR id);

class HoverTips {
public:
    using TextAt = std::function<std::wstring(POINT)>;

    // `key` names who attached (a host, a surface): detach with another key leaves the window as it is (a window's new
    // host attached before its old one went).
    void attach(HWND w, TextAt textAt, const void* key) {
        if (!w) return;
        windows_[w] = {std::move(textAt), key};
        SetWindowSubclass(w, subclassProc, kSubclassId, 0);
    }
    void detach(HWND w, const void* key) {
        auto it = windows_.find(w);
        if (!w || it == windows_.end() || (key && it->second.key != key)) return;
        if (w == owner_) hide(), cancel(), text_.clear(), owner_ = nullptr;
        windows_.erase(it);
        RemoveWindowSubclass(w, subclassProc, kSubclassId);
    }

    // The tip window now (nullptr: none shows).
    HWND tipWindow() const { return Win32Host::windowOf(view_); }

    // Shows `text` at once with its top left corner at `screen` (datashots: the tooltip spec), owned by `owner`.
    HWND showAt(HWND owner, POINT screen, const std::wstring& text) {
        hide(), cancel();
        owner_ = owner, text_ = text;
        open(screen, false);
        return tipWindow();
    }
    void hide() {
        if (HWND t = tipWindow()) {
            if (auto* h = dynamic_cast<Win32Host*>(view_.host())) h->onClosed = nullptr;
            DestroyWindow(t);
            hiddenAt_ = GetTickCount();
        }
        shown_ = false;
    }

private:
    static constexpr UINT_PTR kSubclassId = 0x71B5;

    void cancel() {
        if (timer_) KillTimer(nullptr, timer_), timer_ = 0;
    }
    void start(UINT ms) {
        cancel();
        timer_ = SetTimer(nullptr, 0, ms, [](HWND, UINT, UINT_PTR id, DWORD) { gHoverTipsTick(id); });
    }

    // The mouse moved over `w` at `pt` (client coordinates).
    void moved(HWND w, POINT pt) {
        auto it = windows_.find(w);
        if (it == windows_.end()) return;
        if (w != owner_) hide(), cancel(), text_.clear(), popped_ = false, owner_ = w;
        if (!tracking_.count(w)) {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, w, 0};
            if (TrackMouseEvent(&tme)) tracking_.insert(w);
        }
        std::wstring t = it->second.textAt(pt);
        if (t == text_) return;  // the same part (or still none): the tip stays, or its wait goes on
        const bool was = shown_;
        hide();
        text_ = std::move(t), popped_ = false;
        if (text_.empty()) return cancel();
        const UINT dbl = GetDoubleClickTime();
        start(was || GetTickCount() - hiddenAt_ < dbl ? std::max(1u, dbl / 10) : dbl);
    }
    // The tip goes and waits for the next move (a press, a wheel turn, a key, the mouse left the window).
    void reset(HWND w) {
        if (w != owner_) return;
        hide(), cancel();
        text_.clear(), popped_ = false;
    }

    void tick(UINT_PTR id) {
        if (id != timer_) return;
        cancel();
        if (shown_) {  // the tip showed long enough
            hide();
            popped_ = true;  // (it shows again when the mouse is on another part)
            return;
        }
        if (popped_ || text_.empty() || !owner_ || !IsWindow(owner_)) return;
        // Only while the mouse is still on the owner, on a part with the same tip (and the program is in front).
        POINT m;
        GetCursorPos(&m);
        if (WindowFromPoint(m) != owner_) return;
        POINT c = m;
        ScreenToClient(owner_, &c);
        auto it = windows_.find(owner_);
        if (it == windows_.end() || it->second.textAt(c) != text_) return;
        open(m, true);
    }

    // Opens the tip window for text_: at `at` (screen), under the mouse pointer when `underPointer`.
    void open(POINT at, bool underPointer) {
        view_.text = text_;
        HDC dc = GetDC(nullptr);
        stardraft::draw::Point size{};
        {
            HGDIOBJ of = SelectObject(dc, app.font);
            GdiCanvas c(dc);
            size = view_.measure(c);
            SelectObject(dc, of);
        }
        ReleaseDC(nullptr, dc);
        RECT r{at.x, at.y, at.x + size.x, at.y + size.y};
        if (underPointer) {
            const int below = pointerHeight();
            OffsetRect(&r, 0, below);
            MONITORINFO mi{sizeof mi};
            if (GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi)) {
                if (r.right > mi.rcWork.right) OffsetRect(&r, mi.rcWork.right - r.right, 0);
                if (r.left < mi.rcWork.left) OffsetRect(&r, mi.rcWork.left - r.left, 0);
                if (r.bottom > mi.rcWork.bottom) OffsetRect(&r, 0, at.y - size.y - r.top);  // above the pointer
            }
        }
        // openViewWindow places a Tooltip in the owner's client coordinates.
        POINT o{r.left, r.top};
        ScreenToClient(owner_, &o);
        const stardraft::draw::WindowSpec spec{stardraft::draw::WindowKind::Tooltip, stardraft::draw::Rect{o.x, o.y, o.x + size.x, o.y + size.y}, L""};
        HWND w = openViewWindow(owner_, view_, spec);
        if (!w) return;
        shown_ = true;
        start(GetDoubleClickTime() * 10);  // it goes after a while
    }

    // How far below the pointer's hot spot the tip goes: to the bottom of the arrow's picture.
    static int pointerHeight() {
        int h = GetSystemMetrics(SM_CYCURSOR) * 2 / 3;
        CURSORINFO ci{sizeof ci};
        if (GetCursorInfo(&ci) && ci.hCursor) {
            ICONINFO ii{};
            if (GetIconInfo(ci.hCursor, &ii)) {
                BITMAP bm{};
                if (ii.hbmMask && GetObjectW(ii.hbmMask, sizeof bm, &bm)) h = (ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2) - int(ii.yHotspot);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
            }
        }
        return std::max(h * 2 / 3, scale(16));
    }

    static LRESULT CALLBACK subclassProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);
    friend void gHoverTipsTick(UINT_PTR id);

    struct Window {
        TextAt textAt;
        const void* key = nullptr;
    };
    std::map<HWND, Window> windows_;
    std::set<HWND> tracking_;
    HWND owner_ = nullptr;   // the window the mouse is over (the tip's owner)
    std::wstring text_;      // the tip under the mouse (shown, or waiting)
    bool shown_ = false, popped_ = false;
    UINT_PTR timer_ = 0;
    DWORD hiddenAt_ = 0;
    TipView view_;
};

inline HoverTips gHoverTips;

inline void gHoverTipsTick(UINT_PTR id) { gHoverTips.tick(id); }

inline LRESULT CALLBACK HoverTips::subclassProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEMOVE: gHoverTips.moved(w, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); break;
    case WM_MOUSELEAVE: gHoverTips.tracking_.erase(w), gHoverTips.reset(w); break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED: gHoverTips.reset(w); break;
    case WM_NCDESTROY: {
        gHoverTips.tracking_.erase(w);
        gHoverTips.detach(w, nullptr);
        break;
    }
    }
    return DefSubclassProc(w, msg, wp, lp);
}

void hoverTipsAttach(HWND w, std::function<std::wstring(POINT)> textAt, const void* key) { gHoverTips.attach(w, std::move(textAt), key); }
void hoverTipsDetach(HWND w, const void* key) { gHoverTips.detach(w, key); }

}  // namespace ui
