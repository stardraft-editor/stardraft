// --noactivate, --frames and --wintest: tests of the real window (not a snapshot), which never take the user's desktop.
// --noactivate: the windows do not take the keyboard focus and do not come to the front (SdlHost::setNoActivate,
// SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN), the run is a test run (no sound, the default settings, nothing saved, no file
// types), and the pipe that a second program sends its map on is a test pipe of its own (shell_sdl_win.inc: pipeName;
// Linux and the BSDs: a test socket of its own, sdl_xdg.inc: instanceSocket).
// --frames N: the program ends after N frames went to the window. --wintest <map>: the checks below, in the window,
// one after the other; each prints what it saw (a "(WRONG)" line when it is not right), then the program ends (exit
// code 1 when a check was wrong). Included by main_sdl.cpp before main().

long long gFramesArg = 0;      // --frames
std::string gWinTestMap;       // --wintest: the second program's map (the single-instance pipe's check)
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
    int mapW = 0, railW = 0;
    float camX = 0, camY = 0, zoom = 0;
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

void winTestLayout(const char* when, SDL_Window* w) {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    const bool fits = gRoot->width() == pw && gRoot->height() == ph;
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
        const std::string mapName = gState.ed ? narrow(gState.ed->path().filename().wstring()) : "";
        winTestLine(!mapName.empty() && title.find(mapName) != std::string::npos && title.find("StarDraft 2 - v") != std::string::npos,
                    "title \"" + title + "\"");
        const int colors = winTestMapColors();
        winTestLine(colors > 50, "map texture: " + std::to_string(colors) + " colors");
        t.mapPath = gState.ed ? gState.ed->path().wstring() : L"";
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
        winTestLine(gRoot->map.width() != t.mapW, "resize: the map view's width " + std::to_string(t.mapW) + " -> " + std::to_string(gRoot->map.width()));
        // The window moves to a display of 150 % (SDL's event, with the scale that the display gives).
        t.mapW = gRoot->map.width(), t.railW = gRoot->rail.width();
        std::printf("wintest: display scale %.2f, dpi %d, text %d px\n", double(SDL_GetWindowDisplayScale(w)), kit::theme().dpi, kit::px(15));
        kit_sdl::SdlHost::setTestDisplayScale(1.5f);
        winTestScaleEvent(w);
        winTestNext();
        return;
    }
    case 3: {  // at 150 %
        if (!winTestWaited(300, 5)) return;
        std::snprintf(buf, sizeof buf, "150 %%: dpi %d, text %d px, icons %d px from the %d px atlas, %lld frames presented", kit::theme().dpi, kit::px(15),
                      kit::px(16), kit_sdl::iconAtlasCell(0, kit::px(16)), gHost->presentedFrames() - t.framesAt);
        winTestLine(kit::theme().dpi == 144 && kit::px(15) >= 22, buf);  // (a 24 px icon is the 16 px one, sharp: iconDrawnPx)
        winTestLayout("150 %", w);
        std::snprintf(buf, sizeof buf, "150 %%: the rail %d -> %d px wide", t.railW, gRoot->rail.width());
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
    case 5: {  // minimized
        if (!winTestWaited(1000, 0)) return;
        const bool minimized = (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) != 0;
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
        const int colors = winTestMapColors();
        std::snprintf(buf, sizeof buf, "restored: %lld frames presented, the map %s, camera %s, map texture %d colors", gHost->presentedFrames() - t.framesAt,
                      gState.ed && gState.ed->path().wstring() == t.mapPath ? "the same" : "another",
                      gState.camX == t.camX && gState.camY == t.camY && gState.zoom == t.zoom ? "the same" : "moved", colors);
        winTestLine(gState.ed && gState.ed->path().wstring() == t.mapPath && colors > 50 && gState.zoom == t.zoom, buf);
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
    default:
        std::printf("wintest: %d checks, %d wrong\n", gWinTestChecks, gWinTestWrong);
        gHost->quit();
        return;
    }
}
