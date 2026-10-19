// The Map workspace's command table and its camera and save (portable, core.inc): addCommand, zoomAt and fitMap, the
// table's commands (registerMapCommands, the Mode menu and the View items' keys) and saveMapFile (File > Save). Each shell
// calls registerMapCommands once at its start. The graphics quality (setGraphics) is here; the dock layout's save
// and the trigger pane's zoom are core/dock_prefs.inc.

// The renderer draws in this quality and art (the shell's: shell_win32.inc, shell_sdl.inc; it needs no other editor type).
void shellRendererGraphics(GraphicsQuality q, bool carbot);
// The graphics quality and Carbot: the state, the renderer's (shellRendererGraphics), the palette's pictures.
void setGraphics(GraphicsQuality q, bool carbot) {
    AppState& app = shellState();
    app.quality = q, app.carbot = carbot;
    shellRendererGraphics(q, carbot);
    if (carbot) setMessage(L"Carbot (cartooned) graphics");
    shellPaletteChanged();
    app.dirty = true;
}

// A command with its Win32 id. Labels and shortcuts are the menu's English texts (translated where the menu is made).
void addCommand(int nativeId, const char* id, const wchar_t* label, const char* menu, const wchar_t* shortcut, std::function<void()> run,
                std::function<bool()> checked = {}, bool radio = false, bool accelerator = true) {
    stardraft::Command c;
    c.id = id, c.label = narrow(label), c.menu = menu, c.shortcut = narrow(shortcut);
    c.run = std::move(run), c.checked = std::move(checked), c.radio = radio, c.accelerator = accelerator;
    gCommandIds[nativeId] = c.id;
    gCommands.add(std::move(c));
}

// View > Overlays > Pathing and Scenario > Check pathing (core/pathing_layer.inc).
bool pathingLayerOn();
bool pathingLayerAreas();
void pathingLayerToggle();
void pathingLayerMode(bool areas);
void fogToggle();  // View > Overlays > Fog of war (core/fog_layer.inc)
const std::vector<int>& paletteChosen();

// Edit > Decorate the terrain (roadmap R37): doodads in the terrain selection (else the whole map), one in each
// square of `spacing` tiles where one fits its tiles and its terrain (Editor::doodadScatterFits). The doodads: those
// chosen in the Doodads palette (Ctrl+click there), else all of the tileset's that need tile groups. One undo step.
void decorateTerrain(int spacing) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    std::vector<uint16_t> ids;
    if (app.mode == Mode::Doodads)
        for (int v : paletteChosen())
            if (v >= 0) ids.push_back(uint16_t(v));
    if (ids.empty())
        for (const DoodadType& t : ed.doodadTypes().all())
            if (t.inPalette && t.needsTiles()) ids.push_back(t.id);
    const Rect area = app.hasTerrainSel ? app.terrainSel.normalized() : Rect{0, 0, ed.width() * 32, ed.height() * 32};
    const int n = ed.decorate(ids, area, spacing, uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()));
    app.dirty = true;
    setMessage(n ? L"Placed " + std::to_wstring(n) + L" doodads where they fit the terrain" : std::wstring(tr(L"No doodad fits the terrain there")));
}
// View > Layers > Map revealers (Discord feedback #125): the Map Revealer units are drawn and picked, or not (they stay in
// the map). The editor's picking follows it (Editor::setMapRevealersHidden).
void toggleRevealers() {
    gLayers.revealers = !gLayers.revealers;
    if (AppState& app = shellState(); app.ed) app.ed->setMapRevealersHidden(!gLayers.revealers);
    setMessage(gLayers.revealers ? std::wstring(tr(L"Map revealers show")) : std::wstring(tr(L"Map revealers are hidden (View > Layers shows them again)")));
    shellMenusChanged();
    shellState().dirty = true;
}

