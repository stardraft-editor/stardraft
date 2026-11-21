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

// A mod terrain's variations (a mod's tileset): its pieces are pairs of tiles of their own, not the subtiles of a group
// (a group can have the halves of two rects), so the variations of the rect at tile (x, y) are its terrain's pieces for
// that rect, and the rects that stack with it (a wall's rows: the game's stack links) take the piece of the same variant
// (the rules file's variant; else one group down at the same subtiles, as the built-in sets have their rows).
struct ModVariations {
    std::vector<std::pair<int, int>> tiles;     // the block's tiles: its rows, two a row
    std::vector<std::vector<uint16_t>> values;  // for each variation, each tile's value
    int now = -1;                               // the variation the block has now
    std::vector<int> keys;                      // for each variation: its variant (the rules file's; else its place in the list)
};
bool modVariations(const Editor& ed, int x, int y, ModVariations& out) {
    out = {};
    const stardraft::Tileset& ts = ed.tileset();
    if (ts.modTerrains.empty() || y < 0 || x < 0) return false;
    const stardraft::IsomTiles* stacks = stardraft::IsomTiles::forTileset(ts);
    const int lx = x & ~1;
    auto pairAt = [&](int ty) { return std::pair<uint16_t, uint16_t>{mapTileAt(ed, lx, ty), mapTileAt(ed, lx + 1, ty)}; };
    auto groupsIn = [&](const stardraft::Tileset::ModTerrain& m, std::pair<uint16_t, uint16_t> p) {  // (a pair can be in more than one group)
        std::vector<int> out;
        for (const auto& q : m.pieces)
            if (q[1] == p.first && q[2] == p.second && !stardraft::terrainrules::nullHalf(q[1]) && !stardraft::terrainrules::nullHalf(q[2]) &&
                std::find(out.begin(), out.end(), int(q[0])) == out.end())
                out.push_back(q[0]);
        return out;
    };
    auto groupIn = [&](const stardraft::Tileset::ModTerrain& m, std::pair<uint16_t, uint16_t> p) -> int {
        const auto gs = groupsIn(m, p);
        return gs.empty() ? -1 : gs.front();
    };
    const auto here = pairAt(y);
    if (here.first == 0xFFFF || here.second == 0xFFFF) return false;
    const stardraft::Tileset::ModTerrain* mt = nullptr;
    int g = -1;
    for (const auto& m : ts.modTerrains)
        if ((g = groupIn(m, here)) >= 0) {
            mt = &m;
            break;
        }
    if (!mt) return false;
    const auto& m = *mt;
    auto free = [&](int ty) { return ed.doodadAt(lx * 32 + 16, ty * 32 + 16) < 0 && ed.doodadAt(lx * 32 + 48, ty * 32 + 16) < 0; };
    // (the block: the rows over and under that stack with it, a few at most)
    // (the groups of the pair here that stack with a row above or under, first; a pair in more than one group: the one
    // whose stack link meets)
    auto linked = [&](int upper, int lower) { return stacks && stacks->stackBottom(uint16_t(upper)) && stacks->stackBottom(uint16_t(upper)) == stacks->stackTop(uint16_t(lower)); };
    {
        const auto mine = groupsIn(m, here), up = y > 0 ? groupsIn(m, pairAt(y - 1)) : std::vector<int>{}, down = groupsIn(m, pairAt(y + 1));
        for (int c : mine)
            if (std::any_of(up.begin(), up.end(), [&](int a) { return linked(a, c); }) || std::any_of(down.begin(), down.end(), [&](int b) { return linked(c, b); })) {
                g = c;
                break;
            }
    }
    std::vector<std::pair<int, int>> rows{{y, g}};  // (row, game group)
    for (int ty = y - 1; stacks && ty >= 0 && y - ty <= 3; --ty) {
        int ga = -1;
        for (int a : groupsIn(m, pairAt(ty)))
            if (ga < 0 && linked(a, rows.front().second)) ga = a;
        if (ga < 0 || !free(ty)) break;
        rows.insert(rows.begin(), {ty, ga});
    }
    for (int ty = y + 1; stacks && ty < int(ed.height()) && ty - y <= 3; ++ty) {
        int gb = -1;
        for (int b : groupsIn(m, pairAt(ty)))
            if (gb < 0 && linked(rows.back().second, b)) gb = b;
        if (gb < 0 || !free(ty)) break;
        rows.push_back({ty, gb});
    }
    // (a protected tile in the block: no variations, the block stays one piece)
    for (const auto& [ty, gr] : rows)
        if (ed.tileProtected(lx, ty) || ed.tileProtected(lx + 1, ty)) return false;
    if (!free(y)) return false;
    // (the choices: the top row's pieces, by variant)
    const int gTop = rows.front().second;
    auto variantOf = [&](int gr, std::pair<uint16_t, uint16_t> p) -> int {
        const auto f = m.pieceVariant.find({uint16_t(gr), p.first, p.second});
        return f == m.pieceVariant.end() ? -1 : f->second;
    };
    std::vector<std::pair<uint16_t, uint16_t>> choices;
    for (const auto& q : m.pieces)
        if (q[0] == gTop && !stardraft::terrainrules::nullHalf(q[1]) && !stardraft::terrainrules::nullHalf(q[2]) &&
            std::find(choices.begin(), choices.end(), std::make_pair(q[1], q[2])) == choices.end())
            choices.push_back({q[1], q[2]});
    std::stable_sort(choices.begin(), choices.end(), [&](const auto& a, const auto& b) { return variantOf(gTop, a) < variantOf(gTop, b); });
    if (choices.empty()) return false;
    for (const auto& [ty, gr] : rows) out.tiles.push_back({lx, ty}), out.tiles.push_back({lx + 1, ty});
    std::vector<uint16_t> cur;
    for (const auto& [tx, ty] : out.tiles) cur.push_back(mapTileAt(ed, tx, ty));
    for (const auto& c : choices) {
        std::vector<uint16_t> v = cur;
        const int k = variantOf(gTop, c);
        for (size_t d = 0; d < rows.size(); ++d) {
            const int gr = rows[d].second;
            std::pair<uint16_t, uint16_t> pick{0xFFFF, 0xFFFF};
            if (d == 0) pick = c;
            else {
                for (const auto& q : m.pieces)  // (the same variant)
                    if (k >= 0 && q[0] == gr && variantOf(gr, {q[1], q[2]}) == k && !stardraft::terrainrules::nullHalf(q[1])) pick = {q[1], q[2]};
                if (pick.first == 0xFFFF && groupIn(m, {uint16_t(c.first + 16 * d), uint16_t(c.second + 16 * d)}) == gr)  // (one group down a row)
                    pick = {uint16_t(c.first + 16 * d), uint16_t(c.second + 16 * d)};
            }
            if (pick.first != 0xFFFF) v[2 * d] = pick.first, v[2 * d + 1] = pick.second;
        }
        if (std::find(out.values.begin(), out.values.end(), v) != out.values.end()) continue;
        if (v == cur) out.now = int(out.values.size());
        out.keys.push_back(k >= 0 ? k : int(out.values.size()));
        out.values.push_back(std::move(v));
    }
    if (out.now < 0)  // (the rows under it are not this variant's: the variation of its top row's piece)
        for (size_t i = 0; i < out.values.size() && out.now < 0; ++i)
            if (out.values[i][0] == cur[0] && out.values[i][1] == cur[1]) out.now = int(i);
    return true;
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
    if (ed.doodadAt(x * 32 + 16, y * 32 + 16) >= 0) return setMessage(app, tr(L"A doodad is on this tile. Doodad tiles have no variations.")), false;
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
    if (ModVariations mv; modVariations(*app.ed, x, y, mv)) {  // (a mod terrain's rect: its pieces)
        if (mv.values.size() < 2) return setMessage(app, tr(L"This tile has no other variations")), false;
        if (simBarShown()) return false;
        const int n = int(mv.values.size()), k = (std::max(0, mv.now) + (back ? n - 1 : 1)) % n;
        std::vector<Editor::TileAt> cells;
        for (size_t i = 0; i < mv.tiles.size(); ++i)
            if (mv.values[size_t(k)][i] != mapTileAt(*app.ed, mv.tiles[i].first, mv.tiles[i].second))
                cells.push_back({mv.tiles[i].first, mv.tiles[i].second, mv.values[size_t(k)][i]});
        const int changed = cells.empty() ? 0 : app.ed->placeTileValues(cells);
        app.ed->takeDoodadsRemovedByTerrain(), app.ed->takeUnitsRemovedByTerrain();
        app.dirty = app.dirty || changed > 0;
        if (!changed) return setMessage(app, tr(L"No tile changed: the selected tiles are protected")), false;
        wchar_t m[160];
        std::swprintf(m, 160, tr(L"Variation %d of %d on %d tiles (Ctrl+Z undoes it)"), k + 1, n, changed);
        setMessage(app, m);
        return changed > 0;
    }
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
    std::vector<uint16_t> held;                  // (a mod terrain's block: its tiles' values when made; another: made again)
};
const SelectionPiece& selectionPiece() {
    static SelectionPiece p;
    const uint64_t key = appState().ed ? variationsKey() : 0;
    bool same = key == p.key && key;
    if (same && !p.held.empty() && appState().ed)  // (a mod terrain's block: also its tiles outside the selection)
        for (size_t i = 0; i < p.tiles.size() && same; ++i) same = mapTileAt(*appState().ed, p.tiles[i].first, p.tiles[i].second) == p.held[i];
    if (same) return p;
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
    // (mod terrains' rects: their pieces, for each block a selected tile is in (its rect and the rows that stack with
    // it); variation k puts variant k on each block that has it, as a game terrain's puts a subtile on each tile that has
    // it. Selected tiles of no mod terrain (the ground under a wall) stay as they are)
    if (p.w <= kVariationW && p.h <= kVariationH) {
        std::vector<ModVariations> blocks;
        for (const auto& [x, y] : tiles) {
            const bool known = std::any_of(blocks.begin(), blocks.end(), [&](const ModVariations& b) {
                return std::find(b.tiles.begin(), b.tiles.end(), std::make_pair(x, y)) != b.tiles.end();
            });
            if (ModVariations mv; !known && modVariations(ed, x, y, mv)) blocks.push_back(std::move(mv));
        }
        if (!blocks.empty()) {
            std::vector<std::pair<int, int>> all;  // (every block's tiles, once)
            for (const auto& b : blocks)
                for (const auto& t : b.tiles)
                    if (std::find(all.begin(), all.end(), t) == all.end()) all.push_back(t);
            std::vector<uint16_t> cur;
            for (const auto& [x, y] : all) cur.push_back(mapTileAt(ed, x, y));
            std::set<int> keys;
            for (const auto& b : blocks) keys.insert(b.keys.begin(), b.keys.end());
            for (int key : keys) {
                std::vector<uint16_t> v = cur;
                bool now = true;
                for (const auto& b : blocks) {
                    const auto at = std::find(b.keys.begin(), b.keys.end(), key);
                    if (at == b.keys.end()) continue;
                    const size_t i = size_t(at - b.keys.begin());
                    now = now && int(i) == b.now;
                    for (size_t t = 0; t < b.tiles.size(); ++t)
                        v[size_t(std::find(all.begin(), all.end(), b.tiles[t]) - all.begin())] = b.values[i][t];
                }
                if (std::find(p.values.begin(), p.values.end(), v) != p.values.end()) continue;
                p.subs.push_back(key), p.now.push_back(now), p.values.push_back(std::move(v));
            }
            p.tiles = std::move(all);
            p.x0 = p.y0 = INT_MAX, x1 = y1 = INT_MIN;
            for (const auto& [x, y] : p.tiles) p.x0 = std::min(p.x0, x), p.y0 = std::min(p.y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            p.w = x1 - p.x0 + 1, p.h = y1 - p.y0 + 1;
            p.held = std::move(cur);
            return p;
        }
    }
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
