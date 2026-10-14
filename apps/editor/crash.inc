// Crash reports (included into main.cpp near the top; the system part and the sending are apps/common/crash_report.h).
// When StarDraft crashes, it writes what it can into
// %LOCALAPPDATA%\StarDraft\crashes\<time>\: a minidump (crash.dmp) and report.txt (the version and commit, Windows,
// the video card, the exception and where it happened, the stack as module+offset, the workspace, the last log
// lines). The report goes to GitHub at once, with no user action: the crashed program starts a copy of itself
// (--send-crash <folder>) that sends it as an issue (label "crash"; an issue already there for the same place gets a
// comment). Reports that could not go (no network) go at the next start. Sending needs a token that may open issues
// on the repository, built in by CI (secret STARDRAFT_ISSUES_TOKEN); a build without one keeps the reports on disk.

#include "build_info.h"  // STARDRAFT_COMMIT, written on each build (cmake/build_info.cmake)
#ifndef STARDRAFT_VERSION_NAME
#define STARDRAFT_VERSION_NAME "dev"
#endif
#ifndef STARDRAFT_COMMIT
#define STARDRAFT_COMMIT "unknown"
#endif
#ifndef STARDRAFT_ISSUES_TOKEN
#define STARDRAFT_ISSUES_TOKEN ""
#endif

namespace crash {

constexpr const wchar_t* kRepo = L"stardraft-editor/stardraft";
std::wstring gGpu;                        // the video card, found at start
std::deque<std::wstring> gLog;            // the last log lines
const wchar_t* (*gWhere)() = nullptr;     // what the user works in (the workspace), filled in by main.cpp
bool gQuiet = false;                      // test runs: no message box

fs::path folder() {
    wchar_t* local = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) p = fs::path(local) / "StarDraft" / "crashes";
    CoTaskMemFree(local);
    return p;
}

void remember(const std::wstring& line) {
    gLog.push_back(line);
    if (gLog.size() > 60) gLog.pop_front();
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string windowsVersion() { return stardraft::platform::systemName(); }

std::string gBuild;  // "built Oct 10 12:00" (from main.cpp)

// After the crash's report is written (apps/common/crash_win32.cpp): send it now (a new process sends it: this one is
// not safe to use for much now), ask at the next start, or keep it. Test runs (gQuiet) keep it on disk only.
void written(const fs::path& dir) {
    if (gQuiet) {
        std::ofstream(dir / "sent") << "a test run: not sent";
        return;
    }
    if (gCrashReports != 1) {  // Ask (the next start asks) or Do not send: the report stays on disk
        if (gCrashReports == 2) std::ofstream(dir / "sent") << "not sent: crash reports are off";
        MessageBoxW(nullptr,
                    (L"StarDraft 2 stopped because of an error. The crash report is in " + dir.wstring() +
                     (gCrashReports == 2 ? L" (crash reports are off)." : L". At the next start, StarDraft 2 asks if you want to send the crash report."))
                        .c_str(),
                    L"StarDraft 2 stopped", MB_ICONERROR | MB_TOPMOST);
        return;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --send-crash \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    MessageBoxW(nullptr, L"StarDraft 2 stopped because of an error. StarDraft 2 sends a crash report to the developers so that they can repair the error.", L"StarDraft 2 stopped",
                MB_ICONERROR | MB_TOPMOST);
}

void install() {
    stardraft::platform::CrashHandler h;
    h.folder = folder();
    h.header = [] {
        return "StarDraft " STARDRAFT_VERSION_NAME ", " + gBuild + ", commit " STARDRAFT_COMMIT "\n" + windowsVersion() + ", video card: " + utf8(gGpu) + "\n";
    };
    h.context = [] { return gWhere ? "Workspace: " + utf8(gWhere()) + "\n" : std::string(); };
    h.log = [] {
        std::string r;
        for (const auto& l : gLog) r += "  " + utf8(l) + "\n";
        return r;
    };
    h.written = written;
    stardraft::platform::installCrashHandler(std::move(h));
}

// --- Sending --------------------------------------------------------------------------------------------------------

// A token that may only open issues on the repository.
// A build made here has none; the environment variable STARDRAFT_ISSUES_TOKEN gives one to test with.
std::string token() {
    static const std::string t = [] {
        std::string v = STARDRAFT_ISSUES_TOKEN;
        if (v.empty())
            if (const char* e = std::getenv("STARDRAFT_ISSUES_TOKEN")) v = e;
        return v;
    }();
    return t;
}

// A JSON string (feedback.inc uses it too).
std::string json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\', o += char(ch);
        else if (ch == '\n') o += "\\n";
        else if (ch == '\r') continue;
        else if (ch == '\t') o += "\\t";
        else if (ch < 0x20) continue;
        else o += char(ch);
    }
    return o + "\"";
}

// Sends one report to GitHub; true when it arrived (apps/common/crash_report.cpp).
bool send(const fs::path& dir) { return stardraft::platform::sendCrashReport(dir, utf8(kRepo), token()); }

// At start, on a thread: reports that did not go yet (no network then). Reports older than 30 days go away.
void sendPending() {
    if (token().empty() || gCrashReports != 1) return;
    std::thread([] {
        std::error_code ec;
        const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
        for (const auto& e : fs::directory_iterator(folder(), ec)) {
            if (fs::last_write_time(e.path(), ec) < old) fs::remove_all(e.path(), ec);
            else if (fs::exists(e.path() / "report.txt", ec) && !fs::exists(e.path() / "sent", ec)) send(e.path());
        }
    }).detach();
}

// The reports on this computer that did not go and were not refused.
std::vector<fs::path> unsentReports() { return stardraft::platform::unsentCrashReports(folder()); }

// At start, with Options > Crash reports: Ask: the reports of the last crash: send them, always send, or not.
void askToSend() {
    if (gCrashReports != 0 || token().empty()) return;
    const auto reports = unsentReports();
    if (reports.empty()) return;
    const std::wstring q = L"StarDraft 2 stopped because of an error the last time. Send the crash report to the developers, so that they can "
                           L"repair the error?\n\nThe report has the version, Windows, the video card, where it crashed and the last log lines. "
                           L"It has no maps. It is in " + reports.front().parent_path().wstring() + L".";
    const int r = themedMessageBox(app.main, q.c_str(), L"Crash report", MB_YESNOCANCEL | MB_ICONQUESTION, {L"&Send", L"&Always send", L"Do &not send"});
    if (r == IDNO) setCrashReports(1);  // Always send: from now on, without the question
    if (r == IDYES || r == IDNO) {
        std::thread([reports] {
            for (const auto& d : reports) send(d);
        }).detach();
    } else {
        for (const auto& d : reports) stardraft::platform::markCrashReport(d, "not sent: the user said no");
    }
}

}  // namespace crash
