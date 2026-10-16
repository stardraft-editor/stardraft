// The SDL program's Windows-only parts (stardraft.exe): the files built into the program, the settings of the builds before the settings file (the
// registry), the crash handler and the crash report question, the map files (.scm, .scx) and project.stardraft in
// Explorer, one program for all maps (a map opened from Explorer goes to the program that is open), and the window's
// dark title bar. The icons are res/stardraft_sdl.rc's (SDL gives the first one to its windows). The texts and the
// sending are the core's (core/releases.inc), the registry and the crash's system part apps/common's (file_types.h,
// crash_report.h). On the other systems the same functions do nothing (the end of this file); the crash handler and
// the crash reports are on each system (macOS and Linux: crash_posix.cpp's signal handler).
// Included by main_sdl.cpp after sdl_updates.inc.

fs::path sdlCrashFolder();                 // the crash reports' folder (each system's)
void sdlInstallCrashHandler(bool quiet);   // the system's crash handler (a test run: the report stays on disk)
void sdlAskToSendCrashes();                // the question at start about an earlier crash's reports (the end of this file)

// The last log lines (the end of a crash report: each system's handler reads them). A report goes to a public issue:
// the user's folder in them is "~", and the account's name "<user>" (crash_report.h: withoutUserFolder).
std::deque<std::string> gSdlCrashLog;
void sdlRememberLogLine(const std::wstring& line) {
    static const std::string home = narrow(homeFolder().wstring());
    gSdlCrashLog.push_back(stardraft::platform::withoutUserFolder(narrow(line), home));
    if (gSdlCrashLog.size() > 60) gSdlCrashLog.pop_front();
}
std::string sdlCrashLogText() {
    std::string r;
    for (const auto& l : gSdlCrashLog) r += "  " + l + "\n";
    return r;
}

#ifdef _WIN32

namespace winsdl {

// --- Crash reports ----------------------------------------------------------------------------------------------------

std::wstring gGpu;              // the video card
bool gQuiet = false;            // test runs: no message box, the report stays on disk
std::string gBuild;             // "built Oct 10 12:00"

// %LOCALAPPDATA%\StarDraft\crashes (the Windows program's folder).
fs::path crashFolder() {
    const fs::path local = envPath("LOCALAPPDATA");
    return local.empty() ? fs::path() : local / "StarDraft" / "crashes";
}

// After the crash's report is written (in the crashed program: little is safe now): send it now (a new process sends
// it), or say where it is (the next start asks, or crash reports are off). Test runs keep it on disk only.
void crashWritten(const fs::path& dir) {
    if (gQuiet) {
        std::ofstream(dir / "sent") << "a test run: not sent";
        return;
    }
    if (gSdlCrashReports != 1) {
        if (gSdlCrashReports == 2) std::ofstream(dir / "sent") << "not sent: crash reports are off";
        MessageBoxW(nullptr, releases::crashStoppedText(dir, gSdlCrashReports).c_str(), L"StarDraft 2 stopped", MB_ICONERROR | MB_TOPMOST);
        return;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    stardraft::platform::claimCrashReport(dir);  // (the sender has it: the next start does not send it too)
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --send-crash \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    MessageBoxW(nullptr, releases::crashStoppedText(dir, 1).c_str(), L"StarDraft 2 stopped", MB_ICONERROR | MB_TOPMOST);
}

void installCrashHandler(bool quiet) {
    gQuiet = quiet;
    gBuild = narrow(shellBuildStamp());
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof dd;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i)
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) gGpu = dd.DeviceString;
    stardraft::platform::CrashHandler h;
    h.folder = crashFolder();
    h.header = [] { return releases::crashHeader(gBuild, gGpu); };
    h.context = [] { return "Workspace: " + narrow(reportWorkspaceName(gWorkspace)) + "\n"; };
    h.log = sdlCrashLogText;
    h.written = crashWritten;
    stardraft::platform::installCrashHandler(std::move(h));
}

// --- One program for all maps -----------------------------------------------------------------------------------------

