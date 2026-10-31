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
    if ((!gResourceZones[0] && !gResourceZones[1]) || !app.ed || !app.ed->isOpen()) return;
    const Editor& ed = *app.ed;
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    static const uint32_t kFill[2] = {0xE0403048u, 0xE0A02048u}, kLine[2] = {0xF05040D0u, 0xF0B030D0u};
    for (const Unit& u : ed.units()) {
        if (u.type >= ed.unitTypes().size()) continue;
        const UnitType& t = ed.unitTypes()[u.type];
        if (!t.is(UnitFlag::Building) || (t.is(UnitFlag::FlyingBuilding) && (u.state & UnitState::InTransit))) continue;  // (lifted off: no box)
        const int k = t.is(UnitFlag::ResourceContainer) ? 0 : t.is(UnitFlag::ResourceDepot) ? 1 : -1;
        if (k < 0 || !gResourceZones[k]) continue;
        const float l = float(u.x - t.placementWidth / 2), tp = float(u.y - t.placementHeight / 2);
        render::RectDraw d;
        d.x0 = l - 96, d.y0 = tp - 96, d.x1 = l + t.placementWidth + 96, d.y1 = tp + t.placementHeight + 96;
        if (d.x1 < vx0 || d.x0 > vx1 || d.y1 < vy0 || d.y0 > vy1) continue;
        d.rgba = kFill[k], d.fill = true;
        s.rects.push_back(d);
        d.rgba = kLine[k], d.fill = false;
        s.rects.push_back(d);
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
