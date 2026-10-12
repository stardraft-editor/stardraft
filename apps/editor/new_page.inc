// The New page and the start page (included into main.cpp after the Project page). Each is a painted page in a tab
// of its own (Doc::page); the tab holds a small blank map until it becomes a document. docs/dev/CONTROLS.md, "New page and
// start page".
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

// The text that tells what each kind is (the New page's cards and the start page show the same text).
const wchar_t* kMapTypeText = L"Make standard StarCraft maps.";
const wchar_t* kProjectTypeText = L"Make maps with your own units, weapons and game rules. Change the game's data, and make new triggers with "
                                  L"blocks or with your own code.";
const wchar_t* kModTypeText = L"Custom MPQs and data files, for 1.16.1 and Remastered.";
const wchar_t* kKindNames[] = {L"Classic project", L"EUD project", L"Campaign project", L"Mod project"};

// The New page's cards: two sentences on what each kind lets you make.
const wchar_t* const kKindCards[] = {
    L"Make standard StarCraft maps.",
    L"Make maps with your own units, weapons and game rules. Change the game's data, and make new triggers with blocks "
    L"or with your own code.",
    L"A project with all the maps of your campaign. Go from one map to the next, and copy data and terrain between "
    L"the maps.",
    L"Make mods with your own graphics and game files. Use your own MPQs and data files, for StarCraft 1.16.1 and "
    L"Remastered.",
};

struct NewSlider {
    std::wstring label;
    int lo, hi, value;
    uint16_t terrain = 0;  // a terrain's share (value in half percent); 0: a shape slider
};

struct NewPage {
    HWND wnd = nullptr, name = nullptr, location = nullptr, output = nullptr, seed = nullptr, fromMap = nullptr;
    ui::Surface surface;
    int scroll = 0, height = 0;
    int blockH = 0;  // the settings: the height of the block at the last paint (to put it in the middle)
    // The choices.
    bool project = false, generate = false;
    bool copyMap = false;  // an EUD project: Start "Import an existing map" (a copy of the map in fromMap); generate is off then
    bool terrainOnly = false;  // Start "The terrain of a map": a new map with only the terrain of the map in fromMap
    bool keep = false;  // Back went to the start page: a card opens the settings with the same choices
    int card = 0;       // the start page's card that the keyboard is on (0 or 1)
    uint16_t era = 0, terrain = 0, w = 128, h = 128;  // terrain 0: the tileset's base ground
    int symmetry = 0, feature = 0;
    std::vector<NewSlider> shares;
    std::vector<NewSlider> shape = {{L"Land scale", 5, 150, 30}, {L"Coastline", 20, 200, 75}, {L"Detail", 60, 400, 180}, {L"Roughness", 0, 100, 35}};
    // The generator's tileset and its terrains; the preview: the map's tiles (blank or generated), and its picture at
    // the size it has on the page (made again when the tiles or the size change).
    uint16_t loadedEra = 0xFFFF;
    Tileset tileset;
    std::vector<GenTerrain> roles;
    std::vector<uint32_t> megaColor, tileColor, pixels;
    std::vector<uint16_t> tiles;
    int tilesW = 0, tilesH = 0, tilesVersion = 0, pixelsVersion = -1;
    int previewW = 0, previewH = 0;  // the picture's size in pixels
    bool previewMade = false;        // the tiles are made for the current choices
    int aboveH = 0;                  // at the last paint: from the top of the block to the top of the preview
    // A picture of each tileset (a small generated map), made the first time the page shows.
    std::vector<uint32_t> swatch[8];
    int swatchSide = 0;
    bool swatchesMade = false;
    // What the last paint showed (the self-test reads these).
    bool genShown = false, projectShown = false, createEnabled = true, cardsShown = false;
    int cardsEnabled = 0;  // the cards that can be selected
    int startProjects = 0, startMaps = 0;
    RECT terrainBox{}, footer{};  // the Blank row's terrain box; the Create and Cancel row (in the page's client area)
    RECT previewRect{}, genRect{};  // the preview; the generator's settings (empty when they do not show)
    int sizeX0 = 0, sizeX1 = 0, sizeY[2] = {0, 0};  // the Width and Height sliders: the track's ends and the middle of each
    bool footerStuck = false;  // the Create and Cancel row is held at the bottom of the page (the content scrolls under it)
    std::wstring warning;
    // The sliders under the mouse wheel: each slider's row and one step up (1) or down (-1). Made again at each paint.
    std::vector<std::pair<RECT, std::function<void(int)>>> wheels;
    int tallest = 0;        // the highest bottom the page had since it opened: Create stays there (the page does not jump)
} gNew;

// The tab's name: the kind.
const wchar_t* gNewTitle() { return gNew.project ? L"New EUD project" : L"New classic project"; }
const wchar_t* kSymmetryNames[] = {L"None", L"Mirror left and right", L"Mirror top and bottom", L"Mirror both (4 players)", L"Turn 180 degrees (2 players)"};
const wchar_t* kFeatureNames[] = {L"Brush 1 (whole diamonds)", L"Brush 0.5 (single corners)"};

// The average color of each minitile of each megatile (4 x 4 per megatile), for the previews.
std::vector<uint32_t> newMegaColors(const Tileset& ts) {
    std::vector<uint32_t> out(ts.megatiles.size() * 16, 0);
    for (size_t m = 0; m < ts.megatiles.size(); ++m)
        for (int k = 0; k < 16; ++k) {
            const size_t mini = ts.megatiles[m][k] >> 1;
            if (mini >= ts.minitiles.size()) continue;
            uint32_t sum[3] = {0, 0, 0};
            for (uint8_t idx : ts.minitiles[mini]) {
                const Rgb& c = ts.palette[idx];
                sum[0] += c.r, sum[1] += c.g, sum[2] += c.b;
            }
            out[m * 16 + k] = (sum[0] / 64) << 16 | (sum[1] / 64) << 8 | (sum[2] / 64);
        }
    return out;
}

// The average color of each megatile (from the averages of its minitiles).
std::vector<uint32_t> newTileColors(const std::vector<uint32_t>& mega) {
    std::vector<uint32_t> out(mega.size() / 16, 0);
    for (size_t m = 0; m < out.size(); ++m) {
        uint32_t sum[3] = {0, 0, 0};
        for (int k = 0; k < 16; ++k) {
            const uint32_t c = mega[m * 16 + size_t(k)];
            sum[0] += c >> 16 & 255, sum[1] += c >> 8 & 255, sum[2] += c & 255;
        }
        out[m] = (sum[0] / 16) << 16 | (sum[1] / 16) << 8 | (sum[2] / 16);
    }
    return out;
}

// The map's tiles: a blank map filled with `terrain`, or (`generate`) terrain generated with `p`.
bool newMakeTiles(const Tileset& ts, const TerrainGenParams& p, bool generate, uint16_t terrain, std::vector<uint16_t>& tiles, int& tw, int& th) {
    Chk chk;
    IsomGrid grid;
    std::string err;
    if (!grid.create(chk, p.width, p.height, p.era, generate ? uint16_t(1) : terrain, err)) return false;
    if (generate && !generateTerrain(chk, ts, p, err)) return false;
    TerrainLayer t = readTerrain(chk);
    tiles = std::move(t.tiles), tw = t.width, th = t.height;
    return true;
}

