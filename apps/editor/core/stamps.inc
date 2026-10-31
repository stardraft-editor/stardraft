// Custom terrain (portable, core.inc): tiles that the user saves from a map with a name, to place again on any map of
// the same tileset. Terrain mode's tool strip has Isometric, Rectangle and Custom; with Custom, the panel lists the
// custom terrain of the map's tileset (views_stamps.inc) in place of the Tiles grid, and the map's tools are
// Rectangle's (tiles_edit.inc): a pick in the list is the Brush's stamp (its tiles as they are; the tiles that are not
// in it stay as they are on the map), a click places it, a drag places it again along the way (one undo step), Esc or a
// right click stops. To make one: select tiles with the Select tool (Shift adds tiles, Ctrl takes them out: any shape),
// then right-click the selection > Save as custom terrain.
// Each is a file in the user's folder (shellStampsFolder: <folder>/<tileset>/<name>.sdstamp; the format is
// stardraft/terrain_stamp.h). "_" in a name makes groups in the list: "Jungle_Bridges_Left" is Left in Bridges in Jungle.

// The custom terrain of the open map's tileset, read from the folder when the tileset or the folder changes, or after
// a change (stampsChanged).
struct StampsCache {
    std::vector<TerrainStamp> list;  // by name
    int era = -1;
    fs::path folder;
    bool stale = true;
    bool mod = false;  // with a mod's brushes (a Cosmonarchy map)
};
StampsCache gStamps;
// The list's order: 0 by name (A to Z), 1 the newest first (in the settings).
int gStampsSort = -1;

int stampsSort() {
    if (gStampsSort < 0) {
        gStampsSort = 0;
        if (!gSelfTest && !gSettingsOff) prefs::number(L"CustomTerrainSort", gStampsSort, 0, 1);
    }
    return gStampsSort;
}
void stampsPanelsRepaint();  // (views_stamps.inc: each panel's list)
void setStampsSort(int sort) {
    gStampsSort = std::clamp(sort, 0, 1);
    if (!gSelfTest && !gSettingsOff) prefs::setNumber(L"CustomTerrainSort", gStampsSort);
    stampsPanelsRepaint();
}

const std::vector<TerrainStamp>& stampsList() {
    static const std::vector<TerrainStamp> none;
    const AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return none;
    const int era = app.ed->era() & 7;
    const fs::path folder = shellStampsFolder();
    const bool mod = modTab();
    if (gStamps.stale || gStamps.era != era || gStamps.folder != folder || gStamps.mod != mod) {
        gStamps.list = terrainStamps(folder, uint16_t(era));
        if (mod)  // a Cosmonarchy map: the mod's brushes after the user's own (core/cmbw.inc)
            for (const TerrainStamp& b : stardraft::cmbw::brushStamps(stardraft::cmbw::findInstall(), uint16_t(era))) gStamps.list.push_back(b);
        gStamps.era = era, gStamps.folder = folder, gStamps.stale = false, gStamps.mod = mod;
    }
    return gStamps.list;
}
// The custom terrain of this name in the list (nullptr: none).
const TerrainStamp* stampsFind(const std::wstring& name) {
    for (const TerrainStamp& s : stampsList())
        if (widen(s.name) == name) return &s;
    return nullptr;
}
// The files changed (a save, a rename, a delete): the lists read them again.
void stampsChanged() {
    gStamps.stale = true;
    stampsPanelsRepaint();
    appState().dirty = true;
}

