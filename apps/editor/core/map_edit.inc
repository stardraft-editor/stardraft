// The map editing helpers (the editor's portable core): the camera's world point, the spray, the stroke of placed units,
// doodads and sprites, the snap and the drag offsets, the selection and location edges, and the unit limit. The map
// input (map_input.inc) calls them; they moved here from main.cpp as they were.

// The program (main_sdl.cpp and its sdl_*.inc files) defines these. appState() is the app state (shell_sdl.inc).
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

// After an undo or redo: the doodads and sprites that were selected (`doodads`, `sprites`: the map's before it) stay
// selected where the same ones are now (the same kind, place and owner); with as many as before, one of the same kind
// and owner in its place (a move taken back) stays too. One that is not there is no longer selected, so a selection
// never points to another doodad or sprite.
void keepItemSelection(AppState& app, const std::vector<Doodad>& doodads, const std::vector<MapSprite>& sprites) {
    if (!app.ed) return;
    auto keep = [](std::vector<int>& sel, const auto& before, const auto& now, auto same) {
        std::vector<int> out;
        auto free = [&out](int j) { return std::find(out.begin(), out.end(), j) == out.end(); };
        for (int i : sel) {
            if (i < 0 || size_t(i) >= before.size()) continue;
            const auto& was = before[size_t(i)];
            int at = -1;
            if (size_t(i) < now.size() && same(now[size_t(i)], was, true)) at = i;
            for (size_t j = 0; at < 0 && j < now.size(); ++j)
                if (same(now[j], was, true) && free(int(j))) at = int(j);
            if (at < 0 && now.size() == before.size() && size_t(i) < now.size() && same(now[size_t(i)], was, false)) at = i;
            if (at >= 0 && free(at)) out.push_back(at);
        }
        sel = std::move(out);
    };
    keep(app.selDoodads, doodads, app.ed->doodads(), [](const Doodad& a, const Doodad& b, bool place) {
        return a.id == b.id && a.owner == b.owner && (!place || (a.x == b.x && a.y == b.y));
    });
    keep(app.selSprites, sprites, app.ed->sprites(), [](const MapSprite& a, const MapSprite& b, bool place) {
        return a.id == b.id && a.owner == b.owner && a.flags == b.flags && (!place || (a.x == b.x && a.y == b.y));
    });
}

// The spray's density: how much of the brush one spray puts down (each tick of a held spray too), 1 to 100%. Terrain:
// that part of the brush's diamonds, tiles or rects (sprayShare), each once. Units, doodads and sprites: that part of the
// brush's tiles, one thing for each (sprayCount; only where they fit, so fewer can land). The defaults: 25% for terrain,
// 10% for the others.
constexpr int kDensityMax = 100;

// `whole` things of the brush at the density: at least one.
int sprayShare(size_t whole) { return std::max(1, int(std::ceil(double(whole) * std::clamp(appState().sprayDensity, 1, kDensityMax) / 100.0))); }

int sprayRadius();
int sprayCount() {
    const double tiles = 3.14159265 * sprayRadius() * sprayRadius() / (32.0 * 32.0);
    return std::min(sprayShare(size_t(std::max(1.0, tiles))), 500);
}

int sprayRadius() { return int(appState().brushSize * 64); }

Brush currentBrush() { return currentBrush(appState()); }

// The terrain selection's shape: the Select tool's own (palette_tools.inc).
Editor::SelectShape selectShape() { return selectShape(appState()); }

// The grid of View > Grid: what Options > Units, Sprites and Locations > Snap to the grid use.
float snapGridX(float v, bool on) { return on ? snapRound(v, gDock.gridW, gDock.gridX) : v; }
float snapGridY(float v, bool on) { return on ? snapRound(v, gDock.gridH, gDock.gridY) : v; }

// A location's edge (Options > Locations > Snap to the grid): the nearest line from the map's edge to its other edge. A
// grid larger than the map has no lines to use.
int locationEdge(float v, int g, int o, int size) {
    if (!appState().snapLocations || g >= size) return int(v);
    int c = int(snapRound(v, g, o));
    if (c > size) c -= g;
    if (c < 0) c += g;
    return c >= 0 && c <= size ? c : int(v);
}
int locationEdgeX(float v) { return locationEdge(v, gDock.gridW, gDock.gridX, appState().ed->width() * 32); }
int locationEdgeY(float v) { return locationEdge(v, gDock.gridH, gDock.gridY, appState().ed->height() * 32); }
// A drag inside one grid cell: the side is one cell long, in the direction of the drag (a click stays a click).
void locationSideAtLeastACell(int& lo, int& hi, float from, float to, int g, int size) {
    if (!appState().snapLocations || lo != hi || std::abs(to - from) < 8) return;
    if (to > from ? lo + g <= size : lo - g < 0) hi = lo + g;
    else lo -= g;
}

