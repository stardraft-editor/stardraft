// Edit > Resize map (portable, core.inc, after views_resize.inc): the map's size changed on the map itself, as Crop or
// extend does on a picture of it. A rectangle is the map after the change: its 8 handles (the edges and the corners)
// change the size, a drag in it moves it (where the map's content goes), and it can go past the map's edges to make the
// map larger (the new ground shows in a tint). What is outside it is dim: the change deletes it. Enter makes the
// change (one undo step, as Crop or extend: Editor::resizeMap); Esc leaves the mode. The mode ends when the map changes
// in another way (an undo, another tab or map) or the workspace is not the map's.
// The view can go to the rectangle (clampCamera). The values are a Crop or extend's (ResizeValues): the same rules for
// the size (16 to the largest side) and the offset (a shift that resizeMap uses).

bool applyMapResize(const ResizeValues& v);  // (the program: sdl_map_pages.inc; true: the map has the new size)
bool collabMapShared();                       // (sdl_collab.inc: the open map is in a session: its size cannot change)

struct MapResizeMode {
    bool on = false;
    const Editor* ed = nullptr;  // the map it is for (another map or tab: the mode ends)
    uint64_t revision = 0;       // the map's revision at the start (another: the map changed, or it is another map)
    ResizeValues v, start;
    int drag = -1;  // the handle that a drag holds: 0 top left, then clockwise to 7 left; 8 the rectangle (move); -1 none
    float grabX = 0, grabY = 0;  // where the drag started (map pixels)
};
MapResizeMode gMapResize;

// The rectangle (the map after the change) in the map's pixels now: left, top, right, bottom.
void mapResizeRect(const ResizeValues& v, float& x0, float& y0, float& x1, float& y1) {
    x0 = -v.shiftX * 32.0f, y0 = -v.shiftY * 32.0f, x1 = x0 + v.w * 32.0f, y1 = y0 + v.h * 32.0f;
}

bool mapResizeOn() {
    AppState& app = appState();
    if (gMapResize.on && (!app.ed || app.ed.get() != gMapResize.ed || !app.ed->isOpen() || app.ed->revision() != gMapResize.revision ||
                          gWorkspace != Workspace::Map)) {
        gMapResize = MapResizeMode{};  // (another map, a change of the map, or another workspace)
        app.dirty = true;
    }
    return gMapResize.on;
}

void mapResizeStop(const std::wstring& message) {
    if (!gMapResize.on) return;
    gMapResize = MapResizeMode{};
    appState().dirty = true;
    clampCamera();
    if (!message.empty()) setMessage(message);
}

void mapResizeStart() {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    if (collabMapShared()) return setMessage(L"The map is shared: its size cannot change while the session is open");
    gMapResize = MapResizeMode{};
    gMapResize.on = true, gMapResize.ed = app.ed.get(), gMapResize.revision = app.ed->revision();
    gMapResize.v.w = app.ed->width(), gMapResize.v.h = app.ed->height();
    gMapResize.v.fill = mapPropsFirstTerrain(app.ed->era());
    app.dirty = true;
    setMessage(L"Resize map: drag the edges or the corners; drag inside to move the map's content. Enter: change the size. Esc: cancel.");
}

// The box the view can go to (clampCamera): the rectangle. False: the mode is off.
bool mapResizeCameraBox(float& x0, float& y0, float& x1, float& y1) {
    if (!mapResizeOn()) return false;
    mapResizeRect(gMapResize.v, x0, y0, x1, y1);
    return true;
}

// The handle (0-7), the rectangle (8) or nothing (-1) under the screen point.
int mapResizeHit(const AppState& app, int sx, int sy) {
    float x0, y0, x1, y1;
    mapResizeRect(gMapResize.v, x0, y0, x1, y1);
    auto scr = [&](float wx, float wy, float& px, float& py) { px = (wx - app.camX) * app.zoom, py = (wy - app.camY) * app.zoom; };
    float l, t, r, b;
    scr(x0, y0, l, t), scr(x1, y1, r, b);
    const float mx = (l + r) / 2, my = (t + b) / 2, grab = float(scale(9));
    const float hx[8] = {l, mx, r, r, r, mx, l, l}, hy[8] = {t, t, t, my, b, b, b, my};
    for (int k = 0; k < 8; ++k)
        if (std::abs(sx - hx[k]) <= grab && std::abs(sy - hy[k]) <= grab) return k;
    if (sx > l + grab && sx < r - grab && sy > t + grab && sy < b - grab) return 8;
    // (an edge anywhere along it, not only at its handle)
    if (sy > t && sy < b && std::abs(sx - l) <= grab) return 7;
    if (sy > t && sy < b && std::abs(sx - r) <= grab) return 3;
    if (sx > l && sx < r && std::abs(sy - t) <= grab) return 1;
    if (sx > l && sx < r && std::abs(sy - b) <= grab) return 5;
    return -1;
}

