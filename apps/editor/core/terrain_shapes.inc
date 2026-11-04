// Terrain's shape tools, as Paint's (the tool strip's Line, Contour and Polygon; the keys I, O and P). Line paints along
// the straight line from the press to the release, with the brush's size and shape. Contour fills the shape that a drag
// draws (the release joins its last point to its first). Polygon fills the polygon of the clicked points (a double click
// closes it, Esc stops it). The map changes only at the end, as one undo step; until then the map view shows the result
// over the terrain (ghostTiles: map_scene.inc, shapeScene).
// Isometric: the brush's own paint path changes a copy of the map (Editor::paintTerrainLine, fillTerrainPolygon),
// so the edges are corrected; the end takes the copy's map (Editor::takeScenario), tile for tile as the preview showed it.
// Rectangle, Tile and Custom: the tiles (Rectangle: the rects) that the line's brush covers or whose middle is in the
// shape, as the Brush gives them.
// Bridge (any kind of terrain): the drag goes along one of the map's diagonals from the ground across water to the
// ground; the preview is the bridge that fits the water there (src/bridges.cpp), with the brush's size as its width.
// Ramp (any kind of terrain): a stroke along a cliff edge (as Contour's points); the preview is the ramp that fits the
// edge there, as far as the stroke goes along it (src/ramps.cpp).

