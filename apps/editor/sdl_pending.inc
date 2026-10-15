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

// (until it is in the core) The workspaces: only the Map is on this system yet.
void setWorkspace(Workspace w) {
    if (w != Workspace::Map) setMessage(L"This is not on this system yet");
    gWorkspace = Workspace::Map;
    shellRelayout();
}
void pageSync() {}
void openStartPage() {}

// (until it is in the core) Projects are not on this system yet.
void forgetProject(const EudProject*) {}
