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
LoadingView gLoadingView;  // the map's loading view (core/views_loading.inc): over the workspace while a map opens
bool sdlPieClickAway();  // (the pie palette that shows: a click on the map closes it, below)
bool gTilesShown = false;  // the Tiles panel shows (Rectangle mode, in the Map workspace with a map: MainView::relayout)

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

// Work for the UI thread, from shellPost (any thread): run by the frame hook, in order. The queue is never freed: a
// download thread that ends while the program stops can still post to it (its work then does not run).
struct PostQueue {
    std::mutex mutex;
    std::vector<std::function<void()>> posted;
};
PostQueue& postQueue() {
    static PostQueue* const q = new PostQueue;
    return *q;
}

void runPosted() {
    std::vector<std::function<void()>> todo;
    {
        PostQueue& q = postQueue();
        std::lock_guard<std::mutex> lock(q.mutex);
        todo.swap(q.posted);
    }
    for (auto& fn : todo) fn();
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
    // The terrain when it changed, or a layer (View > Layers: no terrain, or no doodads' tiles) went on or off.
    // (and the creep when it changed: View > Layers > Creep, core/creep_layer.inc)
    static MapLayers uploaded;
    static uint64_t uploadedCreep = 0;
    const uint64_t creepRev = creepRevision(ed);
    if (gMapEd != &ed || gTerrainRev != ed.terrainRevision() || uploaded.terrain != gLayers.terrain ||
        uploaded.doodads != gLayers.doodads || uploaded.creep != gLayers.creep || uploadedCreep != creepRev) {
        gRenderer->setSources(&ed.gameData(), &ed.unitSprites(), &ed.tileset(), ed.era());
        if (gLayers.terrain && gLayers.doodads && !gLayers.creep) gRenderer->updateTerrain(ed.terrain());
        else gRenderer->updateTerrain(terrainWithLayers(ed));  // (core/map_scene.inc)
        gMapEd = &ed, gTerrainRev = ed.terrainRevision(), uploaded = gLayers, uploadedCreep = creepRev;
    }
    // The players' colors: the map's own for players 1 to 8, the fixed ones for the rest (core/map_scene.inc: playerRgba).
    uint32_t team[12];
    for (int i = 0; i < 12; ++i) team[i] = playerRgba(i, 0) >> 8;
    gRenderer->setPlayerColors(team);
    {  // (the palette's player list: each player's color square)
        extern kit::Dropdown gOwnerBox;  // (sdl_panels.inc)
        std::vector<kit::DropdownItem> owners;
        for (int i = 0; i < 12; ++i)
            owners.push_back({L"Player " + std::to_wstring(i + 1) + (i == 11 ? L" (neutral)" : L""), -1, -1,
                              draw::rgb(int(team[i] >> 16 & 255), int(team[i] >> 8 & 255), int(team[i] & 255))});
        bool same = gOwnerBox.items().size() == owners.size();
        for (size_t i = 0; same && i < owners.size(); ++i) same = gOwnerBox.items()[i].swatch == owners[i].swatch;
        if (!same) {  // (setItems closes an open list: only when a color changed)
            const int v = gOwnerBox.value();
            gOwnerBox.setItems(std::move(owners));
            gOwnerBox.setValue(v);
        }
    }
    // (a mod's colors: their own shades, Rainbow as the game shows it; a custom RGB color has no row: tinted)
    uint16_t exact = 0;
    if (modTab())
        for (int i = 0; i < 12; ++i) exact |= uint16_t((i >= 8 || ed.player(i).color.kind != Editor::PlayerColor::Custom) << i);
    gRenderer->setExactTeamColors(exact);
    // The scene (core/map_scene.inc), with the units' idle animations at the game's speed (View > Animate units).
    if (gState.animate) advanceAnims();
    const render::Scene scene = buildScene();
    gRenderer->setTerrainPreview(gState.mouseIn ? ghostTiles : std::vector<std::pair<int, uint16_t>>{});
    gRenderer->drawTo(cmd, gMapTex, uint32_t(w), uint32_t(h), scene);
    mapFrameDone(gState);  // the title, the Locations list and the status bar (core/map_scene.inc)
}

// The start page and the Files page (sdl_start.inc, sdl_files.inc): defined after this file's root view is read.
extern StartView gStartView;
extern FilesPage gFilesPage;  // (core/views_files.inc)
extern NewPageView gNewView;  // the New page (sdl_new_page.inc)
extern bool gNewOpen;         // the New page is open in the Map workspace (it shows in place of the start page)
extern bool gStartOpen;       // File > New with a map open: the start page shows in place of the map
bool newPageEscape();         // (sdl_new_page.inc: Esc closes the New page or that start page)
bool newPageCovers();         // (sdl_new_page.inc: the New page, or the start page over the open map, holds the Map workspace)

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
bool sdlQuickPickTyping();  // the quick picker is open: its field types (sdl_map_dialogs.inc)
bool palettePanelsTyping();  // a palette window's text field has the keyboard (sdl_palette_panels.inc)
void registerWorkspaceView(Workspace w, draw::View& v);

// The context menus (sdl_menus.inc): the palette's View menu opens one too.
std::wstring sdlFeedbackOn(draw::View& from, draw::Point at, const std::wstring& pane);
bool sdlPopupMenu(draw::View& from, draw::Point at, std::vector<kit::MenuItem> items, const std::wstring& on, kit::MenuOptions opts = {});
std::vector<kit::MenuItem> sdlModelItems(const MenuModel& model, std::function<void(int)> pick);
std::optional<Mode> floatPaletteDockedAt(const draw::View* v);  // (sdl_palette_float.inc)
kit::MenuItem sdlPaletteFloatItem(Mode m);
#include "sdl_pictures.inc"  // the palette's pictures (the terrain, doodad, unit and sprite art from the game data)
#include "sdl_panels.inc"  // the side panels: the palette, the owner, the filter, the search, the section box, the second palette

// The map's dialogs and its menu (sdl_map_dialogs.inc): a double click, Enter and the right button.
bool mapDoubleClickSdl(int x, int y);
bool mapEnterSdl();
void mapRightClickSdl(draw::View& map, draw::Point at);

// A live game over the map (experimental: Tools > Simulate, sdl_simulation.inc): its bar at the top of the map
// (what runs, the speed, Cancel), its keys (Esc), and a step of the game each frame.
bool simBarShown();
void simPaintBar(draw::Canvas& c, draw::Rect r);
bool simBarMouse(const draw::MouseEvent& e, draw::Rect r);
std::wstring simBarTip(draw::Point pt, draw::Rect r);
bool simKey(const draw::KeyEvent& e);
void simFrame();

