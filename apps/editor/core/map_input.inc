// The map view's mouse input (the editor's portable core): a click, a drag, a move and a release on the map, with the
// spray held down and the pipette. The program's map view (sdl_main_view.inc) only translates its events into these
// calls. Included by core.inc; the program defines the helpers declared below.

// Defined by the program (main_sdl.cpp and its sdl_*.inc files), until they move into the core themselves.
bool objectSprayOn();
int mapSpriteAt(Editor& ed, int x, int y);
void snapUnitPoint(float& x, float& y);
int applyUnitLimit(Editor& ed);
bool atUnitLimit(const Editor& ed);
std::wstring unitLimitText(const Editor& ed);
bool trigUnitPickClick(float wx, float wy);
int sprayAt(float wx, float wy);
void tilesMouseMove(float wx, float wy);
bool mapLayersEraseOn(AppState& app);  // (core/map_layers.inc)
void mapLayersSync();
void mapLayersLiveStroke(AppState& app);
struct LayersLiveAfter {  // (when the mouse's event ends: what it painted is under the layers over it)
    AppState& app;
    ~LayersLiveAfter() { mapLayersLiveStroke(app); }
};
void moveSelectedTerrain(AppState& app, int dx, int dy);  // (core/map_edit_commands.inc)
bool mapLayersOnLayer();
bool mapLayersActiveRefuses();
std::vector<std::pair<float, float>> gLassoPoints;  // Lasso: the line drawn so far
bool gLassoAdd = false, gLassoRemove = false;      // (Shift, Ctrl at its press)
int gTerrainMoveDX = 0, gTerrainMoveDY = 0;  // Terrain's Move: the drag so far
void mapLayersDragCancel(AppState& app);
void mapLayersEraseDown(AppState& app, float wx, float wy);
void mapLayersMoveDown(AppState& app, float wx, float wy);
void mapLayersDragMove(AppState& app, float wx, float wy);
void mapLayersDragUp(AppState& app);
void tilesMouseUp(float wx, float wy);
bool tilesMouseDown(float wx, float wy, unsigned mods);
void tilesPick(int value, bool fromMap);
void tilesReveal(int value);
void tilesFill(int tx, int ty, bool every);
void pathOverlayMouseMove(float wx, float wy);
void pathingLayerMouseMove(float wx, float wy);  // (core/pathing_layer.inc)
Rect previewLocationRect(float wx, float wy);
RECT strokeBox(float x, float y);
void strokeMove(float wx, float wy);
void setListSel(int i);
void showUnitProperties(int index);
void showLocationEdit(int loc);
bool inSelection();
int sprayRadius();
int brushSizeIndex(float size);
const std::vector<float>& brushSizes();
void dragMoveDelta(float wx, float wy, float& dx, float& dy);
Rect terrainSelectRect(float wx, float wy);
bool moveSelectedDoodads(int dx, int dy, std::string& why);
int locationEdgesAt(float wx, float wy);
AppState::LocationCopy locationCopyOf(const Editor& ed, int l);
void moveSelectedLocations(int dx, int dy);
bool placePaletteUnit(float wx, float wy, bool correct);
bool strokePlace(float x, float y);
std::wstring entryName(const std::string& dat, int v);
std::wstring unitShortName(const UnitType& t);
// The palette's picked values (Ctrl+click): a doodad or sprite drag places one of them at random (the palette's own list).

// The view stays near the map: at most half a view past each edge; a map smaller than the view keeps its middle in
// the view's middle on that axis. Every way the camera moves (drag, wheel, minimap, keys) passes here each frame.
bool mapLayersOutsideBox(const Editor& ed, int& x0, int& y0, int& x1, int& y1);  // (map_layers.inc)
void clampCamera() {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen() || app.zoom <= 0) return;
    int viewW = 0, viewH = 0;
    shellViewSize(viewW, viewH);
    const float vw = viewW / app.zoom, vh = viewH / app.zoom;
    // (the map, and the map layers' terrain outside of it: the view can go to it)
    float lx = 0, ly = 0, hx = app.ed->width() * 32.0f, hy = app.ed->height() * 32.0f;
    if (int ox0, oy0, ox1, oy1; mapLayersOutsideBox(*app.ed, ox0, oy0, ox1, oy1))
        lx = std::min(lx, ox0 * 32.0f), ly = std::min(ly, oy0 * 32.0f), hx = std::max(hx, ox1 * 32.0f), hy = std::max(hy, oy1 * 32.0f);
    auto clampAxis = [](float& cam, float view, float lo, float hi) {
        if (hi - lo < view) {  // all of it fits: its middle stays inside the view
            cam = std::clamp(cam, (lo + hi) / 2 - view, (lo + hi) / 2);
            return;
        }
        cam = std::clamp(cam, lo - view / 2, hi - view / 2);
    };
    clampAxis(app.camX, vw, lx, hx);
    clampAxis(app.camY, vh, ly, hy);
}

// Options > Middle mouse click and hold > Pan map (both programs): while the middle button is held, the camera moves
// toward the mouse, faster the farther the mouse is from where the button went down. (dx, dy): the mouse from that
// point, in screen pixels; dt: the seconds since the last step (at most 0.1 count). The 8 pixels around the point do
// not move the camera. True: the camera moved.
bool middlePanStep(AppState& app, float dx, float dy, float dt) {
    const float len = std::sqrt(dx * dx + dy * dy), dead = 8.0f;
    if (len <= dead || app.zoom <= 0) return false;
    const float speed = (len - dead) * 6.0f;  // screen pixels per second
    dt = std::min(0.1f, std::max(0.0f, dt));
    app.camX += dx / len * speed * dt / app.zoom;
    app.camY += dy / len * speed * dt / app.zoom;
    app.dirty = true;
    return true;
}

// A fit asked for while the map view had no size yet (a map opened before the window exists): the host fits the map
// when the view has its size.
bool gFitPending = false;
// The same for a unit to put in the middle of the view (unitsListCenter; the creep check's picture); -1: none.
int gCenterUnitPending = -1;

// The whole map in the view: the zoom that fits it (at most 4 times), its middle in the view's middle.
void fitMap() {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    int w, h;
    viewSize(w, h);
    gFitPending = w <= 0 || h <= 0;
    if (gFitPending) return;
    app.zoom = std::clamp(std::min(w / (app.ed->width() * 32.0f), h / (app.ed->height() * 32.0f)), 1.0f / 16, 4.0f);
    app.camX = (app.ed->width() * 32.0f - w / app.zoom) / 2;
    app.camY = (app.ed->height() * 32.0f - h / app.zoom) / 2;
    app.dirty = true;
}

void selectItemAt(AppState& app, std::vector<int>& sel, int hit, unsigned mods, Drag move) {
    const bool add = (mods & (draw::kCtrl | draw::kShift)) != 0;
    if (hit < 0) {
        if (!add) sel.clear();
        app.drag = Drag::BoxSelect;
        return;
    }
    const auto it = std::find(sel.begin(), sel.end(), hit);
    if (add) {
        if (it != sel.end()) {
            sel.erase(it);
            return;
        }
        sel.push_back(hit);
    } else if (it == sel.end()) {
        sel = {hit};
    }
    app.drag = move;
    setMessage(app, std::to_wstring(sel.size()) + L" selected: drag to move, Delete removes, Ctrl+C copies");
}

// A held spray, as MS Paint's spray can: a click sprays once; while the button stays down it sprays again and again,
// slowly at first (4 sprays a second), then faster, up to 20 a second after 1.5 seconds. The view's timer calls
// sprayHoldTick with the time since the last call; the self-test calls it with its own times. The press and hold
// is one undo step (the stroke that mouseDown starts and mouseUp ends).
constexpr UINT_PTR kSprayTimer = 78;
float gRerollX = -1e9f, gRerollY = -1e9f;  // where the brush last redrew its own terrain (new variants) in this stroke
float gPaintX = 0, gPaintY = 0;             // where the brush last painted in this stroke (a fast move paints the way between)
std::vector<std::pair<int, int>> gPaintCorners;  // the ISOM corners the brush last painted (a move that keeps them paints nothing)
uint16_t gPaintTerrain = 0;                      // and the terrain it painted there
// The ISOM corners that the brush covers at the map point (wx, wy), at each symmetric position: the brush's place.
std::vector<std::pair<int, int>> brushPlace(const Editor& ed, float wx, float wy, const Brush& brush) {
    std::vector<std::pair<int, int>> corners;
    for (auto [x, y] : ed.terrainSymmetricPoints(int(wx), int(wy), brush))
        for (auto c : ed.brushCorners(x, y, brush)) corners.push_back(c);
    return corners;
}
// Jumble at the map point (wx, wy): the terrains under the brush mixed at random (Editor::jumbleTerrain).
void jumbleAt(AppState& app, float wx, float wy) {
    const int n = app.ed->jumbleTerrain(int(wx), int(wy), currentBrush(app), app.spraySeed++);
    if (n > 0) setMessage(app, L"Jumbled the terrain");
    else if (n < 0) setMessage(app, L"Did not jumble: the brush has only one terrain under it");
}
std::chrono::steady_clock::time_point gSprayTick;
void sprayHoldStart(AppState& app, int sx, int sy) {
    app.sprayHeld = 0, app.sprayCredit = 0;
    app.mouseX = sx, app.mouseY = sy;
    gSprayTick = std::chrono::steady_clock::now();
    shellSprayTimer(true);
}
void sprayHoldStop() {
    shellSprayTimer(false);
}
void sprayHoldTick(AppState& app, double dt) {
    if (app.drag != Drag::Spray || !app.ed) return;
    app.sprayHeld += dt;
    app.sprayCredit += (4.0 + 16.0 * std::min(1.0, app.sprayHeld / 1.5)) * dt;
    float wx, wy;
    toWorld(app, app.mouseX, app.mouseY, wx, wy);
    for (int k = 0; app.sprayCredit >= 1 && k < 8; ++k) {  // a slow frame does not spray a burst
        app.sprayCredit -= 1;
        app.sprayPlaced += sprayAt(wx, wy);
    }
    app.sprayCredit = std::min(app.sprayCredit, 1.0);
    app.dirty = true;
}
void sprayHoldTimer(AppState& app) {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::min(0.1, std::chrono::duration<double>(now - gSprayTick).count());
    gSprayTick = now;
    if (app.drag != Drag::Spray) return sprayHoldStop();
    sprayHoldTick(app, dt);
}

