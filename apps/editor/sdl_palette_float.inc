// The floating palettes on this system (Roadmap R40, as ScmDraft 2's terrain window): each map mode's palette (the
// side bar's views under the mode's tools: core/views_palette_float.inc, FloatPaletteParts) can be in a window of its
// own, a tool window of the host (kit_sdl/sdl_host.h: WindowKind::Tool) that stays over the main window, can move (also
// to another screen) and change its size. The kit draws each host window with its own ImGui draw list into its own
// swapchain, so the palette's window is drawn as the dialogs are (no ImGui multi-viewport).
// Each mode's state, place and size are in the settings (core/dock_prefs.inc: gFloatPal, Layout/<Name>...); the place
// comes back at the next start when it is still on a screen. The modes share the side bar's views, so one window is
// open at a time: the active mode's, while its palette floats; another mode closes it, and opens its own when its
// palette floats. The window does not take the keyboard when it opens; the keys that no view of it takes go to the main
// window (the map's keys and the menus' shortcuts work from it), and a pick in it gives the keyboard to the map (as a
// pick in the side bar does). Dock, or the window's close button, puts the views back in the side bar. Included by
// main_sdl.cpp after sdl_main_view.inc.

void sdlInputLine(const std::string& what);  // (sdl_menus.inc)

FloatPaletteView gFloatPalView;
FloatPaletteNoteView gFloatNote;
bool gFloatPalOpen = false;      // the window is open, and the palette's views are in it
Mode gFloatPalMode = Mode::Terrain;  // ... the views of this mode's palette
bool gFloatPalClosing = false;   // the program closes the window (another mode): the palette stays floating
bool gFloatPalLines = false;     // a snapshot's input lines have the palette's line (sdlFloatPaletteLine)
draw::Rect gFloatPalShotAt;      // where the window opened, in the main window's pixels (a snapshot shows it there)
Uint64 gFloatPalSaveAt = 0;      // the moved or resized window's place is saved then (0: nothing to save)

// The short name of a mode's palette (the end of its command's name: terrain, units, ...).
std::string floatPaletteShort(Mode m) {
    const std::string c = kFloatPalNames[int(m)].command;
    return c.substr(c.rfind('.') + 1);
}

// The side bar's views of the palette of `m`, as the window holds them (from the top).
FloatPaletteParts floatPaletteParts(Mode m) {
    FloatPaletteParts p;
    if (m == Mode::Locations) {
        p.rows = {{&gFilterSdl, nullptr}};
        p.list = &gLocList;
        return p;
    }
    if (m == Mode::Units || m == Mode::Sprites) p.rows.push_back({&gOwnerBox, nullptr});
    if (m != Mode::Terrain) p.rows.push_back({&gSectionDropSdl, nullptr});
    p.rows.push_back({&gSearchSdl, &gViewBtnSdl});
    p.list = &gPalList;
    if (m == Mode::Terrain) p.alt = &gTilesPanel;  // (Rectangle mode's Tiles panel)
    return p;
}
std::vector<draw::View*> floatPaletteViews(const FloatPaletteParts& p) {
    std::vector<draw::View*> out;
    for (auto [fill, button] : p.rows) {
        if (fill) out.push_back(fill);
        if (button) out.push_back(button);
    }
    if (p.list) out.push_back(p.list);
    if (p.alt) out.push_back(p.alt);
    return out;
}

// The palette whose side bar views hold `v` (one of them or a view in one), docked in the side bar: its mode; else
// none. (The main window's right click on it offers Make floating: sdlPaletteFloatItem.)
std::optional<Mode> floatPaletteDockedAt(const draw::View* v) {
    const Mode m = gState.mode;
    if (gFloatPalOpen && gFloatPalMode == m) return std::nullopt;
    const std::vector<draw::View*> views = floatPaletteViews(floatPaletteParts(m));
    for (; v; v = v->parent())
        if (std::find(views.begin(), views.end(), v) != views.end()) return v->shown() ? std::optional<Mode>(m) : std::nullopt;
    return std::nullopt;
}
void shellFloatPalette(Mode m, bool on);
std::wstring accessFree(std::wstring s);  // (sdl_menus.inc)
// The context menus' item that floats the palette of `m` (or docks it while it floats).
kit::MenuItem sdlPaletteFloatItem(Mode m) {
    kit::MenuItem it;
    it.label = accessFree(floatPaletteMenuLabel(m));
    it.run = [m] { shellFloatPalette(m, !gFloatPal[int(m)].floating); };
    return it;
}

