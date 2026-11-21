// View > Overlays > Fog of war of the selected player (roadmap R32; portable, core.inc): what the player of the Units
// palette (Player 1 to 8) sees when the game starts. The map is dark where none of the player's units sees, nor the
// units of the players of its force when the force has Shared vision (Scenario > Players and forces). Each unit sees
// the tiles of its sight mask (units.dat Sight Range: the project's value, else the game's); a ground unit does not see
// a tile higher than the ground it stands on, as in the game; a flying unit sees all of its tiles. The start locations
// see nothing (the game puts a town hall there in melee only).
// The map's fog of war at the start (CHK MASK, Editor::fogMask) shows through: a tile that no unit sees is dark where
// it starts unexplored (black in the game) and gray where it starts explored; the terrain shows under both. A unit's
// tile is clear. Players 9 to 12: a tile is unexplored when it is unexplored for one of the players 1 to 8.
// Terrain's Paint fog (TFog) and Erase fog (TFogErase) tools change the MASK of the players that the tool strip selects
// (one or more): they add fog or remove it, with the brush, a rectangle or a fill. While a fog tool is on, the overlay
// shows the MASK of those players only (no units' sight), so each painted tile shows: dark where all of them have fog,
// gray where some of them have it (View > Overlays > Fog of war stays as it is).

bool gShowFog = false;

struct FogLayer {
    std::vector<render::RectDraw> fills;
    const Editor* shapesFor = nullptr;
    uint64_t shapesRev = 0;
    int player = -1, key = -1;
    bool maskOnly = false;
};
FogLayer gFog;

// The player of the Units palette (0 to 11): the app state's while a Units panel is the active one, else the Units
// panel's own (each panel keeps its player: core/panel_state.inc).
int fogPlayer() {
    const AppState& app = appState();
    if (app.mode == Mode::Units) return std::clamp(app.playerOwner, 0, 11);
    const PanelState* units = panelHomeOf(Mode::Units);
    return std::clamp(units && units != gActivePanel ? units->owner : app.playerOwner, 0, 11);
}
// The bits of MASK that the fog tools change: their players' (one at least).
uint8_t fogToolBits() { return gFogTool.players ? gFogTool.players : uint8_t(1); }
// The fog tools' players as text: "Player 3", "Player 1, Player 4", "Player 1 to Player 8".
std::wstring fogToolPlayerName() {
    const uint8_t bits = fogToolBits();
    if (bits == 0xFF) return tr(L"Player 1 to Player 8");
    std::wstring out;
    for (int p = 0; p < 8; ++p)
        if (bits >> p & 1) {
            wchar_t buf[48];
            std::swprintf(buf, 48, tr(L"Player %d"), p + 1);
            out += (out.empty() ? L"" : L", ") + std::wstring(buf);
        }
    return out;
}
// A text with the tool's player for %ls (and a number for %d after it).
std::wstring fogText(const wchar_t* format, int n) {
    wchar_t buf[512];
    std::swprintf(buf, 512, format, fogToolPlayerName().c_str(), n);
    return buf;
}
bool fogToolOn() {
    const AppState& app = appState();
    return app.mode == Mode::Terrain && isFogTool(currentTool(app)) && app.ed && app.ed->isOpen();
}

const DatTable* rangeTable(const std::string& name);  // (core/map_scene.inc)
std::vector<bool> sightMask(int range, int& size);

// The height of a tile (0 low, 1 middle, 2 high): its middle minitiles' VF4 flags.
inline int fogTileLevel(const Editor& ed, int tx, int ty) {
    const Tileset& ts = ed.tileset();
    const int mega = ts.megatileFor(ed.terrain().tiles[size_t(ty) * ed.width() + tx]);
    if (mega < 0 || size_t(mega) >= ts.minitileFlags.size()) return 0;
    const uint16_t f = ts.minitileFlags[size_t(mega)][5];  // (row 1, column 1 of the 4 x 4)
    return (f & 4) ? 2 : (f & 2) ? 1 : 0;
}

// The class of each tile by MASK, before the units' sight: 1 explored, 2 unexplored (0, clear, is for the seen tiles).

std::vector<uint8_t> fogStartClasses(const Editor& ed, int player) {
    const std::vector<uint8_t> mask = ed.fogMask();
    const uint8_t bits = player < 8 ? uint8_t(1u << player) : uint8_t(0xFF);
    std::vector<uint8_t> out(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) out[i] = (mask[i] & bits) ? 2 : 1;
    return out;
}

