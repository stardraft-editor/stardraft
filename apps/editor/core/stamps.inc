// Custom terrain (portable, core.inc): tiles that the user saves from a map with a name, to place again on any map of
// the same tileset. Terrain mode's tool strip has Isometric, Rectangle and Custom; with Custom, the panel lists the
// custom terrain of the map's tileset (views_stamps.inc) in place of the Tiles grid, and the map's tools are
// Rectangle's (tiles_edit.inc): a pick in the list is the Brush's stamp (its tiles as they are; the tiles that are not
// in it stay as they are on the map), a click places it, a drag places it again along the way (one undo step), Esc or a
// right click stops. To make one: select tiles with the Select tool (Shift adds tiles, Ctrl takes them out: any shape),
// then right-click the selection > Save as custom terrain.
// Each is a file in the user's folder (shellStampsFolder: <folder>/<tileset>/<name>.scterrain; the format is
// stardraft/terrain_stamp.h), in folders of their own when the user puts them there (New folder, a drag onto a folder
// in the list; GitHub #252): <folder>/<tileset>/<folders>/<name>.scterrain. A name is the same custom terrain in any
// folder: each name is there once for a tileset.

// The custom terrain of the open map's tileset, read from the folder when the tileset or the folder changes, or after
// a change (stampsChanged).
struct StampsCache {
    std::vector<TerrainStamp> list;  // by name
    std::vector<std::string> folders;  // its folders (also the empty ones), by name: TerrainStamp::folder's
    int era = -1;
    fs::path folder;
    bool stale = true;
    bool mod = false;  // with a mod's brushes (a Cosmonarchy map)
};
StampsCache gStamps;
std::set<std::wstring> gStampsClosed;  // the Custom list's closed folders (by their path), for this run
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
// How the list shows its items (in the settings): 0 a list, 1 a grid of pictures; their size: 0 normal, 1 large, 2 extra
// large (as the other palettes' View menu has them).
int gStampsGrid = -1, gStampsSize = -1;
int stampsGrid() {
    if (gStampsGrid < 0) {
        gStampsGrid = 0;
        if (!gSelfTest && !gSettingsOff) prefs::number(L"CustomTerrainGrid", gStampsGrid, 0, 1);
    }
    return gStampsGrid;
}
int stampsSize() {
    if (gStampsSize < 0) {
        gStampsSize = 0;
        if (!gSelfTest && !gSettingsOff) prefs::number(L"CustomTerrainSize", gStampsSize, 0, 2);
    }
    return gStampsSize;
}
void setStampsView(int grid, int size) {
    gStampsGrid = std::clamp(grid, 0, 1), gStampsSize = std::clamp(size, 0, 2);
    if (!gSelfTest && !gSettingsOff) prefs::setNumber(L"CustomTerrainGrid", gStampsGrid), prefs::setNumber(L"CustomTerrainSize", gStampsSize);
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
        gStamps.folders = terrainStampFolders(folder, uint16_t(era));
        if (mod) {  // a Cosmonarchy map: the mod's brushes after the user's own, in a folder of their own (core/cmbw.inc)
            for (const TerrainStamp& b : stardraft::cmbw::brushStamps(stardraft::cmbw::findInstall(), uint16_t(era))) {
                gStamps.list.push_back(b);
                gStamps.list.back().folder = "Cosmonarchy";
            }
            if (std::find(gStamps.folders.begin(), gStamps.folders.end(), "Cosmonarchy") == gStamps.folders.end()) gStamps.folders.push_back("Cosmonarchy");
        }
        gStamps.era = era, gStamps.folder = folder, gStamps.stale = false, gStamps.mod = mod;
    }
    return gStamps.list;
}
// The folders of the custom terrain of the open map's tileset (stampsList reads them).
const std::vector<std::string>& stampsFolders() {
    stampsList();
    return gStamps.folders;
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
// Custom terrain of tileset `era` with the name `name` is there already, in any folder (not the file `keep`): replace it?
// Yes: its files go into `old`, which stampsRemoveReplaced deletes once the new file is written. False: no change.
bool stampsReplaceAsked(uint16_t era, const std::wstring& name, const wchar_t* title, std::vector<fs::path>& old, const fs::path& keep = {}) {
    std::error_code ec;
    for (const TerrainStamp& t : terrainStamps(shellStampsFolder(), era)) {
        if (!terrainStampSameName(t.name, narrow(name)) || (!keep.empty() && fs::equivalent(t.file, keep, ec))) continue;
        if (old.empty() && !shellAskYesNo(tr(L"Custom terrain with the name \"") + name + tr(L"\" is already there for this tileset. Replace it?"),
                                          tr(title), tr(L"&Replace"), tr(L"Cancel")))
            return false;
        old.push_back(t.file);
    }
    return true;
}
// The files that the new one replaced (stampsReplaceAsked), not the new file itself (`now`: in the same place it took
// their place).
void stampsRemoveReplaced(const std::vector<fs::path>& old, const fs::path& now) {
    std::error_code ec;
    for (const fs::path& f : old)
        if (!fs::equivalent(f, now, ec)) fs::remove(f, ec);
}

std::wstring gStampsLastName;  // the name typed last (the prompt starts with it)

// The terrain selection as custom terrain with no name: the selection's tiles in their box (the others are not in it).
// False: no map, or no selection (a message says so).
bool stampOfSelection(TerrainStamp& s) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return false;
    const std::vector<std::pair<int, int>> tiles = terrainSelTiles();
    if (tiles.empty()) return setMessage(L"Select tiles with the Select tool first"), false;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& [x, y] : tiles) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    s = TerrainStamp{};
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
    return true;
}

