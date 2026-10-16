// The system parts of apps/common that need no network and no window: the self-update's unpacking and replacing (on
// files in a scratch folder, never on the running program), the program's own file, and the crash report's title. On
// Linux and the BSDs also: one program for all maps (its socket), a notification with no session bus, and the crash
// handler (in child processes that crash).
//
// usage: platform_test <scratch dir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "crash_report.h"
#include "update.h"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "desktop_xdg.h"
#endif

namespace fs = std::filesystem;
using namespace stardraft::platform;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++failures;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

// A zip with one file, stardraft.exe, whose text is "new program".
const unsigned char kZip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x31, 0xad, 0x4e, 0x5d, 0x56, 0xa7, 0xde, 0x3c, 0x0d, 0x00, 0x00, 0x00, 0x0b,
    0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x73, 0x74, 0x61, 0x72, 0x64, 0x72, 0x61, 0x66, 0x74, 0x2e, 0x65, 0x78, 0x65, 0xcb, 0x4b, 0x2d,
    0x57, 0x28, 0x28, 0xca, 0x4f, 0x2f, 0x4a, 0xcc, 0x05, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x31,
    0xad, 0x4e, 0x5d, 0x56, 0xa7, 0xde, 0x3c, 0x0d, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x61, 0x72, 0x64, 0x72, 0x61, 0x66, 0x74, 0x2e, 0x65, 0x78, 0x65,
    0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00};

