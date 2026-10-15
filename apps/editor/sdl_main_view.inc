// The SDL shell's root view (research/ports/MAC-BUILD-PLAN.md step 6): the main window's panes, placed by computeLayout
// (core/layout.inc). The panes that are views in the core are shown (the menu bar, the document tabs, the workspace rail,
// the status bar, the minimap, the map view); the others have no view on this system yet and show a dim "Not on this
// system yet". The map view paints the frame the renderer drew into its own GPU texture (SdlHost::setFrameHook, as
// apps/next/main.cpp does) and forwards its mouse to the core's map input. Included by main_sdl.cpp after the core.

#include "menu.h"

namespace kit = stardraft::kit;

// The editor's state: the core's (AppState), and the program's: the game data, the renderer, the host and the root.
AppState gState;
render::Renderer* gRenderer = nullptr;
SDL_GPUDevice* gDevice = nullptr;
kit_sdl::SdlHost* gHost = nullptr;
bool gDataOk = false;        // the game data is open (maps need it)
std::string gDataError;      // why it is not (shown in the map view)
unsigned gDpi = 96;          // the window's dpi (scale() gives device pixels for it)
struct MainView;
MainView* gRoot = nullptr;

// The map's texture: the renderer draws the open map into it each frame, and the map view shows it.
SDL_GPUTexture* gMapTex = nullptr;
int gMapW = 0, gMapH = 0;
const Editor* gMapEd = nullptr;  // the editor the renderer's sources are set for
uint64_t gTerrainRev = ~0ull;    // ... and the terrain revision they have
bool gMapFitPending = false;     // a map was opened: the camera fits it on the next frame

void releaseMapTexture() {
    if (gMapTex && gDevice) SDL_ReleaseGPUTexture(gDevice, gMapTex);
    gMapTex = nullptr, gMapW = gMapH = 0;
    gMapEd = nullptr;
}

// Work for the UI thread, from shellPost (any thread): run by the frame hook, in order.
std::mutex gPostMutex;
std::vector<std::function<void()>> gPosted;

void runPosted() {
    std::vector<std::function<void()>> todo;
    {
        std::lock_guard<std::mutex> lock(gPostMutex);
        todo.swap(gPosted);
    }
    for (auto& fn : todo) fn();
}

// The scene of the open map: the camera and the units (the Windows program's scene has more; see the shell's notes).
render::Scene buildMapScene() {
    render::Scene scene;
    scene.camera = {gState.camX, gState.camY, gState.zoom};
    Editor& ed = *gState.ed;
    const UnitTypes& types = ed.unitTypes();
    UnitSprites& sp = ed.unitSprites();
    for (const Unit& u : ed.units()) {
        if (u.type >= types.size()) continue;
        render::SpriteDraw s;
        s.image = sp.imageFor(u.type);
        if (s.image < 0) continue;
        s.frame = sp.defaultFrame(u.type, s.image, s.flip);
        s.x = u.x, s.y = u.y, s.owner = u.owner;
        s.air = types[u.type].is(UnitFlag::Flyer) && !types[u.type].is(UnitFlag::Building);
        scene.sprites.push_back(s);
    }
    return scene;
}

// The map into its texture (w x h pixels), in the command buffer the host submits before the windows are painted.
void drawMapFrame(SDL_GPUCommandBuffer* cmd, int w, int h) {
    if (!gRenderer || !gState.ed || !gState.ed->isOpen() || w <= 0 || h <= 0) return;
    if (!gMapTex || gMapW != w || gMapH != h) {
        releaseMapTexture();
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = gRenderer->colorFormat();
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = uint32_t(w), ti.height = uint32_t(h), ti.layer_count_or_depth = 1, ti.num_levels = 1;
        gMapTex = SDL_CreateGPUTexture(gDevice, &ti);
        if (!gMapTex) return;
        gMapW = w, gMapH = h;
    }
    Editor& ed = *gState.ed;
    clampCamera();
    if (gMapEd != &ed || gTerrainRev != ed.terrainRevision()) {
        gRenderer->setSources(&gState.data, &ed.unitSprites(), &ed.tileset(), ed.era());
        gRenderer->updateTerrain(ed.terrain());
        gMapEd = &ed, gTerrainRev = ed.terrainRevision();
    }
    // The players' colors: the map's own for players 1 to 8, the fixed ones for the rest (as the Windows program).
    static const uint32_t kOtherTeams[12] = {0xF40404, 0x0C48CC, 0x2CB494, 0x88409C, 0xF88C14, 0x703014,
                                             0xCCE0D0, 0xFCFC38, 0x088008, 0xFCFC7C, 0xECC4B0, 0x4068D4};
    uint32_t team[12];
    for (int i = 0; i < 12; ++i) team[i] = i < 8 ? ed.playerRgb(i) : kOtherTeams[i];
    gRenderer->setPlayerColors(team);
    gRenderer->drawTo(cmd, gMapTex, uint32_t(w), uint32_t(h), buildMapScene());
}