// Right-click the terrain selection > Save as custom terrain: its tiles under a name that the user types. A name that
// is there already for this tileset: Replace?
void stampsSaveSelection() {
    static const wchar_t* const kTitle = L"Save as custom terrain";
    TerrainStamp s;
    if (!stampOfSelection(s)) return;
    std::wstring name = gStampsLastName;
    if (!shellPromptText(tr(kTitle), name)) return;
    name = stampsTrim(name);
    if (!stampsNameOk(name, kTitle)) return;
    gStampsLastName = name;
    s.name = narrow(name);
    const fs::path folder = shellStampsFolder();
    std::vector<fs::path> replaced;
    if (!stampsReplaceAsked(s.era, name, kTitle, replaced)) return setMessage(L"The custom terrain is not saved");
    std::string err;
    if (!writeTerrainStamp(folder, s, err)) return shellErrorBox(widen(err), tr(kTitle));
    stampsRemoveReplaced(replaced, s.file);
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
    std::vector<fs::path> replaced;
    if (!sameFile && !stampsReplaceAsked(s.era, name, kTitle, replaced, old)) return;
    std::string err;
    if (!writeTerrainStamp(folder, s, err)) return shellErrorBox(widen(err), tr(kTitle));
    stampsRemoveReplaced(replaced, s.file);
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
    if (!fs::remove(s.file, ec) || ec) return shellErrorBox(tr(L"Could not delete the file ") + s.file.wstring(), tr(L"Delete custom terrain"));
    stampsRenamedInPanels(s.era, name, L"");  // (a panel's Brush that has it: nothing is picked there now)
    stampsChanged();
    setMessage(tr(L"Deleted the custom terrain ") + name);
}

// The list's menu: New folder... in folder `parent` (empty: the tileset's folder).
void stampsNewFolder(const std::string& parent) {
    static const wchar_t* const kTitle = L"New folder";
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    std::wstring name;
    if (!shellPromptText(tr(kTitle), name)) return;
    name = stampsTrim(name);
    if (!stampsNameOk(name, kTitle)) return;
    const std::string sub = parent.empty() ? narrow(name) : parent + "/" + narrow(name);
    std::error_code ec;
    const fs::path dir = terrainStampFolderPath(shellStampsFolder(), uint16_t(app.ed->era() & 7), sub);
    if (fs::exists(dir, ec)) return shellErrorBox(tr(L"A folder with this name is already there."), tr(kTitle));
    if (!fs::create_directories(dir, ec) || ec) return shellErrorBox(tr(L"Could not make the folder ") + dir.wstring(), tr(kTitle));
    gStampsClosed.erase(widen(parent));  // (its parent opens: the new folder shows)
    stampsChanged();
    setMessage(tr(L"Made the folder ") + name + tr(L". Drag custom terrain onto it to put it there."));
}

