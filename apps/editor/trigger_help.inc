// Part of main.cpp: help and fixes for the trigger editor.
// - Block help (T.7): F1 on a block in the pane or in the block picker opens a page for the block: what it does,
//   its values and their types, an example, what to watch out for, its effects and cost. The page follows the
//   selected block while it is open. It is painted from the block library, so it works offline.
// - Cost fixes (T.8): a heavy trigger shows the changes that make it cheaper, each with Apply.
// - Build errors (T.1): Build keeps the block of each line of generated/triggers.eps; a euddraft error in a line
//   selects that block in the Triggers workspace.

void trigChanged();
void trigRefreshTree(std::vector<int> select);

// --- Block help --------------------------------------------------------------------------------------------------

struct BlockHelp {
    HWND wnd = nullptr;
    std::string id;
    int scroll = 0, contentH = 0;
};
BlockHelp gBlockHelp;

// A paragraph with word wrap; the y under it.
int blockHelpText(HDC dc, int x, int y, int right, const std::wstring& text, COLORREF color) {
    RECT r{x, y, right, y};
    DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    r.right = right;
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    return r.bottom + scale(8);
}

void paintBlockHelp(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP back = CreateCompatibleBitmap(screen, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(dc, back), of = SelectObject(dc, app.font);
    FillRect(dc, &rc, app.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    FormLayout l = formLayout(rc);
    l.colRight = rc.right - l.x0;
    const int x = l.x0, right = l.colRight;
    int y = scale(14) - gBlockHelp.scroll;
    if (const TriggerDef* d = TriggerDef::find(gBlockHelp.id)) {
        std::wstring sub = widen(d->category) + L"  \x00B7  " + blockKindText(*d);
        if (d->tier == "advanced") sub += L"  \x00B7  advanced";
        y = ui::title(dc, x, y, right, nullptr, widen(d->title), sub);
        y = blockHelpText(dc, x, y, right, blockHelpSentence(*d), kAccent);
        y = sectionHeader(dc, l, y, L"What it does");
        y = blockHelpText(dc, x, y, right, widen(d->docs), kText);
        if (!d->params.empty()) {
            y = sectionHeader(dc, l, y, L"Values");
            for (const auto& p : d->params) {
                bool clear = true;
                const std::wstring line = blockHelpValueLine(p, clear);
                y = blockHelpText(dc, x, y, right, line, clear ? kText : trigPurple());
            }
        }
        if (!d->lists.empty()) {
            y = sectionHeader(dc, l, y, L"Holds");
            y = blockHelpText(dc, x, y, right, blockHelpLists(*d), kText);
        }
        y = sectionHeader(dc, l, y, L"Example");
        y = blockHelpText(dc, x, y, right, d->example.empty() ? L"-" : widen(d->example), kText);
        if (!d->pitfalls.empty()) {
            y = sectionHeader(dc, l, y, L"Watch out");
            y = blockHelpText(dc, x, y, right, widen(d->pitfalls), kWarn);
        }
        const auto effects = blockEffectTexts(*d);
        y = sectionHeader(dc, l, y, L"Effects and cost");
        for (const auto& e : effects) y = blockHelpText(dc, x, y, right, e, kText);
        y = blockHelpText(dc, x, y, right, blockHelpCost(*d), kDim);
    }
    gBlockHelp.contentH = y + gBlockHelp.scroll;
    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, of), SelectObject(dc, ob);
    DeleteObject(back);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK blockHelpProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: return paintBlockHelp(hwnd), 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int maxScroll = std::max(0, gBlockHelp.contentH - int(rc.bottom) + scale(14));
        gBlockHelp.scroll = std::clamp(gBlockHelp.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(40) / WHEEL_DELTA, 0, maxScroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_F1) return DestroyWindow(hwnd), 0;
        break;
    case WM_FEEDBACK_NAME: {
        const TriggerDef* d = TriggerDef::find(gBlockHelp.id);
        reinterpret_cast<FeedbackQuery*>(lp)->name = L"the help for " + (d ? widen(d->title) : std::wstring(L"a block"));
        return 0;
    }
    case WM_DESTROY: gBlockHelp.wnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Opens the help page for a block (or shows another block on it), beside `beside` (the pane, or the block picker). It
// does not take the focus: the arrow keys keep moving through the blocks, and the page follows.
void showBlockHelp(const std::string& id, HWND beside) {
    const TriggerDef* d = TriggerDef::find(id);
    if (!d) return;
    if (!gBlockHelp.wnd) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{sizeof wc};
            wc.lpfnWndProc = blockHelpProc, wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpszClassName = L"StarDraftBlockHelp";
            RegisterClassExW(&wc);
            registered = true;
        }
        RECT nr{};
        GetWindowRect(beside ? beside : app.main, &nr);
        const int w = scale(460), h = scale(600);
        int x = nr.right - w - scale(16), y = nr.top + scale(16);
        if (beside && beside != app.main && beside != gTE.pane) x = nr.right + scale(8), y = nr.top;  // the block picker: beside it
        gBlockHelp.wnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"StarDraftBlockHelp", L"Block help", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h,
                                         app.main, nullptr, app.inst, nullptr);
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(gBlockHelp.wnd, 20, &dark, sizeof dark), titleColors(gBlockHelp.wnd);
    }
    if (gBlockHelp.id != id) gBlockHelp.id = id, gBlockHelp.scroll = 0;
    SetWindowTextW(gBlockHelp.wnd, (L"Block help: " + widen(d->title)).c_str());
    ShowWindow(gBlockHelp.wnd, SW_SHOWNOACTIVATE);
    SetWindowPos(gBlockHelp.wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(gBlockHelp.wnd, nullptr, FALSE);
}

// While the page is open, it shows the block selected in the pane.
void blockHelpFollow(const std::string& id) {
    if (gBlockHelp.wnd && !id.empty() && id != gBlockHelp.id) showBlockHelp(id, nullptr);
}

// F1 in the pane: the selected block's page (F1 again, or Esc, closes it).
void trigPaneHelp() {
    bool close = false;
    const std::string id = trigPaneHelpPick(gBlockHelp.wnd != nullptr, gBlockHelp.id, close);
    if (close && gBlockHelp.wnd) DestroyWindow(gBlockHelp.wnd);
    if (!id.empty()) showBlockHelp(id, gTE.pane);
}

// --- Cost fixes ------------------------------------------------------------------------------------------------------

// (The fixes' Apply links, gTrigFixLinks, and their paint are in core/views_triggers_pane.inc.)

// (trigApplyCostFix is in core/triggers_model.inc.)

// --- Build errors --------------------------------------------------------------------------------------------------

// (The line map, trigCodeLineName, trigShowCodeLine and trigShowBuildError are in core/views_triggers_help.inc.)

// The pane scrolls to a block (trigShowCodeLine): the pane laid out now, then moved to it.
void trigRevealBlock(const TrigBlockRef& ref) {
    UpdateWindow(gTE.pane);
    RECT pc;
    GetClientRect(gTE.pane, &pc);
    trigScrollToBlock(ref, int(pc.bottom));
}