void tilesPick(int value, bool fromMap);
void tilesReveal(int value);
// The pipette (Alt+click on the map, feedback #59): what is under the mouse becomes the pick. Terrain: its terrain
// (Rectangle: its tile); units and sprites: the kind and the player; doodads: the doodad; locations: a copy to paste
// (Ctrl+V). Returns true when it took something.
bool pipette(AppState& app, float wx, float wy) {
    Editor& ed = *app.ed;
    switch (app.mode) {
    case Mode::Terrain:
        if (app.terrainRect && !gTiles->solid) {  // (Solid: its terrain, as Isometric's)
            if (gTiles->stamps) return false;  // (Custom: the list has no tiles to pick)
            const int tx = int(wx) / 32, ty = int(wy) / 32;
            if (tx < 0 || ty < 0 || tx >= ed.width() || ty >= ed.height()) return false;
            const int v = ed.terrain().tiles[size_t(ty) * ed.width() + tx];
            tilesReveal(v), tilesPick(v, true);
            return true;
        }
        if (const uint16_t t = ed.terrainAt(int(wx), int(wy))) return pickUnit(app, t, false), true;
        return false;
    case Mode::Units:
        if (const int u = ed.unitAt(int(wx), int(wy)); u >= 0) {
            const Unit& un = ed.units()[size_t(u)];
            if (un.owner < ownerChoices()) setPlayer(app, un.owner);
            pickUnit(app, un.type, false);
            return true;
        }
        return false;
    case Mode::Doodads:
        if (const int d = ed.doodadAt(int(wx), int(wy)); d >= 0) return pickUnit(app, ed.doodads()[size_t(d)].id, false), true;
        return false;
    case Mode::Sprites:
        if (const int i = mapSpriteAt(ed, int(wx), int(wy)); i >= 0 && (ed.sprites()[size_t(i)].flags & SpriteFlag::DrawAsSprite)) {
            const MapSprite& m = ed.sprites()[size_t(i)];
            if (m.owner < 12) setPlayer(app, m.owner);
            pickUnit(app, m.id, false);
            return true;
        }
        return false;
    case Mode::Locations:
        if (const int l = ed.locationAt(int(wx), int(wy)); l >= 0) {
            app.locationClip = {locationCopyOf(ed, l)};
            setMessage(app, L"Copied " + widen(ed.locationName(l)) + L": Ctrl+V pastes a copy where the mouse is");
            return true;
        }
        return false;
    }
    return false;
}

// A stack of units (units on top of others): the list of the units at a map point (views_map_menu.inc), and the click
// that goes through them. A click again at the same place (4 px or less from the click before it) on the selected unit of
// a stack selects the next unit there when the button comes up with no drag (the top one again after the last). A
// press on the unit that the list or such a click selected keeps it, so a drag moves it, also when it is under others;
// the map menu and a double click act on it too (stackUnitAt).
void showUnitStack(float wx, float wy, int sx, int sy, bool add);
std::wstring unitDisplayName(int u);  // (names.inc)
int gStackClickX = -1000, gStackClickY = -1000;  // the place of the last select click in Units mode (the map view's pixels)
int gStackNext = -1;                             // the unit that the button's release selects (-1: none)
int gStackChosen = -1;                           // the unit that the list or a click again selected last (-1: none)

// The unit that a click at the map point (wx, wy) is on: the unit of the stack there that the list or a click again
// selected (while it is selected), else the top one (Editor::unitAt). -1: none.
int stackUnitAt(const Editor& ed, float wx, float wy) {
    const std::vector<int> stack = ed.unitsAt(int(wx), int(wy));
    if (gStackChosen >= 0 && ed.isSelected(gStackChosen) && std::find(stack.begin(), stack.end(), gStackChosen) != stack.end()) return gStackChosen;
    return stack.empty() ? -1 : stack.front();
}

std::vector<std::pair<int, int>> terrainReachable(const Editor& ed, float wx, float wy, bool isom);  // (below)
int symmetryAnchorNear(const Editor& ed, float wx, float wy, float zoom);  // (views_map_menu.inc)
std::chrono::steady_clock::time_point gSymmetryPressAt{};  // the handle's last click (a drag is not one: a double click's)
int gSymmetryDragged = 0;                                    // the anchor that the drag moves
int gSymmetryHover = -1;
bool gDragOffTold = false;  // (Options > Drag objects to move them off: the status bar told it in this press)                                     // the anchor under the mouse at the last move, or -1
std::pair<int, int> gSymmetryFrom{-1, -1};                  // the center when its drag began (Esc puts it back)
// The symmetry center's handle (Terrain's Select tool, with a mirror or turns on): the screen point (sx, sy) is on it.
// The anchor whose handle is at this place of the window (Terrain's Select tool), or -1. The last one first: on top.
int symmetryHandleIndex(const AppState& app, int sx, int sy) {
    if (!app.ed || !app.ed->isOpen() || app.mode != Mode::Terrain || currentTool(app) != TSelect || app.terrainPasting ||
        (!app.ed->mirrors() && app.ed->turns() <= 1))
        return -1;
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    return symmetryAnchorNear(*app.ed, wx, wy, app.zoom);
}
bool symmetryHandleAt(const AppState& app, int sx, int sy) { return symmetryHandleIndex(app, sx, sy) >= 0; }

