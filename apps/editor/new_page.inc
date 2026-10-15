// The New page and the start page (included into main.cpp after the Project page). Each is a page in a tab of its own
// (Doc::page); the tab holds a small blank map until it becomes a document. docs/dev/CONTROLS.md, "New page and start
// page".
//
// The New page is a view (core/views_new_page.inc: the choices, the generator, the painting and the clicks); this file is
// its Windows host: the page's window (a view window), the folders and files, the list pickers, making the map or the
// project, and the tab's page. The start page is a core view too (core/views_start.inc).
//
// New page: the settings of the kind that a card on the start page selected. Back to the start page, then the columns Project
// (the name and folder, only for an EUD project), Terrain (the tileset and the size) and Start (a blank map filled with
// a terrain, a generated map, or an imported map), with a preview of the map under the Start choices; with Generate
// the generator's settings are to the right of the preview. Create makes the map or the project in the same tab.
// Choices that do not apply are hidden or gray.
//
// Start page ("Start"): the project kinds as cards (Classic project, EUD project; Campaign project and Mod project come
// later), a card opens its settings in the same tab; then the recent projects and maps, Open, and the alpha notice
// under them. StarDraft starts with it when "Open the last project on start" is off; File > New shows it.

int topBarHeight();
void newBack();
void newCancel();
bool newSetKind(int kind);
void newReset(int kind);
void pageSync();
void openStartPage();

// The page's window (a view window with the New page's view, gNew) and the start page's window (a view window in it).
HWND gNewWnd = nullptr;
HWND gStartWnd = nullptr;  // the start page's window (in the page's window)

// The New page's host on Windows: the folders and files, the list pickers, the making of the map or the project.
class NewHostProgram final : public NewPageHost {
public:
    std::filesystem::path defaultFolder() override { return defaultProjectsFolder(); }
    std::wstring pickFolder() override { return pickFolderTitled(L"Select the folder for the project"); }
    std::wstring pickOutputFile(const std::wstring& name) override { return ::pickOutputFile(app.main, name); }
    std::wstring pickMapFile() override { return fileDialog(false); }
    void pickList(const std::wstring& title, const std::vector<NewListItem>& items, const std::string& current, draw::Rect below,
                  const std::function<void(const std::string&)>& picked) override {
        std::vector<ListPickItem> list;
        for (const NewListItem& it : items) list.push_back({it.label, L"", it.value});
        POINT at{below.left, below.bottom + scale(2)};
        ClientToScreen(gNewWnd, &at);
        std::string out;
        if (showListPicker(title, list, current, at, out)) picked(out);
    }
    void back() override { newBack(); }
    void cancel() override { newCancel(); }
    bool make() override;
    void message(const std::wstring& text) override { setMessage(text); }
    void pageChanged() override {
        RECT rc;
        GetClientRect(gNewWnd, &rc);
        if (gStartWnd) MoveWindow(gStartWnd, 0, 0, rc.right, rc.bottom, TRUE);
    }
};
NewHostProgram gNewHost;
NewPageView gNew(gNewHost);

// The tab's name: the kind.
const wchar_t* gNewTitle() { return gNew.project ? L"New EUD project" : L"New classic project"; }

// Makes the map (blank, generated or copied) in this tab; for a project, the project folder around it.
bool NewHostProgram::make() {
    if (activePage() != kPageNew) return false;  // the start page: no kind yet
    auto ed = std::make_unique<Editor>(app.data);
    std::string err;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const TerrainGenParams p = gNew.params();
    const bool copy = gNew.project && gNew.copyMap;  // Import an existing map: the project's map is a copy of it (the file stays as it is)
    const std::string from = narrow(gNew.fromMapText());
    const bool ok = copy               ? ed->open(fs::path(from), err)
                    : gNew.terrainOnly ? ed->newMapFromTerrain(fs::path(from), err)
                    : gNew.generate    ? ed->generateMap(p, err)
                                       : ed->newMap(gNew.w, gNew.h, gNew.era, gNew.terrainId(), err);
    if (!ok) {
        themedMessageBox(app.main, widen(err).c_str(), copy ? L"Could not open the map" : gNew.generate ? L"Could not generate the map" : L"Could not create the map",
                         MB_ICONERROR);
        return false;
    }
    const bool project = gNew.project;
    const std::wstring name = gNew.nameText(), location = gNew.locationText(), output = gNew.outputText();
    // What it is: the page's choices, or (the terrain of a map) the map's own tileset and size.
    const std::wstring what = std::wstring(kTilesetNames[ed->era() & 7]) + L" " + std::to_wstring(ed->width()) + L" x " + std::to_wstring(ed->height());
    gReplacePage = true;
    addDocument(std::move(ed));
    if (!project) {
        setMessage(gNew.generate ? L"Generated a map: " + what + L" (seed " + std::to_wstring(p.seed) + L")" : L"Created a map: " + what);
        return true;
    }
    fs::path out = output;
    if (!out.empty() && out.is_relative()) out = fs::path(location) / name / out;
    if (!out.empty() && out.extension().empty()) out += ".scx";
    if (!createProject(fs::path(location), narrow(name), true, err, out)) {
        themedMessageBox(app.main, widen(err).c_str(), L"Could not create the project", MB_ICONERROR);
        return false;
    }
    return true;
}

