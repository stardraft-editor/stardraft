// Feedback (included into main.cpp after the New project dialog). A right click on almost anything gives "Report
// feedback on <the thing>...": every context menu goes through popupMenu(), which adds the item; where a right click
// opens no menu, the main loop opens one with only that item. The item opens a dialog to type the feedback; Send
// files it on GitHub as an issue (label "feedback"), with the same token as crash reports (crash.inc). Feedback that
// could not go waits in %LOCALAPPDATA%\StarDraft\feedback\ and goes at the next start.

HFONT glyphFont(int px);
void uiIcon(HDC dc, RECT r, UiIcon icon, COLORREF c, int px);
constexpr UINT WM_FEEDBACK_SENT = WM_APP + 30;  // wp: 1 sent, 0 kept to send later
constexpr UINT kFeedbackItem = 0xFEED;
bool gMenuShown = false;      // a menu opened while a right click was handled
std::wstring gFeedbackOn;     // what the menu is about, when the window that opens it knows better (else found)
// A painted pane names what is at a point: it answers WM_FEEDBACK_NAME (lp: FeedbackQuery*) by setting `name`.
constexpr UINT WM_FEEDBACK_NAME = WM_APP + 31;
struct FeedbackQuery {
    POINT pt;
    std::wstring name;
};

// A short name for a text: its first line, at most 48 characters.
std::wstring feedbackShort(std::wstring t) {
    if (const size_t n = t.find_first_of(L"\r\n\t"); n != std::wstring::npos) t.resize(n);
    std::erase(t, L'&');
    while (!t.empty() && (t.back() == L' ' || t.back() == L':' || t.back() == L'.')) t.pop_back();
    if (t.size() > 48) t = t.substr(0, 47) + L"\x2026";
    return t;
}

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
        {L"StarDraftDataForm", L"the data form"},  {L"StarDraftTrigPane", L"the trigger"},       {L"StarDraftQuickPick", L"the quick picker"},
        {L"StarDraftTrigEditor", L"the triggers"}, {L"StarDraftProjectPage", L"the Project page"},
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
    static const wchar_t* ws[] = {L"the Map workspace", L"the Data workspace", L"the Triggers workspace", L"the Assets workspace",
                                  L"the Project page", L"the Briefing workspace", L"the Assets workspace"};
    return ws[int(gWorkspace)];
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

// --- The dialog ------------------------------------------------------------------------------------------------------

struct FeedbackDialog {
    HWND wnd = nullptr, text = nullptr, note = nullptr;
    std::wstring on, typed;
    bool accepted = false;
};
FeedbackDialog* gFbDlg = nullptr;
void (*gFeedbackHook)(HWND) = nullptr;  // tests: sees the dialog, then closes it

std::mutex gSentMutex;  // sent.tsv: written by the thread that sends, read by the window
fs::path feedbackFolder() { return crash::folder().parent_path() / "feedback"; }

// The feedback sent from this computer (sent.tsv), the newest first: the issue number, its page, the title.
struct SentFeedback {
    long number;
    std::wstring page, title;
};
std::vector<SentFeedback> sentFeedback() {
    std::lock_guard<std::mutex> lock(gSentMutex);
    std::vector<SentFeedback> out;
    std::ifstream in(feedbackFolder() / "sent.tsv", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        out.push_back({std::strtol(line.c_str(), nullptr, 10), widen(line.substr(a + 1, b - a - 1)), widen(line.substr(b + 1))});
    }
    std::reverse(out.begin(), out.end());
    return out;
}

int paintedMenu(HMENU h, int x, int y, HWND owner);  // context_menu.inc

