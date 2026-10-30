// Cosmonarchy BW maps (portable, core.inc; src/mods/cmbw.cpp): a tab whose editor reads the mod's files in place of
// the game's, so its palette has the mod's units and its map shows their graphics. Program options > Plugins > Cosmonarchy
// BW: not loaded (the game's data, as always) or loaded (every map opens and is made with the mod's data: gameDataForMap;
// the start page and File > New make Cosmonarchy maps).

bool gCmbwForced = false;  // (a snapshot's --cmbw: Cosmonarchy for the run)
int gGameMode = -1;        // 0 Standard, 1 Cosmonarchy (the setting GameMode, read once: the checks run often); -1 not read yet
bool cmbwMode() {
    if (gGameMode < 0) {
        int64_t v = 0;  // (tests and screenshot runs: Standard, the default; gSettingsOff)
        if (gSettingsOff || gSelfTest) v = 0;
        else if (!prefs::number(L"GameMode", v)) {  // (the old Cosmonarchy BW mode check box)
            bool on = false;
            prefs::flag(L"CosmonarchyMode", on);
            v = on;
        }
        gGameMode = v == 1 ? 1 : 0;
    }
    return gGameMode == 1 || gCmbwForced;
}
void setGraphics(GraphicsQuality q, bool carbot);  // (core/map_commands.inc)
// Cosmonarchy has SD art only: its maps show in Standard definition (View's HD and Carbot items are gray).
void cmbwKeepSd() {
    const AppState& s = shellState();
    if (cmbwMode() && (s.quality != GraphicsQuality::SD || s.carbot)) setGraphics(GraphicsQuality::SD, false);
}
void updateTitle();  // (core/title.inc: the window's title names the game when it is a mod's)
void setGameMode(int mode) {
    gGameMode = mode;
    prefs::setNumber(L"GameMode", mode);
    cmbwKeepSd();
    updateTitle();
}

// Whether the active tab's map is a mod's map (Cosmonarchy BW): its game data loads its own unit data. Its panes differ
// from a standard map's where the mod does (no upgrades; its triggers, establishing shot, ...).
bool modTab() {
    const AppState& s = shellState();
    return s.ed && s.ed->gameData().unitDataLoader() != nullptr;
}

// Tools > Cosmonarchy > Try to repair the ISOM (a Cosmonarchy tab): the map's ISOM made again from its tiles (Editor::repairIsomFromTiles),
// so that the brush paints where the mod's tools left the ISOM out of step with the tiles. One undo step; the tiles stay.
void setMessage(const std::wstring& m);
// Tools > Cosmonarchy > Check the CRIG commands (a Cosmonarchy tab: the mod's trigger script in the triggers' Comments): the lines the game skips (a command's name
// mistyped: cmbw::crigProblems) and the values that the Classic triggers page marks (cmbw::crigValueProblems), all in
// one list.
void shellWarningBox(const std::wstring& text, const std::wstring& title);
void isomRulesShow();  // (core/views_isom_rules.inc: Tools > Cosmonarchy > Custom isom terrain editor)

void cmbwCheckCommentsCommand() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen() || !modTab()) return;
    stardraft::cmbw::CrigMapInfo info = stardraft::cmbw::crigMapInfo(app.ed->chk(), app.ed->unitTypes().size());
    info.orders = stardraft::cmbw::orderNames(app.ed->gameData()).size();
    std::vector<std::string> problems = stardraft::cmbw::crigProblems(app.ed->chk());
    for (std::string& p : stardraft::cmbw::crigValueProblems(app.ed->chk(), info)) problems.push_back(std::move(p));
    if (problems.empty()) return setMessage(tr(L"The CRIG commands have no problems"));
    const std::wstring nl = {wchar_t(13), wchar_t(10)}, bullet = std::wstring(1, wchar_t(0x2022)) + L" ";
    std::wstring text = std::wstring(tr(L"These CRIG commands in the trigger Comments have problems:")) + nl + nl;
    for (size_t i = 0; i < problems.size() && i < 30; ++i) text += bullet + widen(problems[i]) + nl;
    if (problems.size() > 30) text += bullet + std::to_wstring(problems.size() - 30) + L" more" + nl;
    shellWarningBox(text, tr(L"Check the CRIG commands"));
}

