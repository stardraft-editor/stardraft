// A tile's variations (subtile replacement): the other megatiles of its CV5 group, which the brushes pick from at
// random (src/isom.cpp: the common ones before the first empty megatile, the rare ones after it). Terrain mode shows
// them for the selected tiles (core/views_variations.inc, in the side bar) and Alt + wheel on a tile steps through
// them (core/map_input.inc: mapWheel). A variation goes to the tile, to the other half of its ISOM rect
// (the next or the one before group, the same subtile), and to the tiles of the stack above and under them (a wall or
// cliff: StarEdit gives a stack one variation, so a gate keeps its parts), unless only the tile is asked for.

void invalidateMapView();  // (the host's: the map view is drawn again)

struct TileVariations {
    uint16_t group = 0;
    std::vector<uint8_t> subs;  // the subtiles with a megatile, in order
    int common = 0;             // the first `common` of them are the common variations, the others are rare
};

TileVariations tileVariations(const Editor& ed, uint16_t value) {
    TileVariations v;
    v.group = uint16_t(value >> 4);
    const auto& groups = ed.tileset().groups;
    if (v.group >= groups.size()) return v;
    const auto& mega = groups[v.group].megatiles;
    int run = 0;
    while (run < 16 && mega[size_t(run)]) ++run;
    for (int s = 0; s < 16; ++s)
        if (mega[size_t(s)]) v.subs.push_back(uint8_t(s));
    v.common = run;
    return v;
}

// The tile at (x, y) of the map (0xFFFF: not on the map).
uint16_t mapTileAt(const Editor& ed, int x, int y) {
    if (x < 0 || y < 0 || x >= int(ed.width()) || y >= int(ed.height())) return 0xFFFF;
    return ed.terrain().tiles[size_t(y) * size_t(ed.width()) + size_t(x)];
}

// The tiles that take variation `sub` with the tile at (x, y), and their new values: the tile; with `whole`, the other
// half of its rect and the stacks above and under both (each while its group has that variation and no doodad is on it).
std::vector<Editor::TileAt> tileVariationCells(const Editor& ed, int x, int y, int sub, bool whole) {
    std::vector<Editor::TileAt> out;
    const auto& groups = ed.tileset().groups;
    auto has = [&](uint16_t value) {
        const uint16_t g = uint16_t(value >> 4);
        return value != 0xFFFF && g < groups.size() && groups[g].megatiles[size_t(sub)] != 0;
    };
    auto free = [&](int tx, int ty) { return ed.doodadAt(tx * 32 + 16, ty * 32 + 16) < 0; };
    const uint16_t t = mapTileAt(ed, x, y);
    if (!has(t) || !free(x, y)) return out;
    auto add = [&](int tx, int ty, uint16_t value) { out.push_back({tx, ty, uint16_t((value & 0xFFF0) | sub)}); };
    add(x, y, t);
    if (!whole) return out;
    std::vector<int> columns{x};
    const int px = (x & 1) ? x - 1 : x + 1;  // (the rect's other half: tiles 2rx and 2rx + 1)
    const uint16_t pt = mapTileAt(ed, px, y);
    if (has(pt) && free(px, y) && (pt & 15) == (t & 15) && int(pt >> 4) == int(t >> 4) + ((x & 1) ? -1 : 1))
        add(px, y, pt), columns.push_back(px);
    const stardraft::IsomTiles* stacks = stardraft::IsomTiles::forTileset(ed.tileset());
    if (!stacks) return out;
    for (const int cx : columns) {
        const uint16_t start = mapTileAt(ed, cx, y);
        uint16_t g = uint16_t(start >> 4);
        for (int ty = y - 1;; --ty) {  // up: the tile above holds this one (its bottom is this one's top)
            const uint16_t a = mapTileAt(ed, cx, ty);
            if (!stacks->stackTop(g) || a == 0xFFFF || stacks->stackTop(g) != stacks->stackBottom(uint16_t(a >> 4)) || !has(a) || !free(cx, ty)) break;
            add(cx, ty, a), g = uint16_t(a >> 4);
        }
        g = uint16_t(start >> 4);
        for (int ty = y + 1;; ++ty) {  // down
            const uint16_t b = mapTileAt(ed, cx, ty);
            if (b == 0xFFFF || !stacks->stackTop(uint16_t(b >> 4)) || stacks->stackTop(uint16_t(b >> 4)) != stacks->stackBottom(g) || !has(b) || !free(cx, ty)) break;
            add(cx, ty, b), g = uint16_t(b >> 4);
        }
    }
    return out;
}

