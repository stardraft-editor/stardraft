// The documents (portable core): the workspaces, the open maps and projects (the tabs), the two modes (classic and
// project), the commands' table of the Map workspace, and the commands that open a document (openProject, openAny).
// Included by core.inc after app_state.inc. The helpers below are defined by the program (main_sdl.cpp and its
// sdl_*.inc files) until they move into the core themselves.

extern bool gSelfTest;  // (recent.inc)
// The editor's state (what the program holds apart from its windows): the host's (shell_sdl.inc: gState).
AppState& shellState();
void setMessage(const std::wstring& m);
void startMark(const char* what);
void updateTitle();
void activateDoc(int i);
void openMap(const std::wstring& path, bool switchToOpen = true, bool project = false);
int findOpenProject(const std::filesystem::path& folder);
bool classicCommandInProject(int id);  // core/map_edit_commands.inc: the project's map has that classic data (L.4)

// Map: the map editor; Data: the project's .dat files; Triggers, Assets: to come; Project: the project.
Workspace gWorkspace = Workspace::Map;

// --- Documents (tabs) ------------------------------------------------------------------------------
// Each open map is a tab with its own editor (and so its own undo history), camera and selection. The
// active one lives in the app state's ed; the others keep theirs here.

struct MapPageCopies;  // map_pages.inc
inline int gDocKeys = 0;  // (Doc::key's count)
struct Doc {
    std::unique_ptr<Editor> ed;  // empty while the document is active (it is in the app state then)
    float camX = 0, camY = 0, zoom = 1;
    bool hasTerrainSel = false, pasting = false, terrainPasting = false;
    Rect terrainSel{};
    std::vector<uint8_t> terrainSelMask;
    int untitled = 1;  // "Untitled N" until saved
    std::shared_ptr<EudProject> project;  // set when the map was opened as (or made into) an EUD project
    int page = 0;  // a page and not a document yet (new_page.inc): kPageNew, kPageStart; `ed` is a small blank map until then
    std::shared_ptr<MapPageCopies> mapPages;  // the map pages' working copies (map_pages.inc)
    int key = ++gDocKeys;  // the tab's own number while it is open (its place in gDocs changes): its palette panels
    Workspace workspace = Workspace::Map;  // the workspace or map page the tab was on when it went to the back: it comes back to it
};
std::vector<Doc> gDocs;
int gUntitledCount = 0;
int gActive = -1;
constexpr int kPageNew = 1, kPageStart = 2;  // Doc::page
int activeDocKey() { return gActive >= 0 && gActive < int(gDocs.size()) ? gDocs[size_t(gActive)].key : 0; }
int activePage() { return gActive >= 0 && gActive < int(gDocs.size()) ? gDocs[size_t(gActive)].page : 0; }
// A real document (a map or a project) is open in the active tab. A page tab (the start page, the New page) holds only
// a small blank map: the commands that act on a map (mapCommand) are gray there and do nothing.
bool docOpen() {
    AppState& app = shellState();
    return gActive >= 0 && gActive < int(gDocs.size()) && !activePage() && app.ed && app.ed->isOpen();
}
EudProject* activeProject() { return gActive >= 0 && gActive < int(gDocs.size()) ? gDocs[size_t(gActive)].project.get() : nullptr; }

// --- The two modes (roadmap L.1) ---------------------------------------------------------------------------------
// A map opened by itself (no project.stardraft or .eds) is in classic mode; a project is in project mode. Project
// commands (Build, Data, the block triggers, project settings) are not there for a classic map, and classic commands
// are there in a project only when its map has that classic data (L.4: a classic map made a project keeps it). The
// menus, the shortcuts and the rail ask these functions. Classic tools add their commands to classicOnlyCommand.
bool classicMode() { return gActive >= 0 && gActive < int(gDocs.size()) && !gDocs[size_t(gActive)].project; }
bool projectMode() { return activeProject() != nullptr; }