// The text box: Ctrl+Enter sends (as the Send button does, when there is text).
LRESULT CALLBACK feedbackTextProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (msg == WM_KEYDOWN && wp == VK_RETURN && ctrl) {
        const HWND dlg = GetParent(hwnd);
        if (IsWindowEnabled(GetDlgItem(dlg, IDOK))) PostMessageW(dlg, WM_COMMAND, IDOK, 0);
        return 0;
    }
    if (msg == WM_CHAR && wp == 0x0A && ctrl) return 0;  // Ctrl+Enter's line feed: not typed
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK feedbackDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    FeedbackDialog* d = gFbDlg;
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark), titleColors(hwnd);
        d->wnd = hwnd;
        const int pad = scale(ui::kDialogPad), w = scale(560), editH = scale(200), bw = scale(ui::kButtonMinW), bh = scale(ui::kButtonH);
        int y = pad;
        const int glyphW = scale(26);  // the glyph is painted there (WM_PAINT)
        HWND label = CreateWindowExW(0, WC_STATICW, (L"Feedback on " + d->on).c_str(), WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, pad + glyphW, y + scale(1),
                                     w - glyphW, scale(20), hwnd, nullptr, app.inst, nullptr);
        SendMessageW(label, WM_SETFONT, WPARAM(app.font), TRUE);
        y += scale(24);
        d->text = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL, pad, y, w, editH, hwnd,
                                  HMENU(1501), app.inst, nullptr);
        SendMessageW(d->text, WM_SETFONT, WPARAM(app.font), TRUE);
        SendMessageW(d->text, EM_SETLIMITTEXT, 20000, 0);
        SetWindowSubclass(d->text, feedbackTextProc, 1, 0);
        darkControl(d->text, L"DarkMode_Explorer");
        y += editH + scale(8);
        d->note = CreateWindowExW(0, WC_STATICW, L"Feedback is anonymous. Do not include sensitive or private information. Ctrl+Enter sends it.", WS_CHILD | WS_VISIBLE, pad, y, w,
                                  scale(20), hwnd, nullptr, app.inst, nullptr);
        SendMessageW(d->note, WM_SETFONT, WPARAM(app.font), TRUE);
        y += scale(20) + scale(24);
        const wchar_t* names[2] = {L"Send", L"Cancel"};
        const int ids[2] = {IDOK, IDCANCEL};
        for (int i = 0; i < 2; ++i) {
            HWND b = CreateWindowExW(0, WC_BUTTONW, names[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                     pad + w - (2 - i) * bw - (1 - i) * scale(8), y, bw, bh, hwnd, HMENU(INT_PTR(ids[i])), app.inst, nullptr);
            SendMessageW(b, WM_SETFONT, WPARAM(app.font), TRUE);
            darkControl(b, L"DarkMode_Explorer");
        }
        ui::nativeButtonsRight({ui::dlgButton(hwnd, IDOK), ui::dlgButton(hwnd, IDCANCEL)}, pad + w, y);
        EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);  // nothing typed yet
        RECT r{0, 0, pad + w + pad, y + bh + pad};
        AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, app.dpi);
        SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetFocus(d->text);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == 1501 && HIWORD(wp) == EN_CHANGE) EnableWindow(GetDlgItem(hwnd, IDOK), GetWindowTextLengthW(d->text) > 0);
        if (id == IDOK || id == IDCANCEL) {
            d->typed = windowText(d->text);
            d->accepted = id == IDOK && !d->typed.empty();
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT g{scale(12), scale(10), scale(12) + scale(20), scale(10) + scale(20)};
        uiIcon(dc, g, kFeedbackIcon, kFeedback, scale(16));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = HDC(wp);
        SetTextColor(dc, HWND(lp) == d->note ? kDim : kText);
        SetBkColor(dc, msg == WM_CTLCOLORSTATIC ? kBg : kCtl);
        return LRESULT(msg == WM_CTLCOLORSTATIC ? app.bgBrush : app.ctlBrush);
    }
    case WM_CLOSE:
        d->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


// Sends one waiting feedback file (line 1: the title; then the body); deleted when it arrived.
bool sendFeedbackFile(const fs::path& file) {
    if (crash::token().empty()) return false;
    std::ifstream in(file, std::ios::binary);
    std::string title, body;
    std::getline(in, title);
    body.assign(std::istreambuf_iterator<char>(in), {});
    in.close();
    std::string reply;
    const HttpReply r = httpRequest(L"https://api.github.com/repos/" + std::wstring(crash::kRepo) + L"/issues", L"POST", L"application/vnd.github+json", crash::token(),
                                    "{\"title\":" + crash::json(title) + ",\"body\":" + crash::json(body) + ",\"labels\":[\"feedback\"]}", &reply, nullptr, true);
    // A reply that was not read completely, or has no link, keeps the file: the next start sends it again (a possible
    // second issue is better than a lost note).
    if (r.status != 201 || !r.complete) return false;
    // The issue's number and page (the first "html_url" of the reply is the issue's), kept in sent.tsv: one line each,
    // "number<TAB>page<TAB>title", so the feedback window can list them.
    long number = 0;
    std::string page;
    if (const size_t a = reply.find("\"number\":"); a != std::string::npos) number = std::strtol(reply.c_str() + a + 9, nullptr, 10);
    if (const size_t a = reply.find("\"html_url\":\""); a != std::string::npos) page = reply.substr(a + 12, reply.find('"', a + 12) - a - 12);
    if (number <= 0 || page.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(gSentMutex);
        std::ofstream(feedbackFolder() / "sent.tsv", std::ios::binary | std::ios::app) << number << '\t' << page << '\t' << title << '\n';
    }
    std::error_code ec;
    fs::remove(file, ec);
    return true;
}

