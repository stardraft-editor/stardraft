// The editor's palette and tool state (portable, core.inc): the device scale, the status bar's texts and the messages,
// the tool and brush of the active mode, the palette's entries and their list, and the player (owner) choice. The state
// is the app's AppState (passed in as app); the lists, combo boxes, the log and the windows are the host's (shell.h).
// Moved from main.cpp: the Win32 TU keeps the forms without app (each calls the one here with its app).

void tilesSync();  // (tiles_view.inc: the Win32 TU until it moves into the core)
void setMode(Mode m);  // (main.cpp: the mode's own state and window; the Win32 TU until it moves)
std::wstring paletteItemName(int v);  // (main.cpp: the palette entry's name; entryName is in the Win32 TU)

// Device pixels for v at the window's dpi (the rounding of MulDiv(v, dpi, 96): halves away from zero).
int scale(int v) {
    const long long p = (long long)v * shellDpi();
    return int(p >= 0 ? (p + 48) / 96 : -((-p + 48) / 96));
}

void toWorld(const AppState& app, int sx, int sy, float& wx, float& wy) {
    wx = app.camX + sx / app.zoom;
    wy = app.camY + sy / app.zoom;
}

int currentTool(const AppState& app) { return app.toolByMode[int(app.mode)]; }

Brush currentBrush(const AppState& app) { return Brush{app.brushShape, app.brushSize, app.mode == Mode::Terrain ? app.brushHeight : 0.0f}; }

// The terrain selection's shape: the brush shape (diamond, square, circle) while it is made with Select.
Editor::SelectShape selectShape(const AppState& app) {
    return app.brushShape == Brush::Diamond ? Editor::SelectShape::Diamond : app.brushShape == Brush::Circle ? Editor::SelectShape::Ellipse : Editor::SelectShape::Rect;
}

void setStatusPart(AppState& app, int i, const wchar_t* text) {
    if (app.statusText[i] == text) return;
    app.statusText[i] = text;
    shellStatusChanged();  // the bar is drawn in full (StatusView::paint)
}

void updateStatus(AppState& app) {
    if (!shellHasStatusBar()) return;
    wchar_t buf[256];
    float wx, wy;
    toWorld(app, app.mouseX, app.mouseY, wx, wy);
    std::swprintf(buf, 256, L"%s: %s", kModeNames[int(app.mode)],
                  app.mode == Mode::Terrain ? kTerrainTools[currentTool(app)] : app.mode == Mode::Units ? kUnitTools[currentTool(app)]
                  : app.mode == Mode::Doodads ? kDoodadTools[currentTool(app)] : app.mode == Mode::Sprites ? (app.palette >= 0 ? L"Place" : L"Select") : L"Edit");
    setStatusPart(app, 0, buf);
    std::swprintf(buf, 256, L"%d, %d  (tile %d, %d)", int(wx), int(wy), int(wx) / 32, int(wy) / 32);
    setStatusPart(app, 1, buf);
    std::swprintf(buf, 256, L"%.0f%%  %s", app.zoom * 100, widen(qualityName(app.quality)).c_str());
    setStatusPart(app, 2, buf);
    size_t sel = app.ed ? app.ed->selection().size() : 0;
    std::swprintf(buf, 256, L"%zu selected", sel);
    setStatusPart(app, 3, buf);
    setStatusPart(app, 4, app.message.c_str());
    const int sprites = shellSpriteCount();
    // Frames drawn in the last second (the map draws when something changes, so this is low when nothing moves).
    const auto now = std::chrono::steady_clock::now();
    while (!app.frameTimes.empty() && now - app.frameTimes.front() > std::chrono::seconds(1)) app.frameTimes.pop_front();
    std::swprintf(buf, 256, L"%zu fps  %d sprites", app.frameTimes.size(), sprites);
    setStatusPart(app, 5, buf);
}

void setMessage(AppState& app, const std::wstring& m) {
    app.message = m;
    updateStatus(app);
    shellLogLine(m);
}

// The palette list allows multi-select (for sprays); the "current" entry is the caret.
void setListSel(int i) { shellListSelect(i); }

// Compact tag from units.dat's StarEdit group flags: race (T/Z/P, N neutral, I independent) and
// kind (U unit, B building, H hero, O other), e.g. "T U" for Terran units.
std::wstring unitTag(const UnitType& t) {
    wchar_t race = (t.groupFlags & UnitGroup::Terran) ? L'T' : (t.groupFlags & UnitGroup::Zerg) ? L'Z'
                 : (t.groupFlags & UnitGroup::Protoss) ? L'P' : (t.groupFlags & UnitGroup::Independent) ? L'I' : L'N';
    wchar_t kind = t.is(UnitFlag::Hero) ? L'H' : t.is(UnitFlag::Building) ? L'B' : (t.groupFlags & UnitGroup::Men) ? L'U' : L'O';
    return std::wstring{race, L' ', kind};
}

std::wstring unitShortName(const UnitType& t) {
    std::wstring n = widen(t.name);
    for (const wchar_t* prefix : {L"Terran ", L"Zerg ", L"Protoss "})
        if (n.rfind(prefix, 0) == 0) return n.substr(wcslen(prefix));
    return n;
}

