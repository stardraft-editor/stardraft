// Cosmonarchy BW maps (portable, core.inc; src/mods/cmbw.cpp): a tab whose editor reads the mod's files in place of
// the game's, so its palette has the mod's units and its map shows their graphics. Program options > Plugins > Cosmonarchy
// BW: not loaded (the game's data, as always) or loaded (every map opens and is made with the mod's data: gameDataForMap;
// the start page and File > New make Cosmonarchy maps).

// Program options > Plugins is built in or not (STARDRAFT_PLUGINS, CMakeLists.txt: off in release builds for now). Without
// it, the Plugins tab is gone and the mod is never loaded (a GameMode setting of 1 from another build counts as 0).
#ifndef STARDRAFT_PLUGINS
#define STARDRAFT_PLUGINS 1
#endif
constexpr bool kPluginsBuilt = STARDRAFT_PLUGINS != 0;

bool gCmbwForced = false;  // (a snapshot's --cmbw: Cosmonarchy for the run)
int gGameMode = -1;        // 0 Standard, 1 Cosmonarchy (the setting GameMode, read once: the checks run often); -1 not read yet
bool cmbwMode() {
    if (!kPluginsBuilt) return false;
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

// Cosmonarchy's doodad brushes (its brush sets with "doodad" in their names, its trees, and every brush bigger than an
// edge's block of 2 x 3 tiles: ramps, caves, bridges; and the sets of single rects: crevices): entries of the Doodads
// palette on a Cosmonarchy tab, each with a value from its name (kBrushDoodadBase and up, below 65000: pins keep a
// brush when the mod adds or takes away others; brushDoodadValue). A pick places the brush's
// tiles centered under the mouse (its left edge on an even column, as the map's rects), as Custom terrain does; it is
// not a doodad of the map's doodad list (the mod's tilesets have no doodad data for them).
const std::vector<const TerrainStamp*>& brushDoodadList() {
    static std::vector<const TerrainStamp*> list;
    static int era = -1;
    static const std::vector<const TerrainStamp*> none;
    const AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen() || !modTab()) return none;
    if (era != (app.ed->era() & 7)) {
        era = app.ed->era() & 7, list.clear();
        const auto& all = stardraft::cmbw::brushStamps(stardraft::cmbw::findInstall(), uint16_t(era));
        std::map<std::string, bool> singles;  // set -> all its brushes one rect (2 x 1: decoration, as the crevices; not an edge)
        for (const TerrainStamp& s : all) {
            const std::string set = s.name.substr(0, s.name.rfind('_'));
            auto [it, fresh] = singles.try_emplace(set, true);
            it->second = it->second && s.width == 2 && s.height == 1;
        }
        for (const TerrainStamp& s : all) {
            if (singles[s.name.substr(0, s.name.rfind('_'))]) {
                list.push_back(&s);
                continue;
            }
            std::string n = s.name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            // (and every brush bigger than an edge's block, 2 x 3: ramps, caves, bridges, entrances, whatever its set; and the
            // swamp sets: patches of swamp, their islands, edges and fill placed one by one, no border between two terrains)
            if (n.find("doodad") != std::string::npos || n.find("_trees") != std::string::npos || n.find("swamp") != std::string::npos ||
                s.width > 2 || s.height > 3)
                list.push_back(&s);
        }
    }
    return list;
}
// Each brush's value: its name's hash in 40000-64999; two names with one value: the one later by name takes the next
// free value.
const std::map<int, const TerrainStamp*>& brushDoodadValues() {
    static std::map<int, const TerrainStamp*> byValue;
    static const std::vector<const TerrainStamp*>* made = nullptr;
    static size_t madeSize = 0;
    const auto& list = brushDoodadList();
    if (made != &list || madeSize != list.size() || (byValue.empty() && !list.empty()) ||
        (!list.empty() && std::none_of(byValue.begin(), byValue.end(), [&](const auto& kv) { return kv.second == list.front(); }))) {
        byValue.clear();
        std::vector<const TerrainStamp*> sorted(list.begin(), list.end());
        std::sort(sorted.begin(), sorted.end(), [](const TerrainStamp* a, const TerrainStamp* b) { return a->name < b->name; });
        for (const TerrainStamp* s : sorted) {
            uint32_t h = 2166136261u;  // (FNV-1a)
            for (unsigned char c : s->name) h = (h ^ c) * 16777619u;
            int v = kBrushDoodadBase + int(h % 25000u);
            while (byValue.count(v)) v = kBrushDoodadBase + (v - kBrushDoodadBase + 1) % 25000;
            byValue[v] = s;
        }
        made = &list, madeSize = list.size();
    }
    return byValue;
}
int brushDoodadValue(const TerrainStamp* s) {
    for (const auto& [v, b] : brushDoodadValues())
        if (b == s) return v;
    return -1;
}
const TerrainStamp* brushDoodad(int value) {
    if (value < kBrushDoodadBase) return nullptr;
    const auto& m = brushDoodadValues();
    const auto it = m.find(value);
    return it == m.end() ? nullptr : it->second;
}
// Its set (the palette's section: "Structure cliffdoodad grass1") and its own name ("W south 2").
std::pair<std::wstring, std::wstring> brushDoodadParts(const TerrainStamp& s) {
    const auto parts = terrainStampNameParts(s.name);  // Cosmonarchy, the set, the brush
    std::wstring set = parts.size() > 1 ? widen(parts[1]) : widen(s.name), own;
    std::replace(set.begin(), set.end(), L'-', L' ');
    for (size_t i = 2; i < parts.size(); ++i) own += (own.empty() ? L"" : L" ") + widen(parts[i]);
    return {set, own.empty() ? set : own};
}
std::wstring brushDoodadName(const TerrainStamp& s) {
    const auto [set, own] = brushDoodadParts(s);
    return set + L": " + own + L"  (" + std::to_wstring(s.width) + L"x" + std::to_wstring(s.height) + L")";
}
bool brushDoodadFits(AppState& app, const TerrainStamp& s, int tx, int ty);
// The brush's place for a map point (tx, ty: its top left tile) and, with `place`, its tiles there (one undo step, or
// part of an open stroke). True when it went down (or, without `place`, when it is on the map).
bool placeBrushDoodad(AppState& app, const TerrainStamp& s, float wx, float wy, int* tx, int* ty, bool place) {
    Editor& ed = *app.ed;
    const int x0 = (int(wx) / 32 - s.width / 2 + 1) & ~1, y0 = int(wy) / 32 - s.height / 2;  // (the clicked tile in it, its left edge on an even column)
    if (tx) *tx = x0;
    if (ty) *ty = y0;
    std::vector<Editor::TileAt> cells;
    auto put = [&](int ox, int oy) {
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x)
                if (s.has(x, y) && ox + x >= 0 && oy + y >= 0 && ox + x < ed.width() && oy + y < ed.height())
                    cells.push_back({ox + x, oy + y, s.tiles[size_t(y) * s.width + x]});
    };
    put(x0, y0);
    if (!place) return !cells.empty();
    if (cells.empty()) return false;
    // The copies at the symmetry's points (Options > Symmetry), as the game's doodads get them: the same brush there,
    // each left out where it would overlap the brush or a copy before it; all one undo step.
    std::vector<std::pair<int, int>> taken{{x0, y0}};
    const auto points = ed.symmetricPoints(x0 * 32 + s.width * 16, y0 * 32 + s.height * 16);  // (from the brush's middle: its copies mirror it exactly)
    for (size_t k = 1; k < points.size(); ++k) {
        const int cx = (points[k].first / 32 - s.width / 2 + 1) & ~1, cy = points[k].second / 32 - s.height / 2;
        const bool overlaps = std::any_of(taken.begin(), taken.end(), [&](const auto& t) {
            return cx < t.first + s.width && t.first < cx + s.width && cy < t.second + s.height && t.second < cy + s.height;
        });
        if (overlaps || (app.doodadCheck && !brushDoodadFits(app, s, cx, cy))) continue;  // (as the brush itself: only where it joins)
        taken.push_back({cx, cy});
        put(cx, cy);
    }
    return ed.placeTileValues(cells) > 0;
}