// A picture of the tiles, `pw` x `ph` pixels (made at that size, not stretched): the tiles' own pixels when a tile has
// 8 pixels or more, the average colors of its minitiles from 3, else the average color of each tile.
void newPaintTiles(const Tileset& ts, const std::vector<uint32_t>& mega, const std::vector<uint32_t>& tileAvg, const std::vector<uint16_t>& tiles, int tw,
                   int th, int pw, int ph, std::vector<uint32_t>& px) {
    px.assign(size_t(std::max(0, pw)) * std::max(0, ph), 0);
    if (tw <= 0 || th <= 0 || pw <= 0 || ph <= 0 || tiles.size() < size_t(tw) * th) return;
    const int perTile = pw / tw;
    std::vector<int> megaOf(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) megaOf[i] = ts.megatileFor(tiles[i]);
    for (int oy = 0; oy < ph; ++oy) {
        const int my = int((int64_t(oy) * 2 + 1) * th * 32 / (int64_t(ph) * 2));  // the map pixel at the middle of this one
        for (int ox = 0; ox < pw; ++ox) {
            const int mx = int((int64_t(ox) * 2 + 1) * tw * 32 / (int64_t(pw) * 2));
            const int m = megaOf[size_t(my / 32) * tw + mx / 32];
            if (m < 0 || size_t(m) >= ts.megatiles.size()) continue;
            const int k = (my % 32 / 8) * 4 + mx % 32 / 8;  // the minitile
            uint32_t c = 0;
            if (perTile >= 8) {
                const uint32_t e = ts.megatiles[size_t(m)][size_t(k)];
                const size_t mini = e >> 1;
                if (mini >= ts.minitiles.size()) continue;
                const int x = e & 1 ? 7 - mx % 8 : mx % 8;
                const Rgb& rgb = ts.palette[ts.minitiles[mini][size_t(my % 8 * 8 + x)]];
                c = uint32_t(rgb.r) << 16 | uint32_t(rgb.g) << 8 | rgb.b;
            } else if (perTile >= 3) {
                if (size_t(m) * 16 + size_t(k) < mega.size()) c = mega[size_t(m) * 16 + size_t(k)];
            } else if (size_t(m) < tileAvg.size()) {
                c = tileAvg[size_t(m)];
            }
            px[size_t(oy) * pw + ox] = c;
        }
    }
}

// Loads the tileset for the generator: its terrains and one share slider for each (not the base ground).
void newLoadTileset() {
    if (gNew.loadedEra == gNew.era) return;
    std::string err;
    if (!gNew.tileset.load(app.data, gNew.era, err)) return;
    gNew.loadedEra = gNew.era;
    gNew.roles = generatorTerrains(gNew.era, gNew.tileset);
    gNew.megaColor = newMegaColors(gNew.tileset);
    gNew.tileColor = newTileColors(gNew.megaColor);
    gNew.shares.clear();
    for (const auto& r : gNew.roles) {
        if (r.role == GenTerrain::Base) continue;
        std::wstring name = std::wstring(size_t(std::max(0, r.depth - 1)) * 3, L' ') + widen(r.name);
        if (r.role == GenTerrain::Sea) name += L" (water)";
        if (r.role == GenTerrain::High) name += L" (high ground)";
        const int def = int(std::lround(r.defaultShare * 200));
        gNew.shares.push_back({name, 0, std::clamp(def * 3, 40, 160), def, r.id});
    }
    gNew.previewMade = false;
}

uint32_t newSeed() {
    if (!gNew.seed) return 12345;
    wchar_t buf[32] = L"";
    GetWindowTextW(gNew.seed, buf, 32);
    return uint32_t(wcstoul(buf, nullptr, 10));
}

TerrainGenParams newParams() {
    newLoadTileset();
    TerrainGenParams p;
    p.width = gNew.w, p.height = gNew.h, p.era = gNew.era, p.seed = newSeed();
    for (const auto& sl : gNew.shares) {
        if (p.shares.size() <= sl.terrain) p.shares.resize(sl.terrain + 1, -1.0f);
        p.shares[sl.terrain] = sl.value / 200.0f;
    }
    p.landScale = gNew.shape[0].value, p.coastline = gNew.shape[1].value, p.detail = gNew.shape[2].value;
    p.roughness = gNew.shape[3].value / 100.0f;
    p.minFeature = gNew.feature == 1 ? 0.5f : 1.0f;
    p.symmetry = GenSymmetry(gNew.symmetry);
    return p;
}

// Import an existing map (an EUD project): the map has the terrain, so there is no preview.
bool newImporting() { return (gNew.project && gNew.copyMap) || gNew.terrainOnly; }  // (the map's file gives the tileset and size)
uint16_t newTerrainId();

// The preview again, a moment after the last change (a slider drag changes it many times).
void newChanged(bool regenerate = true) {
    if (regenerate) gNew.previewMade = false;
    if (!gNew.wnd) return;
    if (regenerate && !newImporting()) SetTimer(gNew.wnd, 1, 60, nullptr);
    InvalidateRect(gNew.wnd, nullptr, FALSE);
}

// The preview's map: blank (filled with the selected terrain) or generated.
void newRegenerate() {
    if (newImporting()) return;
    const TerrainGenParams p = newParams();
    gNew.previewMade = newMakeTiles(gNew.tileset, p, gNew.generate, newTerrainId(), gNew.tiles, gNew.tilesW, gNew.tilesH);
    ++gNew.tilesVersion;
    if (gNew.wnd) InvalidateRect(gNew.wnd, nullptr, FALSE);
}

// Each tileset's picture: a small generated map of it.
void newMakeSwatches() {
    if (gNew.swatchesMade) return;
    gNew.swatchesMade = true;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    for (uint16_t era = 0; era < 8; ++era) {
        Tileset ts;
        std::string err;
        if (!ts.load(app.data, era, err)) continue;
        TerrainGenParams p;
        p.width = p.height = 64, p.era = era, p.seed = 7 + era;
        std::vector<uint16_t> tiles;
        int tw = 0, th = 0;
        if (!newMakeTiles(ts, p, true, 1, tiles, tw, th)) continue;
        const std::vector<uint32_t> mega = newMegaColors(ts);
        newPaintTiles(ts, mega, newTileColors(mega), tiles, tw, th, tw * 4, th * 4, gNew.swatch[era]);
        gNew.swatchSide = tw * 4;
    }
    SetCursor(old);
}

// The base ground, or the terrain picked for a blank map.
uint16_t newTerrainId() {
    newLoadTileset();
    if (gNew.terrain) return gNew.terrain;
    return gNew.roles.empty() ? uint16_t(1) : gNew.roles[0].id;
}

std::wstring newText(HWND w) { return w ? windowText(w) : std::wstring(); }

// The project's folder (<location>\<name>) and what is wrong with the choices ("": nothing).
fs::path newProjectFolder() { return fs::path(newText(gNew.location)) / fs::path(newText(gNew.name)); }
// A project name whose folder in `location` is not there yet (or is empty): `base`, else "base 2", "base 3", ...
std::wstring newFreeName(const std::wstring& base, const fs::path& location) {
    std::error_code ec;
    for (int k = 1; k < 1000; ++k) {
        const std::wstring name = k == 1 ? base : base + L" " + std::to_wstring(k);
        const fs::path dir = location / fs::path(name);
        if (!fs::exists(dir, ec) || fs::is_empty(dir, ec)) return name;
    }
    return base;
}
std::wstring newProblem() {
    if (!gNew.project) {  // a classic project: only the map whose terrain it takes
        std::error_code ec;
        const std::wstring map = newText(gNew.fromMap);
        if (gNew.terrainOnly && map.empty()) return L"Select the map whose terrain to use";
        if (gNew.terrainOnly && !fs::is_regular_file(fs::path(map), ec)) return L"The map " + map + L" is not found";
        return L"";
    }
    const std::wstring name = newText(gNew.name);
    if (name.empty()) return L"Type a name for the project";
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return L"The name cannot hold \\ / : * ? \" < > |";
    if (newText(gNew.location).empty()) return L"Type the folder for the project";
    std::error_code ec;
    const fs::path dir = newProjectFolder();
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec))
        return L"There is already a folder " + fs::path(dir).make_preferred().wstring() + L". Type another name, for example " +
               newFreeName(name, fs::path(newText(gNew.location))) + L".";
    if (gNew.copyMap || gNew.terrainOnly) {
        const std::wstring map = newText(gNew.fromMap);
        if (map.empty()) return gNew.terrainOnly ? L"Select the map whose terrain to use" : L"Select the map to copy into the project";
        if (!fs::is_regular_file(fs::path(map), ec)) return L"The map " + map + L" is not found";
    }
    return L"";
}