struct TerrainShape {
    int tool = -1;                            // TLine, TContour, TPolygon, a box or TBridge while a shape is under way; -1: none
    const Editor* ed = nullptr;               // the map it is on
    std::vector<std::pair<int, int>> points;  // map pixels (Line: the press and the mouse)
    bool hasHover = false;                    // Polygon: the mouse is the next point
    std::pair<int, int> hover{};
    uint32_t seed = 1;                        // the random tiles of the preview and of the end (the same ones)
    // The preview: the tiles that change (tile index, tile), and in Rectangle, Tile and Custom the tiles that the end
    // places (before the mirrors' images). made: they are of the points madeFor on the map's revision madeRevision.
    std::vector<std::pair<int, uint16_t>> ghost;
    std::vector<Editor::TileAt> cells;
    std::vector<std::pair<int, int>> red;  // Bridge: the tiles (x, y) where a bridge goes that does not fit there yet
    std::vector<std::pair<int, int>> madeFor;
    std::vector<int64_t> madeKey;  // (shapeSettingsKey)
    bool made = false;
    bool copyHolds = false;  // Isometric: the copy of the map (shapeCopy) holds this preview
    bool unramp = false;     // Ramp with Shift (at the press): the ramps under the stroke become the cliff again
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

// Rectangle, Ellipse and Diamond: the outline of the box between the press and the mouse (its corners; a diamond's:
// the middles of its sides; an ellipse's points, 4 map pixels or more apart, 12 at least and 180 at most). Empty: the
// box has no width or no height.
std::vector<std::pair<int, int>> shapeBoxPoints(int tool, const std::vector<std::pair<int, int>>& pts) {
    if (pts.size() < 2) return {};
    const auto [ax, ay] = pts.front();
    const auto [bx, by] = pts.back();
    const int l = std::min(ax, bx), r = std::max(ax, bx), t = std::min(ay, by), b = std::max(ay, by);
    if (r - l < 2 || b - t < 2) return {};
    if (tool == TRect || tool == TRectFill) return {{l, t}, {r, t}, {r, b}, {l, b}};
    if (tool == TDiamond || tool == TDiamondFill) return {{(l + r) / 2, t}, {r, (t + b) / 2}, {(l + r) / 2, b}, {l, (t + b) / 2}};
    const double cx = (l + r) / 2.0, cy = (t + b) / 2.0, rx = (r - l) / 2.0, ry = (b - t) / 2.0;
    const int n = std::clamp(int(6.2831853 * std::max(rx, ry) / 4), 12, 180);
    std::vector<std::pair<int, int>> out;
    for (int k = 0; k < n; ++k) {
        const double a = 6.2831853 * k / n;
        const std::pair<int, int> p{int(std::lround(cx + rx * std::cos(a))), int(std::lround(cy + ry * std::sin(a)))};
        if (out.empty() || out.back() != p) out.push_back(p);
    }
    return out;
}
// The points the shape paints: a box's outline, else its own points.
std::vector<std::pair<int, int>> shapeGeometry(const std::vector<std::pair<int, int>>& pts) {
    return isBoxTool(gShape.tool) ? shapeBoxPoints(gShape.tool, pts) : pts;
}
// Shift (Rectangle, Ellipse, Diamond): the box from `a` a square (Diamond: twice as wide as high, as the map's
// diamonds), as large as the mouse's farther side.
std::pair<int, int> shapeSquare(std::pair<int, int> a, std::pair<int, int> p, int tool) {
    const int q = tool == TDiamond || tool == TDiamondFill ? 2 : 1;  // (the width for each pixel of height)
    const int dx = p.first - a.first, dy = p.second - a.second, d = std::max(std::abs(dx), q * std::abs(dy));
    return {a.first + (dx < 0 ? -d : d), a.second + (dy < 0 ? -d / q : d / q)};
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
    c.copySymmetryCenter(ed);
    c.setAllowIllegalUnits(ed.allowIllegalUnits()), c.setAllowStacking(ed.allowStacking());
    c.setResourceDistance(ed.resourceDistance()), c.setManyStartLocations(ed.manyStartLocations()), c.setUnitLimit(ed.unitLimit());
}

// Isometric: the shape painted on `e` with the terrain `t`. Line: the brush at most half its size apart along the line.
// Rectangle, Ellipse and Diamond: their outline's lines (filled: as a polygon).
void shapeIsoPaint(Editor& e, int tool, const std::vector<std::pair<int, int>>& pts, uint16_t t, const Brush& brush) {
    if (!isStrokeShape(tool)) {
        e.fillTerrainPolygon(pts, t);
        return;
    }
    if (tool == TLine) return void(e.paintTerrainLine(pts.front().first, pts.front().second, pts.back().first, pts.back().second, t, brush));
    e.paintTerrainPath(pts, true, t, brush);  // (the outline's lines: one pass, one edit)
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
    if (isStrokeShape(gShape.tool)) {  // (Line: one line; Rectangle and Ellipse: the lines round them)
        const size_t lines = gShape.tool == TLine ? 1 : pts.size();
        for (size_t i = 0; i < lines; ++i) {
            const auto& a = gShape.tool == TLine ? pts.front() : pts[i];
            const auto& b = gShape.tool == TLine ? pts.back() : pts[(i + 1) % pts.size()];
            int ax, ay, bx, by;
            tilesAt(float(a.first), float(a.second), ax, ay);
            tilesAt(float(b.first), float(b.second), bx, by);
            for (auto [x, y] : shapeLineTiles(ax, ay, bx, by))
                for (const auto& c : gTiles->solid ? solidFootprint(x, y) : tilesFootprint(x, y)) tiles.insert(c);
        }
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

// The bridge's width from the brush's size: size 0.5 the ends only, else as many middle pieces as the size.
int bridgeWidth() {
    const float size = currentBrush(appState()).size;
    return size < 1 ? 0 : int(size);
}
std::wstring bridgeSize(int pieces) { return pieces ? std::to_wstring(pieces) : std::wstring(L"0.5"); }
// A Bridge message: the translation of `format` with the texts in it (%ls).
std::wstring bridgeText(const wchar_t* format, const std::wstring& a, const std::wstring& b = L"", const std::wstring& c = L"") {
    wchar_t buf[512];
    std::swprintf(buf, 512, tr(format), a.c_str(), b.c_str(), c.c_str());
    return buf;
}

// Bridge: the bridge that the drag makes (gShape.cells, gShape.ghost), and the status bar says it or why there is none.
void bridgeMake(const std::vector<std::pair<int, int>>& pts) {
    const Editor& ed = *appState().ed;
    const BridgePlan p = planBridge(ed, pts.front().first, pts.front().second, pts.back().first, pts.back().second, bridgeWidth(), gShape.seed);
    const std::wstring ground = tr(widen(p.ground)), water = tr(widen(p.water));  // (the terrains' names, as the list shows them)
    for (const Editor::TileAt& c : p.draft) {  // (no bridge yet: where one goes, its tiles under red)
        gShape.red.push_back({c.x, c.y});
        gShape.ghost.push_back({c.y * ed.width() + c.x, c.value});
    }
    switch (p.problem) {
    case BridgePlan::Problem::None: break;
    case BridgePlan::Problem::NoKit: return setMessage(L"This tileset has no bridges");
    case BridgePlan::Problem::NoIsom: return setMessage(L"Could not make a bridge. The map has no isometric terrain data.");
    case BridgePlan::Problem::Short: return setMessage(bridgeText(L"Bridge: drag across the %ls to the %ls on the other side", water, ground));
    case BridgePlan::Problem::ReachGround: return setMessage(bridgeText(L"Bridge: drag on to the %ls on the other side", ground));
    case BridgePlan::Problem::NeedsTerrain:
        return setMessage(bridgeText(L"A bridge goes from land across the %ls to land, along a diagonal of the map", water));
    case BridgePlan::Problem::TooNarrow: return setMessage(bridgeText(L"The %ls is too narrow for a bridge. Paint it wider.", water));
    case BridgePlan::Problem::OffMap: return setMessage(L"The bridge does not fit on the map");
    }
    for (const Editor::TileAt& c : p.tiles)
        if (ed.tileProtected(c.x, c.y)) {
            for (const Editor::TileAt& r : p.tiles) gShape.red.push_back({r.x, r.y});
            return setMessage(L"Part of the bridge is on protected tiles");
        }
    // (the shore and the bridge on the copy of the map: the preview is what it changes, and the end takes the copy)
    Editor& copy = shapeCopy(ed.gameData());
    std::string error;
    if (!copy.openChk(ed.chk(), error)) return setMessage(bridgeText(L"Could not make the bridge: %ls", widen(error)));
    shapeCopySettings(copy, ed);
    copy.setMirrors(0), copy.setTurns(1);  // (one bridge: symmetry does not apply to bridges)
    applyBridge(copy, p);
    const std::vector<uint16_t>&a = ed.terrain().tiles, &b = copy.terrain().tiles;
    if (a.size() == b.size())
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) gShape.ghost.push_back({int(i), b[i]});
    gShape.copyHolds = true;
    gShape.cells = p.tiles;
    setMessage(bridgeText(L"Bridge: length %ls, width %ls. Release the button to place it.", bridgeSize(p.length), bridgeSize(p.width)));
}

// Ramp: the ramp that the stroke paints (gShape.cells, gShape.ghost), and the status bar says it or why there is none.
void rampMake(const std::vector<std::pair<int, int>>& stroke) {
    const Editor& ed = *appState().ed;
    const std::vector<std::pair<int, int>> pts = rampBrushStroke(stroke, currentBrush(appState()).size);
    // (a tileset whose ramps are doodads, a mod's: they go on the copy of the map as doodads, or Shift takes them away)
    if (const std::vector<uint16_t> doodads = rampDoodads(ed); !doodads.empty()) {
        std::vector<int> under;
        std::vector<std::pair<int, int>> tilesUnder;  // (the passages' ramps under the stroke: their tiles, made from the ISOM again)
        if (gShape.unramp) {
            for (const auto& [x, y] : pts)
                if (const int d = ed.doodadAt(x, y); d >= 0 && std::find(doodads.begin(), doodads.end(), ed.doodads()[size_t(d)].id) != doodads.end() &&
                                                     std::find(under.begin(), under.end(), d) == under.end())
                    under.push_back(d);
            tilesUnder = rampTilesUnder(ed, pts);
            if (under.empty() && tilesUnder.empty()) return setMessage(L"Ramp with Shift: paint over a ramp to make it the cliff again");
        }
        RampPlan why;
        const std::vector<RampPlan> ramps = gShape.unramp ? std::vector<RampPlan>{} : planRamps(ed, pts, &why);
        if (!gShape.unramp && ramps.empty())
            return setMessage(why.problem == RampPlan::Problem::NoIsom ? L"Could not make a ramp. The map has no isometric terrain data."
                                                                       : L"Ramp: paint along a cliff edge");
        Editor& copy = shapeCopy(ed.gameData());
        std::string error;
        if (!copy.openChk(ed.chk(), error)) return setMessage(bridgeText(L"Could not make the ramp: %ls", widen(error)));
        shapeCopySettings(copy, ed);
        copy.setMirrors(0), copy.setTurns(1);
        int placed = 0;
        if (gShape.unramp) {
            if (!under.empty()) copy.deleteDoodads(under);
            if (!tilesUnder.empty()) copy.remakeTilesFromIsom(tilesUnder);
        }
        for (const RampPlan& p : ramps)  // (a ramp doodad, or a passage's ramp: its tiles)
            placed += p.doodad >= 0 ? copy.placeDoodad(uint16_t(p.doodad), p.x, p.y, true) : copy.placeTileValues(p.tiles) > 0;
        const std::vector<uint16_t>&a = ed.terrain().tiles, &b = copy.terrain().tiles;
        if (a.size() == b.size())
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) gShape.ghost.push_back({int(i), b[i]}), gShape.cells.push_back({int(i % ed.width()), int(i / ed.width()), b[i]});
        gShape.copyHolds = true;
        if (gShape.unramp) {
            for (const auto& [x, y] : tilesUnder) gShape.cells.push_back({x, y, 0});  // (not empty: there is something to do)
            return setMessage(L"Release the button to make the ramp the cliff again");
        }
        if (!placed) return setMessage(L"Ramp: paint along a cliff edge");
        return setMessage(placed > 1 ? bridgeText(L"%ls ramps. Release the button to place them.", std::to_wstring(placed))
                                     : std::wstring(tr(L"Ramp. Release the button to place it.")));
    }
    if (gShape.unramp) {  // (Shift: the ramps under the stroke, made the cliff again on the copy of the map)
        const std::vector<std::pair<int, int>> under = rampTilesUnder(ed, pts);
        if (under.empty()) return setMessage(L"Ramp with Shift: paint over a ramp to make it the cliff again");
        Editor& copy = shapeCopy(ed.gameData());
        std::string error;
        if (!copy.openChk(ed.chk(), error)) return setMessage(bridgeText(L"Could not make the cliff again: %ls", widen(error)));
        shapeCopySettings(copy, ed);
        copy.setMirrors(0), copy.setTurns(1);
        copy.remakeTilesFromIsom(under);
        const std::vector<uint16_t>&a = ed.terrain().tiles, &b = copy.terrain().tiles;
        if (a.size() == b.size())
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) gShape.ghost.push_back({int(i), b[i]});
        gShape.copyHolds = true;
        for (const auto& [x, y] : under) gShape.cells.push_back({x, y, 0});  // (not empty: there is something to do)
        return setMessage(L"Release the button to make the ramp the cliff again");
    }
    RampPlan why;  // (no ramp: why)
    std::vector<RampPlan> ramps = planRamps(ed, pts, &why);
    // (where the stroke has no ramp: the cliff there changed a little on the copy of the map so that one fits, and the
    // ramps planned again on it; the map takes the copy)
    Editor* reshaped = nullptr;
    bool left = false;  // (a point of the stroke 3 tiles or more from the ramps)
    for (size_t i = 0; i < pts.size() && !left; ++i) {
        left = true;
        for (const RampPlan& p : ramps)
            for (const Editor::TileAt& c : p.tiles)
                if (std::hypot(32.0 * c.x + 16 - pts[i].first, 32.0 * c.y + 16 - pts[i].second) <= 96) left = false;
    }
    if (left && ed.canPaintTerrain()) {
        Editor& copy = shapeCopy(ed.gameData());
        std::string error;
        if (copy.openChk(ed.chk(), error)) {
            shapeCopySettings(copy, ed);
            copy.setMirrors(0), copy.setTurns(1);
            if (reshapeForRamps(copy, pts, ramps)) {
                RampPlan why2;
                ramps = planRamps(copy, pts, &why2);
                reshaped = &copy;
            }
        }
    }
    const std::wstring low = tr(widen(why.low)), high = tr(widen(why.high));
    if (ramps.empty()) {
        switch (why.problem) {
        case RampPlan::Problem::NoKit: return setMessage(L"This tileset has no ramps");
        case RampPlan::Problem::NoIsom: return setMessage(L"Could not make a ramp. The map has no isometric terrain data.");
        default: return setMessage(bridgeText(L"Ramp: paint along a cliff edge between %ls and %ls", high, low));
        }
    }
    std::vector<Editor::TileAt> tiles;
    for (const RampPlan& p : ramps) tiles.insert(tiles.end(), p.tiles.begin(), p.tiles.end());
    for (const Editor::TileAt& c : tiles)
        if (ed.tileProtected(c.x, c.y)) {
            for (const Editor::TileAt& r : tiles) gShape.red.push_back({r.x, r.y});
            return setMessage(L"Part of the ramp is on protected tiles");
        }
    if (reshaped) {  // (the changed cliff and the ramps on the copy: the map takes it)
        reshaped->placeTileValues(tiles);
        const std::vector<uint16_t>&a = ed.terrain().tiles, &b = reshaped->terrain().tiles;
        if (a.size() == b.size())
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) gShape.ghost.push_back({int(i), b[i]});
        gShape.copyHolds = true;
    } else {
        for (const Editor::TileAt& c : tiles)
            if (ed.terrain().tiles[size_t(c.y) * ed.width() + c.x] != c.value) gShape.ghost.push_back({c.y * ed.width() + c.x, c.value});
    }
    gShape.cells = tiles;
    const RampPlan& p = ramps.front();
    setMessage(ramps.size() > 1 ? bridgeText(L"%ls ramps. Release the button to place them.", std::to_wstring(ramps.size()))
                                : bridgeText(L"Ramp facing %ls, width %ls. Release the button to place it.", widen(rampFacingName(p.facing)), bridgeSize(p.slopes)));
}

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
    if (!now && (!app.terrainRect || gShape.tool == TBridge || gShape.tool == TRamp) && cost > 12 && std::chrono::duration<double, std::milli>(t0 - last).count() < 2 * cost)
        return shellShapeLater(int(2 * cost));
    gShape.ghost.clear(), gShape.cells.clear(), gShape.red.clear(), gShape.copyHolds = false;
    gShape.madeFor = pts, gShape.madeKey = key, gShape.made = true;
    const std::vector<std::pair<int, int>> geo = shapeGeometry(pts);
    if (geo.size() < (gShape.tool == TRamp ? 1u : gShape.tool == TLine || gShape.tool == TBridge ? 2u : 3u)) return;
    if (gShape.tool == TBridge || gShape.tool == TRamp) {  // (in every kind of terrain: made at most every 2 times its time too)
        gShape.tool == TBridge ? bridgeMake(geo) : rampMake(geo);
        last = std::chrono::steady_clock::now();
        cost = std::chrono::duration<double, std::milli>(last - t0).count();
        return;
    }
    if (app.terrainRect) return shapeMakeTiles(geo);
    Editor& c = shapeCopy(ed.gameData());
    std::string error;
    if (!c.openChk(ed.chk(), error)) {
        gShape.made = false;
        return;
    }
    shapeCopySettings(c, ed);
    shapeIsoPaint(c, gShape.tool, geo, uint16_t(app.palette), currentBrush(app));
    refitRamps(c, ed.terrain().tiles);  // (the ramps the shape touched: fitted to its cliffs again, in the preview too)
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
    if (currentTool(app) == TBridge || currentTool(app) == TRamp) {  // (no pick: the bridge's and the ramp's tiles are the kits')
        if (!app.ed->canPaintTerrain())
            return setMessage(currentTool(app) == TBridge ? L"Could not make a bridge. The map has no isometric terrain data."
                                                          : L"Could not make a ramp. The map has no isometric terrain data."),
                   false;
        return true;
    }
    if (!app.terrainRect) {
        if (!app.ed->canPaintTerrain()) return setMessage(L"Could not paint the terrain. The map has no isometric terrain data."), false;
        if (app.palette <= 0) return setMessage(L"Select a terrain in the list first"), false;
        return true;
    }
    tilesFresh();
    if (tilesHavePick()) return true;
    setMessage(gTiles->stamps  ? L"Select custom terrain in the Custom list first"
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
    if (tool != TLine && tool != TBridge && !isBoxTool(tool)) pts.erase(std::unique(pts.begin(), pts.end()), pts.end());  // (a Line click: the brush at the point)
    if (isBoxTool(tool) ? shapeBoxPoints(tool, pts).empty() : tool != TLine && tool != TBridge && tool != TRamp && pts.size() < 3) {
        setMessage(isBoxTool(tool)       ? L"Did not paint the shape: drag a larger box"
                   : tool == TPolygon    ? L"Did not fill the polygon: a polygon has 3 points or more"
                                         : L"Did not fill the contour: draw a larger shape");
        shapeReset();
        app.dirty = true;
        return;
    }
    gShape.points = pts;
    gShape.made = false;  // (made again now, of the map as it is: the copy that the map takes holds it)
    shapeUpdate(true);
    if (tool == TRamp && gShape.unramp) {
        if (gShape.cells.empty() || !gShape.copyHolds || gShape.ghost.empty()) {  // (the status bar says why: rampMake)
            shapeReset();
            app.dirty = true;
            return;
        }
        // (the map takes the copy's scenario: the units and the locations that were selected stay selected)
        const std::vector<int> units = ed.selection(), locations = ed.selectedLocations();
        const size_t unitCount = ed.units().size();
        ed.takeScenario(shapeCopy(ed.gameData()));
        if (ed.units().size() == unitCount)
            for (int u : units) ed.select(u, true);
        for (int l : locations) ed.addLocationSelection(l);
        setMessage(L"Made the ramp the cliff again");
    } else if (tool == TRamp) {
        if (gShape.cells.empty()) {  // (the status bar says why: rampMake)
            shapeReset();
            app.dirty = true;
            return;
        }
        if (gShape.copyHolds) {  // (doodad ramps, or a cliff changed to fit: the map takes the copy's scenario, its doodads with it)
            const Editor& copy = shapeCopy(ed.gameData());
            const bool removed = copy.doodads().size() < ed.doodads().size() || copy.units().size() < ed.units().size();  // (by the changed cliff)
            const std::vector<int> units = ed.selection(), locations = ed.selectedLocations();
            const size_t unitCount = ed.units().size();
            ed.takeScenario(shapeCopy(ed.gameData()));
            if (ed.units().size() == unitCount)
                for (int u : units) ed.select(u, true);
            for (int l : locations) ed.addLocationSelection(l);
            setMessage(removed ? L"Placed the ramp. Removed the doodads and units under it that cannot stay there."
                       : ed.mirrors() || ed.turns() > 1 ? L"Placed the ramp. Symmetry does not apply to ramps."
                                                        : L"Placed the ramp");
            shapeReset();
            app.dirty = true;
            return;
        }
        const bool changed = ed.placeTileValues(gShape.cells) > 0;
        const bool removed = ed.takeDoodadsRemovedByTerrain() + ed.takeUnitsRemovedByTerrain() > 0;
        setMessage(!changed ? L"The ramp is there already"
                   : removed ? L"Placed the ramp. Removed the doodads and units under it that cannot stay there."
                   : ed.mirrors() || ed.turns() > 1 ? L"Placed the ramp. Symmetry does not apply to ramps."
                                                    : L"Placed the ramp");
    } else if (tool == TBridge) {
        if (gShape.cells.empty() || !gShape.copyHolds) {  // (the status bar says why: bridgeMake)
            shapeReset();
            app.dirty = true;
            return;
        }
        const bool changed = !gShape.ghost.empty();
        const Editor& copy = shapeCopy(ed.gameData());
        const bool removed = copy.doodads().size() < ed.doodads().size() || copy.units().size() < ed.units().size();  // (by the shore)
        if (changed) {  // (the map takes the copy's scenario: the units and the locations that were selected stay selected)
            const std::vector<int> units = ed.selection(), locations = ed.selectedLocations();
            const size_t unitCount = ed.units().size();
            ed.takeScenario(shapeCopy(ed.gameData()));
            if (ed.units().size() == unitCount)
                for (int u : units) ed.select(u, true);
            for (int l : locations) ed.addLocationSelection(l);
        }
        setMessage(!changed ? L"The bridge is there already"
                   : removed ? L"Placed the bridge. Removed the doodads and units under it that cannot stay there."
                   : ed.mirrors() || ed.turns() > 1 ? L"Placed the bridge. Symmetry does not apply to bridges."
                                                    : L"Placed the bridge");
    } else if (app.terrainRect) {
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
        setMessage(tool == TLine ? L"Painted the line" : tool == TContour ? L"Filled the contour" : tool == TPolygon ? L"Filled the polygon"
                   : tool == TRect ? L"Painted the rectangle" : tool == TRectFill ? L"Filled the rectangle" : tool == TEllipse ? L"Painted the ellipse"
                   : tool == TEllipseFill ? L"Filled the ellipse" : tool == TDiamond ? L"Painted the diamond" : L"Filled the diamond");
    }
    shapeReset();
    app.dirty = true;
}

