// The map's scene (portable, core.inc; moved from main.cpp's buildScene and tiles_view.inc): what the renderer draws on
// the terrain each frame. The grid (View > Show grid, Grid size), the pathing overlays, the doodads' overlays and the
// sprites (View > Layers), the units with their overlays, shadows, add-on connectors, turrets and idle animations (the
// game's ticks: advanceAnims), a moved selection in red where it does not fit, the locations with their names, the
// selection circles, the terrain selection, the ranges (View > Show ranges), the symmetry axes, the paste ghosts, the
// unit, sprite or doodad to place (the doodad's tiles: ghostTiles, the renderer's terrain preview), the Rectangle mode's
// tiles, the brush and the spray area. Both programs draw buildScene() (main.cpp: renderNow; sdl_main_view.inc:
// drawMapFrame). The map unit graphics of a project's edits (syncMapUnitGraphics) and the layers' terrain
// (terrainWithLayers) are here too.

void dataLoadForProject();
void applyProjectGraphics(UnitSprites& sp);  // the project's graphics edits on `sp` (core/project_build.inc)
int gUnitGraphics = 0;  // changes when syncMapUnitGraphics gives the map other unit graphics

// View > Layers: the terrain the renderer gets when a layer is off: no terrain (black), or the terrain without the
// doodads' tiles (StarEdit's copy, TILE); with the start creep's tiles (Creep: core/creep_layer.inc).
TerrainLayer terrainWithLayers(const Editor& ed) {
    TerrainLayer t = !gLayers.terrain || gLayers.doodads ? ed.terrain()
                                                         : readTerrain(ed.chk(), ed.chk().has("TILE") ? "TILE" : "MTXM");
    if (!gLayers.terrain) std::fill(t.tiles.begin(), t.tiles.end(), uint16_t(0));
    addCreepTiles(ed, t);
    return t;
}

// After a frame of the map: a change of the map gives the title its mark (and the Locations list its rows), and the
// status bar its texts (the zoom, the selection).
void updateTitle();
void rebuildList(AppState& app);
void updateStatus();
void mapFrameDone(AppState& app) {
    if (!app.ed) return;
    if (app.ed->revision() != app.editorRev) {
        app.editorRev = app.ed->revision();
        updateTitle();
        if (app.mode == Mode::Locations) rebuildList(app);
        else shellOtherLocationRows();  // (a side's list of the locations)
    }
    shellLocationListSync();  // (the location list shows the map's selected locations)
    updateStatus();
}

// The map font's size on the screen: 1.5 px a unit at 96 dpi, more on a screen of more pixels (a Retina display: 2x)
// and at a larger UI scale, so the names on the map read as large as the program's other text.
float mapTextScale() { return 1.5f * float(shellDpi()) / 96.0f; }

// A location's name in lines that fit in w x h screen pixels (the map font, mapTextScale px a unit, lines 12 units
// apart): words go to the next line; a word too long is cut; lines that do not fit are left out.
std::string wrapLocationName(const std::string& name, float w, float h) {
    const float unit = mapTextScale();
    const int maxW = std::max(1, int(w / unit)), maxLines = std::max(1, int(h / (12 * unit)));
    std::vector<std::string> lines(1);
    std::istringstream in(name);
    for (std::string word; in >> word;) {
        while (render::textWidth(word) > maxW && word.size() > 1) word.pop_back();  // a word that does not fit: cut
        const std::string tryLine = lines.back().empty() ? word : lines.back() + " " + word;
        if (render::textWidth(tryLine) <= maxW || lines.back().empty()) lines.back() = tryLine;
        else lines.push_back(word);
    }
    if (int(lines.size()) > maxLines) lines.resize(size_t(maxLines));
    std::string out;
    for (size_t k = 0; k < lines.size(); ++k) out += (k ? "\n" : "") + lines[k];
    return out;
}

// A location's name as the map shows it: in lines that fit in w x h screen pixels with Options > Locations > Wrap names
// (wrapLocationName), and its size in screen pixels. Kept for each location until its name, its size or the map font's
// size changes (the wrap measures each word).
struct LocationLabel {
    std::string raw;  // (the name as the map has it)
    bool wrap = false;
    float w = 0, h = 0, unit = 0;
    std::string text;
    float width = 0, height = 0;
};
const LocationLabel& locationLabel(int i, const std::string& raw, bool wrap, float w, float h) {
    static std::vector<LocationLabel> labels;
    if (i < 0) i = 0;
    if (labels.size() <= size_t(i)) labels.resize(size_t(i) + 1);
    LocationLabel& l = labels[size_t(i)];
    const float unit = mapTextScale();
    if (!l.raw.empty() && l.raw == raw && l.wrap == wrap && l.unit == unit && (!wrap || (l.w == w && l.h == h))) return l;
    l.raw = raw, l.wrap = wrap, l.w = w, l.h = h, l.unit = unit;
    l.text = narrow(widen(raw));
    if (wrap) l.text = wrapLocationName(l.text, w, h);
    int widest = 0, lines = 0;
    for (size_t from = 0;; ++lines) {
        const size_t to = l.text.find('\n', from);
        widest = std::max(widest, render::textWidth(l.text.substr(from, to == std::string::npos ? std::string::npos : to - from)));
        if (to == std::string::npos) break;
        from = to + 1;
    }
    l.width = float(widest) * unit, l.height = float(lines + 1) * 12 * unit;
    return l;
}

uint32_t playerRgba(int owner, uint8_t alpha) {
    AppState& app = shellState();
    // Players 1-8 in the map's own colors (Map properties > Players and forces).
    if (app.ed && app.ed->isOpen() && owner >= 0 && owner < 8) {
        static const void* cachedFor = nullptr;
        static uint64_t cachedRev = ~0ull;
        static uint32_t cached[8] = {};
        if (cachedFor != app.ed.get() || cachedRev != uint64_t(app.ed->revision())) {
            for (int p = 0; p < 8; ++p) cached[p] = app.ed->playerRgb(p);
            cachedFor = app.ed.get(), cachedRev = uint64_t(app.ed->revision());
        }
        return cached[owner] << 8 | alpha;
    }
    static const uint32_t rgb[12] = {0xF40404, 0x0C48CC, 0x2CB494, 0x88409C, 0xF88C14, 0x703014,
                                     0xCCE0D0, 0xFCFC38, 0x088008, 0xFCFC7C, 0xECC4B0, 0x4068D4};
    return (rgb[std::clamp(owner, 0, 11)] << 8) | alpha;
}

// One image plus the overlays its iscript puts underneath it (shadows).
void addImage(render::Scene& s, const render::SpriteDraw& in) {
    AppState& app = shellState();
    UnitSprites& sp = app.ed->unitSprites();
    // The image's own draw function (images.dat, also as the project's .dat edits set it): the renderer draws it as the
    // game does (a shadow, a glow, a cloak that bends what is under it, a hallucination...).
    render::SpriteDraw d = in;
    const int df = sp.drawFunction(d.image);
    d.drawFunction = df;
    if (df == 10) d.shadow = true, d.air = false;
    else if (df == 9) d.glow = true;
    s.sprites.push_back(d);
    // Overlays its script puts on top (glows, lights): their own resting frame, or the parent's facing.
    for (const ImageScript::Overlay& o : sp.script(d.image).over) {
        render::SpriteDraw ov = d;
        ov.image = o.image;
        // Overlays that turn face as the parent does, or as their Init turns them (setfldirect); others show their own
        // resting frame.
        const bool turns = sp.turns(o.image);
        // (the parent's direction from its frame in its facing set: 17 frames, the left side mirrored, or 32)
        const int pset = sp.facingSet(d.image), idx = d.frame % pset;
        int dir = pset == 32 || !d.flip ? idx : 32 - idx;
        if (sp.script(o.image).direction >= 0) dir = sp.script(o.image).direction;
        const int oset = sp.facingSet(o.image), frame = sp.script(o.image).initFrame;
        bool oflip = false;
        ov.frame = turns ? frame / oset * oset + sp.facingIndex(o.image, dir, oflip) : frame;
        ov.flip = turns && oflip;
        ov.x = d.x + (d.flip ? -o.x : o.x), ov.y = d.y + o.y;
        ov.hasSortY = true, ov.sortY = d.hasSortY ? d.sortY : d.y;  // ordered with its sprite
        ov.depthBias = d.depthBias + 0.0002f;
        ov.shadow = false;
        ov.drawFunction = sp.drawFunction(o.image);
        ov.glow = ov.drawFunction == 9;
        s.sprites.push_back(ov);
    }
    for (const ImageScript::Overlay& o : sp.script(d.image).under) {
        render::SpriteDraw sh = d;
        sh.image = o.image;
        sh.x = d.x + (d.flip ? -o.x : o.x), sh.y = d.y + o.y;
        sh.hasSortY = true, sh.sortY = d.hasSortY ? d.sortY : d.y;
        sh.shadow = sp.isShadow(o.image);
        sh.drawFunction = sp.drawFunction(o.image);
        sh.glow = sh.drawFunction == 9;
        sh.air = false;
        sh.tintRed = false;
        s.sprites.push_back(sh);
    }
}

// New graphics for preplaced units (R8): while the option is on, the map shows the units with the edited graphics,
// but a unit that stays in the map that euddraft gets (the units that Build's plan does not make: remakeMakesUnit)
// keeps the game's graphics. Per unit type, the game's image and subunit image; empty while the option is off
// (syncMapUnitGraphics).
std::vector<int> gGameUnitImage, gGameSubunitImage;
bool remakeMakesUnit(const Unit& u);  // core/project_build.inc
bool keepsGameGraphics(const Unit& u) { return u.type < gGameUnitImage.size() && !remakeMakesUnit(u); }
int mapUnitImage(UnitSprites& sp, const Unit& u) { return keepsGameGraphics(u) ? gGameUnitImage[u.type] : sp.imageFor(u.type); }
int mapSubunitImage(UnitSprites& sp, const Unit& u) { return keepsGameGraphics(u) ? gGameSubunitImage[u.type] : sp.subunitImage(u.type); }

// --- Unit animation ----------------------------------------------------------------------------------

// Each map unit plays its idle animation (iscript: Init, or Built for buildings, as a preplaced unit
// does), at the game's speed: one tick per 42 ms.
// A unit's sprite as the game runs it (SpriteSim, as the Data workspace's image player): its images, overlays, shadow,
// turret and the sprites its scripts make, each with its own script, place, direction and draw function.
struct UnitAnim {
    SpriteSim main, sub;
    uint64_t seen = 0;  // scene that last drew it
};
std::unordered_map<uint64_t, UnitAnim> gAnims;  // by serial and type
const Editor* gAnimsFor = nullptr;
uint64_t gSceneNo = 0;
// The game's ticks in milliseconds (a steady clock; the Windows message loop waits for the next one).
uint64_t animNowMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint64_t gAnimLast = 0;
constexpr uint64_t kTickMs = 42;

// (the key has the graphics too: a unit that the remake plan makes or keeps after another unit's change gets the
// animation of its new image)
uint64_t unitAnimKey(const Unit& u) { return uint64_t(keepsGameGraphics(u)) << 63 | uint64_t(u.serial) << 16 | u.type; }

