// The Rectangle mode's tiles on the map (the editor's portable core): the tile picked, the brush's footprint, the spray,
// the fill and the stroke of a press, a drag and a release. The Tiles panel (the grid, the search and the filter boxes)
// is core/views_tiles.inc; this file calls it through the functions declared here.

void tilesRebuild();                // the panel's grid is made again for the map
int tilesViewH();                   // the grid's visible height in device pixels
void tilesScrollTo(int pos);        // the grid scrolls to a position
RECT tilesCellRect(int k);          // the k-th tile's cell in the grid's content
void tilesPanelRepaint();           // the panel's grid is painted again
void tilesPanelClearSearch();       // the search box is emptied (its change rebuilds the grid)
void tilesPanelShowAllFilter();     // the filter box shows its first entry, All tiles

constexpr uint16_t kTilesNone = 0xFFFF;  // a slot of the grid with no tile: one that the filter hides
struct TilesView {
    // The grid's slots, 16 in a row (views_tiles.inc): the tile values shown (group << 4 | index) in the tileset's
    // order, each group in a row of its own (kTilesNone: a tile that the filter hides).
    std::vector<uint16_t> values;
    int scroll = 0, contentH = 0, layoutW = -1;
    int listW = 16 * 32 + 15;  // the grid's width for the tiles: the view's, or more (tilesMinCell)
    int picked = -1;        // the tile value to place (group << 4 | subtile), -1: none
    std::vector<uint16_t> chosen;  // Ctrl+click and Shift+click: several tiles; Spray places one of them at random (#81)
    int anchor = -1;               // the tile a Shift+click's rectangle starts from: the last click without Shift
    // The stamp: a rectangle of the grid (a box dragged over it, or Shift+click), its tiles as the grid shows them
    // (stampW x stampH, 0xFFFF: a slot with no tile). The Brush places them as they are, and a drag repeats them in a
    // pattern from the press (stampX, stampY: the pattern's top-left tile); Fill fills with the pattern. With Shift on
    // the map, the Brush places the tiles at random instead (scatter). Spray is always random.
    std::vector<uint16_t> stamp;
    int stampW = 0, stampH = 0, stampX = 0, stampY = 0;
    bool scatter = false;  // the Brush stroke under way: Shift was held at its press
    int lastX = 0, lastY = 0;  // the Brush stroke's last tile
    int hover = -1;         // the tile value under the mouse in the list
    // A box dragged over the grid selects the tiles in it (GitHub feedback #110): from the press, in content pixels
    // (the scroll added); Ctrl+drag adds to the tiles selected before.
    bool boxing = false, boxed = false;
    POINT boxFrom{}, boxTo{};
    std::vector<uint16_t> boxBase;
    const Editor* builtFor = nullptr;
    int builtEra = -1;
    // The tiles' pictures, per tile group: its 16 tiles in a row (32 px each in SD, 64 in HD2 and Carbot), for
    // builtFor's tileset, in pixelsMode's art. `used`: the grid's paint that drew it last (tilesPixelsTrim).
    struct GroupPicture {
        draw::Bitmap bitmap;
        uint32_t used = 0;
    };
    std::map<uint16_t, GroupPicture> pixels;
    uint32_t paints = 0;                // the grid's paints
    int pixelsMode = -1;                // the map's art: 0 SD, 1 HD, 2 Carbot
    HdTiles hd;                         // HD2 or Carbot megatiles (pixelsMode 1, 2) of hdEra
    int hdEra = -1, hdMode = -1;
    bool shown = false;
    // The filter box's entries: kind 0 all, 1 a terrain type (terrain), 2 doodad tiles, 3 null tiles, 4 all but null.
    struct Filter {
        std::wstring label;
        int kind = 0;
        uint16_t terrain = 0;
    };
    std::vector<Filter> filters;
    int filter = 0;  // the filters' index that the grid shows
    int filtersEra = -1;
};
TilesView gTiles;

bool tilesOn() { return appState().ed && appState().ed->isOpen() && gWorkspace == Workspace::Map && appState().mode == Mode::Terrain && appState().terrainRect; }

