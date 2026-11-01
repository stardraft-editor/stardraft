// View > Overlays > Invalid resource depot and Invalid resource node (GitHub feedback #181; SCMDraft's names): the ground
// where the placement rule of the resource distance (units.cpp: town halls and start locations keep 3 tiles between
// their box and the boxes of resources, both ways) refuses a unit.
//   - Invalid resource depot: around each resource (minerals, a geyser), its box grown by 3 tiles. The box of a town
//     hall or a start location may not reach into it.
//   - Invalid resource node: around each town hall and start location, its box grown by 3 tiles. The box of a resource
//     may not reach into it.
// A Cosmonarchy BW map's Town Center Markers count as town halls: the mod's computer players build their town hall
// (128 x 96: Ministry, Nexus, Hachirosk, Landlock) on each one.

bool gResourceZones[2] = {false, false};  // (0: invalid resource depot, 1: invalid resource node)

bool modTab();  // (core/cmbw.inc)
// What a unit type is for the resource distance: 0 a resource, 1 a town hall (or a start location, or a mod's Town
// Center Marker), -1 neither; with the box that keeps the distance.
int resourceZoneKind(const Editor& ed, uint16_t type, int& w, int& h) {
    if (type >= ed.unitTypes().size()) return -1;
    const UnitType& t = ed.unitTypes()[type];
    w = t.placementWidth, h = t.placementHeight;
    if (type == 192 && modTab()) return w = 128, h = 96, 1;  // (the mod's Town Center Marker)
    if (!t.is(UnitFlag::Building)) return -1;
    return t.is(UnitFlag::ResourceContainer) ? 0 : t.is(UnitFlag::ResourceDepot) ? 1 : -1;
}

// The zones as one area: each point is covered once, so that zones that overlap have the same color, and the line
// (when `line` is not 0) goes only round the outside. The union is cut on the zones' edges into cells; the covered cells
// of each row are joined into rects, and the edges between a covered cell and one that is not are joined into lines.
void addZoneUnion(render::Scene& s, const std::vector<render::RectDraw>& zones, uint32_t fill, uint32_t line) {
    if (zones.empty()) return;
    std::vector<float> xs, ys;
    for (const render::RectDraw& r : zones) xs.push_back(r.x0), xs.push_back(r.x1), ys.push_back(r.y0), ys.push_back(r.y1);
    std::sort(xs.begin(), xs.end()), xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end()), ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    const int nx = int(xs.size()) - 1, ny = int(ys.size()) - 1;
    std::vector<uint8_t> cov(size_t(nx) * size_t(ny), 0);
    auto index = [](const std::vector<float>& v, float f) { return int(std::lower_bound(v.begin(), v.end(), f) - v.begin()); };
    for (const render::RectDraw& r : zones)
        for (int j = index(ys, r.y0), j1 = index(ys, r.y1); j < j1; ++j)
            for (int i = index(xs, r.x0), i1 = index(xs, r.x1); i < i1; ++i) cov[size_t(j) * nx + i] = 1;
    auto covered = [&](int i, int j) { return i >= 0 && j >= 0 && i < nx && j < ny && cov[size_t(j) * nx + i]; };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx;) {
            if (!covered(i, j)) { ++i; continue; }
            int e = i + 1;
            while (e < nx && covered(e, j)) ++e;
            render::RectDraw d;
            d.x0 = xs[size_t(i)], d.x1 = xs[size_t(e)], d.y0 = ys[size_t(j)], d.y1 = ys[size_t(j + 1)], d.rgba = fill, d.fill = true;
            s.rects.push_back(d);
            i = e;
        }
    if (!line) return;
    auto edge = [&](float x0, float y0, float x1, float y1) {
        render::RectDraw d;
        d.x0 = x0, d.y0 = y0, d.x1 = x1, d.y1 = y1, d.rgba = line, d.line = true;
        s.rects.push_back(d);
    };
    for (int j = 0; j <= ny; ++j)  // (the lines along x: a covered cell on one side only)
        for (int i = 0; i < nx;) {
            const bool above = covered(i, j - 1), below = covered(i, j);
            if (above == below) { ++i; continue; }
            int e = i + 1;
            while (e < nx && covered(e, j - 1) == above && covered(e, j) == below) ++e;
            edge(xs[size_t(i)], ys[size_t(j)], xs[size_t(e)], ys[size_t(j)]);
            i = e;
        }
    for (int i = 0; i <= nx; ++i)  // (the lines along y)
        for (int j = 0; j < ny;) {
            const bool left = covered(i - 1, j), right = covered(i, j);
            if (left == right) { ++j; continue; }
            int e = j + 1;
            while (e < ny && covered(i - 1, e) == left && covered(i, e) == right) ++e;
            edge(xs[size_t(i)], ys[size_t(j)], xs[size_t(i)], ys[size_t(e)]);
            j = e;
        }
}

