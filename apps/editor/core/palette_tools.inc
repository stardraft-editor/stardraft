// The editor's palette and tool state (portable, core.inc): the device scale, the status bar's texts and the messages,
// the tool and brush of the active mode, the palette's entries and their list, and the player (owner) choice. The state
// is the app's AppState (passed in as app); the lists, combo boxes, the log and the windows are the host's (shell.h).
// Moved from main.cpp: the Win32 TU keeps the forms without app (each calls the one here with its app).

void tilesSync();  // (tiles_view.inc: the Win32 TU until it moves into the core)
void setMode(Mode m);
// setMode's host parts (shell_win32.inc; the SDL host has no docked palettes yet): the side the mode's tools are docked
// on, with the palette's view of the mode, and the windows that the mode shows (the Map workspace's).
void shellModeSide(Mode m);
void shellModeWindows(Mode m);
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

// The Diamond keeps the shape of the map's diamonds (GitHub #138): 2 to 1 in pixels, as the ISOM cells. Its height
// follows its size (Height has no value of its own for it): in diamonds (Isometric) the height is the size; in tiles
// (Rectangle) it is half the size, rounded up. Square and Circle: their Height (0: the same as the size; GitHub #113).
bool brushHeightLocked(const AppState& app) { return app.brushShape == Brush::Diamond; }
float brushHeightOf(const AppState& app) {
    if (brushHeightLocked(app)) return app.terrainRect ? float((std::max(1L, std::lround(app.brushSize)) + 1) / 2) : app.brushSize;
    return app.brushHeight > 0 ? app.brushHeight : app.brushSize;
}

Brush currentBrush(const AppState& app) {
    return Brush{app.brushShape, app.brushSize, app.mode == Mode::Terrain && !brushHeightLocked(app) ? app.brushHeight : 0.0f};
}

// The terrain selection's shape: the brush shape (diamond, square, circle) while it is made with Select.
Editor::SelectShape selectShape(const AppState& app) {
    return app.brushShape == Brush::Diamond ? Editor::SelectShape::Diamond : app.brushShape == Brush::Circle ? Editor::SelectShape::Ellipse : Editor::SelectShape::Rect;
}

void setStatusPart(AppState& app, int i, const wchar_t* text) {
    if (app.statusText[i] == text) return;
    app.statusText[i] = text;
    shellStatusChanged();  // the bar is drawn in full (StatusView::paint)
}

std::wstring shellKeysText(const std::wstring& s);  // (the host: on macOS, the Mac's symbols of the shortcuts)
// The units under the mouse in Units mode (a stack: two or more), as the status bar counts them.
size_t unitStackCount(const AppState& app) {
    if (app.mode != Mode::Units || !app.mouseIn || !app.ed || !app.ed->isOpen() || app.palette >= 0 || app.pasting) return 0;
    float wx, wy;
    toWorld(app, app.mouseX, app.mouseY, wx, wy);
    return app.ed->unitsAt(int(wx), int(wy)).size();
}

