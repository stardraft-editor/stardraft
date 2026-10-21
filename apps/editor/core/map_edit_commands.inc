// The Map workspace's Edit and Options commands (portable, core.inc; moved from main.cpp's commandBody): copy, cut,
// paste, duplicate, delete and select all of the active mode, the snap and place rules of each kind, the terrain copy,
// View > Grid size and the town areas, the player keys (1 to 0; in Terrain mode they pick a terrain), the tool keys (V, B, A, F), the side bar's keys
// (Ctrl+1 to Ctrl+9), File > Close and Close all, the next and previous tab, and Help > Report an issue. Both programs'
// command() call commandMapEdit after commandPortable. mapMenuCheck and mapMenuGrayed are these items' checks and gray
// rules for both programs' menus, and kProgramKeys is the keys of the commands (both programs' accelerators).

// Options menu: the snap choices (the same for units, sprites and locations) and the grid of each, in px.
constexpr int kSnapGrids[] = {0, 32, 16, 8};
int snapChoice(int grid) {
    for (int i = 0; i < 4; ++i)
        if (kSnapGrids[i] == grid) return i;
    return 0;
}

// The keys of the commands, as the Windows accelerators had them (the menus show some of them too). The first key that
// matches runs: Ctrl+1 to Ctrl+9 are the side bar's items (the tab's mode: Map data for a classic map, Data for a project).
struct ProgramKey {
    const char* chord;  // "Ctrl+Shift+N", "V", "Ctrl+Tab" (Ctrl is Cmd on macOS)
    int id;
};
constexpr ProgramKey kProgramKeys[] = {
    {"Ctrl+,", ID_OPT_WINDOW},     {"Ctrl+W", ID_FILE_CLOSE},        {"Ctrl+Shift+W", ID_FILE_CLOSEALL}, {"Ctrl+Tab", ID_TAB_NEXT},
    {"Ctrl+Shift+Tab", ID_TAB_PREV}, {"Ctrl+N", ID_FILE_NEW},        {"Ctrl+Shift+N", ID_PROJ_NEW},      {"Ctrl+Shift+O", ID_QUICKOPEN},
    {"Ctrl+O", ID_FILE_OPEN},      {"Ctrl+S", ID_FILE_SAVE},         {"Ctrl+Shift+S", ID_FILE_SAVEAS},   {"Ctrl+Shift+E", ID_FILE_EXPORTIMAGE},   {"Ctrl+Z", ID_EDIT_UNDO},
    {"Ctrl+Y", ID_EDIT_REDO},      {"Ctrl+Shift+Z", ID_EDIT_REDO},   {"Ctrl+Shift+H", ID_EDIT_HISTORY},   {"Ctrl+X", ID_EDIT_CUT},            {"Ctrl+C", ID_EDIT_COPY},
    {"Ctrl+D", ID_EDIT_DUPLICATE}, {"Ctrl+V", ID_EDIT_PASTE},        {"Ctrl+A", ID_EDIT_SELECTALL},      {"V", ID_TOOL_KEY},
    {"B", ID_TOOL_KEY + 1},        {"A", ID_TOOL_KEY + 2},           {"F", ID_TOOL_KEY + 3},             {"Ctrl+1", ID_RAIL_KEY},
    {"Ctrl+2", ID_RAIL_KEY + 1},   {"Ctrl+3", ID_RAIL_KEY + 2},      {"Ctrl+4", ID_RAIL_KEY + 3},        {"Ctrl+5", ID_RAIL_KEY + 4},
    {"Ctrl+6", ID_RAIL_KEY + 5},   {"Ctrl+7", ID_RAIL_KEY + 6},      {"Ctrl+8", ID_RAIL_KEY + 7},        {"Ctrl+9", ID_RAIL_KEY + 8},
    {"Ctrl+B", ID_PROJ_COMPILE},   {"Ctrl+L", ID_VIEW_LOG},            {"F1", ID_HELP_CONTEXT},
#ifdef __APPLE__  // macOS: Cmd+Delete deletes; Cmd+` (with Shift: back) is the next tab
    {"Ctrl+Backspace", ID_EDIT_DELETE}, {"Ctrl+`", ID_TAB_NEXT},     {"Ctrl+Shift+`", ID_TAB_PREV},
#endif
    {"Q", ID_QUICKPICK},           {"1", ID_PLAYER_1},               {"2", ID_PLAYER_1 + 1},             {"3", ID_PLAYER_1 + 2},
    {"4", ID_PLAYER_1 + 3},        {"5", ID_PLAYER_1 + 4},           {"6", ID_PLAYER_1 + 5},             {"7", ID_PLAYER_1 + 6},
    {"8", ID_PLAYER_1 + 7},        {"9", ID_PLAYER_1 + 8},           {"0", ID_PLAYER_1 + 9},
};