// Shift (Line, Polygon): the point from `a` goes in the nearest of the map's directions (horizontal, vertical, 45 degrees
// and the isometric diamond's edges, 2:1), as far along it as the mouse.
std::pair<int, int> shapeSnap(std::pair<int, int> a, std::pair<int, int> p) {
    const float dx = float(p.first - a.first), dy = float(p.second - a.second);
    static const float dirs[][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}, {2, 1}, {2, -1}};
    float best = -1, ux = 1, uy = 0, along = 0;
    for (const auto& d : dirs) {
        const float n = std::hypot(d[0], d[1]), x = d[0] / n, y = d[1] / n, dot = dx * x + dy * y;
        if (std::fabs(dot) > best) best = std::fabs(dot), ux = x, uy = y, along = dot;
    }
    return {a.first + int(std::lround(ux * along)), a.second + int(std::lround(uy * along))};
}

// Bridge: the point from `a` on the nearer of the map's two diagonals (2:1), as far along it as the mouse.
std::pair<int, int> bridgeSnap(std::pair<int, int> a, std::pair<int, int> p) {
    const float dx = float(p.first - a.first), dy = float(p.second - a.second), n = std::hypot(2.f, 1.f);
    const float down = (dx * 2 + dy) / n, up = (dx * 2 - dy) / n;  // (along (2, 1) and (2, -1))
    if (std::fabs(down) >= std::fabs(up)) return {a.first + int(std::lround(down * 2 / n)), a.second + int(std::lround(down / n))};
    return {a.first + int(std::lround(up * 2 / n)), a.second - int(std::lround(up / n))};
}

