#pragma once

// Crash reports. The system part (crash_win32.cpp; crash_posix.cpp on Linux and the BSDs: a signal handler; none on
// macOS yet) catches the crash and
// writes <folder>/<time>/report.txt (the program's lines, where it crashed, the stack) and, on Windows, crash.dmp.
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
};

// Catches what ends the program: the system's crash, and the C and C++ libraries' own errors (abort, terminate, a
// pure virtual call, an invalid parameter).
void installCrashHandler(CrashHandler handler);

// The system and its version ("Windows 10.0.26200").
std::string systemName();

// A report's issue title: the place it crashed, without what changes each time (the address read), so the same crash
// finds its issue again.
std::string crashTitle(const std::string& report);

// Sends <dir>/report.txt to the repository's issues with the token; true when it arrived (the folder is then marked
// "sent"). A search that was not read completely sends nothing: the report waits for the next try.
bool sendCrashReport(const std::filesystem::path& dir, const std::string& repo, const std::string& token);

// The report folders that were not sent and not refused (no "sent" mark).
std::vector<std::filesystem::path> unsentCrashReports(const std::filesystem::path& folder);

// Marks a report folder as done with a reason ("sent", "not sent: the user said no").
void markCrashReport(const std::filesystem::path& dir, const std::string& reason);

}  // namespace stardraft::platform
