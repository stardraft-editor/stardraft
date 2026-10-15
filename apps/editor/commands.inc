// The Map workspace's commands (stardraft::Commands): the Mode menu and the map's View items, their keys and their
// check marks come from this table. Each command keeps its old Win32 id, so the top bar, the self-test and command(id)
// reach it as before. (Included into main.cpp before buildMenu: the actions it calls are all declared by then.)

// A command with its Win32 id. Labels and shortcuts are the menu's English texts (translated where the menu is made).
void addCommand(int nativeId, const char* id, const wchar_t* label, const char* menu, const wchar_t* shortcut, std::function<void()> run,
                std::function<bool()> checked = {}, bool radio = false, bool accelerator = true) {
    stardraft::Command c;
    c.id = id, c.label = narrow(label), c.menu = menu, c.shortcut = narrow(shortcut);
    c.run = std::move(run), c.checked = std::move(checked), c.radio = radio, c.accelerator = accelerator;
    gCommandIds[nativeId] = c.id;
    gCommands.add(std::move(c));
}

int nativeCommandId(const std::string& id) {
    for (const auto& [native, name] : gCommandIds)
        if (name == id) return native;
    return 0;
}

// The menu item's text: the label, then a tab and the shortcut.
std::wstring commandMenuText(const stardraft::Command& c) {
    return widen(c.label) + (c.shortcut.empty() ? L"" : L"\t" + widen(c.shortcut));
}

void appendCommand(HMENU m, const char* id) {
    if (const stardraft::Command* c = gCommands.find(id)) AppendMenuW(m, MF_STRING, UINT_PTR(nativeCommandId(id)), commandMenuText(*c).c_str());
}
void appendCommands(HMENU m, const char* menu) {
    for (const stardraft::Command* c : gCommands.inMenu(menu)) AppendMenuW(m, MF_STRING, UINT_PTR(nativeCommandId(c->id)), commandMenuText(*c).c_str());
}

// The check marks of the commands in the menu bar (a dot for one of a group).
void updateCommandMarks(HMENU m) {
    for (const stardraft::Command& c : gCommands.all()) {
        if (!c.checked) continue;
        const UINT item = UINT(nativeCommandId(c.id));
        if (c.radio) {
            MENUITEMINFOW mi{sizeof mi};
            mi.fMask = MIIM_FTYPE | MIIM_STATE;
            if (!GetMenuItemInfoW(m, item, FALSE, &mi)) continue;
            mi.fType |= MFT_RADIOCHECK;
            mi.fState = (mi.fState & ~MFS_CHECKED) | (c.isChecked() ? MFS_CHECKED : 0);
            SetMenuItemInfoW(m, item, FALSE, &mi);
        } else {
            CheckMenuItem(m, item, MF_BYCOMMAND | (c.isChecked() ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}

// The keys of the commands, as accelerator entries.
std::vector<ACCEL> commandAccelerators() {
    std::vector<ACCEL> out;
    for (const stardraft::Command& c : gCommands.all()) {
        if (!c.accelerator) continue;
        const auto k = stardraft::parseShortcut(c.shortcut);
        if (!k) continue;
        WORD vk = 0;
        if (k->key.size() == 1 && (std::isalnum(static_cast<unsigned char>(k->key[0])))) vk = WORD(k->key[0]);
        else if (k->key == ",") vk = VK_OEM_COMMA;
        else if (k->key == "Del") vk = VK_DELETE;
        else if (k->key.size() > 1 && k->key[0] == 'F') vk = WORD(VK_F1 + std::stoi(k->key.substr(1)) - 1);
        if (!vk) continue;  // (a key this shell gives in its own key handling)
        out.push_back({BYTE(FVIRTKEY | (k->ctrl ? FCONTROL : 0) | (k->shift ? FSHIFT : 0) | (k->alt ? FALT : 0)), vk, WORD(nativeCommandId(c.id))});
    }
    return out;
}

void setGraphics(GraphicsQuality q, bool carbot) {
    app.quality = q, app.carbot = carbot;
    app.renderer.setQuality(q), app.renderer.setCarbot(carbot);
    if (carbot) setMessage(L"Carbot (cartooned) graphics");
    unitPaletteRepaint();
    app.dirty = true;
}

void toggleLayer(int k) {
    bool* layer[4] = {&gLayers.terrain, &gLayers.doodads, &gLayers.sprites, &gLayers.units};
    static const wchar_t* const names[4] = {L"Terrain", L"Doodads", L"Sprites", L"Units"};
    bool& on = *layer[k];
    on = !on;
    setMessage(std::wstring(names[k]) + (on ? L" show" : L" are hidden (View > Layers)"));
    shellMenusChanged();
    app.dirty = true;
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
               [] { return !app.carbot && app.quality == GraphicsQuality::HD2; }, true);
    addCommand(ID_VIEW_HD, "view.hd", L"High definition (&4x)", "View", L"", [] { setGraphics(GraphicsQuality::HD, false); },
               [] { return !app.carbot && app.quality == GraphicsQuality::HD; }, true);
    addCommand(ID_VIEW_SD, "view.sd", L"&Standard definition", "View", L"", [] { setGraphics(GraphicsQuality::SD, false); },
               [] { return !app.carbot && app.quality == GraphicsQuality::SD; }, true);
    // A graphics mode like the others: the Cartooned skin, at full (4x) resolution.
    addCommand(ID_VIEW_CARBOT, "view.carbot", L"&Carbot (cartooned)", "View", L"", [] { setGraphics(GraphicsQuality::HD, true); }, [] { return app.carbot; },
               true);
    // View: what shows
    addCommand(ID_VIEW_LOCATIONS, "view.locations", L"Show &locations", "View", L"", [] { app.showLocations = !app.showLocations; },
               [] { return app.showLocations; });
    addCommand(ID_VIEW_GRID, "view.grid", L"Show &grid", "View", L"G", [] { app.showGrid = !app.showGrid, app.dirty = true; }, [] { return app.showGrid; });
    addCommand(ID_VIEW_RANGES, "view.ranges", L"Show ranges of the selected u&nits", "View", L"", [] {
        gShowRanges = !gShowRanges;
        setMessage(gShowRanges ? L"Ranges of the selected units: sight (yellow), target acquisition (orange), ground weapon (red), air weapon (blue)"
                               : L"Ranges are hidden");
        shellMenusChanged();
        app.dirty = true;
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
        zoomAt(1.0f / app.zoom, w / 2, h / 2);
    });
    addCommand(ID_VIEW_FIT, "view.fit", L"&Fit map", "View", L"Ctrl+F", [] { fitMap(); });
}
