// The floating terrain palette on this system (Roadmap R40, as ScmDraft 2's terrain window): in Terrain mode the
// palette's views (the search box, the View button, the list; Rectangle mode's Tiles panel) can be in a window of their
// own, a tool window of the host (kit_sdl/sdl_host.h: WindowKind::Tool) that stays over the main window, can move (also
// to another screen) and change its size. The kit draws each host window with its own ImGui draw list into its own
// swapchain, so the palette's window is drawn as the dialogs are (no ImGui multi-viewport).
// The state, its place and its size are in the settings (core/dock_prefs.inc: gFloatPal, Layout/Palette...); the place
// comes back at the next start when it is still on a screen. The window does not take the keyboard when it opens; the
// keys that no view of it takes go to the main window (the map's keys and the menus' shortcuts work from it), and a pick
// in it gives the keyboard to the map (as a pick in the side bar does). Dock, or the window's close button, puts the
// views back in the side bar. Included by main_sdl.cpp after sdl_main_view.inc.

void sdlInputLine(const std::string& what);  // (sdl_menus.inc)

FloatPaletteView gFloatPalView;
FloatPaletteNoteView gFloatNote;
bool gFloatPalOpen = false;      // the window is open, and the palette's views are in it
bool gFloatPalClosing = false;   // the program closes the window (another mode): the palette stays floating
bool gFloatPalLines = false;     // a snapshot's input lines have the palette's line (sdlFloatPaletteLine)
draw::Rect gFloatPalShotAt;      // where the window opened, in the main window's pixels (a snapshot shows it there)
Uint64 gFloatPalSaveAt = 0;      // the moved or resized window's place is saved then (0: nothing to save)

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

// The views go back into the main window's side bar (the pie palette stays the last child: over the map).
void floatPaletteViewsBack() {
    gFloatPalView.release();
    if (!gRoot) return;
    for (draw::View* v : {static_cast<draw::View*>(&gSearchSdl), static_cast<draw::View*>(&gViewBtnSdl), static_cast<draw::View*>(&gPalList),
                          static_cast<draw::View*>(&gTilesPanel)})
        gRoot->add(*v, draw::Rect(0, 0, 0, 0));
    gRoot->add(gRoot->pie, gRoot->pie.frame());
}

// Opens or closes the window for the state (MainView::relayout). `terrainShown`: the Map workspace shows a map in
// Terrain mode; `tiles`: Rectangle mode's Tiles panel; `slot`: the views' place in the side bar.
bool floatPaletteSync(bool terrainShown, bool tiles, draw::Rect slot) {
    static bool busy = false;
    const bool want = gFloatPal.floating && terrainShown && gHost && gRoot;
    gFloatPalView.setTilesShown(tiles);
    if (want == gFloatPalOpen || busy) return gFloatPalOpen;
    busy = true;
    if (want) {
        gFloatPalView.hold(gSearchSdl, gViewBtnSdl, gPalList, gTilesPanel);
        // The first place: over the map's top left corner, as wide as the side bar or more; the size that the settings
        // keep (points; a snapshot's window has one pixel to a point).
        const draw::Rect m = gRoot->map.frame();
        int w = std::max(slot.width(), scale(260)), h = std::clamp(m.height() - scale(32), scale(300), scale(600));
        if (gFloatPal.w > 0 && gFloatPal.h > 0) w = gFloatPal.w, h = gFloatPal.h;
        const int x = std::max(slot.right, m.left) + scale(16), y = m.top + scale(16);
        gFloatPalShotAt = draw::Rect(x, y, x + w, y + h);
        gHost->openWindow(*gRoot, gFloatPalView, draw::WindowSpec{draw::WindowKind::Tool, gFloatPalShotAt, L"Terrain palette"}, [] {
            if (!gFloatPalClosing) floatPaletteClosedByUser();
        });
        gFloatPalOpen = gFloatPalView.host() != nullptr;  // (no window: the main window is not shown yet; it opens later)
        if (!gFloatPalOpen) floatPaletteViewsBack();
        if (SDL_Window* sw = gFloatPalOpen ? gHost->sdlWindow(gFloatPalView) : nullptr) {
            SDL_SetWindowMinimumSize(sw, kFloatPalMinW, kFloatPalMinH);
            if (floatPaletteOnScreens(gFloatPal, floatPaletteScreens())) {  // the kept place, when it is still on a screen
                SDL_SetWindowSize(sw, gFloatPal.w, gFloatPal.h);
                SDL_SetWindowPosition(sw, gFloatPal.x, gFloatPal.y);
            }
        }
        if (gFloatPalOpen) gFloatPalView.place();
    } else {
        gFloatPalClosing = true;
        gHost->closeWindow(gFloatPalView);
        gFloatPalClosing = false;
        gFloatPalOpen = false;
        floatPaletteViewsBack();
    }
    busy = false;
    return gFloatPalOpen;
}

