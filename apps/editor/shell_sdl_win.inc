// The SDL program's Windows-only parts (stardraft.exe): the files built into the program, the settings of the builds before the settings file (the
// registry), the crash handler and the crash report question, the map files (.scm, .scx) and project.stardraft in
// Explorer, one program for all maps (a map opened from Explorer goes to the program that is open), and the window's
// dark title bar. The icons are res/stardraft_sdl.rc's (SDL gives the first one to its windows). The texts and the
// sending are the core's (core/releases.inc), the registry and the crash's system part apps/common's (file_types.h,
// crash_report.h). On the other systems the same functions do nothing (the end of this file).
// Included by main_sdl.cpp after sdl_updates.inc.

#ifdef _WIN32

namespace winsdl {

// --- Crash reports ----------------------------------------------------------------------------------------------------

std::deque<std::wstring> gLog;  // the last log lines (the report's end)
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
    h.log = [] {
        std::string r;
        for (const auto& l : gLog) r += "  " + narrow(l) + "\n";
        return r;
    };
    h.written = crashWritten;
    stardraft::platform::installCrashHandler(std::move(h));
}

// At start, with Options > Crash reports: Ask: the reports of the last crash: send them, always send, or not.
void askToSendCrashes() {
    if (gSdlCrashReports != 0 || issueToken().empty() || !gRoot) return;
    const auto reports = stardraft::platform::unsentCrashReports(crashFolder());
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
    return L"\\\\.\\pipe\\StarDraft-" + stem + L"-" + std::to_wstring(session);
}

// A program is open already: the files go to it (it opens them and comes to the front), and this one ends. False when
// no program listens (this one is the first).
bool sendToOpenProgram(const std::vector<std::wstring>& files) {
    const std::wstring name = pipeName();
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 3 && pipe == INVALID_HANDLE_VALUE; ++tries) {
        pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE && (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name.c_str(), 2000))) break;
    }
    if (pipe == INVALID_HANDLE_VALUE) return false;
    AllowSetForegroundWindow(ASFW_ANY);  // (the open program's window may come to the front)
    std::string text;
    for (const std::wstring& f : files) {
        std::error_code ec;
        const fs::path abs = fs::absolute(f, ec);  // (the open program has another working folder)
        text += narrow((ec ? fs::path(f) : abs).wstring()) + "\n";
    }
    DWORD wrote = 0;
    const bool ok = WriteFile(pipe, text.data(), DWORD(text.size()), &wrote, nullptr) && wrote == text.size();
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return ok;
}

// The window comes to the front (restored when it is minimized).
void raiseMainWindow() {
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    if (wins && n > 0) {
        SDL_Window* w = wins[0];
        if (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(w);
        SDL_RaiseWindow(w);
    }
    SDL_free(wins);
}

// The first program listens (on a thread) for the files that a second one sends; each opens as File > Open does.
void listenForOtherPrograms() {
    const std::wstring name = pipeName();
    std::thread([name] {
        bool first = true;
        for (;;) {
            HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 64 * 1024, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) return;  // (another program listens already)
            first = false;
            if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
                std::string text;
                char buf[4096];
                DWORD got = 0;
                while (ReadFile(pipe, buf, sizeof buf, &got, nullptr) && got) text.append(buf, got);
                std::vector<fs::path> files;
                for (size_t a = 0, b; a < text.size(); a = b + 1) {
                    b = text.find('\n', a);
                    if (b == std::string::npos) b = text.size();
                    if (b > a) files.push_back(fs::path(widen(text.substr(a, b - a))));
                }
                shellPost([files] {
                    raiseMainWindow();
                    for (const fs::path& f : files) confirmReplace([f] { openAny(f); });
                });
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }).detach();
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
// with list for maps.
void fileTypesAtStart() {
    if (stardraft::platform::programFile().filename().wstring().rfind(L"stardraft-latest-", 0) != 0)
        stardraft::platform::registerProjectFiles(stardraft::platform::programFile().wstring());
    stardraft::platform::registerMapFiles(stardraft::platform::associationExe(), false);  // in Explorer's Open with list for maps
}

// --- The window -----------------------------------------------------------------------------------------------------

// The title bar is dark, as the program is (Windows 10 2004 and later; older ones keep theirs).
void darkTitleBar() {
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    for (int i = 0; wins && i < n; ++i) {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(wins[i]), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        const BOOL dark = TRUE;
        if (hwnd) DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    }
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
// build keeps them.
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
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(), flags, nullptr, nullptr, &size) != ERROR_SUCCESS) return false;
    out.assign(size, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(), flags, nullptr, out.data(), &size) != ERROR_SUCCESS) return false;
    out.resize(size);
    return true;
}
void shellLegacyNames(const std::wstring& sub, std::vector<std::wstring>& out) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, legacyKeyPath(sub).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD len = 256;
        if (RegEnumValueW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
    }
    RegCloseKey(k);
}
void shellLegacyRemove(const std::wstring& sub, const std::wstring& value) { RegDeleteKeyValueW(HKEY_CURRENT_USER, legacyKeyPath(sub).c_str(), value.c_str()); }

