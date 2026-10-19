// Each palette panel's own state (portable, core.inc): a panel is the side bar's palette of a mode (each map tab has
// one of each mode) or a palette panel in a window of its own (a mode's floating palette, a new panel); each keeps, for
// each mode it shows, its pick, its tools and brush, its player, its mirrors, Isometric or Rectangle and its tiles. The
// panel used last is the active one: its state is the app state's (the map's tools work with it), and the others keep
// theirs here. A panel that is not active paints and measures with its own state in the app state for a moment
// (PanelScope).

struct PanelState {
    Mode mode = Mode::Units;
    int palette = -1, palette2 = -1;  // (AppState's)
    std::vector<int> chosen;          // (shellPaletteChosen)
    int owner = 0;
    float brushSize = 2, brushHeight = 0;
    Brush::Shape brushShape = Brush::Diamond;
    int tool = 0;                     // (AppState::toolByMode of its mode)
    int density = 10;
    int mirrors = 0;                  // (the Editor's)
    bool terrainRect = false;
    TilesView* tiles = nullptr;       // Terrain: its tiles, and the views that show them (its panel's)
    TilesGridView* tilesGrid = nullptr;
    TilesPanelView* tilesPanel = nullptr;
};

std::vector<int>& shellPaletteChosen();
PanelState* gActivePanel = nullptr;

// The app state into the panel's state (the active panel's, before another one is), and back.
void panelStore(PanelState& s) {
    const AppState& a = appState();
    s.palette = a.palette, s.palette2 = a.palette2, s.chosen = shellPaletteChosen(), s.owner = a.playerOwner;
    s.brushSize = a.brushSize, s.brushHeight = a.brushHeight, s.brushShape = a.brushShape;
    s.tool = a.toolByMode[int(s.mode)], s.density = a.sprayDensity, s.terrainRect = a.terrainRect;
    if (a.ed) s.mirrors = a.ed->mirrors();
}
void panelLoad(const PanelState& s) {
    AppState& a = appState();
    a.mode = s.mode;
    a.palette = s.palette, a.palette2 = s.palette2, shellPaletteChosen() = s.chosen, a.playerOwner = s.owner;
    a.brushSize = s.brushSize, a.brushHeight = s.brushHeight, a.brushShape = s.brushShape;
    a.toolByMode[int(s.mode)] = s.tool, a.sprayDensity = s.density, a.terrainRect = s.terrainRect;
    if (a.ed) a.ed->setMirrors(s.mirrors);
    if (s.tiles && s.tilesGrid && s.tilesPanel) gTiles = s.tiles, gTilesGrid = s.tilesGrid, gTilesPanel = s.tilesPanel;
}
// The panel's state as it is now (the active panel's is the app state's).
PanelState& panelFresh(PanelState& s) {
    if (&s == gActivePanel) panelStore(s);
    return s;
}

// A panel that is not active, for a moment (its paint, its height): its state is the app state's, then the active
// panel's again. What changes in it is the panel's.
struct PanelScope {
    PanelState* target = nullptr;
    PanelState saved;
    explicit PanelScope(PanelState* s) {
        if (!s || s == gActivePanel) return;
        target = s;
        const AppState& a = appState();
        saved.mode = a.mode;
        panelStore(saved);
        saved.tiles = gTiles, saved.tilesGrid = gTilesGrid, saved.tilesPanel = gTilesPanel;
        panelLoad(*s);
    }
    ~PanelScope() {
        if (!target) return;
        panelStore(*target);
        panelLoad(saved);
    }
    PanelScope(const PanelScope&) = delete;
    PanelScope& operator=(const PanelScope&) = delete;
};