bool projectOnlyCommand(int id) {
    switch (id) {
    case ID_PROJ_COMPILE:
    case ID_PROJ_FOLDER:
    case ID_PROJ_ADDSCRIPT:
    case ID_PROJ_SETTINGS:
    case ID_WS_DATA:
    case ID_WS_TRIGGERS:
    case ID_WS_PROJECT:
    case ID_VIEW_TRIGCODE:
    case ID_DATA_TABLE:
    case ID_DATA_ENTRY: return true;
    default: return false;
    }
}

bool classicOnlyCommand(int id) { return id == ID_SCEN_CLASSIC || id == ID_SCEN_CLASSICTRIG; }

// True when the command is there in the mode of the active tab.
bool commandInMode(int id) {
    if (projectOnlyCommand(id)) return projectMode();
    // Map data (the unit, upgrade and tech settings) is for classic maps only; a project's map keeps those sections.
    if (id == ID_SCEN_CLASSIC) return classicMode();
    if (classicOnlyCommand(id)) return classicMode() || (projectMode() && classicCommandInProject(id));
    return true;
}

// The menu commands that updateMenus enables or grays by mode. A classic-only command goes here too.
constexpr int kModeCommands[] = {ID_PROJ_COMPILE, ID_PROJ_FOLDER, ID_PROJ_ADDSCRIPT, ID_PROJ_SETTINGS,
                                 ID_WS_DATA,      ID_WS_TRIGGERS, ID_WS_PROJECT,     ID_VIEW_TRIGCODE, ID_DATA_TABLE,   ID_DATA_ENTRY,  ID_SCEN_CLASSIC, ID_SCEN_CLASSICTRIG};
// Is the command enabled in a tab of that mode (the self-test checks the menus with it)?
bool commandForMode(int id, bool project) { return classicOnlyCommand(id) ? !project : !projectOnlyCommand(id) || project; }

// --- Commands --------------------------------------------------------------------------------------

// The commands that are data (commands.inc): the Map workspace's. gCommandIds gives each one's Win32 id.
stardraft::Commands gCommands;
std::map<int, std::string> gCommandIds;

// --- Opening documents ----------------------------------------------------------------------------

// A project in a folder or a .eds (open already: its tab).
void openProject(const std::filesystem::path& edsOrFolder) {
    AppState& app = shellState();
    auto pr = std::make_shared<EudProject>();
    std::string err;
    if (!pr->open(edsOrFolder, err)) {
        shellErrorBox(widen(err), L"Could not open the project");
        return;
    }
    if (const int open = findOpenProject(pr->folder()); open >= 0) {  // open already: go to its tab
        activateDoc(open);
        addRecent(gRecentProjects, pr->folder());
        setMessage(tr(L"The project ") + widen(pr->name()) + tr(L" is open already"));
        return;
    }
    const size_t before = gDocs.size();
    openMap(pr->inputMap().wstring(), false, true);  // a tab of its own, also when the map is open by itself
    if (gDocs.size() < before || gActive < 0 || !app.ed || app.ed->path() != pr->inputMap()) return;  // the map did not open
    gDocs[size_t(gActive)].project = pr;
    updateTitle();
    removeRecent(gRecentMaps, pr->inputMap());  // listed as the project, not as a map
    addRecent(gRecentProjects, pr->folder());
    rememberProjectsFolder(pr->folder());
    setMessage(L"Opened the project " + widen(pr->name()) + L" (" + std::to_wstring(pr->scripts().size()) + L" scripts; Build: Ctrl+B)");
}

// File > Open and the command line: a project (project.stardraft, a .eds, a folder), or a map. A project's map opens
// as that project.
void openAny(const std::filesystem::path& path) {
    AppState& app = shellState();
    startMark("open: start");
    std::error_code ec;
    if (path.extension() == ".stardraft" || path.extension() == ".eds" || fs::is_directory(path, ec)) {
        openProject(path);
        return;
    }
    const fs::path folder = path.parent_path();
    bool candidate = fs::exists(folder / EudProject::kFileName, ec);
    for (const auto& e : fs::directory_iterator(folder, ec)) candidate |= e.path().extension() == ".eds";
    if (candidate) {
        EudProject pr;
        std::string err;
        if (pr.open(folder, err) && fs::equivalent(pr.inputMap(), path, ec)) {
            openProject(folder);
            return;
        }
    }
    openMap(path.wstring(), true);
    if (app.ed && fs::equivalent(app.ed->path(), path, ec)) shellMapFileOpened(path);  // a map file by itself: ask once if maps open with StarDraft 2
}