// Edit > Tools > Place map revealers: the players' dialog (the host: showMapRevealersDialog, sdl_map_dialogs.inc), then a
// revealer for each chosen player on each square of 8 tiles over the map (Editor::placeMapRevealers). One undo step.
bool showMapRevealersDialog(std::vector<uint8_t>& owners);
void placeMapRevealersCommand() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    std::vector<uint8_t> owners;
    if (!showMapRevealersDialog(owners)) return;
    const int n = app.ed->placeMapRevealers(owners, 8);
    app.dirty = true;
    setMessage(n ? L"Placed " + std::to_wstring(n) + L" map revealers" : std::wstring(tr(L"Nothing placed: each chosen player has a map revealer on every square")));
}

// Edit > Tools > Remove all map revealers: every Map Revealer, of every player. One undo step.
void removeMapRevealersCommand() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    const int n = app.ed->removeMapRevealers();
    app.dirty = true;
    setMessage(n ? L"Removed " + std::to_wstring(n) + L" map revealers" : std::wstring(tr(L"There are no map revealers on the map")));
}
extern bool gShowFog;
enum class TerrainFlagsView;  // View > Overlays > Terrain flags (core/terrain_flags_layer.inc)
TerrainFlagsView terrainFlagsView();
void terrainFlagsSet(TerrainFlagsView v);
void pathingCheckCommand();

void toggleLayer(int k) {
    bool* layer[4] = {&gLayers.terrain, &gLayers.doodads, &gLayers.sprites, &gLayers.units};
    static const wchar_t* const names[4] = {L"Terrain", L"Doodads", L"Sprites", L"Units"};
    bool& on = *layer[k];
    on = !on;
    extern int gLayerShows;  // (core/map_edit.inc: the hidden layer's question asks again)
    if (on) ++gLayerShows;
    setMessage(std::wstring(names[k]) + (on ? L" show" : L" are hidden (View > Layers)"));
    shellMenusChanged();
    shellState().dirty = true;
}

void zoomAt(float factor, int sx, int sy) {
    AppState& app = shellState();
    float wx, wy;
    toWorld(sx, sy, wx, wy);
    app.zoom = std::clamp(app.zoom * factor, 1.0f / 16, 16.0f);
    app.camX = wx - sx / app.zoom;
    app.camY = wy - sy / app.zoom;
    app.dirty = true;
}

// The save of the open map (File > Save and Save as): the Editor writes the file, then the title, the recent list and
// the message follow. False, with the error text in err, when the file could not be written.
bool saveMapFile(const fs::path& path, std::string& err) {
    AppState& app = shellState();
    if (!app.ed) return err = "No map is open.", false;
    if (!app.ed->save(path, err)) return false;
    updateTitle();
    if (!activeProject()) addRecent(gRecentMaps, path);
    setMessage(L"Saved " + path.filename().wstring());
    return true;
}
// The same for the map `asked` (the map whose Save as dialog answers): only while it is the open map. A dialog's answer
// that comes after a tab switch or a close does not write another map to the file picked for this one.
bool saveMapFileOf(const Editor* asked, const fs::path& path, std::string& err) {
    if (!asked || shellState().ed.get() != asked) return err = "The map is no longer open. Nothing was saved.", false;
    return saveMapFile(path, err);
}

// File > Save to the map's own file asks for another place (Save as) only when StarDraft cannot write there: the folder
// does not take a new file (the save writes <name>.tmp beside the map first; a test file shows it, as a folder under
// Program Files that only an administrator can write to), or the map's file cannot be written (read-only). A folder
// that StarDraft can write to is good, also under Program Files (GitHub #148).
bool saveNeedsOtherPlace(const fs::path& path) {
    std::error_code ec;
    const fs::path dir = path.parent_path();
    if (dir.empty() || !fs::is_directory(dir, ec)) return false;  // (the save says why)
    const fs::path probe = dir / (L".stardraft-write-test-" + std::to_wstring(shellProcessId()));
    {
        std::ofstream f(probe, std::ios::binary);
        if (!f) return true;
        f.put('\0');
        if (!f.flush()) {
            f.close();
            fs::remove(probe, ec);
            return true;
        }
    }
    fs::remove(probe, ec);
    if (fs::is_regular_file(path, ec)) {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);  // (opened to write, nothing changes)
        if (!f) return true;
    }
    return false;
}