std::wstring itemLabel(const AppState& app, int v) {
    Editor& ed = *app.ed;
    switch (app.mode) {
    case Mode::Terrain: {
        const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era());
        return t ? widen(t->terrainName(uint16_t(v))) : L"";
    }
    case Mode::Units:
        return v < 0 ? L"(Select / move)" : unitTag(ed.unitTypes()[uint16_t(v)]) + L"   " + unitShortName(ed.unitTypes()[uint16_t(v)]);
    case Mode::Doodads: {
        const DoodadType* d = ed.doodadTypes().find(uint16_t(v));
        if (!d) return L"";
        return widen(ed.unitTypes().statText(d->groupString - 1u)) + L"  #" + std::to_wstring(d->id) + L"  (" +
               std::to_wstring(d->width) + L"x" + std::to_wstring(d->height) + L")";
    }
    case Mode::Locations:
        return widen(ed.locationName(v));
    case Mode::Sprites: break;
    }
    return L"";
}

// The palette's entries for the mode (its filter, the Locations search): app.items, and the rows of the list box.
void rebuildList(AppState& app) {
    std::vector<std::wstring> rows;
    app.items.clear();
    const bool open = app.ed && app.ed->isOpen();
    if (open) {
        Editor& ed = *app.ed;
        std::wstring filter = shellFilterText();
        std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);
        std::vector<int> all;
        switch (app.mode) {
        case Mode::Terrain:
            if (const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era()); t && ed.canPaintTerrain())
                for (uint16_t i = 1; i <= t->terrainCount(); ++i)
                    if (t->isTerrain(i)) all.push_back(i);
            break;
        case Mode::Units: break;  // in the unit palette (side bar)
        case Mode::Sprites: break;
        case Mode::Doodads: {
            std::vector<const DoodadType*> ds;
            for (const auto& d : ed.doodadTypes().all()) ds.push_back(&d);
            std::stable_sort(ds.begin(), ds.end(), [](auto* a, auto* b) { return a->groupString < b->groupString; });
            for (auto* d : ds) all.push_back(d->id);
            break;
        }
        case Mode::Locations:
            for (int i = 0; i < ed.locationCount(); ++i)
                if (ed.locationUsed(i)) all.push_back(i);
            break;
        }
        auto squash = [](std::wstring t) {
            t.erase(std::remove(t.begin(), t.end(), L' '), t.end());
            return t;
        };
        for (int v : all) {
            std::wstring label = itemLabel(app, v);
            std::wstring low = label;
            std::transform(low.begin(), low.end(), low.begin(), ::towlower);
            bool match = low.find(filter) != std::wstring::npos || squash(low).find(squash(filter)) != std::wstring::npos;
            if (!filter.empty() && v >= 0 && !match) continue;
            app.items.push_back(v);
            rows.push_back(label);
        }
        if (app.mode == Mode::Terrain && app.items.empty()) rows.push_back(shellTr(L"(Terrain editing needs ISOM data. Only Badlands has it now.)"));
    }
    shellListFill(rows);
    if (open) {
        // Keep the previous choice if still listed.
        if (app.mode != Mode::Units) {
            auto it = std::find(app.items.begin(), app.items.end(), app.palette);
            int sel = it != app.items.end() ? int(it - app.items.begin()) : -1;  // no pick until one is clicked: the mode selects
            if (app.mode == Mode::Locations) sel = -1;
            setListSel(sel);
            app.palette = sel >= 0 ? app.items[sel] : -1;
            if (app.mode == Mode::Locations) app.palette = -1;
        }
    }
    shellPaletteRebuilt();
}

// The tool strip (app.tool) shows the active mode's tools as icons: repaint it after the tool changes.
void fillToolCombo(AppState& app) {
    shellToolsChanged();
    // The terrain selection goes with a tool that does not use it (Brush, Spray); Fill and Replace work in it.
    if (const int t = app.toolByMode[int(Mode::Terrain)]; app.mode == Mode::Terrain && (t == TBrush || t == TSpray) && app.hasTerrainSel)
        app.hasTerrainSel = false, app.dirty = true;
    // The strip's rows follow the tool (size and density for the sprays): its height, and what is under it.
    static int lastRows = -1;
    const int rows = int(app.mode) * 4 + app.toolByMode[int(app.mode)];
    if (shellViewShown() && rows != lastRows) lastRows = rows, shellRelayout();
    tilesSync();
}

void setPlayer(AppState& app, int i) {
    app.playerOwner = i;
    shellOwnerSelect(i);
    setMessage(app, L"Player " + std::to_wstring(i + 1));
    app.dirty = true;
    shellPaletteChanged();  // sprites wear the player's color
}

// A sprite that is a doodad's own overlay goes with its doodad: not selectable in Sprites mode.
bool spriteIsOverlay(const AppState& app, int i) {
    const Editor& ed = *app.ed;
    if (i < 0 || i >= int(ed.sprites().size())) return false;
    const MapSprite& s = ed.sprites()[size_t(i)];
    for (const Doodad& d : ed.doodads())
        if (const DoodadType* t = ed.doodadTypes().find(d.id); t && t->overlaySprite == s.id && d.x == s.x && d.y == s.y) return true;
    return false;
}

