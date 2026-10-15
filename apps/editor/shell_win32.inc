// The Windows host of the editor's seam (shell.h). Included by main.cpp before the main window's procedure; each function
// is the line it replaced in the code that called it.

// shellPost: the function on the heap, run and deleted by the UI thread (mainProc).
constexpr UINT WM_SHELL_POST = WM_APP + 40;  // lp: a std::function<void()>*, run and deleted by the receiver

void shellRelayout() { layout(); }
void shellMenusChanged() { updateMenus(); }
void shellStatusChanged() { gStatus.invalidate(); }
void shellViewSize(int& w, int& h) {
    RECT r;
    GetClientRect(app.view, &r);
    w = r.right, h = r.bottom;
}
void shellPost(std::function<void()> fn) {
    auto* f = new std::function<void()>(std::move(fn));
    if (!PostMessageW(app.main, WM_SHELL_POST, 0, LPARAM(f))) delete f;
}