// The list's menu on a folder: Rename folder...
void stampsRenameFolder(const std::string& sub) {
    static const wchar_t* const kTitle = L"Rename folder";
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    const size_t cut = sub.rfind('/');
    const std::string parent = cut == std::string::npos ? std::string() : sub.substr(0, cut);
    const std::wstring was = widen(cut == std::string::npos ? sub : sub.substr(cut + 1));
    std::wstring name = was;
    if (!shellPromptText(tr(kTitle), name)) return;
    name = stampsTrim(name);
    if (name == was || !stampsNameOk(name, kTitle)) return;
    const uint16_t era = uint16_t(app.ed->era() & 7);
    const fs::path from = terrainStampFolderPath(shellStampsFolder(), era, sub),
                   to = terrainStampFolderPath(shellStampsFolder(), era, parent.empty() ? narrow(name) : parent + "/" + narrow(name));
    std::error_code ec;
    if (fs::exists(to, ec) && !fs::equivalent(from, to, ec)) return shellErrorBox(tr(L"A folder with this name is already there."), tr(kTitle));
    fs::rename(from, to, ec);
    if (ec) return shellErrorBox(tr(L"Could not rename the folder ") + from.wstring(), tr(kTitle));
    stampsChanged();
    setMessage(tr(L"Renamed the folder to ") + name);
}

// The list's menu on a folder: Delete folder (only an empty one: its custom terrain is not deleted with it).
void stampsDeleteFolder(const std::string& sub) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    const std::string inside = sub + "/";
    for (const TerrainStamp& t : stampsList())
        if (t.folder == sub || t.folder.rfind(inside, 0) == 0)
            return setMessage(L"The folder has custom terrain: drag it out of the folder first, or delete it");
    const fs::path dir = terrainStampFolderPath(shellStampsFolder(), uint16_t(app.ed->era() & 7), sub);
    std::error_code ec;
    std::vector<fs::path> dirs;  // (its folders, all empty: the deepest first)
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) dirs.push_back(it->path());
        else return setMessage(L"The folder has other files: it stays");
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    for (const fs::path& d : dirs) fs::remove(d, ec);
    if (!fs::remove(dir, ec) || ec) return shellErrorBox(tr(L"Could not delete the folder ") + dir.wstring(), tr(L"Delete folder"));
    stampsChanged();
    setMessage(tr(L"Deleted the folder ") + widen(sub));
}

// A drag in the list: custom terrain `s` goes into folder `sub` (empty: out of the folders). Its file moves.
void stampsMoveTo(const TerrainStamp& s, const std::string& sub) {
    static const wchar_t* const kTitle = L"Move custom terrain";
    if (s.file.empty() || s.folder == sub) return;  // (a mod's brush: not the user's file)
    if (modTab() && (sub == "Cosmonarchy" || sub.rfind("Cosmonarchy/", 0) == 0))
        return setMessage(L"The Cosmonarchy folder has the mod's brushes only: drop your custom terrain on another folder");
    const fs::path to = terrainStampPath(shellStampsFolder(), s.era, s.name, sub);
    std::error_code ec;
    if (fs::exists(to, ec) && !fs::equivalent(to, s.file, ec)) {
        if (!shellAskYesNo(tr(L"Custom terrain with the name \"") + widen(s.name) + tr(L"\" is already there for this tileset. Replace it?"), tr(kTitle),
                           tr(L"&Replace"), tr(L"Cancel")))
            return;
        fs::remove(to, ec);
    }
    fs::create_directories(to.parent_path(), ec);
    fs::rename(s.file, to, ec);
    if (ec) return shellErrorBox(tr(L"Could not move the file ") + s.file.wstring(), tr(kTitle));
    gStampsClosed.erase(widen(sub));  // (its folder opens: it shows there)
    stampsChanged();
    setMessage(sub.empty() ? tr(L"Moved ") + widen(s.name) + tr(L" out of the folders") : tr(L"Moved ") + widen(s.name) + tr(L" to the folder ") + widen(sub));
}