// Whether the brush joins the tiles around it at (tx, ty) (its top left tile): its edges' pixels within 10 of the map's
// tiles beside them (cmbw::brushEdgeScore; on the mod's maps 88% of the places that use a brush are within it, 15% of
// any places are), or nothing beside it to join.
bool brushDoodadFits(AppState& app, const TerrainStamp& s, int tx, int ty) {
    const int score = stardraft::cmbw::brushEdgeScore(app.ed->tileset(), s, readTerrain(app.ed->chk(), "MTXM"), tx, ty);
    return score <= 10;
}

// The doodad brush whose tiles are all on the map, as it puts them, under (wx, wy): found by the tile there (each tile of
// each brush, an index made once per tileset). A brush placed by StarDraft, by SCMDraft or by hand: the map's tiles say.
bool brushDoodadAt(AppState& app, float wx, float wy, AppState::BrushAt& out) {
    Editor& ed = *app.ed;
    const int tx = int(wx) / 32, ty = int(wy) / 32;
    if (tx < 0 || ty < 0 || tx >= ed.width() || ty >= ed.height()) return false;
    static std::map<uint16_t, std::vector<std::tuple<int, int, int>>> index;  // tile -> (value, x, y in the brush)
    static const void* indexed = nullptr;
    const auto& values = brushDoodadValues();
    if (indexed != &values || index.empty()) {
        index.clear();
        for (const auto& [v, b] : values)
            for (int y = 0; y < b->height; ++y)
                for (int x = 0; x < b->width; ++x)
                    if (b->has(x, y)) index[b->tiles[size_t(y) * b->width + x]].push_back({v, x, y});
        indexed = &values;
    }
    const TerrainLayer map = readTerrain(ed.chk(), "MTXM");
    const auto it = index.find(map.tiles[size_t(ty) * map.width + tx]);
    if (it == index.end()) return false;
    int bestCount = 0;
    for (const auto& [v, bx, by] : it->second) {
        const TerrainStamp* b = brushDoodad(v);
        if (!b || b->count() <= bestCount) continue;  // (the biggest brush that is whole there)
        const int x0 = tx - bx, y0 = ty - by;
        bool whole = true;
        for (int y = 0; y < b->height && whole; ++y)
            for (int x = 0; x < b->width && whole; ++x)
                if (b->has(x, y)) {
                    const int mx = x0 + x, my = y0 + y;
                    whole = mx >= 0 && my >= 0 && mx < map.width && my < map.height && map.tiles[size_t(my) * map.width + mx] == b->tiles[size_t(y) * b->width + x];
                }
        if (whole && b->count() > 1) out = {v, x0, y0}, bestCount = b->count();
    }
    return bestCount > 0;
}
// Takes the selected doodad brushes away: their tiles made again from the terrain (the ISOM) there; one undo step each.
int deleteBrushDoodads(AppState& app) {
    int n = 0;
    const TerrainLayer map = readTerrain(app.ed->chk(), "MTXM");
    for (const AppState::BrushAt& s : app.selBrushes)
        if (const TerrainStamp* b = brushDoodad(s.value)) {
            bool whole = true;  // (still there as it was selected: an undo may have taken it away)
            for (int y = 0; y < b->height && whole; ++y)
                for (int x = 0; x < b->width && whole; ++x)
                    if (b->has(x, y)) {
                        const int mx = s.tx + x, my = s.ty + y;
                        whole = mx >= 0 && my >= 0 && mx < map.width && my < map.height && map.tiles[size_t(my) * map.width + mx] == b->tiles[size_t(y) * b->width + x];
                    }
            if (whole) n += app.ed->retileFromIsom(s.tx, s.ty, s.tx + b->width - 1, s.ty + b->height - 1);
        }
    app.selBrushes.clear();
    return n;
}

