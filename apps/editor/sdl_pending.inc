// The functions the core still needs that only the Win32 program defines (research/ports/MAC-BUILD-PLAN.md step 6).
// Each one is a temporary SDL version until it is in the core (another lane moves them there); the core declares them.
// Included by main_sdl.cpp after the core, next to sdl_main_view.inc.

// (until it is in the core) The state the core's functions reach through appState() (the core declares it in
// core/map_edit.inc): the SDL program's state is gState (sdl_main_view.inc).
AppState& appState() { return gState; }

// (until it is in the core) The palette and the tile panel are not on this system yet: their names and rows are empty.
void tilesRebuild() {}
void tilesSync() {}
int tilesViewH() { return 0; }
void tilesScrollTo(int) {}
RECT tilesCellRect(int) { return RECT{}; }
void tilesPanelShowAllFilter() {}
void tilesPanelRepaint() {}
void tilesPanelClearSearch() {}
bool trigUnitPickClick(float, float) { return false; }

// (until it is in the core) The map pages are not on this system yet: no page has changes.
bool mapPageChangesFor(int) { return false; }
// (until it is in the core) The dock layout is kept in memory on this system for now (it is not saved yet).
void saveDock() {}
// (until it is in the core) The trigger pane is not on this system yet: its zoom is not there.
void trigZoomReset() {}
// (until it is in the core) The graphics quality: the renderer's own (the Win32 program also repaints its palette here).
void setGraphics(GraphicsQuality q, bool carbot) {
    gState.quality = q, gState.carbot = carbot;
    if (gRenderer) gRenderer->setQuality(q), gRenderer->setCarbot(carbot);
    if (carbot) setMessage(L"Carbot (cartooned) graphics");
    gState.dirty = true;
}

// (until it is in the core) The project's custom unit names are not on this system yet (data_extras.inc is Win32 for now).
std::wstring projectUnitName(int) { return L""; }

// The workspaces (the core's setWorkspace; the Windows program's is in main.cpp, with its windows): Assets is Files, and
// a workspace the tab's mode does not have falls back to Map with the same message as Windows. The workspace shows its
// view (sdl_main_view.inc: the registered one, else "Not on this system yet"), and the map's panes show in Map only.
void previewStopSdl();  // (sdl_files.inc: a sound that plays stops when the Files workspace is left)
void setWorkspace(Workspace w) {
    if (w == Workspace::Assets) w = Workspace::Files;  // Assets is part of Files
    if (w != Workspace::Files) previewStopSdl();
    if (!commandInMode(mapPageCommandOf(w))) {
        if (w == Workspace::ClassicTriggers || w == Workspace::ClassicSettings)
            setMessage(w == Workspace::ClassicTriggers ? L"This project's map has no classic triggers. Use the Triggers workspace."
                                                       : L"The map's Data page is for classic maps. In a project, use the Data workspace.");
        else setMessage(L"This tab is a classic map. Data, Triggers and Project need a project. File > Project > Make this map a project makes one.");
        w = Workspace::Map;
    }
    gWorkspace = w;
    shellStatusChanged();  // the map's parts show in Map only
    shellRelayout();
    shellMenusChanged();  // Edit's items follow the workspace (and with them Ctrl+C / X / V)
    shellRedrawTabs();
}
void pageSync() {}
void openStartPage() {}

// (until it is in the core) Projects are not on this system yet.
void forgetProject(const EudProject*) {}