// (A modifier key that changes what a press does here has its mark next to the mouse: core/map_mod_mark.inc.)
void mapMouseDown(AppState& app, int sx, int sy, unsigned mods) {
    LayersLiveAfter live{app};
    if (!app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    mapLayersSync();  // (the last edit is in its layer before the next one)
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    gStackNext = -1;
    // Alt+click (macOS: Option+click) on two or more units: the list of the units there (on one unit, or while a unit
    // type is selected for placing: the pipette).
    if ((mods & draw::kAlt) && app.mode == Mode::Units && app.palette < 0 && !app.pasting && !modeLayerHidden() && ed.unitsAt(int(wx), int(wy)).size() > 1) {
        gStackClickX = gStackClickY = -1000;
        app.drag = Drag::None;
        showUnitStack(wx, wy, sx, sy, (mods & draw::kShift) != 0);
        app.dirty = true;
        return;
    }
    if ((mods & draw::kAlt) && pipette(app, wx, wy)) {  // Alt+click: the pipette
        app.dirty = true;
        return;
    }
    if (modeLayerHidden()) return modeLayerHiddenClick();  // (core/map_edit.inc)
    if (trigUnitPickClick(wx, wy)) return;
    if (mapLayersEraseOn(app)) return mapLayersEraseDown(app, wx, wy);
    if (app.mode == Mode::Layers) return mapLayersMoveDown(app, wx, wy);
    if (app.pasting) {
        // Paste mode stays on (another click pastes again) until a right click or Escape; the pasted
        // units are not selected, so the next paste does not move them.
        float pwx = wx, pwy = wy;
        snapUnitPoint(pwx, pwy);  // Options > Units > Snap to the grid: the middle of the copy on the grid lines
        applyUnitLimit(ed);
        const int copied = int(ed.pasteUnits(int(pwx), int(pwy)).size());  // (the symmetric copies too)
        const int pasted = ed.paste(int(pwx), int(pwy), app.correction);
        setMessage(app, pasted < copied && atUnitLimit(ed) ? unitLimitText(ed)
                   : (pasted < copied ? L"Pasted " + std::to_wstring(pasted) + L" of " + std::to_wstring(copied) + L" units (the other units do not fit here)"
                                      : L"Pasted " + std::to_wstring(pasted) + L" units") + L". To stop, right-click or press Esc.");
        ed.clearSelection();
        app.drag = Drag::None;
        app.dirty = true;
        return;
    }
    app.dragWX = wx, app.dragWY = wy, app.dragSX = sx, app.dragSY = sy, app.dragMoved = false, gDragOffTold = false;
    app.terrainSelOp = 0;  // (Rectangle's Select sets it: tilesMouseDown)
    if (!(mods & (draw::kShift | draw::kCtrl)) && symmetryHandleAt(app, sx, sy)) {  // the symmetry center: a drag moves it; a double click puts it in the map's middle
        const auto now = std::chrono::steady_clock::now();
        const int anchor = symmetryHandleIndex(app, sx, sy);
        if (now - gSymmetryPressAt < std::chrono::milliseconds(400) && anchor == gSymmetryDragged) {
            if (anchor > 0) {
                ed.removeSymmetryAnchor(anchor);
                setMessage(app, L"Removed the symmetry anchor");
            } else if (ed.centerSymmetryAnchor(0)) {  // (not when another anchor is in the middle)
                setMessage(app, L"The symmetry center is in the middle of the map");
            }
            gSymmetryPressAt = {};
        } else {
            gSymmetryPressAt = now;
            gSymmetryDragged = anchor;
            gSymmetryFrom = anchor > 0 ? ed.symmetryAnchors()[size_t(anchor)] : ed.symmetryCenterSet();
            app.drag = Drag::SymmetryCenter;
        }
        app.dirty = true;
        return;
    }
    bool shift = mods & draw::kShift, ctrl = mods & draw::kCtrl;
    if (objectEraseOn() && !app.pastingItems && !app.pasting) {  // Erase: the brush's circle; a drag erases along the way (one undo step)
        ed.beginStroke();
        app.sprayPlaced = eraseAt(wx, wy);
        gPaintX = wx, gPaintY = wy;
        app.drag = Drag::Erase;
    } else switch (app.mode) {
    case Mode::Units: {
        int hit = ed.unitAt(int(wx), int(wy));
        if (currentTool(app) == UFill) {
            const int n = fillObjectsAt(wx, wy);
            if (n >= 0) setMessage(app, L"Filled the ground with " + std::to_wstring(n) + L" units");
            break;
        }
        if (currentTool(app) == USpray) {
            ed.beginStroke();  // the press and hold: one undo step
            app.sprayPlaced = sprayAt(wx, wy);
            setMessage(app, L"Sprayed " + std::to_wstring(app.sprayPlaced) + L" units");
            app.lastSprayX = sx, app.lastSprayY = sy;
            app.drag = Drag::Spray;
            sprayHoldStart(app, sx, sy);
            break;
        }
        if (currentTool(app) == UReplace) {
            if (hit >= 0 && app.palette >= 0) {
                uint16_t from = ed.units()[hit].type;
                bool inSel = ed.isSelected(hit) && ed.selection().size() > 1;
                int n = ed.replaceUnits(from, uint16_t(app.palette), inSel);
                setMessage(app, L"Replaced " + std::to_wstring(n) + L" " + unitShortName(ed.unitTypes()[from]) + L" with " +
                           unitShortName(ed.unitTypes()[uint16_t(app.palette)]) + (inSel ? L" (selection)" : L""));
            } else {
                setMessage(app, L"Replace: select the new type in the list. Then click a unit. All units of the clicked unit's type change to the new type. If the clicked unit is one of several selected units, only the selected units of that type change.");
            }
            break;
        }
        if (app.palette >= 0) {
            // A unit is picked: a click always places (also on a unit: an air unit goes over a ground unit), or the
            // status bar tells why it cannot; dragging keeps placing along the way (one undo step). To select
            // again: Esc or the Select tool.
            ed.beginStroke();
            app.placedInStroke = placePaletteUnit(wx, wy, app.correction) ? 1 : 0;
            app.lastPlaceX = wx, app.lastPlaceY = wy;
            app.drag = Drag::PlaceStroke;
        } else if (hit >= 0) {
            const bool again = std::abs(sx - gStackClickX) <= 4 && std::abs(sy - gStackClickY) <= 4;
            gStackClickX = sx, gStackClickY = sy;
            const std::vector<int> stack = ed.unitsAt(int(wx), int(wy));
            const auto in = ed.selection().size() == 1 ? std::find(stack.begin(), stack.end(), ed.selection()[0]) : stack.end();
            if (!shift && !ctrl && stack.size() > 1 && in != stack.end() && (again || in == stack.begin() || *in == gStackChosen)) {
                // The selected unit of a stack: it stays selected (a drag moves it); a click again here selects the next.
                if (again) gStackNext = stack[size_t(in - stack.begin() + 1) % stack.size()];
                app.drag = Drag::MoveUnits;
            } else {
                if (ctrl) ed.toggleSelected(hit);
                else if (!ed.isSelected(hit)) ed.select(hit, shift);
                app.drag = ed.isSelected(hit) ? Drag::MoveUnits : Drag::None;
            }
            app.lastValidDX = app.lastValidDY = 0;
        } else {
            gStackClickX = gStackClickY = -1000;
            app.drag = Drag::BoxSelect;
        }
        break;
    }
    case Mode::Terrain: {
        if (currentTool(app) == TMoveSel) {  // Move: the selection follows the drag; it moves at the release
            if (!app.hasTerrainSel) {
                setMessage(app, L"Select terrain first, with Select, the Lasso or the Magic wand");
                break;
            }
            if (mapLayersActiveRefuses()) break;
            gTerrainMoveDX = gTerrainMoveDY = 0;
            app.drag = Drag::TerrainMove;
            break;
        }
        if (currentTool(app) == TErase) {  // Erase: Dirt with the isometric brush (in Rectangle, Tile and Custom too)
            if (!ed.canPaintTerrain()) {
                setMessage(app, L"Could not erase: the map has no isometric terrain data");
                break;
            }
            ed.beginTerrainStroke();
            ed.paintBrush(int(wx), int(wy), eraseTerrain(ed), currentBrush(app));
            gPaintX = wx, gPaintY = wy, gRerollX = wx, gRerollY = wy;
            gPaintCorners = brushPlace(ed, wx, wy, currentBrush(app)), gPaintTerrain = eraseTerrain(ed);
            app.drag = Drag::Paint;
            break;
        }
        if (isShapeTool(currentTool(app)) && !(ctrl && !gTiles->stamps)) {  // Line, Contour, Polygon (Ctrl+click picks the terrain or tile, below)
            shapeMouseDown(wx, wy, (mods & draw::kShift) != 0);
            break;
        }
        if (currentTool(app) == TLasso) {  // Lasso: the line, then the tiles inside it at the release
            gLassoPoints = {{wx, wy}};
            gLassoAdd = shift, gLassoRemove = ctrl;
            app.drag = Drag::TerrainLasso;
            break;
        }
        if (currentTool(app) == TWand) {  // the Magic wand: a click selects what is reachable; Shift adds, Ctrl removes
            const std::vector<std::pair<int, int>> more = terrainReachable(ed, wx, wy, !app.terrainRect);
            std::vector<std::pair<int, int>> tiles;
            if (shift || ctrl) tiles = terrainSelTiles();
            if (ctrl) {
                const std::set<std::pair<int, int>> out(more.begin(), more.end());
                std::erase_if(tiles, [&](const std::pair<int, int>& t) { return out.count(t) != 0; });
            } else {
                tiles.insert(tiles.end(), more.begin(), more.end());
                std::sort(tiles.begin(), tiles.end());
                tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
            }
            setTerrainSelTiles(tiles);
            setMessage(app, tiles.empty() ? std::wstring(L"Cleared the terrain selection")
                                          : L"Selected " + std::to_wstring(tiles.size()) + (tiles.size() == 1 ? L" tile" : L" tiles") +
                                                L". Shift+click adds. Ctrl+click removes.");
            app.drag = Drag::None;
            break;
        }
        // Rectangle: tiles as they are, no ISOM needed. (Solid's Ctrl+click picks the terrain, as Isometric's below.)
        if (app.terrainRect && !(gTiles->solid && ctrl && currentTool(app) != TSelect)) {
            tilesMouseDown(wx, wy, mods);
            if (app.drag == Drag::Spray) app.lastSprayX = sx, app.lastSprayY = sy;
            if (app.drag == Drag::Spray) sprayHoldStart(app, sx, sy);
            break;
        }
        const bool shiftDown = (mods & draw::kShift) != 0;
        if (!ed.canPaintTerrain()) {  // (no ISOM: Solid's Ctrl+click has no terrain to pick; its tools work with the list's)
            if (app.terrainRect)
                setMessage(app, app.palette > 0 ? L"Could not select the terrain. The map has no isometric terrain data." : L"Select a terrain in the list first"),
                    app.drag = Drag::None;
            break;
        }
        // The Select tool (not pasting): Shift+drag adds to the selection, Ctrl+drag takes out of it, as in Tile; with no
        // selection, Ctrl+click picks the terrain under the mouse (below)
        if (currentTool(app) == TSelect && !app.terrainPasting && (shift || (ctrl && app.hasTerrainSel))) {
            app.terrainSelOp = shift ? 1 : 2;
            app.drag = Drag::TerrainSelect;
            break;
        }
        if (ctrl) {
            // Eyedropper: pick the terrain under the cursor as the brush terrain.
            uint16_t t = ed.paletteTerrainOf(ed.paletteTerrainAt(int(wx), int(wy)));  // (a mod terrain's piece: that mod terrain; a variant: its terrain)
            if (selectPalette(app, t) < 0 && shellClearFilter()) {  // hidden by the filter: clear it and look again
                rebuildList(app);
                selectPalette(app, t);
            }
            shellPaletteReveal(t);  // (the list scrolls to it)
            // Placement: from Select (or a paste), the Brush paints with it; Spray, Fill and Replace keep their tool.
            if (currentTool(app) == TSelect) app.toolByMode[int(Mode::Terrain)] = TBrush, app.terrainPasting = false;
            app.palette = t;
            fillToolCombo(app), updateStatus(app);
            setMessage(app, L"Painting " + widen(ed.terrainName(t)) +
                                L" (selected with Ctrl+click)");
            app.drag = Drag::None;
            break;
        }
        int tool = currentTool(app);
        if (tool == TSelect) {
            if (app.terrainPasting && mapLayersActiveRefuses()) {
                app.drag = Drag::None;
                break;
            }
            if (app.terrainPasting && mapLayersOnLayer() && !ed.mirrors() && ed.turns() <= 1) {  // (with symmetry: a brush, as on the Background)
                std::set<std::pair<int, int>> tiles;  // (the tiles of the four rects round each corner: what copies these corners again)
                for (const auto& [cx, cy] : ed.terrainPasteCorners(int(wx), int(wy)))
                    for (int ty = cy - 1; ty <= cy; ++ty)
                        for (int tx = 2 * cx - 2; tx <= 2 * cx + 1; ++tx)
                            if (tx >= 0 && ty >= 0 && tx < int(ed.width()) && ty < int(ed.height())) tiles.insert({tx, ty});
                ed.beginStroke();
                applyUnitLimit(ed);
                const bool pasted = ed.pasteTerrain(int(wx), int(wy), app.correction);
                ed.endStroke();
                if (pasted) terrainPastedOnLayer({tiles.begin(), tiles.end()});
                app.drag = Drag::None;
                break;
            }
            if (app.terrainPasting) {  // a brush of the copy: a click pastes it, a drag pastes it along the way (one undo step)
                ed.beginStroke();
                applyUnitLimit(ed);
                if (ed.pasteTerrain(int(wx), int(wy), app.correction)) setMessage(app, L"Pasted terrain");
                app.lastPlaceX = wx, app.lastPlaceY = wy;
                app.drag = Drag::PasteStroke;
            } else {
                app.drag = Drag::TerrainSelect;
            }
            break;
        }
        if (tool == TJumble && !app.terrainRect) {  // (no pick: the terrains under the brush)
            ed.beginTerrainStroke();
            jumbleAt(app, wx, wy);
            gPaintX = wx, gPaintY = wy;
            app.drag = Drag::Jumble;
            break;
        }
        if (app.palette <= 0) break;
        uint16_t t = uint16_t(app.palette);
        if (tool == TBrush) {
            // Painting a terrain onto itself redraws its tiles with new variants (a click, and on along the drag); the
            // stroke is one undo step.
            ed.beginTerrainStroke();
            if (ed.terrainMask().empty() && ed.brushAllTerrain(int(wx), int(wy), currentBrush(app), t)) {
                int n = ed.rerollTerrain(int(wx), int(wy), currentBrush(app), app.spraySeed++);
                setMessage(app, L"New variants for " + std::to_wstring(n) + L" tiles");
            }
            gRerollX = wx, gRerollY = wy;
            ed.paintBrush(int(wx), int(wy), t, currentBrush(app));
            gPaintX = wx, gPaintY = wy;
            gPaintCorners = brushPlace(ed, wx, wy, currentBrush(app)), gPaintTerrain = t;
            app.drag = Drag::Paint;
        } else if (tool == TSpray) {
            ed.beginTerrainStroke();
            sprayAt(wx, wy);
            app.lastSprayX = sx, app.lastSprayY = sy;
            app.drag = Drag::Spray;
            sprayHoldStart(app, sx, sy);
        } else if (tool == TFill && !shiftDown) {
            std::vector<uint16_t> picks;  // (several: in patches, Mix)
            for (int v : selectedPalette())
                if (v > 0) picks.push_back(uint16_t(v));
            if (picks.size() > 1 ? ed.fillTerrainMix(int(wx), int(wy), picks, fillPatch(), app.spraySeed++) : ed.fillTerrain(int(wx), int(wy), t))
                setMessage(app, picks.size() > 1 ? L"Filled with " + std::to_wstring(picks.size()) + L" terrains" : L"Filled with " + itemLabel(app, app.palette));
            else if (!ed.terrainMask().empty())
                setMessage(app, L"Nothing changed: the mask keeps this terrain");
        } else if (tool == TReplace || tool == TFill) {  // Fill with Shift: every place of that terrain
            uint16_t from = ed.terrainAt(int(wx), int(wy));
            const Rect* area = app.hasTerrainSel ? &app.terrainSel : nullptr;
            // (in the selection's shape, or its tiles: a selection of any form)
            int n = app.hasTerrainSel && !app.terrainSelMask.empty()
                        ? ed.replaceTerrain(from, t, area, int(Editor::SelectShape::Rect), [](int x, int y) { return terrainSelHas(x, y); })
                        : ed.replaceTerrain(from, t, area, int(selectShape(app)));
            // (the clicked terrain by the map's isometric data: a mod's tiles drawn over another terrain count as that one)
            if (from == t) setMessage(app, L"Did not fill: the clicked terrain is " + widen(ed.terrainName(from)) + L" already");
            else setMessage(app, L"Replaced " + widen(ed.terrainName(from)) + L" with " + itemLabel(app, app.palette) + L" (" +
                                     std::to_wstring(n) + L" corners" + (area ? L", in selection)" : L")"));
        }
        break;
    }
    case Mode::Sprites:
        if (currentTool(app) == SSpray && app.palette >= 0) {
            ed.beginStroke();  // the press and hold: one undo step
            app.sprayPlaced = sprayAt(wx, wy);
            setMessage(app, L"Sprayed " + std::to_wstring(app.sprayPlaced) + L" sprites");
            app.lastSprayX = sx, app.lastSprayY = sy;
            app.drag = Drag::Spray;
            sprayHoldStart(app, sx, sy);
            break;
        }
        if (app.palette >= 0) {
            // Click places one; dragging places one at each new place of the mouse (one undo step).
            ed.beginStroke();
            app.placedInStroke = 0;
            if (strokePlace(wx, wy)) {
                app.placedInStroke = 1;
                setMessage(app, L"Placed sprite " + entryName("sprites", app.palette));
            } else setMessage(app, L"Did not place the sprite: it is outside the map");
            app.lastPlaceX = wx, app.lastPlaceY = wy;
            app.drag = Drag::PlaceStroke;
        } else if (app.pastingItems) {
            std::vector<Editor::ItemAt> items = app.spriteClip;
            for (auto& it : items) it.x += spriteSnapX(wx), it.y += spriteSnapY(wy);
            const int n = ed.placeSprites(items);
            setMessage(app, L"Pasted " + std::to_wstring(n) + L" sprites");
            // (the copy stays on the mouse: click again to paste again; right-click or Esc stops; GitHub feedback #123)
        } else {
            selectItemAt(app, app.selSprites, mapSpriteAt(ed, int(wx), int(wy)), mods, Drag::MoveSprite);
        }
        break;
    case Mode::Doodads:
        if (currentTool(app) == DFill) {
            const int n = fillObjectsAt(wx, wy);
            if (n >= 0) setMessage(app, L"Filled the ground with " + std::to_wstring(n) + L" doodads");
            break;
        }
        if (currentTool(app) == DSpray) {
            ed.beginStroke();  // the press and hold: one undo step
            app.sprayPlaced = sprayAt(wx, wy);
            setMessage(app, L"Sprayed " + std::to_wstring(app.sprayPlaced) + L" doodads");
            app.lastSprayX = sx, app.lastSprayY = sy;
            app.drag = Drag::Spray;
            sprayHoldStart(app, sx, sy);
            break;
        }
        if (app.palette >= 0) {
            // Click places one; dragging keeps placing along the way (one undo step). Several picked (Ctrl or
            // Shift in the palette): each one placed is one of them, at random.
            std::string why;
            ed.beginStroke();
            app.placedInStroke = 0;
            const RECT box = strokeBox(wx, wy);
            if (placePaletteDoodad(app, wx, wy, why)) {
                app.placedInStroke = 1, app.lastPlacedBox = box;
                setMessage(app, L"Placed doodad");
                if (paletteChosen().size() > 1) {
                    static std::mt19937 rng{std::random_device{}()};
                    app.palette = paletteChosen()[std::uniform_int_distribution<size_t>(0, paletteChosen().size() - 1)(rng)];
                }
            } else setMessage(app, L"Did not place the doodad: " + widen(why));
            app.lastPlaceX = wx, app.lastPlaceY = wy;
            app.drag = Drag::PlaceStroke;
        } else if (app.pastingItems) {
            std::vector<Editor::ItemAt> items;
            for (Editor::ItemAt it : app.doodadClip) {
                it.x += int(wx), it.y += int(wy);
                items.push_back(it);
            }
            const int n = items.empty() ? 0 : ed.placeDoodads(items, app.doodadCheck, app.doodadNearest);
            setMessage(app, L"Pasted " + std::to_wstring(n) + L" of " + std::to_wstring(app.doodadClip.size()) + L" doodads (the others do not fit there)");
            // (the copy stays on the mouse: click again to paste again; right-click or Esc stops; GitHub feedback #123)
        } else {
            // Nothing picked: select (Ctrl or Shift adds), drag to move, a drag on empty ground selects with a box.
            const int at = ed.doodadAt(int(wx), int(wy));
            selectItemAt(app, app.selDoodads, at, mods, Drag::MoveDoodad);
        }
        break;
    case Mode::Locations: {
        if (app.pastingItems) {  // (the copy stays on the mouse: click again to paste again; right-click or Esc stops)
            void pasteLocationsAt(AppState & app, float wx, float wy);
            pasteLocationsAt(app, wx, wy);
            break;
        }
        int edges = locationEdgesAt(wx, wy);
        int inside = ed.locationAt(int(wx), int(wy));
        // Ctrl or Shift on a location: a click takes it in or out of the selection, a drag makes an inverted location.
        const bool adds = (mods & (draw::kShift | draw::kCtrl)) != 0;
        app.locToggle = -1;
        if ((mods & draw::kAlt) && !edges) {  // Alt and a drag: a box selects the locations it touches (Ctrl or Shift keeps the rest)
            app.drag = Drag::BoxSelect;
        } else if (edges && !adds) {
            app.drag = Drag::LocResize;
            app.locEdges = edges;
            app.locStart = ed.locationRect(ed.selectedLocation());
        } else if (inside >= 0 && adds) {
            app.locToggle = inside;
            app.drag = Drag::LocCreate;
        } else if (inside >= 0) {
            // One of the selected: they all move with it (it is the selected one: its edges resize). Else it alone.
            if (ed.locationSelected(inside)) ed.addLocationSelection(inside);
            else ed.selectLocation(inside);
            app.locStart = ed.locationRect(inside);
            app.drag = Drag::LocMove;
        } else {
            app.drag = Drag::LocCreate;
        }
        break;
    }
    }
    app.dirty = true;
}

void mapMouseMove(AppState& app, int sx, int sy, unsigned mods) {
    LayersLiveAfter live{app};
    app.mouseX = sx, app.mouseY = sy;
    if (!app.mouseIn) {
        app.mouseIn = true;
        shellTrackMouseLeave();
    }
    if (std::abs(sx - app.dragSX) + std::abs(sy - app.dragSY) > 4) {
        // (Options > Drag objects to move them off: a press on an object selects it and a drag does not move it)
        const bool objectMove = app.drag == Drag::MoveUnits || app.drag == Drag::MoveDoodad || app.drag == Drag::MoveSprite || app.drag == Drag::LocMove;
        if (!objectMove || app.dragObjects) app.dragMoved = true;
        else if (!std::exchange(gDragOffTold, true)) setMessage(app, L"A drag does not move objects. To move them: Options > Drag objects to move them");
    }
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    if (app.drag == Drag::TerrainLasso) {
        if (std::hypot(wx - gLassoPoints.back().first, wy - gLassoPoints.back().second) * app.zoom >= 3) gLassoPoints.push_back({wx, wy}), app.dirty = true;
        updateStatus(app);
        return;
    }
    if (app.drag == Drag::TerrainMove && app.ed) {
        gTerrainMoveDX = int(wx - app.dragWX), gTerrainMoveDY = int(wy - app.dragWY);
        updateStatus(app);
        app.dirty = true;
        return;
    }
    if (app.drag == Drag::LayerTool && app.ed) {
        mapLayersDragMove(app, wx, wy);
        updateStatus(app);
        return;
    }
    if (app.drag == Drag::SymmetryCenter && app.ed) {
        if (app.dragMoved) app.ed->setSymmetryAnchor(gSymmetryDragged, std::max(0, int(wx)), std::max(0, int(wy)));
        updateStatus(app);
        app.dirty = true;
        return;
    }
    {  // (the handle shows while the mouse is on it; the status bar tells what it does)
        if (const int on = app.drag == Drag::None ? symmetryHandleIndex(app, sx, sy) : -1; on != gSymmetryHover) {
            gSymmetryHover = on, app.dirty = true;
            if (on == 0) setMessage(app, L"Symmetry center: drag it to move it, double-click it to put it in the middle, right-click it for more");
            else if (on > 0) setMessage(app, L"Symmetry anchor: drag it to move it, double-click it to remove it, right-click it for more");
        }
    }
    if (app.drag == Drag::Paint && app.ed) {
        const uint16_t paint = currentTool(app) == TErase ? eraseTerrain(*app.ed) : uint16_t(app.palette);  // (Erase: Dirt)
        // Over the same terrain: new variants each half brush along the drag (not on every small move).
        const float extent = std::min(app.brushSize, currentBrush(app).height());  // a flat brush: its smaller side
        if (std::hypot(wx - gRerollX, wy - gRerollY) >= std::max(16.0f, extent * 32) && app.ed->terrainMask().empty() &&
            app.ed->brushAllTerrain(int(wx), int(wy), currentBrush(app), paint)) {
            app.ed->rerollTerrain(int(wx), int(wy), currentBrush(app), app.spraySeed++);
            gRerollX = wx, gRerollY = wy;
        }
        // A fast move skips ground: paint the way from the last point too, at most half a brush apart, and at most 8
        // more steps a move (zoomed far out, one move crosses much of the map; the next moves paint the rest).
        const float step = std::max(32.0f, extent * 32), dx = wx - gPaintX, dy = wy - gPaintY, len = std::hypot(dx, dy);
        // A point where the brush covers the corners it last painted paints nothing: a held brush that moves a little
        // keeps its tiles (each paint makes them again); a move away and back paints again.
        const int n = std::clamp(int(len / step), 0, 8);
        auto paintAt = [&](float x, float y) {
            std::vector<std::pair<int, int>> place = brushPlace(*app.ed, x, y, currentBrush(app));
            if (place == gPaintCorners && paint == gPaintTerrain) return;
            app.ed->paintBrush(int(x), int(y), paint, currentBrush(app));
            gPaintCorners = std::move(place), gPaintTerrain = paint;
        };
        for (int k = 1; k <= n; ++k) paintAt(gPaintX + dx * k / (n + 1), gPaintY + dy * k / (n + 1));
        paintAt(wx, wy);
        gPaintX = wx, gPaintY = wy;
    }
    if (app.drag == Drag::Jumble && app.ed &&
        std::hypot(wx - gPaintX, wy - gPaintY) >= std::max(16.0f, std::min(app.brushSize, currentBrush(app).height()) * 32)) {
        jumbleAt(app, wx, wy);  // (each half brush along the drag)
        gPaintX = wx, gPaintY = wy;
    }
    if (app.drag == Drag::Erase && app.ed) {  // (each half brush along the way from the last point: a fast move skips no ground)
        const float step = float(std::max(8.0, sprayRadius() * 0.5)), dx = wx - gPaintX, dy = wy - gPaintY;
        const int n = int(std::hypot(dx, dy) / step);
        for (int k = 1; k <= n; ++k) app.sprayPlaced += eraseAt(gPaintX + dx * k / n, gPaintY + dy * k / n);
        if (n > 0) gPaintX = wx, gPaintY = wy;
    }
    if (app.drag == Drag::TerrainSelect && app.dragMoved) {  // (the box's size while it is dragged, GitHub #240)
        const Rect r = terrainSelectRect(wx, wy);
        setHint(L"Selecting " + std::to_wstring(std::max(0, int(r.right - r.left) / 32)) + L" x " +
                            std::to_wstring(std::max(0, int(r.bottom - r.top) / 32)) + L" tiles");
    }
    if (app.drag == Drag::Tiles) tilesMouseMove(wx, wy);
    if (app.mode == Mode::Terrain && gShape.tool >= 0) shapeMouseMove(wx, wy, (mods & draw::kShift) != 0);
    if (app.drag == Drag::Spray && app.ed && std::hypot(sx - app.lastSprayX, sy - app.lastSprayY) > std::max(12.0, sprayRadius() * app.zoom * 0.5)) {
        app.lastSprayX = sx, app.lastSprayY = sy;
        app.sprayPlaced += sprayAt(wx, wy);  // along the path
    }
    if (app.drag == Drag::PendingPlace && app.dragMoved) app.drag = Drag::BoxSelect;
    if (app.drag == Drag::PlaceStroke && app.ed && app.palette >= 0) strokeMove(wx, wy);
    if (app.drag == Drag::PasteStroke && app.ed) {  // the copy again where the mouse is a whole copy away from the last one
        const Rect box = app.ed->terrainClipboardBox();
        if (std::abs(wx - app.lastPlaceX) >= float(std::max(32, box.right)) || std::abs(wy - app.lastPlaceY) >= float(std::max(32, box.bottom))) {
            applyUnitLimit(*app.ed);
            app.ed->pasteTerrain(int(wx), int(wy), app.correction);
            app.lastPlaceX = wx, app.lastPlaceY = wy;
        }
    }
    // (a move with no drag changes only the map's picture and the status bar, which the map view paints again; the
    // other windows paint only for a drag)
    if (app.drag != Drag::None) app.dirty = true;
    updateStatus(app);
    if (app.ed && app.drag == Drag::None) pathOverlayMouseMove(wx, wy), pathingLayerMouseMove(wx, wy);
}

// The Magic wand: what is reachable from the click. Isometric: the corners of that terrain along the diamonds' edges,
// and the tiles round them (so its cliffs and edges too). The Rectangle tools: the tiles next to each other of the same
// tile or the same terrain (Fill's match, for any kind).
std::vector<std::pair<int, int>> terrainReachable(const Editor& ed, float wx, float wy, bool isom) {
    const int w = ed.width(), h = ed.height();
    std::set<std::pair<int, int>> out;
    if (isom) {
        for (const auto& [cx, cy] : ed.terrainRegionCorners(int(wx), int(wy)))
            for (int y = cy - 1; y <= cy; ++y)
                for (int x = 2 * cx - 2; x <= 2 * cx + 1; ++x)
                    if (x >= 0 && y >= 0 && x < w && y < h) out.insert({x, y});
        if (!out.empty()) return {out.begin(), out.end()};
    }
    const int tx = int(wx) / 32, ty = int(wy) / 32;
    if (wx < 0 || wy < 0 || tx >= w || ty >= h) return {};
    const std::vector<uint16_t>& t = ed.terrain().tiles;
    const auto& gt = solidGroupTerrains();
    const uint16_t from = t[size_t(ty) * w + tx];
    auto same = [&](uint16_t v) {
        if (v == from) return true;
        const size_t a = v >> 4, b = from >> 4;
        return a < gt.size() && b < gt.size() && gt[b][0] && gt[a] == gt[b];
    };
    std::vector<std::pair<int, int>> todo{{tx, ty}};
    out.insert({tx, ty});
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int k = 0; k < 4; ++k)
            if (nx[k] >= 0 && ny[k] >= 0 && nx[k] < w && ny[k] < h && same(t[size_t(ny[k]) * w + nx[k]]) && out.insert({nx[k], ny[k]}).second)
                todo.push_back({nx[k], ny[k]});
    }
    return {out.begin(), out.end()};
}