// The pipe that the open program listens on: one for each program file name and Windows session, so a test build
// (another name) and another user's program never get this program's maps. The copies that StarDraft-latest starts
// (stardraft-latest-<n>.exe) share one name.
std::wstring pipeName() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::wstring stem = fs::path(self).stem().wstring();
    for (wchar_t& ch : stem) ch = wchar_t(std::towlower(ch));
    if (stem.rfind(L"stardraft-latest-", 0) == 0) stem = L"stardraft-latest";
    // (a test with real windows, --noactivate: a pipe of its own, so a test never sends a map to the user's program)
    if (gNoActivate) stem += L"-test";
    return L"\\\\.\\pipe\\StarDraft-" + stem + L"-" + std::to_wstring(session);
}

// The window comes to the front (restored when it is minimized). Not in a test with real windows (--noactivate).
void raiseMainWindow() {
    if (gNoActivate || !gHost) return;
    if (SDL_Window* w = gHost->mainWindow()) {  // (not SDL_GetWindows' first: that is the newest window, a menu or a tooltip)
        if (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(w);
        SDL_RaiseWindow(w);
    }
}

// A map given to this program goes to the program that is open already (it opens it and comes to the front), and this
// one ends; else this program is the first and listens (on a thread) for the files that later ones send, each opened
// as File > Open does. Programs that start at once take turns (apps/common/instance.h). True: the file went.
bool sendOrListen(const std::vector<std::wstring>& files) {
    std::string text;
    for (const std::wstring& f : files) {
        std::error_code ec;
        const fs::path abs = fs::absolute(f, ec);  // (the open program has another working folder)
        text += narrow((ec ? fs::path(f) : abs).wstring()) + "\n";
    }
    return stardraft::platform::sendOrListen(pipeName(), text, [](std::string got) {
               std::vector<fs::path> opened;
               for (size_t a = 0, b; a < got.size(); a = b + 1) {
                   b = got.find('\n', a);
                   if (b == std::string::npos) b = got.size();
                   if (b > a) opened.push_back(fs::path(widen(got.substr(a, b - a))));
               }
               shellPost([opened] {
                   raiseMainWindow();
                   for (const fs::path& f : opened) confirmReplace([f] { openAny(f); });
               });
           }) == stardraft::platform::InstanceStart::Sent;
}

// --- The map files ------------------------------------------------------------------------------------------------------

bool askedMapsDefault() {
    bool asked = false;
    prefs::flag(L"AskedMapsDefault", asked);
    return asked;
}

// Program options > Maps, and the question at start: maps open with StarDraft 2. Where Windows keeps another program
// that the user chose, its Default apps settings open.
void makeMapsDefault() {
    if (stardraft::platform::registerMapFiles(stardraft::platform::associationExe(), true)) {
        setMessage(L"Maps (.scm, .scx) open with StarDraft 2");
        gOptionsView.invalidate();
        return;
    }
    if (gRoot)
        msgbox::ask(*gRoot, shellTr(L"Default apps"),
                        shellTr(L"Windows keeps the program that you selected for .scm or .scx maps. In Default apps, search for .scm and .scx and select "
                                L"StarDraft 2."),
                        {L"OK"}, msgbox::Icon::Info);
    SDL_OpenURL("ms-settings:defaultapps");
}

void askMapsDefault() {
    if (askedMapsDefault() || !gRoot) return;  // (two maps opened at once: one question)
    prefs::setNumber(L"AskedMapsDefault", 1);
    if (msgbox::ask(*gRoot, L"StarDraft 2",
                        shellTr(L"Open .scm and .scx maps with StarDraft 2 when you double-click them? You can make StarDraft 2 open them later in "
                                L"Program options > Maps."),
                        {shellTr(L"&Yes"), shellTr(L"&No")}, msgbox::Icon::Question) == 0)
        makeMapsDefault();
}

// At start (not in a test run): the file types, as the Win32 program did them at start: project.stardraft opens with
// this program (not with a copy that StarDraft-latest started: the launcher does that), and it is in Explorer's Open
// with list for maps. A command that opens another copy of StarDraft 2 that is still there stays (a copy run once from
// Downloads or a USB stick does not take the maps from the installed one); the user's Yes points them here.
void fileTypesAtStart() {
    if (stardraft::platform::programFile().filename().wstring().rfind(L"stardraft-latest-", 0) != 0)
        stardraft::platform::registerProjectFiles(stardraft::platform::programFile().wstring(), true);
    stardraft::platform::registerMapFiles(stardraft::platform::associationExe(), false, true);  // in Explorer's Open with list for maps
}

// --- The window -----------------------------------------------------------------------------------------------------

HWND windowHandle(SDL_Window* w) {
    return w ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) : nullptr;
}