UnitAnim& unitAnim(const Unit& u) {
    AppState& app = shellState();
    UnitSprites& sp = app.ed->unitSprites();
    auto [it, fresh] = gAnims.try_emplace(unitAnimKey(u));
    if (fresh) {
        const bool building = u.type < app.ed->unitTypes().size() && app.ed->unitTypes()[u.type].is(UnitFlag::Building);
        const int image = mapUnitImage(sp, u), sub = mapSubunitImage(sp, u), era = app.ed->tileset().era;
        const int dir = sp.facingDirection(u.type, app.randomFacing ? int((u.serial * 2654435761u) >> 27) : -1);
        // A unit as the map shows it: already there, its Init played ahead until it rests (a long wait or its order's
        // turn, at most 64 ticks: the Archon's merge makes its energy), then, but for a building, WalkingToIdle (12):
        // its idle as the game shows it after it stops, whose loop a mod's air units hover in (the Solarion)
        auto settle = [&](SpriteSim& sim, int img) {
            for (int t = 0; t < 64 && sim.running(); ++t) {
                sim.tick();
                if (sim.takeHandoff() >= 2) break;
            }
            sim.takeSounds(), sim.takeAttack();
            if (!building && sp.animOffset(img, 12)) sim.playAnim(12);
        };
        it->second.main = SpriteSim(sp, image, building && sp.animOffset(image, 16) ? 16 : 0, dir, u.serial, era);
        settle(it->second.main, image);
        if (sub >= 0) it->second.sub = SpriteSim(sp, sub, 0, dir, u.serial + 1, era), settle(it->second.sub, sub);
    }
    it->second.seen = gSceneNo;
    return it->second;
}

// Runs the game ticks due since the last call; true if any frame changed.
bool advanceAnims() {
    AppState& app = shellState();
    // The animations hold the sprites of the editor they were made for: after another tab, a closed tab or a new map,
    // that editor may be gone. Start again (buildScene makes them again for this editor).
    if (gAnimsFor != app.ed.get()) {
        gAnims.clear(), gAnimsFor = app.ed.get();
        return false;
    }
    const uint64_t now = animNowMs();
    if (now - gAnimLast > 500) gAnimLast = now - kTickMs;  // after a pause: no catching up
    bool changed = false;
    for (; now - gAnimLast >= kTickMs; gAnimLast += kTickMs)
        for (auto& [k, a] : gAnims) {
            a.main.tick(), a.sub.tick();
            a.main.takeSounds(), a.sub.takeSounds(), a.main.takeAttack(), a.sub.takeAttack();  // (the map plays none)
            changed = true;
        }
    return changed;
}

// A running sprite's images (SpriteSim::draws, back to front) at `base`'s place, ordered with it.
void addSimSprites(render::Scene& s, const SpriteSim& sim, const render::SpriteDraw& base) {
    const std::vector<SpriteSim::Draw> draws = sim.draws();
    for (size_t i = 0; i < draws.size(); ++i) {
        const SpriteSim::Draw& dd = draws[i];
        render::SpriteDraw d = base;
        d.image = dd.image, d.frame = dd.frame, d.flip = dd.flip;
        d.x = base.x + float(dd.x), d.y = base.y + float(dd.y);
        d.hasSortY = true, d.sortY = base.hasSortY ? base.sortY : base.y;
        d.depthBias = base.depthBias + 0.00002f * float(i);  // (later ones on top)
        d.drawFunction = dd.drawFunction, d.level = dd.level;
        d.shadow = dd.drawFunction == 10, d.glow = dd.drawFunction == 9;
        if (d.shadow) d.air = false, d.tintRed = false;
        s.sprites.push_back(d);
    }
}

// A snapshot's --wait (sdl_menus.inc): the game ticks of that time, at once.
void advanceAnimsFor(int ms) {
    for (int t = 0; t < ms / int(kTickMs); ++t)
        for (auto& [k, a] : gAnims) {
            a.main.tick(), a.sub.tick();
            a.main.takeSounds(), a.sub.takeSounds(), a.main.takeAttack(), a.sub.takeAttack();
        }
}

void addUnitSprites(render::Scene& s, const Unit& u, float dx, float dy, float alpha, bool red = false, bool animate = false) {
    AppState& app = shellState();
    Editor& ed = *app.ed;
    UnitSprites& sp = ed.unitSprites();
    if (u.type >= ed.unitTypes().size()) return;
    const UnitType& t = ed.unitTypes()[u.type];
    render::SpriteDraw d;
    d.image = mapUnitImage(sp, u);
    // Options > Units > Random facing: a unit whose units.dat facing is random faces its own way (from its serial, so it
    // keeps it), as preplaced units do in the game.
    const int facing = app.randomFacing ? int((u.serial * 2654435761u) >> 27) : -1;
    d.frame = sp.defaultFrame(u.type, d.image, d.flip, facing);
    const bool mineral = u.type >= 176 && u.type <= 178 && (u.validProps & UnitProp::Resources);
    UnitAnim* anim = animate && app.animate && !mineral ? &unitAnim(u) : nullptr;
    const bool running = anim && anim->main.running();
    // A mineral field (176 to 178) shows how much is left, as in the game: full from 750, fewer crystals below that
    // (at 500 and at 250 when the picture has 4 frames; at 250 when it has 3).
    if (mineral) {
        const Grp* g = sp.grp(d.image);
        const int n = g ? std::max(1, int(g->frames.size())) : 1;
        const uint32_t r = u.resources;
        const int f = r >= 750 ? 0 : n >= 4 ? (r >= 500 ? 1 : r >= 250 ? 2 : 3) : (r >= 250 ? 1 : 2);
        d.frame = std::min(f, n - 1), d.flip = false;
    }
    d.x = u.x + dx, d.y = u.y + dy;
    d.owner = uint8_t(std::min<int>(u.owner, 11));
    d.alpha = alpha;
    d.air = t.is(UnitFlag::Flyer) && !t.is(UnitFlag::Building);
    d.tintRed = red;
    if (running) {  // (the frame and direction its script shows, for the add-on's connector and the turret)
        d.frame = anim->main.mainFrame();
        sp.facingIndex(d.image, anim->main.direction(), d.flip);
        if (!sp.turns(d.image)) d.flip = false;
        addSimSprites(s, anim->main, d);
    } else {
        addImage(s, d);
    }
    // A Terran add-on linked to its building: its connector (the image after the add-on's, as the game's data has
    // them: Comsat Station 271, Comsat Station Connector 272), drawn with it (GitHub #129).
    if ((u.links & UnitLink::Addon) && addonParent(u.type) >= 0 && d.image + 1 < sp.imageCount()) {
        render::SpriteDraw c = d;
        c.image = d.image + 1;
        c.frame = sp.script(c.image).initFrame;
        c.flip = false;
        c.hasSortY = true, c.sortY = d.y;
        c.depthBias = d.depthBias - 0.0001f;  // under the add-on
        addImage(s, c);
    }
    int sub = mapSubunitImage(sp, u);
    if (sub >= 0) {
        render::SpriteDraw st = d;
        st.image = sub;
        st.frame = sp.defaultFrame(u.type, sub, st.flip, facing);
        st.depthBias = 0.0005f;
        // The turret sits at the base's special-overlay offset for the base's current frame, and is ordered at the
        // unit's place (as the game orders the turret's sprite), so it shows on top of its base.
        auto [tx, ty] = sp.specialOffset(d.image, d.frame);
        st.x = d.x + (d.flip ? -tx : tx), st.y = d.y + ty;
        st.hasSortY = true, st.sortY = d.y;
        if (anim && anim->sub.running()) addSimSprites(s, anim->sub, st);
        else addImage(s, st);
    }
}

// Outline of the diamonds whose four corners are all in `corners`; single corners as small diamonds.
void diamondOutline(render::Scene& s, const std::set<std::pair<int, int>>& set, uint32_t rgba) {
    auto line = [&](float x0, float y0, float x1, float y1) {
        render::RectDraw r;
        r.x0 = x0, r.y0 = y0, r.x1 = x1, r.y1 = y1, r.rgba = rgba, r.thickness = 2, r.line = true;
        s.rects.push_back(r);
    };
    auto in = [&](int x, int y) { return set.count({x, y}) != 0; };
    // Diamond centers have x + y even; corners L, T, R, B at (x-1,y), (x,y-1), (x+1,y), (x,y+1).
    std::set<std::pair<int, int>> diamonds;
    for (auto [cx, cy] : set)
        for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            int x = cx + dx, y = cy + dy;
            if (in(x - 1, y) && in(x, y - 1) && in(x + 1, y) && in(x, y + 1)) diamonds.insert({x, y});
        }
    for (auto [x, y] : diamonds) {
        const float l = (x - 1) * 64.0f, r = (x + 1) * 64.0f, cxp = x * 64.0f, t = (y - 1) * 32.0f, bt = (y + 1) * 32.0f, cyp = y * 32.0f;
        // Each edge only where the diamond across it is not part of the footprint.
        if (!diamonds.count({x - 1, y - 1})) line(l, cyp, cxp, t);
        if (!diamonds.count({x + 1, y - 1})) line(cxp, t, r, cyp);
        if (!diamonds.count({x + 1, y + 1})) line(r, cyp, cxp, bt);
        if (!diamonds.count({x - 1, y + 1})) line(cxp, bt, l, cyp);
    }
    if (diamonds.empty())
        for (auto [cx, cy] : set) {
            const float x = cx * 64.0f, y = cy * 32.0f;
            line(x - 32, y, x, y - 16), line(x, y - 16, x + 32, y), line(x + 32, y, x, y + 16), line(x, y + 16, x - 32, y);
        }
}

// The outline of a set of tiles (x, y in tiles): lines on the tile edges between a tile of the set and one that is not
// in it (GitHub #135, #136: the Circle's and the Diamond's brush and selection are drawn as their outline). The edges
// in a line are one line each.
void tileOutline(render::Scene& s, const std::vector<std::pair<int, int>>& tiles, uint32_t rgba, float thickness = 1.5f) {
    if (tiles.empty()) return;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (auto [x, y] : tiles) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    std::vector<uint8_t> in(size_t(w) * size_t(h), 0);
    for (auto [x, y] : tiles) in[size_t(y - y0) * size_t(w) + size_t(x - x0)] = 1;
    auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && in[size_t(y) * size_t(w) + size_t(x)]; };
    auto line = [&](float ax, float ay, float bx, float by) {
        render::RectDraw r;
        r.x0 = ax, r.y0 = ay, r.x1 = bx, r.y1 = by, r.rgba = rgba, r.thickness = thickness, r.line = true;
        s.rects.push_back(r);
    };
    for (int y = 0; y <= h; ++y)  // the edges across: between the rows y - 1 and y
        for (int x = 0; x < w;) {
            if (at(x, y - 1) == at(x, y)) { ++x; continue; }
            const int from = x;
            while (x < w && at(x, y - 1) != at(x, y) && at(x, y) == at(from, y)) ++x;
            line(float((x0 + from) * 32), float((y0 + y) * 32), float((x0 + x) * 32), float((y0 + y) * 32));
        }
    for (int x = 0; x <= w; ++x)  // the edges down: between the columns x - 1 and x
        for (int y = 0; y < h;) {
            if (at(x - 1, y) == at(x, y)) { ++y; continue; }
            const int from = y;
            while (y < h && at(x - 1, y) != at(x, y) && at(x, y) == at(x, from)) ++y;
            line(float((x0 + x) * 32), float((y0 + from) * 32), float((x0 + x) * 32), float((y0 + y) * 32));
        }
}

std::vector<std::pair<int, uint16_t>> ghostTiles;  // doodad preview tiles, set by buildScene

void syncMapUnitGraphics();
void rangesScene(render::Scene& s, float dx = 0, float dy = 0);
void addFogLayer(render::Scene& s);
void addResourceZones(render::Scene& s);
void tilesScene(render::Scene& s, float wx, float wy);

// The units' paste ghost with the mouse at (wx, wy) in the map: each copied unit (and its symmetric copies) where a click
// puts it, and red when it does not fit there (the units before it in the copy count as placed).
struct PasteGhostUnit {
    Unit unit;
    bool red = false;
};
std::vector<PasteGhostUnit> unitPasteGhost(float wx, float wy) {
    const AppState& app = appState();
    Editor& ed = *app.ed;
    std::vector<PasteGhostUnit> out;
    std::vector<Unit> placed = ed.units();
    float pwx = wx, pwy = wy;
    snapUnitPoint(pwx, pwy);
    for (const Unit& u : ed.pasteUnits(int(pwx), int(pwy))) {  // (the symmetric copies too)
        out.push_back({u, !app.allowIllegalUnits && !placementOk(ed.placementContext(&placed), u)});
        placed.push_back(u);
    }
    return out;
}

