// The minimap (portable, core.inc): moved from main.cpp. The map's terrain at one pixel a tile, the units as player-colored
// dots and the view's rectangle; a click or a drag moves the camera. The instance (gMinimap) is made in main.cpp, where
// the window is.


// --- Minimap ----------------------------------------------------------------------------------------

// Where the map is drawn inside the minimap (fit, centered) and the map-pixel -> minimap scale.
void minimapRect(RECT& r, float& scalePx) {
    AppState& app = shellState();
    int cw = 0, ch = 0;
    shellMinimapSize(cw, ch);
    const RECT c{0, 0, cw, ch};
    Editor& ed = *app.ed;
    float mw = ed.width() * 32.0f, mh = ed.height() * 32.0f;
    scalePx = std::min(c.right / mw, c.bottom / mh);
    int w = int(mw * scalePx), h = int(mh * scalePx);
    r = {(c.right - w) / 2, (c.bottom - h) / 2, (c.right - w) / 2 + w, (c.bottom - h) / 2 + h};
}

// A live game (Tools > Simulate): its units as the minimap's dots in place of the map's (map pixels, player, size in
// pixels); false: the map's units.
struct MinimapDot {
    int x = 0, y = 0, owner = 0, size = 32;
};
bool (*gLiveMinimap)(std::vector<MinimapDot>& dots) = nullptr;

// The units' dots on the minimap: the players' colors (Player 1 to Player 12).
const COLORREF kMinimapPlayerColors[12] = {RGB(244, 4, 4),   RGB(12, 72, 204),  RGB(44, 180, 148),  RGB(136, 64, 156),
                                           RGB(248, 140, 20), RGB(112, 48, 20),  RGB(204, 224, 208), RGB(252, 252, 56),
                                           RGB(8, 128, 8),   RGB(252, 252, 124), RGB(236, 196, 176), RGB(64, 104, 212)};

// The minimap's color of each megatile of `ts` (0x00RRGGBB): the mean of its 32 x 32 pixels.
std::vector<uint32_t> megatileColors(const Tileset& ts) {
    std::vector<uint32_t> out(ts.megatiles.size(), 0);
    for (size_t m = 0; m < ts.megatiles.size(); ++m) {
        uint32_t sum[3] = {0, 0, 0};
        for (uint32_t ref : ts.megatiles[m]) {
            size_t mini = ref >> 1;
            if (mini >= ts.minitiles.size()) continue;
            for (uint8_t idx : ts.minitiles[mini]) sum[0] += ts.palette[idx].r, sum[1] += ts.palette[idx].g, sum[2] += ts.palette[idx].b;
        }
        out[m] = (sum[0] / 1024) << 16 | (sum[1] / 1024) << 8 | (sum[2] / 1024);
    }
    return out;
}

// The minimap's color of each tile of `t` (0x00RRGGBB, row by row), with the megatile colors of `ts`.
std::vector<uint32_t> minimapTileColors(const Tileset& ts, const std::vector<uint32_t>& colors, const TerrainLayer& t) {
    std::vector<uint32_t> out(t.tiles.size(), 0);
    for (size_t i = 0; i < t.tiles.size(); ++i) {
        const int m = ts.megatileFor(t.tiles[i]);
        out[i] = m >= 0 && size_t(m) < colors.size() ? colors[size_t(m)] : 0;
    }
    return out;
}

// Colors (0x00RRGGBB) as a bitmap's pixels.
void minimapPixels(stardraft::draw::Bitmap& b, const std::vector<uint32_t>& colors, int w, int h) {
    b.resize(w, h);
    b.opaque = true;
    uint32_t* px = b.pixels();
    for (size_t i = 0; i < colors.size() && i < size_t(w) * size_t(h); ++i) {
        const uint32_t v = colors[i];
        px[i] = 0xFF000000u | (v & 0xFFu) << 16 | (v & 0xFF00u) | (v >> 16 & 0xFFu);
    }
    b.changed();
}

void rebuildMinimapTerrain() {
    AppState& app = shellState();
    Editor& ed = *app.ed;
    const Tileset& ts = ed.tileset();
    if (app.megatileColorsFor != &ts || app.megatileColorsEra != app.ed->era()) {
        app.megatileColor = megatileColors(ts);
        app.megatileColorsFor = &ts, app.megatileColorsEra = app.ed->era();
    }
    app.miniTerrain = minimapTileColors(ts, app.megatileColor, ed.terrain());
    app.miniTerrainRev = ed.terrainRevision();
}

// The current view in minimap pixels.
RECT minimapView(const RECT& r, float sc) {
    AppState& app = shellState();
    int vw, vh;
    viewSize(vw, vh);
    return {r.left + int(app.camX * sc), r.top + int(app.camY * sc), r.left + int((app.camX + vw / app.zoom) * sc),
            r.top + int((app.camY + vh / app.zoom) * sc)};
}

