// The Windows host of the editor's seam (shell.h). Included by main.cpp before the main window's procedure; each function
// is the line it replaced in the code that called it.

// shellPost: the function on the heap, run and deleted by the UI thread (mainProc).
constexpr UINT WM_SHELL_POST = WM_APP + 40;  // lp: a std::function<void()>*, run and deleted by the receiver

void shellRelayout() { layout(); }
void shellMenusChanged() { updateMenus(); }
void shellStatusChanged() { gStatus.invalidate(); }
bool shellLogOpen() { return gLog.wnd != nullptr; }
int shellUiFontLfHeight() {
    LOGFONTW lf{};
    GetObjectW(app.font, sizeof lf, &lf);
    return lf.lfHeight;
}
void shellViewSize(int& w, int& h) {
    RECT r;
    GetClientRect(app.view, &r);
    w = r.right, h = r.bottom;
}
std::chrono::steady_clock::time_point shellProcessStart() {
    const auto now = std::chrono::steady_clock::now();
    FILETIME created{}, x1{}, x2{}, x3{}, nowFt{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &x1, &x2, &x3)) return now;
    GetSystemTimePreciseAsFileTime(&nowFt);
    const auto ft = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    if (ft(nowFt) <= ft(created)) return now;
    return now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::micro>(double(ft(nowFt) - ft(created)) / 10.0));
}
std::wstring shellVersion() { return widen(STARDRAFT_VERSION_NAME); }
// When this .exe was built (its file's time): "built Oct 9 23:48", to tell builds apart; empty when unknown.
std::wstring shellBuildStamp() {
    static const std::wstring stamp = [] {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        FILETIME local;
        SYSTEMTIME st;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa) || !FileTimeToLocalFileTime(&fa.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &st))
            return std::wstring();
        static const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
        wchar_t b[48];
        std::swprintf(b, 48, L"built %s %u %02u:%02u", months[(st.wMonth + 11) % 12], st.wDay, st.wHour, st.wMinute);
        return std::wstring(b);
    }();
    return stamp;
}
void shellSetTitle(const std::wstring& title) { SetWindowTextW(app.main, title.c_str()); }
bool shellHasRail() { return gWsBar != nullptr; }
void shellShowRail(bool show) { ShowWindow(gWsBar, show ? SW_SHOW : SW_HIDE); }
void shellWaitCursor() { SetCursor(LoadCursor(nullptr, IDC_WAIT)); }
void shellMinimapSize(int& w, int& h) {
    RECT r;
    GetClientRect(app.minimap, &r);
    w = r.right, h = r.bottom;
}
void shellRendererSources() {
    app.renderer.setQuality(app.quality);
    app.renderer.setSources(&app.data, &app.ed->unitSprites(), &app.ed->tileset(), app.ed->era());
}
void shellRedrawTabs() {
    if (app.tabs) RedrawWindow(app.tabs, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}
void shellInvalidateTabs() {
    if (app.tabs) InvalidateRect(app.tabs, nullptr, FALSE);
}
void shellInvalidateWorkspaceBar() {
    if (gWsBar) InvalidateRect(gWsBar, nullptr, FALSE);
}
void shellUpdateTabs(stardraft::draw::View& v) {
    if (HWND w = ui::Win32Host::windowOf(v)) UpdateWindow(w);
}
void shellForgetAnims() {
    gAnims.clear(), gAnimsFor = nullptr;
}
void shellPostCommand(int id) { PostMessageW(app.main, WM_COMMAND, id, 0); }
void shellFocusView() { SetFocus(app.view); }
int shellTabSideMenu(const std::wstring& title, int side) {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, title.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (side == 0 ? MF_CHECKED : 0), 1, L"Left");
    AppendMenuW(menu, MF_STRING | (side == 1 ? MF_CHECKED : 0), 2, L"Right");
    const int pick = popupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, app.main);
    DestroyMenu(menu);
    return pick;
}
void shellPost(std::function<void()> fn) {
    auto* f = new std::function<void()>(std::move(fn));
    if (!PostMessageW(app.main, WM_SHELL_POST, 0, LPARAM(f))) delete f;
}
void shellSprayTimer(bool on) {
    if (!app.view) return;
    if (on) SetTimer(app.view, kSprayTimer, 25, nullptr);
    else KillTimer(app.view, kSprayTimer);
}
void shellTrackMouseLeave() {
    TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, app.view, 0};
    TrackMouseEvent(&t);
}
bool shellCtrlOrShiftDown() { return GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0; }
bool shellClearFilter() {
    if (!app.filter) return false;
    gFilterBox.setText(L"");  // (the kit search box: its change runs, and the list is made again)
    return true;
}

