// The crash handler on macOS, Linux and the BSDs: a signal that ends the program (a bad memory access, an illegal
// instruction, a division by zero, a trap, abort, a C++ exception that nobody caught) writes report.txt, then calls the
// program's `written`, then the program ends with that signal as before (macOS's own crash report, the system's core
// dump, the exit status). The handler runs on a stack of its own, so a stack overflow is reported too. (No minidump:
// report.txt only.)

#include "crash_report.h"

#include <dlfcn.h>
#include <signal.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define STARDRAFT_HAS_BACKTRACE 1
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#elif __has_include(<ucontext.h>)
#include <ucontext.h>
#endif

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

CrashHandler gHandler;
volatile sig_atomic_t gTerminate = 0;  // std::terminate: the abort that follows is a C++ exception not caught
volatile sig_atomic_t gOnce = 0;

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGABRT: return gTerminate ? "SIGABRT (C++ exception not caught: terminate)" : "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (trap)";
    default: return "signal";
    }
}

// "stardraft+0x1A2B3C" for an address (the file of the program or library it is in), else the address.
std::string where(const void* address) {
    char b[64];
    Dl_info info{};
    if (address && dladdr(address, &info) && info.dli_fname && info.dli_fbase) {
        std::snprintf(b, sizeof b, "+0x%llX", static_cast<unsigned long long>(static_cast<const char*>(address) - static_cast<const char*>(info.dli_fbase)));
        std::string r = fs::path(info.dli_fname).filename().string() + b;
        if (info.dli_sname) r += std::string("  ") + info.dli_sname;
        return r;
    }
    std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
    return b;
}

// The instruction that crashed (from the signal's context), or null.
const void* crashedAt(void* context) {
#if defined(__linux__) && defined(__x86_64__)
    if (context) return reinterpret_cast<const void*>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__linux__) && defined(__aarch64__)
    if (context) return reinterpret_cast<const void*>(static_cast<ucontext_t*>(context)->uc_mcontext.pc);
#elif defined(__APPLE__) && defined(__aarch64__)
    if (context && static_cast<ucontext_t*>(context)->uc_mcontext)
        return reinterpret_cast<const void*>(__darwin_arm_thread_state64_get_pc(static_cast<ucontext_t*>(context)->uc_mcontext->__ss));
#elif defined(__APPLE__) && defined(__x86_64__)
    if (context && static_cast<ucontext_t*>(context)->uc_mcontext)
        return reinterpret_cast<const void*>(static_cast<ucontext_t*>(context)->uc_mcontext->__ss.__rip);
#else
    (void)context;
#endif
    return nullptr;
}

void writeCrash(int sig, const siginfo_t* info, void* context) {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    std::error_code ec;
    fs::create_directories(gHandler.folder, ec);
    fs::path dir = gHandler.folder / stamp;
    for (int n = 2; !fs::create_directory(dir, ec) && n < 100; ++n) dir = gHandler.folder / (std::string(stamp) + "-" + std::to_string(n));  // (a crash in the same second)
    std::string r = gHandler.header ? gHandler.header() : std::string();
    r += std::string("Exception ") + signalName(sig) + " at " + where(crashedAt(context));
    if ((sig == SIGSEGV || sig == SIGBUS) && info) {
        char line[64];
        std::snprintf(line, sizeof line, ", reading or writing 0x%llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->si_addr)));
        r += line;  // (crashTitle leaves it out: it differs each time)
    }
    r += "\n";
    if (gHandler.context) r += gHandler.context();
    r += "\nStack:\n";
#ifdef STARDRAFT_HAS_BACKTRACE
    void* frames[64];
    const int n = backtrace(frames, 64);
    for (int i = 0; i < n; ++i) r += "  " + where(frames[i]) + "\n";
#else
    r += "  (not on this system)\n";
#endif
    r += "\nLog (last lines):\n";
    if (gHandler.log) r += gHandler.log();
    std::ofstream(dir / "report.txt", std::ios::binary) << r;
    if (gHandler.written) gHandler.written(dir);
}

void onSignal(int sig, siginfo_t* info, void* context) {
    if (!gOnce) {
        gOnce = 1;
        // (a crash inside the C library's heap can make the report wait for a lock forever: the program ends after
        // 2 minutes; `written` may show a message box that waits for the user)
        alarm(120);
        writeCrash(sig, info, context);
    }
    // The program ends with the signal, as without the handler.
    signal(sig, SIG_DFL);
    raise(sig);
}

}  // namespace

void installCrashHandler(CrashHandler handler) {
    gHandler = std::move(handler);
#ifdef STARDRAFT_HAS_BACKTRACE
    void* first[2];
    backtrace(first, 2);  // (loads its library now: not in the handler)
#endif
    // A stack of its own for the handler (a stack overflow leaves none).
    static char altStack[256 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack, ss.ss_size = sizeof altStack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) sigaction(sig, &sa, nullptr);
    std::set_terminate([] {
        gTerminate = 1;
        std::abort();
    });
}

std::string systemName() {
#ifdef __APPLE__
    char v[64] = "";
    size_t size = sizeof v;
    if (sysctlbyname("kern.osproductversion", v, &size, nullptr, 0) == 0 && v[0]) return std::string("macOS ") + v;
    return "macOS";
#else
    std::string name;
#ifdef __linux__
    // The distribution's name and version (/etc/os-release: PRETTY_NAME), then the kernel.
    if (std::ifstream in("/etc/os-release"); in) {
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
            name = line.substr(12);
            if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'')) name = name.substr(1, name.size() - 2);
        }
    }
#endif
    utsname u{};
    if (uname(&u) == 0) name += (name.empty() ? "" : ", ") + std::string(u.sysname) + " " + u.release + " " + u.machine;
    return name.empty() ? std::string("Unix") : name;
#endif
}

}  // namespace stardraft::platform