// A new panel state of mode `m`: the app's brush and player, nothing picked, the mode's first tool, Isometric.
PanelState panelNew(Mode m) {
    const AppState& a = appState();
    PanelState s;
    s.mode = m, s.owner = a.playerOwner;
    s.brushSize = 2, s.brushHeight = 0, s.brushShape = a.brushShape;
    s.density = m == Mode::Terrain ? 5 : 10;
    return s;
}
// Another panel's settings (a new panel like it, a palette floating or docked again): all but its tiles' place (each
// panel has its own Tiles list); its tiles are copied.
void panelCopy(PanelState& to, PanelState& from) {
    panelFresh(from);
    TilesView* tiles = to.tiles;
    TilesGridView* grid = to.tilesGrid;
    TilesPanelView* tp = to.tilesPanel;
    to = from;
    to.tiles = tiles, to.tilesGrid = grid, to.tilesPanel = tp;
    if (tiles && from.tiles && tiles != from.tiles) {
        *tiles = *from.tiles;
        tiles->layoutW = -1;  // (its grid's width: built again)
    }
}

// --- The side bar's panels: one of each mode for each map tab ---

struct DockedPanels {
    std::array<PanelState, kModeCount> modes;
    TilesView tiles;  // Terrain's (the side bar's Tiles list shows the active tab's)
    std::array<PanelState*, kModeCount> lastOfMode{};  // the tab's panel of each mode used last (setMode goes to it)
    PanelState* active = nullptr;                      // the tab's active panel while another tab is active
    bool made = false;
};
std::map<int, DockedPanels> gDockedPanels;  // (by the tab's key: Doc::key)
int activeDocKey();
DockedPanels& dockedPanelsOf(int doc) {
    DockedPanels& d = gDockedPanels[doc];
    if (!d.made) {
        d.made = true;
        for (int k = 0; k < kModeCount; ++k) d.modes[size_t(k)] = panelNew(Mode(k));
        d.modes[size_t(Mode::Terrain)].tiles = &d.tiles;
    }
    return d;
}
TilesGridView* tilesDockGrid();  // (views_tiles.inc: the side bar's Tiles list, showing the active tab's tiles)
TilesPanelView* tilesDockPanel();
void tilesDockBind(TilesView* d, PanelState* s);
// The side bar's panel of mode `m` in the active tab.
PanelState& dockedPanel(Mode m) {
    PanelState& s = dockedPanelsOf(activeDocKey()).modes[size_t(m)];
    if (m == Mode::Terrain) s.tilesGrid = tilesDockGrid(), s.tilesPanel = tilesDockPanel(), tilesDockBind(s.tiles, &s);
    return s;
}
bool isDockedPanel(const PanelState* s) {
    if (!s) return false;
    const DockedPanels& d = dockedPanelsOf(activeDocKey());
    return s >= &d.modes.front() && s <= &d.modes.back();
}

// The panel that setMode makes active: the tab's panel of the mode used last (while it is there), else the mode's
// floating palette, else its side bar's.
PanelState* floatingPanelStateOf(Mode m);  // (sdl_palette_panels.inc)
PanelState* panelHomeOf(Mode m) {
    if (PanelState* s = dockedPanelsOf(activeDocKey()).lastOfMode[size_t(m)]) return s;
    if (PanelState* s = floatingPanelStateOf(m)) return s;
    return &dockedPanel(m);
}
// A panel goes (its window closes): it is no longer one to go back to.
void panelForget(const PanelState* s) {
    for (auto& [doc, d] : gDockedPanels) {
        for (PanelState*& p : d.lastOfMode)
            if (p == s) p = nullptr;
        if (d.active == s) d.active = nullptr;
    }
    if (gActivePanel == s) gActivePanel = nullptr;
}
// The tab goes to the back (another one is active): its active panel keeps its state, and comes back with the tab.
void panelsStash() {
    if (!gActivePanel) return;
    panelStore(*gActivePanel);
    dockedPanelsOf(activeDocKey()).active = gActivePanel;
    gActivePanel = nullptr;
}

