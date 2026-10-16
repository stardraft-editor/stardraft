// The map editing helpers (the editor's portable core): the camera's world point, the spray, the stroke of placed units,
// doodads and sprites, the snap and the drag offsets, the selection and location edges, and the unit limit. The map
// input (map_input.inc) calls them; they moved here from main.cpp as they were.

// main.cpp and the other files define these. appState() is the app state: main.cpp's App is its AppState part.
AppState& appState();
int gUnitLimitClassic = 3400, gUnitLimitEud = 1700;  // the Map unit limit (Program options; saved in the settings)
// The unit limits that Program options saved (both programs, at start; not in tests).
void loadUnitLimits() {
    int64_t limit = 0;
    if (prefs::number(L"UnitLimitClassic", limit) && limit > 0) gUnitLimitClassic = int(std::min<int64_t>(limit, 65535));
    if (prefs::number(L"UnitLimitEud", limit) && limit > 0) gUnitLimitEud = int(std::min<int64_t>(limit, 65535));
}
bool classicMode();
void updateStatus();
std::vector<int> selectedPalette();
const std::vector<int>& paletteChosen();
void tilesSprayAt(float wx, float wy);  // (tiles_edit.inc)
float placeSpacing();
bool placePaletteUnit(float wx, float wy, bool correct);


void toWorld(int sx, int sy, float& wx, float& wy) {
    wx = appState().camX + sx / appState().zoom;
    wy = appState().camY + sy / appState().zoom;
}

int currentTool() { return appState().toolByMode[int(appState().mode)]; }

// After Undo or Redo: the selected doodads and sprites that are not on the map now (an Undo of a placed one) leave the
// selection, so that the status bar, Delete and a drag do not count them.
void dropGoneItems(AppState& app) {
    if (!app.ed) return;
    auto drop = [](std::vector<int>& sel, size_t n) {
        sel.erase(std::remove_if(sel.begin(), sel.end(), [n](int i) { return i < 0 || size_t(i) >= n; }), sel.end());
    };
    drop(app.selDoodads, app.ed->doodads().size());
    drop(app.selSprites, app.ed->sprites().size());
}

// The spray's density: the number of things one spray puts down in the brush (each tick of a held spray too), 1 to
// kDensityMax. Units or doodads (sprayCount; only where they fit, so fewer can land) and terrain dabs or tiles
// (sprayDabs). The defaults: 5 dabs for terrain, 10 units or doodads (about the earlier "medium").
constexpr int kDensityMax = 50;

int sprayCount() { return std::clamp(appState().sprayDensity, 1, kDensityMax); }

int sprayDabs() { return std::clamp(appState().sprayDensity, 1, kDensityMax); }

int sprayRadius() { return int(appState().brushSize * 64); }

Brush currentBrush() { return Brush{appState().brushShape, appState().brushSize, appState().mode == Mode::Terrain ? appState().brushHeight : 0.0f}; }

// The terrain selection's shape: the brush shape (diamond, square, circle) while it is made with Select.
Editor::SelectShape selectShape() {
    return appState().brushShape == Brush::Diamond ? Editor::SelectShape::Diamond : appState().brushShape == Brush::Circle ? Editor::SelectShape::Ellipse : Editor::SelectShape::Rect;
}

Rect previewLocationRect(float wx, float wy) {
    Rect r = appState().locStart;
    float dx = wx - appState().dragWX, dy = wy - appState().dragWY;
    auto sn = [](float v) { return int(snapRound(v, appState().locationGrid)); };
    if (appState().drag == Drag::LocCreate) {
        r = Rect{sn(appState().dragWX), sn(appState().dragWY), sn(wx), sn(wy)}.normalized();
    } else if (appState().drag == Drag::LocMove) {
        int mx = sn(r.left + dx) - r.left, my = sn(r.top + dy) - r.top;
        r = {r.left + mx, r.top + my, r.right + mx, r.bottom + my};
    } else if (appState().drag == Drag::LocResize) {
        if (appState().locEdges & 1) r.left = sn(r.left + dx);
        if (appState().locEdges & 2) r.top = sn(r.top + dy);
        if (appState().locEdges & 4) r.right = sn(r.right + dx);
        if (appState().locEdges & 8) r.bottom = sn(r.bottom + dy);
        r = r.normalized();
    }
    return r;
}

// Unit snap (Options > Units): placement in the middle of a grid cell, moves in whole cells.
// Buildings always snap to tile edges (snapToGrid), as in StarEdit.
void snapUnitPoint(float& x, float& y) { x = snapToCell(x, appState().unitGrid), y = snapToCell(y, appState().unitGrid); }

