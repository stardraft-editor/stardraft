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

// The start page and the Files page (sdl_start.inc, sdl_files.inc): defined after this file's root view is read.
extern StartView gStartView;
extern FilesPage gFilesPage;  // (core/views_files.inc)
extern NewPageView gNewView;  // the New page (sdl_new_page.inc)
extern bool gNewOpen;         // the New page is open in the Map workspace (it shows in place of the start page)

// The panes that have no view on this system yet.
struct NotHereView final : draw::View {
    void paint(draw::Canvas& c) override {
        c.text(draw::Rect(0, 0, width(), height()), shellTr(L"Not on this system yet"), kDim, draw::kCenter | draw::kMiddle | draw::kWrap);
    }
};

// The workspaces' views on this system (the workspace area, the map's place, shows the one of the workspace that is shown).
// Map has the map's panes; a workspace with no view here shows the "Not on this system yet" view. Another lane adds its
// view with registerWorkspaceView (before or after the window exists; the window adds it then).
std::map<Workspace, draw::View*> gWorkspaceViews;
struct MainView;
bool sdlMapPageTyping();  // a field of the open map page has the keyboard (sdl_map_pages.inc)
void registerWorkspaceView(Workspace w, draw::View& v);

#include "sdl_pictures.inc"  // the palette's pictures (the terrain, doodad, unit and sprite art from the game data)
#include "sdl_panels.inc"  // the side panels: the palette, the owner, the filter, the search, the section box, the second palette

// The map's dialogs and its menu (sdl_map_dialogs.inc): a double click, Enter and the right button.
bool mapDoubleClickSdl(int x, int y);
bool mapEnterSdl();
void mapRightClickSdl(draw::View& map, draw::Point at);