Rect previewLocationRect(float wx, float wy) {
    Rect r = appState().locStart;
    float dx = wx - appState().dragWX, dy = wy - appState().dragWY;
    auto snx = [](float v) { return locationEdgeX(v); };
    auto sny = [](float v) { return locationEdgeY(v); };
    if (appState().drag == Drag::LocCreate) {
        r = Rect{snx(appState().dragWX), sny(appState().dragWY), snx(wx), sny(wy)}.normalized();
        locationSideAtLeastACell(r.left, r.right, appState().dragWX, wx, gDock.gridW, appState().ed->width() * 32);
        locationSideAtLeastACell(r.top, r.bottom, appState().dragWY, wy, gDock.gridH, appState().ed->height() * 32);
    } else if (appState().drag == Drag::LocMove) {
        // (a line where the whole location stays on the map)
        auto place = [](int to, int len, int g, int size) { return appState().snapLocations && g < size && to + len > size ? to - (to + len - size + g - 1) / g * g : to; };
        int mx = place(snx(r.left + dx), r.right - r.left, gDock.gridW, appState().ed->width() * 32) - r.left,
            my = place(sny(r.top + dy), r.bottom - r.top, gDock.gridH, appState().ed->height() * 32) - r.top;
        r = {r.left + mx, r.top + my, r.right + mx, r.bottom + my};
    } else if (appState().drag == Drag::LocResize) {
        if (appState().locEdges & 1) r.left = snx(r.left + dx);
        if (appState().locEdges & 2) r.top = sny(r.top + dy);
        if (appState().locEdges & 4) r.right = snx(r.right + dx);
        if (appState().locEdges & 8) r.bottom = sny(r.bottom + dy);
        r = r.normalized();
    }
    return r;
}

// Unit snap (Options > Units > Snap to the grid): the grid of View > Grid. Placement with the unit's origin on the grid
// lines; a drag puts the first selected unit on the lines, and the others keep their places around it.
// Buildings snap to tile edges (snapToGrid) while Options > Units > Snap buildings to tile edges is on (the default, as in
// StarEdit); with it off they go where they are placed, as other units (setBuildingSnap: core/views_tabs.inc, the command).
void snapUnitPoint(float& x, float& y) {
    if (!appState().snapUnits || !appState().ed) return;
    // (the map's edge is a line, but a unit does not fit on it: the first line inside; no line inside: the point stays)
    auto inside = [](float v, int g, int o, int size) {
        v = std::clamp(v, 0.0f, float(size - 1));
        int c = int(snapRound(v, g, o));
        if (c <= 0) c += g;
        if (c >= size) c -= g;
        return c > 0 && c < size ? float(c) : v;
    };
    x = inside(x, gDock.gridW, gDock.gridX, appState().ed->width() * 32);
    y = inside(y, gDock.gridH, gDock.gridY, appState().ed->height() * 32);
}

// Options > Sprites > Snap to the grid: where a sprite goes for a point on the map, and how far a drag of the selected
// sprites goes (the first one ends on the lines, the others keep their places around it).
int spriteSnapX(float x) { return appState().snapSprites ? snapOnMap(x, gDock.gridW, gDock.gridX, appState().ed->width() * 32) : int(x); }
int spriteSnapY(float y) { return appState().snapSprites ? snapOnMap(y, gDock.gridH, gDock.gridY, appState().ed->height() * 32) : int(y); }
void spriteDragDelta(float wx, float wy, int& dx, int& dy) {
    const AppState& app = appState();
    float fx = wx - app.dragWX, fy = wy - app.dragWY;
    if (app.snapSprites && !app.selSprites.empty() && size_t(app.selSprites.front()) < app.ed->sprites().size()) {
        const MapSprite& sp = app.ed->sprites()[size_t(app.selSprites.front())];
        fx = float(spriteSnapX(sp.x + fx) - sp.x), fy = float(spriteSnapY(sp.y + fy) - sp.y);
    }
    dx = int(fx), dy = int(fy);
}

