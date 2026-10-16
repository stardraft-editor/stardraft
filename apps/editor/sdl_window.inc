// The main window as a document window of the system (mac_window.h; included by main_sdl.cpp before sdl_options.inc).
// On macOS: the title bar shows the open map or project (the proxy icon) and the edited dot when it has changes, the
// window goes full screen and into Split View with the green button, the message boxes about the window are alert
// sheets on it, and the theme, the accent color and the rail's color follow the system (Program options > Automatic).
// On Linux and the BSDs the theme and the accent color follow the desktop (the desktop portal: desktop_xdg.h). On
// Windows the seam's functions do nothing.

SDL_Window* gMainWindow = nullptr;  // the program's main window, after host.show (none in a snapshot)

void applyThemeSdl(bool light, int accent, bool save);  // (sdl_options.inc)

// --- The window's document ------------------------------------------------------------------------------------------

// The file that the title bar shows for the active tab: the map's file, or the project's file (its .eds, else its
// project.stardraft, else its folder); "" for a page, an untitled map and an unsaved project.
std::string sdlWindowDocumentPath() {
    if (gActive < 0 || gActive >= int(gDocs.size()) || activePage() || !gState.ed || !gState.ed->isOpen()) return {};
    const Doc& d = gDocs[size_t(gActive)];
    std::error_code ec;
    if (d.project) {
        if (isUnsavedProject(d.project.get())) return {};
        for (const fs::path& p : {d.project->edsFile(), d.project->file(), d.project->folder()})
            if (!p.empty() && fs::exists(p, ec)) return narrow(p.wstring());
        return {};
    }
    return gState.ed->path().empty() ? std::string() : narrow(fs::absolute(gState.ed->path(), ec).wstring());
}
bool sdlWindowDocumentEdited() { return docOpen() && gState.ed->modified(); }

// Each frame: the title bar follows the active tab's file and its changes (only when they change; the file is looked
// for again only when the tab, its map's file or its project's file changes). Only on macOS.
void sdlWindowDocumentSync() {
#ifdef __APPLE__
    static std::string shownPath = "\x01";
    static std::wstring key = L"\x01";
    static bool shownEdited = false;
    if (!gMainWindow) return;
    std::wstring now = std::to_wstring(gActive) + L"|" + std::to_wstring(activePage());
    if (gState.ed) now += L"|" + gState.ed->path().wstring();
    if (gActive >= 0 && gActive < int(gDocs.size()) && gDocs[size_t(gActive)].project) now += L"|" + gDocs[size_t(gActive)].project->file().wstring();
    const std::string path = now == key ? shownPath : sdlWindowDocumentPath();
    key = std::move(now);
    const bool edited = sdlWindowDocumentEdited();
    if (path == shownPath && edited == shownEdited) return;
    shownPath = path, shownEdited = edited;
    macSetWindowDocument(gMainWindow, path, edited);
#endif
}

// --- Message boxes as sheets ----------------------------------------------------------------------------------------

// A button name without its "&" (the access key; "&&" is one "&").
std::wstring sdlSheetName(const std::wstring& name) {
    std::wstring out;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == L'&' && i + 1 < name.size()) ++i;
        out += name[i];
    }
    return out;
}

// The alert of a message box: the short text is the message (a long one goes under the caption), and the buttons in
// the system's order: the box's default button first (at the right: Return), then its cancel button (Escape), then the
// others from left to right (Cmd and the access key). order[k]: the index in box.buttons of the alert's button k.
MacAlert sdlSheetSpec(const std::wstring& caption, const msgbox::Box& box, std::vector<int>& order) {
    MacAlert a;
    std::wstring text = box.text;
    std::erase(text, L'\r');
    while (!text.empty() && text.back() == L'\n') text.pop_back();
    if (text.find(L'\n') == std::wstring::npos && text.size() <= 90) a.message = narrow(text);
    else a.message = narrow(caption), a.info = narrow(text);
    a.style = box.icon == msgbox::Icon::Error ? 2 : box.icon == msgbox::Icon::Warning ? 1 : 0;
    order.clear();
    const int n = int(box.buttons.size());
    if (n == 0) return a;
    order.push_back(std::clamp(box.focus, 0, n - 1));
    if (box.cancel != order[0] && box.cancel >= 0 && box.cancel < n) order.push_back(box.cancel);
    for (int i = 0; i < n; ++i)
        if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    for (size_t k = 0; k < order.size(); ++k) {
        const msgbox::Button& b = box.buttons[size_t(order[k])];
        a.buttons.push_back(narrow(sdlSheetName(b.name)));
        if (k == 0) a.keys.push_back("\r");
        else if (order[k] == box.cancel) a.keys.push_back("\x1b");
        else if (const wchar_t key = msgbox::accessKey(b.name); key > L' ' && key < 0x80) a.keys.push_back(std::string(1, char(key)));
        else a.keys.push_back("");
    }
    return a;
}