// A window's title bar in the program's theme: dark in the dark theme, light in the light theme (Windows 10 2004 and
// later; older ones keep theirs).
void titleBarDark(SDL_Window* w, bool dark) {
    const BOOL on = dark ? TRUE : FALSE;
    if (HWND hwnd = windowHandle(w)) DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof on);
}
// The title bars of all the program's windows (the main window and its dialogs).
void titleBarsDark(bool dark) {
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    for (int i = 0; wins && i < n; ++i) titleBarDark(wins[i], dark);
    SDL_free(wins);
}

// --- The files built into the program -----------------------------------------------------------------------------

// The fonts, the icon atlases and the app icon are the program's RCDATA resources (the build's
// generated/<target>_files.rc), named by their path under the program's folder ("fonts/NotoSans-Regular.ttf"), so a
// release is stardraft.exe only (the update puts one file in place) and a copy of it (StarDraft-latest) runs anywhere.
// A file beside the program comes first (kit_sdl reads it).
std::pair<const void*, size_t> embeddedFile(const std::string& path) {
    static const std::string base = [] {
        const char* b = SDL_GetBasePath();
        return std::string(b ? b : "");
    }();
    if (base.empty() || path.size() <= base.size() || path.compare(0, base.size(), base) != 0) return {nullptr, 0};
    std::wstring name = widen(path.substr(base.size()));
    std::replace(name.begin(), name.end(), L'\\', L'/');
    // (rc keeps a resource's name in upper case, and a quoted name with its quotes)
    for (wchar_t& ch : name) ch = wchar_t(std::towupper(ch));
    HRSRC r = FindResourceW(nullptr, (L"\"" + name + L"\"").c_str(), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!r) r = FindResourceW(nullptr, name.c_str(), MAKEINTRESOURCEW(10));
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    const void* data = g ? LockResource(g) : nullptr;
    return data ? std::pair<const void*, size_t>{data, size_t(SizeofResource(nullptr, r))} : std::pair<const void*, size_t>{nullptr, 0};
}

}  // namespace winsdl

// The files built into the program (kit_sdl::setEmbeddedFiles).
stardraft::kit_sdl::EmbeddedFile sdlEmbeddedFiles() { return winsdl::embeddedFile; }

// The UI text's character height at 96 DPI (kit_sdl::setUiFontHeight): the system's message font, as the Win32 program
// used it (Segoe UI 9 pt: 12 px). 0 when the system does not say.
int sdlUiFontHeight() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, 96)) return 0;
    const int h = ncm.lfMessageFont.lfHeight;
    return h < 0 ? -h : 0;  // (a positive height is a cell height, not the characters': not used)
}

