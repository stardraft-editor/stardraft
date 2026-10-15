// The Map workspace's command table and its camera and save (portable, core.inc): addCommand, zoomAt and fitMap, the
// table's commands (registerMapCommands, the Mode menu and the View items' keys) and saveMapFile (File > Save). Each shell
// calls registerMapCommands once at its start. The helpers below are the shell's (commands.inc on Windows,
// sdl_pending.inc on SDL): the graphics and layer commands, the dock layout's save, the trigger pane's zoom.

void setGraphics(GraphicsQuality q, bool carbot);
void saveDock();
void trigZoomReset();

// A command with its Win32 id. Labels and shortcuts are the menu's English texts (translated where the menu is made).
void addCommand(int nativeId, const char* id, const wchar_t* label, const char* menu, const wchar_t* shortcut, std::function<void()> run,
                std::function<bool()> checked = {}, bool radio = false, bool accelerator = true) {
    stardraft::Command c;
    c.id = id, c.label = narrow(label), c.menu = menu, c.shortcut = narrow(shortcut);
    c.run = std::move(run), c.checked = std::move(checked), c.radio = radio, c.accelerator = accelerator;
    gCommandIds[nativeId] = c.id;
    gCommands.add(std::move(c));
}

void toggleLayer(int k) {
    bool* layer[4] = {&gLayers.terrain, &gLayers.doodads, &gLayers.sprites, &gLayers.units};
    static const wchar_t* const names[4] = {L"Terrain", L"Doodads", L"Sprites", L"Units"};
    bool& on = *layer[k];
    on = !on;
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

void fitMap() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    int w, h;
    viewSize(w, h);
    app.zoom = std::clamp(std::min(w / (app.ed->width() * 32.0f), h / (app.ed->height() * 32.0f)), 1.0f / 16, 4.0f);
    app.camX = (app.ed->width() * 32.0f - w / app.zoom) / 2;
    app.camY = (app.ed->height() * 32.0f - h / app.zoom) / 2;
    app.dirty = true;
}

// The save of the open map (File > Save and Save as): the Editor writes the file, then the title, the recent list and
// the message follow. False, with the error text in err, when the file could not be written.
bool saveMapFile(const fs::path& path, std::string& err) {
    AppState& app = shellState();
    if (!app.ed->save(path, err)) return false;
    updateTitle();
    if (!activeProject()) addRecent(gRecentMaps, path);
    setMessage(L"Saved " + path.filename().wstring());
    return true;
}

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
    addCommand(ID_VIEW_LOCATIONS, "view.locations", L"Show &locations", "View", L"", [] { shellState().showLocations = !shellState().showLocations; },
               [] { return shellState().showLocations; });
    addCommand(ID_VIEW_GRID, "view.grid", L"Show &grid", "View", L"G", [] { shellState().showGrid = !shellState().showGrid, shellState().dirty = true; }, [] { return shellState().showGrid; });
    addCommand(ID_VIEW_RANGES, "view.ranges", L"Show ranges of the selected u&nits", "View", L"", [] {
        gShowRanges = !gShowRanges;
        setMessage(gShowRanges ? L"Ranges of the selected units: sight (yellow), target acquisition (orange), ground weapon (red), air weapon (blue)"
                               : L"Ranges are hidden");
        shellMenusChanged();
        shellState().dirty = true;
    }, [] { return gShowRanges; });
    addCommand(ID_VIEW_MINIMAP, "view.minimap", L"Show &minimap", "View", L"", [] {
        gDock.minimapHidden = !gDock.minimapHidden;
        saveDock(), shellRelayout(), shellMenusChanged();
        setMessage(gDock.minimapHidden ? L"The minimap is hidden (View > Show minimap shows it again)" : L"The minimap shows");
    }, [] { return !gDock.minimapHidden; });
    addCommand(ID_LAYER, "view.layer.terrain", L"&Terrain", "View/Layers", L"", [] { toggleLayer(0); }, [] { return gLayers.terrain; });
    addCommand(ID_LAYER + 1, "view.layer.doodads", L"&Doodads", "View/Layers", L"", [] { toggleLayer(1); }, [] { return gLayers.doodads; });
    addCommand(ID_LAYER + 2, "view.layer.sprites", L"&Sprites", "View/Layers", L"", [] { toggleLayer(2); }, [] { return gLayers.sprites; });
    addCommand(ID_LAYER + 3, "view.layer.units", L"&Units", "View/Layers", L"", [] { toggleLayer(3); }, [] { return gLayers.units; });
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