// The drag at (wx, wy) map pixels: the new values from the ones at its start.
void mapResizeDragTo(float wx, float wy) {
    const ResizeValues& s = gMapResize.start;
    const int maxSide = maxMapSide(), minSide = 16;
    int L = -s.shiftX, T = -s.shiftY, R = L + s.w, B = T + s.h;
    const int k = gMapResize.drag;
    const int tx = int(std::lround(wx / 32.0f)), ty = int(std::lround(wy / 32.0f));
    const bool left = k == 0 || k == 6 || k == 7, right = k == 2 || k == 3 || k == 4;
    const bool top = k == 0 || k == 1 || k == 2, bottom = k == 4 || k == 5 || k == 6;
    if (k == 8) {
        const int dx = int(std::lround((wx - gMapResize.grabX) / 32.0f)), dy = int(std::lround((wy - gMapResize.grabY) / 32.0f));
        L += dx, R += dx, T += dy, B += dy;
    }
    if (left) L = std::clamp(tx, R - maxSide, R - minSide);
    if (right) R = std::clamp(tx, L + minSide, L + maxSide);
    if (top) T = std::clamp(ty, B - maxSide, B - minSide);
    if (bottom) B = std::clamp(ty, T + minSide, T + maxSide);
    // The offset that resizeMap takes (an even x, and x / 2 + y even: the isometric diamonds), nearest to the drag;
    // a left or top edge moves to it (the opposite edge stays), a move keeps the size.
    int sx = -L, sy = -T;
    if (left || top || k == 8) resizeNearestShift(double(-L), double(-T), sx, sy);
    ResizeValues v = gMapResize.v;
    if (k == 8) v.w = s.w, v.h = s.h;
    else v.w = R + sx, v.h = B + sy;
    // (a left or a top edge alone: the other side's size stays; the offset's rule may move the box by a row or two columns)
    if (k == 7) v.h = s.h;
    if (k == 1) v.w = s.w;
    v.shiftX = sx, v.shiftY = sy;
    if (v.w < minSide || v.h < minSide || v.w > maxSide || v.h > maxSide) return;  // (a shift past the limit: the last good values stay)
    if (!(v == gMapResize.v)) gMapResize.v = v, appState().dirty = true;
}

// The map's mouse while the mode is on (map_input.inc): true when it took the event. kind 0 down, 1 move, 2 up.
bool mapResizeMouse(AppState& app, int kind, int sx, int sy) {
    if (!mapResizeOn()) return false;
    float wx, wy;
    toWorld(app, sx, sy, wx, wy);
    if (kind == 0) {
        gMapResize.drag = mapResizeHit(app, sx, sy);
        gMapResize.start = gMapResize.v, gMapResize.grabX = wx, gMapResize.grabY = wy;
        if (gMapResize.drag >= 0) app.drag = Drag::ResizeMap;  // (a lost release: the window's capture ends it, map_input's mouse up)
        return true;
    }
    if (kind == 1) {
        if (gMapResize.drag >= 0 && app.drag != Drag::ResizeMap) gMapResize.drag = -1;  // (the release went elsewhere: a pan, another window)
        if (gMapResize.drag >= 0) mapResizeDragTo(wx, wy);
        const int h = gMapResize.drag >= 0 ? gMapResize.drag : mapResizeHit(app, sx, sy);
        static const wchar_t* const kHints[] = {L"Drag: the top left corner", L"Drag: the top edge", L"Drag: the top right corner",
                                               L"Drag: the right edge", L"Drag: the bottom right corner", L"Drag: the bottom edge",
                                               L"Drag: the bottom left corner", L"Drag: the left edge", L"Drag: move the map's content"};
        setHint(h >= 0 ? std::wstring(kHints[h]) + L" (" + std::to_wstring(gMapResize.v.w) + L" x " + std::to_wstring(gMapResize.v.h) + L" tiles)"
                       : std::wstring());
        return true;
    }
    gMapResize.drag = -1;
    if (app.drag == Drag::ResizeMap) app.drag = Drag::None;
    return true;
}

// The mode's keys: Enter makes the change, Esc leaves. True: taken.
bool mapResizeKey(bool enter, bool escape) {
    if (!mapResizeOn()) return false;
    if (escape) return mapResizeStop(L"Resize map: the map keeps its size"), true;
    if (!enter) return false;
    const ResizeValues v = gMapResize.v;
    AppState& app = appState();
    if (v.w == app.ed->width() && v.h == app.ed->height() && v.shiftX == 0 && v.shiftY == 0) return mapResizeStop(L"Resize map: the map keeps its size"), true;
    if (collabMapShared()) return mapResizeStop(L"The map is shared: its size cannot change while the session is open"), true;
    gMapResize.on = false;  // (before the change: the camera's box is the map again)
    if (!applyMapResize(v)) gMapResize.on = true;  // (it failed and said why: the mode stays)
    else gMapResize = MapResizeMode{};
    app.dirty = true;
    return true;
}

