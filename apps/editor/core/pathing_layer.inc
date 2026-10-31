// The pathing layer and the pathing check (roadmap R35, feedback #75; portable, core.inc), from the library's regions
// (stardraft/pathing_regions.h): StarCraft's ground pathing as the game computes it when a game starts.
//   - View > Overlays > Pathing: the walkable minitiles, each region of the game in its own tint with its borders, and
//     the ground that units cannot walk on dark. View > Overlays > Pathing shows the connected areas: each group of
//     regions that ground units can go between in its own tint (the game's region groups).
//   - Scenario > Check pathing: the nooks and crannies (checkPathing) in a list picker, each with its place and a fix.
//     A pick shows the place on the map (the layer goes on, the problem has a mark) and the status bar tells the fix;
//     "Apply the fix" (a pocket in one tile) changes that tile to one with no walkable minitile there, as one undo step.
//     The problems stay marked while the layer shows; after a change of the terrain the check runs again.
// The regions and the check are made on a worker thread from copies of the terrain and the units when the map changes
// (its revision); the map shows the last result until the new one is ready.

// The layer's shapes with no colors (map pixels), for one mode: the fills (kind: the region or the connected area,
// -1 the ground that units cannot walk on) and the borders between the kinds.
struct PathingShape {
    float x0, y0, x1, y1;
    int kind;
};
struct PathingShapes {
    std::vector<PathingShape> fills, borders;
};

struct PathingLayerResult {
    PathingRegions regions;
    PathingCheck check;
    PathingShapes shapes[2];  // [0] the regions, [1] the connected areas (made on the worker thread)
};

PathingShapes makePathingShapes(const PathingRegions& r, bool areas);

struct PathingLayer {
    bool on = false;
    bool areas = false;  // the connected areas (else the regions)
    std::shared_ptr<const PathingLayerResult> result;
    const Editor* resultFor = nullptr;
    uint64_t resultRev = 0;
    std::future<std::shared_ptr<PathingLayerResult>> job;
    const Editor* jobFor = nullptr;
    uint64_t jobRev = 0;
    std::shared_ptr<const Tileset> tileset;  // the parts the regions and the check read (groups, megatiles, VF4 flags)
    const Tileset* tilesetOf = nullptr;
    int tilesetEra = -1;
    // The shapes with their colors, made again (from the result's shapes) when the result, the mode or the theme changes.
    std::vector<render::RectDraw> fills, borders;
    uint64_t shapesFor = 0;   // the result's serial
    uint64_t serial = 0;      // of the result: +1 for each new one
    int shapesKey = -1;
    // The map that Scenario > Check pathing ran for (its problems show then); the problem picked (-1 none).
    const Editor* checkFor = nullptr;
    int shown = -1;
};
PathingLayer gPathing;

bool pathingLayerOn() { return gPathing.on; }
bool pathingLayerAreas() { return gPathing.areas; }

// Is the result for the map that shows (and its size)?
const PathingLayerResult* pathingResult() {
    const AppState& app = appState();
    if (!app.ed || gPathing.resultFor != app.ed.get() || !gPathing.result) return nullptr;
    const PathingRegions& r = gPathing.result->regions;
    if (r.width != app.ed->width() || r.height != app.ed->height()) return nullptr;
    return gPathing.result.get();
}
const PathingRegions* pathingRegions() {
    const PathingLayerResult* r = pathingResult();
    return r ? &r->regions : nullptr;
}