void mapMouseUp(AppState& app, int sx, int sy, unsigned mods) {
    if (!app.ed || !app.ed->isOpen()) {
        app.drag = Drag::None;
        return;
    }
    Editor& ed = *app.ed;
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    bool shift = mods & draw::kShift;
    if (app.drag == Drag::LayerTool) return mapLayersDragUp(app);
    if (app.drag == Drag::TerrainLasso) {
        app.drag = Drag::None;
        // The tiles whose middle is inside the line, closed from its end to its start.
        std::vector<std::pair<int, int>> inside;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& [x, y] : gLassoPoints) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        for (int ty = std::max(0, int(y0) / 32); ty <= std::min(int(ed.height()) - 1, int(y1) / 32); ++ty)
            for (int tx = std::max(0, int(x0) / 32); tx <= std::min(int(ed.width()) - 1, int(x1) / 32); ++tx) {
                const float px = tx * 32 + 16.0f, py = ty * 32 + 16.0f;
                bool in = false;
                for (size_t i = 0, j = gLassoPoints.size() - 1; i < gLassoPoints.size(); j = i++) {
                    const auto [ax, ay] = gLassoPoints[i];
                    const auto [bx, by] = gLassoPoints[j];
                    if ((ay > py) != (by > py) && px < (bx - ax) * (py - ay) / (by - ay) + ax) in = !in;
                }
                if (in) inside.push_back({tx, ty});
            }
        gLassoPoints.clear();
        std::vector<std::pair<int, int>> tiles;
        if (gLassoAdd || gLassoRemove) tiles = terrainSelTiles();
        if (gLassoRemove) {
            const std::set<std::pair<int, int>> out(inside.begin(), inside.end());
            std::erase_if(tiles, [&](const std::pair<int, int>& t) { return out.count(t) != 0; });
        } else {
            tiles.insert(tiles.end(), inside.begin(), inside.end());
            std::sort(tiles.begin(), tiles.end());
            tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
        }
        setTerrainSelTiles(tiles);
        setMessage(app, tiles.empty() ? std::wstring(L"Cleared the terrain selection")
                                      : L"Selected " + std::to_wstring(tiles.size()) + (tiles.size() == 1 ? L" tile" : L" tiles") + L". Shift adds. Ctrl removes.");
        app.dirty = true;
        return;
    }
    if (app.drag == Drag::TerrainMove) {
        app.drag = Drag::None;
        moveSelectedTerrain(app, int(wx - app.dragWX), int(wy - app.dragWY));
        app.dirty = true;
        return;
    }
    if (app.drag == Drag::SymmetryCenter) {
        app.drag = Drag::None;
        if (app.dragMoved && app.ed && app.ed->mergeSymmetryAnchors()) gSymmetryPressAt = {}, setMessage(app, L"Merged the symmetry anchors");
        else if (app.dragMoved)
            gSymmetryPressAt = {}, setMessage(app, gSymmetryDragged > 0 ? L"Moved the symmetry anchor. Double-click it to remove it"
                                                                         : L"Moved the symmetry center. Double-click it to put it in the middle of the map");
        gSymmetryHover = symmetryHandleIndex(app, sx, sy);  // (after a merge too: the next move keeps this message)
        app.dirty = true;
        return;
    }
    switch (app.drag) {
    case Drag::MoveUnits:
        if (!app.dragMoved && !gDragOffTold && gStackNext >= 0 && gStackNext < int(ed.units().size())) {  // a click again on a stack: the next unit
            const std::vector<int> stack = ed.unitsAt(int(app.dragWX), int(app.dragWY));
            const auto at = std::find(stack.begin(), stack.end(), gStackNext);
            if (at != stack.end()) {
                ed.select(gStackNext, false);
                gStackChosen = gStackNext;
                const Unit& u = ed.units()[size_t(gStackNext)];
                setMessage(app, L"Selected unit " + std::to_wstring(at - stack.begin() + 1) + L" of " + std::to_wstring(stack.size()) + L" here: " +
                                    unitDisplayName(u.type) + L", Player " + std::to_wstring(u.owner + 1) + L". Click again for the next unit.");
            }
        }
        gStackNext = -1;
        if (app.dragMoved) {
            float dx, dy;
            dragMoveDelta(wx, wy, dx, dy);
            const bool held = app.dragSnapBack && !app.allowIllegalUnits;  // the offset fits already
            if (dx || dy) {
                const int moved = ed.moveSelection(int(std::lround(dx)), int(std::lround(dy)), held ? false : app.correction);
                setMessage(app, moved ? L"Moved " + std::to_wstring(moved) + L" units" : L"The units do not fit there. They stay where they were.");
            } else if (held) {
                setMessage(app, L"The units do not fit along the drag. They stay where they were.");
            }
        }
        break;
    case Drag::BoxSelect:
        if (app.mode == Mode::Doodads || app.mode == Mode::Sprites) {
            const Rect r = Rect{int(app.dragWX), int(app.dragWY), int(wx), int(wy)}.normalized();
            auto& sel = app.mode == Mode::Doodads ? app.selDoodads : app.selSprites;
            if (!shift && !(mods & draw::kCtrl)) sel.clear();
            const int n = app.mode == Mode::Doodads ? int(ed.doodads().size()) : int(ed.sprites().size());
            for (int i = 0; i < n; ++i) {
                int x, y;
                if (app.mode == Mode::Doodads) x = ed.doodads()[size_t(i)].x, y = ed.doodads()[size_t(i)].y;
                else if (spriteIsOverlay(app, i)) continue;
                else x = ed.sprites()[size_t(i)].x, y = ed.sprites()[size_t(i)].y;
                if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom && std::find(sel.begin(), sel.end(), i) == sel.end()) sel.push_back(i);
            }
            setMessage(app, L"Selected " + std::to_wstring(sel.size()) + (app.mode == Mode::Doodads ? L" doodads" : L" sprites"));
        } else if (app.mode == Mode::Locations) {
            const Rect b = Rect{int(app.dragWX), int(app.dragWY), int(wx), int(wy)}.normalized();
            const bool add = shift || (mods & draw::kCtrl);
            if (!add) ed.selectLocation(-1);
            for (int i = 0; i < ed.locationCount(); ++i)
                if (i != kAnywhereLocation && ed.locationUsed(i) && !ed.locationSelected(i) && ed.locationRect(i).intersects(b)) ed.addLocationSelection(i);
            setMessage(app, std::to_wstring(ed.selectedLocations().size()) + L" selected: drag to move, Delete removes, Ctrl+C copies");
        } else {
            ed.selectRect({int(app.dragWX), int(app.dragWY), int(wx), int(wy)}, shift || (mods & draw::kCtrl));
        }
        break;
    case Drag::PasteStroke:
        ed.endStroke();
        setMessage(app, L"Pasted terrain. To stop pasting, right-click or press Esc.");
        break;
    case Drag::PlaceStroke:
        ed.endStroke();
        if (app.placedInStroke > 1)
            setMessage(app, L"Placed " + std::to_wstring(app.placedInStroke) +
                       (app.mode == Mode::Doodads ? L" doodads" : app.mode == Mode::Sprites ? L" sprites" : L" units"));
        break;
    case Drag::PendingPlace: {
        uint16_t type = uint16_t(app.palette);
        const UnitType& t = ed.unitTypes()[type];
        uint8_t who = (t.availability & UnitAvail::NonNeutral) ? uint8_t(app.playerOwner) : uint8_t(11);
        snapUnitPoint(wx, wy);
        applyUnitLimit(ed);
        PlaceResult r = ed.placeUnit(type, who, int(wx), int(wy), app.correction);
        ed.clearSelection();  // placed units are not selected (placing is not selecting)
        if (r.placed) {
            uint16_t ux = uint16_t(std::clamp(int(wx), 0, ed.width() * 32 - 1)), uy = uint16_t(std::clamp(int(wy), 0, ed.height() * 32 - 1));
            snapToGrid(t, ux, uy);
            bool moved = r.unit.x != ux || r.unit.y != uy;
            setMessage(app, (r.movedStart ? tr(L"Moved the start location of Player ") + std::to_wstring(who + 1) : L"Placed " + widen(t.name)) +
                                (moved ? L" (moved to the nearest valid spot)" : L""));
        } else {
            std::wstring why;
            for (auto is : r.issues) why += L" " + widen(placementIssueName(is));
            setMessage(app, atUnitLimit(ed) ? unitLimitText(ed) : L"Cannot place here:" + why + (app.correction ? L" (no valid spot nearby)" : L""));
        }
        break;
    }
    case Drag::Paint:
    case Drag::Jumble: ed.endTerrainStroke(); break;
    case Drag::Erase:
        ed.endStroke();
        if (app.mode == Mode::Locations) rebuildList(app);  // (the list shows the locations that are left)
        setMessage(app, L"Erased " + std::to_wstring(app.sprayPlaced) +
                            (app.mode == Mode::Units ? L" units" : app.mode == Mode::Doodads ? L" doodads" : app.mode == Mode::Sprites ? L" sprites" : L" locations"));
        break;
    case Drag::Spray:
        sprayHoldStop();
        if (app.mode == Mode::Terrain && app.terrainRect) tilesMouseUp(wx, wy);
        else if (app.mode == Mode::Terrain) ed.endTerrainStroke();  // (its ramps fit the new cliffs)
        else ed.endStroke();  // the units' and doodads' stroke (the press and hold: one undo step)
        if (app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites)
            setMessage(app, L"Sprayed " + std::to_wstring(app.sprayPlaced) + (app.mode == Mode::Units ? L" units" : app.mode == Mode::Doodads ? L" doodads" : L" sprites"));
        break;
    case Drag::Tiles: tilesMouseUp(wx, wy); break;
    case Drag::Shape: shapeMouseUp(wx, wy, (mods & draw::kShift) != 0); break;
    case Drag::TerrainSelect: {
        // (Rectangle: a Ctrl+click on a tile that is not selected picks it, as with the Brush: the Tiles list shows it,
        // and the Brush places it; Custom and Solid: their list has no tiles)
        if (const int tx = int(std::floor(app.dragWX / 32)), ty = int(std::floor(app.dragWY / 32));  // (the tile pressed)
            app.terrainRect && app.terrainSelOp == 2 && !app.dragMoved && !gTiles->stamps && !gTiles->solid && tx >= 0 && ty >= 0 && tx < ed.width() && ty < ed.height() &&
            !terrainSelHas(tx, ty)) {
            app.terrainSelOp = 0;
            const uint16_t v = ed.terrain().tiles[size_t(ty) * ed.width() + tx];
            tilesPick(v, true), tilesToBrush(), tilesReveal(v);
            break;
        }
        if (app.terrainSelOp) {  // Shift: the tiles go into the selection; Ctrl: out of it (a click: one tile)
            terrainSelCombine(terrainSelectRect(wx, wy), app.terrainSelOp == 1);
            app.terrainSelOp = 0;
            const size_t n = terrainSelTiles().size();
            setMessage(app, n ? L"Selected " + std::to_wstring(n) + (n == 1 ? L" tile" : L" tiles") +
                                    L". Shift+drag adds tiles. Ctrl+drag removes tiles. To save the tiles as custom terrain, right-click the selection."
                              : std::wstring(L"Cleared the terrain selection"));
            break;
        }
        const Rect r = app.terrainRect && !app.dragMoved ? Rect{} : terrainSelectRect(wx, wy);  // Rectangle: a click clears it
        app.hasTerrainSel = r.right - r.left >= 32 && r.bottom - r.top >= 32;
        app.terrainSel = r;
        app.terrainSelMask.clear();
        if (app.terrainRect && app.hasTerrainSel)
            setMessage(app, L"Selected " + std::to_wstring((r.right - r.left) / 32) + L" x " + std::to_wstring((r.bottom - r.top) / 32) +
                       L" tiles. Press Ctrl+C to copy them.");
        else
            setMessage(app, app.hasTerrainSel ? L"Selected the terrain. Press Ctrl+C to copy it. Replace changes only the selected area." : L"Cleared the terrain selection");
        break;
    }
    case Drag::LocCreate: {
        Rect r = previewLocationRect(wx, wy);
        if (r.right - r.left >= 8 && r.bottom - r.top >= 8) {
            // Ctrl or Shift while it is made: an inverted location (holds air units only; drawn red).
            const bool inverted = shellCtrlOrShiftDown();
            int i = ed.createLocation(r, inverted);
            if (i >= 0) rebuildList(app), setMessage(app, L"Created " + widen(ed.locationName(i)) + (inverted ? L" (inverted: it holds air units only)" : L""));
            else setMessage(app, L"Could not create the location: all location slots are in use");
        } else if (app.locToggle >= 0 && ed.locationUsed(app.locToggle)) {
            const int l = app.locToggle;
            if (ed.locationSelected(l)) ed.removeLocationSelection(l);
            else ed.addLocationSelection(l);
            const size_t n = ed.selectedLocations().size();
            setMessage(app, widen(ed.locationName(l)) + (ed.locationSelected(l) ? L" is selected" : L" is not selected") +
                                (n > 1 ? L" (" + std::to_wstring(n) + L" locations selected)" : L""));
        } else {
            ed.selectLocation(-1);
        }
        app.locToggle = -1;
        break;
    }
    case Drag::LocMove:  // the selected locations, by the selected one's way (snapped)
        if (app.dragMoved) {
            const Rect to = previewLocationRect(wx, wy);
            moveSelectedLocations(to.left - app.locStart.left, to.top - app.locStart.top);
        }
        break;
    case Drag::LocResize:
        if (app.dragMoved) ed.setLocationRect(ed.selectedLocation(), previewLocationRect(wx, wy));
        break;
    case Drag::MoveDoodad:
        if (app.dragMoved && !app.selDoodads.empty()) {
            std::string why;
            const int n = int(app.selDoodads.size());
            if (moveSelectedDoodads(int(wx - app.dragWX), int(wy - app.dragWY), why)) {
                app.selDoodads.clear();  // the moved ones are the last ones now
                for (int i = int(ed.doodads().size()) - n; i < int(ed.doodads().size()); ++i) app.selDoodads.push_back(i);
                setMessage(app, L"Moved " + std::to_wstring(n) + L" doodads");
            } else setMessage(app, L"The doodads stay: " + widen(why));
        }
        break;
    case Drag::MoveSprite:
        if (app.dragMoved && !app.selSprites.empty()) {
            int sdx, sdy;
            spriteDragDelta(wx, wy, sdx, sdy);
            if (sdx || sdy) {  // (a drag that stays on the same grid line changes nothing)
                if (ed.moveSprites(app.selSprites, sdx, sdy))
                    setMessage(app, L"Moved " + std::to_wstring(app.selSprites.size()) + L" sprites");
                else setMessage(app, L"Could not move the sprites: the new place is outside the map");
            }
        }
        break;
    default: break;
    }
    app.drag = Drag::None;
    app.dirty = true;
    shellMenusChanged();
}