void command(int id);
void setMessage(const std::wstring& m);
void setPlayer(AppState& app, int i);
void rebuildList(AppState& app);
void fillToolCombo(AppState& app);
void setTerrainRect(bool rect);
void stripToolByKey(int key);
void pathOverlayToggle(int id);
void pathOverlaySize(int id);
void saveDock();
void setMode(Mode m);
void renameSelectedLocation();
AppState::LocationCopy locationCopyOf(const Editor& ed, int l);
std::wstring locationsText(const Editor& ed, const std::vector<int>& locs);
void deleteSelectedLocations();
bool spriteIsOverlay(const AppState& app, int i);
bool closeDoc(int i);
void activateDoc(int i);
void openStartPage();
bool confirmDiscard();
void mapPageClose();
void forgetProject(const EudProject* pr);
int railItemId(size_t n);
std::wstring issueNewUrl();  // core/feedback_send.inc: the repository's new issue page

// Closes all the tabs (each asks to be saved first; Cancel stops it). `reopen`: each closed tab's project folder or map
// file, as it is after its save question (an untitled map saved then has its new name). False: canceled.
bool closeAllDocs(std::vector<std::wstring>* reopen = nullptr) {
    AppState& app = shellState();
    while (!gDocs.empty()) {
        int i = gActive >= 0 ? gActive : 0;
        activateDoc(i);
        if (!confirmDiscard()) return false;
        if (reopen && !gDocs[size_t(i)].page) {
            if (const auto& pr = gDocs[size_t(i)].project) reopen->push_back(pr->folder().wstring());
            else if (app.ed && !app.ed->path().empty()) reopen->push_back(app.ed->path().wstring());
        }
        mapPageClose();
        app.ed.reset();
        shellForgetAnims();
        forgetProject(gDocs[size_t(i)].project.get());
        gDocs.erase(gDocs.begin() + i);
        gActive = -1;
        if (!gDocs.empty()) activateDoc(std::min(i, int(gDocs.size()) - 1));
    }
    return true;
}

// The terrain or tiles copied last, in any map (Copy, Cut, R in Terrain mode): a paste in another map of the same tileset
// pastes them too (GitHub #146). One copy for all maps, as the system's clipboard.
struct SharedTerrainCopy {
    Editor::TerrainCopies copies;
    uint16_t era = 0;
    bool any = false;
};
SharedTerrainCopy gTerrainCopy;
extern const wchar_t* kTilesetNames[8];  // (palette_items.inc)
// After a copy (or a turn of it) in `ed`: it is the copy of all maps.
void shareTerrainCopy(const Editor& ed) { gTerrainCopy = {ed.terrainCopies(), ed.era(), true}; }
// The copy of all maps into `ed` (a map that shows, or a paste): nothing when it is of another tileset. True when the
// copy is of another tileset.
bool takeTerrainCopy(Editor& ed) {
    if (!gTerrainCopy.any) return false;
    if (gTerrainCopy.era != ed.era()) return ed.setTerrainCopies({}), true;
    ed.setTerrainCopies(gTerrainCopy.copies);
    return false;
}