// The tile at (x, y) (and, with `whole`, its rect and stack) takes variation `sub`: one undo step. The status bar says
// what changed.
bool simBarShown();  // (a live game: the map does not change)
bool setTileVariation(int x, int y, int sub, bool whole) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen() || sub < 0 || sub > 15 || simBarShown()) return false;
    Editor& ed = *app.ed;
    if (ed.doodadAt(x * 32 + 16, y * 32 + 16) >= 0) return setMessage(app, tr(L"A doodad is on this tile. Doodad tiles have no variations")), false;
    const std::vector<Editor::TileAt> cells = tileVariationCells(ed, x, y, sub, whole);
    if (cells.empty()) return setMessage(app, tr(L"This tile has no such variation")), false;
    const int changed = ed.placeTileValues(cells);
    ed.takeDoodadsRemovedByTerrain(), ed.takeUnitsRemovedByTerrain();  // (the doodads' tiles are not changed: none go)
    app.dirty = true;
    const TileVariations v = tileVariations(ed, mapTileAt(ed, x, y));
    const auto at = std::find(v.subs.begin(), v.subs.end(), uint8_t(sub));
    const int number = int(at - v.subs.begin()) + 1;
    wchar_t m[160];
    if (!changed) std::swprintf(m, 160, tr(L"The tile uses variation %d of %d already"), number, int(v.subs.size()));
    else if (changed == 1) std::swprintf(m, 160, tr(L"Variation %d of %d on the tile (Ctrl+Z undoes it)"), number, int(v.subs.size()));
    else std::swprintf(m, 160, tr(L"Variation %d of %d on %d tiles (Ctrl+Z undoes it)"), number, int(v.subs.size()), changed);
    setMessage(app, m);
    return changed > 0;
}

// Alt + wheel on the tile at (x, y): its next (or, `back`, previous) variation, for its rect and stack.
bool stepTileVariation(int x, int y, bool back) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return false;
    const uint16_t t = mapTileAt(*app.ed, x, y);
    if (t == 0xFFFF) return false;
    const TileVariations v = tileVariations(*app.ed, t);
    if (v.subs.size() < 2) return setMessage(app, tr(L"This tile has no other variations")), false;
    const auto at = std::find(v.subs.begin(), v.subs.end(), uint8_t(t & 15));
    const int n = int(v.subs.size()), k = at == v.subs.end() ? 0 : int(at - v.subs.begin());
    return setTileVariation(x, y, v.subs[size_t((k + (back ? n - 1 : 1)) % n)], true);
}