// The map view's cursor at a point of the view (both programs): the resize cursors on the selected location's edges and
// corners (GitHub feedback #76), the move cursor while the camera moves and over a selected unit, doodad or sprite that
// a press would move (and during that move), the paint bucket while a click would fill (fillClickFills), else the arrow.
bool trigUnitPickActive();
// A Fill tool is on and a click on the map fills: Units' and Doodads' with a pick in the list (or Ctrl+click picks),
// Terrain's with a pick (Rectangle: the list's; Isometric: a terrain, and the map has isometric data); the mode's layer
// shown; nothing pasting or picking.
bool fillClickFills(AppState& app) {
    if (app.drag != Drag::None || !app.ed || !app.ed->isOpen() || app.pasting || app.pastingItems || trigUnitPickActive() ||
        modeLayerHidden())
        return false;
    if (app.mode == Mode::Units) return currentTool() == UFill && !selectedPalette().empty();
    if (app.mode == Mode::Doodads) return currentTool() == DFill && !selectedPalette().empty();
    if (app.mode != Mode::Terrain || currentTool() != TFill) return false;
    return app.terrainRect ? tilesHavePick() : app.ed->canPaintTerrain() && app.palette > 0;
}
stardraft::draw::Cursor mapCursorAt(AppState& app, int x, int y) {
    using stardraft::draw::Cursor;
    if (app.mode == Mode::Layers && app.drag != Drag::Pan && app.drag != Drag::AutoPan) return Cursor::SizeAll;  // (Move Layer)
    if (app.mode == Mode::Terrain && currentTool(app) == TMoveSel && app.drag != Drag::Pan && app.drag != Drag::AutoPan) return Cursor::SizeAll;
    if (app.drag == Drag::Pan || app.drag == Drag::AutoPan || app.drag == Drag::SymmetryCenter) return Cursor::SizeAll;
    if (app.drag == Drag::None && symmetryHandleAt(app, x, y)) return Cursor::SizeAll;
    if (fillClickFills(app)) return Cursor::Fill;
    if (app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites) {
        // Over one that is not selected: the arrow (a click selects it).
        if (app.drag == Drag::MoveUnits || app.drag == Drag::MoveDoodad || app.drag == Drag::MoveSprite) return Cursor::SizeAll;
        if (app.drag != Drag::None || !app.ed || !app.ed->isOpen() || modeLayerHidden() || app.pasting || app.pastingItems ||
            app.palette >= 0 || trigUnitPickActive())
            return Cursor::Arrow;
        Editor& ed = *app.ed;
        float wx, wy;
        toWorld(x, y, wx, wy);
        auto in = [](const std::vector<int>& v, int i) { return i >= 0 && std::find(v.begin(), v.end(), i) != v.end(); };
        bool hit = false;
        if (app.mode == Mode::Units) {
            const int u = currentTool() == UPlace && !ed.selection().empty() ? ed.unitAt(int(wx), int(wy)) : -1;
            hit = u >= 0 && ed.isSelected(u);
        } else if (app.mode == Mode::Doodads)
            hit = currentTool() == DPlace && !app.selDoodads.empty() && in(app.selDoodads, ed.doodadAt(int(wx), int(wy)));
        else
            hit = !app.selSprites.empty() && in(app.selSprites, mapSpriteAt(ed, int(wx), int(wy)));
        return hit ? Cursor::SizeAll : Cursor::Arrow;
    }
    if (app.mode != Mode::Locations || !app.ed || (app.drag != Drag::None && app.drag != Drag::LocResize)) return Cursor::Arrow;
    int e = app.locEdges;
    if (app.drag == Drag::None) {
        float wx, wy;
        toWorld(x, y, wx, wy);
        e = locationEdgesAt(wx, wy);
    }
    const bool h = e & 5, v = e & 10;
    if (h && v) return ((e & 1) && (e & 2)) || ((e & 4) && (e & 8)) ? Cursor::SizeNWSE : Cursor::SizeNESW;
    return h ? Cursor::SizeWE : v ? Cursor::SizeNS : Cursor::Arrow;
}