// The map view: the texture of the map (drawn by the frame hook) and the core's map input.
struct MapView final : draw::View {
    bool rightDown = false;  // the right button went down on the map: its release opens the map's menu
    // The middle button held: Options > Middle mouse click and hold. Drag map: the map follows the mouse; Pan map (the
    // default): the map moves toward the mouse while the button is held (panStep, each frame), as the Win32 program.
    bool panning = false;
    bool panDrags = false;  // (this press: Drag map)
    bool leftUpInPan = false;  // the left button went up during the pan: its drag ends with the pan
    bool panMoved = false;     // (Pan map: the last step moved the map)
    int panFrames = 0;         // (Pan map: the frames that the mouse's moves asked for; --midpan reads it)
    draw::Point anchor;  // where the middle button went down
    std::chrono::steady_clock::time_point panLast;  // (Pan map: the time of the last step)
    static inline int middleDragTest = -1;  // (--midpan: 0 Pan map, 1 Drag map, in place of the setting)
    bool rightStroke = false;  // a Shift+left stroke paints with the second terrain; the palette is the first one again after it
    int savedPalette = -1;
    draw::Point last;
    bool open() const { return gState.ed && gState.ed->isOpen(); }
    void paint(draw::Canvas& c) override {
        const draw::Rect all(0, 0, width(), height());
        c.fill(all, kBg);
        if (open() && gMapTex && gMapW == width() && gMapH == height()) {
            c.externalImage(gMapTex, all);
            if (simBarShown()) simPaintBar(c, all);
            return;
        }
        const std::wstring text = open() ? std::wstring(L"") : gDataOk ? shellTr(L"No map is open. Start with a map file: stardraft_editor <map>") : widen(gDataError);
        if (!text.empty()) c.text(all, text, kDim, draw::kCenter | draw::kMiddle | draw::kWrap);
    }
    // The keyboard: the map is a Tab stop between the side bars (its own keys: the letters, the arrows, Esc). When Tab
    // gave it the focus, an outline in the accent shows it, until the next click.
    bool tabbed = false;
    bool focusable() const override { return open(); }
    void tabbedTo(bool) override { tabbed = true, invalidate(); }
    void focusChanged(bool has) override {
        if (!has) tabbed = false;
        invalidate();
    }
    void paintOver(draw::Canvas& c) override {
        if (tabbed && hasFocus()) paintFocusOutline(c, {0, 0, width(), height()});
    }
    std::wstring focusedItem() const override { return L"Map"; }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && sdlPieClickAway()) return true;  // (a click away from the pie palette closes it)
        // While a live game shows: its bar's buttons; on the map only the view moves (middle button, wheel, pinch).
        if (simBarShown()) {
            if (simBarMouse(e, draw::Rect(0, 0, width(), height()))) return true;
            const bool press = e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick || e.kind == draw::MouseKind::Up;
            if (press && e.button != 2 && !(panning && e.kind == draw::MouseKind::Up)) return true;
            if (e.kind == draw::MouseKind::Move && !panning) {
                gState.mouseX = e.pt.x, gState.mouseY = e.pt.y, gState.mouseIn = true;
                return true;
            }
        }
        switch (e.kind) {
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick: {
            focus();  // the map takes the keyboard back from a text field or a list
            if (tabbed) tabbed = false, invalidate();  // (the outline of Tab goes)
            if (e.button == 2) {  // middle: drag or pan the map (Options > Middle mouse click and hold)
                if (panning) return true;  // (a press goes on)
                bool drags = false;
                if (middleDragTest >= 0) drags = middleDragTest == 1;
                else prefs::flag(L"MiddleDrag", drags);
                panning = true, panDrags = drags, panMoved = false, last = anchor = e.pt;
                panLast = std::chrono::steady_clock::now();
                if (gState.drag == Drag::None) gState.drag = drags ? Drag::Pan : Drag::AutoPan;  // (the right button's menu waits, as on Windows)
                if (draw::Host* h = host()) h->capture(*this, true);
                return true;
            }
            // Another button while the middle button pans: nothing (its release is not sent on, so it must not start a drag)
            if (panning) return true;
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
                if (panDrags) {
                    gState.camX -= float(e.pt.x - last.x) / gState.zoom, gState.camY -= float(e.pt.y - last.y) / gState.zoom;
                    clampCamera();
                    invalidate();
                }
                last = e.pt;  // (Pan map: panStep moves toward it)
                if (!panDrags) ++panFrames, invalidate();  // (Pan map: the frames step the pan; with no frame the map stands still)
                return true;
            }
            if (!open()) return false;
            mapMouseMove(gState, e.pt.x, e.pt.y);
            invalidate();
            return true;
        case draw::MouseKind::Up:
            if (panning) {
                // Another button during it: nothing (the press goes on). A left drag from before the pan ends with it.
                if (e.button == 0 && gState.drag != Drag::None && gState.drag != Drag::Pan && gState.drag != Drag::AutoPan)
                    leftUpInPan = true;
                if (e.button != 2) return true;
                panEnd();
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
        case draw::MouseKind::Wheel:  // a wheel zooms at the mouse, a trackpad's scroll moves the map (core/map_input.inc: mapWheel)
            if (!open()) return false;
            if (!mapWheel(gState, float(e.pt.x), float(e.pt.y), e.wheel, e.panX, e.panY, e.precise, e.horizontal, (e.mods & draw::kCtrl) != 0,
                          (e.mods & draw::kAlt) != 0))
                return false;
            updateStatus();
            invalidate();
            return true;
        case draw::MouseKind::Pinch:  // the zoom around the point between the fingers (core/map_input.inc: mapPinch)
            if (!open()) return false;  // Alt: the active brush's size (mapBrushPinch); no brush: the zoom
            if (!((e.mods & draw::kAlt) && mapBrushPinch(gState, e.pinch)) && !mapPinch(gState, float(e.pt.x), float(e.pt.y), e.pinch))
                return false;
            updateStatus();
            invalidate();
            return true;
        case draw::MouseKind::SmartZoom:  // macOS's smart zoom: in at the point, then out again (core/map_input.inc: mapSmartZoom)
            if (!open()) return false;
            mapSmartZoom(gState, float(e.pt.x), float(e.pt.y));
            updateStatus();
            invalidate();
            return true;
        case draw::MouseKind::Leave:
            gState.mouseIn = false;
            invalidate();
            return true;
        default: return false;
        }
    }
    // While a live game shows: the tooltip of its bar's "Approximation" chip (how close the simulation is to the game).
    std::wstring tip(draw::Point pt) override { return simBarTip(pt, draw::Rect(0, 0, width(), height())); }
    bool hoverTip() const override { return simBarShown(); }
    // The core's cursor (core/map_input.inc: the move cursor over a selected unit, doodad or sprite and while it moves,
    // the resize cursors on the selected location's edges and corners), as the Windows map view's mapCursorAt.
    draw::Cursor cursor(draw::Point pt) override {
        if (panning) return draw::Cursor::SizeAll;
        return open() ? mapCursorAt(gState, pt.x, pt.y) : draw::Cursor::Arrow;
    }
    // Each frame (drawMapOfRoot): Pan map moves the camera toward the mouse (core/map_input.inc). `dt`: the seconds
    // since the last step (a test gives them; else the clock's).
    void panStep(float dt = -1) {
        const auto now = std::chrono::steady_clock::now();
        if (dt < 0) dt = std::chrono::duration<float>(now - panLast).count();
        panLast = now;
        if (!panning || panDrags || !open()) return;
        // The first step after the map stood still: one frame's time at most (not the time since the last frame: the
        // map jumped when the pan began; GitHub #154).
        panMoved = middlePanStep(gState, float(last.x - anchor.x), float(last.y - anchor.y), panMoved ? dt : std::min(dt, 1.0f / 60));
        if (panMoved) clampCamera(), updateStatus(), invalidate();
    }
    // The mouse capture went away during a press (the window lost the focus: Alt+Tab, another program; the host's
    // SDL_EVENT_WINDOW_FOCUS_LOST): the stroke, the drag or the pan ends at once where the mouse was last, as one undo
    // step, as a release there (the Windows map's WM_CAPTURECHANGED). The first terrain is the palette's pick again after
    // a Shift stroke. After a release (the view lets the capture go itself) there is nothing left to end.
    void captureLost() override {
        rightDown = false, leftUpInPan = false;
        if (panning) {
            panning = false;
            if (gState.drag == Drag::Pan || gState.drag == Drag::AutoPan) gState.drag = Drag::None;
            gState.dirty = true;
        }
        const bool second = std::exchange(rightStroke, false);
        if (gState.drag != Drag::None) mapMouseUp(gState, gState.mouseX, gState.mouseY, 0);
        if (second) gState.palette = savedPalette, gState.dirty = true;
        invalidate();
    }
    // The release of the middle button: the press ends. A left drag from before the pan (move units, paint, a Shift
    // stroke) goes on while its button is held: the view keeps the capture. If its button went up during the pan, the
    // drag ends here (captureLost: a release where the mouse was last).
    void panEnd() {
        if (!panning) return;
        panning = false;
        if (gState.drag == Drag::Pan || gState.drag == Drag::AutoPan) gState.drag = Drag::None;
        gState.dirty = true;
        if (gState.drag != Drag::None && !leftUpInPan) return;
        if (gState.drag != Drag::None) captureLost();  // (the left release of the pan: the drag ends where the mouse was last)
        leftUpInPan = false;
        if (draw::Host* h = host()) h->capture(*this, false);
    }
};