// The settings of the Windows builds before the settings file: HKCU\Software\StarDraft (core/prefs.inc reads a value
// there that the file does not have yet), the same as the Win32 program's (shell_win32.inc), so an update from an old
// build keeps them. A test that gives the settings file (shellSettingsFileGiven) reads none, or the registry file that
// --legacyhive gives (gLegacyHiveArg: its own Software\StarDraft; made when it is not there), so a test of an update
// from those builds never reads or changes the user's.
static HKEY legacyRoot() {
    static const HKEY root = [] {
        if (!gLegacyHiveArg.empty()) {
            HKEY k = nullptr;
            return RegLoadAppKeyW(gLegacyHiveArg.c_str(), &k, KEY_ALL_ACCESS, 0, 0) == ERROR_SUCCESS ? k : HKEY(nullptr);
        }
        return shellSettingsFileGiven() ? HKEY(nullptr) : HKEY_CURRENT_USER;
    }();
    return root;
}
static std::wstring legacyKeyPath(const std::wstring& sub) {
    std::wstring key = L"Software\\StarDraft";
    if (!sub.empty()) {
        std::wstring path = sub;
        std::replace(path.begin(), path.end(), L'/', L'\\');
        key += L"\\" + path;
    }
    return key;
}
bool shellLegacyValue(const std::wstring& sub, const std::wstring& value, LegacyKind kind, std::vector<uint8_t>& out) {
    const DWORD flags = kind == LegacyKind::Dword   ? RRF_RT_REG_DWORD
                        : kind == LegacyKind::Text  ? RRF_RT_REG_SZ
                        : kind == LegacyKind::Texts ? RRF_RT_REG_MULTI_SZ
                                                    : RRF_RT_REG_BINARY;
    const std::wstring key = legacyKeyPath(sub);
    const HKEY root = legacyRoot();
    DWORD size = 0;
    if (!root || RegGetValueW(root, key.c_str(), value.c_str(), flags, nullptr, nullptr, &size) != ERROR_SUCCESS) return false;
    out.assign(size, 0);
    if (RegGetValueW(root, key.c_str(), value.c_str(), flags, nullptr, out.data(), &size) != ERROR_SUCCESS) return false;
    out.resize(size);
    return true;
}
void shellLegacyNames(const std::wstring& sub, std::vector<std::wstring>& out) {
    HKEY k;
    if (!legacyRoot() || RegOpenKeyExW(legacyRoot(), legacyKeyPath(sub).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD len = 256;
        if (RegEnumValueW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
    }
    RegCloseKey(k);
}
void shellLegacyRemove(const std::wstring& sub, const std::wstring& value) {
    if (legacyRoot()) RegDeleteKeyValueW(legacyRoot(), legacyKeyPath(sub).c_str(), value.c_str());
}

bool sdlMakeMapsDefault() {
    if (!gSilent) winsdl::makeMapsDefault();  // (tests leave the user's settings)
    return true;
}
// A .scm or .scx file opened by itself (not a new map, not a project): the first time, the question follows.
void shellMapFileOpened(const std::filesystem::path& file) {
    std::wstring ext = file.extension().wstring();
    for (wchar_t& ch : ext) ch = wchar_t(std::towlower(ch));
    if (gSilent || (ext != L".scm" && ext != L".scx") || winsdl::askedMapsDefault() || stardraft::platform::mapsOpenWithStarDraft()) return;
    shellPost(winsdl::askMapsDefault);
}
bool sdlMapsOpenWithUs() { return stardraft::platform::mapsOpenWithStarDraft(); }

void sdlInstallCrashHandler(bool quiet) { winsdl::installCrashHandler(quiet); }
// A map or project given to a second program goes to the first one: true when it went (this program then ends). A test
// run (a snapshot, --silent) never sends and never listens.
bool sdlSendToOpenProgram(const std::string& file, bool testRun) {
    if (testRun) return false;
    return winsdl::sendOrListen(file.empty() ? std::vector<std::wstring>{} : std::vector<std::wstring>{widen(file)});
}
// The title bars follow the theme: the windows open now, and each dialog window before it shows (sdlSetLook calls it
// again when the theme changes).
void sdlTitleBarsDark(bool dark) { winsdl::titleBarsDark(dark); }
void sdlTitleBarOfNewWindow(SDL_Window* w) { winsdl::titleBarDark(w, !gLightTheme); }
// After the window shows (not in a test run): the title bar in the theme, the reports of a crash, the file types.
void sdlWindowsShown(bool testRun) {
    winsdl::titleBarsDark(!gLightTheme);
    if (testRun) return;
    releases::sendPendingCrashes(winsdl::crashFolder(), gSdlCrashReports);
    shellPost(sdlAskToSendCrashes);
    winsdl::fileTypesAtStart();
}
// The Windows program's StarCraft folder file (%APPDATA%\stardraft\install.txt: its first line), so that both programs
// use the same install.
fs::path sdlWindowsInstallFile() {
    const fs::path a = envPath("APPDATA");
    return a.empty() ? fs::path() : a / "stardraft" / "install.txt";
}

// A program started with ShellExecuteEx: `verb` "runas" asks for administrator rights, null starts it as this program.
std::uintptr_t sdlShellStart(const wchar_t* verb, const std::filesystem::path& exe, const std::filesystem::path& dir, const std::wstring& args,
                             const std::wstring& what, std::wstring& error) {
    SHELLEXECUTEINFOW si{};
    si.cbSize = sizeof si;
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.lpVerb = verb;
    si.lpFile = exe.c_str();
    si.lpParameters = args.c_str();
    si.lpDirectory = dir.c_str();
    si.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&si) && si.hProcess) return std::uintptr_t(si.hProcess);
    const DWORD e = GetLastError();
    error = e == ERROR_CANCELLED ? what + L" did not start: it needs administrator rights" : L"Could not start " + what + L" (error " + std::to_wstring(e) + L")";
    return 0;
}
// A program with administrator rights (epTrace: its manifest asks for them): Windows' elevation prompt.
std::uintptr_t shellStartElevated(const std::filesystem::path& exe, const std::filesystem::path& dir, const std::wstring& args, const std::wstring& what,
                                  std::wstring& error) {
    return sdlShellStart(L"runas", exe, dir, args, what, error);
}
// A program with this program's rights (Restart now).
std::uintptr_t sdlStartProgram(const fs::path& exe, const fs::path& dir, const std::wstring& args, const std::wstring& what, std::wstring& error) {
    return sdlShellStart(nullptr, exe, dir, args, what, error);
}
bool shellProcessEnded(std::uintptr_t process) { return WaitForSingleObject(HANDLE(process), 0) == WAIT_OBJECT_0; }
void shellProcessClose(std::uintptr_t process) { CloseHandle(HANDLE(process)); }
// (core/documents.inc: the unsaved projects' folders; the restart)
std::uint32_t shellProcessId() { return std::uint32_t(GetCurrentProcessId()); }
bool shellProcessRunning(std::uint32_t pid) {
    HANDLE h = pid ? OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid)) : nullptr;
    if (!h) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