// A palette window inside the main window (the floating palette, the panels: they belong to the map tab): the window
// moves in when a part of it is outside the main window's area (or as much as fits). False: it was inside.
bool sdlPanelKeepInside(SDL_Window* w, int* x = nullptr, int* y = nullptr) {
    SDL_Window* main = gHost ? gHost->mainWindow() : nullptr;
    if (!w || !main) return false;
    int mx = 0, my = 0, mw = 0, mh = 0, px = 0, py = 0, pw = 0, ph = 0;
    SDL_GetWindowPosition(main, &mx, &my), SDL_GetWindowSize(main, &mw, &mh);
    SDL_GetWindowPosition(w, &px, &py), SDL_GetWindowSize(w, &pw, &ph);
    if (x) px = *x;
    if (y) py = *y;
    const int nx = std::max(mx, std::min(px, mx + mw - pw)), ny = std::max(my, std::min(py, my + mh - ph));
    int cx = 0, cy = 0;
    SDL_GetWindowPosition(w, &cx, &cy);
    if (nx == cx && ny == cy) return false;
    SDL_SetWindowPosition(w, nx, ny);
    return !(x && y && nx == *x && ny == *y);
}

void sdlPanelCollapse(PanelStripView& v, bool on, int& fullH);  // (sdl_palette_panels.inc)

// The height of a palette window's strip (core/views_palette_float.inc: PanelStripView): 32 px and its line.
int panelStripH() { return scale(32) + 1; }