// --- The map's keys (both programs: the Windows map's WM_KEYDOWN and WM_MOUSEWHEEL, the SDL map view's key()) -----

void enterSelection();
void trigCancelUnitPick();

// Esc on the map: a trigger's unit pick stops; a stroke (place, spray, paste, paint) ends as one undo step; then back
// to the mode's selection (Select, nothing picked, no paste), and in it already, the selection goes. Returns true when
// a stroke ended (the host lets the mouse go).
bool mapCancelStep(AppState& app) {
    trigCancelUnitPick();
    if (app.drag == Drag::LayerTool) return mapLayersDragCancel(app), true;
    if (app.drag == Drag::TerrainMove) return app.drag = Drag::None, app.dirty = true, true;
    if (app.drag == Drag::TerrainLasso) return gLassoPoints.clear(), app.drag = Drag::None, app.dirty = true, true;
    if (app.drag == Drag::SymmetryCenter) {  // (the center's drag: it goes back; the selection stays)
        if (app.ed) app.ed->setSymmetryAnchor(gSymmetryDragged, gSymmetryFrom.first, gSymmetryFrom.second);
        app.drag = Drag::None;
        app.dirty = true;
        return true;
    }
    if (const bool pressed = app.drag == Drag::Shape; shapeCancel()) return pressed;  // (Line, Contour, Polygon: the shape only)
    bool stroke = false;
    if (app.ed && (app.drag == Drag::PlaceStroke || app.drag == Drag::Spray || app.drag == Drag::PasteStroke || app.drag == Drag::Paint ||
                   app.drag == Drag::Jumble || app.drag == Drag::Erase || app.drag == Drag::Tiles)) {
        void tilesStrokeCancelled();  // (tiles_edit.inc)
        if (app.drag == Drag::Tiles) tilesStrokeCancelled();
        sprayHoldStop();
        // (the press so far is one undo step; later edits are steps of their own; a terrain stroke's ground is chosen anew)
        if (app.mode == Mode::Terrain && (app.drag == Drag::Paint || app.drag == Drag::Spray || app.drag == Drag::Jumble)) app.ed->endTerrainStroke();
        else app.ed->endStroke();
        app.drag = Drag::None;
        stroke = true;
    }
    if (!inSelection()) {
        enterSelection();
        setMessage(app, std::wstring(kModeNames[int(app.mode)]) + L": select");
    } else if (app.hasTerrainSel && app.mode == Mode::Terrain) {
        app.hasTerrainSel = false;
        setMessage(app, L"Cleared the selection");
    } else if (app.ed) {
        app.ed->clearSelection();
        if (app.mode == Mode::Locations) app.ed->selectLocation(-1);
        app.selDoodads.clear(), app.selSprites.clear();
        setMessage(app, L"Cleared the selection");
    }
    app.drag = Drag::None;
    app.dirty = true;
    return stroke;
}

