// The functions the core still needs that only the Win32 program defines (research/ports/MAC-BUILD-PLAN.md step 6).
// Each one is a temporary SDL version until it is in the core (another lane moves them there); the core declares them.
// Included by main_sdl.cpp after the core, next to sdl_main_view.inc.

// (until it is in the core) The state the core's functions reach through appState() (the core declares it in
// core/map_edit.inc): the SDL program's state is gState (sdl_main_view.inc).
AppState& appState() { return gState; }

// (until it is in the core) The translations: ui_text.h is Windows-only for now, so the English text is used.
const wchar_t* tr(const wchar_t* english) { return english; }
std::wstring tr(const std::wstring& english) { return english; }

// (until it is in the core) The palette and the tile panel are not on this system yet: their names and rows are empty.
std::wstring paletteItemName(int) { return {}; }
std::wstring entryName(const std::string&, int v) { return std::to_wstring(v); }
void tilesRebuild() {}
void tilesSync() {}
int tilesViewH() { return 0; }
void tilesScrollTo(int) {}
RECT tilesCellRect(int) { return RECT{}; }
void tilesPanelShowAllFilter() {}
void tilesPanelRepaint() {}
void tilesPanelClearSearch() {}
bool trigUnitPickClick(float, float) { return false; }

// (until it is in the core) The startup timing marks are the Windows program's (its Log shows them).
void startMark(const char*) {}

// (until it is in the core) The window's title: the open map's file name (the window is the only one).
void updateTitle() {
    std::wstring title = L"StarDraft";
    if (gState.ed && gState.ed->isOpen()) title = fs::path(gState.ed->path()).filename().wstring() + L" - StarDraft";
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    for (int i = 0; wins && i < n; ++i) SDL_SetWindowTitle(wins[i], narrow(title).c_str());
    SDL_free(wins);
}

// (until it is in the core) The mode's own spray density is kept per mode (setMode swaps it in); the tool strip and the
// palette are not on this system yet.
void setMode(Mode m) {
    gState.densityByMode[int(gState.mode)] = gState.sprayDensity;
    gState.mode = m;
    gState.sprayDensity = gState.densityByMode[int(m)];
    shellToolsChanged();
    shellPaletteRebuilt();
}

// (until it is in the core) The palette's selection: the one entry that is picked.
std::vector<int> selectedPalette() {
    if (gState.palette >= 0) return {gState.palette};
    return {};
}

// (until it is in the core) The map commands (File, Edit, View, ...) are the Windows program's for now: none is run here.
bool mapCommand(int) { return false; }

// (until it is in the core) The documents: one map is open at a time on this system, in the first tab.
void activateDoc(int i) {
    if (i >= 0 && i < int(gDocs.size())) gActive = i;
}
int findOpenProject(const std::filesystem::path&) { return -1; }

// (until it is in the core) Opens a map file: the game data must be open first. Its camera fits the map in the view.
void openMap(const std::wstring& path, bool) {
    if (!gDataOk) return setMessage(widen(gDataError));
    auto ed = std::make_unique<Editor>(gState.data);
    std::string err;
    if (!ed->open(fs::path(path), err)) return setMessage(widen(err));
    gState.ed = std::move(ed);
    if (gDocs.empty()) gDocs.emplace_back();
    gActive = 0;
    gDocs[0].page = 0;
    gDocs[0].project.reset();
    gMapFitPending = true;  // the map fits the view on the next frame (the view's size is known then)
    if (gRoot) gRoot->relayout();
    gState.dirty = true;
    setMessage(L"Opened " + fs::path(path).filename().wstring());
    updateTitle();
}
