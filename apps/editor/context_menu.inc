// Context menus, painted as the pickers: the window's ground, larger corners, the item under the mouse or the keys a
// rounded pill, the command icons and check marks at the left, the keys at the right in dim, submenus to the side.
// It reads a normal HMENU, so the menus are made as before (AppendMenuW); paintedMenu() takes the place of
// TrackPopupMenu (TPM_RETURNCMD): it returns the id picked, or 0.
//
// One modal loop runs all the open menus (the menu and its open submenus). The menus do not take the focus (the
// window under them stays active, as with the system's menus); the loop takes the keys and, with the mouse captured,
// every click: a click outside closes the menus.

struct PaintedMenu {
    struct Item {
        UINT id = 0;
        std::wstring text, keys;  // the text (no '&'), the keys after a tab
        wchar_t mnemonic = 0;     // the letter after '&' (lower case)
        bool separator = false, gray = false, checked = false, radio = false;
        bool reason = false;  // a grayed item that tells why after its colon: the reason is painted in kDim
        HMENU sub = nullptr;
        HBITMAP bmp = nullptr;
        RECT r{};  // in the window
    };
    HWND wnd = nullptr;
    std::vector<Item> items;
    int hot = -1;
    int openItem = -1;  // the item whose submenu is open (the next menu in the stack)
};

struct MenuLoop {
    std::vector<std::unique_ptr<PaintedMenu>> stack;  // [0] the menu, then each open submenu
    bool done = false, armed = false, moved = false, releasing = false;
    POINT start{};
    UINT result = 0;
    // A menu of the menu bar: the bar, which of its menus shows, and the one to show next (the mouse or Left/Right).
    HMENU bar = nullptr;
    HWND barWnd = nullptr;
    int barIndex = -1, switchTo = -1;
    bool rightArmed = false;    // a right button went down in a menu
    std::wstring feedbackOn;    // a right-click on an item: feedback on it (after the menus close)
};
int gMenuBarOpen = -1;  // the menu bar's menu that shows (drawn as pressed in the bar)
MenuLoop* gMenuLoop = nullptr;
HWND gMenuForegroundTest = nullptr;  // self-test: the window taken as in front (to test that the menus close)

// A grayed item whose text after the first ':' tells why it cannot be changed (its item data). The name stays faint,
// the reason is painted in kDim, so that it is easy to read.
const ULONG_PTR kMenuReason = 1;
void markMenuReason(HMENU m, UINT id) {
    MENUITEMINFOW mi{sizeof mi};
    mi.fMask = MIIM_DATA, mi.dwItemData = kMenuReason;
    SetMenuItemInfoW(m, id, FALSE, &mi);
}

// Self-tests: the open menu's window (painted, or the system's), and closing the menus.
HWND openMenuWindow() {
    if (gMenuLoop && !gMenuLoop->stack.empty()) return gMenuLoop->stack.front()->wnd;
    return FindWindowW(L"#32768", nullptr);
}
void closeMenus() {
    if (gMenuLoop) gMenuLoop->done = true;
    else EndMenu();
}

namespace menu_impl {

const int kRowH = 30, kSepH = 9, kPad = 5, kIconW = 34, kSubW = 22;

void read(PaintedMenu& m, HMENU h) {
    const int n = GetMenuItemCount(h);
    for (int i = 0; i < n; ++i) {
        wchar_t buf[512] = L"";
        MENUITEMINFOW mi{sizeof mi};
        mi.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP | MIIM_DATA;
        mi.dwTypeData = buf, mi.cch = 511;
        if (!GetMenuItemInfoW(h, UINT(i), TRUE, &mi)) continue;
        PaintedMenu::Item it;
        it.id = mi.wID, it.sub = mi.hSubMenu;
        it.separator = (mi.fType & MFT_SEPARATOR) != 0;
        it.gray = (mi.fState & (MFS_GRAYED | MFS_DISABLED)) != 0;
        it.checked = (mi.fState & MFS_CHECKED) != 0;
        it.radio = (mi.fType & MFT_RADIOCHECK) != 0;
        it.reason = it.gray && mi.dwItemData == kMenuReason;
        if (UINT_PTR(mi.hbmpItem) > UINT_PTR(HBMMENU_POPUP_MINIMIZE) && mi.hbmpItem != HBMMENU_CALLBACK) it.bmp = mi.hbmpItem;
        std::wstring s = buf;
        if (const size_t tab = s.find(L'\t'); tab != std::wstring::npos) it.keys = s.substr(tab + 1), s.resize(tab);
        for (size_t k = 0; k < s.size(); ++k)
            if (s[k] == L'&') {
                if (k + 1 < s.size() && s[k + 1] != L'&' && !it.mnemonic) it.mnemonic = wchar_t(towlower(s[k + 1]));
                if (k + 1 < s.size()) it.text += s[++k];
            } else it.text += s[k];
        m.items.push_back(std::move(it));
    }
}

// Sizes the items; returns the window's size.
SIZE layout(PaintedMenu& m) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ of = SelectObject(dc, app.font);
    int textW = 0, keysW = 0;
    for (const auto& it : m.items) {
        SIZE a{}, b{};
        GetTextExtentPoint32W(dc, it.text.c_str(), int(it.text.size()), &a);
        GetTextExtentPoint32W(dc, it.keys.c_str(), int(it.keys.size()), &b);
        textW = std::max<int>(textW, a.cx), keysW = std::max<int>(keysW, b.cx);
    }
    SelectObject(dc, of);
    ReleaseDC(nullptr, dc);
    const bool anySub = std::any_of(m.items.begin(), m.items.end(), [](const auto& i) { return i.sub != nullptr; });
    const int w = std::max(scale(200), scale(kIconW) + textW + (keysW ? scale(36) + keysW : 0) + scale(anySub ? kSubW : 0) + scale(18) + 2 * scale(kPad));
    int y = scale(kPad);
    for (auto& it : m.items) {
        const int h = scale(it.separator ? kSepH : kRowH);
        it.r = {scale(kPad), y, w - scale(kPad), y + h};
        y += h;
    }
    return {w, y + scale(kPad)};
}

