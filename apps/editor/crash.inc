// Crash reports (included into main.cpp near the top). When StarDraft crashes, it writes what it can into
// %LOCALAPPDATA%\StarDraft\crashes\<time>\: a minidump (crash.dmp) and report.txt (the version and commit, Windows,
// the video card, the exception and where it happened, the stack as module+offset, the workspace, the last log
// lines). The report goes to GitHub at once, with no user action: the crashed program starts a copy of itself
// (--send-crash <folder>) that sends it as an issue (label "crash"; an issue already there for the same place gets a
// comment). Reports that could not go (no network) go at the next start. Sending needs a token that may open issues
// on the repository, built in by CI (secret STARDRAFT_ISSUES_TOKEN); a build without one keeps the reports on disk.

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

const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "division by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case 0xE06D7363: return "C++ exception";
    case 0xE5D0000A: return "abort";
    case 0xE5D0000B: return "C++ exception not caught (terminate)";
    case 0xE5D0000C: return "pure virtual call";
    case 0xE5D0000D: return "invalid parameter to the C library";
    default: return "exception";
    }
}

// "stardraft.exe+0x1A2B3C" for an address.
std::string where(DWORD64 address) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &m) || !m) {
        char b[32];
        std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(address));
        return b;
    }
    wchar_t name[MAX_PATH];
    GetModuleFileNameW(m, name, MAX_PATH);
    char b[64];
    std::snprintf(b, sizeof b, "+0x%llX", static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(m)));
    return utf8(fs::path(name).filename().wstring()) + b;
}

std::string windowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{sizeof v};
    if (auto f = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) f(&v);
    return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." + std::to_string(v.dwBuildNumber);
}