void shellModeSide(Mode m);
void shellModeWindows(Mode m);
bool shellViewShown();
void shellRelayout();
void shellPanelsChanged();  // the panels' strips and lists show the active panel (sdl)
void fillToolCombo(AppState& app);
void rebuildList(AppState& app);

// The panel becomes the active one: its mode is the app's (another mode: the selected units, doodads and sprites go,
// as they are a mode's), its pick, tools, brush, player, mirrors and tiles are the map's.
void activatePanel(PanelState* p) {
    AppState& app = appState();
    if (!p) return;
    const bool changed = app.mode != p->mode;
    if (changed) app.selDoodads.clear(), app.selSprites.clear(), app.pastingItems = false;
    // The selected units are Units mode's: they show in each mode, so they are let go when another mode starts.
    if (changed && app.mode == Mode::Units && app.ed) app.ed->clearSelection();
    if (p != gActivePanel) {
        if (gActivePanel) panelStore(*gActivePanel);
        gActivePanel = p;
        panelLoad(*p);
    }
    DockedPanels& d = dockedPanelsOf(activeDocKey());
    d.lastOfMode[size_t(p->mode)] = p;
    shellModeSide(app.mode);  // the side the mode's tools are docked on, and the palette's view of the mode
    if (shellViewShown()) shellRelayout();  // the toolbar's side, and what the other column shows
    shellModeWindows(app.mode);
    if (shellViewShown()) shellRelayout();
    fillToolCombo(app);
    app.terrainPasting = false;
    rebuildList(app);
    app.drag = Drag::None;
    app.dirty = true;
    shellPanelsChanged();
}
// The active tab's panel that was active when the tab went to the back (else the home of the app's mode).
void panelsShowTab() {
    DockedPanels& d = dockedPanelsOf(activeDocKey());
    PanelState* p = d.active ? d.active : panelHomeOf(appState().mode);
    d.active = nullptr;
    gActivePanel = nullptr;  // (the tab's state is not the app state yet)
    activatePanel(p);
}

// A panel's state in the map's own file (map_prefs.inc): its mode, tool, brush size, height and shape, density, mirrors,
// Isometric or Rectangle, player, pick, second terrain and tile, tab-separated.
std::string panelStateText(PanelState& s) {
    panelFresh(s);
    char b[160];
    std::snprintf(b, sizeof b, "%d\t%d\t%g\t%g\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", int(s.mode), s.tool, s.brushSize, s.brushHeight, int(s.brushShape), s.density,
                  s.mirrors, int(s.terrainRect), s.owner, s.palette, s.palette2, s.tiles ? s.tiles->picked : -1);
    return b;
}
// Back from the text (from part `at` of the line's tab-separated parts): false when it is not a state of mode `m`.
bool panelStateRead(PanelState& s, const std::vector<std::string>& part, size_t at, Mode m) {
    if (part.size() < at + 12 || std::atoi(part[at].c_str()) != int(m)) return false;
    auto num = [&](size_t k) { return std::atoi(part[at + k].c_str()); };
    s.mode = m, s.tool = std::max(0, num(1));
    s.brushSize = std::clamp(float(std::atof(part[at + 2].c_str())), 0.5f, 32.0f), s.brushHeight = std::clamp(float(std::atof(part[at + 3].c_str())), 0.0f, 32.0f);
    s.brushShape = Brush::Shape(std::clamp(num(4), 0, int(Brush::Circle)));
    s.density = std::clamp(num(5), 1, kDensityMax), s.mirrors = num(6) & Mirror::All, s.terrainRect = num(7) != 0;
    s.owner = std::clamp(num(8), 0, 11), s.palette = num(9), s.palette2 = num(10);
    s.chosen.clear();
    if (s.tiles) s.tiles->picked = num(11);
    return true;
}
// The tab's panels came from its file after the tab showed: the active one's state is the app state's again.
void panelsReloaded() {
    PanelState* p = gActivePanel;
    gActivePanel = nullptr;
    if (p) activatePanel(p);
}