// Moves the selected doodad brushes by (dx, dy) map pixels (whole rects: 64 px across, a tile down): each old place gets
// its terrain's tiles again, the brush its tiles at the new one (where it joins, with Options > Doodads' check); all one
// undo step. Returns how many moved; the selection follows them.
int moveBrushDoodads(AppState& app, float dx, float dy) {
    Editor& ed = *app.ed;
    const int mx = int(std::lround(dx / 64.0f)) * 2, my = int(std::lround(dy / 32.0f));
    if (!mx && !my) return 0;
    auto put = [&](const TerrainStamp& b, int x0, int y0) {
        std::vector<Editor::TileAt> cells;
        for (int y = 0; y < b.height; ++y)
            for (int x = 0; x < b.width; ++x)
                if (b.has(x, y)) cells.push_back({x0 + x, y0 + y, b.tiles[size_t(y) * b.width + x]});
        ed.placeTileValues(cells);
    };
    const std::vector<AppState::BrushAt> selected = app.selBrushes;
    std::vector<AppState::BrushAt> after;
    int n = 0;
    ed.beginStroke();
    for (const AppState::BrushAt& s : selected) {
        const TerrainStamp* b = brushDoodad(s.value);
        if (!b) continue;
        const int nx = s.tx + mx, ny = s.ty + my;
        if (nx < 0 || ny < 0 || nx + b->width > ed.width() || ny + b->height > ed.height()) {
            after.push_back(s);
            continue;
        }
        if (app.doodadCheck && !brushDoodadFits(app, *b, nx, ny)) {  // (does not join there: it stays, nothing changes)
            after.push_back(s);
            continue;
        }
        app.selBrushes = {s};
        if (deleteBrushDoodads(app) == 0) continue;  // (no longer whole there: nothing to move)
        put(*b, nx, ny);
        after.push_back({s.value, nx, ny});
        ++n;
    }
    ed.endStroke();
    app.selBrushes = after;
    return n;
}

