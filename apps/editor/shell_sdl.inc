// The SDL host of the editor's seam (shell.h; research/ports/MAC-BUILD-PLAN.md step 6). Included by main_sdl.cpp after the
// root view. The panes that are not on this system yet have no list, tool strip, palette or menu to change, so those
// calls do nothing; the status bar, the map view and the settings are real.

#include "message_box.h"  // apps/kit: the StarCraft folder's and the yes-or-no boxes (kit::messageBox)

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
bool shellLogOpen() { return gRoot != nullptr; }  // (the Log's bar is in the window: it shows the latest message)
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
// A line in the Log (core/views_log.inc), and on stderr; an error opens the Log.
void shellLogLine(const std::wstring& line) {
    std::fprintf(stderr, "%s\n", narrow(line).c_str());
    if (logAdd(logState(), line, false)) gLogPane.changed();
}
void shellLogError(const std::wstring& line) {
    std::fprintf(stderr, "%s\n", narrow(line).c_str());
    if (!logAdd(logState(), line, true)) return;
    gLogPane.changed();
    gLogPane.setExpanded(true);
}
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
    return sdlPickFromList(items, checked, shellTr(L"Select"));
}
int shellContextMenu(const std::vector<std::wstring>& items) {  // (a modal list: sdl_map_pages.inc)
    return sdlPickFromList(items, -1, shellTr(L"Select")) + 1;
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
// The folder of the projects that are not saved yet (core/documents.inc): StarDraft/Unsaved in the user's data folder
// (~/Library/Application Support on macOS; $XDG_DATA_HOME or ~/.local/share on Linux), as the Windows program's
// %LOCALAPPDATA%\StarDraft\Unsaved.
std::filesystem::path shellUnsavedRoot() {
    fs::path dir;
#ifdef __APPLE__
    if (const char* h = std::getenv("HOME"); h && *h) dir = fs::path(h) / "Library" / "Application Support";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) dir = fs::path(x);
    else if (const char* h = std::getenv("HOME"); h && *h) dir = fs::path(h) / ".local" / "share";
#endif
    return dir.empty() ? fs::path() : dir / "StarDraft" / "Unsaved";
}
// The save question: Save, Do not save or Cancel, in the message box (core/views_message_box.inc; Escape, or no window, is Cancel).
int shellAskSaveChanges(bool project) {
    if (!gRoot) return 2;
    const int r = msgbox::ask(*gRoot, shellTr(L"Unsaved changes"),
                              shellTr(project ? L"Save the new project?" : L"Save changes to the map?"),
                              {shellTr(L"&Save"), shellTr(L"Do &not save"), shellTr(L"Cancel")}, msgbox::Icon::Question);
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
    if (gRoot && !gNoBoxes) msgbox::ask(*gRoot, title, text, {L"OK"}, msgbox::Icon::Error);  // (as the Windows program's MB_ICONERROR box)
    else setMessage(text);  // (a snapshot run, or before the window: the status bar)
}
stardraft::draw::Canvas* shellMeasureCanvas() { return &stardraft::kit_sdl::measuringCanvas(); }
int shellAskUnapplied(const std::wstring& text, const std::wstring& title) {
    if (!gRoot) return 2;
    const int r = msgbox::ask(*gRoot, title, text, {shellTr(L"&Apply"), shellTr(L"Do &not apply"), shellTr(L"Cancel")},
                              msgbox::Icon::Question);
    return r == 0 ? 0 : r == 1 ? 1 : 2;
}
// A side panel's tab, right-clicked: its menu at the mouse (the title, then Left and Right with the side's check). The
// call waits for the pick: 1 Left, 2 Right, 0 none.
int shellTabSideMenu(const std::wstring& title, int side) {
    if (!gRoot || !gHost) return 0;
    struct Pick {
        bool closed = false;
        int id = 0;
    };
    auto pick = std::make_shared<Pick>();
    std::vector<stardraft::kit::MenuItem> items(4);
    items[0].label = title, items[0].enabled = false;
    items[1].separator = true;
    items[2].label = shellTr(L"Left"), items[2].checked = side == 0, items[2].run = [pick] { pick->id = 1; };
    items[3].label = shellTr(L"Right"), items[3].checked = side == 1, items[3].run = [pick] { pick->id = 2; };
    stardraft::kit::MenuOptions opts;
    opts.closed = [pick](bool) { shellPost([pick] { pick->closed = true; }); };  // (after the picked item ran)
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    if (!stardraft::kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, stardraft::kit::Menu::measure(items, &measure), opts)) return 0;
    gHost->waitUntil([pick] { return pick->closed; });
    return pick->id;
}
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
void shellOpenUrl(const std::wstring& url) { SDL_OpenURL(narrow(url).c_str()); }
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
#ifndef _WIN32  // (on Windows the start asks for administrator rights, as the Windows program's: shell_sdl_win.inc's)
// A program the Project page starts (epTrace): SDL's process in `dir`. macOS and Linux ask for no administrator rights
// (the Windows program's elevation prompt). `args`: the words; a word in double quotes may have spaces.
std::uintptr_t shellStartElevated(const std::filesystem::path& exe, const std::filesystem::path& dir, const std::wstring& args,
                                  const std::wstring& what, std::wstring& error) {
    std::vector<std::string> words = {narrow(exe.wstring())};
    std::string word;
    bool quoted = false, any = false;
    for (const char ch : narrow(args)) {
        if (ch == '"') quoted = !quoted, any = true;
        else if (ch == ' ' && !quoted) {
            if (any || !word.empty()) words.push_back(word);
            word.clear(), any = false;
        } else word += ch;
    }
    if (any || !word.empty()) words.push_back(word);
    std::vector<const char*> argv;
    for (const std::string& w : words) argv.push_back(w.c_str());
    argv.push_back(nullptr);
    const std::string where = narrow(dir.wstring());
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    if (!where.empty()) SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, where.c_str());
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) error = L"Could not start " + what + L" (" + widen(SDL_GetError()) + L")";
    return std::uintptr_t(p);
}
bool shellProcessEnded(std::uintptr_t process) {
    int code = 0;
    return !process || SDL_WaitProcess(reinterpret_cast<SDL_Process*>(process), false, &code);
}
void shellProcessClose(std::uintptr_t process) {
    if (process) SDL_DestroyProcess(reinterpret_cast<SDL_Process*>(process));
}
#endif
namespace {
void useInstallFolder(const std::string& picked);  // (main_sdl.cpp: the first start's question)
}
// File > Options > StarCraft folder (and the Project page's): another install is chosen; its game data must open. With no
// game data open, the program opens it now (as the first start's question); else it is used from the next start (the
// settings' StarCraftFolder), as on Windows.
void shellChooseInstallFolder() {
    const std::wstring picked = shellPickFolder(shellTr(L"Select the StarCraft: Remastered folder. It contains .build.info."));
    if (picked.empty() || !gRoot) return;
    if (!gDataOk) return useInstallFolder(narrow(picked));
    const fs::path dir = installFolderOf(fs::path(picked));
    GameData probe;
    std::string err;
    if (!probe.open(dir, err)) {
        stardraft::kit::messageBox(*gRoot, shellTr(L"StarCraft folder"), shellTr(L"Could not read the game data in this folder:") + L"\r\n" + widen(err), {L"OK"},
                                   stardraft::kit::MessageIcon::Warning);
        return;
    }
    if (!gSilent) prefs::setText(L"StarCraftFolder", dir.wstring());  // (tests never change the user's settings)
    gInstallNext = dir;
    gOptionsView.invalidate();
    stardraft::kit::messageBox(*gRoot, shellTr(L"StarCraft folder"),
                               shellTr(L"StarDraft 2 uses this folder after the next start:") + L"\r\n" + dir.wstring() + L"\r\n" +
                                   shellTr(L"To use it now, restart StarDraft 2."),
                               {L"OK"}, stardraft::kit::MessageIcon::Info);
    shellLogLine(L"StarCraft folder set to " + dir.wstring() + L" (from the next start)");
}
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
bool shellPromptText(const std::wstring& title, std::wstring& text) {
    TextPromptView* shown = nullptr;
    return gRoot && runTextPrompt(*gRoot, title, text, true, shown);
}
// The value pop-out (core/views_value_pop.inc) 2 px under the field, in the main window; "A text of the game..." lists
// the Strings tab's entries. A text that is not changed is no change (as the Windows editTextValue).
bool shellEditValue(stardraft::draw::View& v, stardraft::draw::Rect field, const ValuePopSpec& spec, std::string& out) {
    const draw::Point o = v.origin();
    const draw::Point at{o.x + field.left, o.y + field.bottom + scale(2)};
    auto gameTexts = [] {
        dataLoadForProject();
        return entryItems("strings", true);
    };
    if (gPopShot.wanted) {  // (a snapshot: the pop-out shows in the picture, sdl_popshot.inc)
        auto p = std::make_unique<ValuePopView>(spec);
        p->setPlace(at);
        p->gameTexts = gameTexts;
        const draw::Point size = p->outerSize();
        popShotTake(std::move(p), at, {size.x - 2, size.y - 2});
        return false;
    }
    return runValuePop(spec, at, out, nullptr, gameTexts);
}
bool shellEditGameText(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text) {
    ValuePopSpec spec;
    spec.type = "Text", spec.colorsOnly = true, spec.title = title, spec.initial = text;
    std::string out;
    if (!shellEditValue(v, field, spec, out) || out == text) return false;
    text = out;
    return true;
}
bool shellEditValueBelow(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text) {
    ValuePopSpec spec;
    spec.type = "Text", spec.title = title, spec.initial = text;
    std::string out;
    if (!shellEditValue(v, field, spec, out) || out == text) return false;
    text = out;
    return true;
}
std::wstring shellCodePageText(const std::string& bytes) { return widen(bytes); }  // (UTF-8)
std::wstring shellInstallFolderText() {
    std::wstring saved;
    return prefs::text(L"StarCraftFolder", saved) ? saved : std::wstring();
}

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

// Build and the Log (core/project_build.inc): the question in the kit's message box (Escape, or no window, is No).
bool shellAskYesNo(const std::wstring& text, const std::wstring& title, const std::wstring& yes, const std::wstring& no) {
    if (!gRoot) return false;
    auto plain = [](std::wstring t) {  // (the kit's buttons have no access keys)
        std::erase(t, L'&');
        return shellTr(t.c_str());
    };
    return stardraft::kit::messageBox(*gRoot, shellTr(title.c_str()), text, {plain(yes), plain(no)}, stardraft::kit::MessageIcon::Warning) == 0;
}
void shellLogNewBuild() {
    LogState& s = logState();
    s.errors = 0, s.latestError = false;
    gLogPane.invalidate();
}
// --- Program options and feedback ---
// The feedback folder: next to the settings file (~/.config/StarDraft/feedback, ~/Library/Application Support/StarDraft/feedback).
fs::path shellFeedbackFolder() { return shellSettingsFile().parent_path() / "feedback"; }
