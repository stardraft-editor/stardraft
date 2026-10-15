// The Windows host of the editor's seam (shell.h). Included by main.cpp before the main window's procedure; each function
// is the line it replaced in the code that called it.

// shellPost: the function on the heap, run and deleted by the UI thread (mainProc).
constexpr UINT WM_SHELL_POST = WM_APP + 40;  // lp: a std::function<void()>*, run and deleted by the receiver

void shellRelayout() { layout(); }
void shellMenusChanged() { updateMenus(); }
void shellStatusChanged() { gStatus.invalidate(); }
bool shellLogOpen() { return gLog.wnd != nullptr; }
int shellUiFontLfHeight() {
    LOGFONTW lf{};
    GetObjectW(app.font, sizeof lf, &lf);
    return lf.lfHeight;
}
void shellViewSize(int& w, int& h) {
    RECT r;
    GetClientRect(app.view, &r);
    w = r.right, h = r.bottom;
}
void shellPost(std::function<void()> fn) {
    auto* f = new std::function<void()>(std::move(fn));
    if (!PostMessageW(app.main, WM_SHELL_POST, 0, LPARAM(f))) delete f;
}
void shellSprayTimer(bool on) {
    if (!app.view) return;
    if (on) SetTimer(app.view, kSprayTimer, 25, nullptr);
    else KillTimer(app.view, kSprayTimer);
}
void shellTrackMouseLeave() {
    TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, app.view, 0};
    TrackMouseEvent(&t);
}
bool shellCtrlOrShiftDown() { return GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0; }
bool shellClearFilter() {
    if (!app.filter) return false;
    gFilterBox.setText(L"");  // (the kit search box: its change runs, and the list is made again)
    return true;
}

unsigned shellDpi() { return app.dpi; }
void shellLogLine(const std::wstring& line) { logLine(line); }
bool shellHasStatusBar() { return app.status != nullptr; }
int shellSpriteCount() { return app.renderer.stats().sprites; }
void shellListSelect(int i) {
    SendMessageW(app.list, LB_SETSEL, FALSE, -1);
    if (i >= 0) SendMessageW(app.list, LB_SETSEL, TRUE, i), SendMessageW(app.list, LB_SETCARETINDEX, i, FALSE);
}
void shellListFill(const std::vector<std::wstring>& rows) {
    SendMessageW(app.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(app.list, LB_RESETCONTENT, 0, 0);
    for (const auto& r : rows) SendMessageW(app.list, LB_ADDSTRING, 0, LPARAM(r.c_str()));
    SendMessageW(app.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(app.list, nullptr, TRUE);
}
std::wstring shellFilterText() { return gFilterBox.text(); }
bool shellViewShown() { return app.view != nullptr; }
void shellToolsChanged() {
    if (app.tool) InvalidateRect(app.tool, nullptr, FALSE);
}
void shellOwnerSelect(int i) { SendMessageW(app.owner, CB_SETCURSEL, i, 0); }
void shellPaletteChanged() { unitPaletteRepaint(); }
void shellPaletteRebuilt() { rebuildUnitPalette(); }
std::wstring shellTr(const wchar_t* english) { return tr(english); }
std::vector<int>& shellPaletteChosen() { return gPal.chosen; }