// A note that waits to go: a .txt file. The list of the sent ones is not one (sent.tsv; sent.txt in the builds of
// 2026-10-11, which sent that list as an issue).
bool waitingFeedbackFile(const fs::path& p) { return p.extension() == ".txt" && p.filename() != "sent.txt"; }

// At start, on a thread: feedback that could not go before.
void sendPendingFeedback() {
    if (crash::token().empty()) return;
    std::thread([] {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(feedbackFolder(), ec))
            if (waitingFeedbackFile(e.path())) sendFeedbackFile(e.path());
    }).detach();
}

void showFeedbackDialog(const std::wstring& on) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = feedbackDialogProc;
        wc.hInstance = app.inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = app.bgBrush;
        wc.lpszClassName = L"StarDraftFeedback";
        RegisterClassExW(&wc);
        registered = true;
    }
    FeedbackDialog d;
    d.on = on;
    gFbDlg = &d;
    const HWND from = GetActiveWindow();  // the window the feedback is from
    RECT mr;
    GetWindowRect(app.main, &mr);
    const int w = scale(600), h = scale(340);
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT | (gBackground ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0), L"StarDraftFeedback",
                               L"Report feedback", WS_CAPTION | WS_SYSMENU | (gBackground ? 0 : WS_VISIBLE), mr.left + (mr.right - mr.left - w) / 2,
                               mr.top + (mr.bottom - mr.top - h) / 2, w, h, app.main, nullptr, app.inst, nullptr);
    if (gBackground) ShowWindow(wnd, SW_SHOWNOACTIVATE);
    // From a window over the main window (Unit properties): that one waits too, and is active again after.
    const HWND over = from && from != app.main && from != wnd && IsWindowEnabled(from) ? from : nullptr;
    const bool mainOn = IsWindowEnabled(app.main) != FALSE;
    if (over) EnableWindow(over, FALSE);
    EnableWindow(app.main, FALSE);
    if (gFeedbackHook) gFeedbackHook(wnd);
    MSG msg;
    while (IsWindow(wnd) && GetMessageW(&msg, nullptr, 0, 0))
        if (!IsDialogMessageW(wnd, &msg)) TranslateMessage(&msg), DispatchMessageW(&msg);
    if (mainOn) EnableWindow(app.main, TRUE);
    if (over) EnableWindow(over, TRUE);
    if (!gBackground) SetForegroundWindow(over ? over : app.main);
    gFbDlg = nullptr;
    if (!d.accepted) return;
    // Written to a file first: it waits there if it cannot go now.
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[40];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d-%03d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    std::error_code ec;
    fs::create_directories(feedbackFolder(), ec);
    const fs::path file = feedbackFolder() / (std::string(stamp) + ".txt");
    std::string typed = crash::utf8(d.typed);
    std::erase(typed, '\r');
    const std::string summary = crash::utf8(feedbackShort(d.typed));
    std::ofstream(file, std::ios::binary) << "Feedback on " << crash::utf8(on) << ": " << summary << "\n"
                                          << typed << "\n\n---\nOn: " << crash::utf8(on) << "\nWorkspace: " << crash::utf8(crash::gWhere ? crash::gWhere() : L"")
                                          << "\nStarDraft " STARDRAFT_VERSION_NAME ", " << crash::gBuild << ", commit " STARDRAFT_COMMIT "\n" << crash::windowsVersion() << "\n";
    if (crash::token().empty()) {
        setMessage(L"This build cannot send feedback. StarDraft 2 keeps it in " + file.wstring() + L".");
        return;
    }
    setMessage(L"Sending the feedback\x2026");
    std::thread([file, main = app.main] { PostMessageW(main, WM_FEEDBACK_SENT, sendFeedbackFile(file) ? 1 : 0, 0); }).detach();
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
    wchar_t cls[64] = L"";
    GetClassNameW(root, cls, 64);
    return _wcsicmp(cls, L"StarDraftFeedback") == 0 ? nullptr : root;
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