// Edit > Paste in the active mode: doodads and sprites follow the mouse (click to place them), a location's copy goes at
// the mouse (in the middle of the view when the mouse is not over the map), terrain and units follow the mouse.
void pasteMapCopy(AppState& app) {
    if (app.ed && (app.mode == Mode::Doodads || app.mode == Mode::Sprites)) {  // the copy follows the mouse: click to place it
        const bool dd = app.mode == Mode::Doodads;
        if ((dd ? app.doodadClip : app.spriteClip).empty())
            return setMessage(dd ? L"Nothing copied: select doodads and press Ctrl+C" : L"Nothing copied: select sprites and press Ctrl+C");
        app.toolByMode[int(app.mode)] = 0;
        app.palette = -1, shellPaletteChosen().clear();
        app.pastingItems = true;
        shellPaletteChanged(), fillToolCombo(app);
        app.dirty = true;
        setMessage(L"Paste: click to place the copy, and again to place more. Right-click or Esc stops.");
        return;
    }
    if (app.ed && app.mode == Mode::Locations) {  // a copy of the location, at the mouse
        if (app.locationClip.empty()) return setMessage(L"Nothing copied: select a location and press Ctrl+C");
        // At the mouse; the mouse not over the map (Ctrl+V after a click in the list): in the middle of the view.
        int mx = 0, my = 0;
        if (!shellViewMouse(mx, my)) {
            int vw = 0, vh = 0;
            shellViewSize(vw, vh);
            mx = vw / 2, my = vh / 2;
        }
        float wx, wy;
        toWorld(mx, my, wx, wy);
        // The copy's middle at the mouse (its top left snapped: Options > Locations snap), each location where it was in it.
        Rect box = app.locationClip.front().rect;
        for (const auto& c : app.locationClip)
            box = {std::min(box.left, c.rect.left), std::min(box.top, c.rect.top), std::max(box.right, c.rect.right), std::max(box.bottom, c.rect.bottom)};
        const int w = box.right - box.left, hgt = box.bottom - box.top;
        const int left = int(snapRound(wx - w / 2.0f, app.locationGrid)), top = int(snapRound(wy - hgt / 2.0f, app.locationGrid));
        const int dx = left - box.left, dy = top - box.top;
        // Each whole location (its size, name, inversion and elevations); one undo step. The pasted ones are selected.
        std::vector<int> made;
        app.ed->beginStroke();
        for (const auto& c : app.locationClip) {
            const Rect r{c.rect.left + dx, c.rect.top + dy, c.rect.right + dx, c.rect.bottom + dy};
            const int i = app.ed->createLocation(r, c.inverted != 0, c.inverted, c.name + " copy", c.elevation);
            if (i < 0) break;
            made.push_back(i);
        }
        app.ed->endStroke();
        app.ed->selectLocation(-1);
        for (int i : made) app.ed->addLocationSelection(i);
        rebuildList(app);
        app.dirty = true;
        if (made.empty()) return setMessage(L"All location slots are in use");
        setMessage(L"Pasted " + locationsText(*app.ed, made) +
                   (made.size() < app.locationClip.size() ? L" (all location slots are in use: " + std::to_wstring(app.locationClip.size() - made.size()) + L" not pasted)" : L""));
        return;
    }
    if (app.ed && app.mode == Mode::Terrain) {
        // The copy of the other terrain mode (tiles copied in Rectangle, terrain in Isometric): to that mode, then paste
        // it (GitHub feedback #83: Ctrl+V did nothing).
        const bool otherTileset = takeTerrainCopy(*app.ed);  // (the copy of another map)
        if (app.terrainRect ? !app.ed->hasTileClipboard() : !app.ed->hasTerrainClipboard()) {
            if (otherTileset)
                return setMessage(L"The copied terrain is " + std::wstring(kTilesetNames[gTerrainCopy.era & 7]) +
                                  L" terrain. You can paste it only in a map of that tileset");
            if (!(app.terrainRect ? app.ed->hasTerrainClipboard() : app.ed->hasTileClipboard()))
                return setMessage(L"Nothing copied: select terrain with the Select / copy tool and press Ctrl+C");
            setTerrainRect(!app.terrainRect);
        }
        app.toolByMode[int(Mode::Terrain)] = TSelect;
        fillToolCombo(app);
        app.terrainPasting = true;
        app.dirty = true;
        setMessage(app.terrainRect ? L"Paste tiles: click to place them, and again to place more. Right-click or Esc stops."
                                   : L"Paste terrain: click to place it, and again to place more. Press R to turn it. Right-click or Esc stops.");
        return;
    }
    if (!app.ed || !app.ed->canPaste()) return;
    if (app.mode != Mode::Units) return;  // the units' copy pastes in Units
    app.pasting = true;
    app.dirty = true;
    setMessage(L"Paste " + std::to_wstring(app.ed->clipboard().size()) +
               L" units: click to place them, and again to place more. Right-click or Esc stops.");
}