// "Dirt", "Dirt / High dirt"; empty when no Blizzard map uses the group.
std::wstring tilesTerrainNames(const Editor::TileGroupEntry& e) {
    const IsomTerrainTable* t = IsomTerrainTable::forEra(appState().ed->era());
    if (!t || !e.terrains[0]) return L"";
    std::wstring s = widen(t->terrainName(e.terrains[0]));
    if (e.terrains[1]) s += L" / " + widen(t->terrainName(e.terrains[1]));
    return s;
}

const Editor::TileGroupEntry* tilesEntryOfGroup(int group) {
    if (!appState().ed) return nullptr;
    const auto& gs = appState().ed->tileGroups();
    auto it = std::lower_bound(gs.begin(), gs.end(), group, [](const Editor::TileGroupEntry& e, int g) { return int(e.group) < g; });
    return it != gs.end() && int(it->group) == group ? &*it : nullptr;
}

// What a group is: a doodad's, a terrain's, or neither (empty).
std::wstring tilesGroupNote(int group) {
    const Editor::TileGroupEntry* e = tilesEntryOfGroup(group);
    if (!e) return L"Null tiles";
    if (e->doodad >= 0) {
        const DoodadType* d = appState().ed->doodadTypes().find(uint16_t(e->doodad));
        std::wstring s = L"Doodad #" + std::to_wstring(e->doodad);
        if (d && d->groupString) s += L" (" + widen(appState().ed->unitTypes().statText(d->groupString - 1u)) + L")";
        if (d && d->height > 1) s += L", row " + std::to_wstring(e->doodadRow + 1);
        return s;
    }
    return tilesTerrainNames(*e);
}

bool tilesIsNull(int value) { return appState().ed && appState().ed->tileset().megatileFor(uint16_t(value)) <= 0; }

std::wstring tilesValueLabel(int value) {
    const std::wstring s = L"Group " + std::to_wstring(value >> 4) + L", tile " + std::to_wstring(value & 15);
    const std::wstring note = tilesIsNull(value) ? L"Null tile" : tilesGroupNote(value >> 4);
    return note.empty() ? s : s + L"  \x00B7  " + note;
}

bool tilesStampOn() { return gTiles.stamp.size() > 1; }

// From Select, a pick in the grid starts placing: the Brush.
void tilesToBrush() {
    AppState& app = appState();
    app.terrainPasting = false;
    if (app.mode == Mode::Terrain && currentTool(app) == TSelect) {
        app.toolByMode[int(Mode::Terrain)] = TBrush;
        fillToolCombo(app);
    }
}

// The stamp's tile at map tile (x, y) of the pattern that starts at (stampX, stampY); 0xFFFF: none.
uint16_t tilesStampAt(int x, int y) {
    const int w = gTiles.stampW, h = gTiles.stampH;
    if (w <= 0 || h <= 0) return 0xFFFF;
    const int sx = ((x - gTiles.stampX) % w + w) % w, sy = ((y - gTiles.stampY) % h + h) % h;
    return gTiles.stamp[size_t(sy) * size_t(w) + size_t(sx)];
}

// The stamp's top-left tile for the mouse at tile (tx, ty): before a stroke, the stamp around the mouse; in a Brush
// stroke, the pattern's block that has the mouse.
void tilesStampOrigin(int tx, int ty, int& x0, int& y0) {
    const int w = std::max(1, gTiles.stampW), h = std::max(1, gTiles.stampH);
    if (appState().drag != Drag::Tiles) {
        x0 = tx - (w - 1) / 2, y0 = ty - (h - 1) / 2;
        return;
    }
    auto down = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    x0 = gTiles.stampX + down(tx - gTiles.stampX, w) * w, y0 = gTiles.stampY + down(ty - gTiles.stampY, h) * h;
}

// The stamp's tiles with their top-left tile at (x0, y0), in the map.
std::vector<Editor::TileAt> tilesStampCells(int x0, int y0) {
    std::vector<Editor::TileAt> cells;
    const Editor& ed = *appState().ed;
    for (int y = 0; y < gTiles.stampH; ++y)
        for (int x = 0; x < gTiles.stampW; ++x) {
            const uint16_t v = gTiles.stamp[size_t(y) * size_t(gTiles.stampW) + size_t(x)];
            if (v != 0xFFFF && x0 + x >= 0 && y0 + y >= 0 && x0 + x < ed.width() && y0 + y < ed.height()) cells.push_back({x0 + x, y0 + y, v});
        }
    return cells;
}