// The fog tools' classes for the players `bits`: 2 where all of them have fog, 1 where some of them have it, 0 where
// none has it.
std::vector<uint8_t> fogMaskClasses(const Editor& ed, uint8_t bits) {
    const std::vector<uint8_t> mask = ed.fogMask();
    std::vector<uint8_t> out(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) out[i] = (mask[i] & bits) == bits ? 2 : (mask[i] & bits) ? 1 : 0;
    return out;
}

// `maskOnly`: the MASK alone, as fogMaskClasses (the fog tools; `player` is then their players' bits); else the start
// with the units' sight.
void makeFogShapes(const Editor& ed, int player, bool maskOnly) {
    const int key = gLightTheme ? 1 : 0;
    if (gFog.shapesFor == &ed && gFog.shapesRev == ed.revision() && gFog.player == player && gFog.key == key && gFog.maskOnly == maskOnly) return;
    gFog.shapesFor = &ed, gFog.shapesRev = ed.revision(), gFog.player = player, gFog.key = key, gFog.maskOnly = maskOnly;
    gFog.fills.clear();
    const int w = ed.width(), h = ed.height();
    std::vector<uint8_t> seen = maskOnly ? fogMaskClasses(ed, uint8_t(player)) : fogStartClasses(ed, player);  // (then 0 where a unit sees)
    if (seen.size() != size_t(w) * h) seen.assign(size_t(w) * h, maskOnly ? 0 : 2);
    // The players whose units the player sees with: itself, and its force when the force shares vision.
    bool with[12] = {};
    if (!maskOnly) with[std::clamp(player, 0, 11)] = true;
    if (!maskOnly && player < 8) {
        const int f = ed.player(player).force;
        if (f >= 0 && f < 4 && (ed.force(f).flags & Editor::SharedVision))
            for (int p = 0; p < 8; ++p) with[p] |= ed.player(p).force == f;
    }
    const DatTable* units = maskOnly ? nullptr : rangeTable("units");
    const DatField* sight = units ? units->field("Sight Range") : nullptr;
    std::vector<int> levels(size_t(w) * h);
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) levels[size_t(ty) * w + tx] = fogTileLevel(ed, tx, ty);
    for (const Unit& u : ed.units()) {
        if (maskOnly) break;
        if (u.owner >= 12 || !with[u.owner] || u.type >= ed.unitTypes().size() || u.type == 214 /* Start Location */) continue;
        const UnitType& t = ed.unitTypes()[u.type];
        const int tiles = sight ? int(units->value(*sight, u.type)) : 0;
        if (tiles <= 0) continue;
        int n = 0;
        const auto mask = sightMask(tiles, n);
        const int cx = u.x / 32, cy = u.y / 32, tx0 = cx - n / 2, ty0 = cy - n / 2;
        const bool flyer = t.is(UnitFlag::Flyer);
        const int level = cx >= 0 && cy >= 0 && cx < w && cy < h ? levels[size_t(cy) * w + cx] : 0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int X = tx0 + x, Y = ty0 + y;
                if (!mask[size_t(y * n + x)] || X < 0 || Y < 0 || X >= w || Y >= h) continue;
                if (!flyer && levels[size_t(Y) * w + X] > level) continue;  // higher ground: not seen from below
                seen[size_t(Y) * w + X] = 0;
            }
    }
    // Runs of the same class on each row: unexplored dark, explored gray (the terrain shows under both).
    const uint32_t dark = gLightTheme ? 0x10101880u : 0x00000080u, gray = gLightTheme ? 0x30303848u : 0x20202448u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w;) {
            const uint8_t c = seen[size_t(y) * w + x];
            int e = x + 1;
            while (e < w && seen[size_t(y) * w + e] == c) ++e;
            if (c) {
                render::RectDraw d;
                d.x0 = x * 32.0f, d.y0 = y * 32.0f, d.x1 = e * 32.0f, d.y1 = y * 32.0f + 32, d.rgba = c == 2 ? dark : gray, d.fill = true;
                gFog.fills.push_back(d);
            }
            x = e;
        }
}

void addFogLayer(render::Scene& s) {
    const AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return;
    if (fogToolOn()) makeFogShapes(*app.ed, fogToolBits(), true);
    else if (gShowFog) makeFogShapes(*app.ed, fogPlayer(), false);
    else return;
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    for (const render::RectDraw& d : gFog.fills)
        if (d.x1 >= vx0 && d.x0 <= vx1 && d.y1 >= vy0 && d.y0 <= vy1) s.rects.push_back(d);
}

void fogToggle() {
    gShowFog = !gShowFog;
    gFog.key = -1;
    shellMenusChanged();
    appState().dirty = true;
    setMessage(gShowFog ? std::wstring(tr(L"Fog of war: what the selected player (the Units palette's) sees when the game starts"))
                        : std::wstring(tr(L"Fog of war is hidden")));
}