// Edit > Copy in the active mode (the terrain selection, the tiles, the doodads, the sprites, the location or the units).
void copyMapSelection(AppState& app) {
    if (app.mode == Mode::Terrain && app.terrainRect) {
        const Rect& r = app.terrainSel;
        if (app.hasTerrainSel && app.ed->copyTiles(r.left / 32, r.top / 32, (r.right - 1) / 32, (r.bottom - 1) / 32, selectShape()))
            shareTerrainCopy(*app.ed), tilesStampFromCopy();  // the copy is the Brush's stamp too (Discord feedback #49)
        else
            setMessage(L"First, select an area with the Select / copy tool");
    } else if (app.mode == Mode::Terrain) {
        if (app.hasTerrainSel && app.ed->copyTerrain(app.terrainSel, app.copyUnitsWithTerrain, app.copyDoodadsWithTerrain, selectShape()))
            shareTerrainCopy(*app.ed), setMessage(L"Copied the terrain selection");
        else
            setMessage(L"First, select an area with the Select / copy tool");
    } else if (app.mode == Mode::Doodads || app.mode == Mode::Sprites) {
        const bool dd = app.mode == Mode::Doodads;
        std::vector<Editor::ItemAt> items;
        for (int i : dd ? app.selDoodads : app.selSprites) {
            if (dd && i >= 0 && i < int(app.ed->doodads().size())) {
                const Doodad& d = app.ed->doodads()[size_t(i)];
                items.push_back({d.id, d.x, d.y, d.owner});
            } else if (!dd && i >= 0 && i < int(app.ed->sprites().size())) {
                const MapSprite& sp = app.ed->sprites()[size_t(i)];
                items.push_back({sp.id, sp.x, sp.y, sp.owner});
            }
        }
        if (items.empty()) return setMessage(dd ? L"First, select doodads" : L"First, select sprites");
        // From the middle of the group: a paste puts the middle at the mouse.
        int cx = 0, cy = 0;
        for (const auto& it : items) cx += it.x, cy += it.y;
        cx /= int(items.size()), cy /= int(items.size());
        for (auto& it : items) it.x -= cx, it.y -= cy;
        (dd ? app.doodadClip : app.spriteClip) = items;
        setMessage(L"Copied " + std::to_wstring(items.size()) + (dd ? L" doodads" : L" sprites"));
    } else if (app.mode == Mode::Locations) {
        const std::vector<int> locs = app.ed->selectedLocations();
        if (locs.empty()) return setMessage(L"First, select a location");
        app.locationClip.clear();
        for (int l : locs) app.locationClip.push_back(locationCopyOf(*app.ed, l));
        setMessage(L"Copied " + locationsText(*app.ed, locs));
    } else {
        app.ed->copySelection(), setMessage(L"Copied " + std::to_wstring(app.ed->selection().size()) + L" units");
    }
}

// Edit > Cut in the active mode: its copy, then its delete (terrain: null tiles in its place).
void cutMapSelection(AppState& app) {
    if (app.mode == Mode::Units) {
        app.ed->copySelection(), app.ed->deleteSelection();
        return;
    }
    if (app.mode != Mode::Terrain) {
        command(ID_EDIT_COPY), command(ID_EDIT_DELETE);
        return;
    }
    if (!app.hasTerrainSel) return setMessage(L"First, select an area with the Select tool");
    // Terrain: copy it, then null tiles in its place (black; nothing can walk or build there).
    const Rect& r = app.terrainSel;
    const bool copied = app.terrainRect ? app.ed->copyTiles(r.left / 32, r.top / 32, (r.right - 1) / 32, (r.bottom - 1) / 32, selectShape())
                                        : app.ed->copyTerrain(r, app.copyUnitsWithTerrain, app.copyDoodadsWithTerrain, selectShape());
    if (!copied) return setMessage(L"Nothing to cut there: make the selection larger");  // (a small diamond with no corner)
    shareTerrainCopy(*app.ed);
    app.ed->beginStroke();  // the units and the tiles: one undo step
    if (app.copyUnitsWithTerrain && !app.terrainRect) {  // the copied units go too (air units: the null tiles would not remove them)
        app.ed->clearSelection();
        for (int i = 0; i < int(app.ed->units().size()); ++i)
            if (Editor::inSelectShape(r, selectShape(), app.ed->units()[size_t(i)].x, app.ed->units()[size_t(i)].y)) app.ed->select(i, true);
        app.ed->deleteSelection();
    }
    std::vector<std::pair<int, int>> cells;  // the tiles whose middle is in the selection's shape
    for (int ty = r.top / 32; ty <= (r.bottom - 1) / 32; ++ty)
        for (int tx = r.left / 32; tx <= (r.right - 1) / 32; ++tx)
            if (Editor::inSelectShape(r, selectShape(), tx * 32 + 16, ty * 32 + 16)) cells.push_back({tx, ty});
    const int n = app.ed->placeTileCells(cells, 0);  // tile 0: the null tile
    app.ed->endStroke();
    app.ed->takeUnitsRemovedByTerrain();  // (the next paint's message counts only what it removes)
    setMessage(L"Cut the terrain: " + std::to_wstring(n) + L" null tiles in its place (one undo step)");
    app.dirty = true;
}

