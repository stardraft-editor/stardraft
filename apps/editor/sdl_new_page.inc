// The New page on this system (core/views_new_page.inc: the view; this file is its SDL host). The settings of a kind that a
// card on the start page selected (or Project > New EUD project): the Project, Terrain and Start columns, the preview and
// the generator's settings are the view's. The page shows in the Map workspace in place of the start page (or the map,
// when a map is open) until Back, Cancel or Create. The folders and files come from the system's dialogs (shell_sdl.inc);
// Create makes the map, and for an EUD project the project folder around it (core/project_page.inc: createProject).
// Included by main_sdl.cpp after sdl_map_pages.inc (sdlPickFromList) and before shell_sdl.inc.

// The New page is open in the Map workspace (File > New closes it: the start page shows).
bool gNewOpen = false;

// The New page's host on this system: the folders and files, the list pickers, the making of the map, Back and Cancel.
class NewHostSdl final : public NewPageHost {
public:
    fs::path defaultFolder() override { return defaultProjectsFolder(); }
    std::wstring pickFolder() override;
    std::wstring pickOutputFile(const std::wstring& name) override;
    std::wstring pickMapFile() override;
    void pickList(const std::wstring& title, const std::vector<NewListItem>& items, const std::string& current, draw::Rect below,
                  const std::function<void(const std::string&)>& picked) override;
    void back() override;
    void cancel() override;
    bool make() override;
    void message(const std::wstring& text) override { setMessage(text); }
    void pageChanged() override {
        if (gRoot) gRoot->invalidate();
    }
};
NewHostSdl gNewHost;
NewPageView gNewView(gNewHost);  // (declared extern in sdl_main_view.inc: the root view shows it in the Map workspace)

// The folders and files: the system's dialogs (shell_sdl.inc).
std::wstring NewHostSdl::pickFolder() { return shellPickFolder(L"Select the folder for the project"); }
std::wstring NewHostSdl::pickOutputFile(const std::wstring& name) { return shellPickOutputFile(name); }
std::wstring NewHostSdl::pickMapFile() {
    ShellFilePick d;
    d.filters = {{L"StarCraft maps (*.scx;*.scm)", L"*.scx;*.scm"}, {L"Scenario data (*.chk)", L"*.chk"}, {L"All files", L"*.*"}};
    return shellPickFile(d).wstring();
}

// A list picker: the rows are the items' labels, the row picked is its value (sdlPickFromList, a modal dialog).
void NewHostSdl::pickList(const std::wstring& title, const std::vector<NewListItem>& items, const std::string& current, draw::Rect,
                          const std::function<void(const std::string&)>& picked) {
    std::vector<std::wstring> rows;
    int checked = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        rows.push_back(items[i].label);
        if (items[i].value == current) checked = int(i);
    }
    const int i = sdlPickFromList(rows, checked, title);
    if (i >= 0 && i < int(items.size())) picked(items[size_t(i)].value);
}

// Leaves the page: the start page shows again (the map, when one is open, shows).
void NewHostSdl::back() {
    gNewView.card = gNewView.project ? 1 : 0, gNewView.keep = true;  // (a card opens the settings again with the same choices)
    gNewOpen = false;
    shellRelayout(), shellMenusChanged();
}
void NewHostSdl::cancel() {
    gNewView.keep = false;
    gNewOpen = false;
    shellRelayout(), shellMenusChanged();
}

// Makes the map with the page's choices, in a tab of its own (an EUD project: the project folder around it, as the
// Windows program's); the page closes.
bool NewHostSdl::make() {
    auto ed = std::make_unique<Editor>(gState.data);
    std::string err;
    const TerrainGenParams p = gNewView.params();
    const bool copy = gNewView.project && gNewView.copyMap;  // Import an existing map: the project's map is a copy of it (the file stays as it is)
    const bool ok = copy                   ? ed->open(fs::path(narrow(gNewView.fromMapText())), err)
                    : gNewView.terrainOnly ? ed->newMapFromTerrain(fs::path(narrow(gNewView.fromMapText())), err)
                    : gNewView.generate  ? ed->generateMap(p, err)
                                         : ed->newMap(gNewView.w, gNewView.h, gNewView.era, gNewView.terrainId(), err);
    if (!ok) {
        shellErrorBox(widen(err), shellTr(copy ? L"Could not open the map" : gNewView.generate ? L"Could not generate the map" : L"Could not create the map"));
        return false;
    }
    const bool project = gNewView.project;
    const std::wstring name = gNewView.nameText(), location = gNewView.locationText(), output = gNewView.outputText();
    // What it is: the tileset and the size (as the Windows program's message says).
    const std::wstring what = std::wstring(kTilesetNames[ed->era() & 7]) + L" " + std::to_wstring(ed->width()) + L" x " + std::to_wstring(ed->height());
    const std::wstring message = gNewView.generate ? L"Generated a map: " + what + L" (seed " + std::to_wstring(p.seed) + L")" : L"Created a map: " + what;
    addDocument(std::move(ed), true);
    gNewOpen = false;
    shellRelayout(), shellMenusChanged();
    if (!project) return setMessage(message), true;
    if (!createProject(fs::path(location), narrow(name), true, err, projectOutputFile(location, name, output))) {
        shellErrorBox(widen(err), shellTr(L"Could not create the project"));
        return false;
    }
    return true;
}

// File > New (-1) shows the start page; a card or Project > New EUD project (0 classic, 1 EUD project) shows the New page with
// that kind's choices (fresh ones, or the same ones after Back).
void openNewPageSdl(int kind) {
    if (kind < 0) {
        gNewOpen = false, gNewView.keep = false;
        shellRelayout(), shellMenusChanged();
        return;
    }
    if (!gNewOpen && !gNewView.keep) {
        gNewView.setOpenEra(gState.ed && gState.ed->isOpen() ? uint16_t(gState.ed->era() & 7) : uint16_t(0));
        gNewView.reset(kind);
    }
    gNewView.keep = false, gNewView.tallest = 0;
    gNewView.project = kind == 1, gNewView.card = kind;
    gNewView.changed(false);
    gNewOpen = true;
    if (!gNewView.importing() && !gNewView.previewMade) gNewView.schedule(10);
    shellRelayout(), shellMenusChanged();
}

// The document tabs' page (core/views_tabs.inc: activateDoc). This system has no page tabs: the start page and the New
// page show in the Map workspace in place of the map (sdl_main_view.inc), so another tab only lays the panes out again.
void pageSync() { shellRelayout(); }
// The last tab closed (core/views_tabs.inc): the start page, as File > New (no map is open, so it shows).
void openStartPage() { openNewPageSdl(-1); }

// The tab's name: the kind (the document tabs name a page with it).
const wchar_t* gNewTitle() { return gNewView.project ? L"New EUD project" : L"New classic project"; }