// --- Painting the fog of war (Paint fog, Erase fog) ----------------------------------------------------------------

struct FogPaint {
    int tool = -1;             // the press's shape: TBrush (a stroke) or TRectFill (a box)
    bool add = true;           // (adds fog, else removes it)
    int lastX = 0, lastY = 0;  // (the brush: the tile it painted last)
    int changed = 0;           // (the tiles that the press changed)
    bool shift = false;        // (a box: Shift at the last move)
} gFogPaint;

std::vector<std::pair<int, int>> tilesFootprint(int cx, int cy);  // (core/tiles_edit.inc)
void tilesAt(float wx, float wy, int& tx, int& ty);
std::vector<std::pair<int, int>> terrainSelTiles();  // (core/map_edit.inc)
bool terrainSelHas(int tx, int ty);


// The tiles of the filled shape from the press at (x0, y0) to (x1, y1) (map pixels): the tiles whose middle is in it.
// Shift: a square, a circle, or a diamond of the map's shape (twice as wide as high).
std::vector<std::pair<int, int>> fogBoxTiles(int tool, float x0, float y0, float x1, float y1, bool shift) {
    const Editor& ed = *appState().ed;
    if (shift) {
        const float w = std::abs(x1 - x0), h = std::abs(y1 - y0);
        const float side = tool == TDiamondFill ? std::max(w, h * 2) : std::max(w, h);
        const float sideY = tool == TDiamondFill ? side / 2 : side;
        x1 = x0 + (x1 < x0 ? -side : side), y1 = y0 + (y1 < y0 ? -sideY : sideY);
    }
    const float px = x1, py = y1;  // (the mouse)
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = std::max(1.0f, (x1 - x0) / 2), ry = std::max(1.0f, (y1 - y0) / 2);
    std::vector<std::pair<int, int>> out;
    const int tx0 = std::max(0, int(std::floor(x0 / 32))), ty0 = std::max(0, int(std::floor(y0 / 32)));
    const int tx1 = std::min(int(ed.width()) - 1, int(std::floor(x1 / 32))), ty1 = std::min(int(ed.height()) - 1, int(std::floor(y1 / 32)));
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            const float dx = (tx * 32 + 16 - cx) / rx, dy = (ty * 32 + 16 - cy) / ry;
            const bool in = tool == TEllipseFill ? dx * dx + dy * dy <= 1.0f
                          : tool == TDiamondFill ? std::abs(dx) + std::abs(dy) <= 1.0f
                                                 : std::abs(dx) <= 1.0f && std::abs(dy) <= 1.0f;
            if (in) out.push_back({tx, ty});
        }
    if (out.empty() && x1 - x0 < 32 && y1 - y0 < 32 && px >= 0 && py >= 0 && px < ed.width() * 32.0f && py < ed.height() * 32.0f) {  // (a click: the tile under it; a box off the map: none)
        int tx, ty;
        tilesAt(px, py, tx, ty);
        out.push_back({tx, ty});
    }
    return out;
}

// The brush's tiles along the way from tile (ax, ay) to tile (bx, by).
std::vector<std::pair<int, int>> fogBrushTiles(int ax, int ay, int bx, int by) {
    std::vector<std::pair<int, int>> out;
    const int n = std::max(std::abs(bx - ax), std::abs(by - ay));
    for (int k = n ? 1 : 0; k <= n; ++k) {
        const int x = n ? ax + (bx - ax) * k / n : bx, y = n ? ay + (by - ay) * k / n : by;
        for (const auto& c : tilesFootprint(x, y)) out.push_back(c);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Fill: the selection's tiles (a click in the selection), every tile (Shift), else the connected tiles that have the
// clicked tile's fog (the player's bits as the clicked tile has them).
std::vector<std::pair<int, int>> fogFillTiles(int tx, int ty, bool every) {
    const AppState& app = appState();
    const Editor& ed = *app.ed;
    if (app.hasTerrainSel && terrainSelHas(tx, ty)) return terrainSelTiles();
    const int w = ed.width(), h = ed.height();
    std::vector<std::pair<int, int>> out;
    if (every) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) out.push_back({x, y});
        return out;
    }
    const std::vector<uint8_t> mask = ed.fogMask();
    const uint8_t bits = fogToolBits(), want = uint8_t(mask[size_t(ty) * w + tx] & bits);
    std::vector<uint8_t> done(mask.size(), 0);
    std::vector<std::pair<int, int>> todo{{tx, ty}};
    done[size_t(ty) * w + tx] = 1;
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        out.push_back({x, y});
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t i = size_t(ny) * w + nx;
            if (done[i] || (mask[i] & bits) != want) continue;
            done[i] = 1;
            todo.push_back({nx, ny});
        }
    }
    return out;
}

