// The system parts of apps/common that need no network and no window: the self-update's unpacking and replacing (on
// files in a scratch folder, never on the running program), the program's own file, and the crash report's title. On
// Linux and the BSDs also: one program for all maps (its socket), a notification with no session bus, and the crash
// handler (in child processes that crash).
//
// usage: platform_test <scratch dir>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../../src/file_write.h"
#include "crash_report.h"
#include "file_types.h"
#include "instance.h"
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

// The editor's install search (apps/editor/core/install_search.inc: portable, files only).
namespace install {
namespace fs = std::filesystem;
#include "../../apps/editor/core/install_search.inc"
}  // namespace install

#if !defined(_WIN32) && !defined(__APPLE__)
// A call that calls itself until the stack runs out.
int crashDepth(int n) {
    volatile char frame[4096];
    frame[0] = char(n);
    return n > 0 ? crashDepth(n + 1) + frame[0] : 0;
}
#endif

#ifdef _WIN32
// The child of the crash handler's test (platform_test --crash-hang <folder>): it crashes, and its report's write waits
// forever (as when the crash left the heap's lock taken). The handler must end it after writeSeconds.
int crashHang(const fs::path& folder) {
    CrashHandler h;
    h.folder = folder;
    h.header = [] {
        Sleep(INFINITE);
        return std::string();
    };
    h.writeSeconds = 2;
    installCrashHandler(std::move(h));
    *static_cast<volatile int*>(nullptr) = 1;
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 2 && std::string(argv[1]) == "--crash-hang") return crashHang(argv[2]);
#endif
    if (argc > 1 && std::string(argv[1]) == "--sleep") {  // (a copy that runs a while: an update's new program, running)
        std::this_thread::sleep_for(std::chrono::seconds(6));
        return 0;
    }
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

    // An update's new program that runs but cannot work here (no graphics device): the program before the update goes
    // back in its place while the new one still runs (Windows: a running program's file cannot be replaced).
    {
        const fs::path run = dir / "running" / ("stardraft" + self.extension().string());
        fs::create_directories(run.parent_path(), ec);
        fs::copy_file(self, run, ec);
        std::ofstream(oldProgramFile(run), std::ios::binary) << "old program";
        const bool started = startProgram(run, {"--sleep"});
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        check(started && rollBackUpdate(run) && read(run) == "old program" && !fs::exists(oldProgramFile(run), ec),
              "a running new program that cannot work here: the program before the update is in its place again");
        check(!rollBackUpdate(run) && read(run) == "old program", "no program before an update: nothing changes");
    }

    // A project file's write (src/file_write.h: triggers.json, the project file, the data edits): a new file takes the
    // old one's place, so a write that stops (a full disk, a crash) never leaves the file empty or cut off. A second
    // name for the same file (a hard link) still has the old bytes: the old file was not written over in place.
    {
        const fs::path f = dir / "triggers.json", link = dir / "triggers-link.json";
        std::ofstream(f, std::ios::binary) << "old triggers";
        std::error_code lec;
        fs::create_hard_link(f, link, lec);
        std::string err;
        const bool wrote = stardraft::writeFile(f, "new triggers", err);
        check(wrote && read(f) == "new triggers", "a file write gives the file its new bytes");
        if (!lec) check(read(link) == "old triggers", "a file write does not write over the old file in place (a new file takes its place)");
        else std::printf("note  no hard link here (%s): the in-place check is skipped\n", lec.message().c_str());
        int parts = 0;
        for (const auto& e : fs::directory_iterator(dir, ec)) parts += e.path().filename().string().find(".part") != std::string::npos;
        check(parts == 0, "a file write leaves no new file beside it");
        check(!stardraft::writeFile(dir / "no such folder" / "x.json", "x", err) && !err.empty(), "a write into a folder that is not there fails: " + err);
    }

    // A crash report's title: the place, without the address read (it differs each time).
    check(crashTitle("StarDraft\nException 0xC0000005 (access violation) at stardraft.exe+0x1234, reading 0x10\nStack:\n") ==
              "Crash: Exception 0xC0000005 (access violation) at stardraft.exe+0x1234",
          "a crash report's title");
    check(unsentCrashReports(dir / "no crashes").empty(), "no reports in a folder that is not there");

    // The install search: a StarCraft folder that is not there does not try the folder above it (C:\Program Files (x86)
    // is no install: its game data error would say that the install is damaged); a folder inside an install does.
    {
        const fs::path games = dir / "Program Files (x86)", inst = dir / "Games" / "StarCraft";
        fs::create_directories(games, ec), fs::create_directories(inst / "x86_64", ec);
        std::ofstream(inst / ".build.info") << "x";
        const auto missing = install::installTries(games / "StarCraft");
        check(missing.size() == 1 && missing[0] == games / "StarCraft", "a StarCraft folder that is not there is the only try");
        const auto inside = install::installTries(inst / "x86_64");
        check(std::find(inside.begin(), inside.end(), inst) != inside.end(), "a folder inside an install tries the install");
    }

