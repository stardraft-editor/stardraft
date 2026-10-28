// --noactivate, --frames and --wintest: tests of the real window (not a snapshot), which never take the user's desktop.
// --noactivate: the windows do not take the keyboard focus and do not come to the front (SdlHost::setNoActivate,
// SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN), the run is a test run (no sound, the default settings, nothing saved, no file
// types), and the pipe that a second program sends its map on is a test pipe of its own (shell_sdl_win.inc: pipeName;
// Linux and the BSDs: a test socket of its own, sdl_xdg.inc: instanceSocket).
// --frames N: the program ends after N frames went to the window. --wintest <map>: the checks below, in the window,
// one after the other; each prints what it saw (a "(WRONG)" line when it is not right), then the program ends (exit
// code 1 when a check was wrong). <map> is the second program's map (Windows: the single-instance pipe's check); "-":
// no second program. With no map open (no game data, as on GitHub's machines) the checks of the map's picture do not
// run. --wintest-fullscreen: also full screen on and off (macOS: a space of its own; only on a computer that no user
// works on). Included by main_sdl.cpp before main().

long long gFramesArg = 0;      // --frames
std::string gWinTestMap;       // --wintest: the second program's map (the single-instance pipe's check), or "-"
bool gWinTestFull = false;     // --wintest-fullscreen
int gWinTestWrong = 0, gWinTestChecks = 0;

void winTestLine(bool ok, const std::string& text) {
    ++gWinTestChecks;
    if (!ok) ++gWinTestWrong;
    std::printf("wintest: %s%s\n", text.c_str(), ok ? "" : " (WRONG)");
}

// The distinct colors of the map's texture (every 7th pixel): a drawn map has many, an empty or black one 1.
int winTestMapColors() {
    if (!gMapTex || gMapW <= 0 || gMapH <= 0 || !gDevice) return 0;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = uint32_t(gMapW * gMapH * 4);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gDevice, &ti);
    if (!tb) return 0;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = gMapTex, src.w = uint32_t(gMapW), src.h = uint32_t(gMapH), src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(gDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(gDevice, fence);
    std::set<uint32_t> colors;
    if (const uint32_t* p = static_cast<const uint32_t*>(SDL_MapGPUTransferBuffer(gDevice, tb, false))) {
        for (size_t i = 0; i < size_t(gMapW) * size_t(gMapH); i += 7) colors.insert(p[i] & 0xFFFFFFu);
        SDL_UnmapGPUTransferBuffer(gDevice, tb);
    }
    SDL_ReleaseGPUTransferBuffer(gDevice, tb);
    return int(colors.size());
}

#ifdef _WIN32
int winTestDarkAttr(SDL_Window* w) {
    HWND h = winsdl::windowHandle(w);
    BOOL dark = FALSE;
    if (!h || FAILED(DwmGetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark))) return -1;
    return dark ? 1 : 0;
}
#endif

std::string winTestTitle(SDL_Window* w) {
    const char* t = w ? SDL_GetWindowTitle(w) : nullptr;
    return t ? t : "";
}

// The checks, a step at a time (each pump: sdlWinTestPump). A step waits for frames and time first.
struct WinTest {
    int step = 0;
    Uint64 since = 0;
    long long framesAt = 0, missedAt = 0, pumps = 0;
    int mapW = 0, railW = 0, fullW = 0, fullH = 0;
    bool minSeen = false;  // (step 5: the window is minimized now; macOS's animation comes first)
    float camX = 0, camY = 0, zoom = 0, testScale = 1.5f;
    std::wstring mapPath;
#ifdef _WIN32
    PROCESS_INFORMATION second{};
#elif defined(STARDRAFT_XDG)
    SDL_Process* second = nullptr;
#endif
} gWinTest;

void winTestNext() {
    ++gWinTest.step;
    gWinTest.since = SDL_GetTicks();
    gWinTest.framesAt = gHost ? gHost->presentedFrames() : 0;
    gWinTest.missedAt = gHost ? gHost->missedFrames() : 0;
    gWinTest.pumps = 0;
}

// Since the step began: at least `ms` milliseconds and `frames` frames to the window.
bool winTestWaited(Uint64 ms, long long frames) {
    return SDL_GetTicks() - gWinTest.since >= ms && gHost->presentedFrames() - gWinTest.framesAt >= frames;
}