void fogDoneMessage(bool removed, int changed) {
    const wchar_t* format = !changed  ? (removed ? tr(L"Fog of war: %ls. These tiles have no fog.") : tr(L"Fog of war: %ls. These tiles have fog already."))
                          : changed == 1 ? (removed ? tr(L"Fog of war: %ls. Removed fog from 1 tile.") : tr(L"Fog of war: %ls. Added fog to 1 tile."))
                          : (removed ? tr(L"Fog of war: %ls. Removed fog from %d tiles.") : tr(L"Fog of war: %ls. Added fog to %d tiles."));
    setMessage(fogText(format, changed));
}

void fogMouseDown(float wx, float wy, unsigned mods) {
    AppState& app = appState();
    Editor& ed = *app.ed;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    gFogPaint = {};
    gFogPaint.add = currentTool(app) != TFogErase;
    if (gFogTool.shape == 2) {  // Fill
        fogDoneMessage(!gFogPaint.add, ed.setFog(fogFillTiles(tx, ty, (mods & draw::kShift) != 0), fogToolBits(), gFogPaint.add));
    } else if (gFogTool.shape == 1) {  // Rectangle
        gFogPaint.tool = TRectFill;
        app.drag = Drag::Fog;
        setHint(fogText(tr(L"Fog of war: %ls")));
    } else {  // Brush
        ed.beginStroke();  // (the press: one undo step, "Change the fog of war")
        gFogPaint.tool = TBrush, gFogPaint.lastX = tx, gFogPaint.lastY = ty;
        gFogPaint.changed = ed.setFog(fogBrushTiles(tx, ty, tx, ty), fogToolBits(), gFogPaint.add);
        app.drag = Drag::Fog;
        setHint(fogText(tr(L"Fog of war: %ls")));
    }
    app.dirty = true;
}

void fogMouseMove(float wx, float wy, unsigned mods) {
    AppState& app = appState();
    gFogPaint.shift = (mods & draw::kShift) != 0;
    if (gFogPaint.tool == TBrush) {
        int tx, ty;
        tilesAt(wx, wy, tx, ty);
        if (tx == gFogPaint.lastX && ty == gFogPaint.lastY) return;
        gFogPaint.changed += app.ed->setFog(fogBrushTiles(gFogPaint.lastX, gFogPaint.lastY, tx, ty), fogToolBits(), gFogPaint.add);
        gFogPaint.lastX = tx, gFogPaint.lastY = ty;
    }
    app.dirty = true;  // (a box: its outline follows the mouse)
}

void fogMouseUp(float wx, float wy, unsigned mods) {
    AppState& app = appState();
    Editor& ed = *app.ed;
    if (gFogPaint.tool == TBrush) {
        ed.endStroke();
        fogDoneMessage(!gFogPaint.add, gFogPaint.changed);
    } else if (gFogPaint.tool == TRectFill) {
        fogDoneMessage(!gFogPaint.add, ed.setFog(fogBoxTiles(TRectFill, app.dragWX, app.dragWY, wx, wy, (mods & draw::kShift) != 0), fogToolBits(), gFogPaint.add));
    }
    gFogPaint = {};
    app.drag = Drag::None;
    app.dirty = true;
}

// The brush's tiles at the mouse, the box's tiles while it is dragged, or the tile under the mouse (Fill): green where
// the tool adds fog, red where it removes it.
void fogScene(render::Scene& s, float wx, float wy) {
    const AppState& app = appState();
    std::vector<std::pair<int, int>> tiles;
    const bool add = app.drag == Drag::Fog ? gFogPaint.add : currentTool(app) != TFogErase;
    if (app.drag == Drag::Fog && gFogPaint.tool == TRectFill) {
        tiles = fogBoxTiles(TRectFill, app.dragWX, app.dragWY, wx, wy, gFogPaint.shift);
    } else {
        int tx, ty;
        tilesAt(wx, wy, tx, ty);
        if (gFogTool.shape == 0) tiles = tilesFootprint(tx, ty);
        else tiles.push_back({tx, ty});
        std::erase_if(tiles, [&](const auto& t) { return t.first < 0 || t.second < 0 || t.first >= int(app.ed->width()) || t.second >= int(app.ed->height()); });
    }
    tileOutline(s, tiles, add ? 0x40FF40FFu : 0xFF6060F0u, 1.5f);
}