// A test of File > Save as (the snapshots' --saveas): the open map is saved to `path` as Save as saves it, then the file
// opens in a second editor and its parts are compared with the open map's. The line ends with "(yes)" when they are the
// same, else with what is different and "(WRONG)".
std::string mapSaveRoundTrip(const fs::path& path) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return "save round-trip: no map is open (WRONG)";
    std::string err;
    if (!saveMapFile(path, err)) return "save round-trip: could not save " + narrow(path.wstring()) + ": " + err + " (WRONG)";
    const Editor& a = *app.ed;
    Editor b(app.data);
    if (!b.open(path, err)) return "save round-trip: could not open the saved map again: " + err + " (WRONG)";
    std::vector<std::string> diff;
    if (a.width() != b.width() || a.height() != b.height() || a.era() != b.era()) diff.push_back("size or tileset");
    if (a.terrain().tiles != b.terrain().tiles) diff.push_back("terrain");
    auto sameUnit = [](const Unit& x, const Unit& y) {
        return x.x == y.x && x.y == y.y && x.type == y.type && x.owner == y.owner && x.hitPoints == y.hitPoints && x.resources == y.resources &&
               x.state == y.state;
    };
    if (!std::equal(a.units().begin(), a.units().end(), b.units().begin(), b.units().end(), sameUnit)) diff.push_back("units");
    auto sameDoodad = [](const Doodad& x, const Doodad& y) { return x.id == y.id && x.x == y.x && x.y == y.y && x.owner == y.owner; };
    if (!std::equal(a.doodads().begin(), a.doodads().end(), b.doodads().begin(), b.doodads().end(), sameDoodad)) diff.push_back("doodads");
    auto sameSprite = [](const MapSprite& x, const MapSprite& y) { return x.id == y.id && x.x == y.x && x.y == y.y && x.flags == y.flags; };
    if (!std::equal(a.sprites().begin(), a.sprites().end(), b.sprites().begin(), b.sprites().end(), sameSprite)) diff.push_back("sprites");
    auto sameLocation = [](const Location& x, const Location& y) {
        return x.left == y.left && x.top == y.top && x.right == y.right && x.bottom == y.bottom && x.nameString == y.nameString;
    };
    if (!std::equal(a.locations().begin(), a.locations().end(), b.locations().begin(), b.locations().end(), sameLocation)) diff.push_back("locations");
    if (a.strings().size() != b.strings().size()) diff.push_back("strings");
    std::string line = "save round-trip: " + narrow(path.filename().wstring()) + ": " + std::to_string(a.units().size()) + " units, " +
                       std::to_string(a.doodads().size()) + " doodads, " + std::to_string(a.sprites().size()) + " sprites, " +
                       std::to_string(a.locations().size()) + " locations, " + std::to_string(a.width()) + " x " + std::to_string(a.height()) + ": ";
    if (diff.empty()) return line + "the saved map is the same (yes)";
    for (size_t i = 0; i < diff.size(); ++i) line += (i ? ", " : "") + diff[i];
    return line + " different (WRONG)";
}

// View > Floating palettes: the palette of the mode `m` in a window of its own (on) or in the side bar (off). The
// program's (sdl_palette_float.inc).
void shellFloatPalette(Mode m, bool on);

