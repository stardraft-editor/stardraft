#pragma once

// Crash reports. The system part (crash_win32.cpp; on macOS and Linux crash_posix.cpp, a signal handler) catches the
// crash and writes <folder>/<time>/report.txt (the program's lines, where it crashed, the stack) and, on Windows,
// crash.dmp.
// The portable part (crash_report.cpp) sends a report to GitHub as an issue, or as a comment on the open issue of the
// same crash. What the user is asked and told stays in the program.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace stardraft::platform {

struct CrashHandler {
    std::filesystem::path folder;           // the reports go into <folder>/<time>/
    std::function<std::string()> header;    // the report's first lines: program, version, system, video card
    std::function<std::string()> context;   // lines after the crash's place (the workspace); may be empty
    std::function<std::string()> log;       // the last log lines, at the end; may be empty
    // After the report is written (in the crashed program: little is safe now). Starts the sender, tells the user.
    std::function<void(const std::filesystem::path& dir)> written;
    // The longest time the report's write may take. A crash inside the heap can leave its lock taken, and the write
    // then waits for it forever: after this time the program ends without the report (the window does not freeze).
    int writeSeconds = 120;
};

// Catches what ends the program: the system's crash, and the C and C++ libraries' own errors (abort, terminate, a
// pure virtual call, an invalid parameter).
void installCrashHandler(CrashHandler handler);

// The system and its version ("Windows 10.0.26200", "macOS 15.1", "Linux 6.6.87").
std::string systemName();

// A report's issue title: the place it crashed, without what changes each time (the address read), so the same crash
// finds its issue again.
std::string crashTitle(const std::string& report);

// Sends <dir>/report.txt to the repository's issues with the token; true when it arrived (the folder is then marked
// "sent"). A search that was not read completely sends nothing: the report waits for the next try.
// `claimed`: the caller has the folder's "sending" mark already (claimCrashReport: the crashed program takes it for its
// sender); else the send takes it first, and sends nothing when another sender has it.
bool sendCrashReport(const std::filesystem::path& dir, const std::string& repo, const std::string& token, bool claimed = false);

// The report folders that were not sent and not refused (no "sent" mark), and that no sender is sending now.
std::vector<std::filesystem::path> unsentCrashReports(const std::filesystem::path& folder);

// A sender takes a report folder before it sends it (a "sending" mark that only one program can make), so that the
// crashed program's sender and the next start do not both send it. False: another sender has it (a mark older than
// 10 minutes is of a sender that stopped: it is taken again). releaseCrashReport removes the mark.
bool claimCrashReport(const std::filesystem::path& dir);
void releaseCrashReport(const std::filesystem::path& dir);

// A crash report's log lines without the user's own folder: `home` (the user's folder, as C:\Users\Name or
// /home/name, with either separator and in any case) becomes "~", and the account's name (the folder's last part)
// elsewhere becomes "<user>". The report goes to a public issue.
std::string withoutUserFolder(const std::string& text, const std::string& home);

// Marks a report folder as done with a reason ("sent", "not sent: the user said no").
void markCrashReport(const std::filesystem::path& dir, const std::string& reason);

}  // namespace stardraft::platform
