// Cosmonarchy BW maps (portable, core.inc; src/mods/cmbw.cpp): a tab whose editor reads the mod's files in place of
// the game's, so its palette has the mod's units and its map shows their graphics. Other tabs keep the game's data. The
// start page's Cosmonarchy map card makes one (Program options > Start > Show Cosmonarchy BW shows it); a map opens as
// one when it was saved from such a tab, is in the Cosmonarchy folder, or has the mod's own units (gameDataForMap).

// Program options > Start > Show Cosmonarchy BW (or a snapshot's --cmbw): only then do maps that open or save look for
// Cosmonarchy (a map's second read, the install's folder); without it, maps open and save as they always did.
bool gCmbwForced = false;
// Program options > Start > Cosmonarchy BW mode: StarDraft is a Cosmonarchy BW editor; every map opens with the mod's
// data (gameDataForMap), and the start page and File > New make Cosmonarchy maps only.
bool cmbwMode() {
    bool on = false;
    prefs::flag(L"CosmonarchyMode", on);
    return on;
}
bool cmbwOn() {
    bool on = false;
    prefs::flag(L"Cosmonarchy", on);
    return on || gCmbwForced || cmbwMode();
}

// Whether the active tab's map is a mod's map (Cosmonarchy BW): its game data loads its own unit data. Its panes differ
// from a standard map's where the mod does (no upgrades; its triggers, establishing shot, ...).
bool modTab() {
    const AppState& s = shellState();
    return s.ed && s.ed->gameData().unitDataLoader() != nullptr;
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

// The mod's files, opened at the first use (nullptr and `err`: no Cosmonarchy install).
const GameData* cmbwGameData(std::string& err) {
    static std::unique_ptr<GameData> data;
    if (data) return data.get();
    const fs::path install = stardraft::cmbw::findInstall();
    if (install.empty()) return err = "Could not find Cosmonarchy BW (a Prerelease or Release folder under Program Files\\Cosmonarchy)", nullptr;
    auto d = std::make_unique<GameData>();
    if (!stardraft::cmbw::open(*d, install, err)) return nullptr;
    data = std::move(d);
    return data.get();
}

// The maps saved from a Cosmonarchy tab (the setting CosmonarchyMaps: their paths in lower case), so they open as
// Cosmonarchy maps again.
std::wstring cmbwMapKey(const fs::path& p) {
    std::error_code ec;
    std::wstring s = fs::weakly_canonical(p, ec).make_preferred().wstring();
    if (ec || s.empty()) s = p.wstring();
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}
std::vector<std::wstring> cmbwSavedMaps() {
    std::vector<std::wstring> v;
    prefs::list(L"CosmonarchyMaps", v);
    return v;
}
// After a save: `path` is on the list when `data` is the mod's, else off it.
void cmbwRememberMap(const fs::path& path, const GameData& data) {
    if (gSelfTest || gSettingsOff || !cmbwOn()) return;  // (tests leave the user's settings alone)
    const bool mod = data.unitDataLoader() != nullptr;
    std::vector<std::wstring> v = cmbwSavedMaps();
    const std::wstring key = cmbwMapKey(path);
    const bool has = std::find(v.begin(), v.end(), key) != v.end();
    if (mod == has) return;
    if (mod) v.insert(v.begin(), key);
    else std::erase(v, key);
    if (v.size() > 200) v.resize(200);
    prefs::setList(L"CosmonarchyMaps", v);
}

// The game data a map opens with: the mod's for a map saved from a Cosmonarchy tab, one in the Cosmonarchy folder, or
// one with the mod's own units (cmbw::looksLikeModMap); else the game's.
const GameData& gameDataForMap(const fs::path& path) {
    AppState& app = shellState();
    if (!cmbwOn()) return app.data;
    const std::vector<std::wstring> saved = cmbwSavedMaps();
    const bool mod = cmbwMode() || std::find(saved.begin(), saved.end(), cmbwMapKey(path)) != saved.end() || stardraft::cmbw::looksLikeModMap(path);
    if (!mod) return app.data;
    std::string err;
    const GameData* data = cmbwGameData(err);
    if (!data) shellLogLine(L"This map is a Cosmonarchy BW map, but " + widen(err) + L". It opens with the game's units.");
    return data ? *data : app.data;
}

// The start page's New Cosmonarchy map: a 128 x 128 Badlands map with the mod's units, in a tab of its own.
void newCmbwMap() {
    std::string err;
    const GameData* data = cmbwGameData(err);
    auto ed = data ? std::make_unique<Editor>(*data) : nullptr;
    shellWaitCursor();
    if (!ed || !ed->newMap(128, 128, 0, 2, err)) {  // (Badlands dirt, the New page's first terrain)
        shellErrorBox(widen(err), L"Could not create the Cosmonarchy map");
        return;
    }
    addDocument(std::move(ed), false);
    setMessage(L"Created a Cosmonarchy BW map: Badlands 128 x 128 (" + std::to_wstring(shellState().ed->unitTypes().size()) + L" unit types)");
}