// The log's lines go into the crash report's end too.
void sdlRememberLogLine(const std::wstring& line) {
    winsdl::gLog.push_back(line);
    if (winsdl::gLog.size() > 60) winsdl::gLog.pop_front();
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

// main()'s Windows steps. Before anything else: a crashed program's sender (--send-crash <folder>) sends its report and
// ends (returns its exit code; -1 otherwise), and the crash handler goes in (a test run's reports stay on disk);
// --crash-test crashes on purpose (no message box); --crash-test stack: with a stack overflow (a call that calls itself).
int sdlCrashTestDepth(volatile int n) {
    volatile char frame[4096];
    frame[0] = char(n);
    return n > 0 ? sdlCrashTestDepth(n + 1) + frame[0] : 0;  // (n never ends at 0: the stack runs out)
}
int sdlWindowsFirst(int argc, char** argv, bool testRun) {
    const std::string a1 = argc > 1 ? argv[1] : "", a2 = argc > 2 ? argv[2] : "";
    if (a1 == "--send-crash") return releases::sendCrash(fs::path(widen(a2))) ? 0 : 1;
    prefs::number(L"CrashReports", gSdlCrashReports, 0, 2);
    winsdl::installCrashHandler(testRun || a1 == "--crash-test");
    if (a1 == "--crash-test" && a2 == "stack") return sdlCrashTestDepth(1);
    if (a1 == "--crash-test") *static_cast<volatile int*>(nullptr) = 1;
    return -1;
}
// A map or project given to a second program goes to the first one: true when it went (this program then ends). A test
// run (a snapshot, --silent) never sends and never listens.
bool sdlSendToOpenProgram(const std::string& file, bool testRun) {
    if (testRun) return false;
    if (!file.empty() && winsdl::sendToOpenProgram({widen(file)})) return true;
    winsdl::listenForOtherPrograms();
    return false;
}
// After the window shows (not in a test run): the dark title bar, the reports of a crash, the file types.
void sdlWindowsShown(bool testRun) {
    winsdl::darkTitleBar();
    if (testRun) return;
    releases::sendPendingCrashes(winsdl::crashFolder(), gSdlCrashReports);
    shellPost(winsdl::askToSendCrashes);
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

#else  // (the other systems: none of these parts)

stardraft::kit_sdl::EmbeddedFile sdlEmbeddedFiles() { return nullptr; }  // (the files are beside the program, or in the app bundle)
int sdlUiFontHeight() { return 0; }  // (the kit's own size)

fs::path sdlWindowsInstallFile() { return {}; }
// (shellStartElevated, shellProcessEnded and shellProcessClose: shell_sdl.inc, SDL's processes; they ask for no rights)
std::uintptr_t sdlStartProgram(const fs::path& exe, const fs::path& dir, const std::wstring& args, const std::wstring& what, std::wstring& error) {
    return shellStartElevated(exe, dir, args, what, error);
}

void sdlRememberLogLine(const std::wstring&) {}
bool sdlMakeMapsDefault() { return false; }
void shellMapFileOpened(const std::filesystem::path&) {}  // (the file association is Windows only)
bool sdlMapsOpenWithUs() { return false; }
int sdlWindowsFirst(int, char**, bool) { return -1; }
bool sdlSendToOpenProgram(const std::string&, bool) { return false; }
void sdlWindowsShown(bool) {}
// (core/documents.inc: the unsaved projects' folders; the restart)
std::uint32_t shellProcessId() { return std::uint32_t(getpid()); }
bool shellProcessRunning(std::uint32_t pid) { return pid && (kill(pid_t(pid), 0) == 0 || errno == EPERM); }

#endif