// Start the regions and the check when the map changed, and take finished ones. `wait`: until they are current (the
// check's command, tests).
void pathingLayerPoll(bool wait = false) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    for (;;) {
        if (gPathing.job.valid()) {
            if (!wait && gPathing.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            gPathing.result = gPathing.job.get();
            gPathing.resultFor = gPathing.jobFor, gPathing.resultRev = gPathing.jobRev, ++gPathing.serial;
            gPathing.shown = -1;  // (the problems of the new check are in another order)
            app.dirty = true;
        }
        if (gPathing.result && gPathing.resultFor == &ed && gPathing.resultRev == ed.revision()) return;
        const Tileset& ts = ed.tileset();
        if (!gPathing.tileset || gPathing.tilesetOf != &ts || gPathing.tilesetEra != ed.era()) {
            auto slim = std::make_shared<Tileset>();
            slim->groups = ts.groups, slim->megatiles = ts.megatiles, slim->minitileFlags = ts.minitileFlags, slim->era = ts.era;
            gPathing.tileset = slim, gPathing.tilesetOf = &ts, gPathing.tilesetEra = ed.era();
        }
        gPathing.jobFor = &ed, gPathing.jobRev = ed.revision();
        gPathing.job = std::async(std::launch::async, [terrain = ed.terrain(), units = ed.units(), tileset = gPathing.tileset] {
            auto r = std::make_shared<PathingLayerResult>();
            r->regions = computePathingRegions(terrain, *tileset);
            r->check = checkPathing(r->regions, terrain, *tileset, units);
            r->shapes[0] = makePathingShapes(r->regions, false), r->shapes[1] = makePathingShapes(r->regions, true);
            return r;
        });
        if (!wait) return;
    }
}

// The check of the map that shows, once Scenario > Check pathing ran for it (`make`: it runs now, and the result is
// for the map as it is now). After a change, the last result until the new one is ready.
const PathingCheck* pathingCheck(bool make = false) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return nullptr;
    if (make) gPathing.checkFor = app.ed.get(), pathingLayerPoll(true);
    if (gPathing.checkFor != app.ed.get()) return nullptr;
    const PathingLayerResult* r = pathingResult();
    return r ? &r->check : nullptr;
}

// The tint of region (or connected area) k: the hues around the circle, with the theme's lightness.
uint32_t pathingTint(int k) {
    const double hue = std::fmod(20.0 + 137.508 * k, 360.0);
    const double light = gLightTheme ? 0.55 + 0.1 * (k % 3) : 0.62 + 0.1 * (k % 3);
    return rgbaOf(theme::color(light, 0.17, hue), 0x60);
}

// The fills (runs of minitiles of one kind on each row, joined with the same run of the row above) and the borders
// between the regions (or the connected areas). It reads no UI state (the worker thread makes them).
PathingShapes makePathingShapes(const PathingRegions& r, bool areas) {
    PathingShapes out;
    const int w = r.walkW(), h = r.walkH();
    // Per minitile: -1 not walkable, else its region (or its region's connected area).
    std::vector<int32_t> kind(size_t(w) * h, -1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!r.walkable(x, y)) continue;
            const int ri = r.regionAt(x, y);
            if (ri < 0) continue;
            kind[size_t(y) * w + x] = areas ? r.regions[size_t(ri)].group : ri;
        }
    struct Run {
        int x0, x1, kind, y0;
    };
    std::vector<Run> open, row, next;
    auto emit = [&](const Run& run, int y1) { out.fills.push_back({run.x0 * 8.0f, run.y0 * 8.0f, run.x1 * 8.0f, y1 * 8.0f, run.kind}); };
    for (int y = 0; y <= h; ++y) {
        row.clear();
        if (y < h)
            for (int x = 0; x < w;) {
                const int k = kind[size_t(y) * w + x];
                int e = x + 1;
                while (e < w && kind[size_t(y) * w + e] == k) ++e;
                row.push_back({x, e, k, y});
                x = e;
            }
        next.clear();
        size_t i = 0;
        for (Run run : row) {
            while (i < open.size() && open[i].x0 < run.x0) emit(open[i], y), ++i;
            if (i < open.size() && open[i].x0 == run.x0 && open[i].x1 == run.x1 && open[i].kind == run.kind) run.y0 = open[i].y0, ++i;
            next.push_back(run);
        }
        for (; i < open.size(); ++i) emit(open[i], y);
        std::swap(open, next);
    }
    // The borders: where two walkable minitiles of different kinds meet (the shade shows the edges of the ground).
    auto border = [&](float x0, float y0, float x1, float y1) { out.borders.push_back({x0, y0, x1, y1, 0}); };
    auto differ = [&](size_t a, size_t b) { return kind[a] >= 0 && kind[b] >= 0 && kind[a] != kind[b]; };
    for (int y = 1; y < h; ++y)
        for (int x = 0; x < w;) {
            if (!differ(size_t(y - 1) * w + x, size_t(y) * w + x)) {
                ++x;
                continue;
            }
            int e = x + 1;
            while (e < w && differ(size_t(y - 1) * w + e, size_t(y) * w + e)) ++e;
            border(x * 8.0f, y * 8.0f, e * 8.0f, y * 8.0f);
            x = e;
        }
    for (int x = 1; x < w; ++x)
        for (int y = 0; y < h;) {
            if (!differ(size_t(y) * w + x - 1, size_t(y) * w + x)) {
                ++y;
                continue;
            }
            int e = y + 1;
            while (e < h && differ(size_t(e) * w + x - 1, size_t(e) * w + x)) ++e;
            border(x * 8.0f, y * 8.0f, x * 8.0f, e * 8.0f);
            y = e;
        }
    return out;
}