// The map view: the texture of the map (drawn by the frame hook) and the core's map input.
struct MapView final : draw::View {
    bool rightDown = false;  // the right button went down on the map: its release opens the map's menu
    bool panning = false;  // the middle button (or a drag with it) pans
    bool rightStroke = false;  // a Shift+left stroke paints with the second terrain; the palette is the first one again after it
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
        case draw::MouseKind::DoubleClick: {
            focus();  // the map takes the keyboard back from a text field or a list (the map is not focusable itself)
            if (e.button == 2) {  // middle: pan
                panning = true, last = e.pt;
                if (draw::Host* h = host()) h->capture(*this, true);
                return true;
            }
            // A double click: a location's or a unit's properties (as the Windows map's).
            if (e.kind == draw::MouseKind::DoubleClick && e.button == 0 && mapDoubleClickSdl(e.pt.x, e.pt.y)) return true;
            // The right button: its release opens the map's menu (or stops placing), as on Windows.
            if (e.button == 1 && open()) {
                rightDown = gState.drag == Drag::None;
                return true;
            }
            // Shift with the left button paints with the second terrain (isometric terrain, as the Windows program).
            const bool second = e.button == 0 && (e.mods & draw::kShift) && !(e.mods & (draw::kCtrl | draw::kAlt)) &&
                                gState.drag == Drag::None && open() && secondPaints();
            if (second) {
                rightStroke = true, savedPalette = gState.palette;
                gState.palette = gState.palette2;
            } else if (e.button == 1 || !open()) {
                return false;
            }
            if (draw::Host* h = host()) h->capture(*this, true);
            gState.mouseX = e.pt.x, gState.mouseY = e.pt.y, gState.mouseIn = true;
            mapMouseDown(gState, e.pt.x, e.pt.y, second ? (e.mods & ~draw::kShift) : e.mods);  // (the Windows program drops Shift here too)
            invalidate();
            return true;
        }
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
            if (rightStroke && e.button == 0) {  // the end of a second-terrain stroke: the first terrain again
                rightStroke = false;
                mapMouseUp(gState, e.pt.x, e.pt.y, e.mods);
                gState.palette = savedPalette;
                gState.dirty = true;
                if (draw::Host* h = host()) h->capture(*this, false);
                invalidate();
                return true;
            }
            if (e.button == 1 && std::exchange(rightDown, false) && open()) {
                mapRightClickSdl(*this, e.pt);
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
    kit::MenuBar menus;
    TabsView tabs;
    RailView rail;
    StatusView status{gState};
    MapView map;
    MinimapView minimap;
    ModeBarView modes;  // the modes' segmented control, at the top bar's right
    NotHereView notHere;  // the workspace area of a workspace with no view on this system

    MainView() {
        initPanels();
        add(menus, draw::Rect(0, 0, 0, 0));
        add(tabs, draw::Rect(0, 0, 0, 0));
        add(rail, draw::Rect(0, 0, 0, 0));
        add(status, draw::Rect(0, 0, 0, 0));
        add(map, draw::Rect(0, 0, 0, 0));
        add(minimap, draw::Rect(0, 0, 0, 0));
        add(gToolStripView, draw::Rect(0, 0, 0, 0));  // the tool strip (core/views_toolstrip.inc)
        add(modes, draw::Rect(0, 0, 0, 0));
        add(gPanelTabsSdl[0], draw::Rect(0, 0, 0, 0));
        add(gPanelTabsSdl[1], draw::Rect(0, 0, 0, 0));
        add(gOwnerBox, draw::Rect(0, 0, 0, 0));
        add(gFilterSdl, draw::Rect(0, 0, 0, 0));
        add(gSearchSdl, draw::Rect(0, 0, 0, 0));
        add(gSectionDropSdl, draw::Rect(0, 0, 0, 0));
        add(gViewBtnSdl, draw::Rect(0, 0, 0, 0));
        add(gPalList, draw::Rect(0, 0, 0, 0));
        add(gLocList, draw::Rect(0, 0, 0, 0));
        add(gPal2List, draw::Rect(0, 0, 0, 0));
        add(notHere, draw::Rect(0, 0, 0, 0));
        add(gStartView, draw::Rect(0, 0, 0, 0));  // the start page (sdl_start.inc): no map open
        add(gNewView, draw::Rect(0, 0, 0, 0));    // the New page (sdl_new_page.inc): File > New project, a card
        for (auto& [w, v] : gWorkspaceViews) add(*v, draw::Rect(0, 0, 0, 0));
        relayout();
    }
    ~MainView() override {  // (the children go before the members do: their views unlink here)
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }

    void resized() override { relayout(); }
    // The menus' keys run when no view takes the key (the focused view first, then up to here). A search box takes the
    // letters and digits it types: the map's one-letter shortcuts (T, U, G, Q...) wait while it has the keyboard.
    bool key(const draw::KeyEvent& e) override {
        const int k = int(e.key);
        const bool typed = (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9');
        const bool typing = gSearchSdl.field().hasFocus() || gFilterSdl.field().hasFocus() || gFilesPage.typing() || sdlMapPageTyping() ||
                            typingFocusIn(*this);  // (a kit text field of a view: the New page, Program options)
        if (typing && typed && !(e.mods & (draw::kCtrl | draw::kAlt))) return false;
        // Enter on the map: the selected location's or unit's properties (the Windows map's key).
        if (e.down && !e.repeat && e.key == draw::Key::Enter && !e.mods && !typing && map.shown() && mapEnterSdl()) return true;
        return shortcutRun(e);
    }

    // The modes' bar: at the top bar's right, after the menus (the menus take the left).
    RECT modeBarRect() const {
        const int bw = scale(112), bh = scale(26), by = (scale(28) - bh) / 2;
        const int x1 = width() - scale(8), x0 = x1 - kModeCount * bw;
        return RECT{x0, by, x1, by + bh};
    }

    // Places the panes for the window's size and the state (computeLayout; the same rules as the Windows layout).
    void relayout() {
        if (draw::Host* h = host()) gDpi = unsigned(std::lround(96.0 * double(h->scale())));
        syncKitThemeTokens(int(gDpi));
        LayoutInput in;
        in.dpi = int(gDpi);
        in.clientW = width(), in.clientH = height();
        in.statusH = scale(24);
        in.barH = scale(28);             // the menu bar
        in.tabsH = tabStripHeight();     // the document tabs
        in.railW = shellHasRail() ? wsRailWidth() : 0;
        in.mode = gState.mode;
        // The map's panes show in the Map workspace with a map open; with no map, the Map workspace shows the start page.
        const bool mapOpen = gState.ed && gState.ed->isOpen();
        // The New page is open in the Map workspace: it shows in place of the start page or the map (as a tab does).
        const bool newShown = gNewOpen && gWorkspace == Workspace::Map;
        const bool mapWs = gWorkspace == Workspace::Map && mapOpen && !newShown;
        const bool startShown = gWorkspace == Workspace::Map && !mapOpen && !newShown;
        in.mapWs = mapWs;
        in.toolStrip = [](int width) { return toolStripHeight(width, false); };
        in.modeBarH = 0, in.modeBar = modeBarRect();  // (the modes are in the top bar)
        in.mapOpen = mapOpen;
        if (in.mapOpen) in.mapW = gState.ed->width(), in.mapH = gState.ed->height();
        in.minimapHidden = gDock.minimapHidden;
        in.minimapRight = gDock.minimapRight;
        in.leftW = gDock.leftW, in.rightW = gDock.rightW;
        std::copy(gDock.toolsRight, gDock.toolsRight + kModeCount, in.toolsRight);
        in.lastOnSide[0] = gLastOnSide[0], in.lastOnSide[1] = gLastOnSide[1];
        in.hasPanelTab[0] = in.hasPanelTab[1] = true;
        in.hasSectionBox = true;
        in.hasPal2 = true;
        in.pal2Shown = gPal2Shown, in.pal2Mode = gPal2Mode;
        const MainLayout L = computeLayout(in);
        // The state the layout sets (as the Win32 layout): the sides' tabs, the second palette's mode and its shown state.
        gPanelTab[0] = L.panelTab[0], gPanelTab[1] = L.panelTab[1];
        gPal2Shown = L.palette2.visible;
        if (gPal2Shown) {
            gPal2Mode = L.secondMode;
            if (L.palette2Changed) rebuildSecondRows();
        }
        // Which panes show in the mode, as the Windows program shows them (main.cpp's SetMode): the list and the filter
        // in Locations; the palette and the search in the other modes; the owner in Units and Sprites; the section box
        // in Units, Doodads and Sprites. The layout places the rows that share a place, so only these are shown.
        const Mode m = gState.mode;
        const bool locs = m == Mode::Locations;
        const bool owners = m == Mode::Units || m == Mode::Sprites;
        const bool sections = m == Mode::Units || m == Mode::Doodads || m == Mode::Sprites;
        const bool toolsRight = gDock.toolsRight[int(m)];
        apply(menus, L.topBar, true);
        apply(tabs, L.tabs, true);
        apply(rail, L.wsRail, shellHasRail());
        apply(status, L.status, true);
        apply(map, L.view, mapWs);
        apply(minimap, L.minimap, L.minimap.visible && mapWs);
        apply(modes, L.modeBar, mapWs);
        apply(gToolStripView, L.tool, mapWs);
        apply(gPanelTabsSdl[toolsRight ? 1 : 0], L.panelTabActive, L.panelTabActive.visible && mapWs);
        apply(gPanelTabsSdl[toolsRight ? 0 : 1], L.panelTabOther, L.panelTabOther.visible && mapWs);
        apply(gOwnerBox, oneRow(L.owner), owners && mapWs);       // (a closed owner list is one row high)
        apply(gSectionDropSdl, oneRow(L.sectionBox), sections && mapWs);
        apply(gSearchSdl, L.search, !locs && mapWs);
        apply(gViewBtnSdl, L.viewBtn, !locs && mapWs);
        apply(gFilterSdl, L.filter, locs && mapWs);
        apply(gPalList, L.palette, !locs && mapWs);
        apply(gLocList, L.list, locs && mapWs);
        apply(gPal2List, L.palette2, L.palette2.visible && mapWs);
        // The workspace area: the registered view of the workspace shown, else the "Not on this system yet" view.
        const auto found = gWorkspaceViews.find(gWorkspace);
        draw::View* wsView = mapWs || found == gWorkspaceViews.end() ? nullptr : found->second;
        for (auto& [w, v] : gWorkspaceViews) apply(*v, L.view, v == wsView);
        apply(gStartView, L.view, startShown);
        apply(gNewView, L.view, newShown);
        apply(notHere, L.view, !mapWs && !wsView && !startShown && !newShown);
        // A hidden pane keeps no keyboard: when a switch hid the focused one (a search box, the palette's list), the
        // window's root takes the focus, so the keys reach the menus and shortcuts, not a box nobody sees.
        for (View* c : children())
            if (!c->shown() && hasFocusIn(*c)) {
                focus();
                break;
            }
        invalidate();
    }
    // The focused view of the tree types letters (a text field).
    static bool typingFocusIn(const View& v) {
        if (v.hasFocus()) return v.typesText();
        for (const View* c : v.children())
            if (typingFocusIn(*c)) return true;
        return false;
    }
    static bool hasFocusIn(const View& v) {
        if (v.hasFocus()) return true;
        for (const View* c : v.children())
            if (hasFocusIn(*c)) return true;
        return false;
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

void invalidateMapView() {
    if (gRoot) gRoot->map.invalidate();
}

// A workspace's view is added to the window (now when the window exists, else when it is made).
void registerWorkspaceView(Workspace w, draw::View& v) {
    gWorkspaceViews[w] = &v;
    if (gRoot) {
        gRoot->add(v, draw::Rect(0, 0, 0, 0));
        gRoot->relayout();
    }
}

// The frame hook's map work (the window's map view size, in pixels).
void drawMapOfRoot(SDL_GPUCommandBuffer* cmd) {
    if (!gRoot) return;
    menusIfDirty();
    const int w = gRoot->map.width(), h = gRoot->map.height();
    if (gFitPending && w > 0 && h > 0) fitMap(), updateStatus();  // (the map opened before the view had its size: the zoom it fit)
    if (gWorkspace == Workspace::Map) drawMapFrame(cmd, w, h);  // (the other workspaces hide the map: no frame)
}