// The panes that have no view on this system yet.
struct NotHereView final : draw::View {
    void paint(draw::Canvas& c) override {
        c.text(draw::Rect(0, 0, width(), height()), shellTr(L"Not on this system yet"), kDim, draw::kCenter | draw::kMiddle | draw::kWrap);
    }
};

// The map view: the texture of the map (drawn by the frame hook) and the core's map input.
struct MapView final : draw::View {
    bool panning = false;  // the middle button (or a drag with it) pans
    bool rightStroke = false;  // a Shift stroke paints with the second terrain; the palette is the first one again after it
    int savedPalette = -1;
    draw::Point last;
    bool open() const { return gState.ed && gState.ed->isOpen(); }
    void paint(draw::Canvas& c) override {
        const draw::Rect all(0, 0, width(), height());
        c.fill(all, kBg);
        if (open() && gMapTex && gMapW == width() && gMapH == height()) {
            c.externalImage(gMapTex, all);
            return;
        }
        const std::wstring text = open() ? std::wstring(L"") : gDataOk ? shellTr(L"No map is open. Start with a map file: stardraft_editor <map>") : widen(gDataError);
        if (!text.empty()) c.text(all, text, kDim, draw::kCenter | draw::kMiddle | draw::kWrap);
    }
    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick:
            if (e.button == 2) {  // middle: pan
                panning = true, last = e.pt;
                if (draw::Host* h = host()) h->capture(*this, true);
                return true;
            }
            const bool second = e.button == 1 && (e.mods & draw::kShift) && !(e.mods & (draw::kCtrl | draw::kAlt)) && open() && secondPaints();
            if (second) {  // Shift + left: paints with the second terrain (isometric terrain, as the Windows program)
                rightStroke = true, savedPalette = gState.palette;
                gState.palette = gState.palette2;
            } else if (!open()) {
                return false;
            }
            if (draw::Host* h = host()) h->capture(*this, true);
            gState.mouseX = e.pt.x, gState.mouseY = e.pt.y, gState.mouseIn = true;
            mapMouseDown(gState, e.pt.x, e.pt.y, second ? (e.mods & ~draw::kShift) : e.mods);  // (the Windows program drops Shift here too)
            invalidate();
            return true;
        case draw::MouseKind::Move:
            gState.mouseX = e.pt.x, gState.mouseY = e.pt.y, gState.mouseIn = true;
            if (panning) {
                gState.camX -= float(e.pt.x - last.x) / gState.zoom, gState.camY -= float(e.pt.y - last.y) / gState.zoom;
                last = e.pt;
                clampCamera();
                invalidate();
                return true;
            }
            if (!open()) return false;
            mapMouseMove(gState, e.pt.x, e.pt.y);
            invalidate();
            return true;
        case draw::MouseKind::Up:
            if (panning) {
                panning = false;
                if (draw::Host* h = host()) h->capture(*this, false);
                return true;
            }
            if (rightStroke && e.button == 1) {  // the end of a right stroke: the left button's terrain again
                rightStroke = false;
                mapMouseUp(gState, e.pt.x, e.pt.y, e.mods);
                gState.palette = savedPalette;
                gState.dirty = true;
                if (draw::Host* h = host()) h->capture(*this, false);
                invalidate();
                return true;
            }
            if (e.button == 1 || !open()) return false;
            mapMouseUp(gState, e.pt.x, e.pt.y, e.mods);
            if (draw::Host* h = host()) h->capture(*this, false);
            invalidate();
            return true;
        case draw::MouseKind::Wheel: {  // zoom at the mouse: the point under it stays under it (a sideways swipe does nothing)
            if (!open() || e.wheel == 0 || e.horizontal) return false;
            const float wx = gState.camX + e.pt.x / gState.zoom, wy = gState.camY + e.pt.y / gState.zoom;
            gState.zoom = std::clamp(gState.zoom * std::pow(1.2f, e.wheel), 1.0f / 16, 4.0f);
            gState.camX = wx - e.pt.x / gState.zoom, gState.camY = wy - e.pt.y / gState.zoom;
            clampCamera();
            invalidate();
            return true;
        }
        case draw::MouseKind::Leave:
            gState.mouseIn = false;
            invalidate();
            return true;
        default: return false;
        }
    }
    draw::Cursor cursor(draw::Point) override { return panning ? draw::Cursor::SizeAll : draw::Cursor::Arrow; }
};

// The shortcuts and the menus (sdl_menus.inc).
bool shortcutRun(const draw::KeyEvent& e);
void menusIfDirty();

// The main window: the menu bar, the document tabs, the workspace rail, the status bar, the map view with its minimap and
// the panes that are not on this system yet, placed by computeLayout.
struct MainView final : draw::View {
    static constexpr int kNotHere = 11;  // the panes of the map workspace that have no view yet (see relayout)
    kit::MenuBar menus;
    TabsView tabs;
    RailView rail;
    StatusView status{gState};
    MapView map;
    MinimapView minimap;
    std::vector<std::unique_ptr<NotHereView>> notHere;

