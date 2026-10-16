// Feedback (included into main.cpp after the New project dialog). A right click on almost anything gives "Report
// feedback on <the thing>...": every context menu goes through popupMenu(), which adds the item; where a right click
// opens no menu, the main loop opens one with only that item. The item opens the feedback dialog (core/views_feedback.inc
// is its view; feedback_window.inc is its window, included after the windows' code). Send files it on GitHub as an issue
// (label "feedback"), with the same token as crash reports (crash.inc). Feedback that could not go waits in
// %LOCALAPPDATA%\StarDraft\feedback\ and goes at the next start.

HFONT glyphFont(int px);
void uiIcon(HDC dc, RECT r, UiIcon icon, COLORREF c, int px);
constexpr UINT kFeedbackItem = 0xFEED;
bool gMenuShown = false;      // a menu opened while a right click was handled
std::wstring gFeedbackOn;     // what the menu is about, when the window that opens it knows better (else found)
// A painted pane names what is at a point: it answers WM_FEEDBACK_NAME (lp: FeedbackQuery*) by setting `name`.
constexpr UINT WM_FEEDBACK_NAME = WM_APP + 31;
struct FeedbackQuery {
    POINT pt;
    std::wstring name;
};

int paintedMenu(HMENU h, int x, int y, HWND owner);  // context_menu.inc

// The dialog (its view, the one open now; the tests see it through the hook) and its window (feedback_window.inc).
FeedbackView* gFbView = nullptr;
unsigned gFbSerial = 0;  // counts the dialog's opens: a send's result goes to the dialog that sent it, not to a later one
void (*gFeedbackHook)(FeedbackView&) = nullptr;  // tests: sees the dialog, then closes it
HWND feedbackWindow();                        // the dialog's window (nullptr: none)
void feedbackClose();                         // closes the dialog
void showFeedbackDialog(const std::wstring& on);

// (feedbackShort, the files and the sending are core/feedback_send.inc.)

// What is at `pt` in window `w`: a tooltip's text there, a control's text or item, else the pane and the workspace.
std::wstring feedbackThingIn(HWND w, POINT pt) {
    FeedbackQuery q{pt, L""};
    SendMessageW(w, WM_FEEDBACK_NAME, 0, LPARAM(&q));
    if (q.name.empty() && (GetWindowLongW(w, GWL_STYLE) & WS_CHILD))
        if (HWND parent = GetParent(w)) {
            FeedbackQuery pq{pt, L""};
            MapWindowPoints(w, parent, &pq.pt, 1);
            SendMessageW(parent, WM_FEEDBACK_NAME, 0, LPARAM(&pq));
            q.name = pq.name;
        }
    if (!q.name.empty()) return feedbackShort(q.name);
    // A tooltip that has a tool there (the kit's widgets, the rail, the tool buttons).
    struct Find {
        HWND target;
        POINT pt;
        std::wstring text;
    } f{w, pt, L""};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND tip, LPARAM lp) -> BOOL {
            auto& f = *reinterpret_cast<Find*>(lp);
            wchar_t cls[32] = L"";
            GetClassNameW(tip, cls, 32);
            if (_wcsicmp(cls, TOOLTIPS_CLASSW) != 0) return TRUE;
            TTHITTESTINFOW hi{};
            hi.hwnd = f.target, hi.pt = f.pt, hi.ti.cbSize = sizeof(TOOLINFOW);
            if (!SendMessageW(tip, TTM_HITTESTW, 0, LPARAM(&hi))) return TRUE;
            wchar_t buf[512] = L"";
            hi.ti.lpszText = buf;
            SendMessageW(tip, TTM_GETTEXTW, 512, LPARAM(&hi.ti));
            if (!buf[0]) return TRUE;
            f.text = buf;
            return FALSE;
        },
        LPARAM(&f));
    if (!f.text.empty()) return feedbackShort(f.text);
    wchar_t cls[64] = L"";
    GetClassNameW(w, cls, 64);
    if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
        LVHITTESTINFO hi{};
        hi.pt = pt;
        if (ListView_SubItemHitTest(w, &hi) >= 0 && hi.iItem >= 0) {
            wchar_t buf[256] = L"";
            ListView_GetItemText(w, hi.iItem, 0, buf, 256);
            if (buf[0]) return feedbackShort(buf);
        }
    }
    if (_wcsicmp(cls, WC_TREEVIEWW) == 0) {
        TVHITTESTINFO hi{};
        hi.pt = pt;
        if (HTREEITEM it = TreeView_HitTest(w, &hi)) {
            wchar_t buf[256] = L"";
            TVITEMW tv{};
            tv.mask = TVIF_TEXT, tv.hItem = it, tv.pszText = buf, tv.cchTextMax = 256;
            TreeView_GetItem(w, &tv);
            if (buf[0]) return feedbackShort(buf);
        }
    }
    if (_wcsicmp(cls, WC_LISTBOXW) == 0) {
        const LRESULT r = SendMessageW(w, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        if (!HIWORD(r) && SendMessageW(w, LB_GETTEXTLEN, LOWORD(r), 0) < 255) {
            wchar_t buf[256] = L"";
            SendMessageW(w, LB_GETTEXT, LOWORD(r), LPARAM(buf));
            if (buf[0]) return feedbackShort(buf);
        }
    }
    if (_wcsicmp(cls, WC_BUTTONW) == 0 || _wcsicmp(cls, WC_STATICW) == 0 || _wcsicmp(cls, WC_COMBOBOXW) == 0)
        if (const std::wstring t = feedbackShort(windowText(w)); !t.empty()) return t;
    // The pane, by its window class (the painted panes).
    static const std::pair<const wchar_t*, const wchar_t*> panes[] = {
        {L"StarDraftViewport", L"the map"},        {L"StarDraftMinimap", L"the minimap"},       {L"StarDraftDataList", L"the data list"},
        {L"StarDraftDataForm", L"the data form"},  {L"StarDraftTrigPane", L"the trigger"},
        {L"StarDraftTrigEditor", L"the triggers"},
    };
    if (_wcsicmp(cls, MSFTEDIT_CLASS) == 0) return L"the code";
    for (HWND p = w; p; p = GetParent(p)) {
        GetClassNameW(p, cls, 64);
        for (const auto& [c, name] : panes)
            if (_wcsicmp(cls, c) == 0) return name;
    }
    if (const HWND root = GetAncestor(w, GA_ROOT); root && root != app.main) {  // a window over the main window: its title
        const std::wstring title = windowText(root);
        if (!title.empty()) return title;
    }
    return feedbackWorkspaceName(gWorkspace);  // (core/feedback_send.inc)
}