void newReset(int kind);
void pageSync();
// The active tab shows `page` (kPageNew or kPageStart).
void newShowScreen(int page) {
    gNew.scroll = 0;
    if (activePage() && activePage() != page) {
        gDocs[size_t(gActive)].page = page;
        pageSync();
    }
    if (activePage()) updateTitle();
    if (app.tabs) InvalidateRect(app.tabs, nullptr, FALSE);
    newChanged(false);
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
    if (!newImporting() && !gNew.previewMade && gNew.wnd) SetTimer(gNew.wnd, 1, 10, nullptr);
    return true;
}
// Back to the start page (a card opens the settings again with the same choices).
void newBack() {
    if (activePage() != kPageNew) return;
    gNew.card = gNew.project ? 1 : 0, gNew.keep = true;
    newShowScreen(kPageStart);
}

// Fresh choices: the open map's tileset, 128 x 128, blank. kind: 0 a classic project, 1 an EUD project.
void newReset(int kind) {
    gNew.project = kind == 1, gNew.card = kind == 1 ? 1 : 0, gNew.keep = false;
    gNew.generate = false, gNew.copyMap = false, gNew.terrainOnly = false;
    gNew.era = app.ed && app.ed->isOpen() && !activePage() ? uint16_t(app.ed->era() & 7) : uint16_t(0);
    gNew.terrain = 0, gNew.w = gNew.h = 128, gNew.symmetry = 0, gNew.feature = 0, gNew.scroll = 0;
    gNew.loadedEra = 0xFFFF;
    gNew.shape = {{L"Land scale", 5, 150, 30}, {L"Coastline", 20, 200, 75}, {L"Detail", 60, 400, 180}, {L"Roughness", 0, 100, 35}};
    // The default name has no folder yet: MyMap, else MyMap 2, MyMap 3, ...
    if (gNew.name) SetWindowTextW(gNew.name, newFreeName(L"MyMap", defaultProjectsFolder()).c_str());
    if (gNew.location) SetWindowTextW(gNew.location, defaultProjectsFolder().make_preferred().wstring().c_str());
    if (gNew.output) SetWindowTextW(gNew.output, L"");
    if (gNew.seed) SetWindowTextW(gNew.seed, L"12345");
    gNew.previewMade = false;
}

// Randomize: shares around the tileset's averages (half to one and a half times), shape within useful ranges, a new
// seed. `all`: the tileset and the size too.
void newRandomize(bool all) {
    uint32_t r = uint32_t(GetTickCount64()) ^ 0x9E3779B9u;
    auto roll = [&r](int lo, int hi) {
        r = r * 1664525u + 1013904223u;
        return lo + int((r >> 8) % uint32_t(hi - lo + 1));
    };
    if (all) {
        gNew.era = uint16_t(roll(0, 7)), gNew.terrain = 0;  // the tileset; the size stays (the user chose it)
    }
    newLoadTileset();
    for (const auto& r0 : gNew.roles)
        for (auto& sl : gNew.shares)
            if (sl.terrain == r0.id) sl.value = std::clamp(int(std::lround(r0.defaultShare * 200)) * roll(50, 150) / 100, sl.lo, sl.hi);
    const int ranges[4][2] = {{15, 60}, {50, 140}, {120, 300}, {10, 70}};
    for (size_t i = 0; i < gNew.shape.size(); ++i) gNew.shape[i].value = roll(ranges[i][0], ranges[i][1]);
    if (gNew.seed) SetWindowTextW(gNew.seed, std::to_wstring(roll(1, 999999)).c_str());
    newChanged();
}