// The terrain selection (the copy area; while it is made: to the mouse at wx, wy): its outline. Returns the number of
// lines (the tests read it).
size_t terrainSelScene(render::Scene& s, float wx, float wy) {
    AppState& app = shellState();
    Editor& ed = *app.ed;
    const bool dragging = app.mode == Mode::Terrain && app.drag == Drag::TerrainSelect;
    const size_t from = s.rects.size();
    const bool combining = dragging && app.terrainSelOp;  // Shift or Ctrl: the selection and the drag's box
    if (app.mode == Mode::Terrain && app.hasTerrainSel && (combining || (!dragging && !app.terrainSelMask.empty()))) {
        tileOutline(s, terrainSelTiles(), 0xFFD040FFu);  // the outline of the selected tiles (any shape, in parts)
        if (!combining) return s.rects.size() - from;
    }
    if (combining) {  // the drag's box: green adds, red takes out
        outline(s, terrainSelectRect(wx, wy), app.terrainSelOp == 1 ? 0x40FF40FFu : 0xFF5050FFu, 1.5f);
        return s.rects.size() - from;
    }
    if (dragging || (app.mode == Mode::Terrain && app.hasTerrainSel)) {
        const Rect r = dragging ? terrainSelectRect(wx, wy) : app.terrainSel;
        // Isometric while dragging: the shape exactly from the press to the mouse (bright), over what it selects (faint,
        // as it is once the button is up)
        const bool exact = dragging && !app.terrainRect;
        const uint32_t col = exact ? 0xFFD04070u : 0xFFD040FFu;
        if (exact) {
            const float x0 = std::min(app.dragWX, wx), x1 = std::max(app.dragWX, wx), y0 = std::min(app.dragWY, wy), y1 = std::max(app.dragWY, wy);
            const float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
            auto line = [&](float ax, float ay, float bx, float by) {
                render::RectDraw l;
                l.x0 = ax, l.y0 = ay, l.x1 = bx, l.y1 = by, l.rgba = 0xFFD040FFu, l.thickness = 2, l.line = true;
                s.rects.push_back(l);
            };
            if (selectShape() == Editor::SelectShape::Rect) {
                line(x0, y0, x1, y0), line(x1, y0, x1, y1), line(x1, y1, x0, y1), line(x0, y1, x0, y0);
            } else if (selectShape() == Editor::SelectShape::Ellipse) {
                const float rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
                const int n = std::clamp(int(6.2831853f * std::max(rx, ry) * app.zoom / 4), 24, 360);
                for (int k = 0; k < n; ++k) {
                    const float a0 = k * 6.2831853f / n, a1 = (k + 1) * 6.2831853f / n;
                    line(mx + rx * std::cos(a0), my + ry * std::sin(a0), mx + rx * std::cos(a1), my + ry * std::sin(a1));
                }
            } else {  // the diamond in the box (its points at the middle of its sides)
                line(mx, y0, x1, my), line(x1, my, mx, y1), line(mx, y1, x0, my), line(x0, my, mx, y0);
            }
        }
        if (selectShape() == Editor::SelectShape::Rect) outline(s, r, col, 1.5f);
        else if (app.terrainRect) {  // a diamond or an ellipse: the outline of the tiles in it (on the tile edges)
            std::vector<std::pair<int, int>> tiles;
            for (int y = r.top / 32; y * 32 < r.bottom; ++y)
                for (int x = r.left / 32; x * 32 < r.right; ++x)
                    if (Editor::inSelectShape(r, selectShape(), x * 32 + 16, y * 32 + 16)) tiles.push_back({x, y});
            tileOutline(s, tiles, 0xFFD040FFu);
        } else {  // the outline of the diamonds whose corners are in it (on the diamonds' edges)
            const auto corners = ed.cornersInShape(r, selectShape());
            diamondOutline(s, std::set<std::pair<int, int>>(corners.begin(), corners.end()), col);
        }
    }
    return s.rects.size() - from;
}

// The Ramp brush's and the Bridge's outline at a point (map pixels): the brush as the paint brush shows it (its shape and size: Alt + wheel),
// with no trail along the stroke.
std::set<std::pair<int, int>> rampBrushCorners(std::pair<int, int> at) {
    const std::vector<std::pair<int, int>> c = appState().ed->brushCorners(at.first, at.second, currentBrush());
    return std::set<std::pair<int, int>>(c.begin(), c.end());
}

// The shape under way (core/terrain_shapes.inc): its tiles over the terrain (ghostTiles), its lines between its points,
// and Polygon's points. Ramp: its brush where the mouse is, in place of the lines.
void shapeScene(render::Scene& s) {
    shapeUpdate();
    ghostTiles = gShape.ghost;
    for (const auto& [x, y] : gShape.red)  // (Bridge: no bridge fits there yet; Ramp: on protected tiles)
        s.rects.push_back({x * 32.f, y * 32.f, x * 32.f + 32, y * 32.f + 32, 0xE0303058u, true});
    if (gShape.tool == TRamp) {  // (the brush where the mouse is, as the paint brush shows it)
        if (!gShape.points.empty()) diamondOutline(s, rampBrushCorners(gShape.points.back()), gShape.unramp ? 0xFF6060F0u : 0xFFFFFFF0u);
        return;
    }
    if (gShape.tool == TBridge) {  // (the drag as an isometric line of the brush (its size: the bridge's width), over the bridge's tiles)
        if (gShape.points.empty()) return;
        const auto [x0, y0] = gShape.points.front();
        const auto [x1, y1] = gShape.points.back();
        const int n = std::max(1, int(std::hypot(x1 - x0, y1 - y0) / 16));
        std::set<std::pair<int, int>> line;
        for (int k = 0; k <= n; ++k)
            for (const auto& c : rampBrushCorners({x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n})) line.insert(c);
        diamondOutline(s, line, gShape.red.empty() ? 0xFFFFFFF0u : 0xFF6060F0u);
        return;
    }
    const std::vector<std::pair<int, int>> pts = shapeGeometry(shapePath());  // (Rectangle, Ellipse: their outline)
    const size_t lines = pts.size() < 2 ? 0 : gShape.tool == TLine || pts.size() < 3 ? pts.size() - 1 : pts.size();
    for (size_t i = 0; i < lines; ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        render::RectDraw l;
        l.x0 = float(a.first), l.y0 = float(a.second), l.x1 = float(b.first), l.y1 = float(b.second);
        l.rgba = 0xFFFFFFF0u, l.thickness = 1.5f, l.line = true;
        s.rects.push_back(l);
    }
    if (gShape.tool != TPolygon) return;
    const float r = 3 / std::max(0.01f, shellState().zoom);  // (3 screen pixels)
    for (const auto& [x, y] : gShape.points) s.rects.push_back({float(x) - r, float(y) - r, float(x) + r, float(y) + r, 0xFFFFFFF0u, true});
}

// A live game shows in place of the map's units and sprites while it runs (experimental: Tools > Simulate,
// sdl_simulation.inc): it adds its images to the scene. nullptr: the map's own.
void (*gLiveScene)(render::Scene& s) = nullptr;

// The unit to place at the mouse (Units mode, a unit picked): whether it fits where it is (ok), where correction moves it
// (fix: only when it does not fit), and its copies at the mirror images and turns, each where a click puts it. The checks
// look through the map's units (and correction tries up to 81 x 81 spots): the answer is kept until the unit, its place
// (gx, gy: the mouse's point before the snap to the unit's grid), the map or the placement options change.
struct PlaceGhost {
    bool ok = false;
    std::optional<std::pair<uint16_t, uint16_t>> fix;
    std::vector<Unit> copies;
};
const PlaceGhost& placeGhost(const Unit& u, float gx, float gy) {
    const AppState& app = appState();
    const Editor& ed = *app.ed;
    struct Key {
        const Editor* ed = nullptr;
        uint64_t revision = 0;
        uint16_t type = 0, x = 0, y = 0;
        uint8_t owner = 0;
        int gx = 0, gy = 0, mirrors = 0, turns = 0, limit = 0;
        bool illegal = false, correction = false, stacking = false, resourceDistance = false, snapBuildings = false;
        bool operator==(const Key&) const = default;
    };
    static Key cachedKey;
    static PlaceGhost cached;
    const PlacementContext ctx = ed.placementContext();
    const Key key{&ed, ed.revision(), u.type, u.x, u.y, u.owner, int(gx), int(gy), ed.mirrors(), ed.turns(), ed.unitLimit(),
                  app.allowIllegalUnits, app.correction, ctx.stacking, ctx.resourceDistance, app.snapBuildings};
    if (key == cachedKey) return cached;
    cachedKey = key;
    PlaceGhost& g = cached;
    g = {};
    g.ok = app.allowIllegalUnits || checkPlacement(ctx, u).empty();
    // The main unit's place as a click puts it (where it fits, or where correction moves it). Without one, a click
    // places no copies either, so none are drawn.
    Unit main = u;
    bool placed = g.ok;
    if (!g.ok && app.correction)
        if ((g.fix = ed.findValidPosition(u))) main.x = g.fix->first, main.y = g.fix->second, placed = true;
    if (ed.unitLimit() > 0 && int(ed.units().size()) >= ed.unitLimit()) placed = false;
    // The copies at the mirror images and turns, each checked against the units placed before it (as placeUnit does).
    const auto points = placed ? ed.symmetricPoints(int(gx), int(gy)) : std::vector<std::pair<int, int>>{};
    if (points.size() > 1) {
        const UnitType& t = ed.unitTypes()[u.type];
        std::vector<Unit> placedSoFar = ed.units();
        placedSoFar.push_back(main);
        for (size_t k = 1; k < points.size(); ++k) {
            if (ed.unitLimit() > 0 && int(placedSoFar.size()) >= ed.unitLimit()) break;
            uint16_t mx = uint16_t(std::clamp(points[k].first, 0, ed.width() * 32 - 1)), my = uint16_t(std::clamp(points[k].second, 0, ed.height() * 32 - 1));
            snapToGrid(t, mx, my);
            Unit m = makeUnit(t, u.owner, mx, my);
            if (!app.allowIllegalUnits && !checkPlacement(ed.placementContext(&placedSoFar), m).empty()) {
                if (!app.correction) continue;
                auto p = ed.findValidPosition(m);
                if (!p) continue;
                m.x = p->first, m.y = p->second;
            }
            g.copies.push_back(m);
            placedSoFar.push_back(m);
        }
    }
    return g;
}