// Pasting locations: how far the copied locations move so that the copy's middle is at the mouse (its top left snapped:
// Options > Locations snap), each location where it was in the copy.
void locationPasteShift(const AppState& app, float wx, float wy, int& dx, int& dy) {
    dx = dy = 0;
    if (app.locationClip.empty()) return;
    Rect box = app.locationClip.front().rect;
    for (const auto& c : app.locationClip)
        box = {std::min(box.left, c.rect.left), std::min(box.top, c.rect.top), std::max(box.right, c.rect.right), std::max(box.bottom, c.rect.bottom)};
    const int w = box.right - box.left, hgt = box.bottom - box.top;
    dx = locationEdgeX(wx - w / 2.0f) - box.left, dy = locationEdgeY(wy - hgt / 2.0f) - box.top;
}

// The selection and its buildings' add-ons (a building's add-on goes with it): the units a drag moves, by index.
std::vector<bool> movingUnits() {
    Editor& ed = *appState().ed;
    std::vector<bool> moving(ed.units().size(), false);
    for (int i : ed.withLinkedAddons(ed.selection()))
        if (i >= 0 && size_t(i) < moving.size()) moving[size_t(i)] = true;
    return moving;
}

// The units that stay while the `moving` ones move by (dx, dy), and are near where those go: the others are too far away
// to block them (checkPlacement looks at the units within twice UnitTypes::boxReach and the resource distance).
std::vector<Unit> unitsNearMoved(const std::vector<bool>& moving, float dx, float dy) {
    Editor& ed = *appState().ed;
    const auto& units = ed.units();
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (size_t i = 0; i < units.size(); ++i)
        if (moving[i]) {
            const int x = std::clamp(int(std::lround(units[i].x + dx)), 0, ed.width() * 32 - 1), y = std::clamp(int(std::lround(units[i].y + dy)), 0, ed.height() * 32 - 1);
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    std::vector<Unit> nearby;
    if (x0 > x1) return nearby;
    const int grow = 2 * ed.unitTypes().boxReach() + 96 + 32 + 2;  // (32: a building's snap to the tile edges)
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (!moving[i] && u.x >= x0 - grow && u.x <= x1 + grow && u.y >= y0 - grow && u.y <= y1 + grow) nearby.push_back(u);
    }
    return nearby;
}