void registerMapCommands() {
    // Mode
    addCommand(ID_MODE_TERRAIN, "mode.terrain", L"&Terrain", "Mode", L"T", [] { setMode(Mode::Terrain); });
    addCommand(ID_MODE_UNITS, "mode.units", L"&Units", "Mode", L"U", [] { setMode(Mode::Units); });
    addCommand(ID_MODE_DOODADS, "mode.doodads", L"&Doodads", "Mode", L"D", [] { setMode(Mode::Doodads); });
    addCommand(ID_MODE_SPRITES, "mode.sprites", L"&Sprites", "Mode", L"S", [] { setMode(Mode::Sprites); });
    addCommand(ID_MODE_LOCATIONS, "mode.locations", L"&Locations", "Mode", L"L", [] { setMode(Mode::Locations); });
    // View: the graphics (one of four)
    addCommand(ID_VIEW_HD2, "view.hd2", L"&High definition (2x, default)", "View", L"", [] { setGraphics(GraphicsQuality::HD2, false); },
               [] { return !shellState().carbot && shellState().quality == GraphicsQuality::HD2; }, true);
    addCommand(ID_VIEW_HD, "view.hd", L"High definition (&4x)", "View", L"", [] { setGraphics(GraphicsQuality::HD, false); },
               [] { return !shellState().carbot && shellState().quality == GraphicsQuality::HD; }, true);
    addCommand(ID_VIEW_SD, "view.sd", L"&Standard definition", "View", L"", [] { setGraphics(GraphicsQuality::SD, false); },
               [] { return !shellState().carbot && shellState().quality == GraphicsQuality::SD; }, true);
    // A graphics mode like the others: the Cartooned skin, at full (4x) resolution.
    addCommand(ID_VIEW_CARBOT, "view.carbot", L"&Carbot (cartooned)", "View", L"", [] { setGraphics(GraphicsQuality::HD, true); }, [] { return shellState().carbot; },
               true);
    // View: what shows
    addCommand(ID_VIEW_LOCATIONS, "view.locations", L"Show &locations", "View", L"", [] {
        extern int gLayerShows;
        shellState().showLocations = !shellState().showLocations;
        if (shellState().showLocations) ++gLayerShows;
    },
               [] { return shellState().showLocations; });
    addCommand(ID_VIEW_GRID, "view.grid", L"Show &grid", "View", L"G", [] { shellState().showGrid = !shellState().showGrid, shellState().dirty = true; }, [] { return shellState().showGrid; });
    addCommand(ID_VIEW_RANGES, "view.ranges", L"Show ranges of the selected u&nits", "View", L"", [] {
        gShowRanges = !gShowRanges;
        setMessage(gShowRanges ? L"Ranges of the selected units: sight (yellow), target acquisition (orange), ground weapon (red), air weapon (blue), detection (green), psi field (purple)"
                               : L"Ranges are hidden");
        shellMenusChanged();
        shellState().dirty = true;
    }, [] { return gShowRanges; });
    addCommand(ID_VIEW_MINIMAP, "view.minimap", L"Show &minimap", "View", L"", [] {
        gDock.minimapHidden = !gDock.minimapHidden;
        saveDock(), shellRelayout(), shellMenusChanged();
        setMessage(gDock.minimapHidden ? L"The minimap is hidden (View > Show minimap shows it again)" : L"The minimap shows");
    }, [] { return !gDock.minimapHidden; });
    for (const Mode m : kModeOrder)  // View > Floating palettes: an item for each mode's palette
        addCommand(ID_VIEW_FLOATPAL + int(m), kFloatPalNames[int(m)].command, kFloatPalNames[int(m)].menu, "View/Floating palettes", L"",
                   [m] { shellFloatPalette(m, !paletteFloats(m)); }, [m] { return paletteFloats(m); });
    addCommand(ID_LAYER, "view.layer.terrain", L"&Terrain", "View/Layers", L"", [] { toggleLayer(0); }, [] { return gLayers.terrain; });
    addCommand(ID_LAYER + 1, "view.layer.doodads", L"&Doodads", "View/Layers", L"", [] { toggleLayer(1); }, [] { return gLayers.doodads; });
    addCommand(ID_LAYER + 2, "view.layer.sprites", L"&Sprites", "View/Layers", L"", [] { toggleLayer(2); }, [] { return gLayers.sprites; });
    addCommand(ID_LAYER + 3, "view.layer.units", L"&Units", "View/Layers", L"", [] { toggleLayer(3); }, [] { return gLayers.units; });
    addCommand(ID_LAYER_REVEALERS, "view.layer.revealers", L"Map &revealers", "View/Layers", L"", [] { toggleRevealers(); }, [] { return gLayers.revealers; });
    addCommand(ID_LAYER + 4, "view.layer.pathing", L"&Pathing", "View/Pathing", L"", [] { pathingLayerToggle(); }, [] { return pathingLayerOn(); });
    addCommand(ID_LAYER + 5, "view.pathing.regions", L"Pathing shows the &regions", "View/Pathing", L"", [] { pathingLayerMode(false); },
               [] { return !pathingLayerAreas(); }, true);
    {
        const struct {
            const char* id;
            const wchar_t* label;
            int view;
        } flags[] = {{"view.flags.walkable", L"&Walkable ground", 1}, {"view.flags.height", L"Ground &height", 2},
                     {"view.flags.blocksview", L"Ground that blocks the line of &sight", 3}, {"view.flags.buildable", L"&Buildable ground", 4}};
        for (int k = 0; k < 4; ++k) {
            const auto v = TerrainFlagsView(flags[k].view);
            addCommand(ID_TERRAIN_FLAGS + k, flags[k].id, flags[k].label, "View/Terrain flags", L"", [v] { terrainFlagsSet(v); },
                       [v] { return terrainFlagsView() == v; }, true);
        }
    }
    addCommand(ID_FOG, "view.fog", L"&Fog of war of the selected player", "View/Fog", L"", [] { fogToggle(); }, [] { return gShowFog; });
    addCommand(ID_LAYER + 6, "view.pathing.areas", L"Pathing shows the connected &areas", "View/Pathing", L"", [] { pathingLayerMode(true); },
               [] { return pathingLayerAreas(); }, true);
    {  // Edit > Decorate the terrain (roadmap R37): doodads where they fit (Editor::decorate)
        const struct {
            const char* id;
            const wchar_t* label;
            int spacing;
        } levels[] = {{"edit.decorate.few", L"A &few doodads", 9}, {"edit.decorate.some", L"&Some doodads", 6}, {"edit.decorate.many", L"&Many doodads", 3}};
        for (int k = 0; k < 3; ++k) {
            const int spacing = levels[k].spacing;
            addCommand(ID_DECORATE + k, levels[k].id, levels[k].label, "Edit/Decorate", L"", [spacing] { decorateTerrain(spacing); });
        }
    }
    addCommand(ID_REVEAL_PLACE, "edit.revealers.place", L"Place &map revealers\x2026", "Edit/Tools", L"", [] { placeMapRevealersCommand(); });
    addCommand(ID_REVEAL_REMOVE, "edit.revealers.removeAll", L"&Remove all map revealers", "Edit/Tools", L"", [] { removeMapRevealersCommand(); });
    addCommand(ID_SCEN_PATHCHECK, "scen.checkPathing", L"Check &pathing (nooks and crannies)", "Scenario", L"", [] { pathingCheckCommand(); });
    // View: the camera. + and - are the map view's own keys (they also work with the number pad).
    addCommand(ID_VIEW_ZOOMIN, "view.zoomIn", L"Zoom &in", "View", L"+", [] {
        int w, h;
        viewSize(w, h);
        zoomAt(1.25f, w / 2, h / 2);
    }, {}, false, false);
    addCommand(ID_VIEW_ZOOMOUT, "view.zoomOut", L"Zoom &out", "View", L"-", [] {
        int w, h;
        viewSize(w, h);
        zoomAt(0.8f, w / 2, h / 2);
    }, {}, false, false);
    addCommand(ID_VIEW_ZOOM100, "view.actualSize", L"&Actual size", "View", L"Ctrl+0", [] {
        if (gWorkspace == Workspace::Triggers) return trigZoomReset();  // the trigger pane back to its normal size
        int w, h;
        viewSize(w, h);
        zoomAt(1.0f / shellState().zoom, w / 2, h / 2);
    });
    addCommand(ID_VIEW_FIT, "view.fit", L"&Fit map", "View", L"Ctrl+F", [] { fitMap(); });
}