#if !defined(_WIN32) && !defined(__APPLE__)
// A call that calls itself until the stack runs out.
int crashDepth(int n) {
    volatile char frame[4096];
    frame[0] = char(n);
    return n > 0 ? crashDepth(n + 1) + frame[0] : 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: platform_test <scratch dir>\n"), 2;
    const fs::path dir = fs::path(argv[1]) / "platform test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "unpacked", ec);

    const fs::path self = programFile();
    check(!self.empty() && fs::exists(self, ec) && self.filename().string().find("platform_test") == 0, "the program's own file: " + self.filename().string());
    check(std::string(releaseAssetSuffix()).size() > 4, std::string("this system's download ends in ") + releaseAssetSuffix());

    // Unpacking a release's zip.
    std::ofstream(dir / "update.zip", std::ios::binary).write(reinterpret_cast<const char*>(kZip), sizeof kZip);
    check(unpackArchive(dir / "update.zip", dir / "unpacked") && read(dir / "unpacked" / "stardraft.exe") == "new program", "a zip unpacks");

    // Replacing a program file (a stand-in in the scratch folder).
    const fs::path program = dir / "stardraft.exe";
    std::ofstream(program, std::ios::binary) << "old program";
    check(replaceProgram(program, dir / "unpacked" / "stardraft.exe") == Replace::Done && read(program) == "new program" &&
              read(oldProgramFile(program)) == "old program",
          "the new program takes the old one's place; the old one is kept beside it");
#ifdef _WIN32
    check(!startProgram(program, {"--restart", "1"}), "a file that is not a program (a damaged download) does not start");
#endif
    // The new program did not start: it goes, and the old one is in its place again.
    check(restoreProgram(program) && read(program) == "old program" && !fs::exists(oldProgramFile(program), ec),
          "a new program that did not start: the old one is in its place again");
    check(!restoreProgram(program) && read(program) == "old program", "no old program to put back: the program stays");
    check(replaceProgram(program, dir / "no such file") == Replace::NewNotPlaced && read(program) == "old program",
          "a new program that is not there: the program stays as it was");

    // A crash report's title: the place, without the address read (it differs each time).
    check(crashTitle("StarDraft\nException 0xC0000005 (access violation) at stardraft.exe+0x1234, reading 0x10\nStack:\n") ==
              "Crash: Exception 0xC0000005 (access violation) at stardraft.exe+0x1234",
          "a crash report's title");
    check(unsentCrashReports(dir / "no crashes").empty(), "no reports in a folder that is not there");

#if !defined(_WIN32) && !defined(__APPLE__)
    // Linux and the BSDs: one program for all maps (a socket, desktop_xdg.h).
    namespace x = stardraft::xdg;
    const fs::path sock = dir / "instance.sock";
    check(!x::sendToInstance(sock, "a.scx\n"), "no program listens: the map is not sent (this program is the first)");
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::string> got;
    auto collect = [&](std::string text) {
        std::lock_guard<std::mutex> lock(m);
        got.push_back(std::move(text));
        cv.notify_all();
    };
    check(x::listenAsInstance(sock, collect), "the first program listens");
    check(!x::listenAsInstance(sock, collect), "a second program does not listen: the first one does already");
    const std::string paths = "/maps/(4)Bottleneck.scm\n/maps/\xed\x95\x9c\xea\xb5\xad \xec\xa7\x80\xeb\x8f\x84.scx\n";
    check(x::sendToInstance(sock, paths), "a second program sends its maps");
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return !got.empty(); });
        check(got.size() == 1 && got[0] == paths, "the first program gets the maps' paths as they were sent (UTF-8)");
    }
    // A socket file that no program listens on (a program that crashed left it): the next program listens there.
    const fs::path stale = dir / "stale.sock";
    {
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof a.sun_path, "%s", stale.c_str());
        check(fd >= 0 && bind(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0, "a socket file that nobody listens on");
        close(fd);
    }
    check(x::listenAsInstance(stale, [](std::string) {}), "the socket file of a program that ended: the next program listens there");
    const fs::path named = x::instanceSocket("StarDraft-platform-test.sock");
    check(!named.empty() && named.filename() == "StarDraft-platform-test.sock", "the socket's place: " + named.string());

    // No session bus: no notification, and it does not wait.
    {
        const auto start = std::chrono::steady_clock::now();
        const bool sent = x::notify("StarDraft 2", "platform_test");
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        const char* bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!bus || !*bus) check(!sent && ms < 2000, "no session bus: no notification, after " + std::to_string(ms) + " ms");
        else std::printf("note  a session bus: notification %s after %lld ms\n", sent ? "sent" : "not sent", static_cast<long long>(ms));
    }

    // The crash handler (crash_posix.cpp), in a child process that crashes: its report is written, and the child ends
    // with the signal.
    for (const char* kind : {"null", "stack", "abort"}) {
        const fs::path crashes = dir / (std::string("crashes-") + kind);
        const pid_t child = fork();
        if (child == 0) {
            CrashHandler h;
            h.folder = crashes;
            h.header = [] { return std::string("StarDraft test\n" + systemName() + "\n"); };
            h.context = [] { return std::string("Workspace: Map\n"); };
            h.log = [] { return std::string("  a log line\n"); };
            h.written = [](const fs::path& d) { std::ofstream(d / "sent") << "a test run: not sent"; };
            installCrashHandler(std::move(h));
            if (std::string(kind) == "stack") _exit(crashDepth(1));
            if (std::string(kind) == "abort") std::abort();
            *static_cast<volatile int*>(nullptr) = 1;
            _exit(0);
        }
        int status = 0;
        waitpid(child, &status, 0);
        std::string report;
        for (const auto& e : fs::directory_iterator(crashes, ec)) report = read(e.path() / "report.txt");
        const int want = std::string(kind) == "abort" ? SIGABRT : SIGSEGV;
        check(WIFSIGNALED(status) && WTERMSIG(status) == want, std::string("a crash (") + kind + ") ends the program with its signal " + std::to_string(WTERMSIG(status)));
        check(report.find("StarDraft test\n") == 0 && report.find("Exception SIG") != std::string::npos && report.find("Workspace: Map") != std::string::npos &&
                  report.find("\nStack:\n  ") != std::string::npos && report.find("a log line") != std::string::npos,
              std::string("a crash (") + kind + ") writes its report: " + crashTitle(report));
        check(unsentCrashReports(crashes).empty(), std::string("a crash (") + kind + ") in a test: the report is marked, not sent");
    }
    check(crashTitle("x\nException SIGSEGV (segmentation fault) at stardraft+0x1234, reading or writing 0x10\n") ==
              "Crash: Exception SIGSEGV (segmentation fault) at stardraft+0x1234",
          "a Linux crash report's title has no address that changes");
#endif

    fs::remove_all(dir, ec);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