// Would the selected units all be placeable moved by (dx, dy)?
bool selectionFitsAt(float dx, float dy) {
    Editor& ed = *appState().ed;
    const auto& units = ed.units();
    const std::vector<bool> moving = movingUnits();
    const std::vector<Unit> others = unitsNearMoved(moving, dx, dy);
    const PlacementContext ctx = ed.placementContext(&others);
    for (size_t i = 0; i < units.size(); ++i) {
        if (!moving[i]) continue;
        Unit m = units[i];
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
    dx = wx - appState().dragWX, dy = wy - appState().dragWY;
    if (appState().snapUnits) {  // (the first unit that has no snap of its own: a building on the tile grid has one)
        const Editor& ed = *appState().ed;
        const Unit* first = nullptr;
        for (int i : ed.selection()) {
            if (i < 0 || size_t(i) >= ed.units().size()) continue;
            const Unit& u = ed.units()[size_t(i)];
            if (appState().snapBuildings && u.type < ed.unitTypes().size() && ed.unitTypes()[u.type].is(UnitFlag::Building)) continue;
            first = &u;
            break;
        }
        if (first) dx = snapGridX(first->x + dx, true) - first->x, dy = snapGridY(first->y + dy, true) - first->y;
    }
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
            if (const DoodadType* t = ed.doodadTypes().find(d.id); t && t->overlayIs(m) && d.x == m.x && d.y == m.y) overlay = true;
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
    appState().terrainSelFlip = (wx - appState().dragWX) * (wy - appState().dragWY) < 0;  // (the drag's corners: IsoRectFlip)
    // (Rectangle, and the square in Isometric: whole tiles; the diamond and the circle: as dragged)
    if ((!appState().terrainRect && selectShape() != Editor::SelectShape::Rect) || !appState().ed) return r;
    const int w = appState().ed->width(), h = appState().ed->height();
    if (!appState().terrainRect) {  // Isometric's square: its sides on the tile lines nearest the press and the mouse (it
                                    // starts where the press was, not a tile up and left of it)
        auto line = [](float v, int most) { return std::clamp(int(std::lround(v / 32.0f)), 0, most) * 32; };
        Rect q = Rect{line(appState().dragWX, w), line(appState().dragWY, h), line(wx, w), line(wy, h)}.normalized();
        if (q.right == q.left) q.right = std::min(w * 32, q.left + 32), q.left = q.right - 32;  // (at least a tile)
        if (q.bottom == q.top) q.bottom = std::min(h * 32, q.top + 32), q.top = q.bottom - 32;
        return q;
    }
    const int x0 = std::clamp(r.left / 32, 0, w - 1), y0 = std::clamp(r.top / 32, 0, h - 1);
    const int x1 = std::clamp((r.right - 1) / 32, x0, w - 1), y1 = std::clamp((r.bottom - 1) / 32, y0, h - 1);
    return Rect{x0 * 32, y0 * 32, (x1 + 1) * 32, (y1 + 1) * 32};
}

// Is tile (tx, ty) in the terrain selection? Its mask (Rectangle and Stamps: tiles added with Shift and taken out with
// Ctrl), else the tiles whose middle is in its shape.
bool terrainSelHas(int tx, int ty) {
    const AppState& app = appState();
    const Rect& r = app.terrainSel;
    if (!app.hasTerrainSel) return false;
    if (app.terrainSelMask.empty()) return Editor::inSelectShape(r, selectShape(), tx * 32 + 16, ty * 32 + 16);  // (Isometric's box is in pixels)
    if (tx * 32 < r.left || ty * 32 < r.top || tx * 32 >= r.right || ty * 32 >= r.bottom) return false;
    const int w = (r.right - r.left) / 32, x = tx - r.left / 32, y = ty - r.top / 32;
    const size_t i = size_t(y) * size_t(w) + size_t(x);
    return i < app.terrainSelMask.size() && app.terrainSelMask[i];
}
// The tiles in the terrain selection, row by row.
std::vector<std::pair<int, int>> terrainSelTiles() {
    const AppState& app = appState();
    std::vector<std::pair<int, int>> out;
    if (!app.hasTerrainSel || !app.ed) return out;
    const Rect& r = app.terrainSel;
    for (int y = std::max(0, r.top / 32); y <= std::min(int(app.ed->height()) - 1, (r.bottom - 1) / 32); ++y)
        for (int x = std::max(0, r.left / 32); x <= std::min(int(app.ed->width()) - 1, (r.right - 1) / 32); ++x)
            if (terrainSelHas(x, y)) out.push_back({x, y});
    return out;
}
void setTerrainSelTiles(const std::vector<std::pair<int, int>>& tiles);
// A paste on a layer is one copy, and then it is the selection under Move: it shows what was placed, and a drag moves it.
void terrainPastedOnLayer(const std::vector<std::pair<int, int>>& tiles) {
    AppState& app = appState();
    app.terrainPasting = false;
    app.toolByMode[int(Mode::Terrain)] = TMoveSel;
    fillToolCombo(app);
    setTerrainSelTiles(tiles);
    setMessage(app, L"Pasted on the layer. Drag to move it. Ctrl+V pastes again.");
    app.dirty = true;
}
// The terrain selection becomes these tiles (a mask over their box; none: no selection).
void setTerrainSelTiles(const std::vector<std::pair<int, int>>& tiles) {
    AppState& app = appState();
    app.terrainSelMask.clear();
    app.hasTerrainSel = !tiles.empty();
    if (tiles.empty()) return void(app.terrainSel = {});
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& [x, y] : tiles) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    const int w = x1 - x0 + 1;
    app.terrainSel = {x0 * 32, y0 * 32, (x1 + 1) * 32, (y1 + 1) * 32};
    app.terrainSelMask.assign(size_t(w) * size_t(y1 - y0 + 1), 0);
    for (const auto& [x, y] : tiles) app.terrainSelMask[size_t(y - y0) * size_t(w) + size_t(x - x0)] = 1;
}
// Shift (add) or Ctrl (take out) with the Select tool in Rectangle and Stamps: the tiles of the drag's rectangle `r`
// (in its shape) go into the selection or out of it.
void terrainSelCombine(const Rect& r, bool add) {
    std::vector<std::pair<int, int>> tiles = terrainSelTiles();
    std::set<std::pair<int, int>> in(tiles.begin(), tiles.end());
    for (int y = r.top / 32; y * 32 < r.bottom; ++y)
        for (int x = r.left / 32; x * 32 < r.right; ++x)
            if (Editor::inSelectShape(r, selectShape(), x * 32 + 16, y * 32 + 16)) {
                if (add) in.insert({x, y});
                else in.erase({x, y});
            }
    setTerrainSelTiles(std::vector<std::pair<int, int>>(in.begin(), in.end()));
}