int cursorTurnable(const AppState& app);  // (map_input.inc)
render::Scene buildScene() {
    // (the tool strip's turn and mirror buttons: on or gray as soon as something comes onto the cursor or goes)
    static int turnable = -1;
    if (const int now = cursorTurnable(appState()) != 0; now != turnable) turnable = now, shellToolsChanged();
    AppState& app = shellState();
    syncMapUnitGraphics();  // before the animations: they hold the images of the unit types
    render::Scene s;
    ghostTiles.clear();
    if (gAnimsFor != app.ed.get()) gAnims.clear(), gAnimsFor = app.ed.get();
    ++gSceneNo;
    s.camera = {app.camX, app.camY, app.zoom};
    Editor& ed = *app.ed;
    UnitSprites& sp = ed.unitSprites();
    float wx, wy;
    toWorld(app.mouseX, app.mouseY, wx, wy);
    float moveDX = 0, moveDY = 0;
    if (app.drag == Drag::MoveUnits && app.dragMoved) dragMoveDelta(wx, wy, moveDX, moveDY);

    // The grid (View > Show grid; its size and offset: View > Grid size), only while its cells are at least 6 screen pixels.
    if (app.showGrid && std::min(gDock.gridW, gDock.gridH) * app.zoom >= 6) {
        int vw, vh;
        viewSize(vw, vh);
        const float x0 = std::max(0.0f, app.camX), y0 = std::max(0.0f, app.camY);
        const float x1 = std::min(ed.width() * 32.0f, app.camX + vw / app.zoom), y1 = std::min(ed.height() * 32.0f, app.camY + vh / app.zoom);
        const float t = 1 / app.zoom;  // one screen pixel
        const uint32_t rgba = 0xFFFFFF30u;
        const float gw = float(gDock.gridW), gh = float(gDock.gridH), ox = float(gDock.gridX % gDock.gridW), oy = float(gDock.gridY % gDock.gridH);
        for (float x = std::ceil((x0 - ox) / gw) * gw + ox; x <= x1; x += gw) s.rects.push_back({x, y0, x + t, y1, rgba, true});
        for (float y = std::ceil((y0 - oy) / gh) * gh + oy; y <= y1; y += gh) s.rects.push_back({x0, y, x1, y + t, rgba, true});
    }
    // View > Overlays: Terrain flags, Pathing (core/pathing_layer.inc), then Show town areas.
    addTerrainFlagsLayer(s);  // (View > Overlays > Terrain flags: core/terrain_flags_layer.inc)
    addPathingLayer(s);
    addPathOverlays(s);
    addCreepEdges(ed, s);  // (View > Layers > Creep: core/creep_layer.inc)
    addProtectedLayer(ed, s);  // (View > Layers > Protected terrain: core/protected_layer.inc)

    // Only what can show in the view is drawn: the map's sprites and units whose point is in the view or less than
    // kSpriteReach map pixels out of it. (A picture reaches at most 256 pixels from its point: a GRP frame's place and
    // size in its box are bytes, in the game's data at most 128; its overlays and turret are at most 128 more each:
    // iscript's offsets are signed bytes.)
    constexpr float kSpriteReach = 512;
    int viewW, viewH;
    viewSize(viewW, viewH);
    const float viewZoom = std::max(app.zoom, 0.001f);
    const float viewX0 = app.camX - kSpriteReach, viewY0 = app.camY - kSpriteReach;
    const float viewX1 = app.camX + float(viewW) / viewZoom + kSpriteReach, viewY1 = app.camY + float(viewH) / viewZoom + kSpriteReach;
    const float viewLeft = app.camX, viewTop = app.camY, viewRight = app.camX + float(viewW) / viewZoom, viewBottom = app.camY + float(viewH) / viewZoom;
    auto inView = [&](float x, float y) { return x >= viewX0 && x <= viewX1 && y >= viewY0 && y <= viewY1; };

    // Doodad overlays and other map sprites (THG2). View > Layers: a doodad's overlay goes with the doodads, the rest with
    // the sprites.
    // (x, y, sprite) of the doodads' overlays: made again when the map changes
    static std::set<std::tuple<uint16_t, uint16_t, uint16_t>> overlays;
    static std::tuple<const Editor*, uint64_t, const DoodadTypes*, size_t> overlaysFor{};
    if (gLayers.doodads != gLayers.sprites) {
        const std::tuple<const Editor*, uint64_t, const DoodadTypes*, size_t> key{&ed, ed.revision(), &ed.doodadTypes(), ed.doodadTypes().all().size()};
        if (key != overlaysFor) {
            overlaysFor = key;
            overlays.clear();
            for (const Doodad& dd : ed.doodads())
                if (const DoodadType* t = ed.doodadTypes().find(dd.id); t && t->overlaySprite) overlays.insert({dd.x, dd.y, t->overlaySprite});
        }
    }
    for (const MapSprite& m : ed.sprites()) {
        if (gLiveScene) break;
        if (!inView(m.x, m.y)) continue;
        if (!gLayers.doodads || !gLayers.sprites) {
            const bool overlay = (m.flags & SpriteFlag::DrawAsSprite) && overlays.count({m.x, m.y, m.id});
            if (overlay ? !gLayers.doodads : !gLayers.sprites) continue;
        }
        render::SpriteDraw d;
        if (m.flags & SpriteFlag::DrawAsSprite) {
            d.image = sp.imageForSprite(m.id);
            d.frame = sp.script(d.image).startFrameOn(ed.era());  // as the game starts it (playframtile: per tileset)
        } else {
            d.image = sp.imageFor(m.id);
            d.frame = sp.defaultFrame(m.id, d.image, d.flip);
        }
        d.x = m.x, d.y = m.y;
        d.owner = uint8_t(std::min<int>(m.owner, 11));
        addImage(s, d);
    }
    // Units (selected ones follow the cursor while being dragged).
    const auto& units = ed.units();
    std::vector<Unit> others;  // the units that stay near the dragged ones, to check those against
    std::vector<bool> moving;  // the selection and its buildings' add-ons
    if (moveDX || moveDY) moving = movingUnits(), others = unitsNearMoved(moving, moveDX, moveDY);
    ed.setMapRevealersHidden(!gLayers.revealers);  // (the layer's state: the revealers are not picked while hidden)
    // (a unit out of the view keeps its idle animation: it goes on where it was when the unit comes into the view)
    auto keepAnim = [&](const Unit& u) {
        if (gAnims.empty()) return;
        if (const auto it = gAnims.find(unitAnimKey(u)); it != gAnims.end()) it->second.seen = gSceneNo;
    };
    for (size_t i = 0; i < units.size() && gLayers.units && !gLiveScene; ++i) {
        if (!gLayers.revealers && units[i].type == kMapRevealerUnit) continue;
        bool sel = (moveDX || moveDY) && moving[i];
        bool red = false;
        if (sel) {
            Unit moved = units[i];
            moved.x = uint16_t(std::clamp(int(std::lround(moved.x + moveDX)), 0, ed.width() * 32 - 1));
            moved.y = uint16_t(std::clamp(int(std::lround(moved.y + moveDY)), 0, ed.height() * 32 - 1));
            if (moved.type < ed.unitTypes().size()) snapToGrid(ed.unitTypes()[moved.type], moved.x, moved.y);
            if (!inView(moved.x, moved.y)) {
                keepAnim(moved);
                continue;
            }
            red = !app.allowIllegalUnits && !checkPlacement(ed.placementContext(&others), moved).empty();
            addUnitSprites(s, moved, 0, 0, 1, red, true);
            continue;
        }
        if (!inView(units[i].x, units[i].y)) {
            keepAnim(units[i]);
            continue;
        }
        addUnitSprites(s, units[i], 0, 0, 1, false, true);
    }
    std::erase_if(gAnims, [](const auto& kv) { return kv.second.seen != gSceneNo; });
    if (gLiveScene) gLiveScene(s);  // (the live game's units and sprites)

    // Locations.
    if (app.showLocations || app.mode == Mode::Locations) {
        bool active = app.mode == Mode::Locations;
        // A move: each selected location by the selected one's way; a resize: the selected one.
        int moveX = 0, moveY = 0;
        if (app.drag == Drag::LocMove && ed.locationUsed(ed.selectedLocation())) {
            const Rect to = previewLocationRect(wx, wy);
            moveX = to.left - app.locStart.left, moveY = to.top - app.locStart.top;
        }
        for (int i = 0; i < ed.locationCount(); ++i) {
            if (!ed.locationUsed(i)) continue;
            Rect r = ed.locationRect(i);
            const bool sel = active && ed.locationSelected(i), main = sel && ed.selectedLocation() == i;
            if (sel && app.drag == Drag::LocMove) r = {r.left + moveX, r.top + moveY, r.right + moveX, r.bottom + moveY};
            if (main && app.drag == Drag::LocResize) r = previewLocationRect(wx, wy);
            // (not in the view: no box and no edges; its name starts in it and goes right and down, and can come out of it)
            const float edge = 8 / viewZoom;  // (the resize handles and the outline: a few screen pixels out of the box)
            const bool boxShows = float(r.right) + edge >= viewLeft && float(r.left) - edge <= viewRight && float(r.bottom) + edge >= viewTop &&
                                  float(r.top) - edge <= viewBottom;
            if (!boxShows && (float(r.left) > viewRight || float(r.top) > viewBottom)) continue;
            const LocationLabel& label = locationLabel(i, ed.locationName(i), app.locationWrap, float(r.right - r.left) * app.zoom - 6, float(r.bottom - r.top) * app.zoom - 6);
            const float textX = float(r.left) + 3 / app.zoom, textY = float(r.top) + 3 / app.zoom;
            const bool textShows = textX + label.width / viewZoom >= viewLeft && textY + label.height / viewZoom >= viewTop;
            if (!boxShows && !textShows) continue;
            const bool inv = ed.locationInverted(i);  // inverted (holds air units only): red, not blue
            if (boxShows) {
                s.rects.push_back({float(r.left), float(r.top), float(r.right), float(r.bottom), inv ? (active ? 0xFF404030u : 0xFF404018u) : active ? 0x2060FF30u : 0x2060FF18u, true});
                outline(s, r, sel ? 0xFFD040FFu : inv ? (active ? 0xFF5050E0u : 0xFF505080u) : (active ? 0x5080FFE0u : 0x5080FF80u), sel ? 2.0f : 1.0f);
            }
            if (textShows) s.texts.push_back({textX, textY, label.text, active ? 0xFFFFFFFFu : 0xFFFFFFA0u, true, mapTextScale()});
            if (main)  // (the edges of the selected one resize)
                for (int c = 0; c < 4; ++c) {
                    float hx = (c & 1) ? float(r.right) : float(r.left), hy = (c & 2) ? float(r.bottom) : float(r.top);
                    float h = 4 / app.zoom;
                    s.rects.push_back({hx - h, hy - h, hx + h, hy + h, 0xFFD040FFu, true});
                }
        }
        if (app.drag == Drag::BoxSelect)
            s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF40FFu, false, 1, true}),
                s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF4020u, true, 1, true});
        if (active && app.pastingItems) {  // the copy being pasted, where a click puts it
            int dx, dy;
            locationPasteShift(app, wx, wy, dx, dy);
            for (const auto& c : app.locationClip)
                outline(s, {c.rect.left + dx, c.rect.top + dy, c.rect.right + dx, c.rect.bottom + dy}, 0x40FF40FFu, 2);
        }
        if (app.drag == Drag::LocCreate)  // Ctrl or Shift: it is made inverted (red)
            outline(s, previewLocationRect(wx, wy), shellCtrlOrShiftDown() ? 0xFF5050FFu : 0xFFD040FFu, 2);
    }

    // Selection boxes.
    // Selected units: the game's selection circle under the unit (the box where a unit has none).
    for (int i : ed.selection()) {
        if (!inView(units[i].x + moveDX, units[i].y + moveDY)) continue;
        int img, off;
        if (sp.selectionCircle(units[i].type, img, off)) {
            // Where the unit is drawn: while dragging, its moved position snapped like the unit itself
            // (buildings to tile edges).
            Unit at = units[i];
            if (moveDX || moveDY) {
                at.x = uint16_t(std::clamp(int(std::lround(at.x + moveDX)), 0, ed.width() * 32 - 1));
                at.y = uint16_t(std::clamp(int(std::lround(at.y + moveDY)), 0, ed.height() * 32 - 1));
                if (at.type < ed.unitTypes().size()) snapToGrid(ed.unitTypes()[at.type], at.x, at.y);
            }
            render::SpriteDraw c;
            c.image = img;
            c.x = at.x, c.y = at.y + float(off);
            c.ground = true;
            s.sprites.push_back(c);
        } else {
            outline(s, ed.unitBox(units[i]), 0x40FF40FFu, 1.5f, moveDX, moveDY);
        }
    }

    terrainSelScene(s, wx, wy);  // Terrain selection (copy area)

    addResourceZones(s);  // View > Overlays > Invalid resource depot, node (core/resource_zones.inc)
    addFogLayer(s);  // View > Overlays > Fog of war (core/fog_layer.inc)
    if (gRangeKinds && app.mode == Mode::Units) rangesScene(s, moveDX, moveDY);  // (they follow a unit being moved)

    // Symmetry axes.
    if (const int mm = ed.mirrors(); mm || ed.turns() > 1) {
        float W = ed.width() * 32.0f, H = ed.height() * 32.0f, t = 1.5f / app.zoom;
        uint32_t c = 0x80C0FF70u;
        auto vline = [&] { s.rects.push_back({W / 2 - t, 0, W / 2 + t, H, c, true}); };
        auto hline = [&] { s.rects.push_back({0, H / 2 - t, W, H / 2 + t, c, true}); };
        auto diag = [&](bool anti) {
            for (int k = 0; k <= 200; ++k) {
                float f = k / 200.0f, x = f * W, y = anti ? (1 - f) * H : f * H;
                s.rects.push_back({x - t * 2, y - t * 2, x + t * 2, y + t * 2, c, true});
            }
        };
        if (mm & Mirror::LeftRight) vline();
        if (mm & Mirror::TopBottom) hline();
        if (mm & Mirror::Diagonal) diag(false);
        if (mm & Mirror::AntiDiagonal) diag(true);
        if (ed.turns() > 1) {  // a turn: a mark at the middle
            const float r = 10 / app.zoom;
            s.rects.push_back({W / 2 - r, H / 2 - r, W / 2 + r, H / 2 + r, c, true});
        }
    }

    if (!app.mouseIn) return s;
    if (app.pasting && ed.canPaste()) {
        // Ghost of the clipboard at the cursor; red where a unit could not be placed as is.
        for (const PasteGhostUnit& g : unitPasteGhost(wx, wy)) {
            addUnitSprites(s, g.unit, 0, 0, 0.7f, g.red);
            outline(s, ed.unitBox(g.unit), g.red ? 0xFF4040C0u : 0x40FF40C0u);
        }
        return s;
    }
    if (app.mode == Mode::Units) {
        // With a unit picked, the unit under the mouse does not matter: a click places there (it does not select).
        // (Not with Spray: it places random picks around the mouse, not this one in the middle; GitHub feedback #99.)
        if (app.palette >= 0 && !objectSprayOn() && !objectFillOn() && !objectEraseOn() && (app.drag == Drag::None || app.drag == Drag::PendingPlace || app.drag == Drag::PlaceStroke)) {
            // Ghost of the unit to place, with where correction would put it.
            const UnitType& t = ed.unitTypes()[uint16_t(app.palette)];
            float gx = wx, gy = wy;
            snapUnitPoint(gx, gy);
            uint16_t ux = uint16_t(std::clamp(gx, 0.0f, ed.width() * 32.0f - 1)), uy = uint16_t(std::clamp(gy, 0.0f, ed.height() * 32.0f - 1));
            snapToGrid(t, ux, uy);
            uint8_t who = (t.availability & UnitAvail::NonNeutral) ? uint8_t(app.playerOwner) : uint8_t(11);
            Unit u = makeUnit(t, who, ux, uy);
            const PlaceGhost& g = placeGhost(u, gx, gy);
            for (const Unit& m : g.copies) {
                addUnitSprites(s, m, 0, 0, 0.55f);
                outline(s, ed.unitBox(m), 0x40FF40C0u);
            }
            const bool ok = g.ok;
            if (g.fix) {
                Unit c = u;
                c.x = g.fix->first, c.y = g.fix->second;
                addUnitSprites(s, c, 0, 0, 0.55f);
                outline(s, ed.unitBox(c), 0x40FF40C0u);
                outline(s, ed.unitBox(u), 0xFF4040A0u);
                return s;
            }
            addUnitSprites(s, u, 0, 0, 0.55f);
            outline(s, ed.unitBox(u), ok ? 0x40FF40C0u : 0xFF4040C0u);
        }
        if (app.drag == Drag::BoxSelect)
            s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF40FFu, false, 1, true}),
                s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF4020u, true, 1, true});
    } else if (app.mode == Mode::Sprites) {
        // The selected sprites (where a drag puts them); the copy being pasted; the box of a box selection.
        // Options > Sprites snap: a click (or a paste) goes to the middle of a grid cell, a drag moves in whole cells.
        const int ddx = app.drag == Drag::MoveSprite && app.dragMoved ? int(snapRound(wx - app.dragWX, app.spriteGrid)) : 0,
                  ddy = app.drag == Drag::MoveSprite && app.dragMoved ? int(snapRound(wy - app.dragWY, app.spriteGrid)) : 0;
        const float cx = snapToCell(wx, app.spriteGrid), cy = snapToCell(wy, app.spriteGrid);
        for (int i : app.selSprites)
            if (i >= 0 && i < int(ed.sprites().size())) {
                const MapSprite& sp = ed.sprites()[size_t(i)];
                Rect b = mapSpriteBox(ed, sp);  // the box of its picture
                outline(s, {b.left + ddx, b.top + ddy, b.right + ddx, b.bottom + ddy}, 0x40FF40FFu, 2);
            }
        // A sprite's picture where it would go: the picked one at the mouse, the copy being pasted, a dragged selection.
        auto ghost = [&](uint16_t id, float x, float y, uint8_t owner) {
            render::SpriteDraw d;
            d.image = ed.unitSprites().imageForSprite(id);
            if (d.image < 0) return;
            d.frame = ed.unitSprites().script(d.image).startFrameOn(ed.era());
            d.x = x, d.y = y, d.owner = uint8_t(std::min<int>(owner, 11)), d.alpha = 0.6f, d.air = true;
            addImage(s, d);
        };
        if (app.palette >= 0 && !app.pastingItems && !objectSprayOn() && !objectFillOn() && !objectEraseOn()) {
            ghost(uint16_t(app.palette), cx, cy, uint8_t(app.playerOwner));
            // The mirror images and turned copies, where a click would put them too.
            const auto copies = ed.symmetricPoints(int(cx), int(cy));
            for (size_t k = 1; k < copies.size(); ++k) {
                ghost(uint16_t(app.palette), float(copies[k].first), float(copies[k].second), uint8_t(app.playerOwner));
                outline(s, {copies[k].first - 12, copies[k].second - 12, copies[k].first + 12, copies[k].second + 12}, 0x40FF40C0u, 1);
            }
        }
        if (app.drag == Drag::MoveSprite && app.dragMoved)
            for (int i : app.selSprites)
                if (i >= 0 && i < int(ed.sprites().size())) {
                    const MapSprite& sp = ed.sprites()[size_t(i)];
                    ghost(sp.id, float(sp.x + ddx), float(sp.y + ddy), sp.owner);
                }
        if (app.pastingItems)
            for (const auto& it : app.spriteClip) {
                ghost(it.id, cx + it.x, cy + it.y, it.owner);
                outline(s, {int(cx) + it.x - 12, int(cy) + it.y - 12, int(cx) + it.x + 12, int(cy) + it.y + 12}, 0x40FF40C0u, 1);
            }
        if (app.drag == Drag::BoxSelect)
            s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF40FFu, false, 1, true}),
                s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF4020u, true, 1, true});
        if (app.palette >= 0 && !objectEraseOn()) {  // where a click puts the picked sprite
            outline(s, {int(cx) - 12, int(cy) - 12, int(cx) + 12, int(cy) + 12}, 0x40FF40C0u, 1);
        }
    } else if (app.mode == Mode::Doodads && app.palette < 0) {
        // The selected doodads, where a drag puts them (green fits, red does not); the copy being pasted; the box.
        auto ghost = [&](uint16_t id, int x, int y, bool fitTint) {
            if (const DoodadType* t = ed.doodadTypes().find(id)) {
                int tx, ty;
                ed.doodadOrigin(*t, x, y, tx, ty);
                const bool ok = !fitTint || doodadFits(ed.chk(), *t, tx, ty, app.doodadCheck);
                outline(s, {tx * 32, ty * 32, (tx + t->width) * 32, (ty + t->height) * 32}, ok ? 0x40FF40FFu : 0xFF4040FFu, 2);
            }
        };
        for (int i : app.selDoodads)
            if (i >= 0 && i < int(ed.doodads().size())) {
                const Doodad& sd = ed.doodads()[size_t(i)];
                if (const DoodadType* t = ed.doodadTypes().find(sd.id))
                    outline(s, {sd.x - t->width * 16, sd.y - t->height * 16, sd.x + t->width * 16, sd.y + t->height * 16}, 0x40FF40FFu, 2);
                if (app.drag == Drag::MoveDoodad && app.dragMoved) ghost(sd.id, int(sd.x) + int(wx - app.dragWX), int(sd.y) + int(wy - app.dragWY), true);
            }
        if (app.pastingItems)
            for (const auto& it : app.doodadClip) ghost(it.id, int(wx) + it.x, int(wy) + it.y, true);
        if (app.drag == Drag::BoxSelect)
            s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF40FFu, false, 1, true}),
                s.rects.push_back({app.dragWX, app.dragWY, wx, wy, 0x40FF4020u, true, 1, true});
    } else if (app.mode == Mode::Doodads && app.palette >= 0 && !objectSprayOn() && !objectFillOn() && !objectEraseOn()) {
        if (const DoodadType* d = ed.doodadTypes().find(uint16_t(app.palette))) {
            int tx, ty;
            ed.doodadOrigin(*d, int(wx), int(wy), tx, ty);
            Rect r{tx * 32, ty * 32, (tx + d->width) * 32, (ty + d->height) * 32};
            bool ok = doodadFits(ed.chk(), *d, tx, ty, app.doodadCheck) && !ed.doodadOverlaps(*d, tx, ty);  // as a click places
            // The doodad's own tiles where it would go (in the terrain pass), then the fit tint on top.
            std::vector<std::pair<int, uint16_t>> cells;
            for (int r0 = 0; r0 < d->height; ++r0) {
                const size_t g = size_t(d->firstGroup) + r0;
                if (g >= ed.tileset().groups.size()) break;
                for (int c = 0; c < d->width; ++c)
                    if (ed.tileset().groups[g].megatiles[size_t(c)] && tx + c >= 0 && ty + r0 >= 0 && tx + c < ed.width() && ty + r0 < ed.height())
                        cells.push_back({(ty + r0) * ed.width() + tx + c, uint16_t(g << 4 | c)});
            }
            ghostTiles = std::move(cells);
            s.rects.push_back({float(r.left), float(r.top), float(r.right), float(r.bottom), ok ? 0x40FF4018u : 0xFF404040u, true});
            outline(s, r, ok ? 0x40FF40FFu : 0xFF4040FFu, 1.5f);
            // The mirror images and turned copies, as a click places them: none when the doodad itself would not be placed;
            // each one is left out where it overlaps another doodad, or a doodad placed before it, or does not fit.
            if (ok) {
                std::vector<Rect> taken{r};
                const auto copies = ed.symmetricPoints(int(wx), int(wy));
                for (size_t k = 1; k < copies.size(); ++k) {
                    int cx, cy;
                    ed.doodadOrigin(*d, copies[k].first, copies[k].second, cx, cy);
                    if (ed.doodadOverlaps(*d, cx, cy) || !doodadFits(ed.chk(), *d, cx, cy, app.doodadCheck)) continue;
                    const Rect cr{cx * 32, cy * 32, (cx + d->width) * 32, (cy + d->height) * 32};
                    bool clash = false;
                    for (const Rect& o : taken) clash = clash || (cr.left < o.right && o.left < cr.right && cr.top < o.bottom && o.top < cr.bottom);
                    if (clash) continue;
                    outline(s, cr, 0x40FF40FFu, 1.5f);
                    taken.push_back(cr);
                }
            }
            if (d->overlaySprite) {
                render::SpriteDraw g;
                g.image = sp.imageForSprite(d->overlaySprite);
                g.x = r.left + d->width * 16.0f, g.y = r.top + d->height * 16.0f;
                g.alpha = 0.6f;
                s.sprites.push_back(g);
            }
        }
    } else if (app.mode == Mode::Terrain && shapeOn()) {
        shapeScene(s);
    } else if (app.mode == Mode::Terrain && (currentTool() == TRamp || currentTool() == TBridge)) {  // Ramp, Bridge: the brush, a diamond (any kind of terrain)
        diamondOutline(s, rampBrushCorners({int(wx), int(wy)}), 0xFFFFFFF0u);
    } else if (app.mode == Mode::Terrain && currentTool() == TErase) {  // Erase: the isometric brush's diamonds (any kind), in red
        std::set<std::pair<int, int>> set;
        for (auto [px, py] : ed.terrainSymmetricPoints(int(wx), int(wy), currentBrush()))
            for (auto c : ed.brushCorners(px, py, currentBrush())) set.insert(c);
        diamondOutline(s, set, 0xFF6060F0u);
    } else if (app.mode == Mode::Terrain && app.terrainRect) {
        tilesScene(s, wx, wy);
    } else if (app.mode == Mode::Terrain && currentTool() == TSelect) {
        if (app.terrainPasting && ed.hasTerrainClipboard()) {
            Rect b = ed.terrainClipboardBox();
            const auto pts = ed.symmetricPoints(int(wx), int(wy));
            const auto corners = ed.terrainPasteCorners(int(wx), int(wy));
            // (the pasted terrain itself, as the map would have it: the paste on a copy of the map, its changed tiles
            // over the terrain; made again only when the paste's place, the map or the copy changes; GitHub #238)
            // The key: the map and its change, the copy (each copy and turn: gTerrainCopyRevision), the paste's places
            // (also the mirrors'), Correction. A large map's paste is made at most every 2 times its last time (as the
            // shapes' preview), the last one when the mouse rests.
            extern uint64_t gTerrainCopyRevision;  // (map_edit_commands.inc)
            struct PasteGhost {
                std::vector<int64_t> key;
                std::vector<std::pair<int, uint16_t>> cells;
                double cost = 0;
                std::chrono::steady_clock::time_point last{};
            };
            static PasteGhost g;
            std::vector<int64_t> key = {int64_t(reinterpret_cast<uintptr_t>(&ed)), int64_t(ed.revision()), int64_t(gTerrainCopyRevision), int64_t(app.correction),
                                        int64_t(corners.size())};
            if (!corners.empty()) key.push_back(corners.front().first), key.push_back(corners.front().second);
            for (const auto& [px, py] : pts) key.push_back(px / 64), key.push_back(py / 32);
            const auto now = std::chrono::steady_clock::now();
            const bool soon = g.cost > 12 && std::chrono::duration<double, std::milli>(now - g.last).count() < 2 * g.cost;
            if (g.key != key && soon) appState().dirty = true;  // (again on a later frame)
            else if (g.key != key) {
                g.key = std::move(key), g.cells.clear();
                Editor& copy = shapeCopy(ed.gameData());
                std::string error;
                if (copy.openChk(ed.chk(), error)) {
                    shapeCopySettings(copy, ed);
                    copy.setTerrainCopies(ed.terrainCopies());
                    copy.pasteTerrain(int(wx), int(wy), app.correction);
                    const std::vector<uint16_t>&a = ed.terrain().tiles, &c = copy.terrain().tiles;
                    if (a.size() == c.size())
                        for (size_t i = 0; i < a.size(); ++i)
                            if (a[i] != c[i]) g.cells.push_back({int(i), c[i]});
                }
                g.last = std::chrono::steady_clock::now();
                g.cost = std::chrono::duration<double, std::milli>(g.last - now).count();
            }
            ghostTiles = g.cells;
            diamondOutline(s, std::set<std::pair<int, int>>(corners.begin(), corners.end()), 0x40FF40FFu);
            for (size_t k = 1; k < pts.size(); ++k) outline(s, {pts[k].first, pts[k].second, pts[k].first + b.right, pts[k].second + b.bottom}, 0x40FF40FFu, 2);
        }
    } else if (app.mode == Mode::Terrain && (app.palette > 0 || (currentTool() == TJumble && !app.terrainRect))) {  // (Jumble: no pick)
        // Brush footprint as diamonds: the outline of the diamonds whose four corners the brush sets, at
        // every symmetric position. A half-size brush sets a single corner; it shows a half diamond.
        if (currentTool() == TBrush || currentTool() == TSpray || isStrokeShape(currentTool()) || currentTool() == TJumble) {
            std::set<std::pair<int, int>> set;
            for (auto [px, py] : ed.terrainSymmetricPoints(int(wx), int(wy), currentBrush()))
                for (auto c : ed.brushCorners(px, py, currentBrush())) set.insert(c);
            diamondOutline(s, set, currentTool() == TSpray ? 0xFFD040E0u : 0xFFFFFFF0u);
            // The Square and the Circle: their own shape too (a thin line, as IsomGrid::brushCorners measures it: half
            // extents of size x 64 and height x 32 pixels), round the diamonds that they paint.
            const Brush b = currentBrush();
            if (b.shape != Brush::Diamond && !b.starEdit()) {
                const float hw = std::max(0.5f, float(b.size)) * 64, hh = std::max(0.5f, float(b.height())) * 32;
                const uint32_t rgba = currentTool() == TSpray ? 0xFFD040A0u : 0xFFFFFFA0u;
                auto line = [&](float x0, float y0, float x1, float y1) {
                    render::RectDraw l;
                    l.x0 = x0, l.y0 = y0, l.x1 = x1, l.y1 = y1, l.rgba = rgba, l.thickness = 1, l.line = true;
                    s.rects.push_back(l);
                };
                for (auto [px, py] : ed.terrainSymmetricPoints(int(wx), int(wy), b)) {
                    if (b.shape == Brush::Square) {
                        line(px - hw, py - hh, px + hw, py - hh), line(px + hw, py - hh, px + hw, py + hh);
                        line(px + hw, py + hh, px - hw, py + hh), line(px - hw, py + hh, px - hw, py - hh);
                        continue;
                    }
                    const int n = std::clamp(int(6.2831853f * std::max(hw, hh) * app.zoom / 4), 24, 360);  // (about 4 screen pixels a part)
                    for (int k = 0; k < n; ++k) {
                        const float a0 = k * 6.2831853f / n, a1 = (k + 1) * 6.2831853f / n;
                        line(px + hw * std::cos(a0), py + hh * std::sin(a0), px + hw * std::cos(a1), py + hh * std::sin(a1));
                    }
                }
            }
        }
    }
    if (objectSprayOn() || objectEraseOn()) {
        // Spray (Erase: in red) area: a circle, a line 1 pixel wide (short straight parts, about 4 screen pixels each).
        const float r = float(sprayRadius());
        const int n = std::clamp(int(6.2831853f * r * app.zoom / 4), 24, 360);
        for (auto [px, py] : ed.symmetricPoints(int(wx), int(wy)))
            for (int k = 0; k < n; ++k) {
                const float a0 = k * 6.2831853f / n, a1 = (k + 1) * 6.2831853f / n;
                render::RectDraw l;
                l.x0 = px + r * std::cos(a0), l.y0 = py + r * std::sin(a0), l.x1 = px + r * std::cos(a1), l.y1 = py + r * std::sin(a1);
                l.rgba = objectEraseOn() ? 0xFF6060F0u : 0xFFD040E0u, l.thickness = 1, l.line = true;
                s.rects.push_back(l);
            }
    }
    return s;
}

