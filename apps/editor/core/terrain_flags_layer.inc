// View > Overlays > Terrain flags (roadmap R34; portable, core.inc): what the tileset says about each place of the
// terrain, in one of four views:
//   - Walkable: the minitiles (8 px) that ground units can walk on (green) and the others (red), VF4 flag 0x1.
//   - Height: low (blue), middle (yellow) and high ground (red), VF4 flags 0x2 and 0x4. Units on lower ground do not
//     see up, and their attacks up can miss.
//   - Blocks view: the minitiles that block the line of sight (VF4 flag 0x8).
//   - Buildable: the tiles (32 px) where buildings can go (green) and the others (red), from the CV5 group.
// The shapes are made again when the terrain changes (its revision), the tileset or the view changes.

enum class TerrainFlagsView { Off, Walkable, Height, BlocksView, Buildable };

struct TerrainFlagsLayer {
    TerrainFlagsView view = TerrainFlagsView::Off;
    std::vector<render::RectDraw> fills;
    const Editor* shapesFor = nullptr;
    uint64_t shapesRev = 0;
    const Tileset* tilesetOf = nullptr;
    int era = -1, key = -1;
};
TerrainFlagsLayer gTerrainFlags;

TerrainFlagsView terrainFlagsView() { return gTerrainFlags.view; }

// The fills of a grid of kinds (w by h cells of `cell` px; kind 0: no fill): runs of one kind on each row, joined
// with the same run of the row above.
void terrainFlagsFills(const std::vector<uint8_t>& kind, int w, int h, float cell, const uint32_t* colors) {
    struct Run {
        int x0, x1, kind, y0;
    };
    std::vector<Run> open, row, next;
    auto emit = [&](const Run& run, int y1) {
        if (!run.kind) return;
        render::RectDraw d;
        d.x0 = run.x0 * cell, d.y0 = run.y0 * cell, d.x1 = run.x1 * cell, d.y1 = y1 * cell, d.rgba = colors[run.kind], d.fill = true;
        gTerrainFlags.fills.push_back(d);
    };
    for (int y = 0; y <= h; ++y) {
        row.clear();
        if (y < h)
            for (int x = 0; x < w;) {
                const int k = kind[size_t(y) * w + x];
                int e = x + 1;
                while (e < w && kind[size_t(y) * w + e] == k) ++e;
                row.push_back({x, e, k, y});
                x = e;
            }
        next.clear();
        size_t i = 0;
        for (Run run : row) {
            while (i < open.size() && open[i].x0 < run.x0) emit(open[i], y), ++i;
            if (i < open.size() && open[i].x0 == run.x0 && open[i].x1 == run.x1 && open[i].kind == run.kind) run.y0 = open[i].y0, ++i;
            next.push_back(run);
        }
        for (; i < open.size(); ++i) emit(open[i], y);
        std::swap(open, next);
    }
}

void makeTerrainFlagsShapes(const Editor& ed) {
    const int key = int(gTerrainFlags.view) | (gLightTheme ? 16 : 0);
    if (gTerrainFlags.shapesFor == &ed && gTerrainFlags.shapesRev == ed.terrainRevision() && gTerrainFlags.tilesetOf == &ed.tileset() &&
        gTerrainFlags.era == ed.era() && gTerrainFlags.key == key)
        return;
    gTerrainFlags.shapesFor = &ed, gTerrainFlags.shapesRev = ed.terrainRevision(), gTerrainFlags.tilesetOf = &ed.tileset();
    gTerrainFlags.era = ed.era(), gTerrainFlags.key = key;
    gTerrainFlags.fills.clear();
    const TerrainLayer& ter = ed.terrain();
    const Tileset& ts = ed.tileset();
    const int tw = ter.width, th = ter.height;
    const uint8_t a = gLightTheme ? 0x70 : 0x60;
    const uint32_t good = 0x30D05000u | a, bad = 0xE0303000u | a;
    if (gTerrainFlags.view == TerrainFlagsView::Buildable) {
        std::vector<uint8_t> kind(size_t(tw) * th, 0);
        for (int i = 0; i < tw * th; ++i) {
            const uint16_t v = ter.tiles[size_t(i)];
            const TileGroup* g = ts.groupFor(v);
            kind[size_t(i)] = g && ts.megatileFor(v) >= 0 && !(g->buildability & Tileset::kUnbuildable) ? 1 : 2;
        }
        const uint32_t colors[3] = {0, good, bad};
        terrainFlagsFills(kind, tw, th, 32, colors);
        return;
    }
    // The minitiles: their kinds from the VF4 flags.
    const int w = tw * 4, h = th * 4;
    std::vector<uint8_t> kind(size_t(w) * h, 0);
    for (int ty = 0; ty < th; ++ty)
        for (int tx = 0; tx < tw; ++tx) {
            const int mega = ts.megatileFor(ter.tiles[size_t(ty) * tw + tx]);
            const bool known = mega >= 0 && size_t(mega) < ts.minitileFlags.size();
            for (int k = 0; k < 16; ++k) {
                const uint16_t f = known ? ts.minitileFlags[size_t(mega)][size_t(k)] : 0;
                uint8_t& out = kind[size_t(ty * 4 + k / 4) * w + size_t(tx * 4 + k % 4)];
                switch (gTerrainFlags.view) {
                case TerrainFlagsView::Walkable: out = (f & Tileset::kWalkable) ? 1 : 2; break;
                case TerrainFlagsView::Height: out = !known ? 0 : (f & 4) ? 3 : (f & 2) ? 2 : 1; break;
                case TerrainFlagsView::BlocksView: out = (f & 8) ? 1 : 0; break;
                default: break;
                }
            }
        }
    if (gTerrainFlags.view == TerrainFlagsView::Height) {
        const uint32_t colors[4] = {0, 0x4080FF00u | a, 0xE8C83000u | a, 0xF0502800u | a};
        terrainFlagsFills(kind, w, h, 8, colors);
    } else if (gTerrainFlags.view == TerrainFlagsView::BlocksView) {
        const uint32_t colors[2] = {0, 0xE040F000u | uint8_t(a + 0x30)};
        terrainFlagsFills(kind, w, h, 8, colors);
    } else {
        const uint32_t colors[3] = {0, good, bad};
        terrainFlagsFills(kind, w, h, 8, colors);
    }
}

