// The Dock and Finder on macOS (mac_dock.h: the Dock icon's menu of recent maps and projects, Build's bar and badge on
// the Dock icon, --check dock), and the files dropped on the window in the Assets workspace (every system): sounds
// import into the map, as Import sounds... does. Included by main_sdl.cpp after sdl_updates.inc.

// --- Drops in the Assets workspace ---------------------------------------------------------------------------------------

std::vector<fs::path> gAssetDrops;  // the sounds of one drop (SDL gives each file as an event): one import, one undo step

void sdlFlushAssetDrops() {
    if (gAssetDrops.empty()) return;
    std::vector<fs::path> files;
    files.swap(gAssetDrops);
    importSoundFilesSdl(files, gState.ed.get());  // (sdl_files.inc: the status bar says what came in)
}

// A file dropped while the Assets workspace shows: a sound (WAV, OGG) goes into the map with the other files of the
// drop; a map or a project opens as on the other workspaces (false); another file is named in the status bar.
bool sdlAssetsDrop(const fs::path& f) {
    if (gWorkspace != Workspace::Files) return false;
    std::wstring ext = f.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    std::error_code ec;
    if (ext == L".scm" || ext == L".scx" || ext == L".chk" || ext == L".stardraft" || ext == L".eds" || fs::is_directory(f, ec)) return false;
    if (ext == L".wav" || ext == L".ogg") {
        if (gAssetDrops.empty()) shellPost(sdlFlushAssetDrops);  // (after the drop's other files)
        gAssetDrops.push_back(f);
        return true;
    }
    setMessage(L"Did not import " + f.filename().wstring() + L": the Assets workspace imports only sounds (WAV, OGG)");
    return true;
}

// --- The Dock (macOS) --------------------------------------------------------------------------------------------------

// A dialog waits for an answer (runModal): what the Dock and Finder give (a recent map, a dropped file) does not open
// then. (Posted work runs while the dialog waits: an open then could change the documents under the dialog's caller.)
bool sdlUiWaits() { return gHost && gHost->modalOpen(); }

// The Dock icon's menu: the recent maps, then the recent projects (as File > Recent maps and Recent projects). While a
// dialog waits, the items show but cannot be clicked.
std::vector<MacDockItem> sdlDockItems() {
    std::vector<MacDockItem> items;
    const bool enabled = !sdlUiWaits();
    for (size_t k = 0; k < gRecentMaps.size() && k < size_t(kRecentMax); ++k)
        items.push_back({narrow(fs::path(gRecentMaps[k]).filename().wstring()), narrow(gRecentMaps[k]), false, enabled});
    for (size_t k = 0; k < gRecentProjects.size() && k < size_t(kRecentMax); ++k) {
        fs::path p = fs::path(gRecentProjects[k]);
        if (!p.has_filename()) p = p.parent_path();  // (a folder with a slash at its end)
        items.push_back({narrow(p.filename().wstring()), narrow(gRecentProjects[k]), true, enabled});
    }
    return items;
}
// A click on one: it opens as from File > Recent (after the save question for the open map). Not while a dialog waits
// (the click came before the dialog opened).
void sdlDockOpen(const std::string& utf8) {
    const fs::path f = fs::path(widen(utf8));
    shellPost([f] {
        if (!sdlUiWaits()) confirmReplace([f] { openAny(f); });
    });
}

// Each turn of the event loop: Build started or ended (core/project_build.inc: gBuilding), the Dock icon shows it.
void sdlDockWatchBuild() {
    static bool was = false;
    if (gBuilding == was) return;
    was = gBuilding;
    macDockBuild(gBuilding, gBuilding || gBuildResult == 1 ? 0 : std::max(1, logState().errors));
}

// The Settings window's tabs (core/views_options.inc: optionsview::kTabs), with their toolbar icons (SF Symbols).
std::vector<MacSettingsTab> sdlSettingsTabs() {
    static const char* const kSymbols[] = {"gearshape", "keyboard", "map", "gamecontroller", "folder", "arrow.down.circle", "play.circle"};
    static_assert(std::size(kSymbols) == size_t(optionsview::kTabCount), "an icon for each Settings tab");
    std::vector<MacSettingsTab> tabs;
    for (int i = 0; i < optionsview::kTabCount; ++i) tabs.push_back({narrow(shellTr(optionsview::kTabs[i].name)), kSymbols[i]});
    return tabs;
}

// --check dock (a snapshot): what the Dock and Finder parts set up, as lines (mac_dock.mm; on Windows and Linux one line
// that says that they are macOS only). The Dock menu shows two sample maps and a project (a snapshot saves nothing).
// Another name is another part's check: nothing.
void sdlDockCheck(const std::string& name) {
    if (name != "dock") return;
    const auto maps = gRecentMaps, projects = gRecentProjects;
    gRecentMaps = {(fs::path(L"maps") / L"(2)Bottleneck.scm").wstring(), (fs::path(L"maps") / L"(4)Lost Temple.scx").wstring()};
    gRecentProjects = {(fs::path(L"projects") / L"AshenCrown").wstring()};
    std::printf("dock: window drops: %s\n", gHost ? "the drop hook takes them (SDL_EVENT_DROP_FILE)" : "no host (WRONG)");
    std::printf("%s", macDockCheck(sdlSettingsTabs()).c_str());
    std::printf("dock: dialog waits: %s\n", sdlUiWaits() ? "yes (WRONG: a snapshot has none)" : "no; the menu's items and the drops open");
    gRecentMaps = maps, gRecentProjects = projects;
}
