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
bool shellHasStatusBar() { return app.status != nullptr; }
int shellSpriteCount() { return app.renderer.stats().sprites; }
void shellListSelect(int i) {
    SendMessageW(app.list, LB_SETSEL, FALSE, -1);
    if (i >= 0) SendMessageW(app.list, LB_SETSEL, TRUE, i), SendMessageW(app.list, LB_SETCARETINDEX, i, FALSE);
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
void shellOwnerSelect(int i) { SendMessageW(app.owner, CB_SETCURSEL, i, 0); }
void shellPaletteChanged() { unitPaletteRepaint(); }
void shellPaletteRebuilt() { rebuildUnitPalette(); }
std::wstring shellTr(const wchar_t* english) { return tr(english); }
std::vector<int>& shellPaletteChosen() { return gPal.chosen; }
AppState& shellState() { return app; }
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
void shellShowQuickPicker(bool replace, bool open) { showQuickPicker(replace, open); }
void shellMapFileOpened(const std::filesystem::path& file) { mapFileOpened(file); }