std::wstring feedbackThing(POINT screen) {
    if (!gFeedbackOn.empty()) return gFeedbackOn;
    HWND w = WindowFromPoint(screen);
    if (!w) return L"StarDraft 2";
    ScreenToClient(w, &screen);
    return feedbackThingIn(w, screen);
}

// The feedback glyph in kFeedback, `px` pixels square, with alpha: for menu items.
HBITMAP feedbackBitmap(int px) {
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), px, -px, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return DeleteDC(dc), nullptr;
    HGDIOBJ ob = SelectObject(dc, bmp);
    std::memset(bits, 0, size_t(px) * px * 4);
    uiIcon(dc, {0, 0, px, px}, kFeedbackIcon, RGB(255, 255, 255), px);
    GdiFlush();
    // The white glyph's coverage becomes the alpha of kFeedback (premultiplied).
    auto* p = static_cast<uint8_t*>(bits);
    for (int i = 0; i < px * px; ++i, p += 4) {
        const int a = std::max({p[0], p[1], p[2]});
        p[0] = uint8_t(GetBValue(kFeedback) * a / 255), p[1] = uint8_t(GetGValue(kFeedback) * a / 255), p[2] = uint8_t(GetRValue(kFeedback) * a / 255);
        p[3] = uint8_t(a);
    }
    SelectObject(dc, ob);
    DeleteDC(dc);
    return bmp;
}


// Writes the feedback to a file (it waits there if it cannot go now); returns the file. The dialog's Send does it.
fs::path feedbackWrite(const std::wstring& on, const std::wstring& typed) {
    return feedbackWriteFile(on, typed, crash::gWhere ? crash::gWhere() : L"", crash::gBuild, crash::windowsVersion());
}


// Every context menu: the menu `m` with "Report feedback on ..." at the end. Returns the item picked (0 for none,
// or for the feedback item: its dialog is done then).
int popupMenu(HMENU m, UINT flags, int x, int y, HWND owner) {
    gMenuShown = true;
    const std::wstring on = feedbackThing(POINT{x, y});
    gFeedbackOn.clear();
    if (GetMenuItemCount(m) > 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kFeedbackItem, (L"Report feedback on " + on + L"\x2026").c_str());
    static HBITMAP glyph = feedbackBitmap(GetSystemMetricsForDpi(SM_CYMENUCHECK, UINT(app.dpi)));
    MENUITEMINFOW mi{sizeof mi};
    mi.fMask = MIIM_BITMAP, mi.hbmpItem = glyph;
    SetMenuItemInfoW(m, kFeedbackItem, FALSE, &mi);
    (void)flags;
    const int pick = paintedMenu(m, x, y, owner);  // (context_menu.inc)
    if (pick != int(kFeedbackItem)) return pick;
    showFeedbackDialog(on);
    return 0;
}

// The window a right click on `w` gives feedback from: the main window, or a window of StarDraft over it (Unit
// properties, a location, Program options; GitHub feedback #97) that takes input now; not the feedback window itself.
HWND feedbackRoot(HWND w) {
    const HWND root = GetAncestor(w, GA_ROOT);
    if (!root || !IsWindowEnabled(root) || (root != app.main && GetWindow(root, GW_OWNER) != app.main)) return nullptr;
    return gFbView && root == feedbackWindow() ? nullptr : root;
}

// The main loop, after a right click was handled: when nothing opened a menu, the menu with only the feedback item.
// The map view (a right click there cancels or moves) and text boxes (their own menu) are left alone.
void feedbackAfterRightClick(const MSG& msg) {
    if (std::exchange(gMenuShown, false) || gBackground) return;
    wchar_t cls[64] = L"";
    GetClassNameW(msg.hwnd, cls, 64);
    if (msg.hwnd == app.view || _wcsicmp(cls, WC_EDITW) == 0 || _wcsicmp(cls, MSFTEDIT_CLASS) == 0) return;
    const HWND root = feedbackRoot(msg.hwnd);
    if (!root) return;
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    popupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, root);
    DestroyMenu(m);
}

void dialogMessage(MSG& msg, HWND dlg) {
    const bool rightClick = msg.message == WM_RBUTTONUP;
    if (rightClick) gMenuShown = false;
    if (!IsDialogMessageW(dlg, &msg)) TranslateMessage(&msg), DispatchMessageW(&msg);
    if (rightClick) feedbackAfterRightClick(msg);
}