// Several locations selected (Editor::selectedLocations): they move, copy and delete together, each one undo step.
AppState::LocationCopy locationCopyOf(const Editor& ed, int l) {
    return {ed.locationRect(l), ed.locationInvertedAxes(l), ed.locationName(l), ed.locationElevation(l)};
}
std::wstring locationsText(const Editor& ed, const std::vector<int>& locs) {
    return locs.size() == 1 ? widen(ed.locationName(locs[0])) : std::to_wstring(locs.size()) + L" locations";
}
void rebuildList(AppState& app);
void moveSelectedLocations(int dx, int dy) {
    AppState& app = appState();
    Editor& ed = *app.ed;
    const std::vector<int> locs = ed.selectedLocations();
    if (locs.empty() || (!dx && !dy)) return;
    // The group stops at the map's edges as a whole (feedback #135): the offset shrinks so that every one stays inside.
    int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
    for (int l : locs) {
        if (l == kAnywhereLocation) continue;
        const Rect r = ed.locationRect(l);
        left = std::min(left, r.left), top = std::min(top, r.top), right = std::max(right, r.right), bottom = std::max(bottom, r.bottom);
    }
    if (left > right) return;
    dx = std::max(std::min(dx, int(ed.width()) * 32 - right), -left);
    dy = std::max(std::min(dy, int(ed.height()) * 32 - bottom), -top);
    if (!dx && !dy) return;
    ed.beginStroke();
    for (int l : locs) {
        if (l == kAnywhereLocation) continue;  // (always the whole map)
        const Rect r = ed.locationRect(l);
        ed.setLocationRect(l, {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy});
    }
    ed.endStroke();
    if (locs.size() > 1) setMessage(L"Moved " + locationsText(ed, locs));
}
void deleteSelectedLocations() {
    AppState& app = appState();
    Editor& ed = *app.ed;
    const std::vector<int> locs = ed.selectedLocations();
    if (locs.empty()) return setMessage(L"Select a location first");
    const std::wstring what = locationsText(ed, locs);
    ed.beginStroke();
    for (int l : locs) ed.deleteLocation(l);
    ed.endStroke();
    rebuildList(app);
    setMessage(L"Deleted " + what);
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


// Program options > Map unit limit, into the editor before units go into the map (standard maps; EUD projects); a
// Cosmonarchy map: the mod's own limit (its DatExtend dat_limits.h: 4095 map units).
bool modTab();
int applyUnitLimit(Editor& ed) {
    const int n = modTab() ? 4095 : classicMode() ? gUnitLimitClassic : gUnitLimitEud;
    ed.setUnitLimit(n);
    return n;
}

bool atUnitLimit(const Editor& ed) { return ed.unitLimit() > 0 && int(ed.units().size()) >= ed.unitLimit(); }

std::wstring unitLimitText(const Editor& ed) {
    if (modTab()) return L"The map has " + std::to_wstring(ed.unitLimit()) + L" units: the limit of Cosmonarchy BW";
    return L"The map has " + std::to_wstring(ed.unitLimit()) + L" units: the limit (Options > Program options > Map unit limit)";
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
        const int px = spriteSnapX(x), py = spriteSnapY(y);
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
        placed = ed.placeSprite(uint16_t(appState().palette), spriteSnapX(x), spriteSnapY(y), uint8_t(appState().playerOwner));
    }
    if (placed) appState().lastPlacedBox = box;
    return placed;
}