fs::path sdlCrashFolder() { return winsdl::crashFolder(); }

// --check filetypes (sdl_updates.inc): what opens the maps and project.stardraft in Explorer (the user's file types:
// read only).
std::string sdlFileTypesCheck() {
    auto classes = [](const std::wstring& key) {
        wchar_t v[1024] = L"";
        DWORD size = sizeof v;
        return RegGetValueW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + key).c_str(), nullptr, RRF_RT_REG_SZ, nullptr, v, &size) == ERROR_SUCCESS ? narrow(v)
                                                                                                                                                 : std::string("(none)");
    };
    std::string out = "filetypes: this program " + narrow(stardraft::platform::programFile().wstring()) + "\n";
    out += "filetypes: project.stardraft " + classes(L".stardraft") + ", opens with " + classes(L"StarDraft.Project\\shell\\open\\command") + "\n";
    out += "filetypes: maps " + classes(L".scm") + " " + classes(L".scx") + ", opens with " + classes(L"StarDraft.Map\\shell\\open\\command") +
           ", maps open with StarDraft 2: " + (stardraft::platform::mapsOpenWithStarDraft() ? "yes" : "no") + "\n";
    return out;
}

#else  // (the other systems: none of these parts)

stardraft::kit_sdl::EmbeddedFile sdlEmbeddedFiles() { return nullptr; }  // (the files are beside the program, or in the app bundle)
int sdlUiFontHeight() { return 0; }  // (the kit's own size)

fs::path sdlWindowsInstallFile() { return {}; }
// (shellStartElevated, shellProcessEnded and shellProcessClose: shell_sdl.inc, SDL's processes; they ask for no rights)
std::uintptr_t sdlStartProgram(const fs::path& exe, const fs::path& dir, const std::wstring& args, const std::wstring& what, std::wstring& error) {
    return shellStartElevated(exe, dir, args, what, error);
}