// View > Show ranges of the selected units (GitHub feedback #109): around each selected unit (Units mode), its sight
// (the tiles it sees, yellow), and its target acquisition (orange) and weapons' ranges: ground (red) and air (blue), its
// turret's too; a detector's detection range (green dashes) and a Pylon's psi field (purple tiles). From units.dat and weapons.dat: the project's (with its edits) or the game's. A range counts from the
// unit's size box, as the game's.
void dataLoadForProject();
const DatTable* rangeTable(const std::string& name) {
    if (activeProject()) dataLoadForProject();  // the project's tables with their saved edits (once; then at once)
    const DataModel& data = dataState();
    if (data.project && data.project == activeProject())
        for (const auto& t : data.tables)
            if (t && t->schema().name == name) return t.get();
    static std::map<std::string, std::unique_ptr<DatTable>> stock;
    auto& t = stock[name];
    if (!t) {
        const DatSchema* schema = DatSchema::find(name);
        auto loaded = std::make_unique<DatTable>();
        std::string err;
        if (!schema || !loaded->load(shellState().data, *schema, err)) return nullptr;
        t = std::move(loaded);
    }
    return t.get();
}
std::vector<bool> sightMask(int range, int& size);
// A unit's ranges in tiles, as the range overlay has them (rangesScene): sight, target acquisition, the ground and the
// air weapon's range (a turret's for a unit with one), and detection; "" when the tables are not there. For the status
// bar (selectionStatus).
std::wstring unitRangesText(const Unit& unit) {
    const int ut = unit.type;
    auto tiles = [](int px) {  // (a weapon's range in tiles: 32 pixels each, with one decimal where it is not whole)
        wchar_t b[16];
        if (px % 32 == 0) std::swprintf(b, 16, L"%d", px / 32);
        else std::swprintf(b, 16, L"%.1f", px / 32.0);
        return std::wstring(b);
    };
    if (modTab()) {  // (a Cosmonarchy tab: the mod's tables, and the height advantage of the tile the unit stands on)
        const Editor& ed = *shellState().ed;
        cmbw::UnitRanges mr;
        if (!cmbw::unitRanges(ed.gameData(), uint16_t(ut), mr)) return L"";
        const TerrainLayer& mtxm = ed.terrain();
        const int tx = unit.x / 32, ty = unit.y / 32;
        const size_t at = size_t(ty) * mtxm.width + tx;
        const int h = tx < mtxm.width && ty < mtxm.height && at < mtxm.tiles.size() ? cmbw::tileHeight(ed.tileset(), mtxm.tiles[at]) : 0;
        const int g = cmbw::cliffRange(mr.ground, h, mr.cliff), acq = mr.seek + (mr.cliff && h > 0 && mr.ground > 32 ? h : 0);
        std::wstring out;
        auto add = [&](const wchar_t* name, const std::wstring& v) { out += (out.empty() ? L"" : L", ") + std::wstring(tr(name)) + v; };
        if (mr.sight > 0) add(L"Sight ", std::to_wstring(mr.sight));
        if (g > 0) add(L"Ground ", tiles(g) + (g != mr.ground ? L" (" + tiles(mr.ground) + L" + " + tiles(g - mr.ground) + L")" : L""));
        if (mr.air > 0) add(L"Air ", tiles(mr.air));
        if (acq > 0 && (g > 0 || mr.air > 0)) add(L"Acquisition ", std::to_wstring(acq));
        if (mr.flags & 0x8000u) add(L"Detection ", std::to_wstring((mr.flags & 1u) ? 7 : mr.sight));
        if (mr.cliff) add(L"Height ", std::to_wstring(h));
        return out;
    }
    const DatTable* units = rangeTable("units");
    const DatTable* weapons = rangeTable("weapons");
    if (!units || !weapons || ut < 0 || ut >= 228) return L"";
    // (the fields by name once for the tables' schemas: the status bar asks each frame)
    static const DatSchema *unitsSchema = nullptr, *weaponsSchema = nullptr;
    static const DatField *sight = nullptr, *acqF = nullptr, *ground = nullptr, *air = nullptr, *sub = nullptr, *range = nullptr, *flags = nullptr;
    if (unitsSchema != &units->schema() || weaponsSchema != &weapons->schema()) {
        unitsSchema = &units->schema(), weaponsSchema = &weapons->schema();
        sight = units->field("Sight Range"), acqF = units->field("Target Acquisition Range"), ground = units->field("Ground Weapon");
        air = units->field("Air Weapon"), sub = units->field("Subunit 1"), range = weapons->field("Maximum Range");
        flags = units->field("Special Ability Flags");
    }
    if (!sight || !acqF || !ground || !air || !sub || !range || !flags) return L"";
    auto weaponRange = [&](const DatField& f) {  // (pixels; as rangesScene's)
        int w = int(units->value(f, ut));
        const int turret = int(units->value(*sub, ut));
        if (w >= 130 && turret < 228) w = int(units->value(f, turret));
        return w < 130 ? int(weapons->value(*range, w)) : 0;
    };
    std::wstring out;
    auto add = [&](const wchar_t* name, const std::wstring& v) { out += (out.empty() ? L"" : L", ") + std::wstring(tr(name)) + v; };
    const int s = int(units->value(*sight, ut)), acq = int(units->value(*acqF, ut)), g = weaponRange(*ground), a = weaponRange(*air);
    if (s > 0) add(L"Sight ", std::to_wstring(s));
    if (g > 0) add(L"Ground ", tiles(g));
    if (a > 0) add(L"Air ", tiles(a));
    if (acq > 0 && (g > 0 || a > 0)) add(L"Acquisition ", std::to_wstring(acq));
    const uint32_t f = uint32_t(units->value(*flags, ut));
    if (f & 0x8000u) add(L"Detection ", std::to_wstring((f & 1u) ? 7 : s));
    return out;
}
// The ranges as the Data workspace draws them (widgetUnitRanges): the tiles the unit sees (its sight mask, on the map's
// tiles), and the target acquisition (orange), ground (red) and air (blue) ranges as the game measures them: from the
// unit's size box, grown by the game's distance shape (rangeShape). While the units move, the ranges move with them
// (dx, dy: the move so far).
void rangesScene(render::Scene& s, float dx, float dy) {
    AppState& app = shellState();
    if (!app.ed) return;
    // (a Cosmonarchy tab reads the mod's tables, cmbw::unitRanges: the game's .dat tables need not load)
    const bool mod = modTab();
    const DatTable* units = mod ? nullptr : rangeTable("units");
    const DatTable* weapons = mod ? nullptr : rangeTable("weapons");
    if (!mod && (!units || !weapons)) return;
    // (the fields by name once for the tables' schemas: the map asks each frame)
    static const DatSchema *unitsSchema = nullptr, *weaponsSchema = nullptr;
    static const DatField *sight = nullptr, *acqF = nullptr, *ground = nullptr, *air = nullptr, *sub = nullptr, *range = nullptr, *sl = nullptr,
                          *su = nullptr, *sr = nullptr, *sd = nullptr, *flags = nullptr;
    if (!mod && (unitsSchema != &units->schema() || weaponsSchema != &weapons->schema())) {
        unitsSchema = &units->schema(), weaponsSchema = &weapons->schema();
        sight = units->field("Sight Range"), acqF = units->field("Target Acquisition Range"), ground = units->field("Ground Weapon");
        air = units->field("Air Weapon"), sub = units->field("Subunit 1"), range = weapons->field("Maximum Range");
        sl = units->field("Unit Size Left"), su = units->field("Unit Size Up"), sr = units->field("Unit Size Right");
        sd = units->field("Unit Size Down"), flags = units->field("Special Ability Flags");
    }
    if (!mod && (!sight || !acqF || !ground || !air || !sub || !range || !sl || !su || !sr || !sd || !flags)) return;
    auto weaponRange = [&](int ut, const DatField& f) {
        int w = int(units->value(f, ut));
        const int turret = int(units->value(*sub, ut));
        if (w >= 130 && turret < 228) w = int(units->value(f, turret));  // a turret's weapon
        return w < 130 ? int(weapons->value(*range, w)) : 0;
    };
    Editor& ed = *app.ed;
    const auto& list = ed.units();
    const int mw = ed.width(), mh = ed.height();
    // A Cosmonarchy tab: the mod's tables (all its units), and its height advantage: a ground unit's ground weapon and
    // target acquisition reach further by the height of the tile it stands on (as against targets at height 0), so the
    // ranges grow and shrink as a unit is dragged over higher and lower ground.
    const TerrainLayer& mtxm = ed.terrain();
    auto heightAt = [&](int x, int y) {
        const int tx = x / 32, ty = y / 32;
        if (tx < 0 || ty < 0 || tx >= mtxm.width || ty >= mtxm.height || size_t(ty) * mtxm.width + tx >= mtxm.tiles.size()) return 0;
        return cmbw::tileHeight(ed.tileset(), mtxm.tiles[size_t(ty) * mtxm.width + tx]);
    };
    // The ranges of the selected units in the view (or near it: a range reaches into it), nearest its middle first; a
    // very big selection: the first 2000 of them.
    static std::vector<std::pair<float, int>> closest;  // (distance to the view's middle, the unit)
    closest.clear();
    {
        int vw, vh;
        viewSize(vw, vh);
        const float x0 = app.camX, y0 = app.camY, x1 = x0 + float(vw) / app.zoom, y1 = y0 + float(vh) / app.zoom, reach = 20 * 32.0f;
        const float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
        for (int i : ed.selection()) {
            if (i < 0 || i >= int(list.size())) continue;
            const float x = list[size_t(i)].x + dx, y = list[size_t(i)].y + dy;
            if (x < x0 - reach || x > x1 + reach || y < y0 - reach || y > y1 + reach) continue;
            closest.push_back({(x - mx) * (x - mx) + (y - my) * (y - my), i});
        }
        std::sort(closest.begin(), closest.end());
        if (closest.size() > 2000) closest.resize(2000);
    }
    // Each unit's values for the overlay (each kind only when it shows: gRangeKinds). The same values as the last
    // frame's (and the same kinds, zoom and map size): the last frame's lines and tiles again.
    struct Item {
        int cx, cy, sight, acq, g, a, detect, l, t, r, b;
        bool pylon;
    };
    static std::vector<Item> items;
    items.clear();
    for (const auto& [distance, i] : closest) {
        Unit u = list[size_t(i)];
        const int ut = u.type;
        cmbw::UnitRanges mr;
        if (ut < 0 || (mod ? !cmbw::unitRanges(ed.gameData(), uint16_t(ut), mr) : ut >= 228)) continue;
        if (dx || dy) {  // where the drag draws and drops it (buildScene, Editor::moveSelection)
            u.x = uint16_t(std::clamp(int(std::lround(u.x + dx)), 0, mw * 32 - 1));
            u.y = uint16_t(std::clamp(int(std::lround(u.y + dy)), 0, mh * 32 - 1));
            if (u.type < ed.unitTypes().size()) snapToGrid(ed.unitTypes()[u.type], u.x, u.y);
        }
        Item it{};
        it.cx = u.x, it.cy = u.y;
        const int tiles = mod ? mr.sight : int(units->value(*sight, ut));
        it.sight = rangeShown(0) ? tiles : 0;
        RECT box{LONG(it.cx), LONG(it.cy), LONG(it.cx + 1), LONG(it.cy + 1)};
        if (mod) {  // (the tab's unit types: the mod's sizes)
            if (size_t(ut) < ed.unitTypes().size()) {
                const UnitType& t = ed.unitTypes()[size_t(ut)];
                box = {LONG(it.cx - t.sizeLeft), LONG(it.cy - t.sizeUp), LONG(it.cx + t.sizeRight + 1), LONG(it.cy + t.sizeDown + 1)};
            }
        } else
            box = {LONG(it.cx - int(units->value(*sl, ut))), LONG(it.cy - int(units->value(*su, ut))), LONG(it.cx + int(units->value(*sr, ut)) + 1),
                   LONG(it.cy + int(units->value(*sd, ut)) + 1)};
        it.l = int(box.left), it.t = int(box.top), it.r = int(box.right), it.b = int(box.bottom);
        int acq = 0, g = 0, a = 0;
        if (mod) {
            const int h = heightAt(it.cx, it.cy);
            g = cmbw::cliffRange(mr.ground, h, mr.cliff), a = mr.air;
            acq = (mr.seek + (mr.cliff && h > 0 && mr.ground > 32 ? h : 0)) * 32;
        } else
            acq = int(units->value(*acqF, ut)) * 32, g = weaponRange(ut, *ground), a = weaponRange(ut, *air);
        it.acq = rangeShown(1) ? acq : 0, it.g = rangeShown(2) ? g : 0, it.a = rangeShown(3) ? a : 0;
        // Detection (green): a detector finds cloaked and burrowed units in its sight range; a detector building only
        // in 7 tiles (the game's rule). The game measures it from the unit's center.
        const uint32_t f = mod ? mr.flags : uint32_t(units->value(*flags, ut));
        it.detect = (f & 0x8000u) && rangeShown(4) ? ((f & 1u) ? 7 * 32 : tiles * 32) : 0;
        it.pylon = ut == 156 && rangeShown(5);
        items.push_back(it);
    }
    static std::vector<Item> lastItems;
    static std::vector<render::RectDraw> lastRects;
    static int lastKinds = -1, lastW = -1, lastH = -1;
    static float lastZoom = 0;
    static bool lastMod = false;
    auto same = [](const Item& p, const Item& q) {
        return p.cx == q.cx && p.cy == q.cy && p.sight == q.sight && p.acq == q.acq && p.g == q.g && p.a == q.a && p.detect == q.detect &&
               p.l == q.l && p.t == q.t && p.r == q.r && p.b == q.b && p.pylon == q.pylon;
    };
    if (lastKinds == int(gRangeKinds) && lastW == mw && lastH == mh && lastZoom == app.zoom && lastMod == mod &&
        std::equal(items.begin(), items.end(), lastItems.begin(), lastItems.end(), same)) {
        s.rects.insert(s.rects.end(), lastRects.begin(), lastRects.end());
        return;
    }
    lastKinds = int(gRangeKinds), lastW = mw, lastH = mh, lastZoom = app.zoom, lastMod = mod;
    lastItems = items;
    std::vector<render::RectDraw>& out = lastRects;
    out.clear();
    auto shape = [&](RECT box, int r, uint32_t color, float width, bool dashed) {
        if (r <= 0) return;
        // (a line about every 6 screen pixels: many units' shapes zoomed out are cheap)
        const double around = 6.2832 * (r + (box.right - box.left + box.bottom - box.top) / 4.0) * app.zoom;
        const int steps = std::clamp(int(around / 6) / 8 * 8, 16, 256);  // (a multiple of 8: the dashes)
        const auto pts = rangeShapeAround({int(box.left), int(box.top), int(box.right), int(box.bottom)}, double(r), steps);
        for (size_t k = 1; k < pts.size(); ++k) {
            if (dashed && (k / 4) % 2) continue;  // dashes of 4 segments
            render::RectDraw l;
            l.x0 = float(pts[k - 1].x), l.y0 = float(pts[k - 1].y), l.x1 = float(pts[k].x), l.y1 = float(pts[k].y);
            l.rgba = color, l.thickness = width, l.line = true;
            out.push_back(l);
        }
    };
    // The sight masks by range (0..11 tiles; sightMask clamps to these), made once.
    static std::vector<bool> masks[12];
    static int maskSize[12] = {};
    // The tiles that a selected unit sees (one area, so many units do not stack) and the tiles in the psi field of a
    // selected Pylon: only in the box of the tiles that the units' masks touch (not the whole map).
    int bx0 = mw, by0 = mh, bx1 = 0, by1 = 0;  // (the box: bx0 <= x < bx1)
    auto grow = [&](int x0, int y0, int x1, int y1) {
        bx0 = std::min(bx0, std::max(0, x0)), by0 = std::min(by0, std::max(0, y0));
        bx1 = std::max(bx1, std::min(mw, x1)), by1 = std::max(by1, std::min(mh, y1));
    };
    for (Item& it : items) {
        if (it.sight > 0) {
            const int r = std::clamp(it.sight, 0, 11);
            if (masks[r].empty()) masks[r] = sightMask(r, maskSize[r]);
            const int n = maskSize[r];
            grow(it.cx / 32 - n / 2, it.cy / 32 - n / 2, it.cx / 32 - n / 2 + n, it.cy / 32 - n / 2 + n);
        }
        if (it.pylon) grow(it.cx / 32 - 8, it.cy / 32 - 5, it.cx / 32 + 8, it.cy / 32 + 5);
    }
    const int bw = std::max(0, bx1 - bx0), bh = std::max(0, by1 - by0);
    static std::vector<uint8_t> seen, psi;
    seen.clear(), psi.clear();
    for (const Item& it : items) {
        const RECT box{LONG(it.l), LONG(it.t), LONG(it.r), LONG(it.b)};
        // Sight: the tiles of the sight mask around the unit's tile.
        if (it.sight > 0) {
            const int r = std::clamp(it.sight, 0, 11), n = maskSize[r];
            const std::vector<bool>& mask = masks[r];
            const int tx0 = it.cx / 32 - n / 2, ty0 = it.cy / 32 - n / 2;
            if (seen.empty()) seen.assign(size_t(bw) * size_t(bh), 0);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    if (mask[size_t(y * n + x)] && tx0 + x >= 0 && ty0 + y >= 0 && tx0 + x < mw && ty0 + y < mh)
                        seen[size_t(ty0 + y - by0) * size_t(bw) + size_t(tx0 + x - bx0)] = 1;
        }
        shape(box, it.acq, 0xF0B446D0u, 1, false);
        shape(box, it.g, 0xEB5A50F0u, 2, false);
        shape(box, it.a, 0x5AAAF0F0u, 2, it.a == it.g);  // the same range as the ground weapon: blue dashes over the red line
        shape({LONG(it.cx), LONG(it.cy), LONG(it.cx + 1), LONG(it.cy + 1)}, it.detect, 0x50E070F0u, 2, true);
        // Psi field (purple): the tiles where a Pylon gives power, 16 by 10 tiles around its center (the game's mask).
        if (it.pylon) {
            static const char* const kPsi[10] = {"0000011111100000", "0011111111111100", "0111111111111110", "1111111111111111", "1111111111111111",
                                                 "1111111111111111", "1111111111111111", "0111111111111110", "0011111111111100", "0000011111100000"};
            if (psi.empty()) psi.assign(size_t(bw) * size_t(bh), 0);
            const int tx0 = it.cx / 32 - 8, ty0 = it.cy / 32 - 5;  // (the Pylon's center is on a tile corner)
            for (int y = 0; y < 10; ++y)
                for (int x = 0; x < 16; ++x)
                    if (kPsi[y][x] == '1' && tx0 + x >= 0 && ty0 + y >= 0 && tx0 + x < mw && ty0 + y < mh)
                        psi[size_t(ty0 + y - by0) * size_t(bw) + size_t(tx0 + x - bx0)] = 1;
        }
    }
    // An area of tiles (the sight, the psi field): its tiles faintly filled (rows of tiles as one rectangle each) and
    // its outline.
    auto area = [&](const std::vector<uint8_t>& tiles, uint32_t fill, uint32_t line) {
        if (tiles.empty()) return;
        auto at = [&](int x, int y) { return x >= bx0 && y >= by0 && x < bx1 && y < by1 && tiles[size_t(y - by0) * size_t(bw) + size_t(x - bx0)]; };
        auto edge = [&](float x0, float y0, float x1, float y1) {
            render::RectDraw l;
            l.x0 = x0, l.y0 = y0, l.x1 = x1, l.y1 = y1, l.rgba = line, l.thickness = 1, l.line = true;
            out.push_back(l);
        };
        for (int y = by0; y < by1; ++y)
            for (int x = bx0; x < bx1; ++x) {
                if (!at(x, y)) continue;
                int run = x;
                while (at(run + 1, y)) ++run;
                out.push_back({float(x * 32), float(y * 32), float((run + 1) * 32), float(y * 32 + 32), fill, true});
                for (int k = x; k <= run; ++k) {
                    const float X = float(k * 32), Y = float(y * 32);
                    if (!at(k, y - 1)) edge(X, Y, X + 32, Y);
                    if (!at(k, y + 1)) edge(X, Y + 32, X + 32, Y + 32);
                }
                edge(float(x * 32), float(y * 32), float(x * 32), float(y * 32 + 32));
                edge(float((run + 1) * 32), float(y * 32), float((run + 1) * 32), float(y * 32 + 32));
                x = run;
            }
    };
    area(psi, 0xA070FF1Cu, 0xB080FFC0u);
    area(seen, 0xE0E0F014u, 0xFFE040C0u);
    s.rects.insert(s.rects.end(), out.begin(), out.end());
}