// The custom terrain becomes the Brush's stamp: its tiles as they are, those not in it left out (0xFFFF), and its null
// tiles too (no picture: tile 0, or a tile of megatile 0): the map keeps its own tile there.
void stampsUse(const TerrainStamp& s) {
    AppState& app = appState();
    gTiles->stamp.clear(), gTiles->chosen.clear();
    for (size_t i = 0; i < s.tiles.size() && i < s.mask.size(); ++i) {
        const uint16_t v = s.mask[i] && !tileIsNull(s.tiles[i]) ? s.tiles[i] : uint16_t(0xFFFF);
        gTiles->stamp.push_back(v);
        if (v != 0xFFFF && std::find(gTiles->chosen.begin(), gTiles->chosen.end(), v) == gTiles->chosen.end()) gTiles->chosen.push_back(v);
    }
    if (gTiles->chosen.empty()) {
        gTiles->stamp.clear();
        return;
    }
    gTiles->stampW = s.width, gTiles->stampH = s.height, gTiles->stampName = widen(s.name), gTiles->stampEra = s.era & 7;
    gTiles->picked = gTiles->anchor = gTiles->chosen.front();
    app.terrainPasting = false;
    if (app.mode == Mode::Terrain && currentTool(app) != TBrush) {  // (the stamp is the Brush's)
        app.toolByMode[int(Mode::Terrain)] = TBrush;
        fillToolCombo(app);
    }
    setMessage(tr(L"Brush: ") + widen(s.name) + L" (" + std::to_wstring(s.width) + L" x " + std::to_wstring(s.height) + L", " +
               std::to_wstring(s.count()) + tr(L" tiles). Click to place it. Drag to place it again along the way. Right-click or Esc stops."));
    app.dirty = true;
    stampsPanelsRepaint();
}

// The name typed in the prompt: no spaces at its ends.
std::wstring stampsTrim(std::wstring t) {
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t')) t.pop_back();
    size_t i = 0;
    while (i < t.size() && (t[i] == L' ' || t[i] == L'\t')) ++i;
    return t.substr(i);
}
// A name the user typed: true when it is good, else an error box says why.
bool stampsNameOk(const std::wstring& name, const wchar_t* title) {
    std::string why;
    if (terrainStampNameOk(narrow(name), why)) return true;
    shellErrorBox(widen(why), tr(title));
    return false;
}
// The file of `name` is there already (another custom terrain): replace it? (false: no change)
bool stampsReplaceAsked(const fs::path& file, const std::wstring& name, const wchar_t* title) {
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    return shellAskYesNo(tr(L"Custom terrain with the name \"") + name + tr(L"\" is already there for this tileset. Replace it?"), tr(title),
                         tr(L"&Replace"), tr(L"Cancel"));
}

std::wstring gStampsLastName;  // the name typed last (the prompt starts with it)

// Right-click the terrain selection > Save as custom terrain: its tiles (the selection's tiles in their box; the others
// are not in it) under a name that the user types. A name that is there already for this tileset: Replace?
void stampsSaveSelection() {
    static const wchar_t* const kTitle = L"Save as custom terrain";
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    const std::vector<std::pair<int, int>> tiles = terrainSelTiles();
    if (tiles.empty()) return setMessage(L"First, select tiles with the Select tool");
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& [x, y] : tiles) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    TerrainStamp s;
    s.era = uint16_t(app.ed->era() & 7);
    s.width = x1 - x0 + 1, s.height = y1 - y0 + 1;
    s.created = int64_t(std::time(nullptr));
    s.tiles.assign(size_t(s.width) * size_t(s.height), 0);
    s.mask.assign(s.tiles.size(), 0);
    const std::vector<uint16_t>& map = app.ed->terrain().tiles;
    for (const auto& [x, y] : tiles) {
        const size_t i = size_t(y - y0) * size_t(s.width) + size_t(x - x0);
        s.tiles[i] = map[size_t(y) * size_t(app.ed->width()) + size_t(x)];
        s.mask[i] = 1;
    }
    std::wstring name = gStampsLastName;
    if (!shellPromptText(tr(kTitle), name)) return;
    name = stampsTrim(name);
    if (!stampsNameOk(name, kTitle)) return;
    gStampsLastName = name;
    s.name = narrow(name);
    const fs::path folder = shellStampsFolder();
    if (!stampsReplaceAsked(terrainStampPath(folder, s.era, s.name), name, kTitle)) return setMessage(L"The custom terrain is not saved");
    std::string err;
    if (!writeTerrainStamp(folder, s, err)) return shellErrorBox(widen(err), tr(kTitle));
    stampsChanged();
    setMessage(tr(L"Saved the custom terrain ") + name + L" (" + std::to_wstring(s.count()) +
               tr(gTiles->stamps ? L" tiles). The Custom list shows it." : L" tiles). To place it, select Custom in the tool strip."));
}

