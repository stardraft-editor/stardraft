// Terrain's shape tools, as Paint's (the tool strip's Line, Contour and Polygon; the keys I, O and P). Line paints along
// the straight line from the press to the release, with the brush's size and shape. Contour fills the shape that a drag
// draws (the release joins its last point to its first). Polygon fills the polygon of the clicked points (a double click
// closes it, Esc stops it). The map changes only at the end, as one undo step; until then the map view shows the result
// over the terrain (ghostTiles: map_scene.inc, shapeScene).
// Isometric: the brush's own paint path changes a copy of the map (Editor::paintBrush along the line, fillTerrainPolygon),
// so the edges are corrected; the end takes the copy's map (Editor::takeScenario), tile for tile as the preview showed it.
// Rectangle, Tile and Custom: the tiles (Rectangle: the rects) that the line's brush covers or whose middle is in the
// shape, as the Brush gives them.

struct TerrainShape {
    int tool = -1;                            // TLine, TContour or TPolygon while a shape is under way; -1: none
    const Editor* ed = nullptr;               // the map it is on
    std::vector<std::pair<int, int>> points;  // map pixels (Line: the press and the mouse)
    bool hasHover = false;                    // Polygon: the mouse is the next point
    std::pair<int, int> hover{};
    uint32_t seed = 1;                        // the random tiles of the preview and of the end (the same ones)
    // The preview: the tiles that change (tile index, tile), and in Rectangle, Tile and Custom the tiles that the end
    // places (before the mirrors' images). made: they are of the points madeFor on the map's revision madeRevision.
    std::vector<std::pair<int, uint16_t>> ghost;
    std::vector<Editor::TileAt> cells;
    std::vector<std::pair<int, int>> madeFor;
    std::vector<int64_t> madeKey;  // (shapeSettingsKey)
    bool made = false;
    bool copyHolds = false;  // Isometric: the copy of the map (shapeCopy) holds this preview
};
TerrainShape gShape;

void shapeReset() { gShape = TerrainShape{}; }

// A shape is under way on the open map, with its tool (another map, mode or tool: the shape goes).
bool shapeOn() {
    if (gShape.tool < 0) return false;
    const AppState& app = appState();
    if (app.ed && gShape.ed == app.ed.get() && app.ed->isOpen() && app.mode == Mode::Terrain && currentTool(app) == gShape.tool) return true;
    shapeReset();
    return false;
}

// The shape's points, with the mouse as the next point of a polygon.
std::vector<std::pair<int, int>> shapePath() {
    std::vector<std::pair<int, int>> p = gShape.points;
    if (gShape.hasHover && (p.empty() || p.back() != gShape.hover)) p.push_back(gShape.hover);
    return p;
}

// The copy of the map that the Isometric previews paint (one for the program, as the resize preview's).
Editor& shapeCopy(const GameData& data) {
    static std::unique_ptr<Editor> copy;
    static const GameData* of = nullptr;
    if (!copy || of != &data) copy = std::make_unique<Editor>(data), of = &data;
    return *copy;
}

// The copy paints as the map does: its protected tiles, mirrors and turns, and its unit options.
void shapeCopySettings(Editor& c, const Editor& ed) {
    c.setProtectedTiles(ed.protectedTiles());
    c.setMirrors(ed.mirrors()), c.setTurns(ed.turns());
    c.setAllowIllegalUnits(ed.allowIllegalUnits()), c.setAllowStacking(ed.allowStacking());
    c.setResourceDistance(ed.resourceDistance()), c.setManyStartLocations(ed.manyStartLocations()), c.setUnitLimit(ed.unitLimit());
}