// Create (the view's button, the Enter key of the page): what is wrong is said under the fields; else the host makes it.
bool newCreate() { return gNew.create(); }

// The page's kind is `kind` (0 a classic project, 1 an EUD project): fresh choices, the open map's tileset.
void newReset(int kind) {
    gNew.setOpenEra(app.ed && app.ed->isOpen() && !activePage() ? uint16_t(app.ed->era() & 7) : uint16_t(0));
    gNew.reset(kind);
}

// The active tab shows `page` (kPageNew or kPageStart).
void newShowScreen(int page) {
    gNew.scroll = 0;
    if (activePage() && activePage() != page) {
        gDocs[size_t(gActive)].page = page;
        pageSync();
    }
    if (activePage()) updateTitle();
    if (app.tabs) InvalidateRect(app.tabs, nullptr, FALSE);
    gNew.changed(false);
}
// The settings of a kind (0 a classic project, 1 an EUD project) in the active tab: a start page becomes the New page
// (with fresh choices, or the same ones after Back). Campaign project and Mod project (2, 3) cannot be selected yet:
// false; a map tab: false.
bool newSetKind(int kind) {
    if ((kind != 0 && kind != 1) || !activePage()) return false;
    if (activePage() == kPageStart && !gNew.keep) newReset(kind);
    gNew.keep = false, gNew.tallest = 0;  // (the page's height starts again)
    gNew.project = kind == 1, gNew.card = kind;
    newShowScreen(kPageNew);
    if (!gNew.importing() && !gNew.previewMade) gNew.schedule(10);
    return true;
}
// Back to the start page (a card opens the settings again with the same choices).
void newBack() {
    if (activePage() != kPageNew) return;
    gNew.card = gNew.project ? 1 : 0, gNew.keep = true;
    newShowScreen(kPageStart);
}

// Cancel: the tab closes (the only tab goes back to the start page).
void newCancel() {
    gNew.keep = false;
    if (gDocs.size() == 1) {
        gDocs[size_t(gActive)].page = kPageStart;
        updateTitle();
        if (app.tabs) InvalidateRect(app.tabs, nullptr, FALSE);
        pageSync();
        return;
    }
    closeDoc(gActive);
}

// Randomize (the view's buttons): the page's own values.
void newRandomize(bool all) { gNew.randomize(all); }

// --- The start page -------------------------------------------------------------------------------------------------

// The date the file was last changed ("" when it is not there).
std::wstring startDate(const fs::path& p) {
    std::error_code ec;
    fs::path f = p;
    if (fs::is_directory(p, ec) && fs::exists(p / EudProject::kFileName, ec)) f = p / EudProject::kFileName;
    const auto ft = fs::last_write_time(f, ec);
    if (ec) return L"";
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
    if (localtime_s(&tm, &tt)) return L"";
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

// The start page is a view window (gStartWnd) that covers the page window while the start page shows; pageSync gives it
// the keyboard then, so the keys and the mouse go to it.
StartView gStartView;

// Opens recent item `i` (as the File menu's lists do: a missing one leaves the list).
void startOpen(bool project, size_t i) { command(int(i) + (project ? ID_RECENT_PROJECT : ID_RECENT_MAP)); }
void startRemove(bool project, size_t i) {
    auto& list = project ? gRecentProjects : gRecentMaps;
    if (i >= list.size()) return;
    const std::wstring name = fs::path(list[i]).filename().wstring();
    list.erase(list.begin() + long(i));
    saveRecent(), refreshRecentMenus();
    setMessage(tr(L"Removed ") + name + tr(L" from the list"));
    gStartView.invalidate();
}


void createPageWindow() {
    if (gNewWnd) return;
    // The page: a view window with the New page's view (core/views_new_page.inc).
    gNewWnd = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 1, 1,
                              app.main, nullptr, app.inst, &gNew);
    darkControl(gNewWnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    // The start page: its own view window, with its own scroll bar (the page's is hidden while it shows).
    gStartWnd = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, gNewWnd, nullptr, app.inst, &gStartView);
    darkControl(gStartWnd, L"DarkMode_Explorer");
    gStartView.onCard = [](int kind) { openNewPage(kind); };
    gStartView.onOpenRecent = [](bool project, size_t i) { startOpen(project, i); };
    gStartView.onRemoveRecent = [](bool project, size_t i) { startRemove(project, i); };
    gStartView.onOpen = [] { command(ID_FILE_OPEN); };
    gStartView.dateOf = [](const fs::path& p) { return startDate(p); };
    gStartView.scrollInfo = [](int max, int page, int pos) {
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, max, UINT(page), pos};
        SetScrollInfo(gStartWnd, SB_VERT, &si, TRUE);
    };
}