// The message box as a sheet on the main window (msgbox::hostBox): only for a box from a view of the main window.
// Waits for the answer as the box's own window does (the frames go on); quitting first answers with the cancel button.
// While it waits, the program is modal (SdlHost::modalOpen): no window takes input and no file opens from the system.
// A box while a dialog or a sheet is open is the kit's box.
int sdlSheetBox(draw::View& from, const std::wstring& caption, const msgbox::Box& box) {
    if (!gMainWindow || !gHost || !gRoot || gHost->modalOpen()) return -1;
    const draw::View* top = &from;
    while (top->parent()) top = top->parent();
    if (top != gRoot) return -1;  // (a box over a dialog or a pop-out: the kit's box, over that window)
    std::vector<int> order;
    const MacAlert spec = sdlSheetSpec(caption, box, order);
    auto answer = std::make_shared<int>(-2);
    if (!macBeginSheet(gMainWindow, spec, [answer](int k) { *answer = k; })) return -1;
    gHost->setSheetOpen(true);
    gHost->waitUntil([answer] { return *answer != -2; });
    gHost->setSheetOpen(false);
    const int k = *answer;
    return k >= 0 && k < int(order.size()) ? order[size_t(k)] : box.cancel;
}

// --- The system's look ----------------------------------------------------------------------------------------------

#if defined(__APPLE__) || defined(STARDRAFT_XDG)
constexpr bool kSystemLook = true;  // Program options has Automatic (the system's theme and accent color): the default
#else
constexpr bool kSystemLook = false;
#endif
bool gThemeAuto = false, gAccentAuto = false;  // the theme, the accent color: the system's (Automatic)

// The system's appearance and accent color: macOS's (mac_window.mm), or the desktop portal's on Linux and the BSDs
// (desktop_xdg.h: org.freedesktop.appearance color-scheme and accent-color; with no portal, SDL's system theme, else dark).
bool sdlSystemDark() {
#if defined(STARDRAFT_XDG)
    if (const int scheme = stardraft::xdg::portalColorScheme()) return scheme == 1;
    return SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
#else
    return macSystemDark();
#endif
}
bool sdlSystemAccentColor(uint8_t& r, uint8_t& g, uint8_t& b) {
#if defined(STARDRAFT_XDG)
    return stardraft::xdg::portalAccent(r, g, b);
#else
    return macSystemAccent(r, g, b);
#endif
}
void sdlWatchSystemLook() {
#if defined(STARDRAFT_XDG)
    stardraft::xdg::watchLook();
#else
    macWatchLook();
#endif
}
bool sdlSystemLookChanged() {
#if defined(STARDRAFT_XDG)
    // (the portal's signal; SDL's own theme too, for a desktop whose portal SDL reads and this program does not)
    static SDL_SystemTheme sdlTheme = SDL_GetSystemTheme();
    const SDL_SystemTheme now = SDL_GetSystemTheme();
    const bool sdlChanged = now != sdlTheme;
    sdlTheme = now;
    return stardraft::xdg::lookChanged() || sdlChanged;
#else
    return macLookChanged();
#endif
}

// The system's accent color as one of the program's (the nearest, when its hue is near one) or as its own hue.
void sdlSystemAccent(int& accent, double& hue) {
    accent = 0, hue = -1;
    uint8_t r = 0, g = 0, b = 0;
    if (!sdlSystemAccentColor(r, g, b)) return;
    const int found = theme::nearestAccent(RGB(r, g, b), 15, hue);
    if (found >= 0) accent = found, hue = -1;
    else {
        double ignored = 0;
        accent = std::max(0, theme::nearestAccent(RGB(r, g, b), 360, ignored));  // (its name)
    }
}
// The swatch of Automatic in Program options: the accent that the system's color gives, as the swatches show theirs.
COLORREF sdlSystemAccentSwatch() {
    int accent = 0;
    double hue = -1;
    sdlSystemAccent(accent, hue);
    if (hue < 0) return theme::swatch(accent);
    const theme::Oklch b = theme::oklch(theme::kDefaultBlue);
    return theme::color(b.L, b.C, hue);
}
// The colors of the theme `light` with `accent`, or the system's when Automatic is on; the rail in the system's sidebar
// color (macOS); the window's title bar and sheets in the theme.
void sdlSetLook(bool light, int accent) {
    double hue = -1;
    if (gThemeAuto) light = !sdlSystemDark();
    if (gAccentAuto) sdlSystemAccent(accent, hue);
    setThemeColors(light, accent, hue);
    uint8_t r = 0, g = 0, b = 0;
    if (macSidebarColor(!light, r, g, b)) kRail = RGB(r, g, b);
    macSetWindowDark(gMainWindow, !light);
}
// Each frame: a change of the system's appearance or colors applies when the program follows them.
void sdlFollowSystemLook() {
    if (!sdlSystemLookChanged()) return;
    applyThemeSdl(gLightTheme, gAccentIndex, false);
}