// Program options > Cosmonarchy folder: the mod's install that the data opened from (empty: not opened yet), and the one
// chosen for the next start.
fs::path gCmbwInUse, gCmbwNext;
// The plugins line of every report (feedback, crash reports): the loaded plugins, Cosmonarchy BW with its install
// folder's name (Prerelease or Release; not the whole path: it may hold the user's name); `tab`: also whether the
// active tab is a Cosmonarchy BW map (not in a crash report: only settings are read there).
std::string reportPluginsLine(bool tab) {
    if (!cmbwMode()) return "Plugins: none loaded";
    std::string s = "Plugins: Cosmonarchy BW (CMBW) loaded";
    if (!gCmbwInUse.empty()) s += ", its folder " + gCmbwInUse.filename().string();
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

// Program options > Plugins > Temporary override: a terrain rules file (.sdterrain, the Custom isom terrain editor's)
// over the built-in rules and the mod's own file (the settings TerrainOverride and TerrainOverrideOn). It applies to
// the tilesets that load after it is set: maps opened from then on.
fs::path gTerrainOverride;
bool gTerrainOverrideOn = false;
void cmbwApplyTerrainOverride() { stardraft::cmbw::useTerrainOverride(gTerrainOverrideOn ? gTerrainOverride : fs::path()); }
void cmbwUseTerrainOverrideSetting() {
    std::wstring saved;
    int64_t on = 0;
    if (gSettingsOff) return;
    if (prefs::text(L"TerrainOverride", saved)) gTerrainOverride = fs::path(saved);
    prefs::number(L"TerrainOverrideOn", on), gTerrainOverrideOn = on != 0 && !gTerrainOverride.empty();
    cmbwApplyTerrainOverride();
}

// The mod's files, opened at the first use (nullptr and `err`: no Cosmonarchy install).
const GameData* cmbwGameData(std::string& err) {
    static std::unique_ptr<GameData> data;
    if (data) return data.get();
    const fs::path install = stardraft::cmbw::findInstall();
    if (install.empty())
        return err = "Could not find Cosmonarchy BW (a Prerelease or Release folder under Program Files\\Cosmonarchy; Options > Program options: Cosmonarchy folder)", nullptr;
    auto d = std::make_unique<GameData>();
    if (!stardraft::cmbw::open(*d, install, err)) return nullptr;
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