void winTestScaleEvent(SDL_Window* w) {
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;
    e.window.windowID = SDL_GetWindowID(w);
    SDL_PushEvent(&e);
}

bool winTestMapOpen() { return gState.ed && gState.ed->isOpen(); }

void winTestLayout(const char* when, SDL_Window* w) {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    const bool fits = gRoot->width() == pw && gRoot->height() == ph;
    if (!winTestMapOpen()) {  // (no map: the start page fills the window)
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s: window %dx%d px, root %dx%d (no map open)", when, pw, ph, gRoot->width(), gRoot->height());
        return winTestLine(fits && pw > 0 && ph > 0, buf);
    }
    const draw::Point o = gRoot->map.origin();  // (in the window)
    const draw::Rect m(o.x, o.y, o.x + gRoot->map.width(), o.y + gRoot->map.height());
    const bool inside = m.width() > 0 && m.height() > 0 && m.right <= pw && m.bottom <= ph;
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s: window %dx%d px, root %dx%d, map view %dx%d at %d,%d, rail %d px wide, map texture %dx%d", when, pw, ph,
                  gRoot->width(), gRoot->height(), m.width(), m.height(), m.left, m.top, gRoot->rail.width(), gMapW, gMapH);
    winTestLine(fits && inside && gMapW == m.width() && gMapH == m.height(), buf);
}

