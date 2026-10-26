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
bool shellLogOpen() { return gRoot != nullptr; }  // (the Log's bar is in the window: it shows the latest message)
// The Win32 UI font's height (-12 at 100 %) at the program's scale, in the display's pixels: the small fonts, titles and
// counts that are sized from it grow with the scale and the pixel density (a Retina display: 2), as the UI font does.
int shellUiFontLfHeight() { return -stardraft::kit::px(12); }
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
    PostQueue& q = postQueue();
    std::lock_guard<std::mutex> lock(q.mutex);
    q.posted.push_back(std::move(fn));
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
bool shellCtrlOrShiftDown() { return (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) != 0; }  // (Cmd: macOS's Ctrl)
bool shellClearFilter() {  // the Locations filter is cleared (the Win32 filter's own rule: it is always there)
    gFilterSdl.setText(L"");
    return true;
}
unsigned shellDpi() { return gDpi; }
void sdlRememberLogLine(const std::wstring& line);  // (shell_sdl_win.inc: the crash report's last lines on Windows)
// A line in the Log (core/views_log.inc), and on stderr; an error opens the Log.
void shellLogLine(const std::wstring& line) {
    std::fprintf(stderr, "%s\n", narrow(line).c_str());
    sdlRememberLogLine(line);
    if (logAdd(logState(), line, false)) gLogPane.changed();
}
void shellLogError(const std::wstring& line) {
    std::fprintf(stderr, "%s\n", narrow(line).c_str());
    sdlRememberLogLine(line);
    if (!logAdd(logState(), line, true)) return;
    gLogPane.changed();
    gLogPane.setExpanded(true);
}
bool shellHasStatusBar() { return true; }
int shellSpriteCount() { return gRenderer ? gRenderer->stats().sprites : 0; }
// The Locations list (sdl_panels.inc): its rows are the rebuildList's in Locations, its selection is the core's picks.
// In another mode it lists the locations still (the location list's window shows it): the other mode's list is not it.
void shellListSelect(int i) {
    if (gState.mode != Mode::Locations) return;
    if (i >= 0) gLocList.selectOnly(i);
    else gLocList.clearSelection();
}
void shellListFill(const std::vector<std::wstring>& rows) {
    if (gState.mode == Mode::Locations) gListRows = rows, gLocItems = gState.items;
    else locationListRows(gState, gLocItems, gListRows);
    gLocList.setCount(int(gListRows.size()));
    gLocList.invalidate();
}
std::vector<int> shellListSelection() { return gLocList.selection(); }
void shellLocationListSync() {
    if (!gState.ed) return;
    std::vector<int> rows;
    int cursor = -1;
    for (int l : gState.ed->selectedLocations())
        if (const auto it = std::find(gLocItems.begin(), gLocItems.end(), l); it != gLocItems.end())
            (l == gState.ed->selectedLocation() ? cursor : rows.emplace_back()) = int(it - gLocItems.begin());
    if (cursor >= 0) rows.push_back(cursor);
    std::vector<int> want = rows;
    std::sort(want.begin(), want.end());
    if (want == gLocList.selection()) return;
    gLocList.clearSelection();
    for (int r : rows) gLocList.toggle(r);
    if (!rows.empty()) gLocList.ensureVisible(rows.back());
}
// The Locations filter's text (the palette's own search is not the list's filter).
std::wstring shellFilterText() { return gFilterSdl.text(); }
bool shellViewShown() { return gRoot != nullptr; }
void palettePanelsToolsChanged();  // (sdl_palette_panels.inc: the floating panels' strips)
void palettePanelsChanged();
// The active panel changed (panel_state.inc): each panel's strip, list and boxes show their own state again.
void shellPanelsChanged() {
    gToolStripView.invalidate(), gToolStripView2.invalidate(), gToolStripView3.invalidate();
    gPalList.invalidate(), gPal2List.invalidate(), gPal3List.invalidate(), gTilesDockGrid.invalidate();
    palettePanelsChanged();
    if (gFloatPalOpen) gFloatPalView.invalidate();
}
void shellToolsChanged() {
    gToolStripView.invalidate(), gToolStripView2.invalidate(), gToolStripView3.invalidate();
    palettePanelsToolsChanged();
}
// A menu of `items` at `at` of `from` that waits for the pick (the Windows paintedMenu with TPM_RETURNCMD): the item's
// index, or -1. feedback: "Report feedback on ..." at the end (the Windows popupMenu's context menus).
int sdlMenuPick(draw::View& from, draw::Point at, const std::vector<std::wstring>& items, int checked, bool feedback) {
    if (!gHost) return -1;
    struct Pick {
        bool closed = false;
        int index = -1;
    };
    auto pick = std::make_shared<Pick>();
    std::vector<stardraft::kit::MenuItem> kitItems(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        kitItems[i].label = items[i], kitItems[i].checked = int(i) == checked;
        kitItems[i].run = [pick, i] { pick->index = int(i); };
    }
    stardraft::kit::MenuOptions opts;
    opts.closed = [pick](bool) { shellPost([pick] { pick->closed = true; }); };  // (after the picked item ran)
    bool open = false;
    if (feedback) {
        open = sdlPopupMenu(from, at, std::move(kitItems), sdlFeedbackOn(from, at, L""), std::move(opts));
    } else {
        ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
        kit_sdl::ImguiCanvas measure(list, nullptr);
        open = stardraft::kit::Menu::popup(from, at, kitItems, stardraft::kit::Menu::measure(kitItems, &measure), std::move(opts));
    }
    if (!open || std::exchange(gMenuShotOpen, false)) return -1;  // (a snapshot's click: the menu stays in the picture)
    gHost->waitUntil([pick] { return pick->closed; });
    return pick->index;
}
// The tool strip's dropdown (the brush shape): a menu under the box (x, y: the strip's coordinates), as on Windows.
int shellPopupPick(const std::vector<std::wstring>& items, int checked, int x, int y) {
    return sdlMenuPick(gStripInUse && gStripInUse->host() ? *gStripInUse : gToolStripView, {x, y}, items, checked, false);
}
// A context menu at the mouse (the Tiles panel's favorites), with Report feedback: the item from 1, 0 for none.
int shellContextMenu(const std::vector<std::wstring>& items) {
    if (!gRoot || !gHost) return 0;
    return sdlMenuPick(*gRoot, gHost->mouse(*gRoot), items, -1, true) + 1;
}
void palettePanelsOwner(int i);
void palettePanelsReveal(int value);
// (the owner dropdowns show player i, the side bar's and the palette windows'; no change is sent)
void shellOwnerSelect(int i) {
    gOwnerBox.setValue(i);
    palettePanelsOwner(i);
}
void shellPaletteReveal(int value) {
    gPalList.reveal(value);
    palettePanelsReveal(value);  // (the active mode's palette windows too)
}
bool shellPaletteStep(int dir) {
    if (gPalList.mode != gState.mode || gState.palette < 0) return false;
    const std::vector<PaletteRow>& rows = gPalList.rows;
    const int n = int(rows.size());
    int at = -1;
    for (int i = gPalList.pinned ? gPalList.pinned + 1 : 0; i < n && at < 0; ++i)  // (its place in the list, not its pinned copy)
        if (rows[size_t(i)].value == gState.palette) at = i;
    if (at < 0) return false;
    int i = at + dir;
    while (i >= 0 && i < n && rows[size_t(i)].value < 0) i += dir;  // (past the headings)
    if (i < 0 || i >= n) return true;  // (the first or the last: it stays)
    pickUnit(gState, rows[size_t(i)].value, false);
    shellPaletteReveal(rows[size_t(i)].value);
    return true;
}
void shellPaletteChanged() {
    gPalList.invalidate();
    gPal2List.invalidate();
    gPal3List.invalidate();
    palettePanelsChanged();
    invalidateMapView();
}
// The palette's entries are made again: the list of its mode, its section box and the second palette's list.
void shellPaletteRebuilt() {
    rebuildPaletteRows();
    if (gPal2Shown) rebuildSecondRows();
    if (gPal3Shown) rebuildThirdRows();
    palettePanelsRebuild();
}
// The folder of the projects that are not saved yet (core/documents.inc): StarDraft/Unsaved in the user's data folder
// (%LOCALAPPDATA% on Windows, the Win32 program's folder; ~/Library/Application Support on macOS; $XDG_DATA_HOME or
// ~/.local/share on Linux: desktop_xdg.h).
std::filesystem::path shellUnsavedRoot() {
    fs::path dir;
#if defined(_WIN32)
    if (const wchar_t* l = _wgetenv(L"LOCALAPPDATA"); l && *l) dir = fs::path(l);
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME"); h && *h) dir = fs::path(h) / "Library" / "Application Support";
#else
    dir = stardraft::xdg::dataHome();
#endif
    return dir.empty() ? fs::path() : dir / "StarDraft" / "Unsaved";
}
std::filesystem::path shellCacheFolder() {
    if (gSilent) return {};  // (a test run keeps nothing and reads nothing that a run before kept)
    fs::path dir;
#if defined(_WIN32)
    if (const wchar_t* l = _wgetenv(L"LOCALAPPDATA"); l && *l) dir = fs::path(l) / "StarDraft" / "Cache";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME"); h && *h) dir = fs::path(h) / "Library" / "Caches" / "StarDraft";
#else
    if (const fs::path x = stardraft::xdg::cacheHome(); !x.empty()) dir = x / "StarDraft";
#endif
    return dir;
}
// The save question: Save, Do not save or Cancel, in the message box (core/views_message_box.inc; Escape, or no window, is Cancel).
int shellAskSaveChanges(bool project) {
    if (!gRoot) return 2;
    if (gShotDialogs)  // (a snapshot: the output says which tab asks; the next inputs answer the box)
        std::printf("question: %s (tab %s)\n", project ? "Save the new project?" : "Save changes to the map?", narrow(docTitle(gActive, false)).c_str());
    const int r = msgbox::ask(*gRoot, shellTr(L"Unsaved changes"),
                              shellTr(project ? L"Save the new project?" : L"Save changes to the map?"),
                              {shellTr(L"&Save"), shellTr(L"Do &not save"), shellTr(L"Cancel")}, msgbox::Icon::Question);
    return r == 0 ? 0 : r == 1 ? 1 : 2;
}
// The start locations question before a save (core/map_commands.inc: confirmStartLocations): 0 Place them, 1 Save anyway,
// 2 Cancel. Without a window (a test run) the map is saved as it is.
int shellAskStartLocations(const std::wstring& text) {
    if (!gRoot) return 1;
    if (gShotDialogs) std::printf("question: Start locations (the map has no start location for every player)\n");
    return msgbox::ask(*gRoot, shellTr(L"Start locations"), text, {shellTr(L"&Place them"), shellTr(L"&Save anyway"), shellTr(L"Cancel")},
                       msgbox::Icon::Question);
}
// The trigger comments question before a save of a Cosmonarchy map (map_commands.inc confirmTriggerComments): 0 Save
// anyway, 1 Cancel.
int shellAskTriggerComments(const std::wstring& text) {
    if (!gRoot) return 0;
    if (gShotDialogs) std::printf("question: Trigger comments (Cosmonarchy BW skips a line)%c", 10);
    return msgbox::ask(*gRoot, shellTr(L"Trigger comments"), text, {shellTr(L"&Save anyway"), shellTr(L"Cancel")}, msgbox::Icon::Warning);
}
// Save after the question: a map with a file is saved there (true when it was written); an untitled map (or one in a
// folder StarDraft cannot write to) asks for its file and stays open until it is saved (false: close it again then;
// a quit goes on once it is saved). An unsaved project asks where it goes.
bool sdlSaveUnsavedProject();
bool shellSaveChanges(bool project) {
    if (project) return sdlSaveUnsavedProject();
    if (!gState.ed || !gState.ed->isOpen()) return false;
    if (!confirmStartLocations()) return false;  // a map StarCraft cannot start for want of start locations: Place them, Save anyway or Cancel
    if (!gState.ed->path().empty() && !saveNeedsOtherPlace(gState.ed->path())) return saveTo(gState.ed->path());
    saveOpenMap(false, [retry = gQuitRetry](bool ok) {
        if (ok && retry) shellPost(retry);
    });
    return false;
}
// The side the mode's tools are docked on (the Win32 rule: the mode last used on that side is remembered).
void shellModeSide(Mode m) {  // (a floating mode is not its side's: the side keeps showing its last docked tab)
    if (!paletteFloats(m)) gLastOnSide[gDock.toolsRight[int(m)] ? 1 : 0] = m;
}
void shellModeWindows(Mode) {}  // (the palette, list and player windows are not on this system yet)
std::wstring shellTr(const wchar_t* english) { return tr(english); }
std::vector<int>& shellPaletteChosen() {
    static std::vector<int> chosen;
    return chosen;
}