// The shapes of the mode that shows, with the theme's colors.
void colorPathingShapes(const PathingLayerResult& res) {
    const int key = (gLightTheme ? 1 : 0) | (gPathing.areas ? 2 : 0) | (gAccentIndex << 2);
    if (gPathing.shapesFor == gPathing.serial && gPathing.shapesKey == key) return;
    gPathing.shapesFor = gPathing.serial, gPathing.shapesKey = key;
    const PathingShapes& g = res.shapes[gPathing.areas ? 1 : 0];
    gPathing.fills.clear(), gPathing.borders.clear();
    gPathing.fills.reserve(g.fills.size()), gPathing.borders.reserve(g.borders.size());
    const uint32_t shade = rgbaOf(kBg, gLightTheme ? 0xA8 : 0xB8);
    const uint32_t line = gLightTheme ? 0x202020A0u : 0xFFFFFF90u;
    for (const PathingShape& f : g.fills) {
        render::RectDraw d;
        d.x0 = f.x0, d.y0 = f.y0, d.x1 = f.x1, d.y1 = f.y1, d.rgba = f.kind < 0 ? shade : pathingTint(f.kind), d.fill = true;
        gPathing.fills.push_back(d);
    }
    for (const PathingShape& b : g.borders) {
        render::RectDraw d;
        d.x0 = b.x0, d.y0 = b.y0, d.x1 = b.x1, d.y1 = b.y1, d.rgba = line, d.line = true, d.thickness = 1;
        gPathing.borders.push_back(d);
    }
}

// --- The texts of the problems ---------------------------------------------------------------------------------------

std::wstring pathingFormat(const wchar_t* english, int a = 0, int b = 0, int c = 0) {
    wchar_t buf[400];
    std::swprintf(buf, 400, tr(english), a, b, c);
    return buf;
}

std::wstring pathingProblemText(const PathingProblem& p) {
    const int tx = p.x / 32, ty = p.y / 32;
    switch (p.kind) {
    case PathingProblemKind::TooManyRegions: return tr(L"Too many pathing regions: StarCraft does not start the map");
    case PathingProblemKind::Pocket:
        return p.fitsUnit ? pathingFormat(L"Walkable ground of %d minitiles that no start location can reach, at tile %d, %d", p.minitiles, tx, ty)
                          : pathingFormat(L"Walkable pocket of %d minitiles that no unit can use, at tile %d, %d", p.minitiles, tx, ty);
    case PathingProblemKind::Gap: return pathingFormat(L"Gap that no unit fits through, but the game's pathing goes through it, at tile %d, %d", tx, ty);
    case PathingProblemKind::SmallUnitsOnly: return pathingFormat(L"Passage that only small units fit through, at tile %d, %d", tx, ty);
    case PathingProblemKind::ClosedPassage:
        return pathingFormat(L"Ground that units can walk onto, but the game's pathing does not connect, at tile %d, %d", tx, ty);
    case PathingProblemKind::SmallRegion: return pathingFormat(L"Small pathing region of %d minitiles, at tile %d, %d", p.minitiles, tx, ty);
    }
    return {};
}