float snapUnitDelta(float d) { return snapRound(d, appState().unitGrid); }

// Would the selected units all be placeable moved by (dx, dy)?
bool selectionFitsAt(float dx, float dy) {
    Editor& ed = *appState().ed;
    const auto& units = ed.units();
    const std::vector<int> moving = ed.withLinkedAddons(ed.selection());  // a building's add-on goes with it
    std::vector<Unit> others;
    for (size_t i = 0; i < units.size(); ++i)
        if (std::find(moving.begin(), moving.end(), int(i)) == moving.end()) others.push_back(units[i]);
    const PlacementContext ctx = ed.placementContext(&others);
    for (int i : moving) {
        Unit m = units[size_t(i)];
        m.x = uint16_t(std::clamp(int(std::lround(m.x + dx)), 0, ed.width() * 32 - 1));
        m.y = uint16_t(std::clamp(int(std::lround(m.y + dy)), 0, ed.height() * 32 - 1));
        if (m.type < ed.unitTypes().size()) snapToGrid(ed.unitTypes()[m.type], m.x, m.y);
        if (!placementOk(ctx, m)) return false;
    }
    return true;
}

// Offset of a unit drag with the cursor at (wx, wy). With "snap back" on, a spot where the units do
// not fit holds them at the last spot along the drag where they did.
void dragMoveDelta(float wx, float wy, float& dx, float& dy) {
    dx = snapUnitDelta(wx - appState().dragWX), dy = snapUnitDelta(wy - appState().dragWY);
    if (!appState().dragSnapBack || appState().allowIllegalUnits) return;
    if (selectionFitsAt(dx, dy)) appState().lastValidDX = dx, appState().lastValidDY = dy;
    else dx = appState().lastValidDX, dy = appState().lastValidDY;
}

// A map sprite's box: where its picture is (its first frame; SD graphics), so a click on what shows selects it
// and the selection's box holds it (GitHub feedback #60). Nothing to measure: 32 x 32 around its point.
Rect mapSpriteBox(Editor& ed, const MapSprite& m) {
    UnitSprites& us = ed.unitSprites();
    if (m.flags & SpriteFlag::DrawAsSprite) {
        if (const Grp* g = us.grp(us.imageForSprite(m.id)); g && !g->frames.empty() && g->frames[0].width) {
            const Grp::Frame& f = g->frames[0];
            const int x0 = int(m.x) - g->width / 2 + f.x, y0 = int(m.y) - g->height / 2 + f.y;
            return {x0, y0, x0 + f.width, y0 + f.height};
        }
    } else if (const Sprite* sp = us.unitSprite(m.id, m.owner, ed.tileset()); sp && sp->width) {
        const int x0 = int(m.x) - sp->originX, y0 = int(m.y) - sp->originY;
        return {x0, y0, x0 + sp->width, y0 + sp->height};
    }
    return {int(m.x) - 16, int(m.y) - 16, int(m.x) + 16, int(m.y) + 16};
}

// The sprite under a map point: the smallest box that holds it (a doodad's overlay goes with its doodad), else the
// nearest within 24 pixels (as before).
int mapSpriteAt(Editor& ed, int x, int y) {
    int best = -1;
    long long bestArea = 1LL << 62;
    for (size_t i = 0; i < ed.sprites().size(); ++i) {
        const MapSprite& m = ed.sprites()[i];
        const Rect r = mapSpriteBox(ed, m);
        if (!r.contains(x, y)) continue;  // (first: the overlay test below goes through every doodad)
        bool overlay = false;
        for (const Doodad& d : ed.doodads())
            if (const DoodadType* t = ed.doodadTypes().find(d.id); t && t->overlaySprite == m.id && d.x == m.x && d.y == m.y) overlay = true;
        if (overlay) continue;
        const long long area = 1LL * (r.right - r.left) * (r.bottom - r.top);
        if (area < bestArea) best = int(i), bestArea = area;
    }
    return best >= 0 ? best : ed.spriteAt(x, y);
}

// The terrain selection being dragged from the press to (wx, wy). Rectangle: whole tiles, the tile of the press and the
// tile under the mouse both in it (GitHub feedback #112); Isometric: as dragged.
Rect terrainSelectRect(float wx, float wy) {
    Rect r = Rect{int(appState().dragWX), int(appState().dragWY), int(wx), int(wy)}.normalized();
    if (!appState().terrainRect || !appState().ed) return r;
    const int w = appState().ed->width(), h = appState().ed->height();
    const int x0 = std::clamp(r.left / 32, 0, w - 1), y0 = std::clamp(r.top / 32, 0, h - 1);
    const int x1 = std::clamp((r.right - 1) / 32, x0, w - 1), y1 = std::clamp((r.bottom - 1) / 32, y0, h - 1);
    return Rect{x0 * 32, y0 * 32, (x1 + 1) * 32, (y1 + 1) * 32};
}