bool sdlMakeMapsDefault() { return false; }
void shellMapFileOpened(const std::filesystem::path&) {}  // (the file association is Windows only)
bool sdlMapsOpenWithUs() { return false; }
#if !defined(STARDRAFT_XDG)  // (Linux and the BSDs: sdl_xdg.inc has one program for all maps)
bool sdlSendToOpenProgram(const std::string&, bool) { return false; }
#endif
void sdlTitleBarsDark(bool) {}
#if defined(STARDRAFT_XDG)
// Each window gets the program's icon (X11: the task bar and Alt+Tab show it also when no .desktop file matches the
// window, as an AppImage that is not installed).
void sdlTitleBarOfNewWindow(SDL_Window* w) { kit_sdl::setWindowIcon(w, 1); }
#else
void sdlTitleBarOfNewWindow(SDL_Window*) {}
#endif
// (core/documents.inc: the unsaved projects' folders; the restart)
std::uint32_t shellProcessId() { return std::uint32_t(getpid()); }
bool shellProcessRunning(std::uint32_t pid) { return pid && (kill(pid_t(pid), 0) == 0 || errno == EPERM); }
std::string sdlFileTypesCheck() { return "filetypes: none on this system\n"; }

// --- Crash reports (macOS, Linux and the BSDs: crash_posix.cpp's signal handler) -----------------------------------

namespace posixsdl {

bool gQuiet = false;     // test runs: the report stays on disk, no message box
std::string gBuild;      // "built Oct 10 12:00"
std::string gGpu;        // the video card (when the window shows)
std::string gSelf;       // the program's file (found before a crash: the sender starts it)

// After the crash's report is written (in the crashed program: little is safe now): send it now (a new process sends
// it), or say where it is (the next start asks, or crash reports are off). Test runs keep it on disk only. The text
// goes to the error output; on Linux and the BSDs also to a message box (macOS: a window is not safe now).
void crashWritten(const fs::path& dir) {
    if (gQuiet) {
        std::ofstream(dir / "sent") << "a test run: not sent";
        std::fprintf(stderr, "crash: report %s (a test run: not sent)\n", dir.string().c_str());
        return;
    }
    if (gSdlCrashReports == 2) std::ofstream(dir / "sent") << "not sent: crash reports are off";
    if (gSdlCrashReports == 1 && !gSelf.empty()) {
        const std::string dirText = dir.string();
        stardraft::platform::claimCrashReport(dir);  // (the sender has it: the next start does not send it too)
        if (fork() == 0) {  // (the sender: this program with --send-crash)
            execl(gSelf.c_str(), gSelf.c_str(), "--send-crash", dirText.c_str(), static_cast<char*>(nullptr));
            _exit(1);
        }
    }
    const std::string text = narrow(releases::crashStoppedText(dir, gSdlCrashReports));
    std::fprintf(stderr, "%s\n", text.c_str());
#if !defined(__APPLE__)
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "StarDraft 2 stopped", text.c_str(), nullptr);
#endif
}

void installCrashHandler(bool quiet) {
    gQuiet = quiet;
    gBuild = narrow(shellBuildStamp());
    gSelf = stardraft::platform::programFile().string();
    stardraft::platform::CrashHandler h;
    h.folder = sdlCrashFolder();
    h.header = [] { return releases::crashHeader(gBuild, widen(!gGpu.empty() ? gGpu : gRenderer ? gRenderer->driver() : "no GPU yet")); };
    h.context = [] { return "Workspace: " + narrow(reportWorkspaceName(gWorkspace)) + "\n"; };
    h.log = sdlCrashLogText;
    h.written = crashWritten;
    stardraft::platform::installCrashHandler(std::move(h));
}

}  // namespace posixsdl

// The crash reports' folder: on Linux and the BSDs $XDG_STATE_HOME/StarDraft/crashes (~/.local/state); on macOS
// StarDraft/crashes in the user's data folder (~/Library/Application Support), beside the projects that are not saved
// yet.
fs::path sdlCrashFolder() {
#if defined(STARDRAFT_XDG)
    const fs::path state = stardraft::xdg::stateHome();
    return state.empty() ? fs::path() : state / "StarDraft" / "crashes";
#else
    const fs::path unsaved = shellUnsavedRoot();
    return unsaved.empty() ? fs::path() : unsaved.parent_path() / "crashes";
#endif
}

void sdlInstallCrashHandler(bool quiet) { posixsdl::installCrashHandler(quiet); }

// After the window shows: the video card for a crash's report; not in a test run: the reports of an earlier crash
// (sent, or the question).
void sdlWindowsShown(bool testRun) {
    if (gDevice) {
        const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(gDevice), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
        posixsdl::gGpu = name && *name ? name : gRenderer ? gRenderer->driver() : "?";
    }
    if (testRun) return;
    releases::sendPendingCrashes(sdlCrashFolder(), gSdlCrashReports);
    shellPost(sdlAskToSendCrashes);
}