void shareTerrainCopy(const Editor& ed);  // (map_edit_commands.inc)

// What is on the cursor to turn or mirror: 1 isometric terrain to paste (its corners: the edges are made again), 2
// tiles to paste (Tile), 3 the Brush's tiles (copied tiles, custom terrain), 4 units, 5 doodads, 6 sprites to paste; 0 nothing.
bool tilesStampOn();  // (tiles_edit.inc)
int cursorTurnable(const AppState& app) {
    if (!app.ed || !app.ed->isOpen()) return 0;
    if (app.mode == Mode::Units) return app.pasting && app.ed->canPaste() ? 4 : 0;
    if (app.mode == Mode::Doodads) return app.pastingItems && !app.doodadClip.empty() ? 5 : 0;
    if (app.mode == Mode::Sprites) return app.pastingItems && !app.spriteClip.empty() ? 6 : 0;
    if (app.mode != Mode::Terrain) return 0;
    if (app.terrainPasting) return !app.terrainRect ? (app.ed->hasTerrainClipboard() ? 1 : 0) : app.ed->hasTileClipboard() ? 2 : 0;
    return app.terrainRect && currentTool(app) == TBrush && tilesStampOn() ? 3 : 0;
}
// A box of tiles (w x h, row by row) turned a quarter clockwise (back: the other way), or mirrored left and right
// (turn 0, upDown false) or top and bottom; w and h follow.
void turnTileBox(std::vector<uint16_t>& v, int& w, int& h, int turn, bool upDown) {
    if (w <= 0 || h <= 0 || v.size() != size_t(w) * size_t(h)) return;
    std::vector<uint16_t> out(v.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint16_t t = v[size_t(y) * size_t(w) + size_t(x)];
            if (turn > 0) out[size_t(x) * size_t(h) + size_t(h - 1 - y)] = t;        // clockwise: (x, y) -> (h - 1 - y, x)
            else if (turn < 0) out[size_t(w - 1 - x) * size_t(h) + size_t(y)] = t;   // back: (x, y) -> (y, w - 1 - x)
            else if (upDown) out[size_t(h - 1 - y) * size_t(w) + size_t(x)] = t;
            else out[size_t(y) * size_t(w) + size_t(w - 1 - x)] = t;
        }
    v = std::move(out);
    if (turn) std::swap(w, h);
}
void tilesStampTurned(int turn, bool upDown);  // (tiles_edit.inc: the Brush's stamp)
// R (back: Shift+R): what is on the cursor turns a quarter clockwise (back: the other way). Isometric terrain turns
// its corners (the edges are made again); tiles move to their turned places, each tile's picture as it is.
// M (Shift+M): it mirrors left and right (top and bottom).
void mapTurnOrMirror(AppState& app, int turn, bool upDown) {
    const int what = cursorTurnable(app);
    if (!what)
        return setMessage(app, app.mode == Mode::Terrain ? L"Nothing to turn: copy terrain (Ctrl+C), or pick custom terrain or copied tiles for the Brush"
                                                         : L"Nothing to turn: copy first, then paste (Ctrl+C, Ctrl+V)");
    if (what >= 4) {  // units, doodads, sprites: their places round the group's middle (their pictures do not turn)
        if (what == 4) {
            app.ed->turnUnitClipboard(turn, upDown);
        } else {
            for (Editor::ItemAt& it : what == 5 ? app.doodadClip : app.spriteClip) {
                const int x = it.x, y = it.y;
                it.x = turn > 0 ? -y : turn < 0 ? y : upDown ? x : -x, it.y = turn > 0 ? x : turn < 0 ? -x : upDown ? -y : y;
            }
        }
        setMessage(app, turn ? L"Turned the copy, experimental (R and Shift+R turn it, M and Shift+M mirror it)"
                             : upDown ? L"Mirrored the copy top and bottom" : L"Mirrored the copy left and right");
        app.dirty = true;
        return;
    }
    if (what == 1) {
        int dropped = 0;
        if (turn) {
            for (int k = turn > 0 ? 1 : 3; k > 0; --k) dropped += app.ed->rotateTerrainClipboard();
        } else {
            dropped = app.ed->mirrorTerrainClipboard(upDown);
        }
        shareTerrainCopy(*app.ed);
        const std::wstring done = turn ? L"Turned the terrain, experimental" : upDown ? L"Mirrored the terrain top and bottom" : L"Mirrored the terrain left and right";
        setMessage(app, dropped ? done + L"; its " + std::to_wstring(dropped) + L" doodads cannot turn, so they are not included"
                                : done + L" (R and Shift+R turn it, M and Shift+M mirror it)");
    } else if (what == 2) {
        Editor::TerrainCopies c = app.ed->terrainCopies();
        turnTileBox(c.tiles.values, c.tiles.width, c.tiles.height, turn, upDown);
        app.ed->setTerrainCopies(c);
        shareTerrainCopy(*app.ed);
    } else {
        tilesStampTurned(turn, upDown);
    }
    if (what != 1)
        setMessage(app, turn     ? L"Turned the tiles, experimental: each tile's picture stays as it is, so edges and cliffs may not fit"
                        : upDown ? L"Mirrored the tiles top and bottom: each tile's picture stays as it is, so edges and cliffs may not fit"
                                 : L"Mirrored the tiles left and right: each tile's picture stays as it is, so edges and cliffs may not fit");
    app.dirty = true;
}
void mapTurnPaste(AppState& app, bool back) { mapTurnOrMirror(app, back ? -1 : 1, false); }
void mapMirrorPaste(AppState& app, bool upDown) { mapTurnOrMirror(app, 0, upDown); }

