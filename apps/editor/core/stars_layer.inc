// The stars behind space (portable, core.inc; View > Graphics > Stars in space): Space Platform maps show the game's
// parallax stars where the ground is space, as the game does (1.16.1 draws them over each pixel of the screen that is
// color 0; here, over each tile that is all space: megatile 0). The stars are the game's (hd_assets.h: StarField):
// five layers, each a tile of stars that repeats; each layer moves with the view at its own speed (the farther, the
// slower), so they seem to be at different depths. The stars keep their size on the screen at each zoom, as in the
// game. Each layer's pictures are an extra image of the renderer ("parallax\star#<layer>"), each star a sprite on the
// terrain (under every other sprite).

bool gShowStars = true;

// The stars of the art that shows (read once for each quality and skin).
const StarField* starField(const Editor& ed) {
    struct Cache {
        bool made = false, ok = false, carbot = false;
        GraphicsQuality quality = GraphicsQuality::HD2;
        const GameData* data = nullptr;
        StarField field;
    };
    static Cache c;
    const AppState& app = appState();
    if (!c.made || c.quality != app.quality || c.carbot != app.carbot || c.data != &ed.gameData()) {
        c.made = true, c.quality = app.quality, c.carbot = app.carbot, c.data = &ed.gameData();
        std::string err;
        c.ok = readStarField(ed.gameData(), app.quality, app.carbot, c.field, err) && !c.field.layers.empty();
    }
    return c.ok ? &c.field : nullptr;
}

// The stars over the space tiles in the view (map_scene.inc: before the map's sprites).
void addStarsLayer(Editor& ed, render::Scene& s) {
    constexpr uint16_t kPlatform = 1;  // (the era of Space Platform)
    if (!gShowStars || (ed.era() & 7) != kPlatform) return;
    const StarField* f = starField(ed);
    if (!f) return;
    const AppState& app = appState();
    int vw = 0, vh = 0;
    viewSize(vw, vh);
    if (vw <= 0 || vh <= 0 || app.zoom <= 0) return;
    const TerrainLayer& terrain = ed.terrain();
    const Tileset& ts = ed.tileset();
    auto space = [&](float wx, float wy) {  // (a tile that is all space: megatile 0)
        if (wx < 0 || wy < 0) return false;
        const int tx = int(wx / 32), ty = int(wy / 32);
        if (tx >= terrain.width || ty >= terrain.height) return false;
        const uint16_t v = terrain.tiles[size_t(ty) * terrain.width + size_t(tx)];
        const TileGroup* g = ts.groupFor(v);
        return g && g->megatiles[v & 15] == 0;
    };
    const float px = float(f->pxPerMap), inv = 1.0f / app.zoom;
    const int n = int(f->layers.size());
    // The game draws the layers in the file's order reversed: the last is the farthest (the slowest), the first is on top.
    for (int k = n - 1; k >= 0; --k) {
        const StarField::Layer& L = f->layers[size_t(k)];
        const float speed = 0.12f * float(n - k);  // (screen pixels the layer moves for each pixel of the view)
        const float tw = float(L.tileW) / px, th = float(L.tileH) / px;  // the tile on the screen (pixels)
        const float ox = std::fmod(app.camX * app.zoom * speed, tw), oy = std::fmod(app.camY * app.zoom * speed, th);
        const int image = ed.unitSprites().extraImage("parallax\\star#" + std::to_string(k));
        for (float ty = -oy - th; ty < float(vh); ty += th)
            for (float tx = -ox - tw; tx < float(vw); tx += tw)
                for (const StarField::Star& st : L.stars) {
                    const float sx = tx + float(st.x) / px, sy = ty + float(st.y) / px;
                    if (sx < -32 || sy < -32 || sx > float(vw) + 32 || sy > float(vh) + 32) continue;
                    const float wx = app.camX + sx * inv, wy = app.camY + sy * inv;
                    if (!space(wx, wy)) continue;
                    render::SpriteDraw d;
                    d.image = image, d.frame = st.frame, d.x = wx, d.y = wy, d.terrain = true;
                    d.scaleX = d.scaleY = inv, d.pivotX = wx, d.pivotY = wy;  // (its size on the screen, at each zoom)
                    s.sprites.push_back(d);
                }
    }
}