void paint(PaintedMenu& m, HDC out, RECT rc) {
    HDC dc = CreateCompatibleDC(out);
    HBITMAP bb = CreateCompatibleBitmap(out, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(dc, bb);
    FillRect(dc, &rc, app.bgBrush);
    HGDIOBJ of = SelectObject(dc, app.font);
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < int(m.items.size()); ++i) {
        const auto& it = m.items[size_t(i)];
        RECT r = it.r;
        if (it.separator) {
            fillAlpha(dc, {r.left + scale(8), (r.top + r.bottom) / 2, r.right - scale(8), (r.top + r.bottom) / 2 + 1}, kBorder, 255);
            continue;
        }
        if (i == m.hot || i == m.openItem) ui::box(dc, r, kSel, kSel, scale(6));
        const COLORREF fg = it.gray ? kFaint : kText;
        RECT icon{r.left + scale(8), r.top + (r.bottom - r.top - scale(18)) / 2, r.left + scale(8) + scale(18), r.top + (r.bottom - r.top + scale(18)) / 2};
        if (it.bmp) {
            BITMAP bm{};
            GetObjectW(it.bmp, sizeof bm, &bm);
            HDC src = CreateCompatibleDC(dc);
            HGDIOBJ os = SelectObject(src, it.bmp);
            const int bw = bm.bmWidth, bh = std::abs(bm.bmHeight);
            BLENDFUNCTION bf{AC_SRC_OVER, 0, BYTE(it.gray ? 90 : 255), AC_SRC_ALPHA};
            AlphaBlend(dc, (icon.left + icon.right - bw) / 2, (icon.top + icon.bottom - bh) / 2, bw, bh, src, 0, 0, bw, bh, bf);
            SelectObject(src, os);
            DeleteDC(src);
        }
        if (it.checked) {
            if (it.bmp) ui::box(dc, {icon.left - scale(3), icon.top - scale(3), icon.right + scale(3), icon.bottom + scale(3)}, kChip, kAccentLine, scale(4));
            else if (it.radio) {
                const int d = scale(7), cx = (icon.left + icon.right) / 2, cy = (icon.top + icon.bottom) / 2;
                ui::box(dc, {cx - d / 2, cy - d / 2, cx - d / 2 + d, cy - d / 2 + d}, kAccent, kAccent, d);
            } else uiIcon(dc, icon, UiIcon::Check, kAccent, scale(16));
        }
        RECT t{r.left + scale(kIconW), r.top, r.right - scale(it.sub ? kSubW : 10), r.bottom};
        const size_t colon = it.reason ? it.text.find(L':') : std::wstring::npos;
        SetTextColor(dc, fg);
        if (colon == std::wstring::npos) DrawTextW(dc, it.text.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        else {  // the name (with its colon) faint, the reason after it in kDim
            SIZE a{};
            GetTextExtentPoint32W(dc, it.text.c_str(), int(colon + 1), &a);
            DrawTextW(dc, it.text.c_str(), int(colon + 1), &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            RECT rest = t;
            rest.left = std::min(t.right, t.left + a.cx);
            SetTextColor(dc, kDim);
            DrawTextW(dc, it.text.c_str() + colon + 1, -1, &rest, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        if (!it.keys.empty()) {
            SetTextColor(dc, it.gray ? kFaint : kDim);
            DrawTextW(dc, it.keys.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }
        if (it.sub) {  // a chevron: the submenu opens to the side
            const int cx = r.right - scale(13), cy = (r.top + r.bottom) / 2, a = scale(4);
            HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)) + 1, fg);
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, cx - a / 2, cy - a, nullptr);
            LineTo(dc, cx + a / 2, cy);
            LineTo(dc, cx - a / 2, cy + a + 1);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
    }
    SelectObject(dc, of);
    BitBlt(out, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bb);
    DeleteDC(dc);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* m = reinterpret_cast<PaintedMenu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (m) paint(*m, dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CAPTURECHANGED:  // another window took the mouse (a switch to another program): the menus close
        if (gMenuLoop && !gMenuLoop->releasing) gMenuLoop->done = true, PostMessageW(nullptr, WM_NULL, 0, 0);  // (a sent message: wakes the loop)
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Opens a menu window for `h` with its top left at `at` (moved to stay on the screen; `alt` is where it goes when
// there is no room to the right: a submenu opens to the left of its item).
std::unique_ptr<PaintedMenu> open(HMENU h, POINT at, int altX, int altBottom, HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = proc, wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpszClassName = L"StarDraftMenu";
        wc.style = CS_DROPSHADOW;
        RegisterClassExW(&wc);
        registered = true;
    }
    auto m = std::make_unique<PaintedMenu>();
    read(*m, h);
    const SIZE sz = layout(*m);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(POINT{altX, at.y}, MONITOR_DEFAULTTONEAREST), &mi);  // (a submenu: the screen of its menu)
    const RECT wa = mi.rcWork;
    int x = at.x, y = at.y;
    if (x + sz.cx > wa.right) x = altX - sz.cx;
    if (y + sz.cy > wa.bottom) y = std::max<int>(wa.top, altBottom - sz.cy);  // no room below: above the point (or up from its item)
    x = std::clamp<int>(x, wa.left, std::max<int>(wa.left, wa.right - sz.cx));
    if (gBackground) {  // tests: off the screen (where the point is off the screen already, as the test's main window: there)
        const RECT vs{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                      GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        if ((!gMenuLoop || gMenuLoop->stack.empty()) && PtInRect(&vs, at)) x = vs.right + 100, y = 0;
        else x = at.x, y = at.y;
    }
    m->wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"StarDraftMenu", L"", WS_POPUP | WS_BORDER, x, y, sz.cx, sz.cy, owner,
                             nullptr, app.inst, nullptr);
    SetWindowLongPtrW(m->wnd, GWLP_USERDATA, LONG_PTR(m.get()));
    ui::popupFrame(m->wnd);
    const int round = 2;  // DWMWCP_ROUND, as the pickers
    DwmSetWindowAttribute(m->wnd, 33, &round, sizeof round);
    fadeIn(m->wnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m->wnd);
    return m;
}

void setHot(PaintedMenu& m, int i) {
    if (m.hot == i) return;
    m.hot = i;
    InvalidateRect(m.wnd, nullptr, FALSE);
}

// Closes the submenus after level `level` of the stack.
void closeAfter(MenuLoop& L, size_t level) {
    while (L.stack.size() > level + 1) {
        DestroyWindow(L.stack.back()->wnd);
        L.stack.pop_back();
    }
    if (level < L.stack.size() && L.stack[level]->openItem >= 0) L.stack[level]->openItem = -1, InvalidateRect(L.stack[level]->wnd, nullptr, FALSE);
}

void openSub(MenuLoop& L, size_t level, int i, bool byKeys, HWND owner) {
    PaintedMenu& m = *L.stack[level];
    if (m.openItem == i) return;
    closeAfter(L, level);
    const auto& it = m.items[size_t(i)];
    if (!it.sub || it.gray) return;
    if (L.bar) SendMessageW(owner, WM_INITMENUPOPUP, WPARAM(it.sub), MAKELPARAM(i, FALSE));  // its items' states (as the system's menus)
    RECT wr;
    GetWindowRect(m.wnd, &wr);
    m.openItem = i;
    InvalidateRect(m.wnd, nullptr, FALSE);
    auto sub = open(it.sub, POINT{wr.left + it.r.right + scale(2), wr.top + it.r.top - scale(kPad)}, wr.left + it.r.left - scale(2),
                    wr.top + it.r.bottom + scale(kPad), owner);
    if (byKeys)
        for (int k = 0; k < int(sub->items.size()); ++k)
            if (!sub->items[size_t(k)].separator) {
                sub->hot = k;
                break;
            }
    L.stack.push_back(std::move(sub));
}

// Picks item `i` of the menu at `level`: a command closes the menus; a submenu opens.
void choose(MenuLoop& L, size_t level, int i, bool byKeys, HWND owner) {
    const auto& it = L.stack[level]->items[size_t(i)];
    if (it.separator || it.gray) return;
    if (it.sub) return openSub(L, level, i, byKeys, owner);
    L.result = it.id, L.done = true;
}

// The menu under a screen point (the deepest first), or -1.
int menuAt(MenuLoop& L, POINT pt) {
    for (int k = int(L.stack.size()) - 1; k >= 0; --k) {
        RECT wr;
        GetWindowRect(L.stack[size_t(k)]->wnd, &wr);
        if (PtInRect(&wr, pt)) return k;
    }
    return -1;
}

int itemAt(PaintedMenu& m, POINT screen) {
    POINT p = screen;
    ScreenToClient(m.wnd, &p);
    for (int i = 0; i < int(m.items.size()); ++i)
        if (!m.items[size_t(i)].separator && PtInRect(&m.items[size_t(i)].r, p)) return i;
    return -1;
}

void step(PaintedMenu& m, int dir) {
    const int n = int(m.items.size());
    if (!n) return;
    int i = m.hot;
    for (int k = 0; k < n; ++k) {
        i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
        if (!m.items[size_t(i)].separator) return setHot(m, i);
    }
}

}  // namespace menu_impl

// The loop for one menu (and its submenus). bar: a menu of the menu bar (barIndex), whose Left/Right and mouse
// can ask for the bar's next menu (switchTo).
int menuLoop(HMENU h, int x, int y, HWND owner, HMENU bar, int barIndex, int* switchTo, bool byKeys) {
    using namespace menu_impl;
    if (!h || GetMenuItemCount(h) <= 0) return 0;
    if (gMenuLoop) return 0;  // one at a time
    MenuLoop L;
    L.start = {x, y};
    L.bar = bar, L.barWnd = owner, L.barIndex = barIndex;
    gMenuLoop = &L;
    L.stack.push_back(open(h, POINT{x, y}, x, y, owner));
    if (byKeys) step(*L.stack.front(), 1);
    const int barCount = bar ? GetMenuItemCount(bar) : 0;
    auto barItemAt = [&](POINT pt) { return bar ? MenuItemFromPoint(owner, bar, pt) : -1; };
    // The menus close when StarDraft is no longer in front (the Windows key, a click in another program).
    UINT_PTR watch = 0;
    if (!gBackground) SetCapture(L.stack.front()->wnd);
    watch = SetTimer(nullptr, 0, 100, nullptr);
    bool closeLater = false;  // a WM_CLOSE came while the menus were open: sent again after them
    HWND closeWnd = nullptr;
    MSG msg{};
    while (!L.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == watch) {
            if (gBackground && !gMenuForegroundTest) continue;  // a test run is not in front
            DWORD pid = 0;
            GetWindowThreadProcessId(gMenuForegroundTest ? gMenuForegroundTest : GetForegroundWindow(), &pid);
            if (pid != GetCurrentProcessId() || (!gBackground && GetCapture() != L.stack.front()->wnd)) L.done = true;
            continue;
        }
        if (msg.message == WM_CLOSE) {
            closeLater = true, closeWnd = msg.hwnd, L.done = true;
            continue;
        }
        if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) {
            // Where: from the message (the capture window's client coordinates), not the cursor: so the
            // self-test's posted clicks go the same way as the mouse's.
            POINT pt = msg.pt;
            if (msg.hwnd && msg.message != WM_MOUSEWHEEL && msg.message != WM_MOUSEHWHEEL)
                pt = {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)}, ClientToScreen(msg.hwnd, &pt);
            const int k = menuAt(L, pt);
            switch (msg.message) {
            case WM_MOUSEMOVE: {
                if (std::abs(pt.x - L.start.x) > 4 || std::abs(pt.y - L.start.y) > 4) L.moved = true;
                if (k < 0) {
                    if (const int bi = barItemAt(pt); bi >= 0 && bi != L.barIndex) {  // over another menu of the bar: that one
                        L.switchTo = bi, L.done = true;
                        break;
                    }
                    PaintedMenu& last = *L.stack.back();
                    if (last.openItem < 0) setHot(last, -1);
                    break;
                }
                PaintedMenu& m = *L.stack[size_t(k)];
                const int i = itemAt(m, pt);
                if (i >= 0 && i != m.hot) {
                    setHot(m, i);
                    if (m.items[size_t(i)].sub) openSub(L, size_t(k), i, false, owner);
                    else closeAfter(L, size_t(k));
                }
                break;
            }
            case WM_RBUTTONDOWN:
                if (k >= 0) L.rightArmed = true;
                [[fallthrough]];
            case WM_LBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDBLCLK:
                if (k < 0) {  // a click outside: the menus close (on another menu of the bar: that one opens)
                    if (const int bi = barItemAt(pt); bi >= 0 && bi != L.barIndex) L.switchTo = bi;
                    L.done = true;
                } else L.armed = true;
                break;
            case WM_LBUTTONUP:
                if (k >= 0 && (L.armed || L.moved))
                    if (const int i = itemAt(*L.stack[size_t(k)], pt); i >= 0) choose(L, size_t(k), i, false, owner);
                break;
            case WM_RBUTTONUP:  // a right-click on an item: feedback on it (the menus close); a right-drag onto it: it (as before)
                if (k >= 0 && !L.rightArmed && L.moved)
                    if (const int i = itemAt(*L.stack[size_t(k)], pt); i >= 0) choose(L, size_t(k), i, false, owner);
                if (k >= 0 && L.rightArmed)
                    if (const int i = itemAt(*L.stack[size_t(k)], pt); i >= 0 && L.stack[size_t(k)]->items[size_t(i)].id == kFeedbackItem)
                        choose(L, size_t(k), i, false, owner);  // "Report feedback on ...": that item
                    else if (i >= 0 && !L.stack[size_t(k)]->items[size_t(i)].separator) {
                        std::wstring where;
                        if (L.bar) {
                            wchar_t buf[64] = L"";
                            GetMenuStringW(L.bar, UINT(L.barIndex), buf, 64, MF_BYPOSITION);
                            where = buf;
                            std::erase(where, L'&');
                            where += L" > ";
                        }
                        L.feedbackOn = L"the menu item " + where + L.stack[size_t(k)]->items[size_t(i)].text;
                        L.done = true;
                    }
                break;
            }
            continue;
        }
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            TranslateMessage(&msg);  // the access letters come as WM_CHAR
            PaintedMenu& m = *L.stack.back();
            const size_t level = L.stack.size() - 1;
            switch (msg.wParam) {
            case VK_DOWN: step(m, 1); break;
            case VK_UP: step(m, -1); break;
            case VK_HOME: setHot(m, -1), step(m, 1); break;
            case VK_END: setHot(m, -1), step(m, -1); break;
            case VK_RIGHT:
                if (m.hot >= 0 && m.items[size_t(m.hot)].sub) openSub(L, level, m.hot, true, owner);
                else if (barCount) L.switchTo = (L.barIndex + 1) % barCount, L.done = true;  // the bar's next menu
                break;
            case VK_LEFT:
                if (level > 0) closeAfter(L, level - 1);
                else if (barCount) L.switchTo = (L.barIndex + barCount - 1) % barCount, L.done = true;
                break;
            case VK_ESCAPE:
                if (level > 0) closeAfter(L, level - 1);
                else L.done = true;
                break;
            case VK_RETURN:
            case VK_SPACE:
                if (m.hot >= 0) choose(L, level, m.hot, true, owner);
                break;
            case VK_MENU:
            case VK_F10:
            case VK_TAB: L.done = true; break;
            }
            continue;
        }
        if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR) {  // an access letter: picks the item (or the next one)
            PaintedMenu& m = *L.stack.back();
            const wchar_t c = wchar_t(towlower(wchar_t(msg.wParam)));
            std::vector<int> hits;
            for (int i = 0; i < int(m.items.size()); ++i)
                if (!m.items[size_t(i)].separator && !m.items[size_t(i)].gray && m.items[size_t(i)].mnemonic == c) hits.push_back(i);
            if (hits.size() == 1) setHot(m, hits[0]), choose(L, L.stack.size() - 1, hits[0], true, owner);
            else if (!hits.empty()) {
                auto next = std::upper_bound(hits.begin(), hits.end(), m.hot);
                setHot(m, next == hits.end() ? hits[0] : *next);
            }
            continue;
        }
        if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (msg.message == WM_QUIT) PostQuitMessage(int(msg.wParam));
    L.releasing = true;
    if (GetCapture() == L.stack.front()->wnd) ReleaseCapture();
    for (auto it = L.stack.rbegin(); it != L.stack.rend(); ++it) DestroyWindow((*it)->wnd);
    if (watch) KillTimer(nullptr, watch);
    gMenuLoop = nullptr;
    if (closeLater && IsWindow(closeWnd)) PostMessageW(closeWnd, WM_CLOSE, 0, 0);
    if (switchTo) *switchTo = L.switchTo;
    if (!L.feedbackOn.empty()) showFeedbackDialog(L.feedbackOn);
    return int(L.result);
}