// An arrow key (dx, dy: -1, 0 or 1): selected units move (1 pixel, Shift: 8, Ctrl: 16, with Options > Units > Snap to the grid a
// grid cell; a building that snaps to the tiles: a tile at least; a unit that does not fit there makes the move refused, unless "Units anywhere" is on); else the view moves.
// False: Ctrl and no units to move (the key is not the map's).
bool mapArrowKey(AppState& app, int dx, int dy, bool big, bool ctrl) {
    if (app.mode == Mode::Units && app.ed && !app.ed->selection().empty() && !app.pasting) {
        const int free = ctrl ? 16 : big ? 8 : 1;
        int dw = app.snapUnits ? gDock.gridW : free, dh = app.snapUnits ? gDock.gridH : free;
        // (a building on the tile grid moves a whole tile: a smaller step snaps back to the nearest tile edge, so up and
        // left did not move it at all, GitHub #227)
        if (app.snapBuildings)
            for (int i : app.ed->selection())
                if (i >= 0 && size_t(i) < app.ed->units().size() && app.ed->units()[size_t(i)].type < app.ed->unitTypes().size() &&
                    app.ed->unitTypes()[app.ed->units()[size_t(i)].type].is(UnitFlag::Building))
                    dw = (dw + 31) / 32 * 32, dh = (dh + 31) / 32 * 32;
        const int moved = app.ed->moveSelection(dx * dw, dy * dh, false);
        setMessage(app, moved ? L"Moved " + std::to_wstring(moved) + L" units" : L"The units do not fit there. They stay where they were.");
        return true;
    }
    if (ctrl) return false;
    const float step = 64 / app.zoom;
    app.camX += dx * step, app.camY += dy * step;
    return true;
}

// Alt + wheel on the map: the active brush (the terrain brush or spray, Ramp's brush, Bridge's width, the unit and doodad
// spray) one size up or down (brushSizes). False when no brush is active.
bool mapBrushActive(const AppState& app) {
    return (app.mode == Mode::Terrain && (currentTool() == TBrush || currentTool() == TSpray || isStrokeShape(currentTool()) ||
                                          currentTool() == TBridge || currentTool() == TRamp || (currentTool() == TJumble && !app.terrainRect))) ||
           objectSprayOn() || objectEraseOn() || (app.mode == Mode::Terrain && currentTool() == TErase);
}
bool mapBrushWheel(AppState& app, bool up) {
    if (!mapBrushActive(app)) return false;
    const int k = std::clamp(brushSizeIndex(app.brushSize) + (up ? 1 : -1), 0, int(brushSizes().size()) - 1);
    app.brushSize = brushSizes()[size_t(k)];
    shellToolsChanged();  // the size slider
    wchar_t m[48];
    std::swprintf(m, 48, L"Brush size %g", app.brushSize);
    setMessage(app, m);
    app.dirty = true;
    return true;
}

// --- The map's wheel, trackpad and pinch (one rule for every system) ----------------------------------------------
// The zoom changes by `factor` around the view point (x, y) (screen pixels): the map's point under it stays under it.
void mapZoomAt(AppState& app, float x, float y, float factor) {
    if (app.zoom <= 0 || !(factor > 0)) return;
    const float wx = app.camX + x / app.zoom, wy = app.camY + y / app.zoom;
    app.zoom = std::clamp(app.zoom * factor, 1.0f / 16, 4.0f);
    app.camX = wx - x / app.zoom, app.camY = wy - y / app.zoom;
    clampCamera();
    app.dirty = true;
}
// The map moves (dx, dy) screen pixels on the screen (a trackpad's two fingers: the map follows them).
void mapPanBy(AppState& app, float dx, float dy) {
    if (app.zoom <= 0) return;
    app.camX -= dx / app.zoom, app.camY -= dy / app.zoom;
    clampCamera();
    app.dirty = true;
}
// The wheel or a two-finger scroll on the map at (x, y). `notches`: the wheel's steps, positive away from the user
// (a trackpad's are fractions); `panX`, `panY`: how far the content moves in screen pixels, in the system's scroll
// direction (natural scrolling: it follows the fingers); `precise`: a trackpad's scroll, not a wheel's (only macOS
// tells them apart: on Windows and Linux each scroll is a wheel's, also a smooth wheel's fractions of a notch);
// `horizontal`: a wheel's sideways steps (a tilt wheel). The rules:
//  - Alt + wheel: the active brush one size up or down (mapBrushWheel); a trackpad's scroll and a smooth wheel's
//    fractions step once per notch. In Doodads mode with a doodad picked and no spray: the palette's next or previous
//    doodad (Discord feedback #119). With no brush, Alt + wheel does nothing (it never zooms).
//  - Ctrl (Cmd on macOS) + wheel or scroll: the zoom at the mouse (a trackpad's: smoothly; Windows sends a touchpad's
//    pinch as Ctrl + wheel).
//  - A mouse wheel: the zoom at the mouse (a sideways tilt does nothing).
//  - A trackpad's scroll (macOS): the map moves, on both axes. A pinch zooms on each system (mapPinch).
// True: the map took it.
bool mapWheel(AppState& app, float x, float y, float notches, float panX, float panY, bool precise, bool horizontal, bool ctrl, bool alt) {
    static float brushSteps = 0;  // a trackpad's or a smooth wheel's fractions until a whole step
    if (alt && app.mode == Mode::Doodads && !objectSprayOn() && app.palette >= 0 && !horizontal && notches != 0) {
        static float steps = 0;
        steps += notches;
        while (std::fabs(steps) >= 1) {
            const bool up = steps > 0;
            steps += up ? -1.0f : 1.0f;
            shellPaletteStep(up ? -1 : 1);  // (the wheel away: the one before, as a list scrolls)
        }
        return true;
    }
    if (alt) {
        if (!precise && notches == std::trunc(notches)) {
            if (!horizontal && notches != 0 && mapBrushWheel(app, notches > 0)) return true;
        } else if (!horizontal && mapBrushActive(app)) {
            brushSteps += notches;  // (the fractions before a whole step are the brush's too: the map does not move)
            while (std::fabs(brushSteps) >= 1) {
                const bool up = brushSteps > 0;
                brushSteps += up ? -1.0f : 1.0f;
                mapBrushWheel(app, up);
            }
            return true;
        }
        return true;  // (no brush: nothing; Alt + wheel never zooms)
    }
    if (ctrl || !precise) {
        if (horizontal || notches == 0) return false;
        // A notch: 1.2 times; a trackpad's scroll: 1.2 times each 4 of its steps (its steps are small and many). A
        // Windows touchpad's pinch comes as Ctrl and a wheel's fractions: as gentle as a trackpad's.
        const bool fraction = notches != std::trunc(notches);
        mapZoomAt(app, x, y, std::pow(1.2f, precise || (ctrl && fraction) ? notches / 4 : notches));
        return true;
    }
    if (panX == 0 && panY == 0) return false;
    mapPanBy(app, panX, panY);
    return true;
}
// macOS's smart zoom (a double tap with two fingers) on the map at (x, y): the zoom goes in two times around the point;
// the next one, while the zoom is still that, goes back to the zoom before. At the largest zoom it goes out two times.
void mapSmartZoom(AppState& app, float x, float y) {
    static float before = 0, after = 0;  // the zoom before the last smart zoom in, and the zoom it gave
    if (before > 0 && app.zoom == after) {
        mapZoomAt(app, x, y, before / app.zoom);
        before = after = 0;
        return;
    }
    const float was = app.zoom;
    mapZoomAt(app, x, y, 2);
    if (app.zoom == was) mapZoomAt(app, x, y, 0.5f), before = after = 0;  // (at the largest zoom: out)
    else before = was, after = app.zoom;
}
// Alt + pinch on the map: the active brush (as Alt + wheel) one size up when the fingers have moved apart by a step,
// down when together. The steps add up over the pinch's events (each one is a small change). False when no brush is
// active (the pinch zooms then).
bool mapBrushPinch(AppState& app, float scale) {
    static float gathered = 1;  // (what is left of the pinch since the last size: less than a step)
    constexpr float kStep = 1.15f;
    if (!(scale > 0) || !mapBrushActive(app)) return false;
    gathered = std::clamp(gathered * scale, 1 / (kStep * kStep), kStep * kStep);
    for (; gathered >= kStep; gathered /= kStep) mapBrushWheel(app, true);
    for (; gathered <= 1 / kStep; gathered *= kStep) mapBrushWheel(app, false);
    return true;
}

// A pinch on the map: the zoom changes by `scale` (more than 1: in) around the point between the fingers (x, y).
bool mapPinch(AppState& app, float x, float y, float scale) {
    if (!(scale > 0) || scale == 1) return false;
    mapZoomAt(app, x, y, scale);
    return true;
}