// The shortcuts and the menus (sdl_menus.inc).
bool shortcutRun(const draw::KeyEvent& e);
bool sequenceKey(const draw::KeyEvent& e);
void menusIfDirty();
bool sdlNativeMenuBar();  // (sdl_native_menus.inc: the menus are in the system's menu bar, macOS)
void command(int id);

// The Log at the bottom of the window (core/views_log.inc; shell_sdl.inc adds the lines).
LogPaneView gLogPane;

// The floating palettes (sdl_palette_float.inc): the active mode's palette window opens or closes for the state, and
// the palette's views move into it or back (true: they are in its window; `slot` is their place in the side bar, in
// the main window).
bool floatPaletteSync(Mode m, bool shown, bool alt, draw::Rect slot);
void palettePanelsSync();  // (sdl_palette_panels.inc: the active tab's panels show in the Map workspace)

// The main window: the menu bar, the top tool bar under it (core/views_topbar.inc), the document tabs, the workspace rail, the status bar, the map view with its minimap and
// the panes that are not on this system yet, placed by computeLayout.
struct MainView final : draw::View {
    kit::MenuBar menus;
    TopBarView topBar;  // the top tool bar under the menus: the icons, the modes, Build, Report feedback
    TabsView tabs;
    RailView rail;
    StatusView status{gState};
    MapView map;
    MinimapView minimap;
    ModeBarView modes;  // the modes' segmented control, at the top bar's right
    NotHereView notHere;  // the workspace area of a workspace with no view on this system
    PieView pie;  // the pie palette (core/views_palette.inc): over the map while Space is held in Terrain mode

    MainView() {
        initPanels();
        add(menus, draw::Rect(0, 0, 0, 0));
        add(topBar, draw::Rect(0, 0, 0, 0));
        add(tabs, draw::Rect(0, 0, 0, 0));
        add(rail, draw::Rect(0, 0, 0, 0));
        add(status, draw::Rect(0, 0, 0, 0));
        add(gLogPane, draw::Rect(0, 0, 0, 0));
        add(map, draw::Rect(0, 0, 0, 0));
        add(minimap, draw::Rect(0, 0, 0, 0));
        add(gToolStripView, draw::Rect(0, 0, 0, 0));  // the tool strip (core/views_toolstrip.inc)
        add(gToolStripView2, draw::Rect(0, 0, 0, 0));  // the other side's mode's strip
        add(gToolStripView3, draw::Rect(0, 0, 0, 0));
        add(modes, draw::Rect(0, 0, 0, 0));
        add(gPanelTabsSdl[0], draw::Rect(0, 0, 0, 0));
        add(gPanelTabsSdl[1], draw::Rect(0, 0, 0, 0));
        add(gOwnerBox, draw::Rect(0, 0, 0, 0));
        add(gFilterSdl, draw::Rect(0, 0, 0, 0));
        add(gSearchSdl, draw::Rect(0, 0, 0, 0));
        add(gSectionDropSdl, draw::Rect(0, 0, 0, 0));
        add(gViewBtnSdl, draw::Rect(0, 0, 0, 0));
        add(gSortBtnSdl, draw::Rect(0, 0, 0, 0));
        add(gLocSortBtnSdl, draw::Rect(0, 0, 0, 0));
        add(gPalList, draw::Rect(0, 0, 0, 0));
        gTilesDockPanel.holdGrid(gTilesDockGrid);  // the Tiles panel (core/views_tiles.inc): the grid is in the panel on this system
        add(gTilesDockPanel, draw::Rect(0, 0, 0, 0));
        add(gLocList, draw::Rect(0, 0, 0, 0));
        add(gPal2List, draw::Rect(0, 0, 0, 0));
        add(gPal3List, draw::Rect(0, 0, 0, 0));
        add(notHere, draw::Rect(0, 0, 0, 0));
        add(gStartView, draw::Rect(0, 0, 0, 0));  // the start page (sdl_start.inc): no map open
        add(gNewView, draw::Rect(0, 0, 0, 0));    // the New page (sdl_new_page.inc): File > New project, a card
        for (auto& [w, v] : gWorkspaceViews) add(*v, draw::Rect(0, 0, 0, 0));
        add(gLoadingView, draw::Rect(0, 0, 0, 0));  // (over the workspace's views)
        add(pie, draw::Rect(0, 0, 0, 0));  // (the last child: over the map)
        pie.setVisible(false);
        pie.picture = [](uint16_t t, int tiles) { return pieBitmap(t, tiles); };
        pie.ground = [](uint16_t t) { return gState.ed ? terrainGround(*gState.ed, t) : uint16_t(0); };
        relayout();
    }
    ~MainView() override {  // (the children go before the members do: their views unlink here)
        const auto kids = children();
        for (View* c : kids) remove(*c);
    }

    void resized() override { relayout(); }

    // Tab's order follows the screen: the menu bar, the top bar (with the modes in it: TopBarView), the document tabs,
    // the workspace rail, the left side bar from the top down, the map (or the workspace's view), the right side bar from
    // the top down, then the Log's bar. (The status bar has nothing to click.)
    std::vector<View*> tabChildren() const override {
        std::vector<View*> first{const_cast<kit::MenuBar*>(&menus), const_cast<TopBarView*>(&topBar), const_cast<TabsView*>(&tabs),
                                 const_cast<RailView*>(&rail)};
        std::vector<View*> rest;
        for (View* c : children())
            if (std::find(first.begin(), first.end(), c) == first.end() && c != &modes && c != &gLogPane && c != &status && c != &pie) rest.push_back(c);
        // The middle: the views left of the map (the left side bar), the map's column, the views right of it.
        const draw::Rect m = map.shown() ? map.frame() : draw::Rect(0, 0, width(), height());
        auto column = [&](const View* v) {
            const draw::Rect f = v->frame();
            return f.right <= m.left + 1 ? 0 : f.left >= m.right - 1 ? 2 : 1;
        };
        std::stable_sort(rest.begin(), rest.end(), [&](const View* a, const View* b) {
            const int ca = column(a), cb = column(b);
            if (ca != cb) return ca < cb;
            return a->frame().top < b->frame().top;
        });
        first.insert(first.end(), rest.begin(), rest.end());
        first.push_back(&gLogPane);
        first.push_back(const_cast<StatusView*>(&status));
        return first;
    }
    // Tab from `v` to the next view of the order (back: the view before it), as the router's Tab: the modes give the
    // focus on when the top bar has no part after them.
    void tabFrom(View& v, bool back) {
        std::vector<View*> chain;
        focusChain(chain);
        if (chain.empty()) return;
        size_t i = 0;
        while (i < chain.size() && chain[i] != &v) ++i;
        if (i == chain.size()) i = 0;
        View* next = chain[back ? (i + chain.size() - 1) % chain.size() : (i + 1) % chain.size()];
        next->focus();
        if (next->hasFocus()) next->tabbedTo(back);
    }