// The grid's selection became a stamp (a box, or Shift+click): the Brush places it.
void tilesStampChosen() {
    int n = 0;
    for (uint16_t v : gTiles.stamp) n += v != 0xFFFF;
    tilesToBrush();
    setMessage(L"Brush: the " + std::to_wstring(gTiles.stampW) + L" x " + std::to_wstring(gTiles.stampH) + L" tiles you selected (" + std::to_wstring(n) +
               L" tiles). Click to place them; drag to repeat them. Hold Shift to place them at random");
    appState().dirty = true;
}

void tilesPick(int value, bool fromMap) {
    gTiles.picked = value;
    gTiles.stamp.clear(), gTiles.stampW = gTiles.stampH = 0;
    if (!fromMap && value >= 0) tilesToBrush();
    gTiles.anchor = value;
    gTiles.chosen.clear();
    if (value >= 0) gTiles.chosen.push_back(uint16_t(value));
    if (value >= 0)
        setMessage((fromMap ? L"Selected " : L"Placing ") + tilesValueLabel(value) + L": click or drag to place it (the brush's size and shape)");
    tilesPanelRepaint();
    appState().dirty = true;
}

// Scrolls the grid so the tile shows (clears the search and the filter if they hide it).
void tilesReveal(int value) {
    if (value < 0) return;
    auto find = [&] { return int(std::find(gTiles.values.begin(), gTiles.values.end(), uint16_t(value)) - gTiles.values.begin()); };
    int k = find();
    if (k >= int(gTiles.values.size())) {
        tilesPanelClearSearch();  // (its change rebuilds the grid)
        gTiles.filter = 0;
        tilesPanelShowAllFilter();
        tilesRebuild();
        k = find();
    }
    if (k >= int(gTiles.values.size())) return;
    const RECT c = tilesCellRect(k);
    if (c.top < gTiles.scroll || c.bottom > gTiles.scroll + tilesViewH()) tilesScrollTo(c.top - scale(8));
}

// The grid is for the open map's tileset (another map, or Map properties changed the tileset: build it again).
void tilesFresh() {
    if (!appState().ed || !appState().ed->isOpen() || (gTiles.builtFor == appState().ed.get() && gTiles.builtEra == appState().ed->era())) return;
    if (gTiles.builtEra != appState().ed->era()) gTiles.picked = -1, gTiles.scroll = 0;
    gTiles.pixels.clear();
    tilesRebuild();
}

void tilesAt(float wx, float wy, int& tx, int& ty) {
    tx = std::clamp(int(std::floor(wx / 32)), 0, std::max(0, int(appState().ed->width()) - 1));
    ty = std::clamp(int(std::floor(wy / 32)), 0, std::max(0, int(appState().ed->height()) - 1));
}

