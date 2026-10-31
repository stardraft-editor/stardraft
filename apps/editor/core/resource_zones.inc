// View > Overlays > Invalid resource depot and Invalid resource node (GitHub feedback #181; SCMDraft's names): the ground
// where the placement rule of the resource distance (units.cpp: town halls and start locations keep 3 tiles between
// their box and the boxes of resources, both ways) refuses a unit.
//   - Invalid resource depot: around each resource (minerals, a geyser), its box grown by 3 tiles. The box of a town
//     hall or a start location may not reach into it.
//   - Invalid resource node: around each town hall and start location, its box grown by 3 tiles. The box of a resource
//     may not reach into it.

bool gResourceZones[2] = {false, false};  // (0: invalid resource depot, 1: invalid resource node)

void addResourceZones(render::Scene& s) {
    const AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    const Editor& ed = *app.ed;
    // While a unit is picked to place (Units mode), the zones it may not reach into, in red: placing a resource,
    // the ones round the town halls and start locations; placing a town hall or a start location, the ones round the
    // resources (when Options > Keep resources 3 tiles from town halls is on)
    bool placing[2] = {false, false};
    if (app.mode == Mode::Units && app.palette >= 0 && app.resourceDistance && !app.allowIllegalUnits && size_t(app.palette) < ed.unitTypes().size()) {
        const UnitType& p = ed.unitTypes()[size_t(app.palette)];
        if (p.is(UnitFlag::Building)) placing[0] = p.is(UnitFlag::ResourceDepot), placing[1] = p.is(UnitFlag::ResourceContainer);
    }
    if (!gResourceZones[0] && !gResourceZones[1] && !placing[0] && !placing[1]) return;
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    std::vector<render::RectDraw> red;  // (the zones while placing)
    static const uint32_t kFill[2] = {0xE0403048u, 0xE0A02048u}, kLine[2] = {0xF05040D0u, 0xF0B030D0u};
    for (const Unit& u : ed.units()) {
        if (u.type >= ed.unitTypes().size()) continue;
        const UnitType& t = ed.unitTypes()[u.type];
        if (!t.is(UnitFlag::Building) || (t.is(UnitFlag::FlyingBuilding) && (u.state & UnitState::InTransit))) continue;  // (lifted off: no box)
        const int k = t.is(UnitFlag::ResourceContainer) ? 0 : t.is(UnitFlag::ResourceDepot) ? 1 : -1;
        if (k < 0 || (!gResourceZones[k] && !placing[k])) continue;
        const float l = float(u.x - t.placementWidth / 2), tp = float(u.y - t.placementHeight / 2);
        render::RectDraw d;
        d.x0 = l - 96, d.y0 = tp - 96, d.x1 = l + t.placementWidth + 96, d.y1 = tp + t.placementHeight + 96;
        if (d.x1 < vx0 || d.x0 > vx1 || d.y1 < vy0 || d.y0 > vy1) continue;
        if (gResourceZones[k]) {  // (the overlay: its fill and line)
            d.rgba = kFill[k], d.fill = true;
            s.rects.push_back(d);
            d.rgba = kLine[k], d.fill = false;
            s.rects.push_back(d);
        } else {  // (while placing: a red fill only, below)
            red.push_back(d);
        }
    }
    // The red fill covers each point once, so that zones that overlap have the same color: the union of the zones, cut
    // on the zones' edges into cells, and the covered cells of each row joined into rects
    if (red.empty()) return;
    std::vector<float> xs, ys;
    for (const render::RectDraw& r : red) xs.push_back(r.x0), xs.push_back(r.x1), ys.push_back(r.y0), ys.push_back(r.y1);
    std::sort(xs.begin(), xs.end()), xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end()), ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    for (size_t j = 0; j + 1 < ys.size(); ++j) {
        const float cy = (ys[j] + ys[j + 1]) / 2;
        for (size_t i = 0; i + 1 < xs.size();) {
            auto covered = [&](size_t c) {
                const float cx = (xs[c] + xs[c + 1]) / 2;
                return std::any_of(red.begin(), red.end(), [&](const render::RectDraw& r) { return cx > r.x0 && cx < r.x1 && cy > r.y0 && cy < r.y1; });
            };
            if (!covered(i)) { ++i; continue; }
            size_t e = i + 1;
            while (e + 1 < xs.size() && covered(e)) ++e;
            render::RectDraw d;
            d.x0 = xs[i], d.x1 = xs[e], d.y0 = ys[j], d.y1 = ys[j + 1], d.rgba = 0xF0404050u, d.fill = true;
            s.rects.push_back(d);
            i = e;
        }
    }
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