#if defined(_WIN32) || !defined(__APPLE__)
    // One program for all maps when several start at once (Explorer starts one for each of several selected maps): one
    // listens, and each of the others sends its map to it (none opens a window of its own).
    {
#ifdef _WIN32
        const fs::path where = L"\\\\.\\pipe\\StarDraft-platform-test-" + std::to_wstring(GetCurrentProcessId());
#else
        const fs::path where = dir / "race.sock";
#endif
        std::mutex m;
        std::condition_variable cv;
        std::vector<std::string> got;
        std::atomic<int> sent{0}, listening{0};
        std::vector<std::thread> starts;
        for (int i = 0; i < 6; ++i)
            starts.emplace_back([&, i] {
                const InstanceStart r = sendOrListen(where, "map " + std::to_string(i) + "\n", [&](std::string text) {
                    std::lock_guard<std::mutex> lock(m);
                    got.push_back(std::move(text));
                    cv.notify_all();
                });
                (r == InstanceStart::Sent ? sent : listening) += r != InstanceStart::Neither;
            });
        for (auto& t : starts) t.join();
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(10), [&] { return got.size() >= 5; });
        check(listening == 1 && sent == 5 && got.size() == 5, "six programs that start at once: one listens (" + std::to_string(listening.load()) +
                                                                  "), and the others' maps go to it (" + std::to_string(got.size()) + " of 5)");
    }
#endif

    // The map files' open command at a start: another copy of StarDraft 2 that is still there keeps it (a copy run once
    // from Downloads or a USB stick does not take the maps); a command of a program that is not there any more, or none,
    // is this program's now.
    {
        const fs::path installed = dir / "Tools" / "stardraft.exe", copy = dir / "Downloads" / "stardraft.exe";
        fs::create_directories(installed.parent_path(), ec), fs::create_directories(copy.parent_path(), ec);
        std::ofstream(installed) << "x", std::ofstream(copy) << "x";
        const std::wstring cmd = L"\"" + installed.wstring() + L"\" \"%1\"";
        check(commandProgram(cmd) == installed.wstring(), "the program of a file type's open command");
        check(keepRegisteredProgram(cmd, copy.wstring()), "a start of a second copy keeps the installed copy's command");
        check(!keepRegisteredProgram(cmd, installed.wstring()), "the installed copy's own command is its own");
        check(!keepRegisteredProgram(L"\"" + (dir / "USB" / "stardraft.exe").wstring() + L"\" \"%1\"", copy.wstring()),
              "a command of a copy that is not there any more is this program's now");
        check(!keepRegisteredProgram(L"", copy.wstring()), "no command: this program's");
        // StarDraft 2.exe unzipped beside the stardraft.exe of 0.7: the same install, so the maps open with the new one
        const fs::path renamed = dir / "Tools" / "StarDraft 2.exe";
        std::ofstream(renamed) << "x";
        check(!keepRegisteredProgram(cmd, renamed.wstring()), "the stardraft.exe of 0.7 in the same folder: this program's");
        check(keepRegisteredProgram(L"\"" + renamed.wstring() + L"\" \"%1\"", copy.wstring()), "a second copy keeps StarDraft 2.exe's command");
    }

    // One sender for a report: the crashed program's sender takes the folder ("sending"), so the next start does not
    // send the same report too (two issues for one crash).
    {
        const fs::path reports = dir / "crash reports", one = reports / "20261016-120000";
        fs::create_directories(one, ec);
        std::ofstream(one / "report.txt") << "StarDraft\nException 0xC0000005 (access violation) at stardraft.exe+0x1234\n";
        check(unsentCrashReports(reports).size() == 1, "a report that was not sent is unsent");
        check(claimCrashReport(one), "a sender takes the report");
        check(!claimCrashReport(one), "a second sender does not take a report that a sender has");
        check(unsentCrashReports(reports).empty(), "a report that a sender sends now is not sent again by the next start");
        check(!sendCrashReport(one, "stardraft-editor/no-such-repo", "a token"), "a report that a sender has is not sent by another");
        releaseCrashReport(one);
        check(claimCrashReport(one) && (releaseCrashReport(one), unsentCrashReports(reports).size() == 1), "after the sender, the report can be taken again");
    }

    // A crash report's log lines go to a public issue: the user's folder is "~", the account's name "<user>".
    check(withoutUserFolder("  Built C:\\Users\\Jane Doe\\Documents\\StarDraft\\out.scx\n  Saved the project to c:/users/jane doe/maps/a\n"
                            "  euddraft found an error in D:\\Jane Doe\\x.eps; jane doex stays\n",
                            "C:\\Users\\Jane Doe") ==
              "  Built ~\\Documents\\StarDraft\\out.scx\n  Saved the project to ~/maps/a\n  euddraft found an error in D:\\<user>\\x.eps; jane doex stays\n",
          "a crash report's log lines have no user folder or account name");
    check(withoutUserFolder("Opened /home/jdoe/maps/(2)Bottleneck.scm", "/home/jdoe/") == "Opened ~/maps/(2)Bottleneck.scm",
          "a crash report's log lines: the home folder on Linux");

#ifdef _WIN32
    // The crash handler, in a child that crashes and whose report's write waits forever (a heap lock that the crash
    // left taken): the handler ends it after its time (2 s here), so the window never freezes.
    {
        std::wstring cmd = L"\"" + self.wstring() + L"\" --crash-hang \"" + (dir / "hang").wstring() + L"\"";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(self.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            const DWORD r = WaitForSingleObject(pi.hProcess, 30000);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            if (r == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
            check(r == WAIT_OBJECT_0 && code == EXCEPTION_ACCESS_VIOLATION,
                  "a crash whose report cannot be written ends the program after its time (exit code " + std::to_string(code) + ")");
            CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
        } else {
            check(false, "the crash handler's test child starts");
        }
    }
#endif

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