void addResourceZones(render::Scene& s) {
    const AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    const Editor& ed = *app.ed;
    // While a unit is picked to place or units are pasted (Units mode), the zones they may not reach into, in red:
    // placing a resource, the ones round the town halls and start locations; placing a town hall or a start location,
    // the ones round the resources (when Options > Keep resources 3 tiles from town halls is on and Allow illegal units
    // is off)
    bool placing[2] = {false, false};
    if (app.mode == Mode::Units && app.resourceDistance && !app.allowIllegalUnits) {
        auto add = [&](uint16_t type) {
            int w, h;
            if (const int k = resourceZoneKind(ed, type, w, h); k >= 0) placing[1 - k] = true;
        };
        if (app.pasting && ed.canPaste())  // (the copy that follows the mouse)
            for (const Unit& u : ed.clipboard()) add(u.type);
        else if (app.palette >= 0)
            add(uint16_t(app.palette));
    }
    if (!gResourceZones[0] && !gResourceZones[1] && !placing[0] && !placing[1]) return;
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    std::vector<render::RectDraw> zones[3];  // (the overlays' zones, 0 and 1; 2: the red ones while placing)
    for (const Unit& u : ed.units()) {
        int w, h;
        const int k = resourceZoneKind(ed, u.type, w, h);
        if (k < 0 || (!gResourceZones[k] && !placing[k])) continue;
        const UnitType& t = ed.unitTypes()[u.type];
        if (t.is(UnitFlag::FlyingBuilding) && (u.state & UnitState::InTransit)) continue;  // (lifted off: no box)
        const float l = float(u.x - w / 2), tp = float(u.y - h / 2);
        render::RectDraw d;
        d.x0 = l - 96, d.y0 = tp - 96, d.x1 = l + w + 96, d.y1 = tp + h + 96;
        if (d.x1 < vx0 || d.x0 > vx1 || d.y1 < vy0 || d.y0 > vy1) continue;
        zones[gResourceZones[k] ? k : 2].push_back(d);  // (an overlay that is on: its own look, also while placing)
    }
    static const uint32_t kFill[3] = {0xE0403048u, 0xE0A02048u, 0xF0404050u}, kLine[3] = {0xF05040D0u, 0xF0B030D0u, 0};
    for (int k = 0; k < 3; ++k) addZoneUnion(s, zones[k], kFill[k], kLine[k]);
}

void resourceZonesToggle(int k) {
    gResourceZones[k] = !gResourceZones[k];
    shellMenusChanged();
    appState().dirty = true;
    static const wchar_t* const kOn[2] = {L"Invalid resource depot: the box of a town hall or a start location may not reach into the red areas",
                                          L"Invalid resource node: the box of a resource may not reach into the orange areas"};
    static const wchar_t* const kOff[2] = {L"Invalid resource depot is hidden", L"Invalid resource node is hidden"};
    setMessage(std::wstring(tr(gResourceZones[k] ? kOn[k] : kOff[k])));
}