// The tiles that the side bar's strip shows the variations of: the selected tiles, all together (at most
// kVariationTiles; one more in the list says that the selection has more).
constexpr size_t kVariationTiles = 6;  // (a piece of at most kVariationW x kVariationH tiles: a rect's two tiles, up a stack)
constexpr int kVariationW = 2, kVariationH = 3;
std::vector<std::pair<int, int>> variationsTiles() {
    const AppState& app = appState();
    if (app.mode != Mode::Terrain || !app.hasTerrainSel || !app.ed || !app.ed->isOpen()) return {};
    std::vector<std::pair<int, int>> tiles;  // (as terrainSelTiles, up to the most: a big selection is not gone through each frame)
    const Rect& r = app.terrainSel;
    for (int y = std::max(0, r.top / 32); y <= std::min(int(app.ed->height()) - 1, (r.bottom - 1) / 32) && tiles.size() <= kVariationTiles; ++y)
        for (int x = std::max(0, r.left / 32); x <= std::min(int(app.ed->width()) - 1, (r.right - 1) / 32) && tiles.size() <= kVariationTiles; ++x)
            if (terrainSelHas(x, y)) tiles.push_back({x, y});
    return tiles;
}
// A number that changes when the strip has to be drawn again: the selected tiles and their values.
uint64_t variationsKey() {
    const AppState& app = appState();
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(uint64_t(reinterpret_cast<uintptr_t>(app.ed.get())));  // (another map: its tiles)
    for (const auto& [x, y] : variationsTiles()) mix(uint64_t(x) << 40 | uint64_t(y) << 20 | mapTileAt(*app.ed, x, y));
    return h;
}
// The selection's variations as one piece: variation `s` (a subtile, 0 to 15) puts subtile s on each selected tile
// whose group has a megatile there (the others stay as they are, also the tiles under a doodad), as StarEdit gives a
// stack one subtile. The variations: the subtiles that the selected tiles not under a doodad have, in order. Made once
// for each change of the selection or its tiles (variationsKey), not for each paint.
struct SelectionPiece {
    uint64_t key = 0;
    std::vector<std::pair<int, int>> tiles;  // (empty: no selection, or more than kVariationTiles)
    bool tooMany = false;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<int> subs;                       // the variations
    std::vector<std::vector<uint16_t>> values;   // for each variation, each tile's value with it
    std::vector<uint8_t> now;                    // for each variation: the tiles have it now
};
const SelectionPiece& selectionPiece() {
    static SelectionPiece p;
    const uint64_t key = appState().ed ? variationsKey() : 0;
    if (key == p.key && key) return p;
    p = SelectionPiece{};
    p.key = key;
    const AppState& app = appState();
    if (!app.ed) return p;
    const Editor& ed = *app.ed;
    std::vector<std::pair<int, int>> tiles = variationsTiles();
    if (tiles.size() > kVariationTiles) return p.tooMany = true, p;
    if (tiles.empty()) return p;
    int x1 = INT_MIN, y1 = INT_MIN;
    p.x0 = p.y0 = INT_MAX;
    for (const auto& [x, y] : tiles) p.x0 = std::min(p.x0, x), p.y0 = std::min(p.y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    p.w = x1 - p.x0 + 1, p.h = y1 - p.y0 + 1;
    if (p.w > kVariationW || p.h > kVariationH) return p.tooMany = true, p;
    std::vector<uint16_t> cur;
    std::vector<uint8_t> free;
    std::array<bool, 16> has{};
    for (const auto& [x, y] : tiles) {
        cur.push_back(mapTileAt(ed, x, y));
        free.push_back(ed.doodadAt(x * 32 + 16, y * 32 + 16) < 0);
        if (free.back())
            for (const uint8_t s : tileVariations(ed, cur.back()).subs) has[s] = true;
    }
    const auto& groups = ed.tileset().groups;
    for (int s = 0; s < 16; ++s) {
        if (!has[size_t(s)]) continue;
        std::vector<uint16_t> v = cur;
        bool same = true;
        for (size_t i = 0; i < v.size(); ++i)
            if (free[i] && v[i] != 0xFFFF && size_t(v[i] >> 4) < groups.size() && groups[v[i] >> 4].megatiles[size_t(s)]) {
                v[i] = uint16_t((v[i] & 0xFFF0) | s);
                if (v[i] != cur[i]) same = false;
            }
        p.subs.push_back(s), p.values.push_back(std::move(v)), p.now.push_back(same);
    }
    p.tiles = std::move(tiles);
    return p;
}
// The tiles that variation k of the piece changes, with their new values.
std::vector<Editor::TileAt> pieceVariationCells(const SelectionPiece& p, size_t k) {
    std::vector<Editor::TileAt> out;
    if (k >= p.values.size() || !appState().ed) return out;
    for (size_t i = 0; i < p.tiles.size(); ++i)
        if (p.values[k][i] != mapTileAt(*appState().ed, p.tiles[i].first, p.tiles[i].second)) out.push_back({p.tiles[i].first, p.tiles[i].second, p.values[k][i]});
    return out;
}
// A click on variation k of the selection: its tiles take it, one undo step.
bool setSelectionVariation(size_t k) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen() || simBarShown()) return false;
    const std::vector<Editor::TileAt> cells = pieceVariationCells(selectionPiece(), k);
    if (cells.empty()) return setMessage(app, tr(L"The selected tiles use this variation already")), false;
    const int changed = app.ed->placeTileValues(cells);
    app.ed->takeDoodadsRemovedByTerrain(), app.ed->takeUnitsRemovedByTerrain();  // (the doodads' tiles are not changed: none go)
    app.dirty = true;
    wchar_t m[160];
    if (changed == 0) std::swprintf(m, 160, L"%ls", tr(L"No tile changed: the selected tiles are protected"));
    else if (changed == 1) std::swprintf(m, 160, tr(L"Variation %d on the tile (Ctrl+Z undoes it)"), int(k) + 1);
    else std::swprintf(m, 160, tr(L"Variation %d on %d tiles (Ctrl+Z undoes it)"), int(k) + 1, changed);
    setMessage(app, m);
    return changed > 0;
}

// The map's preview while the mouse is over a variation in the strip (MTXM index, tile value): drawn in place of the
// tiles (the host's terrain preview), with no change to the map.
std::vector<std::pair<int, uint16_t>> gVarPreview;
bool gVarStripHover = false;  // the mouse is over the strip: the map shows the selection's tiles with no outline
void setVariationStripHover(bool on) {
    if (on != gVarStripHover) gVarStripHover = on, invalidateMapView();
}
void setVariationPreview(const std::vector<Editor::TileAt>& cells) {
    std::vector<std::pair<int, uint16_t>> now;
    if (const AppState& app = appState(); app.ed)
        for (const Editor::TileAt& c : cells) now.push_back({c.y * int(app.ed->width()) + c.x, c.value});
    if (now != gVarPreview) gVarPreview = std::move(now), invalidateMapView();
}
