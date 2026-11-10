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
    int rulesRev = 0;  // (gTerrainRulesRev of the pictures: terrain rules loaded again draw the terrains again)
    std::vector<Sprite> hdIcons;  // HD2 (or Carbot) command icons, for the mode
    std::future<std::vector<Sprite>> hdIconsReading;  // (read on a worker: a map's first frame does not wait for them)
    std::shared_ptr<const HdTiles> hdTiles;  // HD2 (or Carbot) megatiles, for terrain and doodad pictures (sharedHdTiles)
    bool hdTilesLoaded = false;
    int hdTilesEra = -1, hdTilesMode = -1;  // (the era and mode of hdTiles: another map of that tileset uses them too)
    std::map<std::tuple<int, int, int, bool>, std::unique_ptr<draw::Bitmap>> bitmaps;  // (mode of the row, value, owner, sprite)
    std::map<std::pair<uint16_t, int>, std::unique_ptr<draw::Bitmap>> pieBitmaps;  // the pie palette's terrains (pieBitmap)
    std::set<const draw::Bitmap*> blanks;  // the pictures that are the game's placeholder icon (drawPaletteArt: a neutral icon)
    uint32_t iconTint = 0;  // the icons' color the pictures have: 0 the game's; else 0x1000000 | the accent (0x00BBGGRR)
    // The HD pictures of units with no command icon (hdUnitPicture: 5 to 15 ms each), made on workers while a palette
    // paints; by the key of `bitmaps`.
    std::map<std::tuple<int, int, int, bool>, std::future<Sprite>> unitPictures;
    void waitUnitPictures() {
        for (auto& [k, f] : unitPictures)
            if (f.valid()) f.wait();
        unitPictures.clear();
    }
};
PaletteArt gPaletteArt;

// Program options > Appearance > Unit icons: in the accent color (the setting TintIcons; not saved in a test).
bool& tintIconsFlag() {
    static bool on = [] {
        bool v = false;
        if (!gSelfTest && !gSettingsOff) prefs::flag(L"TintIcons", v);
        return v;
    }();
    return on;
}
bool tintIconsOn() { return tintIconsFlag(); }
void setTintIcons(bool on) {
    tintIconsFlag() = on;
    if (!gSelfTest && !gSettingsOff) prefs::setNumber(L"TintIcons", on ? 1 : 0);
}
// The pictures' textures go before the GPU device does (main_sdl.cpp's shutdown); a later release would use a freed device.
void releasePaletteArt() { gPaletteArt.waitUnitPictures(), gPaletteArt = PaletteArt{}; }
// No map is open: the HD icons and tiles go (the pictures' textures stay until the next map's: the GPU may still draw
// them this frame). The next map loads them again.
void releasePaletteArtData() {
    gPaletteArt.ed = nullptr, gPaletteArt.data = nullptr, gPaletteArt.mode = -1;
    gPaletteArt.waitUnitPictures();  // (they read the game data: before it goes)
    if (gPaletteArt.hdIconsReading.valid()) gPaletteArt.hdIconsReading.wait();
    gPaletteArt.hdIconsReading = {};
    gPaletteArt.hdIcons.clear(), gPaletteArt.hdIcons.shrink_to_fit();
    gPaletteArt.hdTiles.reset(), gPaletteArt.hdTilesLoaded = false, gPaletteArt.hdTilesEra = gPaletteArt.hdTilesMode = -1;
    gTilesArt.hdByData.clear();
    releaseSharedHdTiles();
}

// The terrain that terrain `value` stands on ("X (Y)": Y; a mod terrain's ground), or 0: the palette's picture shows it
// as a square at its bottom right, the pie palette as the end of the slice.
uint16_t terrainGround(const Editor& ed, uint16_t value) {
    if (const Tileset::ModTerrain* m = ed.tileset().modTerrain(value)) return m->ground;
    const std::string n = ed.terrainName(value);
    if (n.size() <= 3 || n.back() != ')' || n.find(" (") == std::string::npos) return 0;
    const std::string y = n.substr(n.find(" (") + 2, n.size() - n.find(" (") - 3);
    const IsomTerrainTable* t = IsomTerrainTable::forEra(ed.era());
    for (uint16_t i = 1; t && i <= t->terrainCount(); ++i)
        if (t->isTerrain(i) && ed.terrainName(i) == y) return i;
    for (uint16_t i = 1; t && i <= t->terrainCount(); ++i)  // (Y named with its own ground: "Dirt" is the main terrain)
        if (t->isTerrain(i) && ed.terrainName(i).rfind(y + " (", 0) == 0) return i;
    return 0;
}