std::string gBuild;  // "built Oct 10 12:00" (from main.cpp)

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    static LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::error_code ec;
    const fs::path dir = folder() / stamp;
    fs::create_directories(dir, ec);
    // The minidump: threads, stacks and the memory they point at.
    HANDLE f = CreateFileW((dir / "crash.dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mei,
                          nullptr, nullptr);
        CloseHandle(f);
    }
    // The report.
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const DWORD64 at = reinterpret_cast<DWORD64>(ep->ExceptionRecord->ExceptionAddress);
    std::string r;
    char line[256];
    r += "StarDraft " STARDRAFT_VERSION_NAME ", " + gBuild + ", commit " STARDRAFT_COMMIT "\n";
    r += windowsVersion() + ", video card: " + utf8(gGpu) + "\n";
    std::snprintf(line, sizeof line, "Exception 0x%08lX (%s) at %s", code, exceptionName(code), where(at).c_str());
    r += line;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0], addr = ep->ExceptionRecord->ExceptionInformation[1];
        std::snprintf(line, sizeof line, ", %s 0x%llX", kind == 0 ? "reading" : kind == 1 ? "writing" : "running", static_cast<unsigned long long>(addr));
        r += line;
    }
    r += "\n";
    if (gWhere) r += "Workspace: " + utf8(gWhere()) + "\n";
    // The stack: walked from the crash's own context.
    r += "\nStack:\n";
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip, sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp, sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp, sf.AddrStack.Mode = AddrModeFlat;
    HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool syms = SymInitialize(proc, nullptr, TRUE);
    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !sf.AddrPC.Offset) break;
        r += "  " + where(sf.AddrPC.Offset);
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
        auto* s = reinterpret_cast<SYMBOL_INFO*>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFO), s->MaxNameLen = 255;
        DWORD64 off = 0;
        if (syms && SymFromAddr(proc, sf.AddrPC.Offset, &off, s)) r += std::string("  ") + s->Name;
        r += "\n";
    }
    r += "\nLog (last lines):\n";
    for (const auto& l : gLog) r += "  " + utf8(l) + "\n";
    std::ofstream(dir / "report.txt", std::ios::binary) << r;
    // A new process sends it (this one is not safe to use for much now). Test runs (gQuiet) keep it on disk only.
    if (gQuiet) {
        std::ofstream(dir / "sent") << "a test run: not sent";
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (gCrashReports != 1) {  // Ask (the next start asks) or Do not send: the report stays on disk
        if (gCrashReports == 2) std::ofstream(dir / "sent") << "not sent: crash reports are off";
        MessageBoxW(nullptr,
                    (L"StarDraft stopped because of an error. The crash report is in " + dir.wstring() +
                     (gCrashReports == 2 ? L" (crash reports are off)." : L". At the next start, StarDraft asks if it can send it."))
                        .c_str(),
                    L"StarDraft stopped", MB_ICONERROR | MB_TOPMOST);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --send-crash \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    if (!gQuiet)
        MessageBoxW(nullptr, L"StarDraft stopped because of an error. StarDraft sends a crash report to the developers so that they can repair the error.", L"StarDraft stopped",
                    MB_ICONERROR | MB_TOPMOST);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Errors the C library and C++ report in their own ways: made into exceptions, so onCrash writes them down too.
void raise(DWORD code) { RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr); }
void install() {
    SetUnhandledExceptionFilter(onCrash);
    std::set_terminate([] { raise(0xE5D0000B); });
    _set_purecall_handler([] { raise(0xE5D0000C); });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { raise(0xE5D0000D); });
    signal(SIGABRT, [](int) { raise(0xE5D0000A); });
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
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

// A request to GitHub's API; the answer's body, or "" on failure (`status` the HTTP status).
std::string github(const wchar_t* method, const std::wstring& path, const std::string& body, DWORD& status) {
    status = 0;
    std::string out;
    HINTERNET s = WinHttpOpen(L"StarDraft", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return out;
    HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET q = c ? WinHttpOpenRequest(c, method, path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (q) {
        const std::wstring headers = L"Authorization: Bearer " + widen(token()) + L"\r\nAccept: application/vnd.github+json\r\nContent-Type: application/json\r\n";
        if (WinHttpSendRequest(q, headers.c_str(), DWORD(-1), LPVOID(body.data()), DWORD(body.size()), DWORD(body.size()), 0) && WinHttpReceiveResponse(q, nullptr)) {
            DWORD size = sizeof status;
            WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            for (DWORD n = 0; WinHttpQueryDataAvailable(q, &n) && n;) {
                std::string chunk(n, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(q, chunk.data(), n, &read)) break;
                out.append(chunk.data(), read);
            }
        }
        WinHttpCloseHandle(q);
    }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return out;
}
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
std::wstring urlEncode(const std::string& s) {
    std::wstring o;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += wchar_t(ch);
        else {
            wchar_t b[4];
            std::swprintf(b, 4, L"%%%02X", ch);
            o += b;
        }
    }
    return o;
}

// The issue's title: the place it happened, so the same crash finds its issue again.
std::string title(const std::string& report) {
    const size_t a = report.find("Exception ");
    std::string e = a == std::string::npos ? "a crash" : report.substr(a, report.find('\n', a) - a);
    if (const size_t r = e.find(", reading"); r != std::string::npos) e.resize(r);  // the address it read differs each time
    if (const size_t r = e.find(", writing"); r != std::string::npos) e.resize(r);
    return "Crash: " + e;
}

// Sends one report to GitHub; true when it arrived. The folder is then marked "sent".
bool send(const fs::path& dir) {
    std::error_code ec;
    if (token().empty() || fs::exists(dir / "sent", ec)) return false;
    std::ifstream in(dir / "report.txt", std::ios::binary);
    std::string report((std::istreambuf_iterator<char>(in)), {});
    if (report.empty()) return false;
    if (report.size() > 60000) report.resize(60000);
    const std::string t = title(report), body = "```\n" + report + "```\n";
    // The same crash: a comment on its open issue; a new one: a new issue.
    DWORD status = 0;
    const std::string found = github(L"GET", L"/search/issues?q=" + urlEncode("repo:" + utf8(kRepo) + " is:issue is:open in:title \"" + t + "\""), "", status);
    const size_t num = found.find("\"number\":");
    if (status == 200 && num != std::string::npos && found.find("\"total_count\":0") == std::string::npos) {
        const std::string n = std::to_string(std::atoi(found.c_str() + num + 9));
        github(L"POST", L"/repos/" + std::wstring(kRepo) + L"/issues/" + widen(n) + L"/comments", "{\"body\":" + json(body) + "}", status);
    } else {
        github(L"POST", L"/repos/" + std::wstring(kRepo) + L"/issues", "{\"title\":" + json(t) + ",\"body\":" + json(body) + ",\"labels\":[\"crash\"]}", status);
    }
    if (status != 201) return false;
    std::ofstream(dir / "sent") << "sent";
    return true;
}

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
std::vector<fs::path> unsentReports() {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder(), ec))
        if (fs::exists(e.path() / "report.txt", ec) && !fs::exists(e.path() / "sent", ec)) out.push_back(e.path());
    return out;
}

// At start, with Options > Crash reports: Ask: the reports of the last crash: send them, always send, or not.
void askToSend() {
    if (gCrashReports != 0 || token().empty()) return;
    const auto reports = unsentReports();
    if (reports.empty()) return;
    const std::wstring q = L"StarDraft stopped because of an error the last time. Send the crash report to the developers, so that they can "
                           L"repair the error?\n\nThe report has the version, Windows, the video card, where it crashed and the last log lines. "
                           L"It has no maps. It is in " + reports.front().parent_path().wstring() + L".";
    const int r = themedMessageBox(app.main, q.c_str(), L"Crash report", MB_YESNOCANCEL | MB_ICONQUESTION, {L"&Send", L"&Always send", L"Do &not send"});
    if (r == IDNO) setCrashReports(1);  // Always send: from now on, without the question
    if (r == IDYES || r == IDNO) {
        std::thread([reports] {
            for (const auto& d : reports) send(d);
        }).detach();
    } else {
        for (const auto& d : reports) std::ofstream(d / "sent") << "not sent: the user said no";
    }
}

}  // namespace crash
