// The palette's pictures on this system (research/ports/MAC-BUILD-PLAN.md step 6): the terrain tiles, the doodads, the
// units' command icons and the sprites, as the Win32 palette shows them (unitPaletteBitmap in main.cpp), made from the
// same game data as RGBA pixels for a draw::Bitmap. The Win32 program keeps its own GDI bitmaps; this is the SDL copy.
// Included by sdl_main_view.inc before sdl_panels.inc, which draws the rows with them.

// The pictures' cache: they are made when a row first shows, and kept while the editor, its era and the graphics mode
// stay (the mode is 0 for SD, 1 for HD2 and 2 for Carbot, as the Win32 palette's).
struct PaletteArt {
    const Editor* ed = nullptr;
    const GameData* data = nullptr;  // the game data the HD icons came from (a Cosmonarchy tab has its own)
    int era = -1, mode = -1;
    std::vector<Sprite> hdIcons;  // HD2 (or Carbot) command icons, for the mode
    HdTiles hdTiles;              // HD2 (or Carbot) megatiles, for terrain and doodad pictures
    bool hdTilesLoaded = false;
    int hdTilesEra = -1, hdTilesMode = -1;  // (the era and mode of hdTiles: another map of that tileset uses them too)
    std::map<std::tuple<int, int, int, bool>, std::unique_ptr<draw::Bitmap>> bitmaps;  // (mode of the row, value, owner, sprite)
};
PaletteArt gPaletteArt;
// The pictures' textures go before the GPU device does (main_sdl.cpp's shutdown); a later release would use a freed device.
void releasePaletteArt() { gPaletteArt = PaletteArt{}; }