// View > Floating terrain palette, the palette's View menu, Dock: the state changes, is saved, and the window opens
// (in Terrain mode) or closes.
void shellFloatPalette(bool on) {
    gFloatPalLines = true;
    if (gFloatPal.floating == on) return;
    gFloatPal.floating = on;
    saveDock();
    if (gRoot) gRoot->relayout();
    shellMenusChanged();
    if (on) setMessage(gFloatPalOpen ? L"Moved the terrain palette to its own window" : L"The terrain palette shows in its own window in Terrain mode");
    else setMessage(L"Docked the terrain palette");
}

// The window's close button: the palette docks (the window is gone; its views go back).
void floatPaletteClosedByUser() {
    gFloatPalOpen = false;
    floatPaletteViewsBack();
    shellFloatPalette(false);
}

// Each turn of the event loop: the place and the size of the window as the user moves it; they are saved half a second
// after the last change (not at each step of a drag).
void sdlFloatPaletteFrame() {
    if (!gFloatPalOpen || !gHost) return;
    SDL_Window* w = gHost->sdlWindow(gFloatPalView);
    if (!w) return;
    FloatPalette now = gFloatPal;
    SDL_GetWindowPosition(w, &now.x, &now.y);
    SDL_GetWindowSize(w, &now.w, &now.h);
    if (!(now == gFloatPal)) gFloatPal = now, gFloatPalSaveAt = SDL_GetTicks() + 500;
    if (gFloatPalSaveAt && SDL_GetTicks() >= gFloatPalSaveAt) gFloatPalSaveAt = 0, saveDock();
}

// The wiring, once (main_sdl.cpp, after the main window shows): Dock in the window and in the side bar's note, the
// keys of the window; then the window opens when the settings keep the palette floating.
void sdlFloatPaletteSetup() {
    gFloatPalView.onDock = gFloatNote.onDock = [] { shellPost([] { shellFloatPalette(false); }); };  // (after the click: the window closes)
    gFloatPalView.keys = [](const draw::KeyEvent& e) { return gRoot && gRoot->key(e); };
    if (gFloatPal.floating) gFloatPalLines = true;
    if (!gRoot) return;
    // (a snapshot with no input: the root is in the host only while a picture is made; the window is a picture over it)
    const bool lone = gHost && !gRoot->host();
    if (lone) gRoot->attach(gHost);
    gRoot->relayout();
    if (lone) gRoot->attach(nullptr);
}

// A snapshot's line after each input (sdl_menus.inc: sdlInputLine), once the floating palette is in use: its state,
// the window's size in pixels and its place (a snapshot: in the main window's pixels; else on the screens), the
// Terrain pick, and the view under the views' root.
void sdlFloatPaletteLine() {
    if (!gFloatPalLines) return;
    const bool inSide = gRoot && gPalList.parent() == gRoot && gTilesPanel.parent() == gRoot;
    if (!gFloatPal.floating)
        return void(std::printf("  palette: docked, %s; pick=%d\n", inSide ? "in the side bar" : "not in the side bar (WRONG)", gState.palette));
    if (!gFloatPalOpen) return void(std::printf("  palette: floating, no window (not in Terrain mode); pick=%d\n", gState.palette));
    int x = gFloatPalShotAt.left, y = gFloatPalShotAt.top;
    if (SDL_Window* w = gHost ? gHost->sdlWindow(gFloatPalView) : nullptr) SDL_GetWindowPosition(w, &x, &y);
    draw::Point size;  // (the window that takes a snapshot's keys now: the palette's when it is the last one opened)
    const bool keysHere = gHost && gRoot && &gHost->snapshotTop(*gRoot, size) == &gFloatPalView;
    std::printf("  palette: floating, window %dx%d px at %d,%d, %s; pick=%d; keys=%s; kept %d,%d %dx%d\n", gFloatPalView.width(), gFloatPalView.height(), x,
                y, gFloatPalView.tilesShown ? "tiles" : gPalList.parent() == &gFloatPalView ? "list" : "empty (WRONG)", gState.palette,
                keysHere ? "window" : "main", gFloatPal.x, gFloatPal.y, gFloatPal.w, gFloatPal.h);
}