std::wstring pathingFixText(const PathingProblem& p) {
    switch (p.kind) {
    case PathingProblemKind::TooManyRegions: return tr(L"Fix: make fewer small walkable and unwalkable areas");
    case PathingProblemKind::Pocket:
        if (p.fixTileX >= 0) return tr(L"Fix: use Apply the fix, or put an unwalkable tile or a doodad here");
        return p.fitsUnit ? tr(L"Fix: connect it to the ground beside it, or make it unwalkable")
                          : tr(L"Fix: put an unwalkable tile or a doodad here");
    case PathingProblemKind::Gap: return tr(L"Fix: close it with an unwalkable tile or a doodad, or make it 2 minitiles (16 pixels) wide");
    case PathingProblemKind::SmallUnitsOnly: return tr(L"Fix: make it 4 minitiles (32 pixels) wide for medium units, or close it");
    case PathingProblemKind::ClosedPassage: return tr(L"Fix: change the tiles here so that the ground connects, or make it unwalkable");
    case PathingProblemKind::SmallRegion: return tr(L"Fix: make the ground here one level, or make it unwalkable");
    }
    return {};
}

const wchar_t* pathingSection(PathingProblemKind k) {
    switch (k) {
    case PathingProblemKind::TooManyRegions: return L"Too many regions";
    case PathingProblemKind::Pocket: return L"Pockets";
    case PathingProblemKind::Gap: return L"Gaps";
    case PathingProblemKind::SmallUnitsOnly: return L"Small units only";
    case PathingProblemKind::ClosedPassage: return L"Closed passages";
    case PathingProblemKind::SmallRegion: return L"Small regions";
    }
    return L"";
}

// --- The map ---------------------------------------------------------------------------------------------------------

void addPathingLayer(render::Scene& s) {
    if (!gPathing.on) return;
    pathingLayerPoll();
    const PathingLayerResult* res = pathingResult();
    if (!res) return;
    const PathingRegions* r = &res->regions;
    colorPathingShapes(*res);
    const AppState& app = appState();
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    auto visible = [&](const render::RectDraw& d) { return d.x1 >= vx0 && d.x0 <= vx1 && d.y1 >= vy0 && d.y0 <= vy1; };
    for (const render::RectDraw& d : gPathing.fills)
        if (visible(d)) s.rects.push_back(d);
    if (app.zoom * 8 >= 3)  // (the borders while a minitile is 3 screen pixels or more)
        for (const render::RectDraw& d : gPathing.borders)
            if (visible(d)) s.rects.push_back(d);
    // The problems of the check: a box around each, the one picked with its minitiles filled.
    const PathingCheck* c = pathingCheck();
    if (!c) return;
    const float px = 1 / app.zoom;
    for (size_t i = 0; i < c->problems.size(); ++i) {
        const PathingProblem& p = c->problems[i];
        const bool hot = int(i) == gPathing.shown;
        const float m = (hot ? 6 : 3) * px;
        render::RectDraw d;
        d.x0 = p.x0 * 8.0f - m, d.y0 = p.y0 * 8.0f - m, d.x1 = p.x1 * 8.0f + m, d.y1 = p.y1 * 8.0f + m;
        if (!visible(d)) continue;
        d.rgba = rgbaOf(hot ? kFeedback : kWarn, 0xFF), d.thickness = hot ? 3.0f : 2.0f;
        s.rects.push_back(d);
        if (!hot) continue;
        const int w = r->walkW();
        for (size_t k = 0; k < p.cells.size();) {  // (runs of the cells on a row)
            size_t e = k + 1;
            while (e < p.cells.size() && p.cells[e] == p.cells[e - 1] + 1 && p.cells[e] / w == p.cells[k] / w) ++e;
            render::RectDraw f;
            f.x0 = (p.cells[k] % w) * 8.0f, f.y0 = (p.cells[k] / w) * 8.0f, f.x1 = f.x0 + 8.0f * float(e - k), f.y1 = f.y0 + 8;
            f.rgba = rgbaOf(kBad, 0xA0), f.fill = true;
            s.rects.push_back(f);
            k = e;
        }
    }
}

