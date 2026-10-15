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