// The commands above. True when `id` is one of them (it ran); the menus follow and the map is drawn again then.
bool commandMapEdit(AppState& app, int id) {
    const bool open = app.ed && app.ed->isOpen();
    auto done = [&app] {
        shellMenusChanged();
        app.dirty = true;
        return true;
    };
    if (id >= ID_PLAYER_1 && id < ID_PLAYER_1 + 10) {
        // While placing, the key picks the owner of what you are about to place. Otherwise, with units
        // selected (Units mode), it gives them to that player and nothing else changes. With neither, it
        // picks the player for the next placement.
        const int player = id - ID_PLAYER_1;
        // Terrain mode: the key picks the palette's terrain at that place (1 the first, 0 the tenth; the headers do not
        // count), as a click on its row does.
        if (app.ed && app.mode == Mode::Terrain) {
            int nth = 0;
            for (size_t i = 0; i < app.items.size(); ++i)
                if (app.items[i] >= 0 && nth++ == player) {
                    pickUnit(app, app.items[i], false);
                    setListSel(int(i));
                    shellPaletteReveal(app.items[i]);
                    return true;
                }
            return true;  // (fewer terrains listed: the key does nothing)
        }
        const bool placing = app.palette >= 0;
        if (app.ed && app.mode == Mode::Units && !placing && !app.ed->selection().empty()) {
            const size_t selected = app.ed->selection().size();
            const int n = app.ed->setSelectionOwner(uint8_t(player));
            setMessage(L"Gave " + std::to_wstring(n) + L" of " + std::to_wstring(selected) + L" selected units to Player " +
                       std::to_wstring(player + 1) + (n < int(selected) ? L" (resources and critters stay neutral)" : L""));
            return true;
        }
        setPlayer(app, player);
        return true;
    }
    // Options menu: the snap of units, sprites or locations (each a grid choice).
    if (id >= ID_SNAP_UNITS && id < ID_SNAP_UNITS + 12) {
        const int kind = (id - ID_SNAP_UNITS) / 4, choice = (id - ID_SNAP_UNITS) % 4;
        (kind == 0 ? app.unitGrid : kind == 1 ? app.spriteGrid : app.locationGrid) = kSnapGrids[choice];
        static const wchar_t* kinds[] = {L"Units", L"Sprites", L"Locations"};
        static const wchar_t* snaps[] = {L"do not snap", L"snap to tiles (32 px)", L"snap to half tiles (16 px)", L"snap to quarter tiles (8 px)"};
        setMessage(std::wstring(kinds[kind]) + L": " + snaps[choice]);
        return done();
    }
    if (id >= ID_GRID_SIZE && id < ID_GRID_SIZE + int(std::size(kGridPresets))) {  // View > Grid size: a preset
        const GridPreset& g = kGridPresets[id - ID_GRID_SIZE];
        gDock.gridW = gDock.gridH = g.px, gDock.gridX = gDock.gridY = 0;
        app.showGrid = true, app.dirty = true;
        saveDock();
        setMessage(L"Grid: " + std::wstring(g.name) + L", " + std::to_wstring(g.px) + L" pixels");
        return done();
    }
    if (id >= ID_RAIL_KEY && id < ID_RAIL_KEY + 9) {
        // The side bar's items as it shows them (a classic map: Map, Map data, Map triggers, ...; a project: Map, Data,
        // Triggers, ...), so Ctrl+n goes to the n-th, in each mode.
        shellRailUpdateNow();  // (the items of this tab)
        if (const int item = railItemId(size_t(id - ID_RAIL_KEY))) command(item);
        return true;
    }
    switch (id) {
    case ID_FILE_CLOSE: closeDoc(gActive); break;
    case ID_FILE_CLOSEALL:
        if (closeAllDocs()) openStartPage();  // always keep one tab open: the start page
        break;
    case ID_TAB_NEXT:
    case ID_TAB_PREV:
        if (gDocs.size() > 1) activateDoc((gActive + (id == ID_TAB_NEXT ? 1 : int(gDocs.size()) - 1)) % int(gDocs.size()));
        break;
    case ID_HELP_ISSUE: shellOpenUrl(issueNewUrl()); return true;
    case ID_EDIT_COPY:
        if (open) copyMapSelection(app);
        break;
    case ID_EDIT_TERRAINUNITS: app.copyUnitsWithTerrain = !app.copyUnitsWithTerrain; break;
    case ID_OPT_LOCWRAP: app.locationWrap = !app.locationWrap, app.dirty = true; break;
    case ID_EDIT_TERRAINDOODADS: app.copyDoodadsWithTerrain = !app.copyDoodadsWithTerrain; break;
    case ID_EDIT_CUT:  // the mode's copy, then its delete
        if (open) cutMapSelection(app);
        break;
    case ID_EDIT_PASTE: pasteMapCopy(app); break;
    case ID_EDIT_DUPLICATE:
        // The active mode's copy, then its paste (the copy follows the mouse; locations: at the mouse).
        if (open && app.mode != Mode::Units && app.mode != Mode::Terrain) {
            command(ID_EDIT_COPY), pasteMapCopy(app);
            break;
        }
        if (!open || app.ed->selection().empty()) {
            setMessage(L"Select units to duplicate");
            break;
        }
        setMode(Mode::Units);
        app.ed->copySelection();
        pasteMapCopy(app);
        break;
    case ID_EDIT_DELETE:
        if (!open) break;
        if (app.mode == Mode::Locations) deleteSelectedLocations();
        else if (app.mode == Mode::Sprites) {
            if (app.ed->deleteSprites(app.selSprites)) setMessage(L"Deleted " + std::to_wstring(app.selSprites.size()) + L" sprites");
            app.selSprites.clear();
        } else if (app.mode == Mode::Doodads) {
            if (app.ed->deleteDoodads(app.selDoodads)) setMessage(L"Deleted " + std::to_wstring(app.selDoodads.size()) + L" doodads");
            app.selDoodads.clear();
        } else app.ed->deleteSelection();
        break;
    case ID_EDIT_SELECTALL:  // the active mode's things
        if (!open) break;
        if (app.mode == Mode::Doodads) {
            app.selDoodads.clear();
            for (int i = 0; i < int(app.ed->doodads().size()); ++i) app.selDoodads.push_back(i);
            setMessage(L"Selected " + std::to_wstring(app.selDoodads.size()) + L" doodads");
        } else if (app.mode == Mode::Sprites) {
            app.selSprites.clear();
            for (int i = 0; i < int(app.ed->sprites().size()); ++i)
                if (!spriteIsOverlay(app, i)) app.selSprites.push_back(i);
            setMessage(L"Selected " + std::to_wstring(app.selSprites.size()) + L" sprites");
        } else if (app.mode == Mode::Units) app.ed->selectAll();
        else if (app.mode == Mode::Locations) {
            app.ed->selectLocation(-1);
            for (int i = 0; i < app.ed->locationCount(); ++i)
                if (i != kAnywhereLocation) app.ed->addLocationSelection(i);
            setMessage(L"Selected " + std::to_wstring(app.ed->selectedLocations().size()) + L" locations");
        }
        break;
    case ID_EDIT_CORRECTION: app.correction = !app.correction; break;
    case ID_OPT_DRAGBACK: app.dragSnapBack = !app.dragSnapBack; break;
    case ID_OPT_SNAPBUILDINGS:  // buildings on tile edges, or where they are placed (the unit snap is for the other units)
        app.snapBuildings = !app.snapBuildings;
        setBuildingSnap(app.snapBuildings);
        setMessage(app.snapBuildings ? L"Buildings: snap to tile edges" : L"Buildings: do not snap");
        break;
    case ID_OPT_RESDIST:
        app.resourceDistance = !app.resourceDistance;
        for (auto& d : gDocs)
            if (d.ed) d.ed->setResourceDistance(app.resourceDistance);
        if (app.ed) app.ed->setResourceDistance(app.resourceDistance);
        setMessage(app.resourceDistance ? L"Town halls and resources keep 3 tiles apart" : L"Town halls and resources can be near each other");
        break;
    case ID_OPT_STACK:
        app.allowStacking = !app.allowStacking;
        for (auto& d : gDocs)
            if (d.ed) d.ed->setAllowStacking(app.allowStacking);
        if (app.ed) app.ed->setAllowStacking(app.allowStacking);
        setMessage(app.allowStacking ? L"Units: can be on top of others" : L"Units: cannot be on top of others (air units also)");
        break;
    case ID_OPT_ANIMATE:
        app.animate = !app.animate;
        shellForgetAnims();
        app.dirty = true;
        break;
    case ID_OPT_SNAPVALID:
    case ID_OPT_STRICT:
    case ID_OPT_ILLEGAL:
        app.correction = id == ID_OPT_SNAPVALID;
        app.allowIllegalUnits = id == ID_OPT_ILLEGAL;
        for (auto& d : gDocs)
            if (d.ed) d.ed->setAllowIllegalUnits(app.allowIllegalUnits);
        if (app.ed) app.ed->setAllowIllegalUnits(app.allowIllegalUnits);
        setMessage(id == ID_OPT_ILLEGAL ? L"Units: place anywhere (terrain edits do not move them)"
                   : id == ID_OPT_STRICT ? L"Units: place only where it fits"
                                         : L"Units: move to the nearest place where it fits");
        break;
    case ID_EDIT_DOODADCHECK: app.doodadCheck = !app.doodadCheck, app.doodadNearest = false; break;
    case ID_EDIT_SNAPLOC: command(ID_SNAP_LOCATIONS + (app.locationGrid ? 0 : 1)); return true;  // on and off (a tile)
    case ID_EDIT_SNAPUNITS: command(ID_SNAP_UNITS + (app.unitGrid ? 0 : 1)); return true;
    case ID_DOODAD_FIT:
    case ID_DOODAD_NEAREST:
    case ID_DOODAD_ANYWHERE:
        app.doodadCheck = id != ID_DOODAD_ANYWHERE, app.doodadNearest = id == ID_DOODAD_NEAREST;
        setMessage(id == ID_DOODAD_ANYWHERE ? L"Doodads: place anywhere on the map (StarDraft 2 does not check the terrain)"
                   : id == ID_DOODAD_NEAREST ? L"Doodads: move to the nearest place where it fits"
                                             : L"Doodads: place only where it fits");
        break;
    case ID_TOOL_KEY:
    case ID_TOOL_KEY + 1:
    case ID_TOOL_KEY + 2:
    case ID_TOOL_KEY + 3: stripToolByKey(id - ID_TOOL_KEY); break;
    case ID_GRID_CUSTOM: {  // the grid's width, height and offset, in map pixels
        std::wstring text = std::to_wstring(gDock.gridW) + L", " + std::to_wstring(gDock.gridH) + L", " + std::to_wstring(gDock.gridX) + L", " +
                            std::to_wstring(gDock.gridY);
        if (!shellPromptText(L"Grid: width, height, x offset, y offset (map pixels)", text)) break;
        int v[4] = {gDock.gridW, gDock.gridH, gDock.gridX, gDock.gridY}, n = 0;
        for (const wchar_t* c = text.c_str(); *c && n < 4;) {
            if (iswdigit(*c)) {
                v[n++] = int(std::wcstol(c, nullptr, 10));
                while (iswdigit(*c)) ++c;
            } else ++c;
        }
        if (n < 2) {
            setMessage(L"Type the width and the height (and the offsets), for example 48, 48, 16, 0");
            break;
        }
        gDock.gridW = std::clamp(v[0], 1, 8192), gDock.gridH = std::clamp(v[1], 1, 8192);
        gDock.gridX = n > 2 ? std::clamp(v[2], 0, 8191) : 0, gDock.gridY = n > 3 ? std::clamp(v[3], 0, 8191) : 0;
        app.showGrid = true, app.dirty = true;
        saveDock();
        setMessage(L"Grid: " + std::to_wstring(gDock.gridW) + L" x " + std::to_wstring(gDock.gridH) + L" pixels, offset " + std::to_wstring(gDock.gridX) +
                   L", " + std::to_wstring(gDock.gridY));
        break;
    }
    case ID_VIEW_WALK:
    case ID_VIEW_PATHS:
    case ID_VIEW_TOWNS: pathOverlayToggle(id); break;
    case ID_PATHSIZE_SMALL:
    case ID_PATHSIZE_MEDIUM:
    case ID_PATHSIZE_LARGE: pathOverlaySize(id); break;
    default: return false;
    }
    return done();
}