int locationEdgesAt(float wx, float wy) {
    Editor& ed = *appState().ed;
    int i = ed.selectedLocation();
    if (!ed.locationUsed(i)) return 0;
    Rect r = ed.locationRect(i);
    float tol = 6 / appState().zoom;
    if (wx < r.left - tol || wx > r.right + tol || wy < r.top - tol || wy > r.bottom + tol) return 0;
    int e = 0;
    if (std::abs(wx - r.left) <= tol) e |= 1;
    if (std::abs(wy - r.top) <= tol) e |= 2;
    if (std::abs(wx - r.right) <= tol) e |= 4;
    if (std::abs(wy - r.bottom) <= tol) e |= 8;
    return e;
}


// Program options > Map unit limit, into the editor before units go into the map (standard maps; EUD projects).
int applyUnitLimit(Editor& ed) {
    const int n = classicMode() ? gUnitLimitClassic : gUnitLimitEud;
    ed.setUnitLimit(n);
    return n;
}

bool atUnitLimit(const Editor& ed) { return ed.unitLimit() > 0 && int(ed.units().size()) >= ed.unitLimit(); }

std::wstring unitLimitText(const Editor& ed) {
    return L"The map has " + std::to_wstring(ed.unitLimit()) + L" units: the limit (Program options > Map unit limit)";
}

