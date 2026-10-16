// The crash handler on macOS and Linux: a signal that ends the program (a bad memory access, an illegal instruction,
// abort, a C++ exception that no one catches) writes report.txt, then calls the program's `written`; then the signal
// goes on to the system as before (macOS's own crash report, the core file, the exit status). The handler runs on a
// stack of its own, so that a stack overflow also writes its report.

#include "crash_report.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>

#include <dlfcn.h>
#include <execinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

CrashHandler gHandler;
volatile sig_atomic_t gOnce = 0;
constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "segmentation fault";
    case SIGBUS: return "bus error";
    case SIGILL: return "illegal instruction";
    case SIGFPE: return "arithmetic error";
    case SIGABRT: return "abort";
    case SIGTRAP: return "trap";
    default: return "signal";
    }
}

// "stardraft+0x1A2B3C" for an address (the file that has it, and the place in it), as the Windows report has it.
std::string where(const void* address) {
    Dl_info info{};
    char b[64];
    if (!dladdr(address, &info) || !info.dli_fbase) {
        std::snprintf(b, sizeof b, "%p", address);
        return b;
    }
    std::snprintf(b, sizeof b, "+0x%lX", static_cast<unsigned long>(static_cast<const char*>(address) - static_cast<const char*>(info.dli_fbase)));
    std::string r = info.dli_fname ? fs::path(info.dli_fname).filename().string() : std::string("?");
    r += b;
    if (info.dli_sname) r += std::string("  ") + info.dli_sname;
    return r;
}

void writeCrash(int sig, siginfo_t* si, const char* what) {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &t);
    std::error_code ec;
    const fs::path dir = gHandler.folder / stamp;
    fs::create_directories(dir, ec);
    std::string r = gHandler.header ? gHandler.header() : std::string();
    char line[256];
    if (what) std::snprintf(line, sizeof line, "%s", what);
    else std::snprintf(line, sizeof line, "Signal %d (%s), address %p", sig, signalName(sig), si ? si->si_addr : nullptr);
    r += line;
    r += "\n";
    if (gHandler.context) r += gHandler.context();
    // The stack of the thread that crashed (the handler runs on it, on its own stack): the handler's frames first.
    r += "\nStack:\n";
    void* frames[64];
    const int n = backtrace(frames, 64);
    for (int i = 0; i < n; ++i) r += "  " + where(frames[i]) + "\n";
    r += "\nLog (last lines):\n";
    if (gHandler.log) r += gHandler.log();
    std::ofstream(dir / "report.txt", std::ios::binary) << r;
    if (gHandler.written) gHandler.written(dir);
}

void onSignal(int sig, siginfo_t* si, void*) {
    if (!gOnce) {
        gOnce = 1;
        writeCrash(sig, si, nullptr);
    }
    // The system's own handling now: macOS's crash report, the exit status of the signal.
    signal(sig, SIG_DFL);
    raise(sig);
}

}  // namespace

void installCrashHandler(CrashHandler handler) {
    gHandler = std::move(handler);
    static char altStack[256 * 1024];  // (a stack overflow leaves the thread's own stack no room for the report)
    stack_t ss{};
    ss.ss_sp = altStack, ss.ss_size = sizeof altStack, ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (const int s : kSignals) sigaction(s, &sa, nullptr);
    std::set_terminate([] {
        if (!gOnce) {
            gOnce = 1;
            writeCrash(SIGABRT, nullptr, "C++ exception not caught (terminate)");
        }
        signal(SIGABRT, SIG_DFL);
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
    utsname u{};
    if (uname(&u) == 0) return std::string(u.sysname) + " " + u.release;
    return "Linux";
#endif
}

}  // namespace stardraft::platform
