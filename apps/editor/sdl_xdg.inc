// The free desktops (Linux and the BSDs: desktop_xdg.h), included by main_sdl.cpp after sdl_dock.inc: the maps and
// projects that open go to the desktop's recently used files, a Build that ends while the window does not have the focus
// gives a desktop notification, Build's progress shows on the dock's or the task bar's icon (SDL: the launcher API), and
// --check desktop:<folder>. The theme and the accent color are sdl_window.inc's; the paths are shell_sdl.inc's. On
// Windows and macOS the functions do nothing (and --check desktop says so).

// The program's packaged form: an AppImage or a Flatpak updates as its package does (not with Help > Check for updates).
bool sdlUpdatesWithPackage() {
#if defined(STARDRAFT_XDG)
    return !stardraft::xdg::appImageFile().empty() || stardraft::xdg::inFlatpak();
#else
    return false;
#endif
}

#if defined(STARDRAFT_XDG)
namespace xdgsdl {

// The newest recent map and project when the program last looked: a new one at the top of a list goes to the desktop's
// recently used files (core/recent.inc keeps the lists; it is the same for every system).
std::wstring gSeenMap, gSeenProject;
bool gSeenSet = false;

void addToDesktopRecent(const std::wstring& item) {
    const fs::path p(item);
    std::string err;
    if (!stardraft::xdg::addRecent(stardraft::xdg::recentFile(), p, stardraft::xdg::mimeTypeOf(p), err))
        std::fprintf(stderr, "recently used files: %s\n", err.c_str());
}

void watchRecent() {
    const std::wstring map = gRecentMaps.empty() ? std::wstring() : gRecentMaps.front();
    const std::wstring project = gRecentProjects.empty() ? std::wstring() : gRecentProjects.front();
    if (!gSeenSet) {  // (the lists as the last run left them: nothing new)
        gSeenMap = map, gSeenProject = project, gSeenSet = true;
        return;
    }
    if (gSilent || gSettingsOff || gSelfTest) return;  // (a test run leaves the desktop as it is)
    if (map != gSeenMap && !map.empty()) addToDesktopRecent(map);
    if (project != gSeenProject && !project.empty()) addToDesktopRecent(project);
    gSeenMap = map, gSeenProject = project;
}

// Build started or ended (core/project_build.inc: gBuilding): the icon's progress, and a notification at the end when
// the window does not have the focus (the user works in another window).
void watchBuild() {
    static bool was = false;
    if (gBuilding == was) return;
    was = gBuilding;
    if (gMainWindow) SDL_SetWindowProgressState(gMainWindow, gBuilding ? SDL_PROGRESS_STATE_INDETERMINATE : SDL_PROGRESS_STATE_NONE);
    if (gBuilding || gSilent || !gMainWindow) return;
    if (SDL_GetWindowFlags(gMainWindow) & SDL_WINDOW_INPUT_FOCUS) return;
    std::wstring text;
    if (gBuildResult == 1) {
        const EudProject* pr = activeProject();
        text = shellTr(L"Built ") + (pr ? pr->outputMap().filename().wstring() : std::wstring());
    } else {
        text = shellTr(L"Could not build the map (see the log)");
    }
    stardraft::xdg::notify(narrow(text), "");
}

}  // namespace xdgsdl
#endif

// Each turn of the event loop (main_sdl.cpp: the host's before-pump hook).
void sdlXdgFrame() {
#if defined(STARDRAFT_XDG)
    xdgsdl::watchRecent();
    xdgsdl::watchBuild();
#endif
}

// --check desktop:<folder> (a snapshot): the program's id, its folders, what the desktop has (the session bus, the
// portal, the notification server) and the recently used files' entry of the open map (or of a sample file), written
// to <folder>/recently-used.xbel (two times: one entry with the count 2), not to the user's file. Another name: false.
bool sdlXdgCheck(const std::string& what, std::string& out) {
    if (what != "desktop" && what.rfind("desktop:", 0) != 0) return false;
#if defined(STARDRAFT_XDG)
    namespace x = stardraft::xdg;
    out += std::string("desktop: app id ") + x::kAppId + "; SDL app id " + (SDL_GetHint(SDL_HINT_APP_ID) ? SDL_GetHint(SDL_HINT_APP_ID) : "none (WRONG)") + "\n";
    out += "desktop: settings " + narrow(shellSettingsFile().wstring()) + "\n";
    out += "desktop: data " + narrow(shellUnsavedRoot().parent_path().wstring()) + "\n";
    out += "desktop: feedback " + narrow(shellFeedbackFolder().wstring()) + "\n";
    out += "desktop: tile colors " + narrow(stardraft::preview::tileColorsFolder().wstring()) + "\n";
    out += "desktop: package " + std::string(!x::appImageFile().empty() ? "AppImage" : x::inFlatpak() ? "Flatpak" : "none") + "\n";
    out += x::report();
    const std::string dir = what.size() > 8 ? what.substr(8) : std::string();
    if (dir.empty()) return out += "desktop: no folder for the recently used files (desktop:<folder>)\n", true;
    const fs::path xbel = fs::path(widen(dir)) / "recently-used.xbel";
    const fs::path file = gState.ed && !gState.ed->path().empty() ? gState.ed->path() : fs::path(widen(dir)) / "Sample map.scx";
    std::string err;
    const bool ok = x::addRecent(xbel, file, x::mimeTypeOf(file), err) && x::addRecent(xbel, file, x::mimeTypeOf(file), err);
    std::string xml;
    if (std::ifstream in(xbel, std::ios::binary); in) xml.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    size_t entries = 0;
    for (size_t at = 0; (at = xml.find("<bookmark href=", at)) != std::string::npos; ++at) ++entries;
    const bool twice = xml.find("name=\"StarDraft 2\"") != std::string::npos && xml.find("count=\"2\"") != std::string::npos;
    out += "desktop: recently used " + std::string(ok ? "written" : "not written (WRONG): " + err) + ", " + std::to_string(entries) +
           " entry, type " + x::mimeTypeOf(file) + (entries == 1 && twice ? " (yes)" : " (WRONG)") + "\n";
#else
    out += "desktop: not on this system (Linux and the BSDs only)\n";
#endif
    return true;
}