// Moves the selected doodads by (dx, dy) by the same rule: with Move to the nearest place where it fits, a move that does
// not fit goes to the nearest offset (in whole tiles) where all of them fit.
bool moveSelectedDoodads(int dx, int dy, std::string& why) {
    Editor& ed = *appState().ed;
    if (ed.moveDoodads(appState().selDoodads, dx, dy, appState().doodadCheck, &why)) return true;
    if (!appState().doodadNearest) return false;
    std::vector<std::pair<int, int>> offsets;  // nearest first
    for (int oy = -4; oy <= 4; ++oy)
        for (int ox = -4; ox <= 4; ++ox)
            if ((ox || oy) && ox * ox + oy * oy <= 16) offsets.push_back({ox, oy});
    std::stable_sort(offsets.begin(), offsets.end(),
                     [](const auto& a, const auto& b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
    std::string ignored;
    for (auto [ox, oy] : offsets)
        if (ed.moveDoodads(appState().selDoodads, dx + ox * 32, dy + oy * 32, appState().doodadCheck, &ignored)) return true;
    return false;
}

// The box a doodad or sprite of the palette takes at (x, y): the next one in a drag goes where it does not touch it.
RECT strokeBox(float x, float y) {
    int w = 32, h = 32;
    if (appState().mode == Mode::Doodads) {
        if (const DoodadType* d = appState().ed->doodadTypes().find(uint16_t(appState().palette))) w = std::max(1, int(d->width)) * 32, h = std::max(1, int(d->height)) * 32;
    } else if (appState().mode == Mode::Sprites) {
        const int image = appState().ed->unitSprites().imageForSprite(uint16_t(appState().palette));
        if (const Grp* g = image >= 0 ? appState().ed->unitSprites().grp(image) : nullptr; g && g->width) w = std::max(16, int(g->width) / 2), h = std::max(16, int(g->height) / 2);
    }
    return {LONG(x) - w / 2, LONG(y) - h / 2, LONG(x) + (w + 1) / 2, LONG(y) + (h + 1) / 2};
}

// One place in a drag (units, doodads, sprites): true when something went down there.
bool strokePlace(float x, float y) {
    Editor& ed = *appState().ed;
    if (appState().mode == Mode::Units) return placePaletteUnit(x, y, false);  // in a drag: only where it fits, under the mouse
    const RECT box = strokeBox(x, y);
    RECT overlap;
    if (appState().mode == Mode::Sprites) {  // sprites: one at each new place of the mouse (feedback #54, #78), with no gaps
        const int px = int(snapToCell(x, appState().spriteGrid)), py = int(snapToCell(y, appState().spriteGrid));
        if (appState().placedInStroke && px == appState().lastPlacedBox.left && py == appState().lastPlacedBox.top) return false;  // the same place again
        const bool placed = ed.placeSprite(uint16_t(appState().palette), px, py, uint8_t(appState().playerOwner));
        if (placed) {
            appState().lastPlacedBox = {px, py, px, py};
            if (paletteChosen().size() > 1) {  // several picked: the next one is one of them, at random
                static std::mt19937 rng{std::random_device{}()};
                appState().palette = paletteChosen()[std::uniform_int_distribution<size_t>(0, paletteChosen().size() - 1)(rng)];
            }
        }
        return placed;
    }
    if (appState().placedInStroke && IntersectRect(&overlap, &box, &appState().lastPlacedBox)) return false;  // still on the last one
    bool placed = false;
    if (appState().mode == Mode::Doodads) {
        placed = ed.placeDoodad(uint16_t(appState().palette), int(x), int(y), appState().doodadCheck);
        if (placed && paletteChosen().size() > 1) {  // several picked: each one placed is one of them, at random
            static std::mt19937 rng{std::random_device{}()};
            appState().palette = paletteChosen()[std::uniform_int_distribution<size_t>(0, paletteChosen().size() - 1)(rng)];
        }
    } else if (appState().mode == Mode::Sprites) {
        placed = ed.placeSprite(uint16_t(appState().palette), int(snapToCell(x, appState().spriteGrid)), int(snapToCell(y, appState().spriteGrid)), uint8_t(appState().playerOwner));
    }
    if (placed) appState().lastPlacedBox = box;
    return placed;
}

// While the mouse is down with something picked: along the path from the last one placed, place the next one as soon
// as there is room for it (units: their placement size apart, and only where they fit; doodads and sprites: where
// they do not touch the last one). A place that does not fit now is tried again as the mouse moves on.
void strokeMove(float wx, float wy) {
    if (appState().mode == Mode::Sprites) {  // sprites: one at each place on the path, one pixel (or one snap cell) apart (#78)
        const float step = float(std::max(1, appState().spriteGrid));
        const float dx = wx - appState().lastPlaceX, dy = wy - appState().lastPlaceY, len = std::hypot(dx, dy);
        if (!appState().placedInStroke || len < step) {
            if (strokePlace(wx, wy)) ++appState().placedInStroke;
            if (len >= step || !appState().placedInStroke) appState().lastPlaceX = wx, appState().lastPlaceY = wy;
            return;
        }
        const int n = int(std::ceil(len / step));  // (each step at most one pixel or one cell)
        for (int k = 1; k <= n; ++k)  // the places between the last mouse position and this one (a fast drag skips none)
            if (strokePlace(appState().lastPlaceX + dx * k / n, appState().lastPlaceY + dy * k / n)) ++appState().placedInStroke;
        appState().lastPlaceX = wx, appState().lastPlaceY = wy;
        return;
    }
    const float step = appState().mode == Mode::Units && (appState().allowStacking || appState().allowIllegalUnits) ? placeSpacing() : 8.0f;
    float dx = wx - appState().lastPlaceX, dy = wy - appState().lastPlaceY, len = std::hypot(dx, dy);
    if (len < step) return;
    const float ux = dx / len, uy = dy / len;
    for (float d = step; d <= len + 0.01f; d += step) {
        const float x = appState().lastPlaceX + ux * d, y = appState().lastPlaceY + uy * d;
        if (strokePlace(x, y)) {
            ++appState().placedInStroke;
            appState().lastPlaceX = x, appState().lastPlaceY = y, len = std::hypot(wx - x, wy - y), d = 0;
            if (len < step) break;
            dx = wx - x, dy = wy - y;
            const float l = std::hypot(dx, dy);
            if (l <= 0) break;
            // (the direction stays: the rest of the path from here)
        }
    }
    if (appState().mode != Mode::Units) appState().lastPlaceX = wx, appState().lastPlaceY = wy;  // boxes decide the room: follow the mouse
}

// Distance between units placed while dragging: the unit's footprint; with a snap, a tile (32 px snap) or the footprint
// in whole grid cells.
float placeSpacing() {
    if (appState().unitGrid == 32) return 32;
    const UnitType& t = appState().ed->unitTypes()[uint16_t(appState().palette)];
    const int foot = std::max({int(t.placementWidth), int(t.placementHeight), 16});
    return appState().unitGrid > 0 ? float((foot + appState().unitGrid - 1) / appState().unitGrid * appState().unitGrid) : float(foot);
}

// Places the palette unit at a map point (grid-snapped if on, corrected if on) and reports it.
bool placePaletteUnit(float wx, float wy, bool correct) {
    Editor& ed = *appState().ed;
    uint16_t type = uint16_t(appState().palette);
    const UnitType& t = ed.unitTypes()[type];
    uint8_t who = (t.availability & UnitAvail::NonNeutral) ? uint8_t(appState().playerOwner) : uint8_t(11);
    snapUnitPoint(wx, wy);
    applyUnitLimit(ed);
    PlaceResult r = ed.placeUnit(type, who, int(wx), int(wy), correct);
    ed.clearSelection();  // placed units are not selected (placing is not selecting)
    if (r.placed) {
        uint16_t ux = uint16_t(std::clamp(int(wx), 0, ed.width() * 32 - 1)), uy = uint16_t(std::clamp(int(wy), 0, ed.height() * 32 - 1));
        snapToGrid(t, ux, uy);
        bool moved = r.unit.x != ux || r.unit.y != uy;
        setMessage(L"Placed " + widen(t.name) + (moved ? L" (moved to the nearest valid spot)" : L""));
    } else {
        std::wstring why;
        for (auto is : r.issues) why += L" " + widen(placementIssueName(is));
        setMessage(atUnitLimit(ed) ? unitLimitText(ed) : L"Cannot place here:" + why + (correct ? L" (no valid spot nearby)" : L""));
    }
    return r.placed;
}

// One spray at a map point with the active mode's Spray: terrain (ISOM; in Rectangle mode, tiles), units or doodads.
// Returns the units or doodads it placed.
int sprayAt(float wx, float wy) {
    if (!appState().ed) return 0;
    Editor& ed = *appState().ed;
    if (appState().mode == Mode::Terrain) {
        if (appState().terrainRect) tilesSprayAt(wx, wy);
        else if (appState().palette > 0) ed.sprayTerrain(int(wx), int(wy), uint16_t(appState().palette), currentBrush(), sprayDabs(), appState().spraySeed++);
        return 0;
    }
    std::vector<uint16_t> picks;
    for (int v : selectedPalette()) picks.push_back(uint16_t(v));
    if (appState().mode == Mode::Units) {
        applyUnitLimit(ed);
        const int n = ed.sprayUnits(picks, uint8_t(appState().playerOwner), int(wx), int(wy), sprayRadius(), sprayCount(), appState().spraySeed++);
        ed.clearSelection();  // placed units are not selected (placing is not selecting)
        if (atUnitLimit(ed)) setMessage(unitLimitText(ed));
        return n;
    }
    if (appState().mode == Mode::Doodads) return ed.sprayDoodads(picks, int(wx), int(wy), sprayRadius(), sprayCount(), appState().spraySeed++, appState().doodadCheck);
    if (appState().mode == Mode::Sprites && !picks.empty()) {  // random sprites of the selected kinds in the circle
        std::mt19937 rng(appState().spraySeed++);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const float r = float(sprayRadius());
        int n = 0;
        for (int k = 0; k < sprayCount(); ++k) {
            const float a = unit(rng) * 6.2831853f, d = r * std::sqrt(unit(rng));
            const uint16_t id = picks[size_t(rng() % picks.size())];
            n += ed.placeSprite(id, int(snapToCell(wx + d * std::cos(a), appState().spriteGrid)), int(snapToCell(wy + d * std::sin(a), appState().spriteGrid)),
                                uint8_t(appState().playerOwner));
        }
        return n;
    }
    return 0;
}

// View > Layers: the active mode's layer is hidden. Its map edits are refused (nothing changes that does not show).
bool modeLayerHidden() {
    switch (appState().mode) {
    case Mode::Terrain: return !gLayers.terrain;
    case Mode::Units: return !gLayers.units;
    case Mode::Doodads: return !gLayers.doodads;
    case Mode::Sprites: return !gLayers.sprites;
    case Mode::Locations: return !appState().showLocations;
    }
    return false;
}

void modeLayerHiddenMessage() {
    setMessage(std::wstring(kModeNames[int(appState().mode)]) + L": this layer is hidden. View > Layers shows it again (then you can edit it).");
}

// Options: the most units a map may have (GitHub feedback #103): standard maps, and maps of EUD projects.

void outline(render::Scene& s, const Rect& r, uint32_t rgba, float thickness = 1, float dx = 0, float dy = 0) {
    s.rects.push_back({r.left + dx, r.top + dy, r.right + dx, r.bottom + dy, rgba, false, thickness, true});
}

void viewSize(int& w, int& h) { shellViewSize(w, h); }

// A status-bar hint (what is under the mouse): not worth a log line.
void setHint(const std::wstring& m) {
    appState().message = m;
    updateStatus();
}