// Isometric: the shape painted on `e` with the terrain `t`. Line: the brush at most half its size apart along the line.
void shapeIsoPaint(Editor& e, int tool, const std::vector<std::pair<int, int>>& pts, uint16_t t, const Brush& brush) {
    if (tool != TLine) {
        e.fillTerrainPolygon(pts, t);
        return;
    }
    const float x0 = float(pts.front().first), y0 = float(pts.front().second);
    const float dx = float(pts.back().first) - x0, dy = float(pts.back().second) - y0;
    const float step = std::max(8.0f, std::min(brush.size, brush.height()) * 16), len = std::hypot(dx, dy);
    const int n = int(std::ceil(len / step));
    e.beginTerrainStroke();
    for (int k = 0; k <= n; ++k) e.paintBrush(int(x0 + (n ? dx * k / n : 0)), int(y0 + (n ? dy * k / n : 0)), t, brush);
    e.endTerrainStroke();
}

// The tiles from tile (x0, y0) to tile (x1, y1) on a straight line (each step one tile across or down, or both).
std::vector<std::pair<int, int>> shapeLineTiles(int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> out;
    const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int k = 0; k <= n; ++k)
        out.push_back({x0 + (n ? int(std::lround(double(x1 - x0) * k / n)) : 0), y0 + (n ? int(std::lround(double(y1 - y0) * k / n)) : 0)});
    return out;
}

// Whether the map point (px, py) is in the polygon (the last point joins the first; even-odd).
bool shapeInside(const std::vector<std::pair<int, int>>& pts, double px, double py) {
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const double xi = pts[i].first, yi = pts[i].second, xj = pts[j].first, yj = pts[j].second;
        if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

// Rectangle, Tile and Custom: the tiles of the shape (gShape.cells) and their preview with the mirrors' images.
void shapeMakeTiles(const std::vector<std::pair<int, int>>& pts) {
    AppState& app = appState();
    const Editor& ed = *app.ed;
    const int w = ed.width(), h = ed.height();
    std::set<std::pair<int, int>> tiles;  // Rectangle: the rects (rx, ry: tiles 2rx and 2rx + 1 of row ry)
    if (gShape.tool == TLine) {
        int ax, ay, bx, by;
        tilesAt(float(pts.front().first), float(pts.front().second), ax, ay);
        tilesAt(float(pts.back().first), float(pts.back().second), bx, by);
        for (auto [x, y] : shapeLineTiles(ax, ay, bx, by))
            for (const auto& c : gTiles->solid ? solidFootprint(x, y) : tilesFootprint(x, y)) tiles.insert(c);
    } else {
        int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
        for (const auto& [x, y] : pts) left = std::min(left, x), top = std::min(top, y), right = std::max(right, x), bottom = std::max(bottom, y);
        const int cw = gTiles->solid ? 64 : 32;  // (a rect is 2 tiles wide)
        for (int y = std::max(0, top / 32); y <= std::min(h - 1, bottom / 32); ++y)
            for (int x = std::max(0, left / cw); x <= std::min((w - 1) * 32 / cw, right / cw); ++x)
                if (shapeInside(pts, x * cw + cw / 2.0, y * 32 + 16.0)) tiles.insert({x, y});
    }
    std::vector<Editor::TileAt> cells;
    if (gTiles->solid) {
        const uint32_t was = app.spraySeed;
        app.spraySeed = gShape.seed;  // (the same random tiles in each preview and at the end)
        cells = solidTiles(std::vector<std::pair<int, int>>(tiles.begin(), tiles.end()));
        app.spraySeed = was;
    } else {
        std::mt19937 rng(gShape.seed);
        int ox, oy;  // the stamp's pattern starts at the first point
        tilesAt(float(pts.front().first), float(pts.front().second), ox, oy);
        gTiles->stampX = ox, gTiles->stampY = oy;
        for (const auto& [x, y] : tiles) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            const uint16_t v = tilesStampOn() ? tilesStampAt(x, y)
                             : gTiles->chosen.size() > 1 ? gTiles->chosen[rng() % gTiles->chosen.size()] : uint16_t(gTiles->picked);
            if (v != 0xFFFF) cells.push_back({x, y, v});
        }
    }
    for (const auto& c : gTiles->solid ? solidMirrored(cells) : tilesMirrored(cells))
        if (ed.terrain().tiles[size_t(c.y) * w + c.x] != c.value) gShape.ghost.push_back({c.y * w + c.x, c.value});
    gShape.cells = std::move(cells);
}