// The check of one of these menu items now: 1 checked, 0 not, -1 the item has no check here (the Options menu's snaps
// and rules, the terrain copy, the names in locations, the grid's size, the pathing overlays).
int mapMenuCheck(int id) {
    const AppState& app = shellState();
    auto snap = [](int first, int grid) { return first + snapChoice(grid); };
    if (id >= ID_SNAP_UNITS && id < ID_SNAP_UNITS + 4) return id == snap(ID_SNAP_UNITS, app.unitGrid);
    if (id >= ID_SNAP_SPRITES && id < ID_SNAP_SPRITES + 4) return id == snap(ID_SNAP_SPRITES, app.spriteGrid);
    if (id >= ID_SNAP_LOCATIONS && id < ID_SNAP_LOCATIONS + 4) return id == snap(ID_SNAP_LOCATIONS, app.locationGrid);
    bool preset = false;
    for (int k = 0; k < int(std::size(kGridPresets)); ++k) {
        const bool on = gDock.gridW == kGridPresets[k].px && gDock.gridH == kGridPresets[k].px && !gDock.gridX && !gDock.gridY;
        if (id == ID_GRID_SIZE + k) return on;
        preset |= on;
    }
    switch (id) {
    case ID_GRID_CUSTOM: return !preset;
    case ID_OPT_STRICT: return !app.allowIllegalUnits && !app.correction;
    case ID_OPT_SNAPVALID: return !app.allowIllegalUnits && app.correction;
    case ID_OPT_ILLEGAL: return app.allowIllegalUnits;
    case ID_DOODAD_FIT: return app.doodadCheck && !app.doodadNearest;
    case ID_DOODAD_NEAREST: return app.doodadCheck && app.doodadNearest;
    case ID_DOODAD_ANYWHERE: return !app.doodadCheck;
    case ID_OPT_STACK: return app.allowStacking;
    case ID_OPT_RESDIST: return app.resourceDistance;
    case ID_OPT_SNAPBUILDINGS: return app.snapBuildings;
    case ID_OPT_DRAGBACK: return app.dragSnapBack && !app.allowIllegalUnits;
    case ID_EDIT_TERRAINUNITS: return app.copyUnitsWithTerrain;
    case ID_OPT_LOCWRAP: return app.locationWrap;
    case ID_EDIT_TERRAINDOODADS: return app.copyDoodadsWithTerrain;
    case ID_VIEW_WALK: return gPath.walk;
    case ID_VIEW_PATHS: return gPath.paths;
    case ID_VIEW_TOWNS: return gPath.towns;
    case ID_PATHSIZE_SMALL: return gPath.size == PathUnitSize::Small;
    case ID_PATHSIZE_MEDIUM: return gPath.size == PathUnitSize::Medium;
    case ID_PATHSIZE_LARGE: return gPath.size == PathUnitSize::Large;
    default: return -1;
    }
}