// --check floatpalette:<folder>: the settings keep the state. A state goes into a new settings file in <folder> and a
// second Settings reads it back; then the rule of the kept place: on a screen, on a second screen, off the screens.
bool sdlFloatPaletteCheck(const std::string& name, std::string& out) {
    if (name.rfind("floatpalette:", 0) != 0) return false;
    gFloatPalLines = true;
    const fs::path file = fs::path(widen(name.substr(13))) / L"float-settings.json";
    FloatPalette p;
    p.floating = true, p.x = 1700, p.y = 120, p.w = 320, p.h = 480;
    {
        stardraft::Settings s(file);
        floatPaletteSave(s, p);
    }
    const stardraft::Settings again(file);
    const FloatPalette back = floatPaletteLoad(again);
    char buf[320];
    std::snprintf(buf, sizeof buf, "floatpalette: saved floating %d at %d,%d %dx%d, read back floating %d at %d,%d %dx%d %s\n", int(p.floating), p.x, p.y,
                  p.w, p.h, int(back.floating), back.x, back.y, back.w, back.h, back == p ? "(yes)" : "(WRONG)");
    out += buf;
    FloatPalette docked = p;
    docked.floating = false;
    {
        stardraft::Settings s(file);
        floatPaletteSave(s, docked);
    }
    const FloatPalette back2 = floatPaletteLoad(stardraft::Settings(file));
    out += std::string("floatpalette: docked again, read back ") + (back2 == docked ? "docked with the same place (yes)" : "another state (WRONG)") + "\n";
    const std::vector<RECT> one{RECT{0, 0, 1600, 900}}, two{RECT{0, 0, 1600, 900}, RECT{1600, 0, 3520, 1080}};
    const bool onSecond = floatPaletteOnScreens(p, two), gone = floatPaletteOnScreens(p, one);
    FloatPalette edge = p;
    edge.x = 1580;  // (only 20 points of its title bar on the screen)
    out += std::string("floatpalette: the place on the second screen ") + (onSecond ? "comes back (yes)" : "does not come back (WRONG)") +
           "; with that screen gone " + (!gone ? "a new place (yes)" : "the old place (WRONG)") + "; 20 points on a screen " +
           (!floatPaletteOnScreens(edge, one) ? "a new place (yes)" : "the old place (WRONG)") + "\n";
    return true;
}

// The script's verb palette (sdl_soak.inc): "close", the window's close button (SDL's close request for it).
void sdlFloatPaletteScript(const std::string& what) {
    if (what != "close") return void(std::printf("palette %s: not close (WRONG)\n", what.c_str()));
    if (!gFloatPalOpen || !gHost) std::printf("palette close: no window (WRONG)\n");
    else gHost->snapshotClose(gFloatPalView);
    runPosted();
    sdlInputLine("palette close");
}

// The end of a snapshot's input (SdlHost::snapshotInputEnd): its windows close, the palette's too (not by the user: it
// stays floating). The window opens again as a picture over the root (the host's snapshot with no window paints it at
// its place), for the frames after the input.
void sdlFloatPaletteInputEnd(kit_sdl::SdlHost& host, draw::View& root) {
    const bool was = gFloatPalOpen;
    gFloatPalClosing = true;
    host.snapshotInputEnd();
    gFloatPalClosing = false;
    if (!was || !gRoot) return;
    gFloatPalOpen = false;
    floatPaletteViewsBack();
    root.attach(&host);
    gRoot->relayout();
    root.attach(nullptr);
}
