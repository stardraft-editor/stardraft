// File > New EUD project: the Windows host of the dialog (core/views_new_project.inc: the view and its kit dialog). It
// gives the defaults (the open map's name, the working folder), runs the dialog over the main window through the
// program's anchor (options_window.inc) and makes the project (createProject, main.cpp). Included by main.cpp after
// options_window.inc.

NewProjectView* gNewProjectView = nullptr;  // the dialog's content while it is open (the self-test and the datashots)
// Self-test: runs in the open dialog (a timer of the thread: the dialog's own loop dispatches it), with its window.
void (*gProjectHook)(NewProjectView&, HWND) = nullptr;
VOID CALLBACK projectHookTimer(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    if (gNewProjectView && gProjectHook) gProjectHook(*gNewProjectView, ui::Win32Host::windowOf(*gNewProjectView));
}

void showProjectDialog() {
    NewProjectStart start;
    start.name = L"MyMap";
    if (app.ed && !app.ed->path().empty()) start.name = app.ed->path().stem().wstring();
    start.location = defaultProjectsFolder().make_preferred().wstring();
    if (app.ed && app.ed->isOpen()) start.openMap = docTitle(gActive, false);
    NewProjectChoice c;
    gMainAnchor.host.ownerWindow(app.main);
    if (gProjectHook) SetTimer(nullptr, 0, 500, projectHookTimer);
    const bool created = runNewProjectDialog(gMainAnchor.view, start, c, gNewProjectView);
    if (!gBackground) SetForegroundWindow(app.main);
    if (!created) return;
    std::string err;
    if (!createProject(fs::path(c.location), narrow(c.name), c.fromOpenMap, err, fs::path(c.output)))
        themedMessageBox(app.main, widen(err).c_str(), L"Could not create the project", MB_ICONERROR);
}

// The datashots' picture of the dialog: captured to `file`, then closed with Cancel.
bool capture(const fs::path& file, HWND wnd);  // (the self-test's tools, main.cpp)
void newProjectShot(const fs::path& file) {
    static fs::path shot;
    shot = file;
    gProjectHook = [](NewProjectView& v, HWND w) {
        if (w) capture(shot, w);
        v.cancel();
    };
    showProjectDialog();
    gProjectHook = nullptr;
}
