// The map view's mouse input (the editor's portable core): a click, a drag, a move and a release on the map, with the
// spray held down and the pipette. The Windows view (viewProc in main.cpp) only translates its messages into these calls
// (winMods gives the modifier keys). Included by core.inc; main.cpp defines the helpers declared below.

// main.cpp and its included files define these (the Win32 TU), until they move into the core themselves.
bool objectSprayOn();
int mapSpriteAt(Editor& ed, int x, int y);
void snapUnitPoint(float& x, float& y);
int applyUnitLimit(Editor& ed);
bool atUnitLimit(const Editor& ed);
std::wstring unitLimitText(const Editor& ed);
bool trigUnitPickClick(float wx, float wy);
int sprayAt(float wx, float wy);
void tilesMouseMove(float wx, float wy);
void tilesMouseUp(float wx, float wy);
bool tilesMouseDown(float wx, float wy, unsigned mods);
void tilesPick(int value, bool fromMap);
void tilesReveal(int value);
void tilesFill(int tx, int ty, bool every);
void pathOverlayMouseMove(float wx, float wy);
Rect previewLocationRect(float wx, float wy);
RECT strokeBox(float x, float y);
void strokeMove(float wx, float wy);
void setListSel(int i);
void showUnitProperties(int index);
void showLocationEdit(int loc);
bool inSelection();
bool secondPaints();
void cancelStep();
int sprayRadius();
int brushSizeIndex(float size);
const std::vector<float>& brushSizes();
int sprayDabs();
void dragMoveDelta(float wx, float wy, float& dx, float& dy);
Rect terrainSelectRect(float wx, float wy);
bool moveSelectedDoodads(int dx, int dy, std::string& why);
int locationEdgesAt(float wx, float wy);
bool placePaletteUnit(float wx, float wy, bool correct);
bool strokePlace(float x, float y);
std::wstring entryName(const std::string& dat, int v);
std::wstring unitShortName(const UnitType& t);
// The palette's picked values (Ctrl+click): a doodad or sprite drag places one of them at random (the palette's own list).

// The view stays near the map: at most half a view past each edge; a map smaller than the view keeps its middle in
// the view's middle on that axis. Every way the camera moves (drag, wheel, minimap, keys) passes here each frame.
void clampCamera() {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen() || app.zoom <= 0) return;
    int viewW = 0, viewH = 0;
    shellViewSize(viewW, viewH);
    const float vw = viewW / app.zoom, vh = viewH / app.zoom, mw = app.ed->width() * 32.0f, mh = app.ed->height() * 32.0f;
    auto clampAxis = [](float& cam, float view, float map) {
        if (map < view) {  // all of the map fits: its middle stays inside the view
            cam = std::clamp(cam, map / 2 - view, map / 2);
            return;
        }
        cam = std::clamp(cam, -view / 2, map - view / 2);
    };
    clampAxis(app.camX, vw, mw);
    clampAxis(app.camY, vh, mh);
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

// Isometric terrain: Shift with the left button paints the second terrain (Ctrl+click in the palette) with the Brush,
// Spray, Fill or Replace tool, as the left button alone paints the first. The Win32 view keeps the first terrain
// during such a stroke (gSecondStrokeSaved).
bool secondPaints() {
    AppState& app = appState();
    const int tool = currentTool(app);
    return app.ed && app.mode == Mode::Terrain && !app.terrainRect && app.palette2 > 0 && (tool == TBrush || tool == TSpray || tool == TFill || tool == TReplace);
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
        if (app.terrainRect) {
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
            if (un.owner < 12) setPlayer(app, un.owner);
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
            app.locationClip = ed.locationRect(l), app.locationClipName = ed.locationName(l), app.locationClipInverted = ed.locationInvertedAxes(l),
            app.locationClipElevation = ed.locationElevation(l);
            app.hasLocationClip = true;
            setMessage(app, L"Copied " + widen(ed.locationName(l)) + L": Ctrl+V pastes a copy where the mouse is");
            return true;
        }
        return false;
    }
    return false;
}