// The page of the active tab, over the whole area under the tabs (the rail too); the map's windows are hidden while
// it shows, and shown again after.
std::vector<HWND> gPageHidden;
bool gPageShown = false;
void pageSync() {
    const int p = activePage();
    if (!p && !gPageShown) return;
    if (!p) {
        gPageShown = false;
        ShowWindow(gNewWnd, SW_HIDE);
        for (HWND w : std::exchange(gPageHidden, {}))
            if (IsWindow(w)) ShowWindow(w, SW_SHOWNA);
        setWorkspace(gWorkspace);  // the workspace's own windows as they should be
        return;
    }
    createPageWindow();
    const bool was = gPageShown;
    gPageShown = true;
    RECT rc;
    GetClientRect(app.main, &rc);
    RECT sr{};
    if (app.status) GetWindowRect(app.status, &sr);
    const int top = topBarHeight() + tabStripHeight(), bottom = int(rc.bottom) - (sr.bottom - sr.top) - logPanelHeight();
    // Every other window in the area: hidden (the top bar, the tabs, the log and the status bar stay).
    for (HWND w = GetWindow(app.main, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (w == gNewWnd || w == gTopBar || w == app.tabs || w == app.status || w == gLog.wnd || !IsWindowVisible(w)) continue;
        ShowWindow(w, SW_HIDE);
        gPageHidden.push_back(w);
    }
    SetWindowPos(gNewWnd, HWND_TOP, 0, top, int(rc.right), std::max(1, bottom - top), SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if (!was) gNew.resetScroll(), gStartView.resetScroll();
    const bool start = p == kPageStart;  // the start page's view covers the page; its scroll bar is its own
    gNew.covered = start;
    if (!start) gStartView.resetCards();  // (the New page is shown: the start page's cards are not)
    if (p == kPageNew && !gNew.importing() && !gNew.previewMade) gNew.schedule(10);
    InvalidateRect(gNewWnd, nullptr, FALSE);
    if (GetFocus() == app.view || !GetFocus()) SetFocus(gNewWnd);
    ShowScrollBar(gNewWnd, SB_VERT, !start);
    MoveWindow(gStartWnd, 0, 0, int(rc.right), std::max(1, bottom - top), TRUE);
    ShowWindow(gStartWnd, start ? SW_SHOW : SW_HIDE);
    if (start) {
        gStartView.setCard(gNew.card);
        // The start page takes the focus only from the page window (the map view or nothing had it before); the log keeps it.
        if (GetFocus() == gNewWnd || GetFocus() == gStartWnd) gStartView.focus();
        // Its edit boxes are hidden here (the New page shows them when it paints).
        gNew.hideFields();
    }
}

// The New page with the settings of a kind (0 a classic project, 1 an EUD project): an open one, the start page in the
// active tab (a card), or a new tab (Project > New project). kind -1: the start page (File > New).
void openNewPage(int kind) {
    if (kind < 0) return openStartPage();
    for (int i = 0; i < int(gDocs.size()); ++i)
        if (gDocs[size_t(i)].page == kPageNew) {
            activateDoc(i);
            newSetKind(kind);
            return;
        }
    if (activePage() == kPageStart) {  // the start page becomes the New page
        newSetKind(kind);
        return;
    }
    newReset(kind);
    openPageTab(kPageNew);
}

// The start page: in a tab of its own (an open one is shown).
void openStartPage() {
    for (int i = 0; i < int(gDocs.size()); ++i)
        if (gDocs[size_t(i)].page == kPageStart) return activateDoc(i);
    gNew.keep = false;
    openPageTab(kPageStart);
}

// A new tab for a page, with a small blank map in it until it becomes a document.
void openPageTab(int page) {
    if (app.ed && gWorkspace != Workspace::Map) setWorkspace(Workspace::Map);  // a page tab is not a project: no message
    auto ed = std::make_unique<Editor>(app.data);
    std::string err;
    if (!ed->newMap(64, 64, 0, 1, err)) {
        themedMessageBox(app.main, widen(err).c_str(), L"Could not open the page", MB_ICONERROR);
        return;
    }
    gNewDocPage = page;
    addDocument(std::move(ed));
}