// New graphics for preplaced units (Project page, R8): the game makes the preplaced units before the .dat edits run,
// so they keep the game's graphics. With the option on, Build adds code that makes them again, and the map shows them
// with the edited graphics.
void applyProjectGraphics(UnitSprites& sp);  // the project's graphics edits on `sp` (core/project_build.inc)

// The map view's unit graphics: the edited ones while the option is on, else the game's.
void syncMapUnitGraphics() {
    AppState& app = shellState();
    static const Editor* edFor = nullptr;
    static long long key = -1;
    static std::set<const Editor*> edited;  // editors that show edited graphics (or did)
    if (!app.ed) return;
    const EudProject* pr = activeProject();
    const bool on = pr && pr->remakeUnits();
    if (on) dataLoadForProject();
    const long long now = on ? (long long)(uintptr_t(pr) & 0xFFFFFF) << 32 | unsigned(dataState().edits) : -2;
    if (edFor == app.ed.get() && key == now) return;
    edFor = app.ed.get(), key = now;
    UnitSprites& sp = app.ed->unitSprites();
    gGameUnitImage.clear(), gGameSubunitImage.clear();
    if (on) {
        static std::unique_ptr<UnitSprites> game;
        std::string err;
        if (!game && !(game = std::make_unique<UnitSprites>())->load(app.data, err)) game.reset();
        for (uint16_t t = 0; game && t < app.ed->unitTypes().size(); ++t)
            gGameUnitImage.push_back(game->imageFor(t)), gGameSubunitImage.push_back(game->subunitImage(t));
        applyProjectGraphics(sp);
        edited.insert(edFor);
    } else if (edited.erase(edFor)) {
        std::string err;
        sp.load(app.data, err);
    } else {
        return;
    }
    gAnims.clear();  // the animations of the old images
    shellRendererReloadImages();  // their glows (draw function, remapping)
    ++gUnitGraphics;  // the Units palette's pictures
}


