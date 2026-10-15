#include <bit>

// --- Rectangle mode: tiles as they are (roadmap N.7) --------------------------------------------------------------
// The Terrain tool strip's Isometric | Rectangle choice. In Rectangle mode the terrain tools work on tiles, as they
// are, without the terrain correction, as StarEdit's rectangular terrain and SCMDraft's tile palette do
// (Editor::placeTiles): the Brush places the picked tile in the brush's size and shape (a click, or along a drag), the
// Spray places it at random in the brush, Fill fills the connected tiles of one value (Shift: every tile of that value),
// Select selects a rectangle of tiles and Ctrl+C / Ctrl+V copy and paste the tiles as they are. Ctrl+click picks the
// tile under the cursor. The panel takes the place of the terrain palette: one grid of every tile of the map's tileset
// (CV5), group by group and in each group by its index, null tiles too, with a search box. The filter box above it
// shows all tiles, or only one terrain type's tiles (the terrain types of Isometric mode; a tile group's terrain types
// come from the tile rules: tileGroupTerrains), the doodads' tiles or the null tiles. Later ISOM brushes keep
// the placed tiles unless they change the ISOM of their rects.
// The panel is the core's (core/views_tiles.inc: gTilesPanel, gTilesGrid, the grid's model and paint); this file is the
// Win32 program's part: the panel's windows in the palette's place (tilesSync), and the datashots (the preview on the map,
// tilesScene, is core/map_scene.inc). main.cpp has the hooks: the choice in the tool strip, tilesSync (layout, fillToolCombo), the map clicks,
// the preview in buildScene, copy and paste, and the datashot spec.

// The panel's windows: a view window with gTilesPanel (the filter box and the search box), and in it a view window with
// gTilesGrid and the native vertical bar (the window's host keeps the bar in step with the grid's scroll).
void tilesCreate() {
    gTiles.wnd = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, &gTilesPanel);
    gTiles.list = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 1, 1, gTiles.wnd, nullptr, app.inst, &gTilesGrid);
    darkControl(gTiles.list, L"DarkMode_Explorer");  // the scroll bar in the theme
}

// Shows the Tiles panel in the place of the palette while the Tiles tool is on (and the palette again after).
void tilesSync() {
    const bool on = tilesOn();
    if (on && !gTiles.wnd) tilesCreate();
    if (!gTiles.wnd) return;
    if (on) {
        // The palette's place: from its search box down to its end.
        RECT a{}, b{};
        GetWindowRect(gPal.search, &a);
        GetWindowRect(gPal.wnd, &b);
        MapWindowPoints(nullptr, app.main, reinterpret_cast<POINT*>(&a), 2);
        MapWindowPoints(nullptr, app.main, reinterpret_cast<POINT*>(&b), 2);
        for (HWND h : {gPal.search, gPal.viewBtn, gPal.wnd})
            if (h) ShowWindow(h, SW_HIDE);
        MoveWindow(gTiles.wnd, a.left, a.top, std::max(1, int(b.right - a.left)), std::max(1, int(b.bottom - a.top)), TRUE);
        const draw::Rect g = gTilesPanel.gridRect();  // (the panel's view has the window's new size now)
        MoveWindow(gTiles.list, g.left, g.top, g.width(), g.height(), TRUE);
        ShowWindow(gTiles.wnd, SW_SHOW);
        tilesFresh();
        InvalidateRect(gTiles.wnd, nullptr, FALSE);
        InvalidateRect(gTiles.list, nullptr, FALSE);
    } else if (gTiles.shown) {
        ShowWindow(gTiles.wnd, SW_HIDE);
        const bool show = gWorkspace == Workspace::Map && app.mode != Mode::Locations;
        for (HWND h : {gPal.search, gPal.viewBtn, gPal.wnd})
            if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    }
    gTiles.shown = on;
}

// --- On the map ---

// --- Datashots ---

void pump();
bool capture(const fs::path& file, HWND wnd);