void mapMouseDown(AppState& app, int sx, int sy, unsigned mods) {
    if (!app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    if ((mods & draw::kAlt) && pipette(app, wx, wy)) {  // Alt+click: the pipette
        app.dirty = true;
        return;
    }
    if (modeLayerHidden()) return modeLayerHiddenMessage();
    if (trigUnitPickClick(wx, wy)) return;
    if (app.pasting) {
        // Paste mode stays on (another click pastes again) until a right click or Escape; the pasted
        // units are not selected, so the next paste does not move them.
        float pwx = wx, pwy = wy;
        snapUnitPoint(pwx, pwy);  // Options > Units snap: the middle of the copy in the middle of a grid cell
        applyUnitLimit(ed);
        const int pasted = ed.paste(int(pwx), int(pwy), app.correction), copied = int(ed.clipboard().size());
        setMessage(app, pasted < copied && atUnitLimit(ed) ? unitLimitText(ed)
                   : (pasted < copied ? L"Pasted " + std::to_wstring(pasted) + L" of " + std::to_wstring(copied) + L" units (the other units do not fit here)"
                                      : L"Pasted " + std::to_wstring(pasted) + L" units") + L". To stop, right-click or press Esc.");
        ed.clearSelection();
        app.drag = Drag::None;
        app.dirty = true;
        return;
    }
    app.dragWX = wx, app.dragWY = wy, app.dragSX = sx, app.dragSY = sy, app.dragMoved = false;
    bool shift = mods & draw::kShift, ctrl = mods & draw::kCtrl;
    switch (app.mode) {
    case Mode::Units: {
        int hit = ed.unitAt(int(wx), int(wy));
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
            if (ctrl) ed.toggleSelected(hit);
            else if (!ed.isSelected(hit)) ed.select(hit, shift);
            app.drag = ed.isSelected(hit) ? Drag::MoveUnits : Drag::None;
            app.lastValidDX = app.lastValidDY = 0;
        } else {
            app.drag = Drag::BoxSelect;
        }
        break;
    }
    case Mode::Terrain: {
        if (app.terrainRect) {  // Rectangle: tiles as they are, no ISOM needed
            tilesMouseDown(wx, wy, mods);
            if (app.drag == Drag::Spray) app.lastSprayX = sx, app.lastSprayY = sy;
            if (app.drag == Drag::Spray) sprayHoldStart(app, sx, sy);
            break;
        }
        const bool shiftDown = (mods & draw::kShift) != 0;
        if (!ed.canPaintTerrain()) break;
        if (ctrl) {
            // Eyedropper: pick the terrain under the cursor as the brush terrain.
            uint16_t t = ed.terrainAt(int(wx), int(wy));
            if (selectPalette(app, t) < 0 && shellClearFilter()) {  // hidden by the filter: clear it and look again
                rebuildList(app);
                selectPalette(app, t);
            }
            const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
            // Placement: from Select (or a paste), the Brush paints with it; Spray, Fill and Replace keep their tool.
            if (currentTool(app) == TSelect) app.toolByMode[int(Mode::Terrain)] = TBrush, app.terrainPasting = false;
            app.palette = t;
            fillToolCombo(app), updateStatus(app);
            setMessage(app, L"Painting " + widen(table ? table->terrainName(t) : "") + L" (selected with Ctrl+click)");
            app.drag = Drag::None;
            break;
        }
        int tool = currentTool(app);
        if (tool == TSelect) {
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
        if (app.palette <= 0) break;
        uint16_t t = uint16_t(app.palette);
        if (tool == TBrush) {
            // Painting a terrain onto itself redraws its tiles with new variants (a click, and on along the drag); the
            // stroke is one undo step.
            ed.beginTerrainStroke();
            if (ed.brushAllTerrain(int(wx), int(wy), currentBrush(app), t)) {
                int n = ed.rerollTerrain(int(wx), int(wy), currentBrush(app), app.spraySeed++);
                setMessage(app, L"New variants for " + std::to_wstring(n) + L" tiles");
            }
            gRerollX = wx, gRerollY = wy;
            ed.paintBrush(int(wx), int(wy), t, currentBrush(app));
            gPaintX = wx, gPaintY = wy;
            app.drag = Drag::Paint;
        } else if (tool == TSpray) {
            ed.beginTerrainStroke();
            ed.sprayTerrain(int(wx), int(wy), t, currentBrush(app), sprayDabs(), app.spraySeed++);
            app.lastSprayX = sx, app.lastSprayY = sy;
            app.drag = Drag::Spray;
            sprayHoldStart(app, sx, sy);
        } else if (tool == TFill && !shiftDown) {
            if (ed.fillTerrain(int(wx), int(wy), t)) setMessage(app, L"Filled with " + itemLabel(app, app.palette));
        } else if (tool == TReplace || tool == TFill) {  // Fill with Shift: every place of that terrain
            uint16_t from = ed.terrainAt(int(wx), int(wy));
            const Rect* area = app.hasTerrainSel ? &app.terrainSel : nullptr;
            int n = ed.replaceTerrain(from, t, area, int(selectShape(app)));  // (in the selection's shape)
            const IsomTerrainTable* tt = IsomTerrainTable::forEra(ed.era());
            setMessage(app, L"Replaced " + widen(tt ? tt->terrainName(from) : "") + L" with " + itemLabel(app, app.palette) + L" (" +
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
            for (auto& it : items) it.x += int(snapToCell(wx, app.spriteGrid)), it.y += int(snapToCell(wy, app.spriteGrid));
            const int n = ed.placeSprites(items);
            setMessage(app, L"Pasted " + std::to_wstring(n) + L" sprites");
            // (the copy stays on the mouse: click again to paste again; right-click or Esc stops; GitHub feedback #123)
        } else {
            selectItemAt(app, app.selSprites, mapSpriteAt(ed, int(wx), int(wy)), mods, Drag::MoveSprite);
        }
        break;
    case Mode::Doodads:
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
            std::vector<Editor::ItemAt> items = app.doodadClip;
            for (auto& it : items) it.x += int(wx), it.y += int(wy);
            const int n = ed.placeDoodads(items, app.doodadCheck, app.doodadNearest);
            setMessage(app, L"Pasted " + std::to_wstring(n) + L" of " + std::to_wstring(items.size()) + L" doodads (the others do not fit there)");
            // (the copy stays on the mouse: click again to paste again; right-click or Esc stops; GitHub feedback #123)
        } else {
            // Nothing picked: select (Ctrl or Shift adds), drag to move, a drag on empty ground selects with a box.
            selectItemAt(app, app.selDoodads, ed.doodadAt(int(wx), int(wy)), mods, Drag::MoveDoodad);
        }
        break;
    case Mode::Locations: {
        int edges = locationEdgesAt(wx, wy);
        int inside = ed.locationAt(int(wx), int(wy));
        if (edges) {
            app.drag = Drag::LocResize;
            app.locEdges = edges;
            app.locStart = ed.locationRect(ed.selectedLocation());
        } else if (inside >= 0 && !shift) {
            ed.selectLocation(inside);
            app.locStart = ed.locationRect(inside);
            app.drag = Drag::LocMove;
            setListSel(int(std::find(app.items.begin(), app.items.end(), inside) - app.items.begin()));
        } else {
            app.drag = Drag::LocCreate;
        }
        break;
    }
    }
    app.dirty = true;
}

void mapMouseMove(AppState& app, int sx, int sy) {
    app.mouseX = sx, app.mouseY = sy;
    if (!app.mouseIn) {
        app.mouseIn = true;
        shellTrackMouseLeave();
    }
    if (std::abs(sx - app.dragSX) + std::abs(sy - app.dragSY) > 4) app.dragMoved = true;
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    if (app.drag == Drag::Paint && app.ed) {
        // Over the same terrain: new variants each half brush along the drag (not on every small move).
        const float extent = std::min(app.brushSize, currentBrush(app).height());  // a flat brush: its smaller side
        if (std::hypot(wx - gRerollX, wy - gRerollY) >= std::max(16.0f, extent * 32) &&
            app.ed->brushAllTerrain(int(wx), int(wy), currentBrush(app), uint16_t(app.palette))) {
            app.ed->rerollTerrain(int(wx), int(wy), currentBrush(app), app.spraySeed++);
            gRerollX = wx, gRerollY = wy;
        }
        // A fast move skips ground: paint the way from the last point too, at most half a brush apart, and at most 8
        // more steps a move (zoomed far out, one move crosses much of the map; the next moves paint the rest).
        const float step = std::max(32.0f, extent * 32), dx = wx - gPaintX, dy = wy - gPaintY, len = std::hypot(dx, dy);
        const int n = std::clamp(int(len / step), 0, 8);
        for (int k = 1; k <= n; ++k)
            app.ed->paintBrush(int(gPaintX + dx * k / (n + 1)), int(gPaintY + dy * k / (n + 1)), uint16_t(app.palette), currentBrush(app));
        app.ed->paintBrush(int(wx), int(wy), uint16_t(app.palette), currentBrush(app));
        gPaintX = wx, gPaintY = wy;
    }
    if (app.drag == Drag::Tiles) tilesMouseMove(wx, wy);
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
    app.dirty = true;
    updateStatus(app);
    if (app.ed && app.drag == Drag::None) pathOverlayMouseMove(wx, wy);
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
    switch (app.drag) {
    case Drag::MoveUnits:
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
            setMessage(app, L"Placed " + widen(t.name) + (moved ? L" (moved to the nearest valid spot)" : L""));
        } else {
            std::wstring why;
            for (auto is : r.issues) why += L" " + widen(placementIssueName(is));
            setMessage(app, atUnitLimit(ed) ? unitLimitText(ed) : L"Cannot place here:" + why + (app.correction ? L" (no valid spot nearby)" : L""));
        }
        break;
    }
    case Drag::Paint: ed.endTerrainStroke(); break;
    case Drag::Spray:
        sprayHoldStop();
        if (app.mode == Mode::Terrain && app.terrainRect) tilesMouseUp(wx, wy);
        else ed.endStroke();  // the terrain's stroke, or the units' and doodads' (the press and hold: one undo step)
        if (app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites)
            setMessage(app, L"Sprayed " + std::to_wstring(app.sprayPlaced) + (app.mode == Mode::Units ? L" units" : app.mode == Mode::Doodads ? L" doodads" : L" sprites"));
        break;
    case Drag::Tiles: tilesMouseUp(wx, wy); break;
    case Drag::TerrainSelect: {
        const Rect r = app.terrainRect && !app.dragMoved ? Rect{} : terrainSelectRect(wx, wy);  // Rectangle: a click clears it
        app.hasTerrainSel = r.right - r.left >= 32 && r.bottom - r.top >= 32;
        app.terrainSel = r;
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
        } else {
            ed.selectLocation(-1);
        }
        break;
    }
    case Drag::LocMove:
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
            if (ed.moveSprites(app.selSprites, int(snapRound(wx - app.dragWX, app.spriteGrid)), int(snapRound(wy - app.dragWY, app.spriteGrid))))
                setMessage(app, L"Moved " + std::to_wstring(app.selSprites.size()) + L" sprites");
            else setMessage(app, L"Could not move the sprites: the new place is outside the map");
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
// a press would move (and during that move), else the arrow.
bool trigUnitPickActive();
stardraft::draw::Cursor mapCursorAt(AppState& app, int x, int y) {
    using stardraft::draw::Cursor;
    if (app.drag == Drag::Pan || app.drag == Drag::AutoPan) return Cursor::SizeAll;
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
    bool stroke = false;
    if (app.ed && (app.drag == Drag::PlaceStroke || app.drag == Drag::Spray || app.drag == Drag::PasteStroke || app.drag == Drag::Paint)) {
        sprayHoldStop();
        app.ed->endStroke();  // the press so far is one undo step; later edits are steps of their own
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

// R (back: Shift+R) while terrain is about to be pasted: the copy turns clockwise (back: the other way). Tiles
// (Rectangle) do not turn.
void mapTurnPaste(AppState& app, bool back) {
    if (!app.terrainPasting || app.terrainRect || !app.ed || !app.ed->hasTerrainClipboard()) return;
    int dropped = 0;
    for (int k = back ? 3 : 1; k > 0; --k) dropped += app.ed->rotateTerrainClipboard();
    setMessage(app, dropped ? L"Turned the terrain; its " + std::to_wstring(dropped) + L" doodads cannot turn, so they are not included"
                            : L"Turned the terrain (R turns clockwise, Shift+R turns back)");
}

// An arrow key (dx, dy: -1, 0 or 1): selected units move (1 pixel, big: 8, with Options > Units snap a grid cell; a
// unit that does not fit there makes the move refused, unless "Units anywhere" is on); else the view moves.
void mapArrowKey(AppState& app, int dx, int dy, bool big) {
    if (app.mode == Mode::Units && app.ed && !app.ed->selection().empty() && !app.pasting) {
        const int d = app.unitGrid ? app.unitGrid : big ? 8 : 1;
        const int moved = app.ed->moveSelection(dx * d, dy * d, false);
        setMessage(app, moved ? L"Moved " + std::to_wstring(moved) + L" units" : L"The units do not fit there. They stay where they were.");
        return;
    }
    const float step = 64 / app.zoom;
    app.camX += dx * step, app.camY += dy * step;
}

// Alt + wheel on the map: the active brush (the terrain brush or spray, the unit and doodad spray) one size up or down
// (brushSizes). False when no brush is active (the wheel zooms then).
bool mapBrushWheel(AppState& app, bool up) {
    const bool brushActive = (app.mode == Mode::Terrain && (currentTool() == TBrush || currentTool() == TSpray)) || objectSprayOn();
    if (!brushActive) return false;
    const int k = std::clamp(brushSizeIndex(app.brushSize) + (up ? 1 : -1), 0, int(brushSizes().size()) - 1);
    app.brushSize = brushSizes()[size_t(k)];
    shellToolsChanged();  // the size slider
    wchar_t m[48];
    std::swprintf(m, 48, L"Brush size %g", app.brushSize);
    setMessage(app, m);
    app.dirty = true;
    return true;
}