// The overlay's tint of minitile k (0-15) of tile v, as the map shows it (the Tiles list draws it over its tiles,
// GitHub #176): false where it draws nothing. `color` is a COLORREF (0x00BBGGRR), `alpha` its opacity.
bool terrainFlagsMinitile(const Tileset& ts, uint16_t v, int k, draw::Color& color, uint8_t& alpha) {
    if (gTerrainFlags.view == TerrainFlagsView::Off) return false;
    const uint8_t a = gLightTheme ? 0x70 : 0x60;
    auto rgb = [&](uint32_t rgba, uint8_t al) { color = draw::Color(((rgba >> 24) & 0xFF) | ((rgba >> 8) & 0xFF00) | ((rgba << 8) & 0xFF0000)), alpha = al; return true; };
    const int mega = ts.megatileFor(v);
    if (gTerrainFlags.view == TerrainFlagsView::Buildable) {
        const TileGroup* g = ts.groupFor(v);
        return rgb(g && mega >= 0 && !(g->buildability & Tileset::kUnbuildable) ? 0x30D05000u : 0xE0303000u, a);
    }
    const bool known = mega >= 0 && size_t(mega) < ts.minitileFlags.size();
    const uint16_t f = known ? ts.minitileFlags[size_t(mega)][size_t(k)] : 0;
    switch (gTerrainFlags.view) {
    case TerrainFlagsView::Walkable: return rgb((f & Tileset::kWalkable) ? 0x30D05000u : 0xE0303000u, a);
    case TerrainFlagsView::Height: return known && rgb((f & 4) ? 0xF0502800u : (f & 2) ? 0xE8C83000u : 0x4080FF00u, a);
    case TerrainFlagsView::BlocksView: return (f & 8) && rgb(0xE040F000u, uint8_t(a + 0x30));
    default: return false;
    }
}

void addTerrainFlagsLayer(render::Scene& s) {
    const AppState& app = appState();
    if (gTerrainFlags.view == TerrainFlagsView::Off || !app.ed || !app.ed->isOpen()) return;
    makeTerrainFlagsShapes(*app.ed);
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    for (const render::RectDraw& d : gTerrainFlags.fills)
        if (d.x1 >= vx0 && d.x0 <= vx1 && d.y1 >= vy0 && d.y0 <= vy1) s.rects.push_back(d);
}

// A view of the menu: it shows (the same view again hides the layer).
void terrainFlagsSet(TerrainFlagsView v) {
    gTerrainFlags.view = gTerrainFlags.view == v ? TerrainFlagsView::Off : v;
    gTerrainFlags.key = -1;
    shellMenusChanged();
    appState().dirty = true;
    shellPaletteRebuilt();  // (the Tiles list shows the overlay on its tiles too)
    switch (gTerrainFlags.view) {
    case TerrainFlagsView::Off: setMessage(tr(L"Terrain flags are hidden (View > Overlays)")); break;
    case TerrainFlagsView::Walkable: setMessage(tr(L"Terrain flags: walkable (green), not walkable (red)")); break;
    case TerrainFlagsView::Height: setMessage(tr(L"Terrain flags: low ground (blue), middle ground (yellow), high ground (red)")); break;
    case TerrainFlagsView::BlocksView: setMessage(tr(L"Terrain flags: the ground that blocks the line of sight (purple)")); break;
    case TerrainFlagsView::Buildable: setMessage(tr(L"Terrain flags: buildable (green), not buildable (red)")); break;
    }
}
