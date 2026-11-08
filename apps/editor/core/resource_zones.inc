// View > Overlays > Invalid resource depot and Invalid resource node (GitHub feedback #181; SCMDraft's names): the ground
// where the placement rule of the resource distance (units.cpp: town halls and start locations keep 3 tiles between
// their box and the boxes of resources, both ways) refuses a unit.
//   - Invalid resource depot: around each resource (minerals, a geyser), its box grown by 3 tiles. The box of a town
//     hall or a start location may not reach into it.
//   - Invalid resource node: around each town hall and start location, its box grown by 3 tiles. The box of a resource
//     may not reach into it.

bool gResourceZones[2] = {false, false};  // (0: invalid resource depot, 1: invalid resource node)

// What a unit type is for the resource distance, by its flags as the placement rule reads them (units.cpp): 0 a
// resource, 1 a town hall (or a start location), -1 neither; with the box that keeps the distance.
int resourceZoneKind(const Editor& ed, uint16_t type, int& w, int& h) {
    if (type >= ed.unitTypes().size()) return -1;
    const UnitType& t = ed.unitTypes()[type];
    w = t.placementWidth, h = t.placementHeight;
    if (!t.is(UnitFlag::Building)) return -1;
    return t.is(UnitFlag::ResourceContainer) ? 0 : t.is(UnitFlag::ResourceDepot) ? 1 : -1;
}

// The zones as one area: each point is covered once, so that zones that overlap have the same color, and the line
// (when `line` is not 0) goes only round the outside. A sweep from top to bottom: the map is cut into bands at the zones'
// top and bottom edges; each band has the zones across it, joined into runs (bands with the same runs are one band).
// Each run is a rect; the lines are the runs' sides and, between two bands, the parts that only one of them covers.
void zoneUnion(std::vector<render::RectDraw>& out, std::vector<render::RectDraw> zones, uint32_t fill, uint32_t line) {
    if (zones.empty()) return;
    using Runs = std::vector<std::pair<float, float>>;
    struct Band {
        float y0, y1;
        Runs runs;
    };
    std::vector<float> ys;
    for (const render::RectDraw& r : zones) ys.push_back(r.y0), ys.push_back(r.y1);
    std::sort(ys.begin(), ys.end()), ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    std::sort(zones.begin(), zones.end(), [](const render::RectDraw& a, const render::RectDraw& b) { return a.y0 < b.y0; });
    std::vector<Band> bands;
    std::vector<const render::RectDraw*> active;
    size_t next = 0;
    for (size_t j = 0; j + 1 < ys.size(); ++j) {
        const float y0 = ys[j], y1 = ys[j + 1];
        while (next < zones.size() && zones[next].y0 <= y0) active.push_back(&zones[next++]);
        active.erase(std::remove_if(active.begin(), active.end(), [y0](const render::RectDraw* r) { return r->y1 <= y0; }), active.end());
        Runs runs;
        for (const render::RectDraw* r : active) runs.push_back({r->x0, r->x1});
        std::sort(runs.begin(), runs.end());
        Runs joined;
        for (const auto& r : runs)
            if (!joined.empty() && r.first <= joined.back().second) joined.back().second = std::max(joined.back().second, r.second);
            else joined.push_back(r);
        if (!bands.empty() && bands.back().runs == joined) bands.back().y1 = y1;
        else bands.push_back({y0, y1, std::move(joined)});
    }
    for (const Band& b : bands)
        for (const auto& [x0, x1] : b.runs) {
            render::RectDraw d;
            d.x0 = x0, d.x1 = x1, d.y0 = b.y0, d.y1 = b.y1, d.rgba = fill, d.fill = true;
            out.push_back(d);
        }
    if (!line) return;
    auto edge = [&](float x0, float y0, float x1, float y1) {
        render::RectDraw d;
        d.x0 = x0, d.y0 = y0, d.x1 = x1, d.y1 = y1, d.rgba = line, d.line = true;
        out.push_back(d);
    };
    auto in = [](const Runs& runs, float x) {  // (the runs are in order and apart)
        auto it = std::upper_bound(runs.begin(), runs.end(), x, [](float v, const std::pair<float, float>& r) { return v < r.first; });
        return it != runs.begin() && x < std::prev(it)->second;
    };
    const Runs none;
    for (size_t k = 0; k <= bands.size(); ++k) {  // (the lines along x: where the band above and the band below differ)
        const Runs& above = k > 0 ? bands[k - 1].runs : none;
        const Runs& below = k < bands.size() ? bands[k].runs : none;
        const float y = k < bands.size() ? bands[k].y0 : bands.back().y1;
        std::vector<float> xs;
        for (const auto& [a, b] : above) xs.push_back(a), xs.push_back(b);
        for (const auto& [a, b] : below) xs.push_back(a), xs.push_back(b);
        std::sort(xs.begin(), xs.end()), xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        for (size_t i = 0; i + 1 < xs.size();) {
            auto differs = [&](size_t c) { const float m = (xs[c] + xs[c + 1]) / 2; return in(above, m) != in(below, m); };
            if (!differs(i)) { ++i; continue; }
            size_t e = i + 1;
            while (e + 1 < xs.size() && differs(e)) ++e;
            edge(xs[i], y, xs[e], y);
            i = e;
        }
    }
    for (const Band& b : bands)  // (the lines along y: the runs' sides)
        for (const auto& [x0, x1] : b.runs) edge(x0, b.y0, x0, b.y1), edge(x1, b.y0, x1, b.y1);
}

// The zones of the map's units as rects and lines, made again only when the map or what shows changes; each frame
// takes those in the view.
struct ResourceZoneCache {
    const Editor* editor = nullptr;
    uint64_t revision = ~0ull;
    int shown = -1;  // (the overlays and the placing zones that show: a bit each)
    std::vector<render::RectDraw> parts;
} gResourceZoneCache;

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
    const int shown = int(gResourceZones[0]) | int(gResourceZones[1]) << 1 | int(placing[0]) << 2 | int(placing[1]) << 3;
    if (!shown) return;
    ResourceZoneCache& c = gResourceZoneCache;
    if (c.editor != &ed || c.revision != uint64_t(ed.revision()) || c.shown != shown) {
        c.editor = &ed, c.revision = uint64_t(ed.revision()), c.shown = shown;
        c.parts.clear();
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
            zones[gResourceZones[k] ? k : 2].push_back(d);  // (an overlay that is on: its own look, also while placing)
        }
        static const uint32_t kFill[3] = {0xE0403048u, 0xE0A02048u, 0xF0404050u}, kLine[3] = {0xF05040D0u, 0xF0B030D0u, 0};
        for (int k = 0; k < 3; ++k) zoneUnion(c.parts, std::move(zones[k]), kFill[k], kLine[k]);
    }
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    for (const render::RectDraw& d : c.parts)
        if (std::max(d.x0, d.x1) >= vx0 && std::min(d.x0, d.x1) <= vx1 && std::max(d.y0, d.y1) >= vy0 && std::min(d.y0, d.y1) <= vy1) s.rects.push_back(d);
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