// The items that the state grays now (their keys do nothing then): Undo and Redo with no step, Paste with nothing copied
// (Triggers, Briefing, Data and Map data paste their own copies), and the drag back of units when units go anywhere.
bool trigCanUndo(bool redo);  // core/views_triggers_pane.inc
bool dataCanUndo(bool redo);  // core/data_model.inc
bool mapMenuGrayed(int id) {
    const AppState& app = shellState();
    if (id == ID_EDIT_UNDO || id == ID_EDIT_REDO) {  // a step to undo (or redo) in the workspace that shows
        const bool redo = id == ID_EDIT_REDO;
        if (gWorkspace == Workspace::Triggers) return !trigCanUndo(redo);
        if (gWorkspace == Workspace::Data) return !dataCanUndo(redo);
        return !(app.ed && app.ed->isOpen() && (redo ? app.ed->canRedo() : app.ed->canUndo()));
    }
    if (id == ID_EDIT_HISTORY) return mapMenuGrayed(ID_EDIT_UNDO) && mapMenuGrayed(ID_EDIT_REDO);  // (no step either way)
    if (id == ID_EDIT_PASTE) {
        if (gWorkspace == Workspace::Triggers || gWorkspace == Workspace::Briefing || gWorkspace == Workspace::Data || gWorkspace == Workspace::ClassicSettings)
            return false;
        const bool open = app.ed && app.ed->isOpen();
        // Every kind of copy (a gray item also stops Ctrl+V): units, terrain, tiles, locations, doodads, sprites (#83).
        return !(open && (app.ed->canPaste() || app.ed->hasTerrainClipboard() || app.ed->hasTileClipboard() || (gTerrainCopy.any && app.mode == Mode::Terrain) || !app.locationClip.empty() ||
                          !app.doodadClip.empty() || !app.spriteClip.empty()));
    }
    if (id == ID_OPT_DRAGBACK) return app.allowIllegalUnits;  // (not used when units go anywhere)
    return false;
}