// Files dropped on the window (and, on macOS, the files that Finder opens with StarDraft): maps (.scm, .scx, .chk),
// project files (.stardraft, .eds) and project folders go to `open` (openAny, or the host's step before it); the status
// bar names each other file.
void openDropped(const std::vector<std::filesystem::path>& files, const std::function<void(const std::filesystem::path&)>& open) {
    for (const fs::path& f : files) {
        std::wstring ext = f.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        std::error_code ec;
        if (ext == L".scm" || ext == L".scx" || ext == L".chk" || ext == L".stardraft" || ext == L".eds" || fs::is_directory(f, ec)) open(f);
        else setMessage(L"Did not open " + f.filename().wstring() + L": StarDraft 2 opens maps (.scm, .scx, .chk) and projects");
    }
}

// The editor function that finds an open document's editor (views_tabs.inc).
const Editor* docEditor(int i);
// The same file or folder, compared as text (case where the host's file system ignores it, slashes, a relative path, a
// trailing separator): no disk access, as a network share can make it wait.
bool samePath(const fs::path& a, const fs::path& b) {
    auto key = [](const fs::path& p) {
        std::error_code ec;
        const fs::path full = fs::absolute(p, ec);
        std::wstring s = (ec ? p : full).lexically_normal().make_preferred().wstring();
        while (s.size() > 1 && s.back() == fs::path::preferred_separator) s.pop_back();
        if (shellPathsFoldCase()) std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    };
    return !a.empty() && !b.empty() && key(a) == key(b);
}

// The tab that has this project (its folder) or this map by itself open, or -1.
int findOpenProject(const fs::path& folder) {
    for (int i = 0; i < int(gDocs.size()); ++i)
        if (!gDocs[size_t(i)].page && gDocs[size_t(i)].project && samePath(gDocs[size_t(i)].project->folder(), folder)) return i;
    return -1;
}
int findOpenMap(const fs::path& path) {
    for (int i = 0; i < int(gDocs.size()); ++i) {
        const Editor* ed = docEditor(i);
        if (ed && !gDocs[size_t(i)].page && !gDocs[size_t(i)].project && samePath(ed->path(), path)) return i;
    }
    return -1;
}

// Opens a map file in a tab (or the tab that has it open). Defined here so the open is the core's; the native parts are the host's.
void activateDoc(int i);
const GameData& gameDataForMap(const fs::path& path);  // (cmbw.inc)
void addDocument(std::unique_ptr<Editor> ed, bool replaceBlank);
void setMessage(const std::wstring& m);
// `project`: the map of a project, which always has the game's data (a Cosmonarchy map is never a project).
void openMap(const std::wstring& path, bool switchToOpen, bool project) {
    AppState& app = shellState();
    if (const int open = switchToOpen ? findOpenMap(path) : -1; open >= 0) {  // open already: go to its tab
        activateDoc(open);
        addRecent(gRecentMaps, path);
        setMessage(fs::path(path).filename().wstring() + L" is open already");
        return;
    }
    // (the loading view over the workspace: each step with its time, as it is done)
    loadingStart(tr(L"Opening the map\x2026"), fs::path(path).filename().wstring());
    shellWaitCursor();
    const GameData& data = *loadingTimed(tr(L"Loading the game data\x2026"), 0.15f, [&]() -> const GameData* { return project ? &app.data : &gameDataForMap(fs::path(path)); });
    auto ed = std::make_unique<Editor>(data);
    loadingSteps(*ed, 0.15f, 0.85f);
    std::string err;
    if (!ed->open(fs::path(path), err)) {
        loadingEnd();
        shellErrorBox(widen(err), L"Could not open the map");
        return;
    }
    ed->onOpenStep = nullptr;
    loadingTimed(tr(L"Making the tab\x2026"), 1.0f, [&] { return addDocument(std::move(ed), true), 0; });  // (replaces the untouched startup map)
    loadingEnd();
    addRecent(gRecentMaps, path);
    shellMapOpened(path);
    setMessage(L"Opened " + fs::path(path).filename().wstring() + L" (" + widen(tilesetName(app.ed->era())) + L")");
    if (app.ed->isomRebuilt())  // a map made by a tool that writes only the tiles (or protected)
        shellLogLine(L"The map had no isometric terrain data (ISOM). StarDraft 2 made it from the tiles, so the isometric tools work. Saving the map keeps it.");
}