// The preview: the picked tile in the brush's footprint at the mouse, with its outline; a paste: the copied tiles.
// The Circle's and the Diamond's brush: only the outline. gTilesPreview: the last preview's tiles and outline lines.
struct TilesPreviewCount {
    size_t tiles = 0, lines = 0;
} gTilesPreview;
void tilesScene(render::Scene& s, float wx, float wy) {
    AppState& app = shellState();
    gTilesPreview = {};
    if (!app.ed || !app.ed->isOpen()) return;
    tilesFresh();
    Editor& ed = *app.ed;
    int cx, cy;
    tilesAt(wx, wy, cx, cy);
    std::vector<std::pair<int, uint16_t>> cells;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    auto add = [&](int x, int y, uint16_t v) {
        if (x < 0 || y < 0 || x >= ed.width() || y >= ed.height()) return;
        cells.push_back({y * ed.width() + x, v});
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    };
    // The mirrors' images of the brush's tiles (tilesMirrored): ghosts too, outside the brush's frame.
    auto addMirrors = [&](const std::vector<Editor::TileAt>& brush) {
        const std::vector<Editor::TileAt> all = tilesMirrored(brush);
        for (size_t i = brush.size(); i < all.size(); ++i) cells.push_back({all[i].y * ed.width() + all[i].x, all[i].value});
    };
    const int tool = currentTool();
    if (tool == TSelect) {
        if (!app.terrainPasting || !ed.hasTileClipboard()) return;
        tilesPasteAt(wx, wy, cx, cy);  // (it may hang over the map's edge: the tiles on the map show)
        const int cw = ed.tileClipboardWidth();
        for (int y = 0; y < ed.tileClipboardHeight(); ++y)
            for (int x = 0; x < cw; ++x) add(cx + x, cy + y, ed.tileClipboard()[size_t(y) * cw + x]);
    } else if (tool == TBrush && tilesStampOn() && !(appState().drag == Drag::Tiles && gTiles->scatter)) {  // the stamp at the mouse
        int sx, sy;
        tilesStampOrigin(cx, cy, sx, sy);
        const std::vector<Editor::TileAt> stamp = tilesStampCells(sx, sy);
        for (const auto& t : stamp) add(t.x, t.y, t.value);
        addMirrors(stamp);
    } else if (gTiles->solid && (tool == TBrush || tool == TSpray)) {  // Solid: the outline of the brush's rects (each rect at random)
        if (app.palette <= 0) return;
        std::vector<Editor::TileAt> foot;
        for (auto [x, y] : solidBrushTiles(cx, cy))  // (the Brush's tiles; the spray's rects are whole)
            if (tool == TBrush && x >= 0 && y >= 0 && x < ed.width() && y < ed.height()) foot.push_back({x, y, 0});
        if (tool == TSpray)
            for (auto [rx, ry] : solidFootprint(cx, cy))
                for (int x = 2 * rx; x <= 2 * rx + 1; ++x)
                    if (x >= 0 && ry >= 0 && x < ed.width() && ry < ed.height()) foot.push_back({x, ry, 0});
        std::vector<std::pair<int, int>> tiles;
        for (const auto& t : solidMirrored(foot)) tiles.push_back({t.x, t.y});  // (and the mirrors' images)
        ghostTiles.clear();
        const size_t before = s.rects.size();
        tileOutline(s, tiles, tool == TSpray ? 0xFFD040E0u : 0x40FF40FFu);
        gTilesPreview.tiles = tiles.size(), gTilesPreview.lines = s.rects.size() - before;
        return;
    } else if (tool == TBrush || tool == TSpray) {
        if (gTiles->picked < 0 || !ed.tileset().groupFor(uint16_t(gTiles->picked))) return;
        if (app.brushShape != Brush::Square) {  // the Circle and the Diamond: their outline on the tile edges (#135, #136)
            std::vector<std::pair<int, int>> tiles;
            std::vector<Editor::TileAt> foot;
            for (auto [x, y] : tilesFootprint(cx, cy))
                if (x >= 0 && y >= 0 && x < ed.width() && y < ed.height()) foot.push_back({x, y, 0});
            for (const auto& t : tilesMirrored(foot)) tiles.push_back({t.x, t.y});  // (and the mirrors' images)
            ghostTiles.clear();
            const size_t before = s.rects.size();
            tileOutline(s, tiles, tool == TSpray ? 0xFFD040E0u : 0x40FF40FFu);
            gTilesPreview.lines = s.rects.size() - before;
            return;
        }
        std::vector<Editor::TileAt> foot;
        for (auto [x, y] : tilesFootprint(cx, cy))
            if (tool == TBrush) add(x, y, uint16_t(gTiles->picked)), foot.push_back({x, y, uint16_t(gTiles->picked)});
            else x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);  // the spray: its area only
        addMirrors(foot);
    } else {
        return;
    }
    gTilesPreview.tiles = cells.size(), gTilesPreview.lines = x0 <= x1 ? 1 : 0;
    ghostTiles = std::move(cells);
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, ed.width() - 1), y1 = std::min(y1, ed.height() - 1);
    if (x0 <= x1) outline(s, Rect{x0 * 32, y0 * 32, (x1 + 1) * 32, (y1 + 1) * 32}, 0x40FF40FFu, 1.5f);
}
