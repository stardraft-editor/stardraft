// The Project workspace's Windows host (included into main.cpp after the scripts' helpers): the page's window (a view
// window with the native scroll bar) and the script preview's window beside it. The page is core/views_project.inc
// (ProjectPageView, ProjectPreviewView) and its program parts are core/project_page.inc (ProjectProgram, gProj).

HWND gProjWnd = nullptr, gProjPreviewWnd = nullptr;  // the page; the selected script's code, on the right (View: the </> button)

// The preview's place: the right part of the page's window (projectPreviewWidth), shown when it has a width.
int projectPreviewPlace() {
    if (!gProjPreviewWnd) return 0;
    RECT rc;
    GetClientRect(gProjWnd, &rc);
    const int pw = projectPreviewWidth(rc.right);
    MoveWindow(gProjPreviewWnd, rc.right - pw, 0, pw, rc.bottom, TRUE);
    ShowWindow(gProjPreviewWnd, pw ? SW_SHOW : SW_HIDE);
    return pw;
}

// The background music's length: the sound decoded as the Files workspace decodes it (sound_files.inc).
uint32_t soundFileMs(const Bytes& data) {
    DecodedSound d;
    return decodeSound(data, d) ? d.ms : 0;
}

void createProjectPage() {
    if (gProjWnd) return;
    gProjWnd = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, &gProjView);
    cleanScroll(gProjWnd);
    darkControl(gProjWnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    gProjPreviewWnd = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD, 0, 0, 1, 1, gProjWnd, nullptr, app.inst, &gProjPreview);
}