// Custom terrain of tileset `era` got the name `now` (empty: it is deleted): each panel whose Brush has it follows
// (views_stamps.inc: every panel's tiles).
void stampsRenamedInPanels(int era, const std::wstring& was, const std::wstring& now);

// The list's menu: Rename...
void stampsRename(TerrainStamp s) {  // (a copy: the list is read again)
    static const wchar_t* const kTitle = L"Rename custom terrain";
    const std::wstring was = widen(s.name);
    std::wstring name = was;
    if (!shellPromptText(tr(kTitle), name)) return;
    name = stampsTrim(name);
    if (name == was || !stampsNameOk(name, kTitle)) return;
    const fs::path folder = shellStampsFolder(), old = s.file;
    s.name = narrow(name);
    const bool sameFile = terrainStampSameName(narrow(was), s.name);  // (only the case changed: the same file)
    if (!sameFile && !stampsReplaceAsked(terrainStampPath(folder, s.era, s.name), name, kTitle)) return;
    std::string err;
    if (!writeTerrainStamp(folder, s, err)) return shellErrorBox(widen(err), tr(kTitle));
    std::error_code ec;
    if (!sameFile) fs::remove(old, ec);
    stampsRenamedInPanels(s.era, was, name);
    stampsChanged();
    setMessage(tr(L"Renamed the custom terrain to ") + name);
}

// The list's menu: Delete... (after a question; the file is deleted, not undone).
void stampsDelete(const TerrainStamp& s) {
    const std::wstring name = widen(s.name);
    if (!shellAskYesNo(tr(L"Delete the custom terrain \"") + name + tr(L"\"? You cannot undo this."), tr(L"Delete custom terrain"), tr(L"&Delete"),
                       tr(L"Cancel")))
        return;
    std::error_code ec;
    if (!fs::remove(s.file, ec) || ec) return shellErrorBox(tr(L"Cannot delete the file ") + s.file.wstring(), tr(L"Delete custom terrain"));
    stampsRenamedInPanels(s.era, name, L"");  // (a panel's Brush that has it: nothing is picked there now)
    stampsChanged();
    setMessage(tr(L"Deleted the custom terrain ") + name);
}

// The menu of an item of the list (Rename, Delete, Show in folder).
void stampsItemMenu(const TerrainStamp& s) {
    if (s.file.empty()) return;  // (a mod's brush: not the user's file)
    const TerrainStamp copy = s;  // (a menu's run reads the list again)
    const int pick = shellContextMenu({tr(L"Rename\x2026"), tr(L"Delete\x2026"), tr(L"Show in folder")});
    if (pick == 1) stampsRename(copy);
    else if (pick == 2) stampsDelete(copy);
    else if (pick == 3) shellRevealFile(copy.file);
}

// The quick picker's rows in Custom (Q): each custom terrain of the tileset, found by its name; the value is its index
// in stampsList().
std::vector<PaletteUnit> quickPickStamps() {
    std::vector<PaletteUnit> out;
    const auto& list = stampsList();
    for (size_t i = 0; i < list.size(); ++i) {
        PaletteUnit u;
        u.value = int(i);
        u.label = widen(list[i].name);
        u.section = std::wstring(tr(L"Custom terrain")) + L"  \x00B7  " + std::to_wstring(list[i].width) + L" x " + std::to_wstring(list[i].height);
        u.hay = u.label;
        for (wchar_t& ch : u.hay) ch = ch == L'_' ? L' ' : wchar_t(::towlower(ch));  // ("bridges left" finds Jungle_Bridges_Left)
        u.hay += L" " + u.label;
        std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
        out.push_back(std::move(u));
    }
    return out;
}