// A palette's paint makes at most kPaletteArtBudgetMs of new pictures (a map's first frame had all of them: 190 ms);
// the others come in the next frames (the view paints again; the snapshots' frames wait for them, as for the sprites).
constexpr double kPaletteArtBudgetMs = 8;
std::chrono::steady_clock::time_point gPaletteArtDeadline{};  // (set only while a palette paints)
int gPaletteArtLeft = 0;                                       // pictures left for a next frame

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
    // (the icons' color: the game's yellow, or the accent: its pictures again when it changes)
    if (const uint32_t tint = tintIconsOn() ? (MutedAccent::themeAccent() & 0xFFFFFF) | 0x1000000 : 0; tint != gPaletteArt.iconTint) {
        gPaletteArt.waitUnitPictures();
        gPaletteArt.bitmaps.clear(), gPaletteArt.blanks.clear(), gPaletteArt.iconTint = tint;
    }
    if (gPaletteArt.ed != &ed || gPaletteArt.era != int(ed.era()) || gPaletteArt.mode != mode || gPaletteArt.rulesRev != gTerrainRulesRev) {
        gPaletteArt.waitUnitPictures();  // (of the map before, or another mode)
        gPaletteArt.bitmaps.clear(), gPaletteArt.pieBitmaps.clear(), gPaletteArt.blanks.clear(), gPaletteArt.rulesRev = gTerrainRulesRev;
        if (gPaletteArt.mode != mode || gPaletteArt.data != &ed.gameData()) {
            if (gPaletteArt.hdIconsReading.valid()) gPaletteArt.hdIconsReading.wait();
            gPaletteArt.hdIconsReading = {};
            gPaletteArt.hdIcons.clear();
            gPaletteArt.data = &ed.gameData();
            // the HD icons take the SD icon's color (ticon.pcx), scaled by their gray (HD; Carbot's keep theirs)
            bool tinted = false;
            uint8_t tint[3] = {255, 255, 255};
            if (mode == 1)
                if (const Sprite* sd = ed.unitSprites().commandIcon(0)) {
                    int best = -1;
                    tinted = true;
                    for (size_t i = 0; i + 3 < sd->rgba.size(); i += 4)
                        if (sd->rgba[i + 3] && sd->rgba[i] + sd->rgba[i + 1] + sd->rgba[i + 2] > best)
                            best = sd->rgba[i] + sd->rgba[i + 1] + sd->rgba[i + 2], std::copy_n(&sd->rgba[i], 3, tint);
                }
            if (mode)  // (read on a worker through the game data's handle for workers)
                gPaletteArt.hdIconsReading = std::async(std::launch::async, [data = &ed.gameData(), carbot = mode == 2, tinted, t0 = tint[0], t1 = tint[1], t2 = tint[2]] {
                    std::vector<Sprite> icons;
                    std::string err;
                    loadHdCommandIcons(*data, carbot, icons, err, true);
                    const uint8_t tint[3] = {t0, t1, t2};
                    if (tinted)
                        for (Sprite& icon : icons)
                            for (size_t i = 0; i + 3 < icon.rgba.size(); i += 4) {
                                const int gray = std::max({icon.rgba[i], icon.rgba[i + 1], icon.rgba[i + 2]});
                                for (int k = 0; k < 3; ++k) icon.rgba[i + k] = uint8_t(tint[k] * gray / 255);
                            }
                    return icons;
                });
        }
        // (the HD tiles come from the game data by the era and mode only: a tab of another map of the same tileset keeps them)
        if (gPaletteArt.hdTilesEra != int(ed.era()) || gPaletteArt.hdTilesMode != mode) gPaletteArt.hdTilesLoaded = false;
        gPaletteArt.ed = &ed, gPaletteArt.era = int(ed.era()), gPaletteArt.mode = mode;
    }
    if (!unitKind) sprite = false;
    if (gPaletteArt.hdIconsReading.valid()) {  // (the HD icons: there now, or a next frame's)
        const bool inPalette = gPaletteArtDeadline != std::chrono::steady_clock::time_point{};  // (another view: it waits for them)
        if (!inPalette || gPaletteArt.hdIconsReading.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            gPaletteArt.hdIcons = gPaletteArt.hdIconsReading.get();
        else if (iconFrame || (unitKind && !sprite)) return ++gPaletteArtLeft, nullptr;
    }
    // (a unit's picture is its player's color: the key has it, so a color changed on the Players page shows; players 1
    // to 8: the colors kept for the map's revision, core/map_scene.inc playerRgba)
    const uint32_t ownerRgb = !unitKind ? 0 : owner >= 0 && owner < 8 ? playerRgba(owner, 0) >> 8 : ed.playerRgb(owner);
    const auto key = std::make_tuple(iconFrame ? 100 : int(what), value, unitKind ? int(owner) << 24 | int(ownerRgb & 0xFFFFFF) : 0, sprite);
    if (auto it = gPaletteArt.bitmaps.find(key); it != gPaletteArt.bitmaps.end()) return it->second.get();
    if (gPaletteArtDeadline != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() > gPaletteArtDeadline)
        return ++gPaletteArtLeft, nullptr;  // (the paint's time for new pictures is over: a next frame)

    // The picture: a sprite of the game data (straight alpha), cropped to its opaque pixels below.
    Sprite art;
    const Sprite* s = nullptr;
    if (iconFrame) {
        if (!mode) s = ed.unitSprites().commandIcon(uint16_t(value));
        else if (value < int(gPaletteArt.hdIcons.size()) && gPaletteArt.hdIcons[size_t(value)].width) s = &gPaletteArt.hdIcons[size_t(value)];
        if (!s) s = ed.unitSprites().blankIcon();  // (a frame with no picture: the game's BLANK, as the game shows it)
    } else if (!unitKind) {
        if (mode && !gPaletteArt.hdTilesLoaded) {
            std::string err;
            bool busy = false;  // (the renderer's worker reads them now: this picture in a next frame, no wait)
            auto tiles = sharedHdTiles(ed.gameData(), ed.era(), GraphicsQuality::HD2, mode == 2, &err, &busy);
            if (busy) return ++gPaletteArtLeft, nullptr;
            gPaletteArt.hdTilesLoaded = true, gPaletteArt.hdTilesEra = int(ed.era()), gPaletteArt.hdTilesMode = mode;
            gPaletteArt.hdTiles = std::move(tiles);
        }
        const HdTiles* hd = mode && gPaletteArt.hdTiles && !gPaletteArt.hdTiles->tiles.empty() ? gPaletteArt.hdTiles.get() : nullptr;
        if (what == Mode::Terrain) {
            art = terrainPicture(ed.tileset(), uint16_t(value), hd);
            // The terrain it stands on ("X (Y)": Y) as a small square at the bottom right, half the picture's height.
            const uint16_t ground = terrainGround(ed, uint16_t(value));
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
            int frame = 0;
            bool flip = false;
            const int image = ed.unitSprites().mapSpriteImage(uint16_t(value), false, ed.era(), frame, flip);
            const Grp* g = image >= 0 ? ed.unitSprites().grp(image) : nullptr;
            if (g && g->width) {  // the sprite as on the map, in the tileset's palette (as the Win32 palette's)
                art.width = g->width, art.height = g->height, art.originX = g->width / 2, art.originY = g->height / 2;
                art.rgba.assign(size_t(art.width) * size_t(art.height) * 4, 0);
                ed.unitSprites().drawImage(art, image, frame, flip, 0, ed.tileset());
            }
        } else if (const DoodadType* d = ed.doodadTypes().find(uint16_t(value))) art = doodadPicture(*d, ed.tileset(), ed.unitSprites(), hd);
        if (art.width) s = &art;
    } else {
        if (!sprite && !ed.unitSprites().hasBlankIcon(uint16_t(value))) {  // the command icon (a sprite view, or the game's placeholder: the unit's picture below)
            if (!mode) s = ed.unitSprites().unitIcon(uint16_t(value));
            else if (value < int(gPaletteArt.hdIcons.size()) && gPaletteArt.hdIcons[size_t(value)].width) s = &gPaletteArt.hdIcons[size_t(value)];
        }
        if (!s && mode) {  // no icon: the unit's picture of the HD art (a palette's paint: made on a worker, shown a next frame)
            const bool inPalette = gPaletteArtDeadline != std::chrono::steady_clock::time_point{};
            auto it = gPaletteArt.unitPictures.find(key);
            if (it == gPaletteArt.unitPictures.end()) {
                // (4 workers at most: the pictures still being made; made ones wait for their next paint, as a scroll leaves them)
                int busy = 0;
                for (auto& [k, f] : gPaletteArt.unitPictures) busy += f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
                if (inPalette && busy >= 4) return ++gPaletteArtLeft, nullptr;
                const HdUnitPictureJob job = hdUnitPictureJob(ed.unitSprites(), uint16_t(value));  // (the sprites: here, on this thread)
                it = gPaletteArt.unitPictures.emplace(key, std::async(std::launch::async, [data = &ed.gameData(), job, rgb = ed.playerRgb(owner), carbot = mode == 2] {
                    Sprite out;
                    std::string err;
                    if (!hdUnitPictureFrom(*data, job, rgb, carbot, out, err, true)) out = {};
                    return out;
                })).first;
            }
            if (inPalette && it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return ++gPaletteArtLeft, nullptr;
            art = it->second.get();  // (another view waits for it)
            gPaletteArt.unitPictures.erase(it);
            if (art.width) s = &art;
        }
        if (!s) s = ed.unitSprites().unitSprite(uint16_t(value), uint8_t(owner), ed.tileset());
        // The icons' view with no icon of the unit's own: its picture as an icon is drawn: in the icons' color (their
        // brightest color, scaled by the picture's gray: as the HD icons take the SD icons'), without its shadow (the
        // see-through pixels; an air unit's shadow is far under it: the crop below then has the unit alone).
        if (!sprite && s && s != ed.unitSprites().unitIcon(uint16_t(value)) &&
            !(value < int(gPaletteArt.hdIcons.size()) && s == &gPaletteArt.hdIcons[size_t(value)])) {
            static uint8_t tint[3] = {0, 0, 0};
            if (!tint[0] && !tint[1] && !tint[2])
                if (const Sprite* sd = ed.unitSprites().commandIcon(0)) {
                    int best = -1;
                    for (size_t i = 0; i + 3 < sd->rgba.size(); i += 4)
                        if (sd->rgba[i + 3] && sd->rgba[i] + sd->rgba[i + 1] + sd->rgba[i + 2] > best)
                            best = sd->rgba[i] + sd->rgba[i + 1] + sd->rgba[i + 2], std::copy_n(&sd->rgba[i], 3, tint);
                }
            if (s != &art) art = *s;
            for (size_t i = 0; i + 3 < art.rgba.size(); i += 4) {
                uint8_t* px = &art.rgba[i];
                const int gray = std::max({px[0], px[1], px[2]});
                if (px[3] < 200 && gray < 96) {  // (a shadow: dark and see-through; not in an icon)
                    px[0] = px[1] = px[2] = px[3] = 0;
                    continue;
                }
                for (int k = 0; k < 3; ++k) px[k] = uint8_t((tint[k] ? tint[k] : 255) * gray / 255);
            }
            s = &art;
        }
    }
    // The icons in the accent color (Program options > Appearance > Unit icons): each pixel gets the accent's hue and
    // keeps its own saturation and brightness (a hue shift: the icon stays as light as the game's).
    if (gPaletteArt.iconTint && s && (iconFrame || (unitKind && !sprite))) {
        if (s != &art) art = *s;
        const float ar = float(gPaletteArt.iconTint & 255), ag = float(gPaletteArt.iconTint >> 8 & 255), ab = float(gPaletteArt.iconTint >> 16 & 255);
        const float amax = std::max({ar, ag, ab}), amin = std::min({ar, ag, ab});
        // (the accent's color at full saturation and brightness: 0..1 in each channel)
        const float span = std::max(amax - amin, 1.0f), hr = (ar - amin) / span, hg = (ag - amin) / span, hb = (ab - amin) / span;
        for (size_t i = 0; i + 3 < art.rgba.size(); i += 4) {
            uint8_t* px = &art.rgba[i];
            const float v = float(std::max({px[0], px[1], px[2]})), lo = float(std::min({px[0], px[1], px[2]}));
            const float c = v - lo;  // (the chroma: v at full saturation, 0 for a gray)
            px[0] = uint8_t(std::lround(lo + c * hr)), px[1] = uint8_t(std::lround(lo + c * hg)), px[2] = uint8_t(std::lround(lo + c * hb));
        }
        s = &art;
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
    bool blank = false;
    if (unitKind && !visible(s)) s = ed.unitSprites().blankIcon(), blank = true;
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
    if (blank) gPaletteArt.blanks.insert(b.get());
    return (gPaletteArt.bitmaps[key] = std::move(b)).get();
}

// The pie palette's picture of a terrain: tiles x tiles SD tiles (8 x 8: 256 x 256 pixels; more for a big ring), with no
// ground square; the ring shows every slice at the same size, 32 pixels a tile at 100 % (core/views_palette.inc:
// PieView). nullptr: the terrain has none.
draw::Bitmap* pieBitmap(uint16_t terrain, int tiles) {
    if (!gState.ed || !gState.ed->isOpen()) return nullptr;
    paletteBitmap(Mode::Terrain, terrain, 0);  // (the cache follows this editor, era and mode)
    const std::pair<uint16_t, int> key{terrain, tiles};
    if (const auto it = gPaletteArt.pieBitmaps.find(key); it != gPaletteArt.pieBitmaps.end()) return it->second.get();
    const Sprite s = terrainPicture(gState.ed->tileset(), terrain, nullptr, tiles, tiles);
    std::unique_ptr<draw::Bitmap> b;
    if (s.width > 0 && s.height > 0) {
        b = std::make_unique<draw::Bitmap>(s.width, s.height);
        uint32_t* px = b->pixels();
        for (size_t i = 0; i < size_t(s.width) * size_t(s.height); ++i) {
            const uint8_t* p = &s.rgba[i * 4];
            px[i] = p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | 0xFF000000u;  // (opaque: the tiles)
        }
        b->changed();
    }
    return (gPaletteArt.pieBitmaps[key] = std::move(b)).get();
}

// Draws a palette picture centered in `box`, shrunk to fit (enlarged at most to the display's scale), as the Win32
// palette's drawFitted.
void drawPaletteArt(draw::Canvas& c, draw::Bitmap& b, draw::Rect box) {
    // (a unit with no picture of its own, the game's yellow "BLANK": a neutral icon of the set, dim)
    if (gPaletteArt.blanks.count(&b)) return c.icon(box, int(UiIcon::Units), kFaint, std::min(scale(32), std::max(scale(16), std::min(box.width(), box.height()) * 2 / 3)));
    const int bw = box.width(), bh = box.height();
    if (bw <= 0 || bh <= 0 || b.width() <= 0 || b.height() <= 0) return;
    const float k = std::min({float(bw) / b.width(), float(bh) / b.height(), float(gDpi) / 96.0f});
    const int w = std::max(1, int(b.width() * k)), h = std::max(1, int(b.height() * k));
    const int x = box.left + (bw - w) / 2, y = box.top + (bh - h) / 2;
    c.bitmap(b, draw::Rect(0, 0, b.width(), b.height()), draw::Rect(x, y, x + w, y + h), true);
}