void cmbwRepairIsomCommand() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen() || !modTab()) return;
    std::string err;
    const int n = app.ed->repairIsomFromTiles(err);
    if (n < 0) return setMessage(L"The ISOM could not be made from the tiles: " + widen(err));
    setMessage(n ? L"Made the ISOM again from the tiles: " + std::to_wstring(n) + L" values changed (Undo takes it back)" : L"The ISOM already matches the tiles");
    app.dirty = true;
}

// Program options > Cosmonarchy folder: the mod's install that the data opened from (empty: not opened yet), and the one
// chosen for the next start.
fs::path gCmbwInUse, gCmbwNext;
// The plugins line of every report (feedback, crash reports): the loaded plugins, Cosmonarchy BW with its install
// folder's name (Prerelease or Release; not the whole path: it may hold the user's name); `tab`: also whether the
// active tab is a Cosmonarchy BW map (not in a crash report: only settings are read there).
fs::path cmbwInUse();
std::string reportPluginsLine(bool tab) {
    if (!cmbwMode()) return "Plugins: none loaded";
    std::string s = "Plugins: Cosmonarchy BW (CMBW) loaded";
    if (const fs::path in = cmbwInUse(); !in.empty()) s += ", its folder " + in.filename().string();
    if (tab) s += modTab() ? "; this tab is a CMBW map" : "; this tab is not a CMBW map";
    return s;
}
// The Cosmonarchy folder that the user chose (the setting CosmonarchyFolder): the mod's data opens from it, else from
// the one under Program Files. At the start, before the mod's data opens.
void cmbwUseFolderSetting() {
    std::wstring saved;
    if (!gSettingsOff && prefs::text(L"CosmonarchyFolder", saved) && !saved.empty())
        if (const fs::path in = stardraft::cmbw::installIn(fs::path(saved)); !in.empty()) stardraft::cmbw::useInstall(in);
}

// Program options > Plugins > ISOM terrain rules override: a terrain rules file (.isom, the Custom isom terrain editor's)
// over the built-in rules and the mod's own file (the settings TerrainOverride and TerrainOverrideOn). It applies to
// the tilesets that load after it is set: maps opened from then on.
fs::path gTerrainOverride;
bool gTerrainOverrideOn = false;
void cmbwApplyTerrainOverride() { stardraft::cmbw::useTerrainOverride(gTerrainOverrideOn ? gTerrainOverride : fs::path()); }
struct {
    fs::path f, override;
    bool same = false;
} gOverrideAsked;  // (the last answer: a window asks at each paint, the files are opened once; a save asks again)
bool cmbwIsTerrainOverride(const fs::path& f) {
    if (!gTerrainOverrideOn || f.empty()) return false;
    if (f == gOverrideAsked.f && gTerrainOverride == gOverrideAsked.override) return gOverrideAsked.same;
    std::error_code ec;
    gOverrideAsked = {f, gTerrainOverride, fs::equivalent(f, gTerrainOverride, ec)};
    return gOverrideAsked.same;
}
// The Custom isom terrain editor saved `f`: when it is the override in use, the open map loads its terrain rules again
// (its terrains and its brushes; the map's tiles stay). False: not the override, or no map open.
int gTerrainRulesRev = 0;  // (one more each time: the palette's terrain pictures are made again)
const GameData* cmbwGameData(std::string& err);
bool cmbwReloadTerrainRules(const fs::path& f) {
    gOverrideAsked = {};  // (the file may be new)
    if (!cmbwIsTerrainOverride(f)) return false;
    std::string err;
    const GameData* mod = cmbwGameData(err);
    if (!mod) return false;
    bool any = false;
    auto reload = [&](Editor* ed) {  // (a map of the mod: only its terrain rules come from the file)
        if (ed && ed->isOpen() && &ed->gameData() == mod) ed->reloadTileset(), any = true;
    };
    reload(shellState().ed.get());
    for (Doc& d : gDocs) reload(d.ed.get());  // (the tabs at the back: theirs too)
    if (!any) return false;
    ++gTerrainRulesRev;
    shellPaletteRebuilt();  // (the Terrain palette: the terrains as the file has them)
    return true;
}
void cmbwUseTerrainOverrideSetting() {
    std::wstring saved;
    int64_t on = 0;
    if (gSettingsOff) return;
    if (prefs::text(L"TerrainOverride", saved)) gTerrainOverride = fs::path(saved);
    prefs::number(L"TerrainOverrideOn", on), gTerrainOverrideOn = on != 0 && !gTerrainOverride.empty();
    cmbwApplyTerrainOverride();
}