unsigned shellDpi() { return app.dpi; }
void shellLogLine(const std::wstring& line) { logLine(line); }
void shellLogError(const std::wstring& line) { logLine(line, true); }
bool shellHasStatusBar() { return app.status != nullptr; }
int shellSpriteCount() { return app.renderer.stats().sprites; }
void shellListSelect(int i) {
    SendMessageW(app.list, LB_SETSEL, FALSE, -1);
    if (i >= 0) SendMessageW(app.list, LB_SETSEL, TRUE, i), SendMessageW(app.list, LB_SETCARETINDEX, i, FALSE);
}
std::vector<int> shellListSelection() {
    std::vector<int> idx;
    const int n = int(SendMessageW(app.list, LB_GETSELCOUNT, 0, 0));
    if (n <= 0) return idx;
    idx.assign(size_t(n), 0);
    SendMessageW(app.list, LB_GETSELITEMS, n, LPARAM(idx.data()));
    return idx;
}
void shellListFill(const std::vector<std::wstring>& rows) {
    SendMessageW(app.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(app.list, LB_RESETCONTENT, 0, 0);
    for (const auto& r : rows) SendMessageW(app.list, LB_ADDSTRING, 0, LPARAM(r.c_str()));
    SendMessageW(app.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(app.list, nullptr, TRUE);
}
std::wstring shellFilterText() { return gFilterBox.text(); }
bool shellViewShown() { return app.view != nullptr; }
void shellToolsChanged() {
    if (app.tool) InvalidateRect(app.tool, nullptr, FALSE);
}
int paintedMenu(HMENU h, int x, int y, HWND owner);  // context_menu.inc
int shellPopupPick(const std::vector<std::wstring>& items, int checked, int x, int y) {
    HMENU m = CreatePopupMenu();
    for (size_t k = 0; k < items.size(); ++k) AppendMenuW(m, MF_STRING | (checked == int(k) ? MF_CHECKED : 0), UINT_PTR(k + 1), items[k].c_str());
    POINT p{x, y};
    ClientToScreen(app.tool, &p);
    const int pick = paintedMenu(m, p.x, p.y, app.tool);
    DestroyMenu(m);
    return pick >= 1 && pick <= int(items.size()) ? pick - 1 : -1;
}
int shellContextMenu(const std::vector<std::wstring>& items) {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    for (size_t k = 0; k < items.size(); ++k) AppendMenuW(m, MF_STRING, UINT_PTR(k + 1), items[k].c_str());
    HWND owner = WindowFromPoint(pt);
    const int pick = popupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, owner ? owner : app.main);
    DestroyMenu(m);
    return pick >= 1 && pick <= int(items.size()) ? pick : 0;
}
void shellOwnerSelect(int i) { SendMessageW(app.owner, CB_SETCURSEL, i, 0); }
void shellPaletteChanged() { unitPaletteRepaint(); }
void shellPaletteRebuilt() { rebuildUnitPalette(); }
std::filesystem::path shellUnsavedRoot() { return unsavedRoot(); }
// The save question (closing with work in it): Save, Do not save or Cancel, in the theme's message box.
int shellAskSaveChanges(bool project) {
    const int r = project ? themedMessageBox(app.main, L"Save the new project?", L"Unsaved changes", MB_YESNOCANCEL | MB_ICONQUESTION,
                                             {L"&Save", L"Do &not save", L"Cancel"})
                          : themedMessageBox(app.main, L"Save changes to the map?", L"Unsaved changes", MB_YESNOCANCEL | MB_ICONQUESTION,
                                             {L"&Save", L"Do &not save", L"Cancel"});
    return r == IDCANCEL ? 2 : r == IDNO ? 1 : 0;
}
bool shellSaveChanges(bool project) { return project ? saveUnsavedProject() : saveMap(false); }
// setMode's windows: the side the mode's tools are docked on, with the palette's view of the mode.
void shellModeSide(Mode m) {
    gLastOnSide[gDock.toolsRight[int(m)] ? 1 : 0] = m;
    const PaletteView& v = gViewByMode[int(m)];
    gPal.grid = v.grid, gPal.sprites = v.sprites, gPal.large = v.large;
}
// The windows that the mode shows (the Map workspace only): the filter and list (Locations), the section box, the search
// and view buttons and the palette (not Locations), and the player.
void shellModeWindows(Mode m) {
    // Units, terrains and doodads are picked in the palette; in Locations the same place lists the locations.
    const bool locs = m == Mode::Locations, mapWs = gWorkspace == Workspace::Map;
    ShowWindow(app.filter, mapWs && locs ? SW_SHOW : SW_HIDE);
    ShowWindow(app.list, mapWs && locs ? SW_SHOW : SW_HIDE);
    if (gPal.sectionBox)
        ShowWindow(gPal.sectionBox, mapWs && (m == Mode::Units || m == Mode::Doodads || m == Mode::Sprites) ? SW_SHOW : SW_HIDE);
    for (HWND h : {gPal.search, gPal.viewBtn, gPal.wnd})
        if (h) ShowWindow(h, mapWs && !locs ? SW_SHOW : SW_HIDE);
    if (app.modeBar) InvalidateRect(app.modeBar, nullptr, FALSE);
    // The player: for units and sprites (their owner and color). Doodads have no player (they are placed as player 1).
    ShowWindow(app.owner, mapWs && (m == Mode::Units || m == Mode::Sprites) ? SW_SHOW : SW_HIDE);
}
std::wstring shellTr(const wchar_t* english) { return tr(english); }
std::vector<int>& shellPaletteChosen() { return gPal.chosen; }
AppState& shellState() { return app; }
void shellRendererGraphics(GraphicsQuality q, bool carbot) { app.renderer.setQuality(q), app.renderer.setCarbot(carbot); }
void shellSettingsChanged() { saveAppSettings(); }
std::filesystem::path shellSettingsFile() {
    if (const wchar_t* file = _wgetenv(L"STARDRAFT_SETTINGS"); file && *file) return file;  // (tests)
    std::filesystem::path dir;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) dir = p;
    CoTaskMemFree(p);
    if (dir.empty()) dir = std::filesystem::temp_directory_path();
    return dir / L"StarDraft" / L"settings.json";
}
// The registry key under HKCU: Software\StarDraft, or Software\StarDraft\<sub> (the '/' of sub are the key separators).
static std::wstring legacyKeyPath(const std::wstring& sub) {
    std::wstring key = L"Software\\StarDraft";
    if (!sub.empty()) {
        std::wstring path = sub;
        std::replace(path.begin(), path.end(), L'/', L'\\');
        key += L"\\" + path;
    }
    return key;
}
bool shellLegacyValue(const std::wstring& sub, const std::wstring& value, LegacyKind kind, std::vector<uint8_t>& out) {
    const DWORD flags = kind == LegacyKind::Dword   ? RRF_RT_REG_DWORD
                        : kind == LegacyKind::Text  ? RRF_RT_REG_SZ
                        : kind == LegacyKind::Texts ? RRF_RT_REG_MULTI_SZ
                                                    : RRF_RT_REG_BINARY;
    const std::wstring key = legacyKeyPath(sub);
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(), flags, nullptr, nullptr, &size) != ERROR_SUCCESS) return false;
    out.assign(size, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(), flags, nullptr, out.data(), &size) != ERROR_SUCCESS) return false;
    out.resize(size);
    return true;
}
void shellLegacyNames(const std::wstring& sub, std::vector<std::wstring>& out) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, legacyKeyPath(sub).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD len = 256;
        if (RegEnumValueW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
    }
    RegCloseKey(k);
}
void shellLegacyRemove(const std::wstring& sub, const std::wstring& value) { RegDeleteKeyValueW(HKEY_CURRENT_USER, legacyKeyPath(sub).c_str(), value.c_str()); }
void shellErrorBox(const std::wstring& text, const std::wstring& title) { themedMessageBox(app.main, text.c_str(), title.c_str(), MB_ICONERROR); }
stardraft::draw::Canvas* shellMeasureCanvas() { return nullptr; }
int shellAskUnapplied(const std::wstring& text, const std::wstring& title) {
    const int r = themedMessageBox(app.main, text.c_str(), title.c_str(), MB_YESNOCANCEL | MB_ICONQUESTION, {L"&Apply", L"Do &not apply", L"Cancel"});
    return r == IDYES ? 0 : r == IDCANCEL ? 2 : 1;  // (any other answer, a box that could not be made included: do not apply, as before)
}
void shellRecentMenu(bool projects, const std::vector<std::wstring>& labels, const std::wstring& none) {
    HMENU m = projects ? gRecentProjectsMenu : gRecentMapsMenu;
    const int firstId = projects ? ID_RECENT_PROJECT : ID_RECENT_MAP;
    const int clearId = projects ? ID_RECENT_CLEAR_PROJECTS : ID_RECENT_CLEAR_MAPS;
    if (!m) return;
    while (GetMenuItemCount(m) > 0) DeleteMenu(m, 0, MF_BYPOSITION);
    for (size_t i = 0; i < labels.size(); ++i) AppendMenuW(m, MF_STRING, UINT_PTR(firstId + int(i)), labels[i].c_str());
    if (labels.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, none.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (labels.empty() ? MF_GRAYED : 0), UINT_PTR(clearId), L"&Clear list");
}
bool shellPathsFoldCase() { return true; }  // NTFS ignores case
bool shellUiIconMask(int size, int icon, std::vector<uint8_t>& out) {
    int stride = 0;
    const uint8_t* m = uiIconMask(size, UiIcon(icon), stride);
    if (!m) return false;
    out.resize(size_t(size) * size_t(size));
    for (int y = 0; y < size; ++y) std::copy_n(m + size_t(y) * size_t(stride), size, out.begin() + std::ptrdiff_t(y) * size);
    return true;
}
void shellShowQuickPicker(bool replace, bool open) { showQuickPicker(replace, open); }
void shellMapFileOpened(const std::filesystem::path& file) { mapFileOpened(file); }
void shellOpenUrl(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void shellOpenFolder(const std::filesystem::path& folder) { ShellExecuteW(app.main, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void shellOpenInEditor(const std::filesystem::path& file) {
    // The editor for .eps ("edit", else "open"); Notepad when nothing is registered for it.
    if (INT_PTR(ShellExecuteW(app.main, L"edit", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32 &&
        INT_PTR(ShellExecuteW(app.main, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        shellOpenWithNotepad(file);
}
void shellOpenWithNotepad(const std::filesystem::path& file) {
    ShellExecuteW(app.main, L"open", L"notepad.exe", (L"\"" + file.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}
void shellRevealFile(const std::filesystem::path& file) {
    ShellExecuteW(app.main, L"open", L"explorer.exe", (L"/select,\"" + std::filesystem::path(file).make_preferred().wstring() + L"\"").c_str(), nullptr,
                  SW_SHOWNORMAL);
}
std::wstring shellFileTimeText(const std::filesystem::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(file.wstring().c_str(), GetFileExInfoStandard, &a)) return {};
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&a.ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t t[32];
    std::swprintf(t, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return t;
}
std::uintptr_t shellStartElevated(const std::filesystem::path& exe, const std::filesystem::path& dir, const std::wstring& args,
                                  const std::wstring& what, std::wstring& error) {
    SHELLEXECUTEINFOW si{sizeof si};
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.hwnd = app.main;
    si.lpVerb = L"runas";  // the program needs administrator rights (its manifest asks for them)
    si.lpFile = exe.c_str();
    si.lpParameters = args.c_str();
    si.lpDirectory = dir.c_str();
    si.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&si) && si.hProcess) return std::uintptr_t(si.hProcess);
    const DWORD e = GetLastError();
    error = e == ERROR_CANCELLED ? what + L" did not start: it needs administrator rights" : L"Could not start " + what + L" (error " + std::to_wstring(e) + L")";
    return 0;
}
bool shellProcessEnded(std::uintptr_t process) { return WaitForSingleObject(HANDLE(process), 0) == WAIT_OBJECT_0; }
void shellProcessClose(std::uintptr_t process) { CloseHandle(HANDLE(process)); }
void shellChooseInstallFolder() { chooseInstallFolder(); }
// (the active window owns the picker: the dialog that asks, else the main window)
std::wstring shellPickFolder(const std::wstring& title) {
    HWND owner = GetActiveWindow();
    const auto picked = platform::pickFolderDialog(owner ? owner : app.main, narrow(title));
    return picked ? picked->wstring() : std::wstring();
}
std::wstring shellPickOutputFile(const std::wstring& name) {
    HWND owner = GetActiveWindow();
    return pickOutputFile(owner ? owner : app.main, name);
}
std::filesystem::path shellDocumentsFolder() {
    wchar_t* docs = nullptr;
    std::filesystem::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) p = docs;
    CoTaskMemFree(docs);
    return p;
}
std::filesystem::path shellPickFile(const ShellFilePick& pick) {
    platform::FileDialog d;
    d.owner = app.main;
    for (const auto& [name, patterns] : pick.filters) d.filters.push_back({narrow(name), narrow(patterns)});
    d.fileName = pick.fileName;
    d.initialDir = pick.initialDir;
    if (!pick.title.empty()) d.title = narrow(pick.title);
    d.defaultExtension = narrow(pick.defaultExtension);
    d.pathMustExist = pick.save;
    const auto picked = pick.save ? platform::saveFileDialog(d) : platform::openFileDialog(d);
    return picked ? *picked : std::filesystem::path();
}
bool shellPromptText(const std::wstring& title, std::wstring& text) { return prompt(title.c_str(), text); }
// (the value pop-out under the field, in screen coordinates: 2 px below it)
bool shellEditValueBelow(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text) {
    POINT at{field.left, field.bottom + scale(2)};
    ClientToScreen(ui::Win32Host::windowOf(v), &at);
    return editTextValue(title, text, at);
}
// (the native value pop-out, main.cpp: showValuePopup, under the field)
bool shellEditValue(stardraft::draw::View& v, stardraft::draw::Rect field, const ValuePopSpec& spec, std::string& out) {
    POINT at{field.left, field.bottom + scale(2)};
    ClientToScreen(ui::Win32Host::windowOf(v), &at);
    return showValuePopup(spec, at, out);
}
// (main.cpp: editGameText, under the field)
bool shellEditGameText(stardraft::draw::View& v, stardraft::draw::Rect field, const std::wstring& title, std::string& text) {
    POINT at{field.left, field.bottom + scale(2)};
    ClientToScreen(ui::Win32Host::windowOf(v), &at);
    return editGameText(title, text, at);
}
// (the RichEdit read a script's bytes in the system's code page: the file may be in Korean or another)
std::wstring shellCodePageText(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
// (the first line of install.txt in the settings folder)
std::wstring shellInstallFolderText() {
    std::ifstream in(settingsPath());
    std::string line;
    std::getline(in, line);
    return widen(line);
}

// The pop-outs that asked to close (shellClosePopout); the loop of each one ends when it sees its own.
std::set<const stardraft::draw::View*> gPopoutsClosing;
void shellClosePopout(stardraft::draw::View& content) { gPopoutsClosing.insert(&content); }
// The pop-out's window: a view window with a thin border and round corners, faded in, owned by the main window. Its loop:
// a press outside the pop-out only closes it (popoutEatsPress); the window closes when it loses activation (the view
// window's Popup kind).
void shellRunPopout(stardraft::draw::View& content, int x, int y, int w, int h, stardraft::draw::View* focus, const std::function<void()>& opened) {
    POINT at{x, y};
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi);
    at.x = std::clamp<LONG>(at.x, mi.rcWork.left, mi.rcWork.right - w);
    at.y = std::clamp<LONG>(at.y, mi.rcWork.top, mi.rcWork.bottom - h);
    gPopoutsClosing.erase(&content);
    HWND wnd = CreateWindowExW(WS_EX_TOOLWINDOW | (gBackground ? WS_EX_NOACTIVATE : 0), ui::kViewClass, L"", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, at.x, at.y,
                               w, h, app.main, nullptr, app.inst, &content);
    if (!wnd) return;
    if (auto* host = dynamic_cast<ui::Win32Host*>(content.host())) host->kind = stardraft::draw::WindowKind::Popup, host->isWindow = true;
    ui::popupFrame(wnd);
    {
        const int round = 2;  // DWMWCP_ROUND, as the quick search
        DwmSetWindowAttribute(wnd, 33, &round, sizeof round);
    }
    fadeIn(wnd, gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!gBackground && focus) focus->focus();
    if (opened) opened();
    MSG msg;
    while (IsWindow(wnd) && !gPopoutsClosing.count(&content) && GetMessageW(&msg, nullptr, 0, 0))
        if (!popoutEatsPress(msg, wnd)) TranslateMessage(&msg), DispatchMessageW(&msg);
    gPopoutsClosing.erase(&content);
    if (IsWindow(wnd)) DestroyWindow(wnd);
}

// Build and the Log (core/project_build.inc).
bool shellAskYesNo(const std::wstring& text, const std::wstring& title, const std::wstring& yes, const std::wstring& no) {
    return themedMessageBox(app.main, text, title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2, {yes, no}) == IDYES;
}
void shellLogNewBuild() {
    gLog.errors = 0, gLog.latestError = false;
    if (gLog.wnd) InvalidateRect(gLog.wnd, nullptr, FALSE);
}
// --- Program options and feedback ---
fs::path shellFeedbackFolder() { return crash::folder().parent_path() / "feedback"; }
