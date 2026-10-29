// The New page on this system (core/views_new_page.inc: the view; this file is its SDL host). The settings of a kind that a
// card on the start page selected (or Project > New EUD project): the Project, Terrain and Start columns, the preview and
// the generator's settings are the view's. The page shows in the Map workspace in place of the start page (or the map,
// when a map is open) until Cancel (or Esc) or Create. The folders and files come from the system's dialogs (shell_sdl.inc);
// Create makes the map, and for an EUD project the project folder around it (core/project_page.inc: createProject).
// Included by main_sdl.cpp after sdl_map_pages.inc (sdlPickFromList) and before shell_sdl.inc.

// The New page is open in the Map workspace (File > New closes it: the start page shows).
bool gNewOpen = false;
// File > New (and the tabs' +) with a map open: the start page shows in the Map workspace in place of the map, as the
// Windows program's start page tab did, until a card, Esc, another tab or a map or project that opens.
bool gStartOpen = false;

// The New page's host on this system: the folders and files, the list pickers, the making of the map, and Cancel.
class NewHostSdl final : public NewPageHost {
public:
    fs::path defaultFolder() override { return defaultProjectsFolder(); }
    std::wstring pickFolder() override;
    std::wstring pickOutputFile(const std::wstring& name) override;
    std::wstring pickMapFile() override;
    void pickList(const std::wstring& title, const std::vector<NewListItem>& items, const std::string& current, draw::Rect below,
                  const std::function<void(const std::string&)>& picked) override;
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

// A list picker: a dropdown menu under the field (the items' labels, the current one checked); the item picked gives
// its value.
void NewHostSdl::pickList(const std::wstring& title, const std::vector<NewListItem>& items, const std::string& current, draw::Rect below,
                          const std::function<void(const std::string&)>& picked) {
    std::vector<kit::MenuItem> menu;
    for (const NewListItem& it : items) {
        kit::MenuItem m;
        m.label = it.label, m.checked = it.value == current;
        m.run = [picked, v = it.value] { picked(v); };
        menu.push_back(std::move(m));
    }
    (void)title;  // (a dropdown: no Report feedback item, as sdlPopupMenu adds)
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    kit::Menu::popup(gNewView, {below.left, below.bottom}, menu, kit::Menu::measure(menu, &measure), {});
}

// Leaves the page: the map shows again (the start page, when no map is open).
void NewHostSdl::cancel() {
    gNewOpen = false, gStartOpen = false;
    shellRelayout(), shellMenusChanged();
}

// Makes the map with the page's choices, in a tab of its own (an EUD project: the project folder around it, as the
// Windows program's); the page closes.
bool NewHostSdl::make() {
    std::string err;
    const GameData* data = &gState.data;
    const bool copy = gNewView.project && gNewView.copyMap;  // Import an existing map: the project's map is a copy of it (the file stays as it is)
    // (the loading view while the map is made: what, each step, the bar)
    loadingStart(copy ? tr(L"Opening ") + fs::path(gNewView.fromMapText()).filename().wstring()
                      : gNewView.generate ? std::wstring(tr(L"Generating a map")) : std::wstring(tr(L"Making a new map")));
    if (gNewView.cosmonarchy && !(data = loadingTimed(tr(L"game data"), 0.2f, [&] { return cmbwGameData(err); }))) {  // (the mod's units and tiles)
        loadingEnd();
        shellErrorBox(widen(err), shellTr(L"Could not create the Cosmonarchy map"));
        return false;
    }
    auto ed = std::make_unique<Editor>(*data);
    const TerrainGenParams p = gNewView.params();
    if (copy) loadingSteps(*ed, 0.2f, 0.8f);
    const bool ok = copy                   ? ed->open(fs::path(gNewView.fromMapText()), err)
                    : gNewView.terrainOnly ? loadingTimed(tr(L"the map from the other map's terrain"), 0.8f, [&] { return ed->newMapFromTerrain(fs::path(gNewView.fromMapText()), err); })
                    : gNewView.generate  ? loadingTimed(tr(L"the generated map"), 0.8f, [&] { return ed->generateMap(p, err); })
                                         : loadingTimed(tr(L"the map"), 0.8f, [&] { return ed->newMap(gNewView.w, gNewView.h, gNewView.era, gNewView.terrainId(), err); });
    ed->onOpenStep = nullptr;
    if (!ok) {
        loadingEnd();
        shellErrorBox(widen(err), shellTr(copy ? L"Could not open the map" : gNewView.generate ? L"Could not generate the map" : L"Could not create the map"));
        return false;
    }
    if (!copy && !gNewView.terrainOnly && gNewView.generate) {  // the rules' bases (roadmap R49): a Start Location on each, player 1 on the first
        const std::vector<GenBase> bases = generatorBases(p);
        for (size_t k = 0; k < bases.size() && k < 8; ++k) ed->placeUnit(214, uint8_t(k), bases[k].x * 32, bases[k].y * 32 + 16);
    }
    if (!copy && !gNewView.terrainOnly && gNewView.generate && gNewView.doodads.value > 0) {  // Doodads (roadmap R37)
        std::vector<uint16_t> ids;
        for (const DoodadType& t : ed->doodadTypes().all())
            if (t.inPalette && t.needsTiles()) ids.push_back(t.id);
        std::vector<Rect> bases;  // (the rules' open ground stays open, roadmap R49)
        const int r = generatorBaseRadius(p) * 32;
        for (const GenBase& b : generatorBases(p)) bases.push_back({b.x * 32 - r, b.y * 32 - r, b.x * 32 + r, b.y * 32 + r});
        ed->decorate(ids, Rect{0, 0, ed->width() * 32, ed->height() * 32}, gNewView.doodadSpacing(), p.seed, bases);
    }
    const bool project = gNewView.project;
    const std::wstring name = gNewView.nameText(), location = gNewView.locationText(), output = gNewView.outputText();
    // What it is: the tileset and the size (as the Windows program's message says).
    const std::wstring what = std::wstring(kTilesetNames[ed->era() & 7]) + L" " + std::to_wstring(ed->width()) + L" x " + std::to_wstring(ed->height());
    const std::wstring message = gNewView.generate ? L"Generated a map: " + what + L" (seed " + std::to_wstring(p.seed) + L")" : L"Created a map: " + what;
    loadingTimed(tr(L"the tab, the minimap and the panels"), 1.0f, [&] { return addDocument(std::move(ed), false), 0; });  // (a tab of its own)
    loadingEnd();
    gNewOpen = false;
    shellRelayout(), shellMenusChanged();
    if (!project) return setMessage(message), true;
    if (!createProject(fs::path(location), narrow(name), true, err, projectOutputFile(location, name, output))) {
        shellErrorBox(widen(err), shellTr(L"Could not create the project"));
        return false;
    }
    return true;
}

// File > New (-1) shows the start page (in place of the open map too); a card or Project > New EUD project (0 classic, 1
// EUD project) shows the New page with that kind's choices. Both show in the Map workspace; Create makes the map or the
// project in a tab of its own (the open maps and projects stay).
void openNewPageSdl(int kind) {
    if (cmbwMode() && kind != 2) kind = -1;  // (Cosmonarchy: the start page, its Cosmonarchy map card; 2: that card's page)
    if (gWorkspace != Workspace::Map) setWorkspace(Workspace::Map);
    if (kind < 0) {
        gNewOpen = false, gStartOpen = true;
        gStartView.resetScroll();
        shellRelayout(), shellMenusChanged();
        return;
    }
    gStartOpen = false;
    if (!gNewOpen) {
        gNewView.setOpenEra(gState.ed && gState.ed->isOpen() ? uint16_t(gState.ed->era() & 7) : uint16_t(0));
        gNewView.reset(kind);
    }
    gNewView.tallest = 0;
    gNewView.project = kind == 1, gNewView.cosmonarchy = kind == 2, gNewView.card = kind == 2 ? 0 : kind;
    gNewView.changed(false);
    gNewOpen = true;
    if (!gNewView.importing() && !gNewView.previewMade) gNewView.schedule(10);
    shellRelayout(), shellMenusChanged();
}

// The document tabs' page (core/views_tabs.inc: activateDoc). This system has no page tabs: the start page and the New
// page show in the Map workspace in place of the map (sdl_main_view.inc). Another tab (or a map or project that opens)
// shows its map: the start page that File > New showed over a map closes.
void pageSync() {
    gStartOpen = false;
    shellRelayout();
}
// The New page is open, or the start page shows over the open map: it stays in the Map workspace until Create, Cancel
// (Esc) or a card. The other workspaces and their keys and menu items do nothing then (gray), so nobody edits the
// project's data, triggers or pages before its map is made.
bool newPageCovers() { return gNewOpen || (gStartOpen && gState.ed && gState.ed->isOpen()); }

// Esc: the New page closes (as Cancel), or the start page that File > New showed over the open map. False: neither shows.
bool newPageEscape() {
    if (gNewOpen && gNewView.shown()) return gNewView.cancelPage(), true;
    if (gStartOpen && gStartView.shown() && gState.ed && gState.ed->isOpen()) {
        gStartOpen = false;
        shellRelayout(), shellMenusChanged();
        return true;
    }
    return false;
}
// The last tab closed (core/views_tabs.inc): the start page, as File > New (no map is open, so it shows).
void openStartPage() { openNewPageSdl(-1); }

// The tab strip's page tab (core/views_tabs.inc): the page that shows in the Map workspace (sdl_main_view.inc) is the
// active tab, "Start" or the New page's kind.
const wchar_t* gNewTitle();
const wchar_t* shellPageTab() {
    if (gWorkspace != Workspace::Map) return nullptr;
    if (gNewOpen) return gNewTitle();
    return gStartOpen || !(gState.ed && gState.ed->isOpen()) ? L"Start" : nullptr;
}
// Its x: the New page goes (as Cancel), or the start page over a map. With no map, the start page stays (no x).
bool shellPageTabClosable() { return gNewOpen || (gState.ed && gState.ed->isOpen()); }
void shellPageTabClose() {
    if (!newPageEscape() && gNewOpen) gNewView.cancelPage();
}

// The tab's name: the kind (the document tabs name a page with it).
const wchar_t* gNewTitle() { return gNewView.cosmonarchy ? L"New Cosmonarchy map" : gNewView.project ? L"New EUD project" : L"New classic project"; }
