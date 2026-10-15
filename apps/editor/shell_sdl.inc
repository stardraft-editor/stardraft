// The SDL host of the editor's seam (shell.h; research/ports/MAC-BUILD-PLAN.md step 6). Included by main_sdl.cpp after the
// root view. The panes that are not on this system yet have no list, tool strip, palette or menu to change, so those
// calls do nothing; the status bar, the map view and the settings are real.

// The app state the core reads (documents.inc declares it).
AppState& shellState() { return gState; }

void shellRelayout() {
    if (gRoot) gRoot->relayout();
}
void shellMenusChanged() { gMenusDirty = true; }  // (the menu bar is made again on the next frame: sdl_menus.inc)
void shellStatusChanged() {
    if (gRoot) gRoot->invalidate();
}
bool shellLogOpen() { return false; }  // (the Log is not on this system yet)
int shellUiFontLfHeight() { return -12; }  // (the Win32 UI font's height; the kit's views size their own text)
// The map view's size: none until the root view is in a window (a map opened from the command line comes before the
// window, and the layout's placeholder then is 1 x 1); fitMap waits for it (gFitPending).
void shellViewSize(int& w, int& h) {
    const bool shown = gRoot && gRoot->host();
    w = shown ? gRoot->map.width() : 0;
    h = shown ? gRoot->map.height() : 0;
}

// The posted work runs on the UI thread in the frame hook (sdl_main_view.inc: runPosted); the host paints each frame
// while the editor runs (setAnimating), so the work runs soon.
void shellPost(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(gPostMutex);
    gPosted.push_back(std::move(fn));
}

// The spray held down: a timer every 25 ms while it is on (the host's timers run on the UI thread).
bool gSprayOn = false;
int gSprayTimerId = 0;
void armSprayTimer() {
    if (!gHost) return;
    gSprayTimerId = gHost->timer(25, [] {
        gSprayTimerId = 0;
        if (!gSprayOn) return;
        sprayHoldTimer(gState);
        armSprayTimer();
    });
}
void shellSprayTimer(bool on) {
    gSprayOn = on;
    if (on) {
        if (!gSprayTimerId) armSprayTimer();
    } else if (gSprayTimerId && gHost) {
        gHost->cancelTimer(gSprayTimerId);
        gSprayTimerId = 0;
    }
}
void shellTrackMouseLeave() {}  // (the host sends the leave of a view that the mouse leaves)
bool shellCtrlOrShiftDown() { return (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) != 0; }
bool shellClearFilter() {  // the Locations filter is cleared (the Win32 filter's own rule: it is always there)
    gFilterSdl.setText(L"");
    return true;
}
unsigned shellDpi() { return gDpi; }
void shellLogLine(const std::wstring& line) { std::fprintf(stderr, "%s\n", narrow(line).c_str()); }
bool shellHasStatusBar() { return true; }
int shellSpriteCount() { return gRenderer ? gRenderer->stats().sprites : 0; }
// The Locations list (sdl_panels.inc): its rows are the rebuildList's, its selection is the core's picks.
void shellListSelect(int i) {
    if (i >= 0) gLocList.selectOnly(i);
    else gLocList.clearSelection();
}
void shellListFill(const std::vector<std::wstring>& rows) {
    gListRows = rows;
    gLocList.setCount(int(rows.size()));
    gLocList.invalidate();
}
std::vector<int> shellListSelection() { return gLocList.selection(); }
// The Locations filter's text (the palette's own search is not the list's filter).
std::wstring shellFilterText() { return gFilterSdl.text(); }
bool shellViewShown() { return gRoot != nullptr; }
void shellToolsChanged() { gToolStripView.invalidate(); }
int shellPopupPick(const std::vector<std::wstring>&, int, int, int) { return -1; }  // (the SDL shell has no popup menus yet)
void shellOwnerSelect(int i) { gOwnerBox.setValue(i); }  // (the owner dropdown shows player i; no change is sent)
void shellPaletteChanged() {
    gPalList.invalidate();
    gPal2List.invalidate();
    invalidateMapView();
}
// The palette's entries are made again: the list of its mode, its section box and the second palette's list.
void shellPaletteRebuilt() {
    rebuildPaletteRows();
    if (gPal2Shown) rebuildSecondRows();
}
std::filesystem::path shellUnsavedRoot() { return {}; }  // (new projects are not on this system yet)
// The save question: Save, Do not save or Cancel, in the kit's message box (Escape, or no window, is Cancel).
int shellAskSaveChanges(bool project) {
    if (!gRoot) return 2;
    const int r = stardraft::kit::messageBox(*gRoot, shellTr(L"Unsaved changes"),
                                             shellTr(project ? L"Save the new project?" : L"Save changes to the map?"),
                                             {shellTr(L"Save"), shellTr(L"Do not save"), shellTr(L"Cancel")}, stardraft::kit::MessageIcon::Question);
    return r == 0 ? 0 : r == 1 ? 1 : 2;
}
// Save after the question: a map with a file is saved there (true when it was written); an untitled map asks for its
// file and stays open until it is saved (false: close it again then). New projects are not on this system yet.
bool shellSaveChanges(bool project) {
    if (project || !gState.ed || !gState.ed->isOpen()) return false;
    if (!gState.ed->path().empty()) return saveTo(gState.ed->path());
    saveOpenMap(false);
    return false;
}
// The side the mode's tools are docked on (the Win32 rule: the mode last used on that side is remembered).
void shellModeSide(Mode m) { gLastOnSide[gDock.toolsRight[int(m)] ? 1 : 0] = m; }
void shellModeWindows(Mode) {}  // (the palette, list and player windows are not on this system yet)
std::wstring shellTr(const wchar_t* english) { return tr(english); }
std::vector<int>& shellPaletteChosen() {
    static std::vector<int> chosen;
    return chosen;
}

