// View > Overlays > Fog of war of the selected player (roadmap R32; portable, core.inc): what the player of the Units
// palette (Player 1 to 8) sees when the game starts. The map is dark where none of the player's units sees, nor the
// units of the players of its force when the force has Shared vision (Scenario > Players and forces). Each unit sees
// the tiles of its sight mask (units.dat Sight Range: the project's value, else the game's); a ground unit does not see
// a tile higher than the ground it stands on, as in the game; a flying unit sees all of its tiles. The start locations
// see nothing (the game puts a town hall there in melee only).

bool gShowFog = false;

struct FogLayer {
    std::vector<render::RectDraw> fills;
    const Editor* shapesFor = nullptr;
    uint64_t shapesRev = 0;
    int player = -1, key = -1;
};
FogLayer gFog;

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

void makeFogShapes(const Editor& ed, int player) {
    const int key = gLightTheme ? 1 : 0;
    if (gFog.shapesFor == &ed && gFog.shapesRev == ed.revision() && gFog.player == player && gFog.key == key) return;
    gFog.shapesFor = &ed, gFog.shapesRev = ed.revision(), gFog.player = player, gFog.key = key;
    gFog.fills.clear();
    const int w = ed.width(), h = ed.height();
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    // The players whose units the player sees with: itself, and its force when the force shares vision.
    bool with[12] = {};
    with[player] = true;
    if (player < 8) {
        const int f = ed.player(player).force;
        if (f >= 0 && f < 4 && (ed.force(f).flags & Editor::SharedVision))
            for (int p = 0; p < 8; ++p) with[p] |= ed.player(p).force == f;
    }
    const DatTable* units = rangeTable("units");
    const DatField* sight = units ? units->field("Sight Range") : nullptr;
    std::vector<int> levels(size_t(w) * h);
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) levels[size_t(ty) * w + tx] = fogTileLevel(ed, tx, ty);
    for (const Unit& u : ed.units()) {
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
                seen[size_t(Y) * w + X] = 1;
            }
    }
    // The dark: runs of unseen tiles on each row.
    const uint32_t dark = gLightTheme ? 0x101018A8u : 0x000000B8u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w;) {
            if (seen[size_t(y) * w + x]) {
                ++x;
                continue;
            }
            int e = x + 1;
            while (e < w && !seen[size_t(y) * w + e]) ++e;
            render::RectDraw d;
            d.x0 = x * 32.0f, d.y0 = y * 32.0f, d.x1 = e * 32.0f, d.y1 = y * 32.0f + 32, d.rgba = dark, d.fill = true;
            gFog.fills.push_back(d);
            x = e;
        }
}

void addFogLayer(render::Scene& s) {
    const AppState& app = appState();
    if (!gShowFog || !app.ed || !app.ed->isOpen()) return;
    makeFogShapes(*app.ed, std::clamp(app.playerOwner, 0, 11));
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