// The status bar's text for the place under the mouse (map pixels): a problem, else the region.
std::wstring pathingHover(float wx, float wy) {
    const PathingRegions* r = gPathing.on ? pathingRegions() : nullptr;
    if (!r) return {};
    if (const PathingCheck* c = pathingCheck())
        for (const PathingProblem& p : c->problems)
            if (wx >= p.x0 * 8.0f - 4 && wx < p.x1 * 8.0f + 4 && wy >= p.y0 * 8.0f - 4 && wy < p.y1 * 8.0f + 4)
                return pathingProblemText(p) + L". " + pathingFixText(p);
    const int mx = int(std::floor(wx / 8)), my = int(std::floor(wy / 8));
    const int ri = r->regionAt(mx, my);
    if (ri < 0) return {};
    const PathingRegion& g = r->regions[size_t(ri)];
    if (!r->walkable(mx, my)) return tr(L"Pathing: units cannot walk here");
    static const wchar_t* const levels[3] = {L"low ground", L"middle ground", L"high ground"};
    int inArea = 0;
    for (const PathingRegion& o : r->regions) inArea += o.group == g.group;
    wchar_t buf[300];
    std::swprintf(buf, 300, tr(L"Pathing region %d: %d tiles, %ls; connected area %d (%d regions)"), ri, g.tiles, tr(levels[g.elevation()]), g.group, inArea);
    return buf;
}

void pathingLayerMouseMove(float wx, float wy) {
    if (!gPathing.on) return;
    const std::wstring text = pathingHover(wx, wy);
    if (!text.empty()) setHint(text);
}

std::wstring pathingLegendText() {
    return gPathing.areas ? tr(L"Pathing: each connected area (the ground that units can walk across) has its own color. Dark: units cannot walk here")
                          : tr(L"Pathing: each pathing region of the game has its own color. Dark: units cannot walk here");
}

void pathingLayerToggle() {
    gPathing.on = !gPathing.on;
    pathingLayerPoll();
    shellMenusChanged();
    appState().dirty = true;
    setMessage(gPathing.on ? pathingLegendText() : tr(L"Pathing is hidden (View > Overlays)"));
}

void pathingLayerMode(bool areas) {
    gPathing.areas = areas, gPathing.on = true;
    pathingLayerPoll();
    shellMenusChanged();
    appState().dirty = true;
    setMessage(pathingLegendText());
}

// Shows problem i of the check: the layer on, the problem in the middle of the view, its text and fix in the status bar.
void pathingShowProblem(int i) {
    const PathingCheck* c = pathingCheck();
    if (!c || i < 0 || size_t(i) >= c->problems.size()) return;
    const PathingProblem& p = c->problems[size_t(i)];
    AppState& app = appState();
    gPathing.on = true, gPathing.shown = i;
    int vw = 0, vh = 0;
    shellViewSize(vw, vh);
    app.camX = (p.x0 + p.x1) * 4.0f - vw / 2.0f / app.zoom, app.camY = (p.y0 + p.y1) * 4.0f - vh / 2.0f / app.zoom;
    shellMenusChanged();
    app.dirty = true;
    setMessage(pathingProblemText(p) + L". " + pathingFixText(p));
}

// Apply the fix of problem i (a pocket in one tile): that tile becomes the tile with no walkable minitile there.
void pathingApplyFix(int i) {
    const PathingCheck* c = pathingCheck();
    AppState& app = appState();
    if (!c || i < 0 || size_t(i) >= c->problems.size() || !app.ed) return;
    const PathingProblem p = c->problems[size_t(i)];
    if (p.fixTileX < 0) return;
    Editor& ed = *app.ed;
    if (ed.doodadAt(p.fixTileX * 32 + 16, p.fixTileY * 32 + 16) >= 0) {
        setMessage(tr(L"A doodad is on this tile: the fix would remove it. Change the tile in Terrain > Tile"));
        return;
    }
    if (!ed.placeTileValues({Editor::TileAt{p.fixTileX, p.fixTileY, p.fixValue}})) {
        setMessage(tr(L"The fix did not change the map"));
        return;
    }
    gPathing.shown = -1;
    pathingLayerPoll(true);
    app.dirty = true;
    setMessage(pathingFormat(L"Tile %d, %d has no walkable minitile now (Ctrl+Z undoes it)", p.fixTileX, p.fixTileY));
}