// --- The unsaved project ---------------------------------------------------------------------------------------
// StarDraft starts with a project that is not saved yet: a 128 x 128 badlands map in a project folder under the unsaved
// root (the host's, shell.h), so every workspace works from the start. Save asks where the project goes; closing it
// without saving deletes the folder.
void mapPageClose();
void stashActive();
void showActive();
void setWorkspace(Workspace w);
void fitMap();
void forgetProject(const EudProject* pr);
bool isUnsavedProject(const EudProject* pr) {
    if (!pr) return false;
    std::error_code ec;
    const fs::path root = shellUnsavedRoot();
    return !root.empty() && fs::equivalent(pr->folder().parent_path().parent_path(), root, ec);
}
// Whether an unsaved project holds work: a changed map, triggers, data or scripts. `ed`: the project's map (nullptr:
// the active tab's).
bool unsavedProjectTouched(const EudProject& pr, const Editor* ed = nullptr) {
    if (!ed) ed = appState().ed.get();
    if (ed && (ed->modified() || ed->canUndo())) return true;
    std::error_code ec;
    if (fs::exists(pr.folder() / "triggers.json", ec)) return true;
    if (fs::exists(pr.folder() / "dat", ec) && !fs::is_empty(pr.folder() / "dat", ec)) return true;
    return pr.scripts().size() > 1;
}
void dropUnsavedProject(const std::shared_ptr<EudProject>& pr) {
    if (!isUnsavedProject(pr.get())) return;
    std::error_code ec;
    fs::remove_all(pr->folder().parent_path(), ec);
}
// The folders of unsaved projects whose StarDraft no longer runs (a crash, an older start): each folder's name starts
// with the process id of its program (<process>-<time>). Both programs run it once (newUntitledProject, the start);
// a test or snapshot run leaves the folder alone.
void cleanUnsavedProjects() {
    static bool cleaned = false;
    if (gSelfTest || gSettingsOff || std::exchange(cleaned, true)) return;
    const fs::path root = shellUnsavedRoot();
    if (root.empty()) return;
    std::error_code ec;
    std::vector<fs::path> gone;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        const auto pid = std::uint32_t(std::strtoul(name.c_str(), nullptr, 10));
        if (pid == shellProcessId() || shellProcessRunning(pid)) continue;  // this program's, or another StarDraft's that runs
        gone.push_back(e.path());
    }
    for (const fs::path& p : gone) fs::remove_all(p, ec);
}
void newMap();
// A new project that is not saved yet: a 128 x 128 badlands map in <unsaved root>\<process>-<time>\Untitled. With no
// unsaved root, or when the folder cannot be made, the new map stays a plain map.
void newUntitledProject() {
    AppState& app = appState();
    newMap();
    if (!app.ed || gActive < 0) return;
    cleanUnsavedProjects();
    const fs::path root = shellUnsavedRoot();
    if (root.empty()) return;
    std::error_code ec;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const fs::path parent = root / (std::to_string(shellProcessId()) + "-" + std::to_string(ms));
    fs::create_directories(parent, ec);
    auto pr = std::make_shared<EudProject>();
    std::string err;
    if (!EudProject::create(parent, "Untitled", {}, *pr, err) || !app.ed->save(pr->folder() / "Untitled.scx", err) || !pr->open(pr->file(), err)) {
        shellLogError(L"Cannot make the new project: " + widen(err));  // the map stays: a plain map
        return;
    }
    gDocs[size_t(gActive)].project = pr;
    updateTitle();
    shellInvalidateWorkspaceBar();
    shellInvalidateTabs();
}

