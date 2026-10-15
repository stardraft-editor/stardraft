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

// (until it is in the core) Closing a map with changes: Save is not on this system yet, so it is not closed (nothing
// is lost); the message says why.
bool confirmDiscard() {
    if (gState.ed && gState.ed->isOpen() && gState.ed->modified()) {
        setMessage(L"The map has changes, and saving is not on this system yet: the map stays open");
        return false;
    }
    return true;
}

// (until it is in the core) Projects are not on this system yet.
void forgetProject(const EudProject*) {}
void dropUnsavedProject(const std::shared_ptr<EudProject>&) {}

// (until it is in the core) A map that opened: one tab on this system; the map fits the view on the next frame.
void addDocument(std::unique_ptr<Editor> ed, bool) {
    gState.ed = std::move(ed);
    if (gDocs.empty()) gDocs.emplace_back();
    gActive = 0;
    gDocs[0].page = 0;
    gMapFitPending = true;
    shellRelayout();
    gState.dirty = true;
    updateTitle();
}