// The picker of the problems (the host's: sdl_map_dialogs.inc): true with the picked row's value.
bool shellPickPathingProblem(std::vector<ListPickItem> items, std::string& out);

// Scenario > Check pathing.
void pathingCheckCommand() {
    const PathingCheck* c = pathingCheck(true);
    const PathingRegions* r = pathingRegions();
    if (!c || !r) return;
    gPathing.on = true;
    shellMenusChanged();
    appState().dirty = true;
    if (c->problems.empty()) {
        setMessage(pathingFormat(L"No pathing problems: %d regions, %d connected areas", int(r->regions.size()), r->groups));
        return;
    }
    std::vector<ListPickItem> items;
    for (size_t i = 0; i < c->problems.size(); ++i) {
        const PathingProblem& p = c->problems[i];
        const std::wstring section = tr(pathingSection(p.kind));
        items.push_back(ListPickItem{pathingProblemText(p), section, "p:" + std::to_string(i), "", -1});
        if (p.fixTileX >= 0)
            items.push_back(ListPickItem{pathingFormat(L"Apply the fix: tile %d, %d gets no walkable minitile there", p.fixTileX, p.fixTileY), section,
                                         "f:" + std::to_string(i), "", -1});
    }
    if (c->more) items.push_back(ListPickItem{pathingFormat(L"%d more problems are not in the list", int(c->more)), L"", "", "", -1});
    setMessage(pathingFormat(L"Pathing: %d problems (%d regions, %d connected areas). Pick one to show it on the map",
                             int(c->problems.size() + c->more), int(r->regions.size()), r->groups));
    std::string picked;
    if (!shellPickPathingProblem(std::move(items), picked) || picked.size() < 3) return;
    const int i = std::atoi(picked.c_str() + 2);
    if (picked[0] == 'f') pathingApplyFix(i);
    else pathingShowProblem(i);
}

// For the snapshots' scripts (sdl_soak.inc: "pathing"): the layer, the regions and the check.
std::string pathingStateLine() {
    pathingLayerPoll(true);
    const PathingRegions* r = pathingRegions();
    std::string s = std::string("pathing: layer ") + (gPathing.on ? "on" : "off") + (gPathing.areas ? ", connected areas" : ", regions");
    if (!r) return s + ", no regions";
    s += ", " + std::to_string(r->regions.size()) + " regions, " + std::to_string(r->groups) + " connected areas";
    if (const PathingLayerResult* res = pathingResult()) {  // (the shapes of the mode that shows, from the worker thread)
        const PathingShapes& g = res->shapes[gPathing.areas ? 1 : 0];
        s += "; shapes " + std::to_string(g.fills.size()) + " fills, " + std::to_string(g.borders.size()) + " borders";
    }
    if (const PathingCheck* c = pathingCheck()) {
        s += "; check: " + std::to_string(c->problems.size() + c->more) + " problems (pockets " + std::to_string(c->pockets) + ", gaps " +
             std::to_string(c->gaps) + ", small units only " + std::to_string(c->narrow) + ", closed " + std::to_string(c->closed) + ", small regions " +
             std::to_string(c->smallRegions) + ")";
        if (gPathing.shown >= 0 && size_t(gPathing.shown) < c->problems.size()) {
            const PathingProblem& p = c->problems[size_t(gPathing.shown)];
            s += "; shown: " + std::string(pathingProblemName(p.kind)) + " at tile " + std::to_string(p.x / 32) + "," + std::to_string(p.y / 32);
        }
    }
    if (appState().ed) {
        char cam[64];
        std::snprintf(cam, sizeof cam, "; camera %.0f,%.0f; undo steps %zu", appState().camX, appState().camY, appState().ed->undoSteps());
        s += cam;
    }
    return s;
}