    MainView() {
        add(menus, draw::Rect(0, 0, 0, 0));
        add(tabs, draw::Rect(0, 0, 0, 0));
        add(rail, draw::Rect(0, 0, 0, 0));
        add(status, draw::Rect(0, 0, 0, 0));
        add(map, draw::Rect(0, 0, 0, 0));
        add(minimap, draw::Rect(0, 0, 0, 0));
        for (int i = 0; i < kNotHere; ++i) {
            notHere.push_back(std::make_unique<NotHereView>());
            add(*notHere.back(), draw::Rect(0, 0, 0, 0));
        }
        relayout();
    }
    ~MainView() override {  // (the children go before the members do: their views unlink here)
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }

    void resized() override { relayout(); }
    // The menus' keys run when no view takes the key (the focused view first, then up to here).
    bool key(const draw::KeyEvent& e) override { return shortcutRun(e); }

    // Places the panes for the window's size and the state (computeLayout; the same rules as the Windows layout).
    void relayout() {
        if (draw::Host* h = host()) gDpi = unsigned(std::lround(96.0 * double(h->scale())));
        LayoutInput in;
        in.dpi = int(gDpi);
        in.clientW = width(), in.clientH = height();
        in.statusH = scale(24);
        in.barH = scale(28);             // the menu bar
        in.tabsH = tabStripHeight();     // the document tabs
        in.railW = shellHasRail() ? wsRailWidth() : 0;
        in.mode = gState.mode;
        in.mapWs = true;
        in.toolStrip = [](int) { return 0; };  // (the tool strip is not on this system yet)
        in.mapOpen = gState.ed && gState.ed->isOpen();
        if (in.mapOpen) in.mapW = gState.ed->width(), in.mapH = gState.ed->height();
        in.minimapHidden = gDock.minimapHidden;
        in.minimapRight = gDock.minimapRight;
        in.leftW = gDock.leftW, in.rightW = gDock.rightW;
        std::copy(gDock.toolsRight, gDock.toolsRight + kModeCount, in.toolsRight);
        const MainLayout L = computeLayout(in);
        // Which panes show in the mode, as the Windows program shows them (main.cpp's SetMode): the list and the filter
        // in Locations; the palette and the search in the other modes; the owner in Units and Sprites; the section box
        // in Units, Doodads and Sprites. The layout places the rows that share a place, so only these are shown.
        const Mode m = gState.mode;
        const bool locs = m == Mode::Locations;
        const bool owners = m == Mode::Units || m == Mode::Sprites;
        const bool sections = m == Mode::Units || m == Mode::Doodads || m == Mode::Sprites;
        apply(menus, L.topBar, true);
        apply(tabs, L.tabs, true);
        apply(rail, L.wsRail, shellHasRail());
        apply(status, L.status, true);
        apply(map, L.view, true);
        apply(minimap, L.minimap, L.minimap.visible);
        apply(*notHere[0], L.tool, true);
        apply(*notHere[1], oneRow(L.owner), owners);       // (a closed owner list is one row high)
        apply(*notHere[2], oneRow(L.sectionBox), sections);
        apply(*notHere[3], L.search, !locs);
        apply(*notHere[4], L.viewBtn, !locs);
        apply(*notHere[5], L.filter, locs);
        apply(*notHere[6], L.palette, !locs);
        apply(*notHere[7], L.list, locs);
        apply(*notHere[8], L.panelTabActive, true);
        apply(*notHere[9], L.panelTabOther, true);
        apply(*notHere[10], L.palette2, true);
        invalidate();
    }
    // A pane whose drop-down is tall in the layout (the owner, the section box) is one row high when closed.
    PaneSlot oneRow(PaneSlot s) const {
        s.rc.bottom = s.rc.top + scale(24);
        return s;
    }
    // Shown only when the pane is placed (the layout placed it) and the mode wants it.
    static void apply(draw::View& v, const PaneSlot& s, bool shown) {
        if (s.placed) v.setFrame(draw::Rect(s.rc));
        v.setVisible(s.placed && shown);
    }
};

// The camera fits the open map in the map view (the whole map, as far as the view allows; 1/16 to 4 times).
void fitMapToView(float w, float h) {
    const float mw = gState.ed->width() * 32.0f, mh = gState.ed->height() * 32.0f;
    gState.zoom = std::clamp(std::min(w / mw, h / mh), 1.0f / 16, 4.0f);
    gState.camX = (mw - w / gState.zoom) / 2, gState.camY = (mh - h / gState.zoom) / 2;
}

// The frame hook's map work (the window's map view size, in pixels).
void drawMapOfRoot(SDL_GPUCommandBuffer* cmd) {
    if (!gRoot) return;
    menusIfDirty();
    const int w = gRoot->map.width(), h = gRoot->map.height();
    if (gMapFitPending && gState.ed && gState.ed->isOpen() && w > 0 && h > 0) {
        fitMapToView(float(w), float(h));
        gMapFitPending = false;
    }
    drawMapFrame(cmd, w, h);
}
