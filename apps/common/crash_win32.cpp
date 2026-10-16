// The crash handler on Windows: an unhandled exception (and the C and C++ libraries' errors, made into exceptions)
// writes crash.dmp (a minidump) and report.txt, then calls the program's `written`.

#include "crash_report.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <exception>
#include <fstream>

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

CrashHandler gHandler;

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

// The crash's report, written by its own thread (crashWork): `thread` is the crashed one (its id and a handle to it).
// `wrote` is set once report.txt is written, before `written` (which can wait for the user).
void writeCrash(EXCEPTION_POINTERS* ep, DWORD threadId, HANDLE thread, HANDLE wrote = nullptr) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::error_code ec;
    const fs::path dir = gHandler.folder / stamp;
    fs::create_directories(dir, ec);
    // The minidump: threads, stacks and the memory they point at.
    HANDLE f = CreateFileW((dir / "crash.dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{threadId, ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mei,
                          nullptr, nullptr);
        CloseHandle(f);
    }
    // The report.
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const DWORD64 at = reinterpret_cast<DWORD64>(ep->ExceptionRecord->ExceptionAddress);
    std::string r = gHandler.header ? gHandler.header() : std::string();
    char line[256];
    std::snprintf(line, sizeof line, "Exception 0x%08lX (%s) at %s", code, exceptionName(code), where(at).c_str());
    r += line;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0], addr = ep->ExceptionRecord->ExceptionInformation[1];
        std::snprintf(line, sizeof line, ", %s 0x%llX", kind == 0 ? "reading" : kind == 1 ? "writing" : "running", static_cast<unsigned long long>(addr));
        r += line;
    }
    r += "\n";
    if (gHandler.context) r += gHandler.context();
    // The stack: walked from the crash's own context.
    r += "\nStack:\n";
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip, sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp, sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp, sf.AddrStack.Mode = AddrModeFlat;
    HANDLE proc = GetCurrentProcess();
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
    if (gHandler.log) r += gHandler.log();
    std::ofstream(dir / "report.txt", std::ios::binary) << r;
    if (wrote) SetEvent(wrote);
    if (gHandler.written) gHandler.written(dir);
}

struct CrashJob {
    EXCEPTION_POINTERS* ep;
    DWORD threadId;
    HANDLE wrote;
};
DWORD WINAPI crashWork(void* p) {
    const CrashJob& job = *static_cast<const CrashJob*>(p);
    HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, job.threadId);
    writeCrash(job.ep, job.threadId, thread ? thread : GetCurrentThread(), job.wrote);
    if (thread) CloseHandle(thread);
    return 0;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    static LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;
    // The report is written by a new thread with a stack of its own: after a stack overflow the crashed thread has
    // too little stack left for it (the minidump was written, and then the report stopped). A crash inside the heap can
    // leave its lock taken by this thread, and the report's thread then waits for it forever: after writeSeconds
    // without the report, the program ends (as the macOS and Linux handler's alarm). After the report, `written` can
    // wait for the user (its message box).
    HANDLE wrote = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CrashJob job{ep, GetCurrentThreadId(), wrote};
    if (HANDLE worker = CreateThread(nullptr, 1u << 20, crashWork, &job, 0, nullptr)) {
        HANDLE both[2] = {wrote, worker};
        const DWORD limit = DWORD(std::max(1, gHandler.writeSeconds)) * 1000;
        const DWORD r = wrote ? WaitForMultipleObjects(2, both, FALSE, limit) : WaitForSingleObject(worker, limit);
        if (r == WAIT_TIMEOUT) TerminateProcess(GetCurrentProcess(), ep->ExceptionRecord->ExceptionCode);
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    } else {
        writeCrash(ep, job.threadId, GetCurrentThread());
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// Errors the C library and C++ report in their own ways: made into exceptions, so onCrash writes them down too.
void raise(DWORD code) { RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr); }

}  // namespace

void installCrashHandler(CrashHandler handler) {
    gHandler = std::move(handler);
    ULONG guarantee = 64 * 1024;  // the stack that this thread's handler has after a stack overflow (to start the report's thread)
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(onCrash);
    std::set_terminate([] { raise(0xE5D0000B); });
    _set_purecall_handler([] { raise(0xE5D0000C); });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { raise(0xE5D0000D); });
    signal(SIGABRT, [](int) { raise(0xE5D0000A); });
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

std::string systemName() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{sizeof v};
    if (auto f = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) f(&v);
    return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." + std::to_string(v.dwBuildNumber);
}

}  // namespace stardraft::platform