// L.4: a project made from a classic map keeps its classic data in the map. Does the project's map have classic
// triggers (any trigger in TRIG), or classic settings (a unit, upgrade or tech that does not use the game's values, or
// players with their own values)? Read again only when the map changed.
bool classicSettingsUsed(const Editor& ed) {
    const SettingDefaults* def = ed.settingDefaults();
    if (!def) return false;
    auto own = [](const std::array<bool, kSettingPlayers>& usesDefault) {
        return std::any_of(usesDefault.begin(), usesDefault.end(), [](bool b) { return !b; });
    };
    const UnitSettings u = ed.unitSettings();
    for (int i = 0; i < kSettingUnits; ++i)
        if (!u.units[size_t(i)].useDefault || !u.availability[size_t(i)].global || own(u.availability[size_t(i)].usesDefault)) return true;
    const UpgradeSettings g = ed.upgradeSettings();
    for (int i = 0; i < kSettingUpgrades; ++i)
        if (!g.upgrades[size_t(i)].useDefault || own(g.levels[size_t(i)].usesDefault)) return true;
    const TechSettings t = ed.techSettings();
    for (int i = 0; i < kSettingTechs; ++i)
        if (!t.techs[size_t(i)].useDefault || own(t.players[size_t(i)].usesDefault)) return true;
    return false;
}

bool classicCommandInProject(int id) {
    static const Editor* ed = nullptr;
    static uint64_t revision = 0;
    static bool triggers = false, settings = false;
    const AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return false;
    if (ed != app.ed.get() || revision != app.ed->revision()) {
        ed = app.ed.get(), revision = app.ed->revision();
        triggers = !app.ed->classicTriggers().triggers.empty();
        settings = classicSettingsUsed(*app.ed);
    }
    return id == ID_SCEN_CLASSICTRIG ? triggers : id == ID_SCEN_CLASSIC ? settings : false;
}