void updateStatus(AppState& app) {
    if (!shellHasStatusBar()) return;
    wchar_t buf[256];
    float wx, wy;
    toWorld(app, app.mouseX, app.mouseY, wx, wy);
    std::swprintf(buf, 256, L"%ls: %ls", kModeNames[int(app.mode)],
                  app.mode == Mode::Terrain ? kTerrainTools[currentTool(app)] : app.mode == Mode::Units ? kUnitTools[currentTool(app)]
                  : app.mode == Mode::Doodads ? kDoodadTools[currentTool(app)] : app.mode == Mode::Sprites ? (app.palette >= 0 ? L"Place" : L"Select") : L"Edit");
    setStatusPart(app, 0, buf);
    std::swprintf(buf, 256, L"%d, %d  (tile %d, %d)", int(wx), int(wy), int(wx) / 32, int(wy) / 32);
    // Units mode: two or more units under the mouse (a stack) say how many, and how to see them.
    if (const size_t n = unitStackCount(app); n > 1) {
        wchar_t more[128];
        std::swprintf(more, 128, tr(L"%d units here (Alt+click: list)"), int(n));
        setStatusPart(app, 1, (std::wstring(buf) + L"   " + shellKeysText(more)).c_str());
    } else {
        setStatusPart(app, 1, buf);
    }
    std::swprintf(buf, 256, L"%.0f%%  %ls", app.zoom * 100, widen(qualityName(app.quality)).c_str());
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

// The program's one state: the shell's (shell_win32.inc: main.cpp's App, whose AppState part it is; shell_sdl.inc: gState).
AppState& shellState();
AppState& appState() { return shellState(); }
void setMessage(const std::wstring& m) { setMessage(appState(), m); }
void updateStatus() { updateStatus(appState()); }

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
        if (add && app.mode == Mode::Terrain && !app.terrainRect) {  // the second terrain: Shift+click or Shift+drag paints it
            app.palette2 = app.palette2 == v ? -1 : v;
            if (currentTool(app) == TSelect) app.toolByMode[int(Mode::Terrain)] = TBrush, fillToolCombo(app);
            setMessage(app, app.palette2 > 0 ? shellTr(L"Shift+drag paints ") + paletteItemName(v) + shellTr(L"; drag paints ") +
                                              (app.palette > 0 ? paletteItemName(app.palette) : std::wstring(shellTr(L"the terrain you click"))) : shellTr(L"Shift+drag: no terrain"));
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

// The tool strip's tools of a mode: its buttons, and the keys V, B, A, F (the Map workspace's strip is the host's).
struct StripItem {
    UiIcon icon;
    const wchar_t* tip;
    int tool;     // the mode's tool it sets
    bool select;  // the Select button: the tool with nothing picked
};
// Every mode's tools, Select first. Select is the mode's tool with nothing picked (Terrain has a Select tool of its
// own); Place is the same tool with a pick from the list.
std::vector<StripItem> stripTools(Mode m) {
    const AppState& app = appState();
    switch (m) {
    case Mode::Terrain:
        if (app.terrainRect)  // Rectangle: the same tools on tiles as they are
            return {{UiIcon::Select, L"Select: drag a rectangle of tiles to copy (Ctrl+C). Esc returns to Select.", TSelect, true},
                    {UiIcon::Brush, L"Brush: place the tile selected in the Tiles list, with the brush's size and shape. With several tiles selected, each tile of the brush gets one of the selected tiles at random.", TBrush, false},
                    {UiIcon::Spray, L"Spray: place the tile at random spots in the brush (Density). Hold the button to spray more.", TSpray, false},
                    {UiIcon::Fill, L"Fill: click in the selection to fill the whole selection. Click elsewhere to fill the connected tiles that match the clicked tile. Shift+click fills every matching tile (in the selection, if there is one). With several tiles selected in the Tiles list, each filled tile gets one of them at random.", TFill, false}};
        return {{UiIcon::Select, L"Select: drag a rectangle of terrain to copy (Ctrl+C) or move. Esc returns to the Select tool.", TSelect, true},
                {UiIcon::Brush, L"Brush: paint the terrain selected in the list", TBrush, false},
                {UiIcon::Spray, L"Spray: random dabs of the terrain (Density). Hold the button to spray more.", TSpray, false},
                {UiIcon::Fill, L"Fill: the connected area of one terrain. Shift+click: every place of that terrain (in the selection, if there is one).", TFill, false}};
    case Mode::Units:
        return {{UiIcon::Select, L"Select units: click or drag a box; drag them to move. Esc returns to the Select tool.", UPlace, true},
                {UiIcon::Add, L"Place the unit selected in the list", UPlace, false},
                {UiIcon::Spray, L"Spray: random units of the selected kinds (Density). Hold the button to spray more.", USpray, false}};
    case Mode::Doodads:
        return {{UiIcon::Select, L"Select a doodad: click it; drag it to move. Esc returns to the Select tool.", DPlace, true},
                {UiIcon::Brush, L"Place the doodads selected in the list", DPlace, false},
                {UiIcon::Spray, L"Spray: random doodads of the selected kinds (Density). Hold the button to spray more.", DSpray, false}};
    case Mode::Sprites:
        return {{UiIcon::Select, L"Select a sprite: click it; drag it to move. Esc goes to Select.", SPlace, true},
                {UiIcon::Add, L"Place the sprites selected in the list (several: each one at random); drag to place more", SPlace, false},
                {UiIcon::Spray, L"Spray: random sprites of the selected kinds (Density). Hold the button to spray more.", SSpray, false}};
    case Mode::Locations: return {{UiIcon::Select, L"Select, move and resize locations; drag on empty ground to make one", 0, true}};
    }
    return {};
}
// The key of a strip tool (ID_TOOL_KEY + it): 0 V select, 1 B brush or place, 2 A spray, 3 F fill; -1 none.
int stripToolKey(const StripItem& it) {
    if (it.select) return 0;
    if (it.icon == UiIcon::Spray) return 2;
    if (it.icon == UiIcon::Fill) return 3;
    if (it.icon == UiIcon::Brush || it.icon == UiIcon::Add) return 1;
    return -1;
}

// Whether the active mode is in its selection: its Select tool with nothing picked.
bool inSelection() {
    AppState& app = appState();
    for (const StripItem& it : stripTools(app.mode))
        if (it.select)
            return currentTool(app) == it.tool && (app.mode == Mode::Terrain || app.palette < 0) && !app.pasting && !app.terrainPasting && !app.pastingItems;
    return true;
}
// To the active mode's selection: its Select tool, nothing picked, no paste following the mouse (Esc, the Select
// button, entering a mode).
void enterSelection() {
    AppState& app = appState();
    for (const StripItem& it : stripTools(app.mode))
        if (it.select) app.toolByMode[int(app.mode)] = it.tool;
    app.pasting = app.terrainPasting = app.pastingItems = false;
    app.palette = -1;
    if (app.mode != Mode::Locations) shellPaletteChosen().clear(), shellPaletteChanged();
    fillToolCombo(app);
    updateStatus(app);
    app.dirty = true;
}

// Switches the mode: its own spray density and symmetry come with it, the palette shows its entries, and the mode's
// selection is on (a new mode starts in it). The host shows the mode's side and windows (shellModeSide, shellModeWindows).
void setMode(Mode m) {
    AppState& app = appState();
    const bool changed = app.mode != m;
    if (changed) app.selDoodads.clear(), app.selSprites.clear(), app.pastingItems = false;
    // The selected units are Units mode's: they show in each mode, so they are let go when another mode starts.
    if (changed && app.mode == Mode::Units && app.ed) app.ed->clearSelection();
    if (app.mode != m) app.palette = -1, app.palette2 = -1, shellPaletteChosen().clear();
    // Density and symmetry are each mode's own.
    app.densityByMode[int(app.mode)] = app.sprayDensity;
    if (app.ed) app.symmetryByMode[int(app.mode)] = app.ed->mirrors();
    app.sprayDensity = app.densityByMode[int(m)];
    if (app.ed) app.ed->setMirrors(app.symmetryByMode[int(m)]);
    app.mode = m;
    shellModeSide(m);  // the side the mode's tools are docked on, and the palette's view of the mode
    if (shellViewShown()) shellRelayout();  // the toolbar's side, and what the other column shows
    // Units, terrains and doodads are picked in the palette; in Locations the same place lists the locations.
    shellModeWindows(m);  // the filter, list, section, search and player windows show for the mode
    if (shellViewShown()) shellRelayout();
    fillToolCombo(app);
    app.terrainPasting = false;
    // A mode starts in selection: nothing picked, Terrain on Select. Picking a terrain, unit or doodad in the palette
    // starts placing it.
    if (changed) enterSelection();
    rebuildList(app);
    app.drag = Drag::None;
    app.dirty = true;
}

// The palette's selection: the picked units or terrains (Ctrl+click), else the one picked; in Locations, the rows the
// list has selected.
std::vector<int> selectedPalette() {
    AppState& app = appState();
    std::vector<int> out;
    if (app.mode != Mode::Locations) {
        for (int v : shellPaletteChosen())
            if (v >= 0) out.push_back(v);
        if (out.empty() && app.palette >= 0) out.push_back(app.palette);
        return out;
    }
    for (int i : shellListSelection())
        if (i >= 0 && i < int(app.items.size()) && app.items[i] >= 0) out.push_back(app.items[i]);
    return out;
}