void minimapJump(int mx, int my) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    RECT r;
    float sc;
    minimapRect(r, sc);
    if (sc <= 0) return;
    float wx = (mx - r.left) / sc, wy = (my - r.top) / sc;
    int vw, vh;
    viewSize(vw, vh);
    app.camX = wx - vw / 2.0f / app.zoom;
    app.camY = wy - vh / 2.0f / app.zoom;
    app.dirty = true;
}

// The minimap as a view: the terrain (one pixel per tile, scaled smooth), the units as player-colored dots, and the
// current view as a tinted box. A click or a drag moves the view there.
struct MinimapView final : stardraft::draw::View {
    stardraft::draw::Bitmap terrain;  // the map's tiles, one pixel each
    uint64_t terrainRev = 0;
    bool dragging = false;

    void paint(stardraft::draw::Canvas& c) override {
        const stardraft::draw::Rect all{0, 0, width(), height()};
        c.fill(all, RGB(0, 0, 0));
        AppState& app = shellState();
        if (!app.ed || !app.ed->isOpen()) return;
        Editor& ed = *app.ed;
        if (app.miniTerrainRev != ed.terrainRevision() || app.megatileColorsFor != &ed.tileset() || app.megatileColorsEra != ed.era())
            rebuildMinimapTerrain();
        if (terrainRev != app.miniTerrainRev || terrain.width() != ed.width() || terrain.height() != ed.height()) {
            minimapPixels(terrain, app.miniTerrain, ed.width(), ed.height());
            terrainRev = app.miniTerrainRev;
        }
        RECT r;
        float sc;
        minimapRect(r, sc);
        c.bitmap(terrain, {0, 0, terrain.width(), terrain.height()}, r, true);
        // Units as player-colored dots.
        auto dot = [&](int ux, int uy, int size, int owner) {
            const int x = r.left + int(ux * sc), y = r.top + int(uy * sc);
            const int d = std::clamp(std::max(2, size * int(sc * 100) / 100), 2, 6);
            c.fill({x - d / 2, y - d / 2, x - d / 2 + d, y - d / 2 + d}, kMinimapPlayerColors[std::clamp(owner, 0, 11)]);
        };
        static std::vector<MinimapDot> live;
        live.clear();
        if (gLiveMinimap && gLiveMinimap(live))
            for (const MinimapDot& u : live) dot(u.x, u.y, u.size, u.owner);
        else
            for (const Unit& u : ed.units()) dot(u.x, u.y, int(ed.unitBox(u).right - ed.unitBox(u).left), u.owner);
        // The current view: a light tint, the line in the accent.
        const RECT v = minimapView(r, sc);
        c.fill(v, kAccent, 36);
        c.frame(v, kAccentBright);
        if (keyFocus && hasFocus()) paintFocusOutline(c, all);
    }

    // The keyboard: one Tab stop; the arrow keys move the map's view a quarter of its size.
    bool keyFocus = false;  // the focus came from Tab: the outline shows
    bool focusable() const override { return shellState().ed && shellState().ed->isOpen(); }
    bool focusOnClick() const override { return false; }  // (a click leaves the keyboard where it is)
    void focusChanged(bool) override { invalidate(); }
    void tabbedTo(bool) override { keyFocus = true, invalidate(); }
    bool key(const stardraft::draw::KeyEvent& e) override {
        using stardraft::draw::Key;
        AppState& app = shellState();
        if (!e.down || e.mods || !app.ed || app.zoom <= 0) return false;
        const int dx = e.key == Key::Left ? -1 : e.key == Key::Right ? 1 : 0, dy = e.key == Key::Up ? -1 : e.key == Key::Down ? 1 : 0;
        if (!dx && !dy) return false;
        int vw, vh;
        viewSize(vw, vh);
        app.camX += float(dx * vw) / 4.0f / app.zoom, app.camY += float(dy * vh) / 4.0f / app.zoom;
        clampCamera();
        app.dirty = true;
        invalidate();
        return true;
    }
    std::wstring focusedItem() const override { return keyFocus ? L"Minimap" : L""; }
    bool mouse(const stardraft::draw::MouseEvent& e) override {
        using stardraft::draw::MouseKind;
        if (e.button != 0 && (e.kind == MouseKind::Down || e.kind == MouseKind::Up)) return false;  // (right click: the dock menu)
        switch (e.kind) {
        case MouseKind::Down:
            keyFocus = false;
            dragging = true;
            if (auto* h = host()) h->capture(*this, true);
            minimapJump(e.pt.x, e.pt.y);
            return true;
        case MouseKind::Move:
            if (dragging) minimapJump(e.pt.x, e.pt.y);
            return true;
        case MouseKind::Up:
            if (dragging) {
                dragging = false;
                if (auto* h = host()) h->capture(*this, false);
            }
            return true;
        default: return false;
        }
    }
    stardraft::draw::Cursor cursor(stardraft::draw::Point) override { return stardraft::draw::Cursor::Hand; }
};
