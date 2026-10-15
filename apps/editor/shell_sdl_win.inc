// The SDL program's Windows-only parts (stardraft-sdl.exe; the Windows program has them in crash.inc, update.inc and
// main.cpp): the crash handler and the crash report question, the map files (.scm, .scx) and project.stardraft in
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
    const int r = kit::messageBox(*gRoot, shellTr(L"Crash report"), releases::crashAskText(reports.front().parent_path()),
                                  {shellTr(L"Send"), shellTr(L"Always send"), shellTr(L"Do not send")}, kit::MessageIcon::Question);
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
// (another name) and another user's program never get this program's maps.
std::wstring pipeName() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::wstring stem = fs::path(self).stem().wstring();
    for (wchar_t& ch : stem) ch = wchar_t(std::towlower(ch));
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

// Whether this program is the one a release puts in place (stardraft.exe): only that one makes project.stardraft open
// with itself at start. stardraft-sdl.exe, beside the Windows program until it replaces it, leaves the Windows
// program's file types (Program options > Maps still makes maps open with it, when the user asks).
bool isReleaseProgram() {
    std::wstring a = stardraft::platform::programFile().filename().wstring(), b = releases::releaseProgramName();
    for (auto* s : {&a, &b})
        for (wchar_t& ch : *s) ch = wchar_t(std::towlower(ch));
    return a == b;
}

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
        kit::messageBox(*gRoot, shellTr(L"Default apps"),
                        shellTr(L"Windows keeps the program that you selected for .scm or .scx maps. In Default apps, search for .scm and .scx and select "
                                L"StarDraft 2."),
                        {L"OK"}, kit::MessageIcon::Info);
    SDL_OpenURL("ms-settings:defaultapps");
}

void askMapsDefault() {
    if (askedMapsDefault() || !gRoot) return;  // (two maps opened at once: one question)
    prefs::setNumber(L"AskedMapsDefault", 1);
    if (kit::messageBox(*gRoot, L"StarDraft 2",
                        shellTr(L"Open .scm and .scx maps with StarDraft 2 when you double-click them? You can make StarDraft 2 open them later in "
                                L"Program options > Maps."),
                        {shellTr(L"Yes"), shellTr(L"No")}, kit::MessageIcon::Question) == 0)
        makeMapsDefault();
}

// At start (not in a test run): the file types, as the Windows program does them at start.
void fileTypesAtStart() {
    if (!isReleaseProgram()) return;
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

}  // namespace winsdl

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
// --crash-test crashes on purpose (no message box).
int sdlWindowsFirst(int argc, char** argv, bool testRun) {
    const std::string a1 = argc > 1 ? argv[1] : "", a2 = argc > 2 ? argv[2] : "";
    if (a1 == "--send-crash") return releases::sendCrash(fs::path(widen(a2))) ? 0 : 1;
    prefs::number(L"CrashReports", gSdlCrashReports, 0, 2);
    winsdl::installCrashHandler(testRun || a1 == "--crash-test");
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

#else  // (the other systems: none of these parts)

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

#endif