// Places the palette doodad at a map point by the Options > Doodads rule: only where it fits, at the nearest place where
// it fits, or anywhere on the map (the terrain is not checked). True when it was placed.
bool placePaletteDoodad(AppState& app, float wx, float wy, std::string& why) {
    Editor& ed = *app.ed;
    const uint16_t id = uint16_t(app.palette);
    if (ed.placeDoodad(id, int(wx), int(wy), app.doodadCheck, &why)) return true;
    if (!app.doodadNearest) return false;
    const auto at = ed.nearestDoodadFit(id, int(wx), int(wy), app.doodadCheck);
    return at && ed.placeDoodad(id, at->first, at->second, app.doodadCheck, &why);
}

const std::vector<int>& paletteChosen() { return shellPaletteChosen(); }

int selectPalette(AppState& app, int value) {
    if (app.mode != Mode::Locations) {
        shellPaletteChosen().clear();
        if (value >= 0) shellPaletteChosen().push_back(value);
        app.palette = value;
        shellPaletteChanged();
        return value;
    }
    auto it = std::find(app.items.begin(), app.items.end(), value);
    if (it == app.items.end()) return -1;
    int i = int(it - app.items.begin());
    setListSel(i);
    app.palette = value;
    return i;
}

// Picking a unit means placing it: Units mode, out of paste mode, nothing selected, so the next click
// on the map places one (Spray and Replace keep their tool: they use the pick too). Ctrl adds units to
// the pick (or removes them) for sprays.
void pickUnit(AppState& app, int v, bool add) {
    if (!app.ed || v < 0) return;
    if (app.mode == Mode::Terrain || app.mode == Mode::Doodads || app.mode == Mode::Sprites) {
        // Terrain: paint with it (leaving Select / copy); doodads and sprites: place it (Ctrl adds to a spray, #79).
        if (add && app.mode == Mode::Terrain && !app.terrainRect) {  // the second terrain: the right button paints it
            app.palette2 = app.palette2 == v ? -1 : v;
            if (currentTool(app) == TSelect) app.toolByMode[int(Mode::Terrain)] = TBrush, fillToolCombo(app);
            setMessage(app, app.palette2 > 0 ? shellTr(L"Right button paints ") + paletteItemName(v) + shellTr(L"; left button paints ") +
                                              (app.palette > 0 ? paletteItemName(app.palette) : std::wstring(shellTr(L"the terrain you click"))) : L"Right button: no terrain");
            app.dirty = true;
            shellPaletteChanged();
            return;
        }
        if (add && (app.mode == Mode::Doodads || app.mode == Mode::Sprites)) {
            auto it = std::find(shellPaletteChosen().begin(), shellPaletteChosen().end(), v);
            if (it != shellPaletteChosen().end()) shellPaletteChosen().erase(it);
            else shellPaletteChosen().push_back(v);
            app.palette = shellPaletteChosen().empty() ? -1 : shellPaletteChosen().back();
        } else {
            shellPaletteChosen() = {v};
            app.palette = v;
        }
        app.terrainPasting = false;
        if (app.mode == Mode::Terrain && currentTool(app) == TSelect) {
            app.toolByMode[int(Mode::Terrain)] = TBrush;
            fillToolCombo(app);
        }
        if (app.palette >= 0)
            setMessage(app, (app.mode == Mode::Terrain ? L"Painting " : app.mode == Mode::Sprites ? L"Placing sprite " : L"Placing doodad ") + paletteItemName(app.palette) +
                       (app.mode == Mode::Doodads ? L": click to place it (green if it fits, red if it does not)" : L""));
        app.dirty = true;
        shellPaletteChanged();
        return;
    }
    if (app.mode != Mode::Units) setMode(Mode::Units);
    if (add) {
        auto it = std::find(shellPaletteChosen().begin(), shellPaletteChosen().end(), v);
        if (it != shellPaletteChosen().end()) shellPaletteChosen().erase(it);
        else shellPaletteChosen().push_back(v);
        app.palette = shellPaletteChosen().empty() ? -1 : shellPaletteChosen().back();
    } else {
        shellPaletteChosen() = {v};
        app.palette = v;
    }
    app.pasting = false;
    app.ed->clearSelection();
    if (currentTool(app) != USpray && currentTool(app) != UReplace) {
        app.toolByMode[int(Mode::Units)] = UPlace;
        fillToolCombo(app);
    }
    if (app.palette >= 0)
        setMessage(app, L"Placing " + widen(app.ed->unitTypes()[uint16_t(app.palette)].name) +
                   (shellPaletteChosen().size() > 1 ? L" and " + std::to_wstring(shellPaletteChosen().size() - 1) + L" more" : L"") +
                   L": click to place. Drag to place a row. Right-click to stop.");
    app.dirty = true;
    shellPaletteChanged();
}