// A press on the map with a shape tool: Line and Contour start (Drag::Shape), Polygon adds a point (the first starts it).
// snap: Shift held (shapeSnap).
void shapeMouseDown(float wx, float wy, bool snap) {
    AppState& app = appState();
    app.drag = Drag::None;
    if (!shapeHasPick()) return;
    const int tool = currentTool(app);
    const std::pair<int, int> p{int(wx), int(wy)};
    if (tool == TPolygon && shapeOn()) {
        const std::pair<int, int> q = snap ? shapeSnap(gShape.points.back(), p) : p;
        if (gShape.points.back() != q) gShape.points.push_back(q);
        gShape.hasHover = false;
        shapeUpdate();
        return;
    }
    shapeReset();
    gShape.tool = tool, gShape.ed = app.ed.get(), gShape.seed = app.spraySeed++;
    gShape.unramp = tool == TRamp && snap;
    gShape.points = {p};
    if (tool == TLine || tool == TBridge || isBoxTool(tool)) gShape.points.push_back(p);
    if (tool != TPolygon) app.drag = Drag::Shape;
    setMessage(tool == TBridge    ? L"Bridge: drag from the ground across the water to the ground on the other side"
               : tool == TRamp    ? (snap ? L"Ramp with Shift: paint over a ramp to make it the cliff again" : L"Ramp: paint along a cliff edge")
               : tool == TLine    ? L"Line: release the button at the end of the line. Hold Shift to lock the angle"
               : isBoxTool(tool)  ? L"Release the button at the other corner. Hold Shift for a square, a circle or a diamond of the map's shape"
               : tool == TContour ? L"Contour: draw the shape, then release the button"
                                  : L"Polygon: click to add points. Double-click to close the polygon and fill it. Hold Shift to lock the angle. Esc stops the polygon.");
    shapeUpdate();
}