// The mod's files, opened at the first use (nullptr and `err`: no Cosmonarchy install).
std::mutex gCmbwDataLock;  // (the isom terrain editor opens the mod's data on a worker thread)
std::atomic<bool> gCmbwOpening{false};  // (the mod's data is open or opening: its folder is chosen)
// The mod's folder whose data is open (empty: none yet, or it is opening now: the lock is not waited for).
fs::path cmbwInUse() {
    std::unique_lock<std::mutex> lock(gCmbwDataLock, std::try_to_lock);
    return lock.owns_lock() ? gCmbwInUse : fs::path();
}
const GameData* cmbwGameData(std::string& err) {
    std::lock_guard<std::mutex> lock(gCmbwDataLock);
    static std::unique_ptr<GameData> data;
    if (data) return data.get();
    gCmbwOpening = true;
    const fs::path install = stardraft::cmbw::findInstall();
    if (install.empty())
        return gCmbwOpening = false,
               err = "Could not find Cosmonarchy BW (a Prerelease or Release folder under Program Files\\Cosmonarchy; Options > Program options: Cosmonarchy folder)", nullptr;
    auto d = std::make_unique<GameData>();
    if (!stardraft::cmbw::open(*d, install, err)) return gCmbwOpening = false, nullptr;
    data = std::move(d);
    gCmbwInUse = install;
    return data.get();
}

// The game data a map opens with: the mod's in the Cosmonarchy game mode, else the game's.
const GameData& gameDataForMap(const fs::path& /*path*/) {
    AppState& app = shellState();
    if (!cmbwMode()) return app.data;
    std::string err;
    const GameData* data = cmbwGameData(err);
    if (!data) shellLogLine(L"This map is a Cosmonarchy BW map, but " + widen(err) + L". It opens with the game's units.");
    return data ? *data : app.data;
}

// The start page's New Cosmonarchy map: a 128 x 128 Badlands map with the mod's units, in a tab of its own.
void newCmbwMap() {
    std::string err;
    loadingStart(tr(L"Creating Cosmonarchy map\x2026"));
    shellWaitCursor();
    const GameData* data = loadingTimed(tr(L"Loading game data (CASC/MPQ)\x2026"), 0.4f, [&] { return cmbwGameData(err); });
    auto ed = data ? std::make_unique<Editor>(*data) : nullptr;
    if (!ed || !loadingTimed(tr(L"Creating MTXM and ISOM\x2026"), 0.85f, [&] { return ed->newMap(128, 128, 0, 2, err); })) {  // (Badlands dirt, the New page's first terrain)
        loadingEnd();
        shellErrorBox(widen(err), L"Could not create the Cosmonarchy map");
        return;
    }
    loadingTimed(tr(L"Creating tab and minimap\x2026"), 1.0f, [&] { return addDocument(std::move(ed), false), 0; });
    loadingEnd();
    setMessage(L"Created a Cosmonarchy BW map: Badlands 128 x 128 (" + std::to_wstring(shellState().ed->unitTypes().size()) + L" unit types)");
}