// The window's hit test: the strip moves the window (the system's drag: smooth; the window comes back inside the main
// window when the drag ends, sdlPanelKeepInside); on Windows and Linux (no frame of the system's: drawFrame) its edges
// and corners change its size too.
SDL_HitTestResult sdlPanelHitTest(SDL_Window* win, const SDL_Point* p, void* data) {
    const PanelStripView& v = *static_cast<const PanelStripView*>(data);
    const float d = SDL_GetWindowPixelDensity(win);
    const draw::Point at{int(p->x * d), int(p->y * d)};
    const int e = scale(5), w = v.width(), h = v.height();
    if (!v.collapsed && v.drawFrame) {
        const bool l = at.x < e, r = at.x >= w - e, t = at.y < e, b = at.y >= h - e;
        if (t && l) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (t && r) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (b && l) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (b && r) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (l) return SDL_HITTEST_RESIZE_LEFT;
        if (r) return SDL_HITTEST_RESIZE_RIGHT;
        if (b) return SDL_HITTEST_RESIZE_BOTTOM;
        if (t && !v.dragAt(at)) return SDL_HITTEST_RESIZE_TOP;
    }
    if (v.dragAt(at)) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

// A palette window's look and drag (the floating palette's, a panel's): the slim strip in place of the system's title
// bar (core/views_palette_float.inc: PanelStripView; macOS: the title bar shows nothing, macSetupToolWindow; Windows and
// Linux: no frame of the system's, a line around it, the edges and the strip through the hit test), and a drag on the
// strip moves the window with the mouse, kept in the main window (Wayland: the system moves it; it cannot be kept in).
void sdlPanelWindowSetup(PanelStripView& v) {
    SDL_Window* sw = gHost ? gHost->sdlWindow(v) : nullptr;
    if (!sw) return;
    v.titleH = panelStripH();
    if (!macSetupToolWindow(sw)) {
        SDL_SetWindowBordered(sw, false);
        v.drawFrame = true;
    }
    SDL_SetWindowHitTest(sw, sdlPanelHitTest, &v);
}
// After a drag (no mouse button down): the window back inside the main window.
void sdlPanelKeepInsideAfterDrag(SDL_Window* w) {
    if (!(SDL_GetGlobalMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) sdlPanelKeepInside(w);
}

// The screens' usable areas, in SDL's window coordinates.
std::vector<RECT> floatPaletteScreens() {
    std::vector<RECT> out;
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n; ++i) {
        SDL_Rect r{};
        if (SDL_GetDisplayUsableBounds(ids[i], &r) && r.w > 0 && r.h > 0) out.push_back(RECT{r.x, r.y, r.x + r.w, r.y + r.h});
    }
    SDL_free(ids);
    return out;
}

void floatPaletteClosedByUser();

// The views go back into the main window's side bar (the pie palette stays the last child: over the map). A place that
// is not saved yet is saved now.
void floatPaletteViewsBack() {
    const std::vector<draw::View*> views = floatPaletteViews(gFloatPalView.parts());
    gFloatPalView.release();
    if (gFloatPalSaveAt) gFloatPalSaveAt = 0, saveDock();
    if (!gRoot) return;
    for (draw::View* v : views) gRoot->add(*v, draw::Rect(0, 0, 0, 0));
    gRoot->add(gRoot->pie, gRoot->pie.frame());
}

// The window closes (not by the user: the palette stays floating) and its views go back.
void floatPaletteClose() {
    if (!gFloatPalOpen) return;
    gFloatPalClosing = true;
    if (gHost) gHost->closeWindow(gFloatPalView);
    gFloatPalClosing = false;
    gFloatPalOpen = false;
    floatPaletteViewsBack();
}

// The modes' floating palettes (Terrain, Units, Doodads, Sprites: palettes of their own, sdl_palette_panels.inc) open
// or close for the state; Terrain's holds the Tiles panel in Rectangle mode. True: the palette of `active` floats.
bool floatPalettesSync(Mode active, bool shown, bool alt, draw::Rect slot);

// Opens or closes the windows for the state (MainView::relayout): each floating palette shows in every mode while the
// Map workspace shows a map. `m`: the active mode; `shown`: the Map workspace shows a map and the side bar has a place
// for the palette (`slot`, in the main window); `alt`: Rectangle mode's Tiles panel. The location list's window holds
// the side bar's location list and its filter (only Locations mode shows them in the side bar). True: the palette of
// `m` is in its window (the side bar shows the note).
bool locationListSync(bool shown, draw::Rect slot);
bool floatPaletteSync(Mode m, bool shown, bool alt, draw::Rect slot) {
    static bool busy = false;
    if (busy) return gFloatPal[int(m)].floating;
    busy = true;
    const bool others = floatPalettesSync(m, shown, alt, slot);
    busy = false;
    gFloatNote.setPalette(m);  // (the side bar's note, while it floats)
    const bool loc = locationListSync(shown, slot);
    return m == Mode::Locations ? loc : others;
}

// The location list's window (gFloatPalView, with the side bar's location views) while the list floats.
bool locationListSync(bool shown, draw::Rect slot) {
    const Mode m = Mode::Locations;
    static bool busy = false;
    if (busy) return gFloatPalOpen;
    const bool want = gFloatPal[int(m)].floating && shown && gHost && gRoot;
    if (gFloatPalOpen && !want) {
        busy = true;
        floatPaletteClose();
        busy = false;
    }
    if (gFloatPalOpen) return true;
    if (!want) return false;
    busy = true;
    const FloatPalette& kept = gFloatPal[int(m)];
    gFloatPalMode = m;
    gFloatPalView.name = kModeNames[int(m)];
    gFloatPalView.dock.setTip(kFloatPalNames[int(m)].tip);
    gFloatPalView.altShown = false;
    gFloatPalView.collapsed = false;  // (a window opens whole)
    gFloatPalView.toolsMode = Mode::Locations;  // (its whole panel: Locations' tools over the filter and the list)
    gFloatPalView.hold(floatPaletteParts(m));
    // The first place: over the map, at the side bar's edge, as wide as the side bar or more; the size that the settings
    // keep (points; a snapshot's window has one pixel to a point).
    const draw::Rect map = gRoot->map.frame();
    int w = std::max(slot.width(), scale(260)), h = std::clamp(map.height() - scale(32), scale(300), scale(600));
    if (kept.w > 0 && kept.h > 0) w = kept.w, h = kept.h;
    const bool right = slot.left >= map.right;  // (the palette's side bar is on the right of the map)
    const int x = right ? std::max(map.left, map.right - scale(16) - w) : std::max(slot.right, map.left) + scale(16), y = map.top + scale(16);
    gFloatPalShotAt = draw::Rect(x, y, x + w, y + h);
    gHost->openWindow(*gRoot, gFloatPalView, draw::WindowSpec{draw::WindowKind::Tool, gFloatPalShotAt, kModeNames[int(m)]}, [] {
        if (!gFloatPalClosing) floatPaletteClosedByUser();
    });
    gFloatPalOpen = gFloatPalView.host() != nullptr;  // (no window: the main window is not shown yet; it opens later)
    if (!gFloatPalOpen) floatPaletteViewsBack();
    if (SDL_Window* sw = gFloatPalOpen ? gHost->sdlWindow(gFloatPalView) : nullptr) {
        SDL_SetWindowMinimumSize(sw, kFloatPalMinW, kFloatPalMinH);
        sdlPanelWindowSetup(gFloatPalView);
        if (floatPaletteOnScreens(kept, floatPaletteScreens())) {  // the kept place, when it is still on a screen
            SDL_SetWindowSize(sw, kept.w, kept.h);
            SDL_SetWindowPosition(sw, kept.x, kept.y);
        }
        sdlPanelKeepInside(sw);
    }
    if (gFloatPalOpen && !gHost->sdlWindow(gFloatPalView)) gFloatPalView.titleH = panelStripH();  // (a snapshot: the strip, as in the window)
    if (gFloatPalOpen) gFloatPalView.place();
    busy = false;
    return gFloatPalOpen;
}

// View > Floating palettes, a palette's View menu, Dock: the state of the palette of `m` changes, is saved, and its
// window opens (in its mode) or closes.
bool floatingPanelSet(Mode m, bool on);  // (sdl_palette_panels.inc: the active tab's floating palette of m)
void shellFloatPalette(Mode m, bool on) {
    gFloatPalLines = true;
    FloatPalette& p = gFloatPal[int(m)];
    if (m != Mode::Locations) {  // (a palette of the active tab's own, kept with the map's panels; gFloatPal: its place)
        if (!floatingPanelSet(m, on)) return;
        if (gRoot) gRoot->relayout();
        shellMenusChanged();
        setMessage(on ? kFloatPalNames[int(m)].moved : kFloatPalNames[int(m)].docked);
        return;
    }
    if (p.floating == on) return;
    p.floating = on;
    saveDock();
    if (gRoot) gRoot->relayout();
    shellMenusChanged();
    const FloatPaletteNames& n = kFloatPalNames[int(m)];
    if (on) setMessage(n.moved);
    else setMessage(n.docked);
}

// The window's close button: the palette docks (the window is gone; its views go back).
void floatPaletteClosedByUser() {
    gFloatPalOpen = false;
    floatPaletteViewsBack();
    shellFloatPalette(gFloatPalMode, false);
}

// Each turn of the event loop: the place and the size of the window as the user moves it; they are saved half a second
// after the last change (not at each step of a drag).
void sdlFloatPaletteFrame() {
    if (!gFloatPalOpen || !gHost) return;
    SDL_Window* w = gHost->sdlWindow(gFloatPalView);
    if (!w) return;
    FloatPalette& kept = gFloatPal[int(gFloatPalMode)];
    FloatPalette now = kept;
    sdlPanelKeepInsideAfterDrag(w);
    SDL_GetWindowPosition(w, &now.x, &now.y);
    SDL_GetWindowSize(w, &now.w, &now.h);
    if (gFloatPalView.collapsed) now.h = kept.h;  // (its height while collapsed is the strip's: the kept one stays)
    if (!(now == kept)) kept = now, gFloatPalSaveAt = SDL_GetTicks() + 500;
    if (gFloatPalSaveAt && SDL_GetTicks() >= gFloatPalSaveAt) gFloatPalSaveAt = 0, saveDock();
}

// The wiring, once (main_sdl.cpp, after the main window shows): Dock in the window and in the side bar's note, the
// keys of the window; then the window opens when the settings keep the active mode's palette floating.
void palettePanelsSetup();  // (sdl_palette_panels.inc)
void sdlFloatPaletteSetup() {
    palettePanelsSetup();
    gFloatPalView.onDock = [] { shellPost([] { shellFloatPalette(gFloatPalMode, false); }); };  // (after the click: the window closes)
    gFloatNote.onDock = [] { shellPost([] { shellFloatPalette(gState.mode, false); }); };
    gFloatPalView.keys = [](const draw::KeyEvent& e) { return gRoot && gRoot->key(e); };
    // The collapse box: the window becomes its strip, and back (its height is not saved while it is collapsed).
    gFloatPalView.onCollapse = [] {
        shellPost([] {
            static int full = 0;
            sdlPanelCollapse(gFloatPalView, !gFloatPalView.collapsed, full);
            gFloatPalView.place();
        });
    };
    // The duplicate box: a panel like the floating palette (its mode, view and order), by its window.
    gFloatPalView.onDuplicate = [] {
        const Mode m = gFloatPalMode;
        shellPost([m] { palettePanelNew(m, gViewByMode[int(m)], gSortByMode[int(m)], &gFloatPalView); });
    };
    for (const FloatPalette& p : gFloatPal)
        if (p.floating) gFloatPalLines = true;
    if (!gRoot) return;
    // (a snapshot with no input: the root is in the host only while a picture is made; the window is a picture over it)
    const bool lone = gHost && !gRoot->host();
    if (lone) gRoot->attach(gHost);
    gRoot->relayout();
    if (lone) gRoot->attach(nullptr);
}

// The window of the active mode's floating palette (the location list's, or the tab's floating palette's), or null.
draw::View* floatingWindowOf(Mode m);  // (sdl_palette_panels.inc)
std::string floatingPanelLine(Mode m);  // (sdl_palette_panels.inc: the part of the line for a tab's floating palette)

// A snapshot's line after each input (sdl_menus.inc: sdlInputLine), once a floating palette is in use: the active
// mode's palette, its state, the window's size in pixels and its place (a snapshot: in the main window's pixels; else
// on the screens), the pick, the view under the views' root, its kept place, and the palettes that float.
void sdlFloatPaletteLine() {
    if (!gFloatPalLines) return;
    const Mode a = gState.mode;
    std::string floating;
    for (const Mode m : kModeOrder)
        if (paletteFloats(m)) floating += (floating.empty() ? "" : ",") + floatPaletteShort(m);
    if (floating.empty()) floating = "none";
    const std::string name = floatPaletteShort(a);
    if (!paletteFloats(a)) {
        bool inSide = gRoot != nullptr;
        for (draw::View* v : floatPaletteViews(floatPaletteParts(a))) inSide = inSide && v->parent() == gRoot;
        return void(std::printf("  palette: docked %s, %s; pick=%d; floating=%s\n", name.c_str(), inSide ? "in the side bar" : "not in the side bar (WRONG)",
                                gState.palette, floating.c_str()));
    }
    if (a != Mode::Locations) return void(std::printf("  palette: floating %s, %s; pick=%d; floating=%s\n", name.c_str(), floatingPanelLine(a).c_str(), gState.palette, floating.c_str()));
    if (!gFloatPalOpen) return void(std::printf("  palette: floating %s, no window; pick=%d; floating=%s\n", name.c_str(), gState.palette, floating.c_str()));
    int x = gFloatPalShotAt.left, y = gFloatPalShotAt.top;
    if (SDL_Window* w = gHost ? gHost->sdlWindow(gFloatPalView) : nullptr) SDL_GetWindowPosition(w, &x, &y);
    draw::Point size;  // (the window that takes a snapshot's keys now: the palette's when it is the last one opened)
    const bool keysHere = gHost && gRoot && &gHost->snapshotTop(*gRoot, size) == &gFloatPalView;
    const FloatPaletteParts& parts = gFloatPalView.parts();
    const FloatPalette& kept = gFloatPal[int(a)];
    std::printf("  palette: floating %s, window %dx%d px at %d,%d, %s; pick=%d; keys=%s; search=\"%s\"%s; kept %d,%d %dx%d; floating=%s\n", name.c_str(),
                gFloatPalView.width(), gFloatPalView.height(), x, y, parts.list && parts.list->parent() == &gFloatPalView ? "list" : "empty (WRONG)", gState.palette,
                keysHere ? "window" : "main", narrow(gFilterSdl.field().text()).c_str(), gFilterSdl.field().hasFocus() ? " (typing)" : "", kept.x, kept.y, kept.w,
                kept.h, floating.c_str());
}

// --check floatpalette:<folder>: the settings keep each palette's state. Two palettes' states (terrain, units) go into a
// new settings file in <folder> and a second Settings reads them back; one docks again and the other keeps its own;
// then the rule of the kept place: on a screen, on a second screen, off the screens.
bool sdlFloatPaletteCheck(const std::string& name, std::string& out) {
    if (name.rfind("floatpalette:", 0) != 0) return false;
    gFloatPalLines = true;
    const fs::path file = fs::path(widen(name.substr(13))) / L"float-settings.json";
    FloatPalette p, q;
    p.floating = true, p.x = 1700, p.y = 120, p.w = 320, p.h = 480;
    q.floating = true, q.x = 240, q.y = 300, q.w = 300, q.h = 420;
    {
        stardraft::Settings s(file);
        floatPaletteSave(s, Mode::Terrain, p);
        floatPaletteSave(s, Mode::Units, q);
    }
    char buf[320];
    {
        const stardraft::Settings again(file);
        for (const auto& [m, want] : {std::pair{Mode::Terrain, p}, std::pair{Mode::Units, q}}) {
            const FloatPalette back = floatPaletteLoad(again, m);
            std::snprintf(buf, sizeof buf, "floatpalette: saved %s floating %d at %d,%d %dx%d, read back floating %d at %d,%d %dx%d %s\n",
                          floatPaletteShort(m).c_str(), int(want.floating), want.x, want.y, want.w, want.h, int(back.floating), back.x, back.y, back.w, back.h,
                          back == want ? "(yes)" : "(WRONG)");
            out += buf;
        }
        const FloatPalette none = floatPaletteLoad(again, Mode::Doodads);
        if (!(none == FloatPalette{})) out += "floatpalette: the doodad palette has a state that nobody saved (WRONG)\n";
    }
    FloatPalette docked = p;
    docked.floating = false;
    {
        stardraft::Settings s(file);
        floatPaletteSave(s, Mode::Terrain, docked);
    }
    const stardraft::Settings third(file);
    const FloatPalette back2 = floatPaletteLoad(third, Mode::Terrain), back3 = floatPaletteLoad(third, Mode::Units);
    out += std::string("floatpalette: the terrain palette docked again, read back ") + (back2 == docked ? "docked with the same place (yes)" : "another state (WRONG)") +
           "; the unit palette " + (back3 == q ? "still floats at its own place (yes)" : "has another state (WRONG)") + "\n";
    const std::vector<RECT> one{RECT{0, 0, 1600, 900}}, two{RECT{0, 0, 1600, 900}, RECT{1600, 0, 3520, 1080}};
    const bool onSecond = floatPaletteOnScreens(p, two), gone = floatPaletteOnScreens(p, one);
    FloatPalette edge = p;
    edge.x = 1580;  // (only 20 points of its title bar on the screen)
    out += std::string("floatpalette: the place on the second screen ") + (onSecond ? "comes back (yes)" : "does not come back (WRONG)") +
           "; with that screen gone " + (!gone ? "a new place (yes)" : "the old place (WRONG)") + "; 20 points on a screen " +
           (!floatPaletteOnScreens(edge, one) ? "a new place (yes)" : "the old place (WRONG)") + "\n";
    return true;
}

// The script's verb palette (sdl_soak.inc): "close", the window's close button (SDL's close request for it); "scale
// <percent>", the window moves to a screen of that scale (SDL's scale event for it): a line says the program's scale.
void sdlFloatPaletteScript(const std::string& what) {
    if (what.rfind("scale ", 0) == 0) {
        const float s = float(std::atof(what.c_str() + 6)) / 100.0f;
        // (the screens' scale, not the test's --scale: the program's scale is the main window's screen's, 1 here)
        const float before = gHost ? gHost->scale() : 0, after = gHost ? (gHost->setScale(0), gHost->scale()) : 0;
        draw::View* win = floatingWindowOf(gState.mode);
        if (!win || !gHost || s <= 0) std::printf("palette %s: no window or no scale (WRONG)\n", what.c_str());
        else gHost->snapshotScaleChanged(*win, s);
        runPosted();
        sdlInputLine("palette " + what);
        std::printf("  palette scale: the program's scale %.2f, %.2f before\n", gHost ? double(gHost->scale()) : 0.0, double(after));
        if (gHost) gHost->setScale(before);
        return;
    }
    if (what != "close") return void(std::printf("palette %s: not close or scale (WRONG)\n", what.c_str()));
    draw::View* win = floatingWindowOf(gState.mode);
    if (!win || !gHost) std::printf("palette close: no window (WRONG)\n");
    else gHost->snapshotClose(*win);
    runPosted();
    sdlInputLine("palette close");
}

// The end of a snapshot's input (SdlHost::snapshotInputEnd): its windows close, the palette's too (not by the user: it
// stays floating). The window opens again as a picture over the root (the host's snapshot with no window paints it at
// its place), for the frames after the input.
void palettePanelsWindowsGone(bool before);  // (sdl_palette_panels.inc)
void sdlFloatPaletteInputEnd(kit_sdl::SdlHost& host, draw::View& root) {
    const bool was = gFloatPalOpen;
    gFloatPalClosing = true;
    palettePanelsWindowsGone(true);  // (the palette panels' windows close too; the panels stay and open again)
    host.snapshotInputEnd();
    gFloatPalClosing = false;
    const bool panels = (palettePanelsWindowsGone(false), true);
    if ((!was && !panels) || !gRoot) return;
    gFloatPalOpen = false;
    if (was) floatPaletteViewsBack();
    root.attach(&host);
    gRoot->relayout();
    root.attach(nullptr);
}

// A pick in the palette's window gives the keyboard to the map (shellFocusView): the window's search box loses it (the
// window's root has it; the keys that the root does not take go to the main window), and the main window gets the
// system's keyboard focus when the palette's window had it.
void palettePanelsKeysToMap();  // (sdl_palette_panels.inc: the same for the panels' windows)
void floatPaletteKeysToMap() {
    palettePanelsKeysToMap();
    if (!gFloatPalOpen || !gHost || !gFloatPalView.host()) return;
    gFloatPalView.focus();
    SDL_Window* pw = gHost->sdlWindow(gFloatPalView);
    if (SDL_Window* m = gHost->mainWindow(); pw && m && SDL_GetKeyboardFocus() == pw) SDL_RaiseWindow(m);
}