// The mouse moves: Line's end, Contour's next point (each 6 screen pixels or more from the last one), Polygon's next point.
void shapeMouseMove(float wx, float wy, bool snap) {
    if (!shapeOn()) return;
    const AppState& app = appState();
    const std::pair<int, int> p{int(wx), int(wy)};
    if (gShape.tool == TPolygon) {
        gShape.hasHover = true, gShape.hover = snap ? shapeSnap(gShape.points.back(), p) : p;
    } else if (app.drag != Drag::Shape) {
        return;
    } else if (gShape.tool == TLine) {
        gShape.points.back() = snap ? shapeSnap(gShape.points.front(), p) : p;
    } else if (gShape.tool == TBridge) {
        gShape.points.back() = bridgeSnap(gShape.points.front(), p);
    } else if (isBoxTool(gShape.tool)) {
        gShape.points.back() = snap ? shapeSquare(gShape.points.front(), p, gShape.tool) : p;
    } else {
        const auto& q = gShape.points.back();
        if (std::hypot(float(p.first - q.first), float(p.second - q.second)) * app.zoom < 6) return;
        gShape.points.push_back(p);
    }
    shapeUpdate();
}

// The release of a Line or Contour press: the shape goes on the map.
void shapeMouseUp(float wx, float wy, bool snap) {
    if (shapeOn()) {
        const std::pair<int, int> p{int(wx), int(wy)};
        if (gShape.tool == TLine) gShape.points.back() = snap ? shapeSnap(gShape.points.front(), p) : p;
        else if (gShape.tool == TBridge) gShape.points.back() = bridgeSnap(gShape.points.front(), p);
        else if (isBoxTool(gShape.tool)) gShape.points.back() = snap ? shapeSquare(gShape.points.front(), p, gShape.tool) : p;
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
    setMessage(gShape.tool == TLine      ? L"Stopped the line"
               : gShape.tool == TContour ? L"Stopped the contour"
               : gShape.tool == TBridge  ? L"Stopped the bridge"
               : gShape.tool == TRamp    ? L"Stopped the ramp"
                                         : L"Stopped the polygon");
    shapeReset();
    appState().drag = Drag::None;
    appState().dirty = true;
    return true;
}