// An environment variable as a path ("" when it is not set). SDL gives it as UTF-8 on each system (on Windows a path in
// the system's code page would lose the letters outside it).
fs::path envPath(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v ? fs::path(widen(v)) : fs::path();
}
// The user's folder: HOME, or USERPROFILE on Windows.
fs::path homeFolder() {
    fs::path h = envPath("HOME");
#ifdef _WIN32
    if (h.empty()) h = envPath("USERPROFILE");
#endif
    return h;
}

// The settings file: %APPDATA%\StarDraft on Windows (the Windows program's file), ~/Library/Application Support/StarDraft
// on macOS, $XDG_CONFIG_HOME or ~/.config/StarDraft on Linux (the environment variable STARDRAFT_SETTINGS gives another
// file, as on Windows).
void shellSettingsChanged() { saveSettingsTable(); }  // (the menus' options: core/app_settings.inc)
fs::path gSettingsFileArg;  // --settings <file> (main_sdl.cpp): this run's settings file
fs::path gLegacyHiveArg;    // --legacyhive <file> (Windows): the registry of the builds before the settings file, for a test
// A settings file that a test gives (--settings, or STARDRAFT_SETTINGS): then the registry of the Windows builds before
// the file is not read either (shell_sdl_win.inc: only --legacyhive's), so a test never reads the user's.
bool shellSettingsFileGiven() { return !gSettingsFileArg.empty() || !envPath("STARDRAFT_SETTINGS").empty(); }
std::filesystem::path shellSettingsFile() {
    if (!gSettingsFileArg.empty()) return gSettingsFileArg;
    if (fs::path file = envPath("STARDRAFT_SETTINGS"); !file.empty()) return file;
    fs::path dir;
#if defined(_WIN32)
    if (const fs::path a = envPath("APPDATA"); !a.empty()) dir = a / "StarDraft";
#elif defined(__APPLE__)
    if (const fs::path h = homeFolder(); !h.empty()) dir = h / "Library" / "Application Support" / "StarDraft";
#else
    if (const fs::path x = stardraft::xdg::configHome(); !x.empty()) dir = x / "StarDraft";  // (desktop_xdg.h)
#endif
    if (dir.empty()) dir = fs::temp_directory_path() / "StarDraft";
    return dir / "settings.json";
}
#ifndef _WIN32
// The registry of the Windows builds before the file: this system never had one, so no value is there. (Windows:
// shell_sdl_win.inc reads it, as the Win32 program does.)
bool shellLegacyValue(const std::wstring&, const std::wstring&, LegacyKind, std::vector<uint8_t>&) { return false; }
void shellLegacyNames(const std::wstring&, std::vector<std::wstring>&) {}
void shellLegacyRemove(const std::wstring&, const std::wstring&) {}
#endif