    // The menus' keys run when no view takes the key (the focused view first, then up to here). A search box takes the
    // letters and digits it types: the map's one-letter shortcuts (T, U, G, Q...) wait while it has the keyboard.
    bool key(const draw::KeyEvent& e) override {
        if (simKey(e)) return true;  // (Esc ends a live game)
        if (simBarShown()) return shortcutRun(e);  // (a live game: the menus' keys only, not the map's)
        const int k = int(e.key);
        const bool typed = (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9');
        const bool typing = gSearchSdl.field().hasFocus() || gFilterSdl.field().hasFocus() || gFilesPage.typing() || sdlMapPageTyping() || sdlQuickPickTyping() ||
                            typingFocusIn(*this) ||  // (a kit text field of a view: the New page, Program options)
                            palettePanelsTyping();   // (a palette window's search box, as the side bar's)
        // The focused Triggers pane types too: a letter or a digit opens its block picker (from the text event), so the
        // map's keys (the player 1 to 0, the tools V B A F, Q) do not run (Windows: editFocus for the pane).
        if ((typing || trigPaneFocused()) && typed && !(e.mods & (draw::kCtrl | draw::kAlt))) return false;
        // Enter on the map: the selected location's or unit's properties (the Windows map's key).
        if (e.down && !e.repeat && e.key == draw::Key::Enter && !e.mods && !typing && map.shown() && mapEnterSdl()) return true;
        // Esc with the pie palette open (from the menu): it closes with no change.
        if (e.down && e.key == draw::Key::Escape && pie.shown()) {
            pie.setVisible(false);
            invalidate();
            return true;
        }
        // Esc while a trigger waits for a unit picked on the map: back to the triggers (the Windows cancelStep).
        if (e.down && e.key == draw::Key::Escape && trigUnitPickActive()) return trigCancelUnitPick(), true;
        // Esc with the New page (Cancel) or the start page over the open map (File > New): the page closes. (A field of the
        // New page that has the keyboard gives Esc to the page: core/views_new_page.inc.)
        if (e.down && !e.repeat && e.key == draw::Key::Escape && !e.mods && newPageEscape()) return true;
        // Esc on a control of the window that does not use it (a tool bar's button, a tab, a list): the keyboard goes
        // back to the map.
        if (e.down && !e.repeat && e.key == draw::Key::Escape && !e.mods && map.shown() && !map.hasFocus() && hasFocusIn(*this) && !hasFocus()) {
            map.focus();
            return true;
        }
        // Space on the map in Terrain mode: the pie palette while it is held (the Windows map's VK_SPACE, also with Shift);
        // its repeats do nothing more, and letting go paints with the slice's terrain, with any keys held. When the program
        // lost the keyboard (Alt+Tab: SDL lets go of the held keys), the ring closes with no change (the Windows
        // WM_KILLFOCUS).
        if (e.key == draw::Key::Space && !e.down && pie.shown()) {
            if (SDL_GetKeyboardFocus()) return pieRelease(), true;
            pie.setVisible(false);
            invalidate();
            return true;
        }
        if (e.key == draw::Key::Space && e.down && !(e.mods & ~unsigned(draw::kShift)) && !typing && map.shown() &&
            (map.hasFocus() || !hasFocusIn(*this))) {
            if (!e.repeat) pieOpen();
            return pie.shown();
        }
        if (shortcutRun(e)) return true;  // (the menus' keys first, as the Windows accelerators run before the map's keys)
        // The map's keys (Esc, R, the arrows) run only when the map has the keyboard, or no view has it (as the Windows
        // map window's WM_KEYDOWN): Left or Right in a list does not move the selected units.
        return e.down && !typing && map.shown() && (map.hasFocus() || !hasFocusIn(*this)) && mapKey(e);
    }

    // A right click on the minimap, the tool strip or the modes' bar (the Windows main window's WM_CONTEXTMENU): the
    // dock menu (core/menu_model.inc: dockMenuModel), Left or Right, and Hide for the minimap.
    bool mouse(const draw::MouseEvent& e) override {
        // A swipe of three fingers (macOS's Swipe between pages) that no view took: the next tab (to the left) or the
        // tab before, as Safari goes between its pages (the tabs' strip takes a two-finger swipe too: core/views_tabs.inc).
        if (e.kind == draw::MouseKind::Swipe) return swipeTabs(e.swipe), true;
        if (splitMouse(e)) return true;
        if (e.kind != draw::MouseKind::Up || e.button != 1) return false;
        auto over = [&](const draw::View& v) { return v.shown() && v.frame().contains(e.pt); };
        const bool mini = over(minimap), tools = !mini && (over(gToolStripView) || over(gToolStripView2) || over(gToolStripView3) || over(modes));
        if (!mini && !tools) {
            // Nothing took the right click: a menu with only Report feedback (the Windows feedbackAfterRightClick), when
            // no menu opened for it (a view's menu may open after the event: the check waits for that); on a mode's
            // palette in the side bar (the location list, the boxes over a list, the Tiles panel), Make floating first.
            // The map and the text fields are left alone.
            draw::Point local;
            draw::View* under = viewAt(e.pt, local);
            for (draw::View* v = under; v && v != this; v = v->parent())
                if (v == &map || dynamic_cast<kit::TextField*>(v)) return false;
            const draw::Point at = e.pt;
            const std::wstring on = sdlFeedbackOn(*this, at, L"");
            const std::optional<Mode> palette = floatPaletteDockedAt(under);
            shellPost([this, at, on, palette] {
                if (kit::Menu::open()) return;
                std::vector<kit::MenuItem> items;
                if (palette) items.push_back(sdlPaletteFloatItem(*palette));
                sdlPopupMenu(*this, at, std::move(items), on);
            });
            return true;
        }
        const bool right = mini ? gDock.minimapRight : gDock.toolsRight[int(gState.mode)];
        sdlPopupMenu(*this, e.pt, sdlModelItems(dockMenuModel(mini, right, gState.mode), [](int id) { command(id); }),
                     mini ? L"the minimap" : sdlFeedbackOn(*this, e.pt, L""));
        return true;
    }

    // The side bars' drag edges (the layout's splitters: the gap between a side bar and the map, in the Map workspace):
    // the width cursor over them; a drag with the left button changes the side's width (96-dpi pixels, kMinSideW to
    // kMaxSideW) while the map keeps its middle in place; the release saves the width in the dock settings (core/
    // dock_prefs.inc: saveDock), as the Windows main window's WM_LBUTTONDOWN, WM_MOUSEMOVE and WM_LBUTTONUP did.
    // The minimap's bottom edge (splitter 2): the height cursor; a drag changes its height (more room for the panel
    // under it, or a bigger minimap); a double click gives it the map's shape again.
    draw::Rect splitters[3] = {};
    int splitDrag = -1, splitGrab = 0;
    int splitAt(draw::Point pt) const {
        for (int i = 0; i < 3; ++i)
            if (splitters[i].width() > 0 && splitters[i].contains(pt)) return i;
        return -1;
    }
    draw::Cursor cursor(draw::Point pt) override {
        const int i = splitDrag >= 0 ? splitDrag : splitAt(pt);
        return i == 2 ? draw::Cursor::SizeNS : i >= 0 ? draw::Cursor::SizeWE : draw::Cursor::Arrow;
    }
    bool splitMouse(const draw::MouseEvent& e) {
        if (e.kind == draw::MouseKind::DoubleClick && e.button == 0 && splitAt(e.pt) == 2) {
            gDock.minimapH = 0;  // (the map's shape again)
            relayout();
            saveDock();
            return true;
        }
        if (e.kind == draw::MouseKind::Down && e.button == 0 && splitDrag < 0) {
            const int i = splitAt(e.pt);
            if (i < 0) return false;
            splitDrag = i, splitGrab = i == 2 ? e.pt.y : e.pt.x;
            if (i == 2 && gDock.minimapH <= 0) gDock.minimapH = MulDiv(minimap.height(), 96, int(gDpi));  // (from its height now)
            if (draw::Host* h = host()) h->capture(*this, true);
            return true;
        }
        if (splitDrag < 0) return false;
        if (e.kind == draw::MouseKind::Move && splitDrag == 2) {
            const int dpi = int(gDpi), dy = MulDiv(e.pt.y - splitGrab, 96, dpi);
            const int nh = std::max(40, gDock.minimapH + dy);
            if (nh != gDock.minimapH) {
                const int before = minimap.height();
                gDock.minimapH = nh;
                relayout();
                splitGrab += minimap.height() - before;  // (the edge stays under the mouse, also at its limits)
            }
            return true;
        }
        if (e.kind == draw::MouseKind::Move) {
            const int dpi = int(gDpi), dx = MulDiv(e.pt.x - splitGrab, 96, dpi);
            int& w = splitDrag == 0 ? gDock.leftW : gDock.rightW;
            const int nw = std::clamp(w + (splitDrag == 0 ? dx : -dx), kMinSideW, kMaxSideW);
            if (nw != w) {
                splitGrab += MulDiv(splitDrag == 0 ? nw - w : w - nw, dpi, 96);
                const float z = gState.zoom > 0 ? gState.zoom : 1.0f;
                const float cx = gState.camX + float(map.width()) / 2.0f / z, cy = gState.camY + float(map.height()) / 2.0f / z;
                w = nw;
                relayout();
                gState.camX = cx - float(map.width()) / 2.0f / z, gState.camY = cy - float(map.height()) / 2.0f / z;
                gState.dirty = true;
                map.invalidate();
            }
            return true;
        }
        if (e.kind == draw::MouseKind::Up && e.button == 0) {
            if (splitDrag == 2) gDock.minimapH = MulDiv(minimap.height(), 96, int(gDpi));  // (as it is: within its limits)
            splitDrag = -1;
            if (draw::Host* h = host()) h->capture(*this, false);
            saveDock();
            return true;
        }
        return true;  // (another button while the edge is held)
    }
    void captureLost() override {
        if (splitDrag < 0) return;
        splitDrag = -1;
        saveDock();
    }

    // The map's own keys (the Windows map's WM_KEYDOWN; the core's functions, core/map_input.inc): Esc stops the step
    // or clears the selection, R and Shift+R turn the terrain to paste, the arrows move the selected units or the view.
    bool mapKey(const draw::KeyEvent& e) {
        if (!map.open() || (e.mods & (draw::kCtrl | draw::kAlt))) return false;
        const bool shift = (e.mods & draw::kShift) != 0;
        switch (e.key) {
        case draw::Key::Escape:
            if (mapCancelStep(gState))
                if (draw::Host* h = host()) h->capture(map, false);
            break;
        case draw::Key('R'): mapTurnPaste(gState, shift); break;
        case draw::Key::Left:
        case draw::Key::Right:
        case draw::Key::Up:
        case draw::Key::Down:
            mapArrowKey(gState, e.key == draw::Key::Left ? -1 : e.key == draw::Key::Right ? 1 : 0,
                        e.key == draw::Key::Up ? -1 : e.key == draw::Key::Down ? 1 : 0, shift);
            clampCamera();
            break;
        default: return false;
        }
        gState.dirty = true;
        map.invalidate();
        return true;
    }

    // The pie palette (core/palette_view.inc: pieTerrains): the ring in the middle of the map, its slice the one the
    // mouse points at. Nothing when the map has no terrain to paint (or the mode is not Terrain).
    void pieOpen() {
        pie.onClick = [this] { pieRelease(); };  // (from the menu: a click picks; with Space held, a click picks too)
        pie.items = pieTerrains(gState);
        if (pie.items.empty() || !map.shown()) return;
        const draw::Rect m = map.frame();
        // The ring as wide as its terrains need, in the map's room (with the name's row under it), not smaller than at
        // first.
        const int fit = (std::min(m.width(), m.height() - 2 * PieView::labelHeight()) - scale(16)) / 2;
        pie.radius = std::max(pieRadius(), std::min(pieRadius(int(pie.items.size())), fit));
        const int s = pie.ringSize(), cx = (m.left + m.right) / 2, cy = (m.top + m.bottom) / 2;  // (the ring in the map's middle)
        pie.setFrame({cx - s / 2, cy - s / 2, cx - s / 2 + s, cy - s / 2 + s + PieView::labelHeight()});
        pie.setVisible(true);
        const draw::Point at = host() ? host()->mouse(pie) : pie.middle();
        pie.hot = pie.sliceAt(at);
        pie.invalidate();
    }
    // Space is let go: the brush paints with the slice's terrain (none in the middle).
    void pieRelease() {
        const int h = pie.hot;
        pie.setVisible(false);
        invalidate();
        if (h >= 0 && h < int(pie.items.size())) pickUnit(gState, pie.items[size_t(h)], false), invalidateMapView();
    }

    // The menu bar's height; the top tool bar is under it (as the Windows menu and its top bar).
    // macOS: none, the menus are in the system's menu bar (sdl_native_menus.inc).
    static int menuBarHeight() { return sdlNativeMenuBar() ? 0 : scale(28); }
    // The modes' bar: in the top tool bar, after its icons (core/views_topbar.inc), in the window's coordinates.
    RECT modeBarRect() const {
        RECT r = topBarModesRect();
        r.top += menuBarHeight(), r.bottom += menuBarHeight();
        return r;
    }

    // Places the panes for the window's size and the state (computeLayout; the same rules as the Windows layout).
    void relayout() {
        if (draw::Host* h = host()) gDpi = unsigned(std::lround(96.0 * double(h->scale())));
        syncKitThemeTokens(int(gDpi));
        LayoutInput in;
        in.dpi = int(gDpi);
        in.clientW = width(), in.clientH = height();
        in.statusH = scale(24);
        in.barH = menuBarHeight() + topBarHeight();  // the menu bar and the top tool bar
        in.logH = logPaneHeight(height(), in.barH, tabStripHeight(), in.statusH);  // (the Log's bar, or the open Log)
        in.tabsH = tabStripHeight();     // the document tabs
        in.railW = shellHasRail() ? wsRailWidth() : 0;
        in.mode = gState.mode;
        // The map's panes show in the Map workspace with a map open; with no map, the Map workspace shows the start page.
        const bool mapOpen = gState.ed && gState.ed->isOpen();
        // The New page is open in the Map workspace: it shows in place of the start page or the map (as a tab does).
        const bool newShown = gNewOpen && gWorkspace == Workspace::Map;
        const bool startShown = gWorkspace == Workspace::Map && (!mapOpen || gStartOpen) && !newShown;  // (File > New over a map)
        const bool mapWs = gWorkspace == Workspace::Map && mapOpen && !newShown && !startShown;
        in.mapWs = mapWs;
        // Each docked list's tools are its side bar panel's (panel_state.inc: its tools, sizes and options).
        in.toolStrip = [](int width) { return toolStripHeight(width, &dockedPanel(gState.mode)); };
        in.toolStripOf = [](int width, Mode m) { return toolStripHeight(width, &dockedPanel(m)); };
        in.modeBarH = 0, in.modeBar = modeBarRect();  // (the modes are in the top bar)
        in.mapOpen = mapOpen;
        if (in.mapOpen) in.mapW = gState.ed->width(), in.mapH = gState.ed->height();
        in.minimapHidden = gDock.minimapHidden;
        in.minimapRight = gDock.minimapRight;
        in.leftW = gDock.leftW, in.rightW = gDock.rightW, in.minimapH = gDock.minimapH;
        std::copy(gDock.toolsRight, gDock.toolsRight + kModeCount, in.toolsRight);
        in.lastOnSide[0] = gLastOnSide[0], in.lastOnSide[1] = gLastOnSide[1];
        in.hasPanelTab[0] = in.hasPanelTab[1] = true;
        in.hasSectionBox = true;
        in.hasPal2 = true;
        in.pal2Shown = gPal2Shown, in.pal2Mode = gPal2Mode;
        in.hasPal3 = true;
        in.pal3Shown = gPal3Shown, in.pal3Mode = gPal3Mode;
        for (int k = 0; k < kModeCount; ++k) in.floats[k] = paletteFloats(Mode(k));  // (a floating panel has no tab)
        const MainLayout L = computeLayout(in);
        for (int i = 0; i < 3; ++i) splitters[i] = mapWs ? draw::Rect(L.splitters[i]) : draw::Rect{};
        // The state the layout sets (as the Win32 layout): the sides' tabs, the second palette's mode and its shown state.
        gPanelTab[0] = L.panelTab[0], gPanelTab[1] = L.panelTab[1];
        gPal2Shown = L.palette2.visible;
        if (gPal2Shown) {
            gPal2Mode = L.secondMode;
            if (L.palette2Changed) rebuildSecondRows();
        }
        gPal3Shown = L.palette3.visible;
        if (gPal3Shown) {
            gPal3Mode = L.thirdMode;
            if (L.palette3Changed) rebuildThirdRows();
        }
        // Which panes show in the mode, as the Windows program shows them (main.cpp's SetMode): the list and the filter
        // in Locations; the palette and the search in the other modes; the owner in Units and Sprites; the section box
        // in Units, Doodads and Sprites. The layout places the rows that share a place, so only these are shown.
        const Mode m = gState.mode;
        const bool locs = m == Mode::Locations;
        // The side bar's lists and strips show their panels (the side bar's panel of their mode, in the active tab).
        gPalList.state = &dockedPanel(m), gToolStripView.state = &dockedPanel(m);
        gPal2List.state = gToolStripView2.state = &dockedPanel(L.secondMode);
        gPal3List.state = gToolStripView3.state = &dockedPanel(L.thirdMode);
        const bool owners = m == Mode::Units || m == Mode::Sprites;
        if (owners) gOwnerBox.setValue(panelFresh(dockedPanel(m)).owner);  // (its panel's player)
        const bool sections = m == Mode::Units || m == Mode::Doodads || m == Mode::Sprites;
        const bool toolsRight = gDock.toolsRight[int(m)];
        PaneSlot menuSlot = L.topBar, barSlot = L.topBar;  // the menus, then the tool bar under them
        menuSlot.rc.bottom = menuSlot.rc.top + menuBarHeight();
        barSlot.rc.top = menuSlot.rc.bottom;
        apply(menus, menuSlot, !sdlNativeMenuBar());
        apply(topBar, barSlot, true);
        apply(tabs, L.tabs, true);
        apply(rail, L.wsRail, shellHasRail());
        apply(status, L.status, true);
        apply(gLogPane, L.log, true);
        apply(map, L.view, mapWs);
        apply(minimap, L.minimap, L.minimap.visible && mapWs);
        apply(modes, L.modeBar, mapWs);
        apply(gPanelTabsSdl[toolsRight ? 1 : 0], L.panelTabActive, L.panelTabActive.visible && mapWs);
        apply(gPanelTabsSdl[toolsRight ? 0 : 1], L.panelTabOther, L.panelTabOther.visible && mapWs);
        // Rectangle mode's Tiles panel takes the palette's place: from its search box down to its end (the Win32 tilesSync),
        // where the side bar's Terrain panel is in Rectangle (its own Isometric or Rectangle: panel_state.inc).
        const bool rectT = mapWs && panelFresh(dockedPanel(Mode::Terrain)).terrainRect && !paletteFloats(Mode::Terrain);
        const bool tiles = rectT && m == Mode::Terrain;
        gTilesShown = tiles;
        PaneSlot tilesSlot = L.search;
        tilesSlot.placed = L.search.placed && L.palette.placed;
        tilesSlot.rc = RECT{L.search.rc.left, L.search.rc.top, L.palette.rc.right, L.palette.rc.bottom};
        // The floating panels (sdl_palette_float.inc, sdl_palette_panels.inc): the active mode's whole panel (its tools,
        // Isometric and Rectangle, its options, its list) can be in a window of its own; then the side bar shows
        // another tab's list in their place (gPal3List, as the other side's; its tab is not in the tabs: it is back when the window closes).
        PaneSlot noteSlot = tilesSlot;
        noteSlot.rc.top = L.tool.rc.top;
        const bool floated = floatPaletteSync(m, mapWs && noteSlot.placed, tiles, draw::Rect(noteSlot.rc));
        palettePanelsSync();
        const bool here = mapWs && !floated;  // (the active mode's panel is in the side bar)
        apply(gToolStripView, L.tool, here);
        const auto side = [this](draw::View& v, const PaneSlot& s, bool shown) {
            if (v.parent() == this) apply(v, s, shown);
        };
        side(gOwnerBox, oneRow(L.owner), owners && here);       // (a closed owner list is one row high)
        side(gSectionDropSdl, oneRow(L.sectionBox), sections && here);
        // Rectangle mode with Terrain's list in the side bar but not active (the other side's list, or the active side's
        // while the active mode floats): its list is the Tiles panel, as Terrain's floating window shows it in every mode.
        const bool tiles2 = !tiles && rectT && L.palette2.visible && L.secondMode == Mode::Terrain;
        const bool tiles3 = !tiles && !tiles2 && rectT && L.palette3.visible && L.thirdMode == Mode::Terrain;
        if (tiles2 || tiles3) gTilesShown = true;
        side(gTilesDockPanel, tiles2 ? L.palette2 : tiles3 ? L.palette3 : tilesSlot, (tiles && !floated) || tiles2 || tiles3);
        side(gSearchSdl, L.search, !locs && here && !tiles);
        side(gViewBtnSdl, L.viewBtn, !locs && here && !tiles);
        sortTipsSdl();
        side(gSortBtnSdl, L.sortBtn, L.sortBtn.visible && !locs && here && !tiles);
        side(gLocSortBtnSdl, L.sortBtn, locs && here);
        side(gPalList, L.palette, !locs && here && !tiles);
        side(gFilterSdl, L.filter, locs && here);
        side(gLocList, L.list, locs && here);
        apply(gPal2List, L.palette2, L.palette2.visible && mapWs && !tiles2);
        apply(gToolStripView2, L.tool2, L.tool2.visible && mapWs);
        apply(gToolStripView3, L.tool3, L.tool3.visible && mapWs);
        apply(gPal3List, L.palette3, L.palette3.visible && mapWs && !tiles3);
        // The workspace area: the registered view of the workspace shown, else the "Not on this system yet" view.
        const auto found = gWorkspaceViews.find(gWorkspace);
        draw::View* wsView = mapWs || found == gWorkspaceViews.end() ? nullptr : found->second;
        for (auto& [w, v] : gWorkspaceViews) apply(*v, L.workspace, v == wsView);  // (the whole workspace, as the Windows windows)
        // The start page and "Not on this system yet" take the whole workspace too: the map's side columns are not
        // shown with them (L.view keeps their room, so a narrow window cut the start page's cards off).
        apply(gStartView, L.workspace, startShown);
        apply(gNewView, L.view, newShown);
        apply(notHere, L.workspace, !mapWs && !wsView && !startShown && !newShown);
        apply(gLoadingView, L.workspace, gLoading.on);
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

// The Tiles panel shows in the palette's place while Rectangle mode is on (the core calls this when the tool or the mode
// changes): the panes are placed again when that changes.
// The side bar's panel at a point of the main window (its list, its strip, its boxes, its Tiles list), or none: a press
// there makes it the active panel (sdl_palette_panels.inc: palettePanelsPress).
PanelState* sidePanelAt(draw::Point pt) {
    if (!gRoot || !docOpen()) return nullptr;
    auto in = [&](const draw::View& v) { return v.parent() == gRoot && v.shown() && v.frame().contains(pt); };
    for (ToolStripView* t : {&gToolStripView, &gToolStripView2, &gToolStripView3})
        if (in(*t)) return t->state;
    for (PaletteGridView* l : {static_cast<PaletteGridView*>(&gPalList), static_cast<PaletteGridView*>(&gPal2List), static_cast<PaletteGridView*>(&gPal3List)})
        if (in(*l)) return l->state;
    if (in(gTilesDockPanel)) return &dockedPanel(Mode::Terrain);
    for (draw::View* v : std::initializer_list<draw::View*>{&gSearchSdl, &gViewBtnSdl, &gSortBtnSdl, &gSectionDropSdl, &gOwnerBox})
        if (in(*v)) return &dockedPanel(gState.mode);
    if (in(gLocList) || in(gFilterSdl) || in(gLocSortBtnSdl)) return &dockedPanel(Mode::Locations);
    return nullptr;
}
void floatPalettesTiles();  // (sdl_palette_panels.inc: the windows' Tiles lists)
void tilesSync() {
    if (gRoot) gRoot->relayout();  // (a panel's Isometric or Rectangle: its list or its Tiles list)
    if (gTilesShown) tilesFresh(), gTilesDockGrid.invalidate();
    floatPalettesTiles();
}

// The modes' control shows in the top tool bar (Build goes after it then: core/views_topbar.inc).
bool topBarModesShown() { return gRoot && gRoot->modes.shown(); }
// Tab between the top bar's parts and the modes in it (core/views_topbar.inc, core/views_modebar.inc).
bool modeBarTabIn(bool back) {
    if (!topBarModesShown()) return false;
    gRoot->modes.enter(back);
    return true;
}
void topBarBesideModes(bool back) {
    if (!gRoot) return;
    if (!gRoot->topBar.focusBesideModes(back)) gRoot->tabFrom(gRoot->topBar, back);
}

void invalidateMapView() {
    if (gRoot) gRoot->map.invalidate();
}

// --pie in a snapshot: Terrain mode, the pie open with the mouse at two thirds of the radius to the right of its middle
// (as the Windows test's pie:); the output says how many slices and which one is hot. sdlPieShotRelease lets go of Space
// after the picture and says whether the brush paints with that terrain.
void sdlPieShot() {
    if (!gRoot) return;
    setWorkspace(Workspace::Map);
    setMode(Mode::Terrain);
    gRoot->relayout();
    gRoot->pieOpen();
    PieView& p = gRoot->pie;
    if (!p.shown()) return void(std::printf("pie: not open (no terrain to paint)\n"));
    p.track({p.middle().x + p.ringRadius() * 2 / 3, p.middle().y});
    std::printf("pie: slices %d, pointed at %d\n", int(p.items.size()), p.hot);
    std::string names;
    for (const uint16_t t : p.items) names += (names.empty() ? "" : "; ") + narrow(p.terrainName(t));
    std::printf("pie: terrains %s\n", names.c_str());
}
// View > Terrain pie palette: the ring that Space held opens, in Terrain mode, open until a click (on a slice: its
// terrain; elsewhere: no change) or Esc.
void sdlPieFromMenu() {
    if (!gRoot || !gState.ed || !gState.ed->isOpen()) return;
    setWorkspace(Workspace::Map);
    if (gState.mode != Mode::Terrain) setMode(Mode::Terrain);
    gRoot->relayout();
    gRoot->pieOpen();
    if (!gRoot->pie.shown()) setMessage(tr(L"The pie palette has no terrains for this tileset"));
}
// The loading view's frame: the window laid out (the view shows while gLoading.on) and painted now.
void shellLoadingFrame() {
    if (!gRoot || !gHost) return;
    gRoot->relayout();
    gLoadingView.invalidate();
    gHost->paintNow();
}
bool sdlPieClickAway() {
    if (!gRoot || !gRoot->pie.shown()) return false;
    gRoot->pie.setVisible(false);
    gRoot->invalidate();
    return true;
}
void sdlPieShotRelease() {
    if (!gRoot || !gRoot->pie.shown()) return;
    const int hot = gRoot->pie.hot;
    const int want = hot >= 0 && hot < int(gRoot->pie.items.size()) ? gRoot->pie.items[size_t(hot)] : 0;
    gRoot->pieRelease();
    std::printf("pie: painting it %s\n", hot >= 0 && gState.palette == want && currentTool(gState) == TBrush ? "yes" : "NO");
}

// --midpan (after the picture): Options > Middle mouse click and hold, both values, as the Win32 self-test's midpan:.
// The middle button goes down at (400, 300) of the map view and the mouse moves 100 px to the right.
void sdlMidPanCheck() {
    if (!gRoot || !gState.ed || !gState.ed->isOpen()) return void(std::printf("midpan: no map open\n"));
    MapView& map = gRoot->map;
    auto mouse = [&](draw::MouseKind kind, int x, int button = 2) {
        draw::MouseEvent e;
        e.kind = kind, e.pt = {x, 300}, e.button = button;
        map.mouse(e);
    };
    auto center = [&] {
        gState.zoom = 1;
        gState.camX = gState.ed->width() * 16.0f - map.width() / 2.0f, gState.camY = gState.ed->height() * 16.0f - map.height() / 2.0f;
    };
    // Pan map: the map moves to the right while the button is held, and goes on when the mouse stands still.
    MapView::middleDragTest = 0;
    center();
    const float x0 = gState.camX, y0 = gState.camY;
    mouse(draw::MouseKind::Down, 400), mouse(draw::MouseKind::Move, 500);
    const bool noJump = gState.camX == x0;
    map.panStep(0.05f);
    const float x1 = gState.camX;
    map.panStep(0.05f);
    const bool goesOn = gState.camX > x1 && x1 > x0 && std::abs(gState.camY - y0) < 0.01f;
    mouse(draw::MouseKind::Down, 500, 1), mouse(draw::MouseKind::Up, 500, 1);  // a right click during it: no menu, the press goes on
    const bool rightKeeps = map.panning && gState.drag == Drag::AutoPan;
    // The pan begins at once and smoothly (GitHub #154): a move out of the dead zone asks for a frame (the frames step
    // the pan), and the first step after the map stood still is one frame's time at most (552 px/s: 9.2 px).
    mouse(draw::MouseKind::Up, 500);
    center();
    const float s0 = gState.camX;
    mouse(draw::MouseKind::Down, 400), mouse(draw::MouseKind::Move, 404);
    map.panStep(0.05f);  // (in the dead zone: the map stands still)
    const bool deadStill = gState.camX == s0;
    const int asked = map.panFrames;
    mouse(draw::MouseKind::Move, 500);
    const bool frameAsked = map.panFrames > asked;
    map.panStep(0.1f);
    const float firstStep = gState.camX - s0;
    std::printf("midpan: Pan map: a move out of the dead zone asks for a frame %s, the map stands still in it %s, the first step is one frame's "
                "time %s (%.1f px)\n",
                frameAsked ? "yes" : "(WRONG)", deadStill ? "yes" : "(WRONG)", firstStep > 0 && firstStep < 9.5f ? "yes" : "(WRONG)",
                double(firstStep));
    mouse(draw::MouseKind::Down, 500, 0), mouse(draw::MouseKind::Up, 500, 0);  // a left click during it: no drag starts
    const bool leftKeeps = map.panning && gState.drag == Drag::AutoPan;
    mouse(draw::MouseKind::Up, 500);
    const float x2 = gState.camX;
    map.panStep(0.05f);
    std::printf("midpan: Pan map: the map moves toward the mouse while the button is held %s (%.1f -> %.1f -> %.1f), not when the mouse moves %s, "
                "a right click does not end it %s, a left click starts no drag %s, it stops on release %s\n",
                goesOn ? "yes" : "(WRONG)", double(x0), double(x1), double(x2), noJump ? "yes" : "(WRONG)", rightKeeps ? "yes" : "(WRONG)",
                leftKeeps ? "yes" : "(WRONG)",
                gState.camX == x2 && gState.drag == Drag::None ? "yes" : "(WRONG)");
    // Drag map: the map follows the mouse, and stays when the mouse stays.
    MapView::middleDragTest = 1;
    center();
    const float d0 = gState.camX;
    mouse(draw::MouseKind::Down, 400), mouse(draw::MouseKind::Move, 500);
    const float moved = d0 - gState.camX;
    map.panStep(0.05f);
    const bool still = std::abs(d0 - moved - gState.camX) < 0.01f;
    mouse(draw::MouseKind::Up, 500);
    std::printf("midpan: Drag map: the map follows the mouse %s (%.1f -> %.1f), and stays when the mouse stays %s, the drag ends on release %s\n",
                std::abs(moved - 100.0f) < 0.01f ? "yes" : "(WRONG)", double(d0), double(d0 - moved), still ? "yes" : "(WRONG)",
                gState.drag == Drag::None && !map.panning ? "yes" : "(WRONG)");
    // A pan during a left drag (the mode's tool: paint, move units): the left drag goes on after the middle release,
    // and ends on the left release. A left release during the pan ends the drag with the pan.
    MapView::middleDragTest = 0;
    center();
    mouse(draw::MouseKind::Down, 400, 0);
    const Drag left = gState.drag;
    mouse(draw::MouseKind::Down, 420), mouse(draw::MouseKind::Up, 420);
    const bool leftGoesOn = left != Drag::None && gState.drag == left && !map.panning;
    mouse(draw::MouseKind::Up, 420, 0);
    const bool leftEnds = gState.drag == Drag::None;
    mouse(draw::MouseKind::Down, 400, 0), mouse(draw::MouseKind::Down, 420), mouse(draw::MouseKind::Up, 420, 0);
    mouse(draw::MouseKind::Up, 420);
    const bool endsWithPan = gState.drag == Drag::None && !map.panning;
    std::printf("midpan: a pan during a left drag: the left drag goes on after the middle release %s, it ends on the left release %s, "
                "a left release during the pan ends it with the pan %s\n",
                leftGoesOn ? "yes" : "(WRONG)", leftEnds ? "yes" : "(WRONG)", endsWithPan ? "yes" : "(WRONG)");
    MapView::middleDragTest = -1;
}

// --palview in a snapshot: the View menu's items (list, icons, sprites, normal, large, xlarge; the orders: category, name,
// name-down, number, number-down, tier) on the palette of the mode.
void sdlPaletteViewShot(const std::string& spec) {
    std::stringstream in(spec);
    for (std::string w; std::getline(in, w, ',');) {
        const int id = w == "list" ? ID_PAL_LIST : w == "icons" ? ID_PAL_ICONS : w == "sprites" ? ID_PAL_SPRITES : w == "normal" ? ID_PAL_NORMAL
                                                                                                  : w == "large" ? ID_PAL_LARGE : w == "xlarge" ? ID_PAL_XLARGE
                       : w == "category" ? ID_PAL_SORT_CATEGORY : w == "name" ? ID_PAL_SORT_NAME : w == "number" ? ID_PAL_SORT_NUMBER
                       : w == "tier" ? ID_PAL_SORT_TIER : w == "name-down" ? ID_PAL_SORT_NAME_DOWN : w == "number-down" ? ID_PAL_SORT_NUMBER_DOWN : 0;
        if (!applyPaletteViewCommand(gState.mode, id))
            std::fprintf(stderr, "--palview %s: not list, icons, sprites, normal, large, xlarge, category, name, name-down, number, number-down or tier\n", w.c_str());
    }
    sortChangedSdl(gState.mode);
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
    if (gCenterUnitPending >= 0 && w > 0 && h > 0) unitsListCenter(gCenterUnitPending), gCenterUnitPending = -1;
    simFrame();  // (a live game: its frames for the time that went by)
    gRoot->map.panStep();  // (Options > Middle mouse click and hold > Pan map: the map moves toward the mouse)
    if (gWorkspace == Workspace::Map) drawMapFrame(cmd, w, h);  // (the other workspaces hide the map: no frame)
}