// What a preview is of besides the points: the map's revision, the kind of terrain, the pick, the brush and the mirrors.
std::vector<int64_t> shapeSettingsKey() {
    const AppState& app = appState();
    const Brush b = currentBrush(app);
    return {int64_t(app.ed->revision()), terrainKind(), app.palette, gTiles->picked, int64_t(gTiles->chosen.size()), int64_t(gTiles->stamp.size()),
            int64_t(b.shape), int64_t(b.size * 64), int64_t(b.height() * 64), app.ed->mirrors(), app.ed->turns()};
}

void shellShapeLater(int ms);  // (the program: shapeUpdate again after ms, then the map view paints)

// The preview of the shape (gShape.ghost, gShape.cells): made again only when its points or settings changed. `now`: at
// once; else an Isometric preview that takes long (a large map) is made at most every 2 times its time (shellShapeLater
// makes the last one).
void shapeUpdate(bool now = false) {
    if (!shapeOn()) return;
    AppState& app = appState();
    Editor& ed = *app.ed;
    const std::vector<std::pair<int, int>> pts = shapePath();
    const std::vector<int64_t> key = shapeSettingsKey();
    if (gShape.made && gShape.madeFor == pts && gShape.madeKey == key) return;
    static double cost = 0;  // the last Isometric preview's time, ms
    static std::chrono::steady_clock::time_point last{};
    const auto t0 = std::chrono::steady_clock::now();
    if (!now && !app.terrainRect && cost > 12 && std::chrono::duration<double, std::milli>(t0 - last).count() < 2 * cost)
        return shellShapeLater(int(2 * cost));
    gShape.ghost.clear(), gShape.cells.clear(), gShape.copyHolds = false;
    gShape.madeFor = pts, gShape.madeKey = key, gShape.made = true;
    if (pts.size() < (gShape.tool == TLine ? 2u : 3u)) return;
    if (app.terrainRect) return shapeMakeTiles(pts);
    Editor& c = shapeCopy(ed.gameData());
    std::string error;
    if (!c.openChk(ed.chk(), error)) {
        gShape.made = false;
        return;
    }
    shapeCopySettings(c, ed);
    shapeIsoPaint(c, gShape.tool, pts, uint16_t(app.palette), currentBrush(app));
    const std::vector<uint16_t>&a = ed.terrain().tiles, &b = c.terrain().tiles;
    if (a.size() == b.size())
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) gShape.ghost.push_back({int(i), b[i]});
    gShape.copyHolds = true;
    last = std::chrono::steady_clock::now();
    cost = std::chrono::duration<double, std::milli>(last - t0).count();
}

// The shape tools have something to paint: Isometric a terrain (and the map its isometric terrain data), else the pick of
// the Tiles, Custom or terrain list. False: the status bar tells what to do.
bool shapeHasPick() {
    AppState& app = appState();
    if (!app.terrainRect) {
        if (!app.ed->canPaintTerrain()) return setMessage(L"Could not paint the terrain. The map has no isometric terrain data."), false;
        if (app.palette <= 0) return setMessage(L"Select a terrain in the list first"), false;
        return true;
    }
    tilesFresh();
    if (tilesHavePick()) return true;
    setMessage(gTiles->stamps  ? L"First, select custom terrain in the Custom list"
               : gTiles->solid ? L"Select a terrain in the list first"
                               : L"Select a tile in the Tiles list first (or Ctrl+click a tile on the map)");
    return false;
}