// The picture of one item (value) of a palette mode, or nullptr when it has none (the Win32 palette's blank case: the
// game's "BLANK" icon). Terrains and doodads: their tiles; units: the command icon (HD in the HD modes); sprites: their
// image (SD art, as the Win32 palette's sprites when the HD art is not there).
// `iconFrame`: the command icon of that frame (cmdicons; HD in the HD modes), for the Data workspace's entries.
// `sprite`: a unit's sprite, not its command icon (the palette's Grid: sprites view).
draw::Bitmap* paletteBitmap(Mode what, int value, int owner, bool iconFrame = false, bool sprite = false) {
    if (!gState.ed || !gState.ed->isOpen() || value < 0) return nullptr;
    Editor& ed = *gState.ed;
    const int mode = gState.carbot ? 2 : gState.quality == GraphicsQuality::SD ? 0 : 1;
    const bool unitKind = what == Mode::Units && !iconFrame;
    if (gPaletteArt.ed != &ed || gPaletteArt.era != int(ed.era()) || gPaletteArt.mode != mode) {
        gPaletteArt.bitmaps.clear();
        if (gPaletteArt.mode != mode || gPaletteArt.data != &ed.gameData()) {
            gPaletteArt.hdIcons.clear();
            gPaletteArt.data = &ed.gameData();
            std::string err;
            if (mode) loadHdCommandIcons(ed.gameData(), mode == 2, gPaletteArt.hdIcons, err);
            if (mode == 1)  // the HD icons take the SD icon's color (ticon.pcx), scaled by their gray
                if (const Sprite* sd = ed.unitSprites().commandIcon(0)) {
                    uint8_t tint[3] = {255, 255, 255};
                    int best = -1;
                    for (size_t i = 0; i + 3 < sd->rgba.size(); i += 4)
                        if (sd->rgba[i + 3] && sd->rgba[i] + sd->rgba[i + 1] + sd->rgba[i + 2] > best)
                            best = sd->rgba[i] + sd->rgba[i + 1] + sd->rgba[i + 2], std::copy_n(&sd->rgba[i], 3, tint);
                    for (Sprite& icon : gPaletteArt.hdIcons)
                        for (size_t i = 0; i + 3 < icon.rgba.size(); i += 4) {
                            const int gray = std::max({icon.rgba[i], icon.rgba[i + 1], icon.rgba[i + 2]});
                            for (int k = 0; k < 3; ++k) icon.rgba[i + k] = uint8_t(tint[k] * gray / 255);
                        }
                }
        }
        // (the HD tiles come from the game data by the era and mode only: a tab of another map of the same tileset keeps them)
        if (gPaletteArt.hdTilesEra != int(ed.era()) || gPaletteArt.hdTilesMode != mode) gPaletteArt.hdTilesLoaded = false;
        gPaletteArt.ed = &ed, gPaletteArt.era = int(ed.era()), gPaletteArt.mode = mode;
    }
    if (!unitKind) sprite = false;
    // (a unit's picture is its player's color: the key has it, so a color changed on the Players page shows)
    const auto key = std::make_tuple(iconFrame ? 100 : int(what), value, unitKind ? int(owner) << 24 | int(ed.playerRgb(owner) & 0xFFFFFF) : 0, sprite);
    if (auto it = gPaletteArt.bitmaps.find(key); it != gPaletteArt.bitmaps.end()) return it->second.get();

    // The picture: a sprite of the game data (straight alpha), cropped to its opaque pixels below.
    Sprite art;
    const Sprite* s = nullptr;
    if (iconFrame) {
        if (!mode) s = ed.unitSprites().commandIcon(uint16_t(value));
        else if (value < int(gPaletteArt.hdIcons.size()) && gPaletteArt.hdIcons[size_t(value)].width) s = &gPaletteArt.hdIcons[size_t(value)];
    } else if (!unitKind) {
        if (mode && !gPaletteArt.hdTilesLoaded) {
            std::string err;
            gPaletteArt.hdTilesLoaded = true, gPaletteArt.hdTilesEra = int(ed.era()), gPaletteArt.hdTilesMode = mode;
            if (!loadHdTiles(ed.gameData(), ed.era(), GraphicsQuality::HD2, gPaletteArt.hdTiles, err, mode == 2))
                gPaletteArt.hdTiles = {};
        }
        const HdTiles* hd = mode && !gPaletteArt.hdTiles.tiles.empty() ? &gPaletteArt.hdTiles : nullptr;
        if (what == Mode::Terrain) {
            art = terrainPicture(ed.tileset(), uint16_t(value), hd);
            // The terrain it stands on ("X (Y)": Y) as a small square at the bottom right, half the picture's height.
            uint16_t ground = 0;
            if (const Tileset::ModTerrain* m = ed.tileset().modTerrain(uint16_t(value))) ground = m->ground;
            else if (const std::string n = ed.terrainName(uint16_t(value)); n.size() > 3 && n.back() == ')') {
                const std::string y = n.substr(n.find(" (") + 2, n.size() - n.find(" (") - 3);
                const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era());
                for (uint16_t i = 1; t && i <= t->terrainCount() && !ground; ++i)
                    if (t->isTerrain(i) && ed.terrainName(i) == y) ground = i;
                if (!ground && t)  // (Y named with its own ground: "Dirt" is the main terrain)
                    for (uint16_t i = 1; i <= t->terrainCount() && !ground; ++i)
                        if (t->isTerrain(i) && ed.terrainName(i).rfind(y + " (", 0) == 0) ground = i;
            }
            if (ground && art.width && art.height >= 8) {
                const Sprite g = terrainPicture(ed.tileset(), ground, hd);
                const int side = art.height / 2, x0 = art.width - side, y0 = art.height - side;
                for (int y = 0; y < side && g.width && g.height; ++y)
                    for (int x = 0; x < side; ++x) {
                        const bool frame = x == 0 || y == 0;
                        const size_t d = (size_t(y0 + y) * art.width + x0 + x) * 4;
                        const size_t s2 = (size_t(y * g.height / art.height) * g.width + x * g.height / art.height) * 4;
                        for (int k = 0; k < 3; ++k) art.rgba[d + k] = frame ? 16 : g.rgba[s2 + k];
                        art.rgba[d + 3] = 255;
                    }
            }
        }
        else if (what == Mode::Sprites) {
            const int image = ed.unitSprites().imageForSprite(uint16_t(value));
            const Grp* g = image >= 0 ? ed.unitSprites().grp(image) : nullptr;
            if (g && g->width) {  // the sprite's first frame, in the tileset's palette (as the Win32 palette's)
                art.width = g->width, art.height = g->height, art.originX = g->width / 2, art.originY = g->height / 2;
                art.rgba.assign(size_t(art.width) * size_t(art.height) * 4, 0);
                ed.unitSprites().drawImage(art, image, 0, false, 0, ed.tileset());
            }
        } else if (const TerrainStamp* b = brushDoodad(value)) {
            std::vector<uint16_t> tiles(b->tiles.size());
            for (size_t i = 0; i < tiles.size(); ++i) tiles[i] = i < b->mask.size() && b->mask[i] ? b->tiles[i] : uint16_t(0xFFFF);
            art = tilesSprite(ed.tileset(), b->width, b->height, tiles, hd);
        } else if (const DoodadType* d = ed.doodadTypes().find(uint16_t(value))) art = doodadPicture(*d, ed.tileset(), ed.unitSprites(), hd);
        if (art.width) s = &art;
    } else {
        if (!sprite) {  // the command icon (a sprite view: the unit's picture below)
            if (!mode) s = ed.unitSprites().commandIcon(uint16_t(value));
            else if (value < int(gPaletteArt.hdIcons.size()) && gPaletteArt.hdIcons[size_t(value)].width) s = &gPaletteArt.hdIcons[size_t(value)];
        }
        if (!s && mode) {  // no icon: the unit's picture of the HD art
            std::string err;
            if (hdUnitPicture(ed.gameData(), ed.unitSprites(), uint16_t(value), ed.playerRgb(owner), mode == 2, art, err) && art.width) s = &art;
        }
        if (!s) s = ed.unitSprites().unitSprite(uint16_t(value), uint8_t(owner), ed.tileset());
    }
    // Nothing to show (no icon, no visible sprite): the game's blank icon, for units only.
    auto visible = [](const Sprite* p) {  // at least a few pixels across: a stray dot is not a picture
        if (!p) return false;
        int x0 = p->width, y0 = p->height, x1 = -1, y1 = -1;
        for (int y = 0; y < p->height; ++y)
            for (int x = 0; x < p->width; ++x)
                if (p->rgba[(size_t(y) * p->width + x) * 4 + 3]) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        return x1 - x0 >= 3 && y1 - y0 >= 3;
    };
    if (unitKind && !visible(s)) s = ed.unitSprites().blankIcon();
    // Nothing to draw: that is cached too (an empty entry), so the row does not build the picture again on every paint.
    if (!s) return gPaletteArt.bitmaps.emplace(key, nullptr), nullptr;

    // The opaque part, premultiplied: 0xAABBGGRR per pixel, rows from the top (draw::Bitmap's).
    int x0 = s->width, y0 = s->height, x1 = -1, y1 = -1;
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x)
            if (s->rgba[(size_t(y) * s->width + x) * 4 + 3]) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    if (x1 < 0) return gPaletteArt.bitmaps.emplace(key, nullptr), nullptr;
    auto b = std::make_unique<draw::Bitmap>(x1 - x0 + 1, y1 - y0 + 1);
    uint32_t* px = b->pixels();
    for (int y = 0; y < b->height(); ++y)
        for (int x = 0; x < b->width(); ++x) {
            const uint8_t* p = &s->rgba[(size_t(y0 + y) * s->width + x0 + x) * 4];
            const uint32_t a = p[3];
            const uint32_t r = p[0] * a / 255, g = p[1] * a / 255, bl = p[2] * a / 255;
            px[size_t(y) * b->width() + x] = r | (g << 8) | (bl << 16) | (a << 24);
        }
    b->changed();
    return (gPaletteArt.bitmaps[key] = std::move(b)).get();
}

// Draws a palette picture centered in `box`, shrunk to fit (enlarged at most to the display's scale), as the Win32
// palette's drawFitted.
void drawPaletteArt(draw::Canvas& c, draw::Bitmap& b, draw::Rect box) {
    const int bw = box.width(), bh = box.height();
    if (bw <= 0 || bh <= 0 || b.width() <= 0 || b.height() <= 0) return;
    const float k = std::min({float(bw) / b.width(), float(bh) / b.height(), float(gDpi) / 96.0f});
    const int w = std::max(1, int(b.width() * k)), h = std::max(1, int(b.height() * k));
    const int x = box.left + (bw - w) / 2, y = box.top + (bh - h) / 2;
    c.bitmap(b, draw::Rect(0, 0, b.width(), b.height()), draw::Rect(x, y, x + w, y + h), true);
}