// While the mouse is down with something picked: along the path from the last one placed, place the next one as soon
// as there is room for it (units: their placement size apart, and only where they fit; doodads and sprites: where
// they do not touch the last one). A place that does not fit now is tried again as the mouse moves on.
void strokeMove(float wx, float wy) {
    if (appState().mode == Mode::Sprites) {  // sprites: one at each place on the path, one pixel (or one snap cell) apart (#78)
        const float step = appState().snapSprites ? float(std::min(gDock.gridW, gDock.gridH)) : 1.0f;
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

// Edit > Snap to the grid: units, doodads or sprites are selected in their mode, and that layer shows.
bool modeLayerHidden();
bool canSnapToGrid(const AppState& app) {
    if (!app.ed || !app.ed->isOpen() || app.pasting || app.pastingItems || modeLayerHidden()) return false;
    return (app.mode == Mode::Units && !app.ed->selection().empty()) || (app.mode == Mode::Doodads && !app.selDoodads.empty()) ||
           (app.mode == Mode::Sprites && !app.selSprites.empty());
}

// Edit > Arrange: two units or more are selected in Units mode, and the units show.
bool canArrange(const AppState& app) {
    return app.ed && app.ed->isOpen() && !app.pasting && !app.pastingItems && !modeLayerHidden() && app.mode == Mode::Units && app.ed->selection().size() >= 2;
}

// Distance between units placed while dragging: the unit's footprint; with a snap, the footprint in whole grid cells.
float placeSpacing() {
    const UnitType& t = appState().ed->unitTypes()[uint16_t(appState().palette)];
    const int foot = std::max({int(t.placementWidth), int(t.placementHeight), 16});
    if (!appState().snapUnits) return float(foot);
    const int g = std::max(gDock.gridW, gDock.gridH);
    return float((foot + g - 1) / g * g);
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
        setMessage((r.movedStart ? tr(L"Moved the start location of Player ") + std::to_wstring(who + 1) : L"Placed " + widen(t.name)) +
                   (moved ? L" (moved to the nearest valid spot)" : L""));
    } else {
        std::wstring why;
        for (auto is : r.issues) why += L" " + widen(placementIssueName(is));
        setMessage(atUnitLimit(ed) ? unitLimitText(ed) : L"Cannot place here:" + why + (correct ? L" (no valid spot nearby)" : L""));
    }
    return r.placed;
}

// The patches' size for Fill with several picks (Terrain), from the Mix setting: 10 (Mix 1, large) to 1 (Mix 10: each
// diamond or tile its own).
int fillPatch() { return 11 - std::clamp(appState().fillMix, 1, 10); }

// Erase in Units, Doodads, Sprites or Locations (a brush: its circle, Size).
bool objectEraseOn() {
    const AppState& app = appState();
    const int t = currentTool(app);
    return (app.mode == Mode::Units && t == UErase) || (app.mode == Mode::Doodads && t == DErase) || (app.mode == Mode::Sprites && t == SErase) ||
           (app.mode == Mode::Locations && t == LErase);
}

// Erase in Terrain: the terrain it paints, Dirt; where the tileset has no Dirt, its first terrain (a new map's ground).
uint16_t eraseTerrain(const Editor& ed) {
    const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
    if (!table) return 1;
    uint16_t first = 0;
    for (uint16_t i = 1; i <= table->terrainCount(); ++i)
        if (table->isTerrain(i)) {
            if (!first) first = i;
            if (ed.terrainName(i) == "Dirt") return i;
        }
    return first ? first : 1;
}

// Erase in Units, Doodads, Sprites or Locations: what is in the brush's circle (Size), at every symmetric position, is
// deleted (a location: where its rectangle meets the circle; Anywhere stays; a doodad's overlay sprite goes with its
// doodad, not alone). Part of the press's stroke. Returns how many.
int eraseAt(float wx, float wy) {
    AppState& app = appState();
    if (!app.ed) return 0;
    Editor& ed = *app.ed;
    const double r = sprayRadius();
    const auto centers = ed.symmetricPoints(int(wx), int(wy));
    auto inBrush = [&](double x0, double y0, double x1, double y1) {  // (a rectangle, or a point: x0 = x1)
        for (auto [px, py] : centers) {
            const double cx = std::clamp(double(px), std::min(x0, x1), std::max(x0, x1)), cy = std::clamp(double(py), std::min(y0, y1), std::max(y0, y1));
            if ((cx - px) * (cx - px) + (cy - py) * (cy - py) <= r * r) return true;
        }
        return false;
    };
    std::vector<int> hit;
    switch (app.mode) {
    case Mode::Units:
        for (size_t i = 0; i < ed.units().size(); ++i)  // (the units that show: not the hidden map revealers)
            if (ed.pickable(int(i)) && inBrush(ed.units()[i].x, ed.units()[i].y, ed.units()[i].x, ed.units()[i].y)) hit.push_back(int(i));
        if (hit.empty()) return 0;
        ed.clearSelection();
        for (int i : hit) ed.select(i, true);
        ed.deleteSelection();
        break;
    case Mode::Doodads:
        for (size_t i = 0; i < ed.doodads().size(); ++i)
            if (inBrush(ed.doodads()[i].x, ed.doodads()[i].y, ed.doodads()[i].x, ed.doodads()[i].y)) hit.push_back(int(i));
        if (!hit.empty()) ed.deleteDoodads(hit), app.selDoodads.clear();  // (the selection's indices no longer hold)
        break;
    case Mode::Sprites:
        for (size_t i = 0; i < ed.sprites().size(); ++i)
            if (inBrush(ed.sprites()[i].x, ed.sprites()[i].y, ed.sprites()[i].x, ed.sprites()[i].y) && !spriteIsOverlay(app, int(i))) hit.push_back(int(i));
        if (!hit.empty()) ed.deleteSprites(hit), app.selSprites.clear();  // (the selection's indices no longer hold)
        break;
    case Mode::Locations:
        for (int i = int(ed.locations().size()) - 1; i >= 0; --i)
            if (i != kAnywhereLocation && ed.locationUsed(i)) {
                const Rect b = ed.locationRect(i);
                if (inBrush(b.left, b.top, b.right, b.bottom)) hit.push_back(i);
            }
        for (int i : hit) ed.deleteLocation(i);
        break;
    default: break;
    }
    return int(hit.size());
}

// Units' or Doodads' Fill: the tool's press fills; no preview follows the mouse.
bool objectFillOn() {
    const AppState& app = appState();
    return (app.mode == Mode::Units && currentTool(app) == UFill) || (app.mode == Mode::Doodads && currentTool(app) == DFill);
}

// Fill in Units and Doodads: the connected ground of the clicked tile's terrain (its tiles, 4 ways: the isometric terrain,
// or the same tile without it) gets units or doodads of the selected kinds at random places, where they fit: as many
// tries as one in 10 of its tiles at Density 10 (Density 50: half), 5000 at most. One undo step (one edit of all the
// places). Returns the number placed (-1: nothing to place or no ground).
int fillObjectsAt(float wx, float wy) {
    AppState& app = appState();
    if (!app.ed) return -1;
    Editor& ed = *app.ed;
    std::vector<uint16_t> picks;
    for (int v : selectedPalette())
        if (v >= 0) picks.push_back(uint16_t(v));
    const int w = ed.width(), h = ed.height(), tx = int(wx) / 32, ty = int(wy) / 32;
    if (picks.empty()) {
        setMessage(app.mode == Mode::Units ? L"Select a unit in the list first" : L"Select a doodad in the list first");
        return -1;
    }
    if (tx < 0 || ty < 0 || tx >= w || ty >= h) return -1;
    const std::vector<uint16_t>& tiles = ed.terrain().tiles;
    auto kind = [&](int x, int y) -> uint32_t {
        const uint16_t t = ed.terrainAt(x * 32 + 16, y * 32 + 16);
        return t ? t : 0x10000u | tiles[size_t(y) * w + x];
    };
    const uint32_t from = kind(tx, ty);
    std::vector<std::pair<int, int>> ground;
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<std::pair<int, int>> todo{{tx, ty}};
    seen[size_t(ty) * w + tx] = 1;
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        ground.push_back({x, y});
        const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int k = 0; k < 4; ++k)
            if (nx[k] >= 0 && ny[k] >= 0 && nx[k] < w && ny[k] < h && !seen[size_t(ny[k]) * w + nx[k]] && kind(nx[k], ny[k]) == from)
                seen[size_t(ny[k]) * w + nx[k]] = 1, todo.push_back({nx[k], ny[k]});
    }
    const int tries = std::clamp(int(ground.size() * size_t(app.sprayDensity) / 100), 1, 5000);
    std::mt19937 rng(app.spraySeed++);
    std::vector<std::pair<int, int>> points;
    for (int i = 0; i < tries; ++i) {
        const auto [x, y] = ground[rng() % ground.size()];
        points.push_back({x * 32 + int(rng() % 32), y * 32 + int(rng() % 32)});
    }
    if (app.mode == Mode::Units) applyUnitLimit(ed);
    const int placed = app.mode == Mode::Units ? ed.sprayUnitsAt(picks, uint8_t(app.playerOwner), points, app.spraySeed++)
                                               : ed.sprayDoodadsAt(picks, points, app.spraySeed++, app.doodadCheck);
    if (app.mode == Mode::Units) ed.clearSelection();  // (placed units are not selected)
    return placed;
}