// Create: the map (blank or generated) in this tab; for a project, the project folder around it.
bool newCreate() {
    if (activePage() != kPageNew) return false;  // the start page: no kind yet
    if (const std::wstring problem = newProblem(); !problem.empty()) {
        setMessage(problem);
        return false;
    }
    auto ed = std::make_unique<Editor>(app.data);
    std::string err;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const TerrainGenParams p = newParams();
    const bool copy = gNew.project && gNew.copyMap;  // Import an existing map: the project's map is a copy of it (the file stays as it is)
    const bool ok = copy               ? ed->open(fs::path(newText(gNew.fromMap)), err)
                    : gNew.terrainOnly ? ed->newMapFromTerrain(fs::path(newText(gNew.fromMap)), err)
                    : gNew.generate    ? ed->generateMap(p, err)
                                       : ed->newMap(gNew.w, gNew.h, gNew.era, newTerrainId(), err);
    if (!ok) {
        themedMessageBox(app.main, widen(err).c_str(), copy ? L"Could not open the map" : gNew.generate ? L"Could not generate the map" : L"Could not create the map",
                         MB_ICONERROR);
        return false;
    }
    const bool project = gNew.project;
    const std::wstring name = newText(gNew.name), location = newText(gNew.location), output = newText(gNew.output);
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

// --- Painting -------------------------------------------------------------------------------------------------------

// A native edit box on the page, placed where the paint puts it (hidden when it does not show).
void newPlaceEdit(HWND e, RECT r, bool show, int clipTop, int clipBottom) {
    if (!e) return;
    show = show && r.top >= clipTop && r.bottom <= clipBottom;
    if (show) {
        RECT now;
        GetWindowRect(e, &now);
        MapWindowPoints(nullptr, gNew.wnd, reinterpret_cast<POINT*>(&now), 2);
        if (!EqualRect(&now, &r)) MoveWindow(e, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    }
    if (bool(IsWindowVisible(e)) != show) ShowWindow(e, show ? SW_SHOWNA : SW_HIDE);
}

// A picture (4 x 4 pixels per tile) fitted into `r`, keeping its proportions; `crop`: fill `r` (the middle part).
void newDrawPicture(HDC dc, RECT r, const std::vector<uint32_t>& px, int pw, int ph, bool crop) {
    if (px.empty() || pw <= 0 || ph <= 0) return;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), pw, -ph, 1, 32, BI_RGB};
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    const int rw = r.right - r.left, rh = r.bottom - r.top;
    if (crop) {
        const double k = std::max(double(rw) / pw, double(rh) / ph);
        const int sw = int(rw / k), sh = int(rh / k);
        StretchDIBits(dc, r.left, r.top, rw, rh, (pw - sw) / 2, (ph - sh) / 2, sw, sh, px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        return;
    }
    const double k = std::min(double(rw) / pw, double(rh) / ph);
    const int w = int(pw * k), h = int(ph * k);
    StretchDIBits(dc, r.left + (rw - w) / 2, r.top + (rh - h) / 2, w, h, 0, 0, pw, ph, px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

// Text in `r`, word-wrapped; returns the bottom.
int newParagraph(HDC dc, RECT r, const std::wstring& text, COLORREF c) {
    SetTextColor(dc, c);
    RECT calc = r;
    DrawTextW(dc, text.c_str(), -1, &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    r.bottom = r.top + (calc.bottom - calc.top);
    DrawTextW(dc, text.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    return r.bottom;
}

// A warning line: the icon and the text in kWarn.
int newWarning(HDC dc, int x, int y, int right, const std::wstring& text) {
    uiIcon(dc, {x, y + scale(2), x + scale(16), y + scale(18)}, UiIcon::Warning, kWarn, scale(16));
    return newParagraph(dc, {x + scale(22), y, right, y + scale(200)}, text, kWarn);
}

// A map size as a slider that stops at each size StarDraft makes (kMapSizes): the name and the size on the left (as
// the Amounts sliders), the slider on the right, with a mark under each stop. `which`: 0 the width, 1 the height.
void newSizeRow(HDC dc, int x, int y, int labelW, int right, const wchar_t* name, uint16_t& value, int which) {
    const int n = int(std::size(kMapSizes)), rowH = scale(ui::kFormRowH), cy = y + rowH / 2;
    int at = 0;
    for (int i = 0; i < n; ++i)
        if (kMapSizes[i] <= value) at = i;
    RECT t{x, y, x + labelW - scale(48), y + rowH};
    SetTextColor(dc, kDim);
    DrawTextW(dc, name, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT vt{x + labelW - scale(56), y, x + labelW - scale(4), y + rowH};
    SetTextColor(dc, kText);
    DrawTextW(dc, std::to_wstring(value).c_str(), -1, &vt, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    const int x0 = x + labelW + scale(8), x1 = right - scale(8);
    for (int i = 0; i < n; ++i) {  // the stops
        const int tx = x0 + (x1 - x0) * i / (n - 1);
        fillAlpha(dc, {tx - std::max(1, scale(1)) / 2, cy + scale(9), tx - std::max(1, scale(1)) / 2 + std::max(1, scale(1)), cy + scale(13)}, kBorderHot, 255);
    }
    const RECT area{x0 - scale(8), y, right, y + rowH};
    ui::sliderFace(dc, x0, x1, cy, float(at) / float(n - 1), ui::hot(area));
    gNew.sizeX0 = x0, gNew.sizeX1 = x1, gNew.sizeY[which] = cy;
    uint16_t* p = &value;
    hitsOut().push_back({{x, y, area.left, y + rowH}, nullptr, nullptr, L"", IDC_ARROW, {}});  // its name and value: feedback on it
    hitsOut().back().name = name;
    hitsOut().push_back({area, [p, x0, x1, n](POINT pt) {
                             const float f = std::clamp(float(pt.x - x0) / float(std::max(1, x1 - x0)), 0.0f, 1.0f);
                             const uint16_t nv = kMapSizes[std::clamp(int(std::lround(f * float(n - 1))), 0, n - 1)];
                             if (nv != *p) *p = nv, newChanged();
                         },
                         nullptr, L"", IDC_HAND, {}});
    gNew.wheels.push_back({area, [p, n](int dir) {
                               int i = 0;
                               for (int k = 0; k < n; ++k)
                                   if (kMapSizes[k] <= *p) i = k;
                               const uint16_t nv = kMapSizes[std::clamp(i + dir, 0, n - 1)];
                               if (nv != *p) *p = nv, newChanged();
                           }});
}

// A slider row: the name and value on the left, the slider on the right.
void newSliderRow(HDC dc, int x, int y, int labelW, int right, NewSlider& sl, bool percent) {
    const int rowH = scale(ui::kRowH), cy = y + rowH / 2;
    wchar_t v[32];
    if (percent) std::swprintf(v, 32, L"%.1f%%", sl.value / 2.0);
    else std::swprintf(v, 32, L"%d", sl.value);
    RECT t{x, y, x + labelW - scale(48), y + rowH};
    SetTextColor(dc, kDim);
    DrawTextW(dc, sl.label.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT vt{x + labelW - scale(56), y, x + labelW - scale(4), y + rowH};
    DrawTextW(dc, v, -1, &vt, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    const int x0 = x + labelW + scale(8), x1 = right - scale(8);
    const RECT area{x0 - scale(8), y, right, y + rowH};
    ui::sliderFace(dc, x0, x1, cy, float(sl.value - sl.lo) / float(std::max(1, sl.hi - sl.lo)), ui::hot(area));
    NewSlider* p = &sl;
    hitsOut().push_back({{x, y, area.left, y + rowH}, nullptr, nullptr, L"", IDC_ARROW, {}});  // its name and value: feedback on it
    hitsOut().back().name = sl.label;
    hitsOut().push_back({area, [p, x0, x1](POINT pt) {
                             const float f = std::clamp(float(pt.x - x0) / float(std::max(1, x1 - x0)), 0.0f, 1.0f);
                             const int nv = p->lo + int(std::lround(f * float(p->hi - p->lo)));
                             if (nv != p->value) p->value = nv, newChanged();
                         },
                         nullptr, L"", IDC_HAND, {}});
    gNew.wheels.push_back({area, [p](int dir) {  // a step of the wheel: 1/50 of the range, at least 1
                               const int nv = std::clamp(p->value + dir * std::max(1, (p->hi - p->lo) / 50), p->lo, p->hi);
                               if (nv != p->value) p->value = nv, newChanged();
                           }});
}

// The terrain list for the Blank row: the tileset's terrains by name.
void newPickTerrain(RECT b) {
    gNew.copyMap = false, gNew.generate = false, gNew.terrainOnly = false;  // the dropdown selects Blank too
    newChanged();
    const IsomTerrainTable* t = IsomTerrainTable::forEra(gNew.era);
    std::vector<ListPickItem> items;
    for (uint16_t k = 1; t && k <= t->terrainCount(); ++k)
        if (t->isTerrain(k)) items.push_back({widen(t->terrainName(k)), L"", std::to_string(k)});
    std::string picked;
    if (!items.empty() && showListPicker(L"Terrain", items, std::to_string(newTerrainId()), belowBox(b), picked)) gNew.terrain = uint16_t(std::stoi(picked)), newChanged();
}

// The preview: the map's picture, made at the size that it has in `pv` (kept in its proportions, in the middle).
void newDrawPreview(HDC mem, RECT pv) {
    fillAlpha(mem, pv, RGB(0, 0, 0), 255);  // a picture of the game: dark in both themes
    // The last picture stays until the new one is made (a change makes it again a moment later; no black frames).
    if (gNew.tilesW > 0 && gNew.tilesH > 0) {
        const int side = std::min(pv.right - pv.left, pv.bottom - pv.top), big = std::max(gNew.tilesW, gNew.tilesH);
        const int pw = std::max(1, side * gNew.tilesW / big), ph = std::max(1, side * gNew.tilesH / big);
        if (gNew.pixelsVersion != gNew.tilesVersion || gNew.previewW != pw || gNew.previewH != ph) {
            newPaintTiles(gNew.tileset, gNew.megaColor, gNew.tileColor, gNew.tiles, gNew.tilesW, gNew.tilesH, pw, ph, gNew.pixels);
            gNew.previewW = pw, gNew.previewH = ph, gNew.pixelsVersion = gNew.tilesVersion;
        }
        newDrawPicture(mem, pv, gNew.pixels, pw, ph, false);
    }
    ui::box(mem, pv, CLR_NONE, kBorder, 2);
}

void paintNewPage(HDC mem, RECT rc) {
    gNew.cardsShown = false;
    newMakeSwatches();
    newLoadTileset();
    const int pad = scale(ui::kDialogPad), gap = scale(32), rowH = scale(ui::kRowH), fieldH = scale(ui::kFieldH);
    const int bh = scale(ui::kButtonH);
    const int avail = int(rc.right) - 2 * pad;
    // The block (Back, the title, the columns, then Create and Cancel) is in the middle of the page, across and up and
    // down; a block taller than the page starts at the top and scrolls. The height is from the last paint.
    const int startY = std::max(pad, (int(rc.bottom) - gNew.blockH) / 2);
    // The edit boxes stay above the Create and Cancel bar when it is held at the bottom (a tall block).
    const int footerTop = startY + gNew.blockH + pad > int(rc.bottom) ? int(rc.bottom) - (bh + 2 * pad) : int(rc.bottom);
    // The columns: Project (only for an EUD project), Terrain, Start. Start has the preview (up to 600 x 600 at 100%,
    // as tall as the page lets it be) and to its right the generator's settings; in a narrow window the settings go
    // under the preview. The width does not change with the Start choice.
    const bool proj = gNew.project;
    if (!proj) gNew.copyMap = false;  // Import an existing map is for an EUD project (a classic map: File > Open)
    const bool importing = newImporting();
    const int gap1 = proj ? gap : 0, pvGap = scale(24), sideMin = scale(300), gwMin = scale(320), gwMax = scale(420);
    int w1 = proj ? scale(300) : 0, w2 = scale(320);
    const int room = avail - w1 - gap1 - w2 - gap;
    int side = std::max(sideMin, std::min(scale(600), int(rc.bottom) - 2 * pad - gNew.aboveH - scale(24) - bh));
    const bool besides = room >= sideMin + pvGap + gwMin;  // the generator's settings to the right of the preview
    int w3 = 0, gw = 0;
    if (besides) {
        gw = std::clamp(room - side - pvGap, gwMin, gwMax);
        side = std::min(side, room - pvGap - gw);
        w3 = side + pvGap + gw;
    } else {
        w3 = std::clamp(room, scale(300), scale(520));
        if (w1 + w2 + w3 + gap1 + gap > avail) {  // a narrow window: the columns share what there is
            const int k = std::max(1, avail - gap1 - gap);
            if (proj) w1 = k * 30 / 104, w2 = k * 32 / 104, w3 = k - w1 - w2;
            else w2 = k * 32 / 74, w3 = k - w2;
        }
        side = std::min(side, w3), gw = w3;
    }
    const int x1 = std::max(pad, (int(rc.right) - (w1 + gap1 + w2 + gap + w3)) / 2), x2 = x1 + w1 + gap1, x3 = x2 + w2 + gap;
    const int blockTop = startY - gNew.scroll;
    int y = blockTop;
    // Back to the cards.
    ui::smallButton(mem, x1, y, scale(24), L"Back", [] { newBack(); }, L"Select a different kind (Esc)");
    y += scale(28);
    {
        const IsomTerrainTable* table = IsomTerrainTable::forEra(gNew.era);
        std::wstring sub = std::wstring(kTilesetNames[gNew.era & 7]) + L"  \x00B7  " + std::to_wstring(gNew.w) + L" x " + std::to_wstring(gNew.h) + L"  \x00B7  " +
                           (gNew.generate ? std::wstring(L"generated") : L"filled with " + widen(table ? table->terrainName(newTerrainId()) : ""));
        if (importing) {  // Import an existing map: the map's file names it (its tileset and size come with it)
            const std::wstring map = newText(gNew.fromMap);
            sub = map.empty() ? L"an imported map" : L"imported from " + fs::path(map).filename().wstring();
        }
        y = ui::title(mem, x1, y, x3 + w3, nullptr, kKindNames[proj ? 1 : 0], sub, false, nullptr, proj ? UiIcon::NewProject : UiIcon::New);
    }
    const int top = y + scale(8);
    auto layoutFor = [&](int x, int w) {
        FormLayout l{};
        l.x0 = x, l.labelW = w, l.rowH = rowH, l.colW = w, l.colRight = x + w;
        return l;
    };
    auto label = [&](int x, int yy, int w, const wchar_t* t, bool enabled = true) {
        RECT r{x, yy, x + w, yy + scale(20)};
        SetTextColor(mem, enabled ? kDim : kFaint);
        DrawTextW(mem, t, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        return yy + scale(22);
    };

    // Project (only for an EUD project): the name, the folder and the output file.
    y = proj ? ui::sectionHeader(mem, layoutFor(x1, w1), top, L"Project") : top;
    gNew.projectShown = gNew.project;
    gNew.warning = newProblem();
    {
        const int bw = scale(ui::kButtonMinW);
        int fy = y;
        auto fieldRow = [&](HWND e, const wchar_t* name, bool browse, std::function<void()> click) {
            if (!gNew.project) return newPlaceEdit(e, {}, false, 0, 0);
            fy = label(x1, fy, w1, name);
            const int fr = browse ? x1 + w1 - bw - scale(8) : x1 + w1;
            newPlaceEdit(e, {x1, fy + scale(2), fr, fy + scale(2) + fieldH}, true, 0, footerTop);
            if (browse) ui::button(mem, {x1 + w1 - bw, fy, x1 + w1, fy + scale(ui::kButtonH)}, L"Browse\x2026", ui::Tone::Normal, std::move(click));
            fy += scale(ui::kFormRowH);
        };
        fieldRow(gNew.name, L"Name", false, nullptr);
        fieldRow(gNew.location, L"Location", true, [] {
            const std::wstring dir = pickFolderTitled(L"Select the folder for the project");
            if (!dir.empty()) SetWindowTextW(gNew.location, dir.c_str());
        });
        if (gNew.project) {  // where the folder goes, or what is wrong
            if (!gNew.warning.empty()) fy = newWarning(mem, x1, fy, x1 + w1, gNew.warning) + scale(8);
            else {
                HGDIOBJ of = SelectObject(mem, ui::smallFont());
                fy = newParagraph(mem, {x1, fy, x1 + w1, fy + scale(100)}, L"StarDraft makes the folder " + fs::path(newProjectFolder()).make_preferred().wstring(), kDim) + scale(8);
                SelectObject(mem, of);
            }
        }
        fieldRow(gNew.output, L"Output file", true, [] {
            const std::wstring file = pickOutputFile(app.main, newText(gNew.name) + L"_EUD.scx");
            if (!file.empty()) SetWindowTextW(gNew.output, file.c_str());
        });
        if (gNew.output) SendMessageW(gNew.output, EM_SETCUEBANNER, TRUE, LPARAM((newText(gNew.name) + L"_EUD.scx").c_str()));
        y = std::max(y, fy);
    }
    int bottom = y;

    // Terrain: the tileset and the size (with Import an existing map, the map has them).
    y = ui::sectionHeader(mem, layoutFor(x2, w2), top, L"Terrain");
    if (importing) {
        HGDIOBJ of = SelectObject(mem, app.font);
        y = newParagraph(mem, {x2, y, x2 + w2, y + scale(200)}, L"The tileset, the size and the terrain come from the map.", kDim);
        SelectObject(mem, of);
        bottom = std::max(bottom, y);
    } else {
        y = label(x2, y, w2, L"Tileset");
        for (uint16_t era = 0; era < 8; ++era) {
            const RECT r{x2, y, x2 + w2, y + scale(40)};
            const bool on = gNew.era == era;
            ui::listRow(mem, r, on, ui::hot(r));
            const RECT sw{x2 + scale(10), r.top + scale(4), x2 + scale(10) + scale(64), r.bottom - scale(4)};
            if (!gNew.swatch[era].empty()) newDrawPicture(mem, sw, gNew.swatch[era], gNew.swatchSide, gNew.swatchSide, true);
            else fillAlpha(mem, sw, kPanel, 255);
            ui::box(mem, sw, CLR_NONE, kBorder, 2);
            RECT t{sw.right + scale(12), r.top, r.right - scale(8), r.bottom};
            SetTextColor(mem, ui::rowText(on));
            DrawTextW(mem, kTilesetNames[era], -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            hitsOut().push_back({r, nullptr, [era] {
                                     if (gNew.era != era) gNew.era = era, gNew.terrain = 0, newChanged();
                                 },
                                 L"", IDC_HAND, {}});
            hitsOut().back().name = std::wstring(L"the tileset ") + kTilesetNames[era];
            y = r.bottom;
        }
        y += scale(12);
        newSizeRow(mem, x2, y, std::min(scale(120), w2 * 2 / 5), x2 + w2, L"Width", gNew.w, 0);
        y += scale(ui::kFormRowH);
        newSizeRow(mem, x2, y, std::min(scale(120), w2 * 2 / 5), x2 + w2, L"Height", gNew.h, 1);
        y += scale(ui::kFormRowH);
        bottom = std::max(bottom, y);
    }

    // Start: a blank map filled with a terrain (the dropdown in its row), generated, or (an EUD project) imported.
    y = ui::sectionHeader(mem, layoutFor(x3, w3), top, L"Start");
    for (int k = 0; k < 4; ++k) {
        if (k == 2 && !gNew.project) continue;  // Import an existing map: a project (a classic map: File > Open)
        const bool on = k == 3 ? gNew.terrainOnly : k == 2 ? gNew.copyMap : !gNew.copyMap && !gNew.terrainOnly && gNew.generate == (k == 1);
        const int h = k == 0 ? fieldH + scale(8) : rowH;
        const RECT r{x3, y, x3 + w3, y + h};
        const bool over = ui::hot(r);
        ui::radioMark(mem, {x3 + scale(2), r.top + (h - scale(14)) / 2, x3 + scale(16), r.top + (h - scale(14)) / 2 + scale(14)}, on, over);
        const wchar_t* text = k == 3 ? L"The terrain of a map" : k == 2 ? L"Import an existing map" : k == 1 ? L"Generate" : L"Blank map filled with";
        RECT t{x3 + scale(26), r.top, r.right, r.bottom};
        SetTextColor(mem, on || over ? kText : kDim);
        DrawTextW(mem, text, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        hitsOut().push_back({r, nullptr, [k] {
                                 gNew.copyMap = k == 2, gNew.generate = k == 1, gNew.terrainOnly = k == 3;
                                 newChanged();
                             },
                             k == 3   ? L"The terrain, tiles and doodads of a map that you have (.scm or .scx); no units, locations or triggers"
                             : k == 2 ? L"A copy of a map that you have (.scm or .scx): its terrain, units, triggers and settings"
                             : k == 1 ? L"Land, water and high ground from a seed and the settings next to the preview"
                                      : L"The terrain on the whole map",
                             IDC_HAND, {}});
        if (k == 0) {  // the terrain, in the same row (after the hit of the row: a click on it opens the list)
            const IsomTerrainTable* table = IsomTerrainTable::forEra(gNew.era);
            const int bx = t.left + ui::textWidth(mem, text) + scale(10);
            gNew.terrainBox = {bx, r.top + scale(4), std::min(int(r.right), bx + scale(260)), r.top + scale(4) + fieldH};
            ui::pickBox(mem, gNew.terrainBox, widen(table ? table->terrainName(newTerrainId()) : ""), false, [](RECT b) { newPickTerrain(b); },
                        L"The terrain on the whole map");
        }
        y = r.bottom + scale(4);
    }
    // Import an existing map: the map's file (the copy goes into the project folder; the file stays as it is).
    if (gNew.copyMap || gNew.terrainOnly) {
        y += scale(8);
        const int bw = scale(ui::kButtonMinW), fw = std::min(w3, scale(520));
        newPlaceEdit(gNew.fromMap, {x3, y + scale(2), x3 + fw - bw - scale(8), y + scale(2) + fieldH}, true, 0, footerTop);
        ui::button(mem, {x3 + fw - bw, y, x3 + fw, y + scale(ui::kButtonH)}, L"Browse\x2026", ui::Tone::Normal, [] {
            const std::wstring file = fileDialog(false);
            if (!file.empty()) SetWindowTextW(gNew.fromMap, file.c_str()), newChanged(false);
        });
        y += scale(ui::kFormRowH);
        HGDIOBJ of = SelectObject(mem, ui::smallFont());
        y = newParagraph(mem, {x3, y, x3 + fw, y + scale(100)},
                         gNew.terrainOnly ? L"The new map gets the map's size, tileset, terrain and doodads. The file stays as it is."
                                          : L"StarDraft copies the map into the project folder. The file stays as it is.",
                         kDim) + scale(8);
        SelectObject(mem, of);
    } else {
        newPlaceEdit(gNew.fromMap, {}, false, 0, 0);
    }
    gNew.genShown = gNew.generate && !importing;
    gNew.previewRect = gNew.genRect = {};
    if (!importing) {
        // The preview: for Blank and Generate, under the Start choices.
        y += scale(8);
        if (y - blockTop != gNew.aboveH) {  // the preview's size follows the page's height: paint again with this one
            gNew.aboveH = y - blockTop;
            if (gNew.wnd) InvalidateRect(gNew.wnd, nullptr, FALSE);
        }
        const RECT pv{x3, y, x3 + side, y + side};
        newDrawPreview(mem, pv);
        gNew.previewRect = pv;
        y = pv.bottom;
    }
    if (gNew.genShown) {
        // The generator's settings: to the right of the preview (under it in a narrow window).
        const int gx = besides ? x3 + side + pvGap : x3, gr = gx + gw;
        int gy = besides ? gNew.previewRect.top : y + scale(12);
        const int labelW = std::min(scale(210), gw / 2);
        // Seed, symmetry.
        FormLayout fl = layoutFor(gx, labelW);
        fl.rowH = scale(ui::kFormRowH);
        const int gTop = gy;
        fieldLabel(mem, fl, gy, L"Seed", false);
        const int bw = ui::buttonWidth(mem, L"New seed");
        newPlaceEdit(gNew.seed, {gx + labelW, gy + scale(4), gr - bw - scale(8), gy + scale(4) + fieldH}, true, 0, footerTop);
        ui::button(mem, {gr - bw, gy + scale(2), gr, gy + scale(2) + scale(ui::kButtonH)}, L"New seed", ui::Tone::Normal, [] {
            if (gNew.seed) SetWindowTextW(gNew.seed, std::to_wstring((GetTickCount64() * 2654435761ull >> 8) % 999999 + 1).c_str());
            newChanged();
        });
        gy += fl.rowH;
        fieldLabel(mem, fl, gy, L"Symmetry", false);
        ui::pickBox(mem, {gx + labelW, gy + scale(4), gr, gy + scale(4) + fieldH}, kSymmetryNames[gNew.symmetry], false, [](RECT b) {
            std::vector<ListPickItem> items;
            for (int i = 0; i < int(std::size(kSymmetryNames)); ++i) items.push_back({kSymmetryNames[i], L"", std::to_string(i)});
            std::string picked;
            if (showListPicker(L"Symmetry", items, std::to_string(gNew.symmetry), belowBox(b), picked)) gNew.symmetry = std::stoi(picked), newChanged();
        });
        gy += fl.rowH + scale(4);
        {  // above the sliders: they stay in their place when a tileset has more or fewer sliders
            const int lx = ui::smallButton(mem, gx, gy, rowH, L"Randomize", [] { newRandomize(false); }, L"New amounts, shape and seed");
            ui::smallButton(mem, lx + scale(8), gy, rowH, L"Randomize all", [] { newRandomize(true); }, L"The tileset too (not the size)");
            gy += rowH + scale(8);
        }
        gy = ui::sectionHeader(mem, layoutFor(gx, gw), gy, L"Amounts");
        for (auto& sl : gNew.shares) newSliderRow(mem, gx, gy, labelW, gr, sl, true), gy += rowH;
        gy += scale(12);
        gy = ui::sectionHeader(mem, layoutFor(gx, gw), gy, L"Shape");
        for (auto& sl : gNew.shape) newSliderRow(mem, gx, gy, labelW, gr, sl, false), gy += rowH;
        gy += scale(8);
        gNew.genRect = {gx, gTop, gr, gy};
        y = std::max(y, gy);
    } else {
        newPlaceEdit(gNew.seed, {}, false, 0, 0);
    }
    bottom = std::max(bottom, y);

    // Create and Cancel: at the bottom right of the block, under the columns. When the block is taller than the page,
    // they stay in view at the bottom of the page, on a bar with a line above it (the columns scroll under it).
    gNew.createEnabled = gNew.warning.empty();
    gNew.tallest = std::max(gNew.tallest, bottom - blockTop);  // (Generate is taller than Import: Create stays where it was)
    const int natural = blockTop + gNew.tallest + scale(24), lowest = int(rc.bottom) - pad - bh;
    gNew.footerStuck = natural > lowest;
    const int by = gNew.footerStuck ? lowest : natural;
    if (gNew.footerStuck) {
        const RECT bar{0, by - pad, rc.right, rc.bottom};
        fillAlpha(mem, bar, kBg, 255);
        fillAlpha(mem, {0, bar.top, rc.right, bar.top + 1}, kBorder, 255);
        hitsOut().push_back({bar, nullptr, [] {}, L"", IDC_ARROW, {}});  // a click on the bar does not go to the columns under it
    }
    const int right = x3 + w3, cw = ui::buttonWidth(mem, L"Cancel"), crw = ui::buttonWidth(mem, L"Create"), bgap = scale(ui::kButtonGap);
    ui::button(mem, {right - cw, by, right, by + bh}, L"Cancel", ui::Tone::Normal, [] { newCancel(); });
    ui::button(mem, {right - cw - bgap - crw, by, right - cw - bgap, by + bh}, L"Create", ui::Tone::Primary, [] { newCreate(); },
               gNew.createEnabled ? (gNew.project ? L"Make the project folder and its map" : L"Make the map in this tab") : gNew.warning, gNew.createEnabled);
    gNew.footer = {right - cw - bgap - crw, by, right, by + bh};
    const int h = natural + bh - blockTop;
    gNew.height = startY + h + pad;
    if (h != gNew.blockH) {  // the height changed (Generate, a warning): paint again, in the middle
        gNew.blockH = h;
        if (gNew.wnd) InvalidateRect(gNew.wnd, nullptr, FALSE);
    }
}

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

// Opens recent item `i` (as the File menu's lists do: a missing one leaves the list).
void startOpen(bool project, size_t i) { command(int(i) + (project ? ID_RECENT_PROJECT : ID_RECENT_MAP)); }
void startRemove(bool project, size_t i) {
    auto& list = project ? gRecentProjects : gRecentMaps;
    if (i >= list.size()) return;
    const std::wstring name = fs::path(list[i]).filename().wstring();
    list.erase(list.begin() + long(i));
    saveRecent(), refreshRecentMenus();
    setMessage(L"Removed " + name + L" from the list");
    if (gNew.wnd) InvalidateRect(gNew.wnd, nullptr, FALSE);
}

// The start page: in the middle of the page, the project kinds as cards (a card opens its settings), one list of the
// recent projects and maps (the newest first) and Open; the alpha notice at the bottom of the page.
void paintStartPage(HDC mem, RECT rc) {
    static int blockH = 0;  // the height of the block at the last paint (to put it in the middle)
    const int pad = scale(ui::kDialogPad), rowH = scale(ui::kRowH), bh = scale(ui::kButtonH), gap = scale(16);
    const int avail = int(rc.right) - 2 * pad;
    const int cardW = std::clamp((avail - 3 * gap) / 4, scale(180), scale(320));
    const int w = 4 * cardW + 3 * gap;
    const int x1 = std::max(pad, (int(rc.right) - w) / 2);
    // The alpha notice (information, not a warning): until 1.0, errors can occur; backup copies keep the work safe.
    const wchar_t* notice = L"StarDraft is alpha software. Errors can occur. Make backup copies of your projects and maps before you edit them. "
                             L"Your feedback makes StarDraft better: right-click anything and select \"Report feedback on \x2026\".";  // (#94)
    RECT nc{0, 0, w - scale(22), 0};
    DrawTextW(mem, notice, -1, &nc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    const int noticeH = int(nc.bottom - nc.top);
    (void)noticeH;
    const int startY = std::max(pad, (int(rc.bottom) - blockH) / 2) - gNew.scroll;  // (the block has the notice under the list)
    int y = startY;

    // The cards: the kinds side by side. Click a card (or Enter on it) to go to its settings.
    const int inPad = scale(16), iconS = scale(32);
    int textH = 0;  // all cards have the height of the longest text
    for (const wchar_t* text : kKindCards) {
        RECT calc{0, 0, cardW - 2 * inPad, 0};
        DrawTextW(mem, text, -1, &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        textH = std::max(textH, int(calc.bottom - calc.top));
    }
    const int cardH = inPad + iconS + scale(12) + scale(26) + scale(6) + textH + inPad;
    const UiIcon icons[] = {UiIcon::Map, UiIcon::Project, UiIcon::Folder, UiIcon::Assets};
    gNew.cardsEnabled = 0;
    for (int k = 0; k < 4; ++k) {
        const bool enabled = k < 2;  // Campaign project and Mod project: later
        const RECT card{x1 + k * (cardW + gap), y, x1 + k * (cardW + gap) + cardW, y + cardH};
        const bool over = enabled && ui::hot(card), focused = enabled && gNew.card == k && GetFocus() == gNew.wnd;
        if (!enabled) ui::box(mem, card, kReadOnly, kReadOnlyLine);
        else ui::box(mem, card, over ? kCtlHot : kCtl, focused ? kAccent : over ? kBorderHot : kBorder);
        uiIcon(mem, {card.left + inPad, card.top + inPad, card.left + inPad + iconS, card.top + inPad + iconS}, icons[k], enabled ? kAccent : kFaint, iconS);
        int ty = card.top + inPad + iconS + scale(12);
        {
            HGDIOBJ of = SelectObject(mem, ui::titleFont());
            RECT tt{card.left + inPad, ty, card.right - inPad, ty + scale(26)};
            SetTextColor(mem, enabled ? kText : kFaint);
            DrawTextW(mem, kKindNames[k], -1, &tt, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(mem, of);
        }
        if (!enabled) {  // the "Coming later" mark, at the top right
            HGDIOBJ of = SelectObject(mem, ui::smallFont());
            const int lw = ui::textWidth(mem, L"Coming later") + scale(16), lh = scale(20);
            const RECT mark{card.right - inPad - lw, card.top + inPad, card.right - inPad, card.top + inPad + lh};
            ui::box(mem, mark, kChipDim, kReadOnlyLine);
            RECT mt = mark;
            SetTextColor(mem, kDim);
            DrawTextW(mem, L"Coming later", -1, &mt, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            SelectObject(mem, of);
        }
        ty += scale(26) + scale(6);
        newParagraph(mem, {card.left + inPad, ty, card.right - inPad, card.bottom - inPad}, kKindCards[k], enabled ? kDim : kFaint);
        if (enabled) {
            ++gNew.cardsEnabled;
            hitsOut().push_back({card, nullptr, [k] { openNewPage(k); }, L"", IDC_HAND, {}});
            hitsOut().back().name = std::wstring(L"the ") + kKindNames[k] + L" card";
        } else {
            hitsOut().push_back({card, nullptr, nullptr, k == 2 ? L"Campaign projects come in a later version" : L"Mod projects come in a later version", IDC_NO, {}});
        }
    }
    gNew.cardsShown = true;
    y += cardH + scale(32);

    // The recent projects and maps.
    FormLayout l{};
    l.x0 = x1, l.labelW = w, l.rowH = rowH, l.colW = w, l.colRight = x1 + w;
    y = ui::sectionHeader(mem, l, y, L"Recent");
    // Projects and maps in one list, the newest first (a file that is not there goes last).
    struct Recent {
        bool project;
        size_t index;
        fs::path path;
        std::wstring date;
    };
    std::vector<Recent> items;
    for (size_t i = 0; i < gRecentProjects.size(); ++i) items.push_back({true, i, fs::path(gRecentProjects[i]), startDate(gRecentProjects[i])});
    for (size_t i = 0; i < gRecentMaps.size(); ++i) items.push_back({false, i, fs::path(gRecentMaps[i]), startDate(gRecentMaps[i])});
    std::stable_sort(items.begin(), items.end(), [](const Recent& a, const Recent& b) { return a.date > b.date; });
    gNew.startProjects = int(gRecentProjects.size()), gNew.startMaps = int(gRecentMaps.size());
    if (items.empty()) {
        RECT n{x1 + scale(8), y, x1 + w, y + rowH};
        SetTextColor(mem, kDim);
        DrawTextW(mem, L"None", -1, &n, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        y += rowH;
    }
    for (const Recent& it : items) {
        const bool project = it.project;
        const size_t i = it.index;
        const RECT r{x1, y, x1 + w, y + rowH};
        ui::listRow(mem, r, false, ui::hot(r));
        hitsOut().push_back({r, nullptr, [project, i] { startOpen(project, i); }, fs::path(it.path).make_preferred().wstring(), IDC_HAND, {}});
        uiIcon(mem, {x1 + scale(8), r.top + (rowH - scale(16)) / 2, x1 + scale(24), r.top + (rowH - scale(16)) / 2 + scale(16)},
               project ? UiIcon::Project : UiIcon::Map, kDim, scale(16));
        // Remove: under the mouse; always where the file is not found (the one thing to do with it).
        const int actionsLeft = ui::rowActions(mem, r, {{UiIcon::Close, L"Remove from the list", [project, i] { startRemove(project, i); }, ui::Tone::Danger}},
                                               it.date.empty());
        const int dateW = scale(96);
        RECT dt{actionsLeft - dateW - scale(8), r.top, actionsLeft - scale(8), r.bottom};
        SetTextColor(mem, it.date.empty() ? kWarn : kDim);
        DrawTextW(mem, it.date.empty() ? L"Not found" : it.date.c_str(), -1, &dt, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        const std::wstring name = it.path.filename().wstring();
        RECT nt{x1 + scale(32), r.top, std::min<int>(x1 + scale(32) + w * 2 / 5, dt.left - scale(16)), r.bottom};
        SetTextColor(mem, kText);
        DrawTextW(mem, name.c_str(), -1, &nt, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        const int nameRight = nt.left + std::min<int>(nt.right - nt.left, ui::textWidth(mem, name));
        std::wstring folder = fs::path(it.path.parent_path()).make_preferred().wstring();
        RECT pt{nameRight + scale(16), r.top, dt.left - scale(16), r.bottom};
        SetTextColor(mem, kDim);
        DrawTextW(mem, folder.data(), -1, &pt, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        y += rowH;
    }
    // Open: a project or a map that is not in the list.
    y += scale(12);
    const int ow = std::max(scale(ui::kButtonMinW), ui::buttonWidth(mem, L"Open\x2026"));
    ui::button(mem, {x1, y, x1 + ow, y + bh}, L"Open\x2026", ui::Tone::Normal, [] { command(ID_FILE_OPEN); }, L"A project or a map (Ctrl+O)");
    y += bh;
    // The alpha notice: under the recent list and Open.
    const int ny = y + scale(24);
    ui::glyph(mem, {x1, ny + scale(2), x1 + scale(16), ny + scale(18)}, L"\xE946", kAccent, scale(14));
    y = newParagraph(mem, {x1 + scale(22), ny, x1 + w, ny + scale(200)}, notice, kDim);
    const int h = y - startY;
    if (h != blockH) {  // the height changed (a row came or went): paint again, in the middle
        blockH = h;
        if (gNew.wnd) InvalidateRect(gNew.wnd, nullptr, FALSE);
    }
    gNew.height = y + gNew.scroll + pad;
    for (HWND e : {gNew.name, gNew.location, gNew.output, gNew.seed, gNew.fromMap}) newPlaceEdit(e, {}, false, 0, 0);
}

void paintPage(HWND hwnd) {
    gNew.surface.begin(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    gNew.wheels.clear();
    if (activePage() == kPageStart) paintStartPage(mem, rc);
    else paintNewPage(mem, rc);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, gNew.height - 1), UINT(rc.bottom), gNew.scroll};
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gNew.surface.end(hwnd);
}

LRESULT CALLBACK pageWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; gNew.surface.message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_PAINT: paintPage(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_TIMER:
        KillTimer(hwnd, 1);
        newRegenerate();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE) newChanged(HWND(lp) == gNew.seed && gNew.generate);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: InvalidateRect(hwnd, nullptr, FALSE); break;  // the keyboard's card
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (activePage() == kPageStart) {  // the cards: Left and Right move, Enter or Space opens the settings
            if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_TAB) gNew.card = wp == VK_LEFT ? 0 : wp == VK_RIGHT ? 1 : 1 - gNew.card;
            else if (wp == VK_RETURN || wp == VK_SPACE) openNewPage(gNew.card);
            else break;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (activePage() != kPageNew) break;
        if (wp == VK_ESCAPE) return newBack(), 0;
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = HDC(wp);
        SetTextColor(dc, kText);
        SetBkColor(dc, kCtl);
        return LRESULT(app.ctlBrush);
    }
    case WM_MOUSEWHEEL:
        if (msg == WM_MOUSEWHEEL) {  // over a slider: the wheel changes it (up: more); elsewhere it scrolls the page
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            for (const auto& [r, step] : gNew.wheels)
                if (PtInRect(&r, pt)) {
                    step(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
        }
        [[fallthrough]];
    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int s = msg == WM_MOUSEWHEEL ? gNew.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(32) * 3 / WHEEL_DELTA : scrollFromMessage(hwnd, wp, gNew.scroll, scale(32));
        gNew.scroll = std::clamp(s, 0, std::max(0, gNew.height - int(rc.bottom)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void createPageWindow() {
    if (gNew.wnd) return;
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpfnWndProc = pageWndProc, wc.lpszClassName = L"StarDraftNewPage";
    RegisterClassExW(&wc);
    gNew.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"StarDraftNewPage", L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 1, 1, app.main, nullptr,
                               app.inst, nullptr);
    darkControl(gNew.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    auto edit = [](const wchar_t* text, DWORD style) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, text, WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | style, 0, 0, 1, 1, gNew.wnd, nullptr, app.inst, nullptr);
        SendMessageW(h, WM_SETFONT, WPARAM(app.font), TRUE);
        darkControl(h, L"DarkMode_CFD");
        return h;
    };
    gNew.name = edit(newFreeName(L"MyMap", defaultProjectsFolder()).c_str(), 0);
    gNew.location = edit(defaultProjectsFolder().make_preferred().wstring().c_str(), 0);
    gNew.output = edit(L"", 0);
    gNew.seed = edit(L"12345", ES_NUMBER);
    gNew.fromMap = edit(L"", 0);
    SendMessageW(gNew.fromMap, EM_SETCUEBANNER, TRUE, LPARAM(L"The map to copy (.scm or .scx)"));
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
        ShowWindow(gNew.wnd, SW_HIDE);
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
        if (w == gNew.wnd || w == gTopBar || w == app.tabs || w == app.status || w == gLog.wnd || !IsWindowVisible(w)) continue;
        ShowWindow(w, SW_HIDE);
        gPageHidden.push_back(w);
    }
    SetWindowPos(gNew.wnd, HWND_TOP, 0, top, int(rc.right), std::max(1, bottom - top), SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if (!was) gNew.scroll = 0;
    if (p == kPageNew && !newImporting() && !gNew.previewMade) SetTimer(gNew.wnd, 1, 10, nullptr);
    InvalidateRect(gNew.wnd, nullptr, FALSE);
    if (GetFocus() == app.view || !GetFocus()) SetFocus(gNew.wnd);
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