// --- The window ------------------------------------------------------------------------------------------------------

// The main window is shown (`testRun`: a test drives it; its message boxes stay the program's own, as a test reads them).
void sdlMainWindowShown(SDL_Window* w, bool testRun) {
    gMainWindow = w;
    macSetupWindow(w);
    macSetWindowDark(w, !gLightTheme);
    sdlWatchSystemLook();
#ifdef __APPLE__
    if (!testRun) msgbox::hostBox() = sdlSheetBox;  // (the sheets are macOS's)
#else
    (void)testRun;
#endif
    sdlWindowDocumentSync();
}
void sdlWindowFrame() {
    sdlWindowDocumentSync();
    sdlFollowSystemLook();
}
// --check window: what the window and the look are (after a snapshot: a hidden window of its own on macOS, the map of
// the run as its file, with changes), and the sheet of the save question (each system: the alert that macOS shows).
void sdlWindowCheck(const std::string& mapPath) {
#ifdef __APPLE__
    std::printf("window: system look %s\n", kSystemLook ? "yes" : "no");
#else
    std::printf("window: system look %s\n", kSystemLook ? "yes (the desktop's)" : "no");
#endif
    std::printf("window: theme %s%s, accent %ls%s\n", gLightTheme ? "light" : "dark", gThemeAuto ? " (the system's)" : "",
                theme::kAccents[gAccentIndex].name, gAccentAuto ? " (the system's)" : "");
    // The system's accent colors (macOS's light appearance) and the accent each one gives.
    const struct {
        const char* name;
        COLORREF c;
    } colors[] = {{"blue", RGB(0, 122, 255)},   {"purple", RGB(149, 61, 150)}, {"pink", RGB(247, 79, 158)},  {"red", RGB(224, 56, 62)},
                  {"orange", RGB(247, 130, 27)}, {"yellow", RGB(255, 199, 38)}, {"green", RGB(98, 186, 70)}, {"graphite", RGB(152, 152, 152)}};
    for (const auto& c : colors) {
        double hue = -1;
        const int k = theme::nearestAccent(c.c, 15, hue);
        if (k >= 0) std::printf("window: system accent %s -> %ls\n", c.name, theme::kAccents[k].name);
        else std::printf("window: system accent %s -> hue %.0f\n", c.name, hue);
    }
    msgbox::Box save = msgbox::makeBox(shellTr(L"Save changes to the map?"), {shellTr(L"&Save"), shellTr(L"Do &not save"), shellTr(L"Cancel")},
                                       msgbox::Icon::Question);
    std::vector<int> order;
    const MacAlert a = sdlSheetSpec(shellTr(L"Unsaved changes"), save, order);
    std::string line = "window: sheet \"" + a.message + "\":";
    for (size_t k = 0; k < a.buttons.size(); ++k) {
        const std::string& key = a.keys[k];
        line += " " + a.buttons[k] + " [" + (key == "\r" ? "Return" : key == "\x1b" ? "Escape" : key.empty() ? "none" : "Cmd+" + key) + "]";
    }
    std::printf("%s\n", line.c_str());
#ifdef __APPLE__
    uint8_t r = 0, g = 0, b = 0;
    std::printf("window: system appearance %s\n", macSystemDark() ? "dark" : "light");
    if (macSystemAccent(r, g, b)) std::printf("window: system accent color %d,%d,%d\n", r, g, b);
    else std::printf("window: system accent color none (WRONG)\n");
    if (macSidebarColor(!gLightTheme, r, g, b)) std::printf("window: rail color %d,%d,%d\n", r, g, b);
    else std::printf("window: rail color none (WRONG)\n");
    SDL_Window* w = SDL_CreateWindow("StarDraft 2", 800, 500, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!w) return void(std::printf("window: no window: %s (WRONG)\n", SDL_GetError()));
    std::error_code ec;
    const std::string file = mapPath.empty() ? std::string() : narrow(fs::absolute(widen(mapPath), ec).wstring());
    macSetupWindow(w);
    macSetWindowDark(w, !gLightTheme);
    macSetWindowDocument(w, file, true);
    std::printf("window: %s\n", macWindowReport(w).c_str());
    SDL_DestroyWindow(w);
#else
    (void)mapPath;
#if defined(STARDRAFT_XDG)
    std::printf("window: system appearance %s, desktop portal %s\n", sdlSystemDark() ? "dark" : "light",
                stardraft::xdg::portalColorScheme() ? "yes" : "no (SDL's system theme)");
#endif
    std::printf("window: %s\n", macWindowReport(nullptr).c_str());
#endif
}