// The map view's part (map_scene.inc): dim over what goes, a tint over the new ground, the rectangle, its handles and
// its size.
void addMapResizeOverlay(render::Scene& s) {
    if (!mapResizeOn()) return;
    const AppState& app = appState();
    const Editor& ed = *app.ed;
    float x0, y0, x1, y1;
    mapResizeRect(gMapResize.v, x0, y0, x1, y1);
    const float mw = ed.width() * 32.0f, mh = ed.height() * 32.0f;
    auto fill = [&](float a, float b, float c, float d, uint32_t rgba) {
        if (c > a && d > b) s.rects.push_back({a, b, c, d, rgba, true});
    };
    const uint32_t dim = 0x000000B0u, fresh = 0x3C8CDC40u;
    // What goes (the old map outside the rectangle).
    fill(0, 0, mw, std::min(y0, mh), dim);
    fill(0, std::max(y1, 0.0f), mw, mh, dim);
    fill(0, std::max(y0, 0.0f), std::min(x0, mw), std::min(y1, mh), dim);
    fill(std::max(x1, 0.0f), std::max(y0, 0.0f), mw, std::min(y1, mh), dim);
    // The new ground (the rectangle outside the old map).
    fill(x0, y0, x1, std::min(0.0f, y1), fresh);
    fill(x0, std::max(mh, y0), x1, y1, fresh);
    fill(x0, std::max(y0, 0.0f), std::min(0.0f, x1), std::min(y1, mh), fresh);
    fill(std::max(mw, x0), std::max(y0, 0.0f), x1, std::min(y1, mh), fresh);
    // The rectangle and its handles (screen-sized: the zoom does not change them).
    const uint32_t line = 0xFFFFFFFFu, accent = 0x4AA3FFFFu;
    render::RectDraw box;
    box.x0 = x0, box.y0 = y0, box.x1 = x1, box.y1 = y1, box.rgba = accent, box.thickness = 2;
    s.rects.push_back(box);
    const float h = float(scale(5)) / app.zoom, mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    const float hx[8] = {x0, mx, x1, x1, x1, mx, x0, x0}, hy[8] = {y0, y0, y0, my, y1, y1, y1, my};
    for (int k = 0; k < 8; ++k) {
        s.rects.push_back({hx[k] - h, hy[k] - h, hx[k] + h, hy[k] + h, line, true});
        render::RectDraw o;
        o.x0 = hx[k] - h, o.y0 = hy[k] - h, o.x1 = hx[k] + h, o.y1 = hy[k] + h, o.rgba = accent, o.thickness = 1;
        s.rects.push_back(o);
    }
}

// The new size over the map, in the program's font (after the map's picture: the map view's paint): a box at the
// rectangle's top left corner (kept in the view), the size, and the keys under it.
void mapResizePaint(draw::Canvas& c, draw::Rect view) {
    if (!mapResizeOn()) return;
    const AppState& app = appState();
    float x0, y0, x1, y1;
    mapResizeRect(gMapResize.v, x0, y0, x1, y1);
    const std::wstring size = std::to_wstring(gMapResize.v.w) + L" x " + std::to_wstring(gMapResize.v.h) + tr(L" tiles");
    const std::wstring keys = tr(L"Enter: change the size. Esc: cancel.");
    const int pad = scale(8), lineH = c.metrics().height, w = std::max(c.measure(size), c.measure(keys)) + 2 * pad, h = 2 * lineH + pad + scale(4);
    int x = int((x0 - app.camX) * app.zoom) + scale(10), y = int((y0 - app.camY) * app.zoom) + scale(10);
    x = std::clamp(x, view.left + scale(8), std::max(view.left + scale(8), view.right - w - scale(8)));
    y = std::clamp(y, view.top + scale(8), std::max(view.top + scale(8), view.bottom - h - scale(8)));
    const draw::Rect box{x, y, x + w, y + h};
    c.roundBox(box, kPanel, kAccent, float(scale(4)));
    c.text({x + pad, y + pad / 2, x + w - pad, y + pad / 2 + lineH}, size, kText, draw::kMiddle);
    c.text({x + pad, y + pad / 2 + lineH + scale(2), x + w - pad, y + pad / 2 + 2 * lineH + scale(2)}, keys, kDim, draw::kMiddle);
}