// The tiles a brush of the tool strip's size and shape covers around tile (cx, cy): size 0.5 and 1 one tile, then a
// "radius" of size - 1 tiles; diamond, square or circle.
// The brush's tiles: size n is n tiles across (size 2: 2 x 2 tiles; GitHub feedback #121), around the tile at the mouse
// (an even size: the tile at the mouse is the one left of and above the middle).
std::vector<std::pair<int, int>> tilesFootprint(int cx, int cy) {
    std::vector<std::pair<int, int>> out;
    // Its width is the size, its height the Height (GitHub #113; linked: the size): the circle stretches to it. The
    // diamond's height is half its size (GitHub #138: the map's diamonds are 2 to 1; brushHeightOf).
    const int n = std::max(1, int(std::lround(appState().brushSize))), m = std::max(1, int(std::lround(brushHeightOf(appState()))));
    const int lo = -(n - 1) / 2, hi = n / 2, loY = -(m - 1) / 2, hiY = m / 2;
    const float mid = (lo + hi) / 2.0f, midY = (loY + hiY) / 2.0f, r = (n - 1) / 2.0f, rY = (m - 1) / 2.0f;
    for (int dy = loY; dy <= hiY; ++dy)
        for (int dx = lo; dx <= hi; ++dx) {
            // In the units of the width: y scaled by width / height, so a stretched shape keeps the size's rules.
            const float x = dx - mid, y = (dy - midY) * (rY > 0 ? r / rY : 1.0f);
            // The Diamond (its height about half its width): the rows at the middle line (one row, or two at an even
            // height) are the full width; each row further out is narrower, as the diamond's edge at the row's side
            // nearest the middle (GitHub #138: size 7 is 3, 7, 7, 3 tiles; size 5 is 3, 5, 3).
            const float e = std::max(0.0f, std::abs(dy - midY) - 0.5f), halfW = n / 2.0f * (1.0f - e / (m / 2.0f));
            const bool in = appState().brushShape == Brush::Square ? true
                          : n == 1 || m == 1                ? true  // one tile wide or high: the whole line
                          : appState().brushShape == Brush::Circle ? x * x + y * y <= r * r + r + 0.25f
                                                            : std::abs(x) <= halfW + 0.01f;
            if (in) out.push_back({cx + dx, cy + dy});
        }
    return out;
}

int gTilesStrokePlaced = 0;

// The message after tiles went down: how many, and the doodads and units that went with them.
void tilesPlacedMessage(int n, const wchar_t* verb) {
    Editor& ed = *appState().ed;
    const int doodads = ed.takeDoodadsRemovedByTerrain(), units = ed.takeUnitsRemovedByTerrain();
    std::wstring m = std::wstring(verb) + (n == 1 ? L" 1 tile" : L" " + std::to_wstring(n) + L" tiles");
    if (!n) m = L"The tiles are the same already";
    if (doodads) m += L". Removed " + std::to_wstring(doodads) + (doodads == 1 ? L" doodad under them" : L" doodads under them");
    if (units) m += L". Removed " + std::to_wstring(units) + (units == 1 ? L" unit that cannot stand there" : L" units that cannot stand there");
    if (doodads || units) m += L".";
    setMessage(m);
}

// One spray in Rectangle mode: the picked tile on random tiles of the brush's footprint (as many as the density's
// dabs). Part of the spray's stroke.
void tilesSprayAt(float wx, float wy) {
    if (!appState().ed || gTiles.picked < 0) return;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    const auto foot = tilesFootprint(tx, ty);
    if (foot.empty()) return;
    std::mt19937 rng(appState().spraySeed++);
    // Each dab: one of the selected tiles, at random (Ctrl+click or Shift+click in the grid; one tile: that tile).
    std::vector<Editor::TileAt> cells;
    for (int i = 0; i < sprayDabs(); ++i) {
        const uint16_t v = gTiles.chosen.size() > 1 ? gTiles.chosen[rng() % gTiles.chosen.size()] : uint16_t(gTiles.picked);
        const auto [x, y] = foot[rng() % foot.size()];
        cells.push_back({x, y, v});
    }
    gTilesStrokePlaced += appState().ed->placeTileValues(cells);
}

// The picked tile in `cells` (one undo step); with several tiles picked (Shift+click in the list), each cell gets one of
// them at random.
// With a stamp: its pattern from (ox, oy).
int tilesPlaceRandom(const std::vector<std::pair<int, int>>& cells, int ox = 0, int oy = 0) {
    Editor& ed = *appState().ed;
    std::vector<Editor::TileAt> values;
    if (tilesStampOn()) {
        gTiles.stampX = ox, gTiles.stampY = oy;
        for (const auto& [x, y] : cells)
            if (const uint16_t v = tilesStampAt(x, y); v != 0xFFFF) values.push_back({x, y, v});
    } else if (gTiles.chosen.size() < 2) {
        return ed.placeTileCells(cells, uint16_t(gTiles.picked));
    }
    std::mt19937 rng(appState().spraySeed++);
    if (!tilesStampOn())
        for (const auto& [x, y] : cells) values.push_back({x, y, gTiles.chosen[rng() % gTiles.chosen.size()]});
    ed.beginStroke();
    const int n = ed.placeTileValues(values);
    ed.endStroke();
    return n;
}