void sdlWinTestPump() {
    if (gFramesArg > 0 && gHost && gHost->presentedFrames() >= gFramesArg && gWinTestMap.empty()) {
        std::printf("frames: %lld presented, %lld turns without a swapchain texture\n", gHost->presentedFrames(), gHost->missedFrames());
        gHost->quit();
        return;
    }
    if (gWinTestMap.empty() || !gHost || !gRoot) return;
    SDL_Window* w = gHost->mainWindow();
    if (!w) return;
    ++gWinTest.pumps;
    WinTest& t = gWinTest;
    char buf[512];
#ifdef _WIN32
    HWND hwnd = winsdl::windowHandle(w);
#endif
    switch (t.step) {
    case 0:  // the start
        t.since = SDL_GetTicks();
        winTestNext();
        return;
    case 1: {  // the first frames: the swapchain presents, the title, the icon, the title bar, no focus taken
        if (!winTestWaited(500, 10)) {
            if (SDL_GetTicks() - t.since > 10000) winTestLine(false, "presented no frames in 10 s"), t.step = 99;
            return;
        }
        std::snprintf(buf, sizeof buf, "swapchain: %lld frames presented, %lld missed, GPU %s", gHost->presentedFrames(), gHost->missedFrames(),
                      gRenderer ? gRenderer->driver() : "?");
        winTestLine(gHost->presentedFrames() >= 10, buf);
        const std::string title = winTestTitle(w);
        if (winTestMapOpen()) {
            const std::string mapName = narrow(gState.ed->path().filename().wstring());
            winTestLine(!mapName.empty() && title.find(mapName) != std::string::npos && title.find("StarDraft 2 - v") != std::string::npos,
                        "title \"" + title + "\"");
            const int colors = winTestMapColors();
            winTestLine(colors > 50, "map texture: " + std::to_string(colors) + " colors");
        } else {
            winTestLine(title.find("StarDraft 2") != std::string::npos, "title \"" + title + "\" (no map open)");
        }
        t.mapPath = gState.ed ? gState.ed->path().wstring() : L"";
        // The display (macOS: a Retina display has the pixel density 2) and the window on it, in points and pixels.
        {
            const SDL_DisplayID display = SDL_GetDisplayForWindow(w);
            SDL_Rect bounds{};
            SDL_GetDisplayBounds(display, &bounds);
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
            const char* name = SDL_GetDisplayName(display);
            int lw = 0, lh = 0, pw = 0, ph = 0;
            SDL_GetWindowSize(w, &lw, &lh);
            SDL_GetWindowSizeInPixels(w, &pw, &ph);
            std::snprintf(buf, sizeof buf, "display: \"%s\", %dx%d points, mode %dx%d at density %.2f, %.0f Hz; content scale %.2f; the window %dx%d points, "
                          "%dx%d px, pixel density %.2f, display scale %.2f, dpi %d%s", name ? name : "?", bounds.w, bounds.h, mode ? mode->w : 0, mode ? mode->h : 0,
                          mode ? double(mode->pixel_density) : 0.0, mode ? double(mode->refresh_rate) : 0.0, double(SDL_GetDisplayContentScale(display)), lw, lh,
                          pw, ph, double(SDL_GetWindowPixelDensity(w)), double(SDL_GetWindowDisplayScale(w)), kit::theme().dpi,
                          (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) ? ", maximized" : "");
            const float d = SDL_GetWindowPixelDensity(w);
            winTestLine(bounds.w > 0 && d > 0 && std::abs(float(pw) - float(lw) * d) <= 1 && kit::theme().dpi == int(std::lround(96 * SDL_GetWindowDisplayScale(w))), buf);
        }
        winTestLine(SDL_GetKeyboardFocus() != w, std::string("keyboard focus: ") + (SDL_GetKeyboardFocus() == w ? "the test window has it" : "not the test window's"));
#ifdef _WIN32
        winTestLine(GetForegroundWindow() != hwnd, std::string("focus: the test window is ") + (GetForegroundWindow() == hwnd ? "in front" : "not in front"));
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        winTestLine((ex & WS_EX_NOACTIVATE) != 0, std::string("WS_EX_NOACTIVATE ") + ((ex & WS_EX_NOACTIVATE) ? "set" : "not set"));
        const int dark = winTestDarkAttr(w);
        winTestLine(dark == (gLightTheme ? 0 : 1), "title bar: DWMWA_USE_IMMERSIVE_DARK_MODE " + std::to_string(dark) + (gLightTheme ? " (light theme)" : " (dark theme)"));
        HRSRC group = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */);
        HRSRC doc = FindResourceW(nullptr, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(14));
        winTestLine(group && doc, std::string("icon resources: app icon ") + (group ? "yes" : "no") + ", map file icon " + (doc ? "yes" : "no"));
        const HICON big = HICON(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0)), little = HICON(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
        const HICON cls = HICON(GetClassLongPtrW(hwnd, GCLP_HICON));
        std::snprintf(buf, sizeof buf, "taskbar icon: WM_GETICON big %s, small %s, class icon %s", big ? "yes" : "no", little ? "yes" : "no", cls ? "yes" : "no");
        winTestLine(big || cls, buf);
#elif defined(STARDRAFT_XDG)
        // The app id (Wayland) and WM_CLASS (X11) that the dock and the task bar find the .desktop file with.
        const char* appId = SDL_GetHint(SDL_HINT_APP_ID);
        winTestLine(appId && std::string(appId) == stardraft::xdg::kAppId,
                    std::string("app id ") + (appId ? appId : "none") + ", video driver " + (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?"));
        winTestLine(!(SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS), "focus: the test window does not have the keyboard focus");
#endif
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(w, &pw, &ph);
        t.mapW = gRoot->map.width(), t.railW = gRoot->rail.width();
        // A resize to 1100 x 700 pixels (SDL's size is in points).
        const float d = SDL_GetWindowPixelDensity(w) > 0 ? SDL_GetWindowPixelDensity(w) : 1.0f;
        winTestLayout("before the resize", w);
        SDL_SetWindowSize(w, int(1100 / d), int(700 / d));
        winTestNext();
        return;
    }
    case 2: {  // after the resize
        if (!winTestWaited(400, 5)) return;
        winTestLayout("after the resize", w);
        std::snprintf(buf, sizeof buf, "resize: %lld frames presented since", gHost->presentedFrames() - t.framesAt);
        winTestLine(gHost->presentedFrames() - t.framesAt >= 5, buf);
        if (winTestMapOpen())
            winTestLine(gRoot->map.width() != t.mapW, "resize: the map view's width " + std::to_string(t.mapW) + " -> " + std::to_string(gRoot->map.width()));
        else {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(w, &pw, &ph);
            // (a small display: the system keeps the window in its free area, the title bar too)
            SDL_Rect area{};
            SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(w), &area);
            const float d = SDL_GetWindowPixelDensity(w) > 0 ? SDL_GetWindowPixelDensity(w) : 1.0f;
            const bool wide = std::abs(pw - 1100) <= 2 || (pw < 1100 && pw >= int(float(area.w) * d) - 80);
            const bool high = std::abs(ph - 700) <= 2 || (ph < 700 && ph >= int(float(area.h) * d) - 80);
            std::snprintf(buf, sizeof buf, "resize: the window is %dx%d px (asked: 1100x700; the display's free area %dx%d points)", pw, ph, area.w, area.h);
            winTestLine(wide && high, buf);
        }
        // The window moves to a display of 50 % more (SDL's event, with the scale that the display gives): 150 % from
        // a display of 100 %, 250 % from a Retina display (200 %).
        t.mapW = gRoot->map.width(), t.railW = gRoot->rail.width();
        t.testScale = (SDL_GetWindowDisplayScale(w) > 0 ? SDL_GetWindowDisplayScale(w) : 1.0f) + 0.5f;
        std::printf("wintest: display scale %.2f, dpi %d, text %d px\n", double(SDL_GetWindowDisplayScale(w)), kit::theme().dpi, kit::px(15));
        kit_sdl::SdlHost::setTestDisplayScale(t.testScale);
        winTestScaleEvent(w);
        winTestNext();
        return;
    }
    case 3: {  // at 50 % more
        if (!winTestWaited(300, 5)) return;
        const int pct = int(std::lround(t.testScale * 100));
        std::snprintf(buf, sizeof buf, "%d %%: dpi %d, text %d px, icons %d px from the %d px atlas, %lld frames presented", pct, kit::theme().dpi, kit::px(15),
                      kit::px(16), kit_sdl::iconAtlasCell(0, kit::px(16)), gHost->presentedFrames() - t.framesAt);
        winTestLine(kit::theme().dpi == int(std::lround(96 * t.testScale)) && kit::px(15) >= int(15 * t.testScale) - 1, buf);  // (a 24 px icon is the 16 px one, sharp: iconDrawnPx)
        winTestLayout(pct == 150 ? "150 %" : "50 % more", w);
        std::snprintf(buf, sizeof buf, "%d %%: the rail %d -> %d px wide", pct, t.railW, gRoot->rail.width());
        winTestLine(gRoot->rail.width() > t.railW, buf);
        kit_sdl::SdlHost::setTestDisplayScale(0);
        winTestScaleEvent(w);
        winTestNext();
        return;
    }
    case 4: {  // back at the display's own scale
        if (!winTestWaited(300, 5)) return;
        std::snprintf(buf, sizeof buf, "back at the display's scale: dpi %d, rail %d px wide (before %d)", kit::theme().dpi, gRoot->rail.width(), t.railW);
        winTestLine(gRoot->rail.width() == t.railW, buf);
        winTestLayout("back at the display's scale", w);
        t.camX = gState.camX, t.camY = gState.camY, t.zoom = gState.zoom;
#ifdef _WIN32
        ShowWindow(hwnd, SW_SHOWMINNOACTIVE);  // (SDL_MinimizeWindow's SW_MINIMIZE activates the next window: the user's)
#else
        SDL_MinimizeWindow(w);
#endif
        winTestNext();
        return;
    }
    case 5: {  // minimized: from the moment the system says so (macOS: after the animation into the Dock), one second
        const bool minimized = (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) != 0;
        if (!t.minSeen) {
            if (!minimized && SDL_GetTicks() - t.since < 5000) return;
            t.minSeen = true;
            const int step = t.step;
            winTestNext();  // (the counts start now)
            t.step = step;
            return;
        }
        if (!winTestWaited(1000, 0)) return;
        std::snprintf(buf, sizeof buf, "minimized: %s, %lld frames presented, %lld turns without a swapchain texture, %lld turns in 1 s",
                      minimized ? "yes" : "no", gHost->presentedFrames() - t.framesAt, gHost->missedFrames() - t.missedAt, t.pumps);
#ifdef _WIN32
        const long long framesWhileMinimizing = 1;  // (nothing is painted after the frame of that turn: SdlHost::pump)
#else
        const long long framesWhileMinimizing = 2;  // (X11: the window manager minimizes the window after a round trip)
#endif
        winTestLine(minimized && gHost->presentedFrames() - t.framesAt <= framesWhileMinimizing && t.pumps <= 40, buf);
#ifdef _WIN32
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
#else
        SDL_RestoreWindow(w);
#endif
        winTestNext();
        return;
    }
    case 6: {  // restored
        if (!winTestWaited(500, 5)) {
            if (SDL_GetTicks() - t.since > 5000) {
                std::snprintf(buf, sizeof buf, "restored: %lld frames in 5 s, minimized %s, %lld turns without a swapchain texture",
                              gHost->presentedFrames() - t.framesAt, (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) ? "yes" : "no",
                              gHost->missedFrames() - t.missedAt);
                winTestLine(false, buf);
                t.step = 98;  // (the next checks need frames: the end)
            }
            return;
        }
        if (winTestMapOpen()) {
            const int colors = winTestMapColors();
            std::snprintf(buf, sizeof buf, "restored: %lld frames presented, the map %s, camera %s, map texture %d colors", gHost->presentedFrames() - t.framesAt,
                          gState.ed && gState.ed->path().wstring() == t.mapPath ? "the same" : "another",
                          gState.camX == t.camX && gState.camY == t.camY && gState.zoom == t.zoom ? "the same" : "moved", colors);
            winTestLine(gState.ed && gState.ed->path().wstring() == t.mapPath && colors > 50 && gState.zoom == t.zoom, buf);
        } else {
            std::snprintf(buf, sizeof buf, "restored: %lld frames presented, minimized %s", gHost->presentedFrames() - t.framesAt,
                          (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) ? "yes" : "no");
            winTestLine(!(SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED), buf);
        }
        winTestLayout("restored", w);
        showAboutSdl();  // a dialog window
        winTestNext();
        return;
    }
    case 7: {  // a dialog: its title stays its own, its title bar is in the theme
        if (!winTestWaited(300, 3)) return;
        SDL_Window* about = gHost->sdlWindow(gAboutView);
        updateTitle();  // (as an edit of the map, a save or a tab change does while the dialog is open)
        const std::string title = winTestTitle(about);
        winTestLine(about && title == "About StarDraft 2", "dialog: About's title after the map's title changed: \"" + title + "\"");
        winTestLine(winTestTitle(w).find("StarDraft 2 - v") != std::string::npos, "dialog: the main window's title \"" + winTestTitle(w) + "\"");
#ifdef _WIN32
        winTestLine(about && winTestDarkAttr(about) == (gLightTheme ? 0 : 1), "dialog: title bar dark mode " + std::to_string(winTestDarkAttr(about)));
        winTestLine(GetForegroundWindow() != winsdl::windowHandle(about), "dialog: not in front of the user's window");
        // The light theme: the title bars go light; then dark; then the run's theme again (not saved).
        const bool wasLight = gLightTheme;
        applyThemeSdl(true, gAccentIndex, false);
        const int lightMain = winTestDarkAttr(w), lightAbout = winTestDarkAttr(about);
        applyThemeSdl(false, gAccentIndex, false);
        const int darkMain = winTestDarkAttr(w), darkAbout = winTestDarkAttr(about);
        applyThemeSdl(wasLight, gAccentIndex, false);
        std::snprintf(buf, sizeof buf, "theme: light gives the title bars %d (main) and %d (About), dark %d and %d", lightMain, lightAbout, darkMain, darkAbout);
        winTestLine(lightMain == 0 && lightAbout == 0 && darkMain == 1 && darkAbout == 1, buf);
#endif
        if (about) gHost->closeWindow(gAboutView);
#ifdef _WIN32
        if (gWinTestMap == "-") return winTestNext();  // (no second program)
        // A second program with a map: it gives the map to this one through the pipe and ends.
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" --noactivate \"" + widen(gWinTestMap) + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESHOWWINDOW, si.wShowWindow = SW_SHOWNOACTIVATE;
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &t.second)) {
            winTestLine(false, "pipe: the second program did not start");
            t.step = 99;
            return;
        }
#elif defined(STARDRAFT_XDG)
        // A second program with a map: it gives the map to this one through the socket and ends.
        const std::string self = narrow(stardraft::platform::programFile().wstring());
        const char* args[] = {self.c_str(), "--noactivate", gWinTestMap.c_str(), nullptr};
        t.second = SDL_CreateProcess(args, false);
        if (!t.second) {
            winTestLine(false, std::string("socket: the second program did not start: ") + SDL_GetError());
            t.step = 99;
            return;
        }
#endif
        winTestNext();
        return;
    }
    case 9: {  // a floating palette: it opens (the map's mode's palette floats)
        if (!gState.ed || !gState.ed->isOpen() || gWorkspace != Workspace::Map) return void(t.step = 11);  // (no map: no palette)
        shellFloatPalette(gState.mode, true);
        winTestNext();
        return;
    }
    case 10: {  // ... it stays where the user puts it outside the main window, and comes back onto a screen from off it
        SDL_Window* pw = nullptr;
        for (auto& p : gPanels)
            if (p->open && p->view->floating) pw = gHost->sdlWindow(*p->view);
        if (!pw || !winTestWaited(300, 3)) {
            if (SDL_GetTicks() - t.since < 10000) return;
            winTestLine(false, "palette: no floating palette window in 10 s");
            winTestNext();
            return;
        }
        int mx = 0, my = 0, mw = 0, mh = 0, ww = 0, wh = 0;
        SDL_GetWindowPosition(w, &mx, &my), SDL_GetWindowSize(w, &mw, &mh), SDL_GetWindowSize(pw, &ww, &wh);
        SDL_Rect area{};
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(w), &area);
        // Outside: right of the main window, else left of it, else below it (a window as big as the screen: none of them).
        int ox = mx + mw + 8, oy = my;
        if (ox + ww > area.x + area.w) ox = mx - ww - 8;
        if (ox < area.x) ox = mx, oy = my + mh + 8;
        const bool room = ox >= area.x && ox + ww <= area.x + area.w && oy >= area.y && oy + wh <= area.y + area.h;
        if (room) {
            SDL_SetWindowPosition(pw, ox, oy);
            SDL_SyncWindow(pw);
            sdlPanelKeepOnScreen(pw);
            int x = 0, y = 0;
            SDL_GetWindowPosition(pw, &x, &y);
            std::snprintf(buf, sizeof buf, "palette: put outside the main window at %d,%d (main %d,%d %dx%d): it is at %d,%d", ox, oy, mx, my, mw, mh, x, y);
            winTestLine(x == ox && y == oy, buf);
        } else {
            winTestLine(true, "palette: no room on the screen outside the main window (not checked)");
        }
        SDL_SetWindowPosition(pw, area.x + area.w + 400, area.y + area.h + 400);  // (off every screen to the lower right)
        SDL_SyncWindow(pw);
        sdlPanelKeepOnScreen(pw);
        SDL_SyncWindow(pw);
        int x = 0, y = 0;
        SDL_GetWindowPosition(pw, &x, &y);
        const SDL_Rect at{x, y, ww, wh};
        SDL_Rect in{};
        const bool onScreen = SDL_GetRectIntersection(&at, &area, &in) && in.w == ww && in.h == wh;
        std::snprintf(buf, sizeof buf, "palette: put off the screens: it came back to %d,%d %dx%d (the screen's area %d,%d %dx%d)", x, y, ww, wh, area.x, area.y, area.w, area.h);
        winTestLine(onScreen, buf);
        shellFloatPalette(gState.mode, false);
        winTestNext();
        return;
    }
    case 8: {  // the second program ended, and its map opened here
#ifdef _WIN32
        if (t.second.hProcess) {
            if (WaitForSingleObject(t.second.hProcess, 0) == WAIT_TIMEOUT) {
                if (SDL_GetTicks() - t.since < 20000) return;
                TerminateProcess(t.second.hProcess, 9);  // (the test's own second program)
                winTestLine(false, "pipe: the second program did not end in 20 s");
            } else {
                DWORD code = 99;
                GetExitCodeProcess(t.second.hProcess, &code);
                std::snprintf(buf, sizeof buf, "pipe: the second program ended with %lu after %llu ms", code, (unsigned long long)(SDL_GetTicks() - t.since));
                winTestLine(code == 0, buf);
            }
            CloseHandle(t.second.hProcess), CloseHandle(t.second.hThread);
            t.second = {};
        }
        std::error_code ec;
        const fs::path want = fs::absolute(widen(gWinTestMap), ec);
        const bool opened = gState.ed && gState.ed->isOpen() && fs::equivalent(gState.ed->path(), want, ec);
        if (!opened && SDL_GetTicks() - t.since < 25000) return;
        winTestLine(opened, "pipe: the open map is " + (gState.ed ? narrow(gState.ed->path().wstring()) : std::string("none")));
        const std::string title = winTestTitle(w);
        winTestLine(title.find(narrow(want.filename().wstring())) != std::string::npos, "pipe: title \"" + title + "\"");
        winTestLine(GetForegroundWindow() != hwnd, "pipe: the window did not come to the front (a test)");
#elif defined(STARDRAFT_XDG)
        if (t.second) {
            int code = 99;
            if (!SDL_WaitProcess(t.second, false, &code)) {
                if (SDL_GetTicks() - t.since < 20000) return;
                SDL_KillProcess(t.second, true);  // (the test's own second program)
                winTestLine(false, "socket: the second program did not end in 20 s");
            } else {
                std::snprintf(buf, sizeof buf, "socket: the second program ended with %d after %llu ms", code, (unsigned long long)(SDL_GetTicks() - t.since));
                winTestLine(code == 0, buf);
            }
            SDL_DestroyProcess(t.second);
            t.second = nullptr;
        }
        std::error_code ec;
        const fs::path want = fs::absolute(widen(gWinTestMap), ec);
        const bool opened = gState.ed && gState.ed->isOpen() && fs::equivalent(gState.ed->path(), want, ec);
        if (!opened && SDL_GetTicks() - t.since < 25000) return;
        winTestLine(opened, "socket: the open map is " + (gState.ed ? narrow(gState.ed->path().wstring()) : std::string("none")));
        const std::string title = winTestTitle(w);
        winTestLine(title.find(narrow(want.filename().wstring())) != std::string::npos, "socket: title \"" + title + "\"");
#endif
        winTestNext();
        return;
    }
    case 11: {  // --wintest-fullscreen: full screen on
        if (!gWinTestFull) return void(t.step = 99);
        SDL_GetWindowSizeInPixels(w, &t.fullW, &t.fullH);
        if (!SDL_SetWindowFullscreen(w, true)) winTestLine(false, std::string("full screen: SDL_SetWindowFullscreen: ") + SDL_GetError());
        winTestNext();
        return;
    }
    case 12:    // full screen: the window has the display's size, and frames go to it
    case 13: {  // full screen off: the window's size of before
        const bool on = t.step == 12;
        const bool flag = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(w, &pw, &ph);
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(w));
        // (off: a window in the display's free area again; SDL gives it the size it last asked for that the system
        // took, not always the size before)
        SDL_Rect area{};
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(w), &area);
        const float d = SDL_GetWindowPixelDensity(w) > 0 ? SDL_GetWindowPixelDensity(w) : 1.0f;
        const bool sized = on ? mode && pw == int(std::lround(mode->w * mode->pixel_density))
                              : pw > 0 && ph > 0 && pw <= int(float(area.w) * d) + 1 && ph <= int(float(area.h) * d) + 1;
        if ((flag != on || !sized || !winTestWaited(500, 5)) && SDL_GetTicks() - t.since < 10000) return;  // (macOS's animation: about a second)
        std::snprintf(buf, sizeof buf, "full screen %s: flag %s, the window %dx%d px (%s %dx%d; before full screen %dx%d), %lld frames presented, root %dx%d",
                      on ? "on" : "off", flag ? "yes" : "no", pw, ph, on ? "the display" : "the free area", on && mode ? int(std::lround(mode->w * mode->pixel_density)) : int(float(area.w) * d),
                      on && mode ? int(std::lround(mode->h * mode->pixel_density)) : int(float(area.h) * d), t.fullW, t.fullH, gHost->presentedFrames() - t.framesAt,
                      gRoot->width(), gRoot->height());
        winTestLine(flag == on && sized && gHost->presentedFrames() - t.framesAt >= 5 && gRoot->width() == pw && gRoot->height() == ph, buf);
        if (on) SDL_SetWindowFullscreen(w, false);
        winTestNext();
        return;
    }
    default:
        std::printf("wintest: %d checks, %d wrong\n", gWinTestChecks, gWinTestWrong);
        gHost->quit();
        return;
    }
}