// The end of the shape: the map takes its preview, as one undo step.
void shapeFinish() {
    AppState& app = appState();
    app.drag = Drag::None;
    if (!shapeOn()) return;
    Editor& ed = *app.ed;
    const int tool = gShape.tool;
    gShape.hasHover = false;  // (Polygon: the clicked points only)
    std::vector<std::pair<int, int>> pts = gShape.points;
    if (tool != TLine) pts.erase(std::unique(pts.begin(), pts.end()), pts.end());  // (a Line click: the brush at the point)
    if (tool != TLine && pts.size() < 3) {
        setMessage(tool == TPolygon ? L"Did not fill the polygon: a polygon has 3 points or more" : L"Did not fill the contour: draw a larger shape");
        shapeReset();
        app.dirty = true;
        return;
    }
    gShape.points = pts;
    gShape.made = false;  // (made again now, of the map as it is: the copy that the map takes holds it)
    shapeUpdate(true);
    if (app.terrainRect) {
        ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
        tilesPlacedMessage(gShape.cells.empty() ? 0 : tilesPut(gShape.cells), L"Placed");
    } else if (!gShape.made || !gShape.copyHolds || gShape.ghost.empty()) {
        setMessage(L"The terrain is the same already");
    } else {
        // (the map takes the copy's scenario: the units and the locations that were selected stay selected)
        const std::vector<int> units = ed.selection(), locations = ed.selectedLocations();
        const size_t unitCount = ed.units().size();
        ed.takeScenario(shapeCopy(ed.gameData()));
        if (ed.units().size() == unitCount)
            for (int u : units) ed.select(u, true);
        for (int l : locations) ed.addLocationSelection(l);
        setMessage(tool == TLine ? L"Painted the line" : tool == TContour ? L"Filled the contour" : L"Filled the polygon");
    }
    shapeReset();
    app.dirty = true;
}

// A press on the map with a shape tool: Line and Contour start (Drag::Shape), Polygon adds a point (the first starts it).
void shapeMouseDown(float wx, float wy) {
    AppState& app = appState();
    app.drag = Drag::None;
    if (!shapeHasPick()) return;
    const int tool = currentTool(app);
    const std::pair<int, int> p{int(wx), int(wy)};
    if (tool == TPolygon && shapeOn()) {
        if (gShape.points.back() != p) gShape.points.push_back(p);
        gShape.hasHover = false;
        shapeUpdate();
        return;
    }
    shapeReset();
    gShape.tool = tool, gShape.ed = app.ed.get(), gShape.seed = app.spraySeed++;
    gShape.points = {p};
    if (tool == TLine) gShape.points.push_back(p);
    if (tool != TPolygon) app.drag = Drag::Shape;
    setMessage(tool == TLine      ? L"Line: release the button at the end of the line"
               : tool == TContour ? L"Contour: draw the shape, then release the button"
                                  : L"Polygon: click to add points. Double-click to close the polygon and fill it. Esc stops the polygon.");
    shapeUpdate();
}

// The mouse moves: Line's end, Contour's next point (each 6 screen pixels or more from the last one), Polygon's next point.
void shapeMouseMove(float wx, float wy) {
    if (!shapeOn()) return;
    const AppState& app = appState();
    const std::pair<int, int> p{int(wx), int(wy)};
    if (gShape.tool == TPolygon) {
        gShape.hasHover = true, gShape.hover = p;
    } else if (app.drag != Drag::Shape) {
        return;
    } else if (gShape.tool == TLine) {
        gShape.points.back() = p;
    } else {
        const auto& q = gShape.points.back();
        if (std::hypot(float(p.first - q.first), float(p.second - q.second)) * app.zoom < 6) return;
        gShape.points.push_back(p);
    }
    shapeUpdate();
}

// The release of a Line or Contour press: the shape goes on the map.
void shapeMouseUp(float wx, float wy) {
    if (shapeOn()) {
        const std::pair<int, int> p{int(wx), int(wy)};
        if (gShape.tool == TLine) gShape.points.back() = p;
        else if (gShape.points.back() != p) gShape.points.push_back(p);
    }
    shapeFinish();
}

// A double click on the map: a polygon under way closes and is filled. True: it took the double click.
bool shapeDoubleClick() {
    if (!shapeOn() || gShape.tool != TPolygon) return false;
    shapeFinish();
    return true;
}

// Esc: the shape under way stops, with no change to the map. True: there was one.
bool shapeCancel() {
    if (!shapeOn()) return false;
    setMessage(gShape.tool == TLine ? L"Stopped the line" : gShape.tool == TContour ? L"Stopped the contour" : L"Stopped the polygon");
    shapeReset();
    appState().drag = Drag::None;
    appState().dirty = true;
    return true;
}