// Fill in Rectangle mode: the connected tiles (4 ways) of the value under the cursor; with Shift (or Replace) every
// tile of that value, in the selection if there is one. One undo step.
void tilesFill(int tx, int ty, bool every) {
    Editor& ed = *appState().ed;
    const int w = ed.width(), h = ed.height();
    const std::vector<uint16_t>& t = ed.terrain().tiles;
    const uint16_t from = t[size_t(ty) * w + tx];
    std::vector<std::pair<int, int>> cells;
    if (every) {
        int x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
        if (appState().hasTerrainSel)
            x0 = std::max(0, appState().terrainSel.left / 32), y0 = std::max(0, appState().terrainSel.top / 32),
            x1 = std::min(w - 1, (appState().terrainSel.right - 1) / 32), y1 = std::min(h - 1, (appState().terrainSel.bottom - 1) / 32);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (t[size_t(y) * w + x] == from && (!appState().hasTerrainSel || Editor::inSelectShape(appState().terrainSel, selectShape(), x * 32 + 16, y * 32 + 16)))
                    cells.push_back({x, y});  // (in the selection's shape)
    } else {
        std::vector<uint8_t> seen(size_t(w) * h, 0);
        std::vector<std::pair<int, int>> todo{{tx, ty}};
        seen[size_t(ty) * w + tx] = 1;
        while (!todo.empty()) {
            const auto [x, y] = todo.back();
            todo.pop_back();
            cells.push_back({x, y});
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                const size_t i = size_t(ny[k]) * w + nx[k];
                if (!seen[i] && t[i] == from) seen[i] = 1, todo.push_back({nx[k], ny[k]});
            }
        }
    }
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
    tilesPlacedMessage(tilesPlaceRandom(cells, tx, ty), every ? L"Replaced" : L"Filled");
}

// Fill selection (Rectangle mode; GitHub feedback #114): every tile in the selection (in its shape), one undo step.
void tilesFillSelection() {
    if (!appState().ed || !appState().hasTerrainSel || gTiles.picked < 0) return;
    Editor& ed = *appState().ed;
    const int w = ed.width(), h = ed.height();
    std::vector<std::pair<int, int>> cells;
    for (int y = std::max(0, appState().terrainSel.top / 32); y <= std::min(h - 1, (appState().terrainSel.bottom - 1) / 32); ++y)
        for (int x = std::max(0, appState().terrainSel.left / 32); x <= std::min(w - 1, (appState().terrainSel.right - 1) / 32); ++x)
            if (Editor::inSelectShape(appState().terrainSel, selectShape(), x * 32 + 16, y * 32 + 16)) cells.push_back({x, y});
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
    tilesPlacedMessage(tilesPlaceRandom(cells, std::max(0, appState().terrainSel.left / 32), std::max(0, appState().terrainSel.top / 32)), L"Filled the selection:");
}

// The brush at a tile (part of its stroke): the picked tile in the footprint; with several tiles selected in the list,
// a scatter: each tile of the footprint one of them at random (GitHub feedback #120).
int tilesBrushAt(int tx, int ty) {
    if (tilesStampOn() && !gTiles.scatter) {  // the stamp: its block of the pattern at the mouse
        int x0, y0;
        tilesStampOrigin(tx, ty, x0, y0);
        return appState().ed->placeTileValues(tilesStampCells(x0, y0));
    }
    if (tilesStampOn()) {  // Shift: the stamp's tiles at random in the footprint
        std::mt19937 rng(appState().spraySeed++);
        std::vector<uint16_t> any;
        for (uint16_t v : gTiles.stamp)
            if (v != 0xFFFF) any.push_back(v);
        std::vector<Editor::TileAt> cells;
        for (const auto& [x, y] : tilesFootprint(tx, ty)) cells.push_back({x, y, any[rng() % any.size()]});
        return appState().ed->placeTileValues(cells);
    }
    if (gTiles.chosen.size() < 2) return appState().ed->placeTileCells(tilesFootprint(tx, ty), uint16_t(gTiles.picked));
    std::mt19937 rng(appState().spraySeed++);
    std::vector<Editor::TileAt> cells;
    for (const auto& [x, y] : tilesFootprint(tx, ty)) cells.push_back({x, y, gTiles.chosen[rng() % gTiles.chosen.size()]});
    return appState().ed->placeTileValues(cells);
}

