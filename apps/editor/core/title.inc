// The window's title and the rail's state for the open tab (portable, core.inc): moved from main.cpp. The title is
// "Name - StarDraft 2 - vX (built ...)"; the host sets the text and shows the rail (shell.h).

void updateTitle() {
    AppState& app = shellState();
    // The brand, the version and the game when it is a mod's (Program options > Mods > Game): "StarDraft 2 - v0.5.0.6",
    // "StarDraft 2 - v0.5.0.6 - Cosmonarchy BW".
    const std::wstring app_ = L"StarDraft 2 - v" + shellVersion() + (cmbwMode() ? L" - Cosmonarchy BW" : L"");
    std::wstring t = app_;
    if (app.ed && app.ed->isOpen())
        t = (gActive >= 0 ? docTitle(gActive) : std::wstring(L"Untitled")) + L" - " + app_;
    if (!shellBuildStamp().empty()) t += L"  (" + shellBuildStamp() + L")";
    shellSetTitle(t);
    shellInvalidateTabs();
    if (shellHasRail()) {
        // The rail follows the tab: shown for a project, gone for a map.
        static int railShown = -1;
        const int rail = 1;  // always: Map and Briefing are there for every map
        if (rail != railShown) {
            railShown = rail;
            shellShowRail(rail);
            shellRelayout();
        }
        shellInvalidateWorkspaceBar();
    }
    shellMenusChanged();  // the tab's mode (map or project) sets which commands are there
}