// The settings file: ~/Library/Application Support/StarDraft on macOS, $XDG_CONFIG_HOME or ~/.config/StarDraft on Linux
// (the environment variable STARDRAFT_SETTINGS gives another file, as on Windows).
void shellSettingsChanged() {}  // (the SDL program keeps no options yet: the Windows program saves its options in the registry)
std::filesystem::path shellSettingsFile() {
    if (const char* file = std::getenv("STARDRAFT_SETTINGS"); file && *file) return file;
    fs::path dir;
#ifdef __APPLE__
    if (const char* h = std::getenv("HOME")) dir = fs::path(h) / "Library" / "Application Support" / "StarDraft";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) dir = fs::path(x) / "StarDraft";
    else if (const char* h = std::getenv("HOME")) dir = fs::path(h) / ".config" / "StarDraft";
#endif
    if (dir.empty()) dir = fs::temp_directory_path() / "StarDraft";
    return dir / "settings.json";
}
// The registry of the Windows builds before the file: this system never had one, so no value is there.
bool shellLegacyValue(const std::wstring&, const std::wstring&, LegacyKind, std::vector<uint8_t>&) { return false; }
void shellLegacyNames(const std::wstring&, std::vector<std::wstring>&) {}
void shellLegacyRemove(const std::wstring&, const std::wstring&) {}

void shellErrorBox(const std::wstring& text, const std::wstring& title) {
    std::fprintf(stderr, "%s: %s\n", narrow(title).c_str(), narrow(text).c_str());
    setMessage(text);
}
int shellTabSideMenu(const std::wstring&, int) { return 0; }  // (the side menu is not on this system yet)
void shellRecentMenu(bool, const std::vector<std::wstring>&, const std::wstring&) { gMenusDirty = true; }  // (the lists are the menus' own: sdl_menus.inc)
void shellShowQuickPicker(bool, bool) { setMessage(L"This is not on this system yet"); }
void shellMapFileOpened(const std::filesystem::path&) {}  // (the file association is Windows only)

// When the program started (its start-up marks count from here): main() takes it first.
std::chrono::steady_clock::time_point shellProcessStart() {
    static const auto t0 = std::chrono::steady_clock::now();
    return t0;
}
std::wstring shellVersion() { return widen(STARDRAFT_VERSION_NAME); }
std::wstring shellBuildStamp() { return widen(std::string("built ") + __DATE__ + " " + __TIME__); }
void shellSetTitle(const std::wstring& title) {
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    for (int i = 0; wins && i < n; ++i) SDL_SetWindowTitle(wins[i], narrow(title).c_str());
    SDL_free(wins);
}
bool shellHasRail() { return true; }  // (the workspace rail: the Map workspace's items, as the core lists them)
void shellShowRail(bool) {}
void shellWaitCursor() {
    static SDL_Cursor* wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    SDL_SetCursor(wait);
}
void shellMinimapSize(int& w, int& h) {
    w = gRoot ? gRoot->minimap.width() : 0;
    h = gRoot ? gRoot->minimap.height() : 0;
}
// The renderer takes the open map's sources again on the next frame (sdl_main_view.inc sets them when gMapEd changes).
void shellRendererSources() {
    if (gRenderer) gRenderer->setQuality(gState.quality);
    gMapEd = nullptr;
}
void shellRedrawTabs() {
    if (gRoot) gRoot->invalidate();
}
void shellInvalidateTabs() {
    if (gRoot) gRoot->invalidate();
}
void shellInvalidateWorkspaceBar() {
    if (gRoot) gRoot->invalidate();
}
void shellForgetAnims() {}  // (no unit animations are kept apart from the renderer's)
// The map view takes the keyboard focus (the host's focus; the map view is the root's child).
void shellFocusView() {
    if (gRoot) gRoot->map.focus();
}
// A command runs after the current message (its menu or tab click ends first): posted to the UI thread.
void shellPostCommand(int id) {
    shellPost([id] { command(id); });
}
// The tab strip is drawn again for a tab that moved (the frame hook repaints the windows while the program runs).
void shellUpdateTabs(stardraft::draw::View&) {
    if (gRoot) gRoot->invalidate();
}
// macOS's file system ignores case unless the user formatted it otherwise; Linux's does not.
bool shellPathsFoldCase() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}
// The Project page's files and programs (the Windows shell's open, the editor, Notepad, the folder and the elevation).
void shellOpenFolder(const std::filesystem::path&) { setMessage(L"This is not on this system yet"); }
void shellOpenInEditor(const std::filesystem::path&) { setMessage(L"This is not on this system yet"); }
void shellOpenWithNotepad(const std::filesystem::path&) { setMessage(L"This is not on this system yet"); }
void shellRevealFile(const std::filesystem::path&) { setMessage(L"This is not on this system yet"); }
std::wstring shellFileTimeText(const std::filesystem::path& file) {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(file, ec);
    if (ec) return {};
    // The file's clock as the system clock (the offset between the two clocks now), in local time.
    const auto sys = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - std::filesystem::file_time_type::clock::now());
    const std::time_t t = std::chrono::system_clock::to_time_t(sys);
    const std::tm* tm = std::localtime(&t);
    if (!tm) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    return widen(buf);
}
std::uintptr_t shellStartElevated(const std::filesystem::path&, const std::filesystem::path&, const std::wstring&, const std::wstring& what,
                                  std::wstring& error) {
    error = what + L" is not on this system yet";
    return 0;
}
bool shellProcessEnded(std::uintptr_t) { return true; }
void shellProcessClose(std::uintptr_t) {}
void shellChooseInstallFolder() { setMessage(L"This is not on this system yet"); }