// A click on the map in Rectangle mode. Select: a rectangle of tiles, or a paste of the copied tiles. Ctrl: pick the
// tile under the cursor. Brush: the picked tile in the brush's footprint, and on along the drag (one undo step for the
// stroke). Spray: random tiles of the footprint, again while the button is held. Fill: the connected tiles.
bool tilesMouseDown(float wx, float wy, unsigned mods) {
    if (!appState().ed || !appState().ed->isOpen()) return false;
    tilesFresh();
    Editor& ed = *appState().ed;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    const int tool = currentTool();
    if (tool == TSelect) {
        if (appState().terrainPasting && ed.hasTileClipboard()) {
            ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
            tilesPlacedMessage(ed.pasteTiles(tx, ty), L"Pasted");
            // The copy stays on the mouse, as a brush: click again to paste again; right-click or Esc stops (GitHub feedback #123).
            appState().drag = Drag::None;
        } else {
            appState().drag = Drag::TerrainSelect;
        }
        return true;
    }
    if (mods & draw::kCtrl) {
        const uint16_t v = ed.terrain().tiles[size_t(ty) * ed.width() + tx];
        tilesPick(v, true);
        tilesReveal(v);
        appState().drag = Drag::None;
        return true;
    }
    if (gTiles.picked < 0) {
        setMessage(L"Select a tile in the Tiles list first (or Ctrl+click a tile on the map)");
        appState().drag = Drag::None;
        return true;
    }
    if (tool == TFill && !(mods & draw::kShift) && appState().hasTerrainSel &&
        Editor::inSelectShape(appState().terrainSel, selectShape(), int(wx), int(wy))) {  // in the selection: all of it
        tilesFillSelection();
        appState().drag = Drag::None;
        return true;
    }
    if (tool == TFill || tool == TReplace) {
        tilesFill(tx, ty, tool == TReplace || (mods & draw::kShift));
        appState().drag = Drag::None;
        return true;
    }
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();
    ed.beginStroke();
    gTilesStrokePlaced = 0;
    if (tool == TSpray) {
        tilesSprayAt(wx, wy);
        appState().drag = Drag::Spray;
        return true;
    }
    gTiles.scatter = (mods & draw::kShift) != 0;
    if (tilesStampOn()) tilesStampOrigin(tx, ty, gTiles.stampX, gTiles.stampY);  // the pattern starts at the press
    appState().drag = Drag::Tiles;
    gTilesStrokePlaced = tilesBrushAt(tx, ty);
    gTiles.lastX = tx, gTiles.lastY = ty;
    return true;
}

// The drag: the footprint at the mouse; the stamp: each block of its pattern along the way from the last tile (a fast
// drag leaves no gaps).
void tilesMouseMove(float wx, float wy) {
    if (!appState().ed || gTiles.picked < 0) return;
    int tx, ty;
    tilesAt(wx, wy, tx, ty);
    if (tilesStampOn() && !gTiles.scatter) {
        const int steps = std::max(std::abs(tx - gTiles.lastX), std::abs(ty - gTiles.lastY));
        int bx = INT_MIN, by = INT_MIN;
        for (int i = 1; i <= steps; ++i) {
            const int x = gTiles.lastX + (tx - gTiles.lastX) * i / steps, y = gTiles.lastY + (ty - gTiles.lastY) * i / steps;
            int x0, y0;
            tilesStampOrigin(x, y, x0, y0);
            if (x0 != bx || y0 != by) bx = x0, by = y0, gTilesStrokePlaced += appState().ed->placeTileValues(tilesStampCells(x0, y0));
        }
    } else {
        gTilesStrokePlaced += tilesBrushAt(tx, ty);
    }
    gTiles.lastX = tx, gTiles.lastY = ty;
}

// The end of a Brush or Spray stroke in Rectangle mode.
void tilesMouseUp(float, float) {
    if (!appState().ed) return;
    appState().ed->endStroke();
    tilesPlacedMessage(gTilesStrokePlaced, L"Placed");
}