// One spray at a map point with the active mode's Spray: terrain (ISOM; in Rectangle mode, tiles), units or doodads.
// Returns the units or doodads it placed.
int sprayAt(float wx, float wy) {
    if (!appState().ed) return 0;
    Editor& ed = *appState().ed;
    if (appState().mode == Mode::Terrain) {
        if (appState().terrainRect) tilesSprayAt(wx, wy);
        else if (appState().palette > 0) {
            // (several terrains: the dabs shared out among them, each its own share at random places)
            std::vector<uint16_t> picks;
            for (int v : selectedPalette())
                if (v > 0) picks.push_back(uint16_t(v));
            if (picks.empty()) picks.push_back(uint16_t(appState().palette));
            ed.sprayTerrainPart(int(wx), int(wy), picks, currentBrush(), std::clamp(appState().sprayDensity, 1, kDensityMax), appState().spraySeed++);
        }
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
    if (appState().mode == Mode::Sprites && !picks.empty())  // random sprites of the selected kinds in the circle
        return ed.spraySprites(picks, uint8_t(appState().playerOwner), wx, wy, sprayRadius(), sprayCount(), appState().spraySeed++, appState().snapSprites ? gDock.gridW : 0,
                               appState().snapSprites ? gDock.gridH : 0, gDock.gridX, gDock.gridY);
    return 0;
}

// View > Modes: the active mode's layer is hidden. Its map edits are refused (nothing changes that does not show).
bool modeLayerHidden() {
    switch (appState().mode) {
    case Mode::Terrain: return !gLayers.terrain;
    case Mode::Units: return !gLayers.units;
    case Mode::Doodads: return !gLayers.doodads;
    case Mode::Sprites: return !gLayers.sprites;
    case Mode::Locations: return false;  // (Locations mode always shows them)
    }
    return false;
}

void modeLayerHiddenMessage() {
    setMessage(std::wstring(kModeNames[int(appState().mode)]) + L": this layer is hidden. View > Modes shows it again (then you can edit it).");
}

// A click on the map while the mode's layer is hidden (Discord feedback #88): the first time after the layer went
// hidden, a question offers to show it; then the status bar's message only.
bool shellAskYesNo(const std::wstring& text, const std::wstring& title, const std::wstring& yes, const std::wstring& no);
void shellMenusChanged();
int gLayerShows = 0;  // +1 each time a layer shows again (View > Modes): the question asks again after it
void modeLayerHiddenClick() {
    AppState& app = appState();
    static int asked[kModeCount] = {-1, -1, -1, -1, -1, -1};  // the gLayerShows of each mode's last question
    if (asked[int(app.mode)] == gLayerShows || gSelfTest) return modeLayerHiddenMessage();
    asked[int(app.mode)] = gLayerShows;
    const std::wstring name = kModeNames[int(app.mode)];
    if (!shellAskYesNo(name + L": this layer is hidden (View > Modes), so you cannot edit it. Show the layer?", L"Hidden layer", L"Show the layer", L"Cancel"))
        return modeLayerHiddenMessage();
    switch (app.mode) {
    case Mode::Terrain: gLayers.terrain = true; break;
    case Mode::Units: gLayers.units = true; break;
    case Mode::Doodads: gLayers.doodads = true; break;
    case Mode::Sprites: gLayers.sprites = true; break;
    case Mode::Locations: app.showLocations = true; break;
    }
    ++gLayerShows;
    shellMenusChanged();
    app.dirty = true;
    setMessage(name + L" show again");
}

// Options: the most units a map may have (GitHub feedback #103): standard maps, and maps of EUD projects.

void outline(render::Scene& s, const Rect& r, uint32_t rgba, float thickness = 1, float dx = 0, float dy = 0) {
    s.rects.push_back({r.left + dx, r.top + dy, r.right + dx, r.bottom + dy, rgba, false, thickness, true});
}

void viewSize(int& w, int& h) { shellViewSize(w, h); }

// The part of the map that the map view shows (map pixels), `margin` map pixels more on each side. False: the view has
// no size yet (the caller then keeps all).
bool mapViewBox(float& x0, float& y0, float& x1, float& y1, float margin = 0) {
    int vw, vh;
    viewSize(vw, vh);
    const AppState& app = appState();
    if (vw <= 0 || vh <= 0 || app.zoom <= 0) return false;
    x0 = app.camX - margin, y0 = app.camY - margin, x1 = app.camX + vw / app.zoom + margin, y1 = app.camY + vh / app.zoom + margin;
    return true;
}

// The rectangles of a kept list (an overlay's) that the map view shows, added to `out` (all when the view has no size).
void addRectsInView(std::vector<render::RectDraw>& out, const std::vector<render::RectDraw>& in) {
    float x0, y0, x1, y1;
    if (!mapViewBox(x0, y0, x1, y1, 1)) return (void)out.insert(out.end(), in.begin(), in.end());
    for (const render::RectDraw& d : in)
        if (std::max(d.x0, d.x1) >= x0 && std::min(d.x0, d.x1) <= x1 && std::max(d.y0, d.y1) >= y0 && std::min(d.y0, d.y1) <= y1) out.push_back(d);
}

// A status-bar hint (what is under the mouse): not worth a log line.
void setHint(const std::wstring& m) {
    appState().message = m;
    updateStatus();
}