#endif

// --- Crash reports (each system) --------------------------------------------------------------------------------------

// main()'s first steps: a crashed program's sender (--send-crash <folder>) sends its report and ends (returns its exit
// code; -1 otherwise), and the crash handler goes in (a test run's reports stay on disk); --crash-test crashes on
// purpose (no message box); --crash-test stack: with a stack overflow (a call that calls itself).
int sdlCrashTestDepth(volatile int n) {
    volatile char frame[4096];
    frame[0] = char(n);
    return n > 0 ? sdlCrashTestDepth(n + 1) + frame[0] : 0;  // (n never ends at 0: the stack runs out)
}
int sdlWindowsFirst(int argc, char** argv, bool testRun) {
    const std::string a1 = argc > 1 ? argv[1] : "", a2 = argc > 2 ? argv[2] : "";
    if (a1 == "--send-crash") return releases::sendCrash(fs::path(widen(a2)), true) ? 0 : 1;  // (the crashed program claimed it)
    prefs::number(L"CrashReports", gSdlCrashReports, 0, 2);
    sdlInstallCrashHandler(testRun || a1 == "--crash-test");
    if (a1 == "--crash-test" && a2 == "stack") return sdlCrashTestDepth(1);
    if (a1 == "--crash-test") *static_cast<volatile int*>(nullptr) = 1;
    return -1;
}

// At start, with Options > Crash reports: Ask: the reports of the last crash: send them, always send, or not.
void sdlAskToSendCrashes() {
    if (gSdlCrashReports != 0 || issueToken().empty() || !gRoot) return;
    const auto reports = stardraft::platform::unsentCrashReports(sdlCrashFolder());
    if (reports.empty()) return;
    const int r = msgbox::ask(*gRoot, shellTr(L"Crash report"), releases::crashAskText(reports.front().parent_path()),
                                  {shellTr(L"&Send"), shellTr(L"&Always send"), shellTr(L"Do &not send")}, msgbox::Icon::Question);
    if (r == 1) gSdlCrashReports = 1, prefs::setNumber(L"CrashReports", 1);  // Always send: from now on, without the question
    if (r == 0 || r == 1) {
        std::thread([reports] {
            for (const auto& d : reports) releases::sendCrash(d);
        }).detach();
    } else {
        for (const auto& d : reports) stardraft::platform::markCrashReport(d, "not sent: the user said no");
    }
}

// --check crashes (sdl_updates.inc): the crash reports in the folder (each one's state) and what the next start does
// with the ones that were not sent (Options > Crash reports; a build made here has no token, and asks and sends nothing).
std::string sdlCrashCheck() {
    int mode = 0;
    prefs::number(L"CrashReports", mode, 0, 2);
    const fs::path folder = sdlCrashFolder();
    std::string out = "crashes: folder " + narrow(folder.wstring()) + ", crash reports " + std::to_string(mode) + "\n";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        std::string state = "not sent";
        if (std::ifstream in(e.path() / "sent"); in) std::getline(in, state);
        const auto size = fs::exists(e.path() / "report.txt", ec) ? fs::file_size(e.path() / "report.txt", ec) : 0;
        out += "crashes: report " + e.path().filename().string() + ", report.txt " + std::to_string(size) + " bytes, crash.dmp " +
               (fs::exists(e.path() / "crash.dmp", ec) ? "yes" : "no") + ", " + state + "\n";
    }
    const auto unsent = stardraft::platform::unsentCrashReports(folder);
    const std::string token = issueToken().empty() ? " (a build made here: nothing, it has no token)" : "";
    if (unsent.empty()) out += "crashes: the next start: nothing (no report waits)\n";
    else if (mode == 0) out += "crashes: the next start asks" + token + ": " + narrow(releases::crashAskText(unsent.front().parent_path())) + "\n";
    else if (mode == 1) out += "crashes: the next start sends " + std::to_string(unsent.size()) + token + "\n";
    else out += "crashes: the next start: nothing (crash reports are off)\n";
    return out;
}