// tiles:[<search>]: the Map workspace in Terrain mode, Rectangle, the search set, the first tile with a picture picked:
// tiles.png and tiles_panel.png.
// tilesplace:[<tile value>]: places the picked tile (or that value) on one tile and as a 6 x 4 rectangle at the middle
// of the view, then the same rectangle again with a brush beside it: tiles_placed.png, tiles_brushed.png.
bool tilesDatashot(const std::string& spec, const fs::path& dir) {
    if (spec.rfind("tiles:", 0) == 0) {
        setWorkspace(Workspace::Map);
        setMode(Mode::Terrain);
        app.terrainRect = true;
        app.toolByMode[int(Mode::Terrain)] = TBrush;
        fillToolCombo();
        std::string search = spec.substr(6);
        tilesSetSearch(widen(search));
        gTiles.scroll = 0;
        tilesRebuild();
        // The first tile that has a picture.
        for (uint16_t v : gTiles.values)
            if (!tilesIsNull(v)) {
                gTiles.picked = v;
                break;
            }
        size_t nulls = 0;
        for (uint16_t v : gTiles.values) nulls += tilesIsNull(v);
        InvalidateRect(gTiles.wnd, nullptr, FALSE);
        for (int i = 0; i < 8; ++i) pump();
        capture(dir / "tiles.png", nullptr);
        capture(dir / "tiles_panel.png", gTiles.wnd);
        std::ofstream log(dir / "datashots.txt", std::ios::app);
        log << "tiles: " << (app.ed ? tilesetName(app.ed->era()) : "-") << ", " << gTiles.values.size() << " tiles shown ("
            << nulls << " null tiles) of " << (app.ed ? app.ed->tileset().groups.size() * 16 : 0) << ", " << gTiles.perRow
            << " in a row, picked " << gTiles.picked << "\n";
        return true;
    }
    if (spec.rfind("tilesplace:", 0) == 0) {
        if (!app.ed || !app.ed->isOpen()) return true;
        if (spec.size() > 11) gTiles.picked = std::atoi(spec.c_str() + 11);
        if (gTiles.picked < 0) return true;
        Editor& ed = *app.ed;
        // The middle of the map, at 100%.
        const int cx = ed.width() / 2, cy = ed.height() / 2;
        int vw, vh;
        viewSize(vw, vh);
        app.zoom = 1;
        app.camX = cx * 32.0f - vw / 2.0f, app.camY = cy * 32.0f - vh / 2.0f;
        const int n1 = ed.placeTiles(cx + 2, cy - 4, cx + 2, cy - 4, uint16_t(gTiles.picked));
        const int n2 = ed.placeTiles(cx - 3, cy - 2, cx + 2, cy + 1, uint16_t(gTiles.picked));
        app.dirty = true;
        for (int i = 0; i < 8; ++i) pump();
        capture(dir / "tiles_placed.png", nullptr);
        // A brush beside the rectangle: the tiles whose rects it does not change stay.
        int brushed = 0;
        if (ed.canPaintTerrain()) {
            const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era());
            const uint16_t at = ed.terrainAt((cx - 6) * 32, cy * 32);
            uint16_t other = 1;
            while (t && other < 64 && (!t->isTerrain(other) || other == at || !t->linked(at, other))) ++other;
            if (other < 64) ed.paintTerrain((cx - 6) * 32, cy * 32, other, 1);
            for (int y = cy - 2; y <= cy + 1; ++y)
                for (int x = cx - 3; x <= cx + 2; ++x) brushed += ed.terrain().tiles[size_t(y) * ed.width() + x] == gTiles.picked;
            app.dirty = true;
            for (int i = 0; i < 8; ++i) pump();
            capture(dir / "tiles_brushed.png", nullptr);
        }
        std::ofstream log(dir / "datashots.txt", std::ios::app);
        log << "tilesplace: tile " << gTiles.picked << " at " << cx << "," << cy << ": " << n1 << " + " << n2 << " tiles placed, " << brushed
            << " of 24 still there after the brush, invalid ISOM diamonds " << ed.invalidTerrain() << "\n";
        return true;
    }
    return false;
}