void shellErrorBox(const std::wstring& text, const std::wstring& title) {
    std::fprintf(stderr, "%s: %s\n", narrow(title).c_str(), narrow(text).c_str());
    if (gRoot && !gNoBoxes) msgbox::ask(*gRoot, title, text, {L"OK"}, msgbox::Icon::Error);  // (as the Windows program's MB_ICONERROR box)
    else setMessage(text);  // (a snapshot run, or before the window: the status bar)
}
void shellWarningBox(const std::wstring& text, const std::wstring& title) {
    std::fprintf(stderr, "%s: %s\n", narrow(title).c_str(), narrow(text).c_str());
    if (gRoot && !gNoBoxes) msgbox::ask(*gRoot, title, text, {L"OK"}, msgbox::Icon::Warning);  // (as the Windows program's MB_ICONWARNING box)
    else setMessage(text);
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
int shellTabSideMenu(const std::wstring& title, int side, int mode) {
    const Mode m = Mode(mode);
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
    items.push_back(stardraft::kit::MenuItem{}), items.back().separator = true;
    items.push_back(stardraft::kit::MenuItem{});
    items.back().label = accessFree(floatPaletteMenuLabel(m)), items.back().run = [pick] { pick->id = 3; };
    if (m != Mode::Locations) {
        items.push_back(stardraft::kit::MenuItem{});
        items.back().label = accessFree(L"&New panel like this"), items.back().run = [pick] { pick->id = 4; };
    }
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
void shellShowQuickPickerCustom() { showQuickPickerCustomSdl(); }
void sdlShowHelp(const std::string& topic);  // (sdl_help.inc)
void shellShowHelp(const std::string& topic) { sdlShowHelp(topic); }
// (shellMapFileOpened: shell_sdl_win.inc; the file association is Windows only)

// When the program started (its start-up marks count from here): main() takes it first.
std::chrono::steady_clock::time_point shellProcessStart() {
    static const auto t0 = std::chrono::steady_clock::now();
    return t0;
}
std::wstring shellVersion() { return widen(STARDRAFT_VERSION_NAME); }
std::wstring shellBuildStamp() { return widen(std::string("built ") + __DATE__ + " " + __TIME__); }
// The main window's title only: a dialog window (Program options, About, Block help) keeps its own.
void shellSetTitle(const std::wstring& title) {
    SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;
    if (!main) return;
#ifdef __APPLE__
    // (the close button's dot shows the changes: the title has no "* ")
    const std::string shown = narrow(title.rfind(L"* ", 0) == 0 ? title.substr(2) : title);
#else
    const std::string shown = narrow(title);
#endif
    SDL_SetWindowTitle(main, shown.c_str());
}
bool shellHasRail() { return true; }  // (the workspace rail: the Map workspace's items, as the core lists them)
void shellShowRail(bool) {}
void shellWaitCursor() {  // (until the mouse moves: the host sets the cursor of the view under it then)
    if (gHost) gHost->setCursor(stardraft::draw::Cursor::Wait);
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
void shellForgetAnims() {  // (the units' idle animations: core/map_scene.inc)
    gAnims.clear(), gAnimsFor = nullptr;
}
void shellRendererReloadImages() {
    if (gRenderer) gRenderer->reloadImages();
}
void shellRailUpdateNow() {}  // (the rail's items are made each time it paints, and it paints each frame)
bool shellViewMouse(int& x, int& y) {  // (the map view keeps the mouse in the state: sdl_main_view.inc)
    if (!gState.mouseIn) return false;
    x = gState.mouseX, y = gState.mouseY;
    return true;
}
// The map view takes the keyboard focus (the host's focus; the map view is the root's child).
void floatPaletteKeysToMap();  // (also from a floating palette's window: sdl_palette_float.inc)
void shellFocusView() {
    if (keyPressRuns()) return;  // (a part pressed with Enter or Space keeps the keyboard: core/item_focus.inc)
    if (gRoot) gRoot->map.focus();
    floatPaletteKeysToMap();
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
bool useInstallFolder(const std::string& picked);  // (main_sdl.cpp: the start page's banner)
void saveInstallFolder(const fs::path& dir);  // (main_sdl.cpp: the settings, and on Windows the Windows program's file)
}
// File > Options > StarCraft folder (and the Project page's): another install is chosen; its game data must open. With no
// game data open, the program opens it now (as the start page's Browse does); else it is used from the next start (the
// settings' StarCraftFolder), as on Windows.
void shellChooseInstallFolder() {
    const std::wstring picked = shellPickFolder(shellTr(L"Select the StarCraft: Remastered folder. It contains .build.info."));
    if (picked.empty() || !gRoot) return;
    if (!gDataOk) {
        if (!useInstallFolder(narrow(picked)))
            msgbox::ask(*gRoot, shellTr(L"StarCraft folder"), shellTr(L"That folder has no StarCraft game data."), {L"OK"}, msgbox::Icon::Warning);
        return;
    }
    const fs::path dir = installFolderOf(fs::path(picked));
    GameData probe;
    std::string err;
    if (!probe.open(dir, err)) {
        msgbox::ask(*gRoot, shellTr(L"StarCraft folder"), shellTr(L"Could not read the game data in this folder:") + L"\r\n" + widen(err), {L"OK"},
                    msgbox::Icon::Warning);
        return;
    }
    if (!gSilent) saveInstallFolder(dir);  // (tests never change the user's settings)
    gInstallNext = dir;
    gOptionsView.invalidate();
    msgbox::ask(*gRoot, shellTr(L"StarCraft folder"),
                shellTr(L"StarDraft 2 uses this folder after the next start:") + L"\r\n" + dir.wstring() + L"\r\n" + shellTr(L"To use it now, restart StarDraft 2."),
                {L"OK"}, msgbox::Icon::Info);
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
    const std::string where = sdlDialogLocation(pick.initialDir, pick.save ? pick.fileName : std::wstring());
    std::filesystem::path picked = sdlPickWait([&](SdlPickState* st) {
        sdlFileDialog(pick.save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE, sdlPickAnswered, st, filters.data(), int(filters.size()),
                      where, narrow(shellTr(pick.title.c_str())));
    });
    if (pick.save && !picked.empty() && picked.extension().empty() && !pick.defaultExtension.empty()) picked += L"." + pick.defaultExtension;
    return picked;
}
// Save for an unsaved project (as the Windows program's saveUnsavedProject): where it goes (a folder of its name is
// made there), then the core moves it there (core/project_page.inc: saveUnsavedProjectTo). True when it was saved.
bool sdlSaveUnsavedProject() {
    if (!gState.ed || !isUnsavedProject(activeProject())) return false;
    ShellFilePick d;
    d.title = shellTr(L"Save the project (StarDraft 2 makes a folder with its name here)");
    d.filters = {{L"StarDraft 2 project (*.stardraft)", L"*.stardraft"}};
    d.defaultExtension = L"stardraft", d.fileName = L"Untitled", d.initialDir = defaultProjectsFolder(), d.save = true;
    std::error_code ec;
    fs::create_directories(d.initialDir, ec);
    const fs::path picked = shellPickFile(d);
    return !picked.empty() && saveUnsavedProjectTo(picked);
}
std::wstring shellPickFolder(const std::wstring& title) {
    const std::string where = sdlDialogLocation(defaultProjectsFolder()), shownTitle = narrow(title);
    return sdlPickWait([&](SdlPickState* st) { sdlFileDialog(SDL_FILEDIALOG_OPENFOLDER, sdlPickAnswered, st, nullptr, 0, where, shownTitle); })
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
    const fs::path home = homeFolder();
    return !home.empty() ? home / "Documents" : std::filesystem::path();
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
bool shellDecodeImage(const std::vector<uint8_t>& file, int& width, int& height, std::vector<uint8_t>& rgba) {
    int n = 0;
    unsigned char* px = stbi_load_from_memory(file.data(), int(file.size()), &width, &height, &n, 4);
    if (!px) return false;
    rgba.assign(px, px + size_t(width) * size_t(height) * 4);
    stbi_image_free(px);
    return true;
}
bool shellEditPlainLines(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text) {
    ValuePopSpec spec;
    spec.type = "Text", spec.code = true, spec.title = title, spec.initial = text;
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
// (no system code page to read in: UTF-8, else Korean CP949, else Windows-1252, as apps/common/text_codec.h decides)
std::wstring shellCodePageText(const std::string& bytes) { return stardraft::text::widenLegacy(bytes); }
std::wstring shellInstallFolderText() {
    std::wstring saved;
    return prefs::text(L"StarCraftFolder", saved) ? saved : std::wstring();
}

// A pop-out: the host's modal pop-out window at (x, y) in the main window, with a 1 px frame in the border color (as
// the Windows pop-out's border, ui::popupFrame); `content` is in the frame. A press outside it only closes it (SdlHost
// eats the press and its release, as the Windows loop's popoutEatsPress); Esc closes it too.
struct PopoutFrameView final : stardraft::draw::View {
    void paint(stardraft::draw::Canvas& c) override { c.frame({0, 0, width(), height()}, stardraft::kit::theme().borderHot); }
    void resized() override {
        for (View* k : children()) k->setFrame({1, 1, std::max(1, width() - 1), std::max(1, height() - 1)});
    }
};
void shellRunPopout(stardraft::draw::View& content, int x, int y, int w, int h, stardraft::draw::View* focus, const std::function<void()>& opened) {
    if (!gHost || !gRoot) return;
    PopoutFrameView frame;
    frame.add(content, {1, 1, std::max(1, w - 1), std::max(1, h - 1)});
    // The timer does nothing once the pop-out is gone (runModal can return before its loop runs, and the content and
    // its focus are the caller's).
    auto alive = std::make_shared<bool>(true);
    gHost->timer(1, [focus, opened, weak = std::weak_ptr<bool>(alive)] {  // (once the window shows)
        if (weak.expired()) return;
        if (focus) focus->focus();
        if (opened) opened();
    });
    // The pop-out belongs to the window that takes the input now (x, y are in its pixels): the main window, or the
    // dialog or pop-out it opens from (a value pop-out's Insert value, the block dialog's Add values). So a click in it
    // keeps that one open, and the keys go back to that one when it closes.
    stardraft::draw::View* owner = gHost->modalView();
    if (gShotDialogs)  // (a snapshot: where it opens, for the inputs that click in it)
        std::printf("pop-out: %dx%d at %d,%d of %s\n", w, h, x, y, owner ? "the window it opens from" : "the main window");
    gHost->runModal(owner ? *owner : *gRoot, frame,
                    stardraft::draw::WindowSpec{stardraft::draw::WindowKind::Popup, stardraft::draw::Rect{x, y, x + w, y + h}, L""});
    gHost->forget(content);
    frame.remove(content);
}
void shellClosePopout(stardraft::draw::View& content) {
    if (!gHost) return;
    auto* frame = dynamic_cast<PopoutFrameView*>(content.parent());
    gHost->closeWindow(frame ? static_cast<stardraft::draw::View&>(*frame) : content);
}
bool shellUiIconMask(int, int, std::vector<uint8_t>&) { return false; }  // (the SDL canvas draws icons from its own atlases)

// Build and the Log (core/project_build.inc): the question in the core message box, as the Windows program's
// MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 (the warning icon; the second button has the focus; Escape, or no window, is No).
msgbox::Box sdlYesNoBox(const std::wstring& text, const std::wstring& yes, const std::wstring& no) {
    return msgbox::makeBox(text, {shellTr(yes.c_str()), shellTr(no.c_str())}, msgbox::Icon::Warning, 1);
}
bool shellAskYesNo(const std::wstring& text, const std::wstring& title, const std::wstring& yes, const std::wstring& no) {
    if (!gRoot) return false;
    return msgbox::runMessageBox(*gRoot, shellTr(title.c_str()), sdlYesNoBox(text, yes, no), nullptr) == 0;
}
void shellLogNewBuild() {
    LogState& s = logState();
    s.errors = 0, s.latestError = false;
    gLogPane.invalidate();
}
// --- Program options and feedback ---
// The feedback folder: next to the settings file (%APPDATA%\StarDraft\feedback, ~/Library/Application
// Support/StarDraft/feedback); on Linux in the data folder ($XDG_DATA_HOME/StarDraft/feedback: it is not a setting).
fs::path shellFeedbackFolder() {
#if defined(STARDRAFT_XDG)
    if (const fs::path d = stardraft::xdg::dataHome(); !d.empty() && !SDL_getenv("STARDRAFT_SETTINGS")) return d / "StarDraft" / "feedback";
#endif
    return shellSettingsFile().parent_path() / "feedback";
}
fs::path gStampsFolderArg;  // --stamps <folder> (main_sdl.cpp): this run's custom terrain folder
fs::path shellStampsFolder() {
    if (!gStampsFolderArg.empty()) return gStampsFolderArg;
    if (fs::path dir = envPath("STARDRAFT_STAMPS_DIR"); !dir.empty()) return dir;
    if (gSilent || gSelfTest || gSettingsOff) {  // a test: a new folder of this run (it reads and changes nothing of the user's)
        static const fs::path own = [] {
            std::error_code ec;
            const fs::path base = fs::temp_directory_path(ec);
            const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
            return base.empty() ? fs::path() : base / ("stardraft-stamps-" + std::to_string(static_cast<unsigned long long>(t)));
        }();
        return own;
    }
#if defined(STARDRAFT_XDG)
    if (const fs::path d = stardraft::xdg::dataHome(); !d.empty() && !SDL_getenv("STARDRAFT_SETTINGS")) return d / "StarDraft" / "stamps";
#endif
    return shellSettingsFile().parent_path() / "stamps";
}