// --- The restart (Program options > Language, Restart now) -----------------------------------------------------------
// The program that ends writes the maps and projects it had open to the setting Reopen and starts the program again
// with `--restart <its process id>`; the new program waits until the old one ends (its settings saved), then opens them.
// Both programs read and write the same setting, so either program can restart into the other.
void restartWrite(const std::vector<std::wstring>& files) { prefs::setList(L"Reopen", files); }
// The new program, before it opens anything: up to 15 seconds for the old program to end.
void restartWaitFor(std::uint32_t pid) {
    for (int i = 0; i < 300 && pid && shellProcessRunning(pid); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
// Then the maps and projects of the old program open again (the ones that are still there); the setting goes.
void restartReopen() {
    std::vector<std::wstring> reopen;
    prefs::list(L"Reopen", reopen);
    prefs::remove(L"Reopen");
    std::error_code ec;
    for (const auto& p : reopen)
        if (fs::exists(p, ec)) openAny(p);
}

int gNewDocPage = 0;        // the page of the tab addDocument opens next
bool gReplacePage = false;  // addDocument puts the map in the active tab (the New page's Create)

// Opens `ed` in a new tab. Opening a file (`replaceBlank`) instead takes the place of the active tab
// when that is the untouched blank map the editor starts with.
void addDocument(std::unique_ptr<Editor> ed, bool replaceBlank) {
    AppState& app = appState();
    mapPageClose();  // a map page shows the new map's values only after it opens again
    // The untouched start: a blank map, or the new project nobody worked in yet (its folder goes).
    const EudProject* activePr = gActive >= 0 && gActive < int(gDocs.size()) ? gDocs[size_t(gActive)].project.get() : nullptr;
    const bool freshProject = isUnsavedProject(activePr) && !unsavedProjectTouched(*activePr);
    // A file opened from the start page takes its place; the New page stays unless its Create made the map.
    const int page = activePage();
    bool reuse = replaceBlank && gActive >= 0 && app.ed && page != kPageNew &&
                 (page == kPageStart || freshProject || (!app.ed->modified() && app.ed->path().empty() && !app.ed->canUndo()));
    if (std::exchange(gReplacePage, false) && page && app.ed) reuse = true;
    if (reuse && freshProject) {
        forgetProject(gDocs[size_t(gActive)].project.get());
        dropUnsavedProject(gDocs[size_t(gActive)].project), gDocs[size_t(gActive)].project.reset();
    }
    if (!reuse) {
        stashActive();
        gDocs.emplace_back();
        gActive = int(gDocs.size()) - 1;
    }
    // A map in the place of the blank tab is a tab of its own: its own palette windows and palettes (none of the blank's).
    if (reuse) gDocs[size_t(gActive)].key = ++gDocKeys;
    app.ed = std::move(ed);
    gDocs[size_t(gActive)].page = std::exchange(gNewDocPage, 0);
    if (app.ed->path().empty() && (!reuse || page) && !gDocs[size_t(gActive)].page) gDocs[size_t(gActive)].untitled = ++gUntitledCount;
    app.hasTerrainSel = app.pasting = app.terrainPasting = false;
    app.terrainSelMask.clear();
    startMark("open: add the tab");
    shellRelayout();  // the minimap takes this map's shape
    startMark("open: layout");
    showActive();
    startMark("open: show the map");
    fitMap();
    startMark("open: fit the map");
    if (gWorkspace >= Workspace::Properties) setWorkspace(Workspace::Map);  // a new map opens on the map, not on a map page
}
