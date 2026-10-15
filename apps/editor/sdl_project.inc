// The Project workspace on this system (the page is core/views_project.inc; its program parts are core/project_page.inc,
// the same as the Windows program's): the page fills the workspace, and the script preview sits over its right part
// when the </> button is on (as the Windows preview window over the page's). The page scrolls with the wheel (no bar).
// Also the project commands of the File menu here: Open project, New EUD project (the dialog, core/views_new_project.inc),
// Add a script, the project's folder and Project settings. Included by main_sdl.cpp after sdl_files.inc (decodeSdlSound)
// and sdl_new_page.inc.

// The workspace's view: the page, and the preview over its right part.
struct SdlProjectWorkspace final : draw::View {
    SdlProjectWorkspace() {
        add(gProjView, {0, 0, 0, 0});
        add(gProjPreview, {0, 0, 0, 0});
        gProjPreview.setVisible(false);
    }
    void resized() override {
        gProjView.setFrame({0, 0, width(), height()});
        projectPreviewRefresh(true);  // (placed, and its text read: the first size comes after the workspace showed)
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
};
SdlProjectWorkspace gSdlProject;

// The preview's place: the right part of the workspace (projectPreviewWidth), shown when it has a width.
int projectPreviewPlace() {
    const int w = gSdlProject.width(), h = gSdlProject.height();
    const int pw = w > 0 ? projectPreviewWidth(w) : 0;
    gProjPreview.setFrame({w - pw, 0, w, h});
    gProjPreview.setVisible(pw > 0);    return pw;
}

// The background music's length: the sound decoded as the Files workspace decodes it (sdl_files.inc).
uint32_t soundFileMs(const Bytes& data) {
    SdlSound d;
    return decodeSdlSound(data, d) ? d.ms : 0;
}

// The workspace shows: the active project's scripts and the preview are read again (the Windows program does it when
// it shows the page).
void sdlProjectShown() { triggersRefresh(); }

// File > Project > New EUD project...: the dialog over the main window, then the project from its choices (the open
// map, or a new blank one), as the Windows host (new_project_window.inc).
void showProjectDialogSdl() {
    if (!gRoot) return;
    NewProjectStart start;
    start.name = L"MyMap";
    if (gState.ed && !gState.ed->path().empty()) start.name = gState.ed->path().stem().wstring();
    start.location = defaultProjectsFolder().make_preferred().wstring();
    if (docOpen()) start.openMap = docTitle(gActive, false);
    NewProjectChoice c;
    NewProjectView* shown = nullptr;
    if (!runNewProjectDialog(*gRoot, start, c, shown)) return;
    std::string err;
    if (!createProject(fs::path(c.location), narrow(c.name), c.fromOpenMap, err, fs::path(c.output)))
        shellErrorBox(widen(err), shellTr(L"Could not create the project"));
    shellRelayout(), shellMenusChanged();
}

// File > Project > Open project...: a project file (project.stardraft or a .eds) from the open dialog.
void openProjectDialogSdl() {
    ShellFilePick d;
    d.filters = {{L"StarDraft 2 project (*.stardraft;*.eds)", L"*.stardraft;*.eds"}};
    d.initialDir = defaultProjectsFolder();
    if (const fs::path picked = shellPickFile(d); !picked.empty()) openProject(picked);
}

// The project commands (sdl_menus.inc's command); false: not one of them.
bool sdlProjectCommand(int id) {
    switch (id) {
    case ID_PROJ_OPEN: confirmReplace(openProjectDialogSdl); return true;
    case ID_PROJ_FROMMAP: showProjectDialogSdl(); return true;
    case ID_PROJ_ADDSCRIPT: addProjectScript(); return true;
    case ID_PROJ_FOLDER:
        if (EudProject* pr = activeProject()) shellOpenFolder(pr->folder());
        return true;
    case ID_PROJ_SETTINGS:  // the Project page (as the Windows program's showProjectSettings)
        if (!activeProject()) setMessage(L"Project settings need a project. Use File > Project > New EUD project or Open project.");
        else setWorkspace(Workspace::Project);
        return true;
    default: return false;
    }
}