// The list's menu: Copy as text (the file's text, JSON, on the clipboard: to give to other people).
void stampsCopyText(const TerrainStamp& s, draw::Host* host) {
    if (!host) return;
    host->setClipboardText(terrainStampText(s));
    setMessage(tr(L"Copied the custom terrain ") + widen(s.name) + tr(L" as text"));
}

// Right-click the terrain selection > Copy as text: its tiles as custom terrain's text on the clipboard (no file), for
// Ctrl+V here or in another map of the tileset, or to give to other people.
void shellSetClipboardText(const std::string& utf8);  // (shell_sdl.inc)
void stampsCopySelectionText() {
    TerrainStamp s;
    if (!stampOfSelection(s)) return;
    s.name = "Copied terrain";
    shellSetClipboardText(terrainStampText(s));
    setMessage(L"Copied the selected terrain as text. Ctrl+V in Terrain mode makes it the Brush");
}

// The list's menu: Import from clipboard (custom terrain copied as text). It goes to the folder of its tileset, under
// its name (a name that is there already: Replace?).
void stampsImportText(const std::string& text) {
    static const wchar_t* const kTitle = L"Import custom terrain";
    TerrainStamp s;
    std::string err;
    if (!parseTerrainStamp(text, s, err))
        return shellErrorBox(tr(L"The clipboard has no custom terrain. Copy custom terrain as text first."), tr(kTitle));
    s.created = int64_t(std::time(nullptr));
    const std::wstring name = widen(s.name);
    const fs::path folder = shellStampsFolder();
    std::vector<fs::path> replaced;
    if (!stampsReplaceAsked(s.era, name, kTitle, replaced)) return setMessage(L"The custom terrain is not imported");
    if (!writeTerrainStamp(folder, s, err)) return shellErrorBox(widen(err), tr(kTitle));
    stampsRemoveReplaced(replaced, s.file);
    stampsChanged();
    AppState& app = appState();
    const bool here = app.ed && app.ed->isOpen() && (app.ed->era() & 7) == s.era;
    setMessage(tr(L"Imported the custom terrain ") + name + L" (" + std::to_wstring(s.count()) +
               tr(here ? L" tiles)" : L" tiles). It is for another tileset: the Custom list shows it on maps of that tileset."));
}

// Ctrl+V in Terrain mode with custom terrain as text on the clipboard (Copy as text, or text from other people): it
// is the Brush's stamp in Tile mode, as tiles copied with Ctrl+C are (not imported: Import from clipboard keeps it).
// When the map's own copy (Ctrl+C) is newer, it pastes: the clipboard's text at that Ctrl+C (gClipboardAtCopy) is the
// same as now. False: no such text, or the copy is newer.
std::string shellClipboardText();  // (shell_sdl.inc)
std::string gClipboardAtCopy;      // (copyMapSelection: the clipboard's text at the map's last Ctrl+C)
bool gMapCopied = false;
bool stampsPasteText() {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return false;
    const std::string text = shellClipboardText();
    TerrainStamp s;
    std::string err;
    if (text.empty() || text.find('{') == std::string::npos || !parseTerrainStamp(text, s, err)) return false;
    if (gMapCopied && text == gClipboardAtCopy) return false;  // (the map's Ctrl+C came after the text)
    if ((app.ed->era() & 7) != s.era) {
        setMessage(tr(L"The custom terrain on the clipboard is for another tileset: ") + widen(tilesetName(s.era)));
        return true;
    }
    if (terrainKind() != 1) setTerrainKind(1);  // (Tile: the tiles as they are)
    stampsUse(s);
    if (gTiles->stamp.empty()) {
        setMessage(L"The custom terrain on the clipboard has no tiles to place");
        return true;
    }
    gTiles->stampName.clear(), gTiles->stampCopied = true;  // (as a copy: in no list)
    stampsPanelsRepaint();
    return true;
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
