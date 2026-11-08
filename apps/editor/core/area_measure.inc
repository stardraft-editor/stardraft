// Optional native controls; portable hosts can use the map menu and keyboard.
std::function<void()> gShowAreaMeasurePanel;
std::function<void()> gAreaMeasureChanged;
// Read-only area selection. Outlines and painted tiles share a local mask, never editor undo.
void enterSelection();
struct AreaMeasureState {
    const Editor* ed = nullptr;
    uint64_t revision = 0;
    bool drawing = false, held = false, closed = false, recount = false;
    int mode = 0, tool = 0, brush = 1; // metric; outline / mark / erase; brush width in tiles
    int left = 0, top = 0, right = 0, bottom = 0;
    std::pair<int, int> lastTile;
    std::vector<std::pair<int, int>> points;
    MeasuredArea result;
    std::vector<uint8_t> selected;
    std::vector<std::vector<uint8_t>> history;
    std::vector<uint16_t> walkMask;
    std::vector<uint8_t> areaMask;
};
AreaMeasureState gAreaMeasure;
void areaMeasureChanged() {
    appState().dirty = true;
    if (gAreaMeasureChanged) gAreaMeasureChanged();
}

bool areaMeasureCurrent() {
    const auto& app = appState();
    if (gAreaMeasure.ed && (!app.ed || gAreaMeasure.ed != app.ed.get() || gAreaMeasure.revision != app.ed->revision())) {
        gAreaMeasure = {};
        areaMeasureChanged();
    }
    return gAreaMeasure.ed != nullptr;
}
void areaMeasureRecount() {
    if (!areaMeasureCurrent() || !gAreaMeasure.recount) return;
    const auto& ed = *appState().ed;
    gAreaMeasure.result = measureAreaMask(ed.terrain(), ed.tileset(), gAreaMeasure.selected);
    const int width = ed.width(), height = ed.height();
    gAreaMeasure.left = width, gAreaMeasure.top = height, gAreaMeasure.right = gAreaMeasure.bottom = 0;
    gAreaMeasure.walkMask.assign(size_t(width) * height, 0);
    gAreaMeasure.areaMask.assign(size_t(width) * height, 0);
    for (const auto& tile : gAreaMeasure.result.tiles) {
        gAreaMeasure.left = std::min(gAreaMeasure.left, tile.x), gAreaMeasure.right = std::max(gAreaMeasure.right, tile.x + 1);
        gAreaMeasure.top = std::min(gAreaMeasure.top, tile.y), gAreaMeasure.bottom = std::max(gAreaMeasure.bottom, tile.y + 1);
        const size_t index = size_t(tile.y) * width + tile.x;
        gAreaMeasure.walkMask[index] = tile.walk;
        gAreaMeasure.areaMask[index] = uint8_t(1 | (tile.buildable ? 2 : 0));
    }
    gAreaMeasure.closed = !gAreaMeasure.result.tiles.empty();
    gAreaMeasure.recount = false;
}
void areaMeasureRemember() {
    if (gAreaMeasure.history.size() >= 20) gAreaMeasure.history.erase(gAreaMeasure.history.begin());
    gAreaMeasure.history.push_back(gAreaMeasure.selected);
}
void areaMeasureBegin() {
    if (!areaMeasureCurrent()) {
        const auto& app = appState();
        if (!app.ed || !app.ed->isOpen()) return;
        gAreaMeasure.ed = app.ed.get(), gAreaMeasure.revision = app.ed->revision();
        gAreaMeasure.selected.assign(size_t(app.ed->width()) * app.ed->height(), 0);
    }
    enterSelection();
    gAreaMeasure.drawing = true;
    appState().drag = Drag::None;
    appState().pasting = appState().terrainPasting = appState().pastingItems = false;
    areaMeasureChanged();
}
void areaMeasureClose() {
    if (!areaMeasureCurrent()) return;
    if (gAreaMeasure.tool == 0 && !gAreaMeasure.points.empty()) {
        if (gAreaMeasure.points.size() < 3) return;
        areaMeasureRemember();
        const auto area = measureArea(appState().ed->terrain(), appState().ed->tileset(), gAreaMeasure.points);
        const int width = appState().ed->width();
        for (const auto& tile : area.tiles) gAreaMeasure.selected[size_t(tile.y) * width + tile.x] = 1;
        gAreaMeasure.points.clear();
        gAreaMeasure.recount = true;
    }
    areaMeasureRecount();
    gAreaMeasure.drawing = false, gAreaMeasure.held = false;
    areaMeasureChanged();
    setMessage(L"Area measured. Use the area tool to add or remove tiles. One Nexus is 12 tiles.");
}
void areaMeasureUndo() {
    if (!areaMeasureCurrent()) return;
    if (!gAreaMeasure.points.empty()) gAreaMeasure.points.pop_back();
    else if (!gAreaMeasure.history.empty()) {
        gAreaMeasure.selected = std::move(gAreaMeasure.history.back());
        gAreaMeasure.history.pop_back();
        gAreaMeasure.recount = true;
    }
    gAreaMeasure.held = false;
    areaMeasureChanged();
}
void areaMeasureClear() {
    if (!areaMeasureCurrent()) return;
    areaMeasureRemember();
    std::fill(gAreaMeasure.selected.begin(), gAreaMeasure.selected.end(), 0);
    gAreaMeasure.points.clear();
    gAreaMeasure.recount = true, gAreaMeasure.held = false;
    areaMeasureChanged();
}
bool areaMeasureCommand(int id) {
    if (id < ID_MEASURE_AREA || id > ID_MEASURE_AREA_CLEAR) return false;
    if (id == ID_MEASURE_AREA_CLEAR) areaMeasureClear();
    else if (id == ID_MEASURE_AREA) {
        areaMeasureBegin();
        if (gShowAreaMeasurePanel) gShowAreaMeasurePanel();
        setMessage(L"Area tool: click outline corners, then Finish. Mark or erase tiles with separate strokes.");
    } else gAreaMeasure.mode = id - ID_MEASURE_AREA_ALL;
    areaMeasureChanged();
    return true;
}
std::pair<int, int> areaMeasurePoint(float x, float y) {
    const auto& ed = *appState().ed;
    return {std::clamp(int(std::lround(x / 32)), 0, int(ed.width())), std::clamp(int(std::lround(y / 32)), 0, int(ed.height()))};
}
std::pair<int, int> areaMeasureTile(float x, float y) {
    const auto& ed = *appState().ed;
    return {std::clamp(int(std::floor(x / 32)), 0, int(ed.width()) - 1), std::clamp(int(std::floor(y / 32)), 0, int(ed.height()) - 1)};
}
void areaMeasureMark(std::pair<int, int> tile) {
    const int width = appState().ed->width(), height = appState().ed->height(), size = std::clamp(gAreaMeasure.brush, 1, 16);
    const int left = tile.first - (size - 1) / 2, top = tile.second - (size - 1) / 2;
    for (int y = std::max(0, top); y < std::min(height, top + size); ++y)
        for (int x = std::max(0, left); x < std::min(width, left + size); ++x)
            gAreaMeasure.selected[size_t(y) * width + x] = uint8_t(gAreaMeasure.tool == 1);
    gAreaMeasure.recount = true;
}
void areaMeasureMarkLine(std::pair<int, int> from, std::pair<int, int> to) {
    const int steps = std::max(std::abs(to.first - from.first), std::abs(to.second - from.second));
    for (int n = 0; n <= steps; ++n)
        areaMeasureMark({from.first + (steps ? int(std::lround(double(to.first - from.first) * n / steps)) : 0),
                         from.second + (steps ? int(std::lround(double(to.second - from.second) * n / steps)) : 0)});
}
void areaMeasureAddPoint(std::pair<int, int> p) {
    auto& points = gAreaMeasure.points;
    if (!points.empty() && points.back() == p) return;
    if (points.size() >= 2) {
        const auto a = points[points.size() - 2], b = points.back();
        if ((b.first - a.first) * (p.second - b.second) == (b.second - a.second) * (p.first - b.first) &&
            (b.first - a.first) * (p.first - b.first) + (b.second - a.second) * (p.second - b.second) >= 0) {
            points.back() = p;
            return;
        }
    }
    points.push_back(p);
}
bool areaMeasureDown(float x, float y) {
    if (!areaMeasureCurrent() || !gAreaMeasure.drawing) return false;
    if (gAreaMeasure.tool == 0) {
        const auto p = areaMeasurePoint(x, y);
        if (gAreaMeasure.points.size() >= 3 && p == gAreaMeasure.points.front()) areaMeasureClose();
        else areaMeasureAddPoint(p), gAreaMeasure.held = true;
    } else {
        areaMeasureRemember();
        gAreaMeasure.lastTile = areaMeasureTile(x, y);
        areaMeasureMark(gAreaMeasure.lastTile);
        gAreaMeasure.held = true;
    }
    areaMeasureChanged();
    return true;
}
bool areaMeasureMove(float x, float y) {
    if (!areaMeasureCurrent() || !gAreaMeasure.drawing) return false;
    if (gAreaMeasure.held) {
        if (gAreaMeasure.tool == 0) areaMeasureAddPoint(areaMeasurePoint(x, y));
        else {
            const auto next = areaMeasureTile(x, y);
            areaMeasureMarkLine(gAreaMeasure.lastTile, next);
            gAreaMeasure.lastTile = next;
        }
    }
    areaMeasureChanged();
    return true;
}
bool areaMeasureUp() {
    if (!areaMeasureCurrent() || !gAreaMeasure.drawing) return false;
    if (gAreaMeasure.held && gAreaMeasure.tool != 0 && !gAreaMeasure.history.empty() &&
        gAreaMeasure.history.back() == gAreaMeasure.selected)
        gAreaMeasure.history.pop_back();
    gAreaMeasure.held = false;
    // Releasing a stroke leaves the outline open and the tile mask editable.
    return true;
}
bool areaMeasureRight() {
    if (!areaMeasureCurrent() || !gAreaMeasure.drawing) return false;
    if (gAreaMeasure.tool == 0) areaMeasureUndo();
    else gAreaMeasure.drawing = false, gAreaMeasure.held = false;
    areaMeasureChanged();
    return true;
}
bool areaMeasureKey(draw::Key key) {
    if (!areaMeasureCurrent() || !gAreaMeasure.drawing) return false;
    if (key == draw::Key::Enter) areaMeasureClose();
    else if (key == draw::Key::Escape) {
        gAreaMeasure.points.clear();
        gAreaMeasure.drawing = false, gAreaMeasure.held = false;
    } else return false;
    areaMeasureChanged();
    return true;
}
void areaMeasureScene(render::Scene& s) {
    if (!areaMeasureCurrent()) return;
    areaMeasureRecount();
    if (gAreaMeasure.closed) {
        const auto& app = appState();
        const int subdivisions = gAreaMeasure.mode == 1 ? 4 : 1;
        const int unit = 32 / subdivisions, width = app.ed->width(), height = app.ed->height();
        int vw = 0, vh = 0;
        viewSize(vw, vh);
        const float zoom = std::max(.01f, app.zoom);
        const int x0 = std::clamp(std::max(int(std::floor(app.camX / unit)), gAreaMeasure.left * subdivisions), 0, width * subdivisions);
        const int x1 =
            std::clamp(std::min(int(std::ceil((app.camX + vw / zoom) / unit)), gAreaMeasure.right * subdivisions), 0, width * subdivisions);
        const int y0 = std::clamp(std::max(int(std::floor(app.camY / unit)), gAreaMeasure.top * subdivisions), 0, height * subdivisions);
        const int y1 = std::clamp(std::min(int(std::ceil((app.camY + vh / zoom) / unit)), gAreaMeasure.bottom * subdivisions), 0,
                                  height * subdivisions);
        auto covered = [&](int x, int y) {
            const size_t index = size_t(y / subdivisions) * width + x / subdivisions;
            if (subdivisions == 4) return bool(gAreaMeasure.walkMask[index] & (1u << ((y % 4) * 4 + x % 4)));
            return bool(gAreaMeasure.areaMask[index] & (gAreaMeasure.mode == 2 ? 2 : 1));
        };
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1;) {
                if (!covered(x, y)) {
                    ++x;
                    continue;
                }
                const int first = x++;
                while (x < x1 && covered(x, y)) ++x;
                s.rects.push_back({float(first * unit), float(y * unit), float(x * unit), float((y + 1) * unit), 0x80D8B438, true});
            }
    }
    const auto& p = gAreaMeasure.points;
    const size_t count = p.empty() ? 0 : p.size() - 1;
    for (size_t i = 0; i < count; ++i) {
        auto [ax, ay] = p[i];
        auto [bx, by] = p[(i + 1) % p.size()];
        s.rects.push_back({float(ax * 32), float(ay * 32), float(bx * 32), float(by * 32), 0x8CE7C9DC, false, 1.5f, true, true});
    }
    if (gAreaMeasure.drawing && gAreaMeasure.tool == 0 && !p.empty()) {
        const auto& app = appState();
        const float half = 3 / std::max(.01f, app.zoom);
        for (auto [x, y] : p) s.rects.push_back({x * 32 - half, y * 32 - half, x * 32 + half, y * 32 + half, 0x8CE7C9DC, true});
        if (app.mouseIn) {
            float wx = 0, wy = 0;
            toWorld(app.mouseX, app.mouseY, wx, wy);
            const auto next = areaMeasurePoint(wx, wy);
            s.rects.push_back({float(p.back().first * 32), float(p.back().second * 32), float(next.first * 32), float(next.second * 32),
                               0x8CE7C990, false, 1.5f, true, true});
        }
    }
    if (gAreaMeasure.drawing && gAreaMeasure.tool != 0 && appState().mouseIn) {
        const auto& app = appState();
        float x = 0, y = 0;
        toWorld(app.mouseX, app.mouseY, x, y);
        const auto tile = areaMeasureTile(x, y);
        const int size = gAreaMeasure.brush, left = tile.first - (size - 1) / 2, top = tile.second - (size - 1) / 2;
        const uint32_t color = gAreaMeasure.tool == 1 ? 0x8CE7C9B0 : 0xEFA995B0;
        s.rects.push_back({float(left * 32), float(top * 32), float((left + size) * 32), float((top + size) * 32), color, false, 1.5f});
    }
    if (gAreaMeasure.closed) {
        const auto& r = gAreaMeasure.result;
        char text[240];
        std::snprintf(text, sizeof(text),
                      "Area (Nexus = 12 tiles)\nAll: %.2f  Walkable: %.2f  Buildable: %.2f\nTerrain only; objects are not subtracted",
                      r.totalNexus(), r.walkableNexus(), r.buildableNexus());
        const float scale = mapTextScale(), factor = scale / 1.5f;
        s.rects.push_back({8 * factor, 8 * factor, 575 * factor, 72 * factor, 0x10232DE0, true, 1, false});
        s.texts.push_back({16 * factor, 16 * factor, text, 0xD4EEE5FF, false, scale});
    }
}
