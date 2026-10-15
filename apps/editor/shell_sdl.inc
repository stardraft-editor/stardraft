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
void shellViewSize(int& w, int& h) {
    w = gRoot ? gRoot->map.width() : 0;
    h = gRoot ? gRoot->map.height() : 0;
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
bool shellClearFilter() { return false; }  // (the palette's search box is not on this system yet)
unsigned shellDpi() { return gDpi; }
void shellLogLine(const std::wstring& line) { std::fprintf(stderr, "%s\n", narrow(line).c_str()); }
bool shellHasStatusBar() { return true; }
int shellSpriteCount() { return gRenderer ? gRenderer->stats().sprites : 0; }
void shellListSelect(int) {}  // (the palette's list is not on this system yet)
void shellListFill(const std::vector<std::wstring>&) {}
std::wstring shellFilterText() { return {}; }
bool shellViewShown() { return gRoot != nullptr; }
void shellToolsChanged() {}  // (the tool strip is not on this system yet)
void shellOwnerSelect(int) {}  // (the player combo box is not on this system yet)
void shellPaletteChanged() {}
void shellPaletteRebuilt() {}
std::wstring shellTr(const wchar_t* english) { return tr(english); }
std::vector<int>& shellPaletteChosen() {
    static std::vector<int> chosen;
    return chosen;
}

// The settings file: ~/Library/Application Support/StarDraft on macOS, $XDG_CONFIG_HOME or ~/.config/StarDraft on Linux
// (the environment variable STARDRAFT_SETTINGS gives another file, as on Windows).
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
