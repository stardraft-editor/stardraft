// The SDL host of the editor's seam (shell.h; research/ports/MAC-BUILD-PLAN.md step 6). Included by main_sdl.cpp after the
// root view. The panes that are not on this system yet have no list, tool strip, palette or menu to change, so those
// calls do nothing; the status bar, the map view and the settings are real.

// The app state the core reads (documents.inc declares it).
AppState& shellState() { return gState; }
void shellRendererGraphics(GraphicsQuality q, bool carbot) {
    if (gRenderer) gRenderer->setQuality(q), gRenderer->setCarbot(carbot);
}

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
void shellLogError(const std::wstring& line) { std::fprintf(stderr, "%s\n", narrow(line).c_str()); }
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
int shellPopupPick(const std::vector<std::wstring>& items, int checked, int, int) {  // (a modal list: sdl_map_pages.inc)
    return sdlPickFromList(items, checked, shellTr(L"Choose"));
}
int shellContextMenu(const std::vector<std::wstring>& items) {  // (a modal list: sdl_map_pages.inc)
    return sdlPickFromList(items, -1, shellTr(L"Choose")) + 1;
}
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
int shellAskUnapplied(const std::wstring& text, const std::wstring& title) {
    if (!gRoot) return 2;
    const int r = stardraft::kit::messageBox(*gRoot, title, text, {shellTr(L"Apply"), shellTr(L"Do not apply"), shellTr(L"Cancel")},
                                             stardraft::kit::MessageIcon::Question);
    return r == 0 ? 0 : r == 1 ? 1 : 2;
}
int shellTabSideMenu(const std::wstring&, int) { return 0; }  // (the side menu is not on this system yet)
void shellRecentMenu(bool, const std::vector<std::wstring>&, const std::wstring&) { gMenusDirty = true; }  // (the lists are the menus' own: sdl_menus.inc)
void shellShowQuickPicker(bool replace, bool open) { showQuickPickerSdl(replace, open); }  // (sdl_map_dialogs.inc)
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
// The Project page's files and programs: a folder or a file opens in the program the system gives its kind (SDL_OpenURL
// with a file: URL; the folder in the file manager, a script in the editor for its kind, a text in the text editor). A
// file shown in its folder opens the folder. The elevation (epTrace) is Windows only.
namespace {
std::string fileUrl(const std::filesystem::path& p) {
    std::error_code ec;
    const std::string s = narrow(std::filesystem::absolute(p, ec).generic_wstring());
    // file:///C:/... on Windows (a drive path gets the third slash; its colon stays), file:///home/... elsewhere.
    std::string url = s.empty() || s[0] != '/' ? "file:///" : "file://";
    static const char* const kHex = "0123456789ABCDEF";
    for (const unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == ':') url += char(ch);
        else url += '%', url += kHex[ch >> 4], url += kHex[ch & 15];
    }
    return url;
}
void openWithSystem(const std::filesystem::path& p) {
    if (!SDL_OpenURL(fileUrl(p).c_str())) setMessage(L"Could not open " + p.wstring() + L": " + widen(SDL_GetError()));
}
}  // namespace
void shellOpenFolder(const std::filesystem::path& folder) { openWithSystem(folder); }
void shellOpenInEditor(const std::filesystem::path& file) { openWithSystem(file); }
void shellOpenWithNotepad(const std::filesystem::path& file) { openWithSystem(file); }
void shellRevealFile(const std::filesystem::path& file) { openWithSystem(file.parent_path()); }
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
// The file and folder dialogs: SDL's (asynchronous); the call waits for the answer (the host handles events and paints
// meanwhile). A dialog whose answer comes after the program quits keeps its small state (the callback may still come).
namespace {
struct SdlPickState {
    std::atomic<bool> done{false};
    std::string picked;  // UTF-8; empty: none was picked
};
void sdlPickAnswered(void* data, const char* const* list, int) {
    auto* st = static_cast<SdlPickState*>(data);
    st->picked = list && list[0] ? list[0] : "";
    st->done = true;
}
// Waits for the dialog that `show` opens with the state; the picked path, or empty.
std::filesystem::path sdlPickWait(const std::function<void(SdlPickState*)>& show) {
    auto* st = new SdlPickState;
    show(st);
    if (!gHost || !gHost->waitUntil([st] { return st->done.load(); })) return {};  // (quit: the state stays for the late answer)
    const std::filesystem::path p = st->picked.empty() ? std::filesystem::path() : std::filesystem::path(widen(st->picked));
    delete st;
    return p;
}
// The patterns as SDL's filters take them: "*.eps;*.py" is "eps;py", "*.*" is "*".
std::string sdlPatterns(const std::wstring& patterns) {
    std::string out, one;
    auto flush = [&] {
        if (one.rfind("*.", 0) == 0) one.erase(0, 2);
        if (one.empty()) one = "*";
        if (!out.empty()) out += ';';
        out += one;
        one.clear();
    };
    for (const char ch : narrow(patterns)) {
        if (ch == ';') flush();
        else if (ch != ' ') one += ch;
    }
    flush();
    return out;
}
}  // namespace
std::filesystem::path shellPickFile(const ShellFilePick& pick) {
    std::vector<std::string> names, patterns;
    for (const auto& [name, pat] : pick.filters) names.push_back(narrow(name)), patterns.push_back(sdlPatterns(pat));
    std::vector<SDL_DialogFileFilter> filters;
    for (size_t i = 0; i < names.size(); ++i) filters.push_back({names[i].c_str(), patterns[i].c_str()});
    // The place to start: the folder, and for a save dialog the name in it.
    std::string where;
    if (!pick.initialDir.empty()) where = narrow(pick.initialDir.wstring());
    if (pick.save && !pick.fileName.empty())
        where = narrow((pick.initialDir.empty() ? std::filesystem::path(pick.fileName) : pick.initialDir / pick.fileName).wstring());
    std::filesystem::path picked = sdlPickWait([&](SdlPickState* st) {
        if (pick.save)
            SDL_ShowSaveFileDialog(sdlPickAnswered, st, dialogWindow(), filters.data(), int(filters.size()), where.empty() ? nullptr : where.c_str());
        else
            SDL_ShowOpenFileDialog(sdlPickAnswered, st, dialogWindow(), filters.data(), int(filters.size()), where.empty() ? nullptr : where.c_str(), false);
    });
    if (pick.save && !picked.empty() && picked.extension().empty() && !pick.defaultExtension.empty()) picked += L"." + pick.defaultExtension;
    return picked;
}
std::wstring shellPickFolder(const std::wstring&) {
    const std::string where = narrow(defaultProjectsFolder().wstring());
    return sdlPickWait([&](SdlPickState* st) {
               SDL_ShowOpenFolderDialog(sdlPickAnswered, st, dialogWindow(), where.empty() ? nullptr : where.c_str(), false);
           })
        .wstring();
}
std::wstring shellPickOutputFile(const std::wstring& name) {
    ShellFilePick d;
    d.filters = {{L"StarCraft Brood War map (*.scx)", L"*.scx"}};
    const std::filesystem::path current(name);
    d.initialDir = current.parent_path(), d.fileName = current.filename().wstring();
    d.title = L"Select the file for the built map";
    d.defaultExtension = L"scx";
    d.save = true;
    return shellPickFile(d).wstring();
}
// The Documents folder: the system's (XDG's on Linux), else ~/Documents.
std::filesystem::path shellDocumentsFolder() {
    if (const char* d = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS); d && *d) return std::filesystem::path(widen(d));
    const char* home = std::getenv("HOME");
    return home && *home ? std::filesystem::path(home) / "Documents" : std::filesystem::path();
}
bool shellPromptText(const std::wstring& title, std::wstring& text) { return gRoot && runPromptDialog(*gRoot, title, text); }
// (no pop-out under the field here: the prompt dialog, centered)
bool shellEditValueBelow(stardraft::draw::View&, stardraft::draw::Rect, const std::wstring& title, std::string& text) {
    std::wstring w = widen(text);
    if (!shellPromptText(title, w)) return false;
    text = narrow(w);
    return true;
}
void shellLogError(const std::wstring& line) { shellLogLine(line); }
std::wstring shellCodePageText(const std::string& bytes) { return widen(bytes); }  // (UTF-8)
std::wstring shellInstallFolderText() {
    std::wstring saved;
    return prefs::text(L"StarCraftFolder", saved) ? saved : std::wstring();

// A pop-out: the host's modal window over the main window (a click outside does not close it yet: Esc and its own
// buttons do).
void shellRunPopout(stardraft::draw::View& content, int x, int y, int w, int h, stardraft::draw::View* focus, const std::function<void()>& opened) {
    if (!gHost || !gRoot) return;
    // The timer does nothing once the pop-out is gone (runModal can return before its loop runs, and the content and
    // its focus are the caller's).
    auto alive = std::make_shared<bool>(true);
    gHost->timer(1, [focus, opened, weak = std::weak_ptr<bool>(alive)] {  // (once the window shows)
        if (weak.expired()) return;
        if (focus) focus->focus();
        if (opened) opened();
    });
    gHost->runModal(*gRoot, content, stardraft::draw::WindowSpec{stardraft::draw::WindowKind::Popup, stardraft::draw::Rect{x, y, x + w, y + h}, L""});
}
void shellClosePopout(stardraft::draw::View& content) {
    if (gHost) gHost->closeWindow(content);
}
bool shellUiIconMask(int, int, std::vector<uint8_t>&) { return false; }  // (the SDL canvas draws icons from its own atlases)
