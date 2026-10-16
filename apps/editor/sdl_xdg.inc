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

// --- Crash reports (apps/common/crash_posix.cpp; the question and --check crashes: sdl_updates.inc) ---

std::deque<std::wstring> gLog;  // the last log lines (the report's end)
std::string gGpu = "?";         // the video card (when the window shows)
std::string gBuild;             // "built Oct 10 12:00"
bool gQuiet = false;            // test runs: the report stays on disk, no message box
fs::path gSelf;                 // the program's file (found before a crash: the sender starts it)

// $XDG_STATE_HOME/StarDraft/crashes (~/.local/state).
fs::path crashFolder() {
    const fs::path s = stardraft::xdg::stateHome();
    return s.empty() ? fs::path() : s / "StarDraft" / "crashes";
}

// After the crash's report is written (in the crashed program: little is safe now): send it now (a new process sends
// it), or say where it is (the next start asks, or crash reports are off). Test runs keep it on disk only.
void crashWritten(const fs::path& dir) {
    if (gQuiet) {
        std::ofstream(dir / "sent") << "a test run: not sent";
        std::fprintf(stderr, "crash: the report is in %s (a test run: not sent)\n", dir.c_str());
        return;
    }
    if (gSdlCrashReports == 2) std::ofstream(dir / "sent") << "not sent: crash reports are off";
    if (gSdlCrashReports == 1 && !gSelf.empty()) {
        const std::string dirText = dir.string();
        if (fork() == 0) {  // (the sender: this program with --send-crash)
            execl(gSelf.c_str(), gSelf.c_str(), "--send-crash", dirText.c_str(), static_cast<char*>(nullptr));
            _exit(1);
        }
    }
    const std::string text = narrow(releases::crashStoppedText(dir, gSdlCrashReports));
    std::fprintf(stderr, "%s\n", text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "StarDraft 2 stopped", text.c_str(), nullptr);
}

void installCrashHandler(bool quiet) {
    gQuiet = quiet;
    gBuild = narrow(shellBuildStamp());
    gSelf = stardraft::platform::programFile();
    stardraft::platform::CrashHandler h;
    h.folder = crashFolder();
    h.header = [] { return releases::crashHeader(gBuild, widen(gGpu)); };
    h.context = [] { return "Workspace: " + narrow(reportWorkspaceName(gWorkspace)) + "\n"; };
    h.log = [] {
        std::string r;
        for (const auto& l : gLog) r += "  " + narrow(l) + "\n";
        return r;
    };
    h.written = crashWritten;
    stardraft::platform::installCrashHandler(std::move(h));
}

// --crash-test stack: a call that calls itself until the stack runs out.
int crashTestDepth(int n) {
    volatile char frame[4096];
    frame[0] = char(n);
    return n > 0 ? crashTestDepth(n + 1) + frame[0] : 0;
}

// --- One program for all maps: a map or project that a second program gets (the file manager's Open) goes to the
// first one, through a socket (desktop_xdg.h: instanceSocket) ---

// The socket's file: one for each program file name (a test build has another name), and a test socket of its own for
// a test with real windows (--noactivate), so a test never sends a map to the user's program.
fs::path instanceSocket() {
    std::string stem = stardraft::platform::programFile().stem().string();
    for (char& ch : stem) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (stem.empty()) stem = "stardraft";
    if (gNoActivate) stem += "-test";
    return stardraft::xdg::instanceSocket("StarDraft-" + stem + ".sock");
}

// The window comes to the front (restored when it is minimized). Not in a test with real windows (--noactivate).
void raiseMainWindow() {
    if (gNoActivate || !gMainWindow) return;
    if (SDL_GetWindowFlags(gMainWindow) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(gMainWindow);
    SDL_RaiseWindow(gMainWindow);
}

// The first program: the files that a second one sends open as File > Open does (one line each).
void listenForOtherPrograms(const fs::path& socket) {
    stardraft::xdg::listenAsInstance(socket, [](std::string text) {
        std::vector<fs::path> files;
        for (size_t a = 0, b; a < text.size(); a = b + 1) {
            b = text.find('\n', a);
            if (b == std::string::npos) b = text.size();
            if (b > a) files.push_back(fs::path(widen(text.substr(a, b - a))));
        }
        if (files.empty()) return;
        shellPost([files] {
            raiseMainWindow();
            for (const fs::path& f : files) confirmReplace([f] { openAny(f); });
        });
    });
}

}  // namespace xdgsdl

// main()'s first steps (the Windows program's are shell_sdl_win.inc's): a crashed program's sender (--send-crash
// <folder>) sends its report and ends (returns its exit code; -1 otherwise), and the crash handler goes in (a test
// run's reports stay on disk); --crash-test crashes on purpose; --crash-test stack: with a stack overflow.
int sdlWindowsFirst(int argc, char** argv, bool testRun) {
    const std::string a1 = argc > 1 ? argv[1] : "", a2 = argc > 2 ? argv[2] : "";
    if (a1 == "--send-crash") return releases::sendCrash(fs::path(widen(a2))) ? 0 : 1;
    prefs::number(L"CrashReports", gSdlCrashReports, 0, 2);
    xdgsdl::installCrashHandler(testRun || a1 == "--crash-test");
    if (a1 == "--crash-test" && a2 == "stack") return xdgsdl::crashTestDepth(1);
    if (a1 == "--crash-test") *static_cast<volatile int*>(nullptr) = 1;
    return -1;
}
void sdlRememberLogLine(const std::wstring& line) {
    xdgsdl::gLog.push_back(line);
    if (xdgsdl::gLog.size() > 60) xdgsdl::gLog.pop_front();
}
// A map or project given to a second program goes to the first one: true when it went (this program then ends). A test
// run (a snapshot, --silent) never sends and never listens.
bool sdlSendToOpenProgram(const std::string& file, bool testRun) {
    if (testRun) return false;
    const fs::path socket = xdgsdl::instanceSocket();
    if (socket.empty()) return false;
    if (!file.empty()) {
        std::error_code ec;
        const fs::path abs = fs::absolute(widen(file), ec);  // (the open program has another working folder)
        if (stardraft::xdg::sendToInstance(socket, narrow((ec ? fs::path(widen(file)) : abs).wstring()) + "\n")) return true;
    }
    xdgsdl::listenForOtherPrograms(socket);
    return false;
}
// After the window shows: the video card for a crash's report; not in a test run: the reports of an earlier crash.
void sdlWindowsShown(bool testRun) {
    if (gDevice) {
        const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(gDevice), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
        xdgsdl::gGpu = name && *name ? name : gRenderer ? gRenderer->driver() : "?";
    }
    if (testRun) return;
    releases::sendPendingCrashes(xdgsdl::crashFolder(), gSdlCrashReports);
    shellPost([] { sdlAskToSendCrashes(xdgsdl::crashFolder()); });
}
std::string sdlCrashCheck() { return sdlCrashReportsCheck(xdgsdl::crashFolder()); }
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