int paintedMenu(HMENU h, int x, int y, HWND owner) { return menuLoop(h, x, y, owner, nullptr, -1, nullptr, false); }

// The menu bar's menus, painted as the context menus: menu `index` under its place in the bar; the mouse over the
// bar or Left/Right goes to the next one. The command picked comes as WM_COMMAND (as from the system's menus).
void menuBarOpen(HWND owner, int index, bool byKeys) {
    HMENU bar = GetMenu(owner);
    const int n = bar ? GetMenuItemCount(bar) : 0;
    int id = 0;
    while (index >= 0 && index < n) {
        HMENU sub = GetSubMenu(bar, index);
        if (!sub) break;
        gMenuBarOpen = index;
        DrawMenuBar(owner);
        SendMessageW(owner, WM_INITMENUPOPUP, WPARAM(sub), MAKELPARAM(index, FALSE));  // the items' states (updateMenus)
        RECT r{};
        GetMenuItemRect(owner, bar, UINT(index), &r);
        int next = -1;
        id = menuLoop(sub, r.left, r.bottom, owner, bar, index, &next, byKeys);
        if (next < 0 || id) break;
        index = next;
    }
    gMenuBarOpen = -1;
    DrawMenuBar(owner);
    if (id) PostMessageW(owner, WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

// Alt+letter (or Alt, F10): the bar's menu with that access letter (Alt alone: the first), by the keys.
bool menuBarKey(HWND owner, wchar_t c) {
    HMENU bar = GetMenu(owner);
    if (!bar) return false;
    if (!c) return menuBarOpen(owner, 0, true), true;
    const int n = GetMenuItemCount(bar);
    for (int i = 0; i < n; ++i) {
        wchar_t buf[64] = L"";
        GetMenuStringW(bar, UINT(i), buf, 64, MF_BYPOSITION);
        for (const wchar_t* p = buf; *p; ++p) {
            if (p[0] == L'&' && p[1] == L'&') {  // "&&": an & in the name
                ++p;
                continue;
            }
            if (*p == L'&' && p[1] && towlower(p[1]) == towlower(c)) return menuBarOpen(owner, i, true), true;
        }
    }
    return false;
}

// --- The self-test: the menus driven as a user does, with posted mouse and key messages (no real input, nothing on
// the screen: in test runs the menus are off the screen). Returns one line for each case, "yes" or "NO".
namespace menu_e2e {

struct Case {
    const char* name;
    std::vector<std::function<void()>> steps;  // one each 40 ms, inside the menu's loop
    int want;                                   // the id picked (0: none)
};
std::vector<std::function<void()>>* gSteps = nullptr;
size_t gStep = 0;
std::string gNote;

POINT itemCenter(size_t level, int item) {  // in the first menu's client coordinates (the capture window)
    PaintedMenu& m = *gMenuLoop->stack[level];
    const RECT& r = m.items[size_t(item)].r;
    POINT p{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    ClientToScreen(m.wnd, &p);
    ScreenToClient(gMenuLoop->stack[0]->wnd, &p);
    return p;
}
void mouse(UINT msg, POINT p) {
    PostMessageW(gMenuLoop->stack[0]->wnd, msg, msg == WM_MOUSEMOVE ? 0 : MK_LBUTTON, MAKELPARAM(p.x, p.y));
}
void click(size_t level, int item) {
    const POINT p = itemCenter(level, item);
    mouse(WM_MOUSEMOVE, p), mouse(WM_LBUTTONDOWN, p), mouse(WM_LBUTTONUP, p);
}
void key(UINT vk) {
    const LPARAM sc = LPARAM(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
    PostMessageW(gMenuLoop->stack[0]->wnd, WM_KEYDOWN, vk, sc | 1);
    PostMessageW(gMenuLoop->stack[0]->wnd, WM_KEYUP, vk, sc | LPARAM(0xC0000001));
}
void expectOpen(size_t levels) {
    const size_t n = gMenuLoop ? gMenuLoop->stack.size() : 0;
    if (n != levels) gNote += " (menus open: " + std::to_string(n) + ", want " + std::to_string(levels) + ")";
}
void expectHot(int item) {
    if (!gMenuLoop || gMenuLoop->stack.back()->hot != item) gNote += " (item under the keys: " + std::to_string(gMenuLoop ? gMenuLoop->stack.back()->hot : -9) + ")";
}

bool gSinkClosed = false;
LRESULT CALLBACK sinkProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) return gSinkClosed = true, 0;
    return DefWindowProcW(h, m, w, l);
}

std::vector<std::string> run(HWND owner) {
    // Items: 0 Cut, 1 Copy, 2 Paste (gray), 3 separator, 4 Show the grid (checked), 5 Owner > (Player 1, Player 2)
    auto make = [] {
        HMENU m = CreatePopupMenu(), sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, 9101, L"Player &1");
        AppendMenuW(sub, MF_STRING, 9102, L"Player &2");
        AppendMenuW(m, MF_STRING, 9001, L"Cu&t\tCtrl+X");
        AppendMenuW(m, MF_STRING, 9002, L"&Copy\tCtrl+C");
        AppendMenuW(m, MF_STRING | MF_GRAYED, 9003, L"&Paste\tCtrl+V");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_CHECKED, 9004, L"&Show the grid");
        AppendMenuW(m, MF_POPUP, UINT_PTR(sub), L"&Owner");
        return m;
    };
    static HWND sink = nullptr;  // a window that takes the WM_CLOSE of the last case
    if (!sink) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = sinkProc, wc.hInstance = app.inst, wc.lpszClassName = L"StarDraftMenuTestSink";
        RegisterClassExW(&wc);
        sink = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, app.inst, nullptr);
    }
    static HWND sinkRef;
    sinkRef = sink;
    std::vector<Case> cases = {
        {"mouse: a click on an item picks it", {[] { click(0, 1); }}, 9002},
        {"mouse: a click on a gray item does nothing", {[] { click(0, 2); }, [] { expectOpen(1), key(VK_ESCAPE); }}, 0},
        {"mouse: a click on the separator does nothing",
         {[] {
              const RECT& r = gMenuLoop->stack[0]->items[3].r;
              const POINT p{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
              mouse(WM_LBUTTONDOWN, p), mouse(WM_LBUTTONUP, p);
          },
          [] { expectOpen(1), key(VK_ESCAPE); }},
         0},
        {"mouse: the submenu opens under the mouse, a click in it picks", {[] { mouse(WM_MOUSEMOVE, itemCenter(0, 5)); }, [] { expectOpen(2), click(1, 0); }}, 9101},
        {"mouse: moving to another item closes the submenu",
         {[] { mouse(WM_MOUSEMOVE, itemCenter(0, 5)); }, [] { expectOpen(2), mouse(WM_MOUSEMOVE, itemCenter(0, 0)); }, [] { expectOpen(1), key(VK_ESCAPE); }},
         0},
        {"mouse: a click outside closes the menus", {[] { mouse(WM_LBUTTONDOWN, POINT{-50, -50}); }}, 0},
        {"mouse: the right button up where the menu opened picks nothing", {[] { mouse(WM_RBUTTONUP, POINT{1, 1}); }, [] { expectOpen(1), key(VK_ESCAPE); }}, 0},
        {"keys: Down, Down, Enter picks the second item", {[] { key(VK_DOWN), key(VK_DOWN), key(VK_RETURN); }}, 9002},
        {"keys: Up from no item goes to the last item", {[] { key(VK_UP); }, [] { expectHot(5), key(VK_ESCAPE); }}, 0},
        {"keys: Down skips the separator", {[] { key(VK_DOWN), key(VK_DOWN), key(VK_DOWN), key(VK_DOWN); }, [] { expectHot(4), key(VK_ESCAPE); }}, 0},
        {"keys: an access letter picks its item", {[] { key('S'); }}, 9004},
        {"keys: the access letter of a gray item does nothing", {[] { key('P'); }, [] { expectOpen(1), key(VK_ESCAPE); }}, 0},
        {"keys: Right opens the submenu, Down, Enter picks in it", {[] { key(VK_UP), key(VK_RIGHT); }, [] { expectOpen(2), key(VK_DOWN), key(VK_RETURN); }}, 9102},
        {"keys: Esc in the submenu closes only the submenu", {[] { key('O'); }, [] { expectOpen(2), key(VK_ESCAPE); }, [] { expectOpen(1), key(VK_ESCAPE); }}, 0},
        {"keys: Left closes the submenu", {[] { key('O'); }, [] { expectOpen(2), key(VK_LEFT); }, [] { expectOpen(1), key(VK_ESCAPE); }}, 0},
        {"keys: Alt closes the menus", {[] { key(VK_MENU); }}, 0},
        {"another program in front closes the menus", {[] { gMenuForegroundTest = GetDesktopWindow(); }, [] {}, [] {}, [] {}, [] {}}, 0},
        {"a close message during the menu closes it and comes after", {[] { PostMessageW(sinkRef, WM_CLOSE, 0, 0); }}, 0},
        {"mouse: a right-click on an item opens feedback on it",
         {[] {
              gFeedbackHook = [](FeedbackView& v) { gNote += v.on() == L"the menu item Copy" ? "" : " (feedback not on Copy)", feedbackClose(); };
              const POINT p = itemCenter(0, 1);
              mouse(WM_RBUTTONDOWN, p), mouse(WM_RBUTTONUP, p);
          }},
         0},
    };
    std::vector<std::string> out;
    for (size_t k = 0; k < cases.size(); ++k) {
        Case& c = cases[k];
        HMENU m = make();
        gSteps = &c.steps, gStep = 0, gNote.clear(), gSinkClosed = false;
        const UINT_PTR t = SetTimer(nullptr, 0, 40, [](HWND, UINT, UINT_PTR, DWORD) {
            if (!gMenuLoop || !gSteps) return;
            if (gStep < gSteps->size()) (*gSteps)[gStep++]();
            else if (gStep++ > gSteps->size() + 25) gNote += " (timed out)", closeMenus();
        });
        const int got = paintedMenu(m, 200, 200, owner);
        KillTimer(nullptr, t);
        gFeedbackHook = nullptr;
        gMenuForegroundTest = nullptr, gSteps = nullptr;
        DestroyMenu(m);
        if (std::string(c.name).rfind("a close message", 0) == 0) {  // the close: sent again after the menu
            MSG msg;
            for (int i = 0; i < 20 && !gSinkClosed; ++i)
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) TranslateMessage(&msg), DispatchMessageW(&msg);
            if (!gSinkClosed) gNote += " (the close did not come after)";
        }
        const bool ok = got == c.want && gNote.empty();
        out.push_back(std::string("menu: ") + c.name + ": " + (ok ? "yes" : "NO (picked " + std::to_string(got) + ", want " + std::to_string(c.want) + ")" + gNote));
    }
    // The menu bar's menus (the main window's bar; nothing is picked, so no command runs).
    struct BarCase {
        const char* name;
        std::function<void()> start;
        std::vector<std::function<void()>> steps;
    };
    static int viewIndex;
    viewIndex = -1;
    for (int i = 0; i < GetMenuItemCount(GetMenu(owner)); ++i) {
        wchar_t buf[64] = L"";
        GetMenuStringW(GetMenu(owner), UINT(i), buf, 64, MF_BYPOSITION);
        if (!wcscmp(buf, L"&View")) viewIndex = i;
    }
    static HWND barOwner;
    barOwner = owner;
    std::vector<BarCase> bars = {
        {"bar: Right goes to the next menu of the bar", [] { menuBarOpen(barOwner, 0, true); },
         {[] { key(VK_RIGHT); }, [] { gMenuBarOpen == 1 ? void() : void(gNote += " (menu " + std::to_string(gMenuBarOpen) + ")"), key(VK_ESCAPE); }}},
        {"bar: Alt+V opens the View menu", [] { menuBarKey(barOwner, L'v'); },
         {[] { gMenuBarOpen == viewIndex ? void() : void(gNote += " (menu " + std::to_string(gMenuBarOpen) + ")"), key(VK_ESCAPE); }}},
        {"bar: F10 opens the first menu", [] { SendMessageW(barOwner, WM_SYSKEYUP, VK_F10, 0), SendMessageW(barOwner, WM_SYSCOMMAND, SC_KEYMENU, 0); },
         {[] { gMenuBarOpen == 0 ? void() : void(gNote += " (menu " + std::to_string(gMenuBarOpen) + ")"), key(VK_ESCAPE); }}},
        {"bar: Alt alone opens no menu", [] { SendMessageW(barOwner, WM_SYSKEYUP, VK_MENU, 0), SendMessageW(barOwner, WM_SYSCOMMAND, SC_KEYMENU, 0); }, {}},
        {"bar: the mouse over another menu's name opens it",
         [] { menuBarOpen(barOwner, 0, false); },
         {[] {
              RECT r{};
              GetMenuItemRect(barOwner, GetMenu(barOwner), 2, &r);
              POINT p{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
              ScreenToClient(gMenuLoop->stack[0]->wnd, &p);
              mouse(WM_MOUSEMOVE, p);
          },
          [] { gMenuBarOpen == 2 ? void() : void(gNote += " (menu " + std::to_string(gMenuBarOpen) + ")"), key(VK_ESCAPE); }}},
    };
    for (auto& c : bars) {
        gSteps = &c.steps, gStep = 0, gNote.clear();
        const UINT_PTR t = SetTimer(nullptr, 0, 40, [](HWND, UINT, UINT_PTR, DWORD) {
            if (!gMenuLoop || !gSteps) return;
            if (gStep < gSteps->size()) (*gSteps)[gStep++]();
            else if (gStep++ > gSteps->size() + 25) gNote += " (timed out)", closeMenus();
        });
        c.start();
        KillTimer(nullptr, t);
        gSteps = nullptr;
        const bool ok = gNote.empty() && gMenuBarOpen == -1;
        out.push_back(std::string("menu: ") + c.name + ": " + (ok ? "yes" : "NO" + gNote));
    }
    return out;
}

}  // namespace menu_e2e
