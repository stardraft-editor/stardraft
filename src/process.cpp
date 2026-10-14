#include "stardraft/process.h"

#include <cerrno>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace stardraft {

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
#endif

// Splits output into lines as it comes: each full line goes to `onLine` (without its CR LF or LF).
struct LineSplitter {
    const std::function<void(const std::string&)>& onLine;
    std::string pending;
    void add(const char* data, size_t n) {
        pending.append(data, n);
        size_t start = 0;
        for (size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
            std::string line = pending.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
        }
        pending.erase(0, start);
    }
    void finish() {
        if (!pending.empty() && onLine) onLine(pending);
        pending.clear();
    }
};

}  // namespace

int runProcess(const std::vector<std::string>& args, const fs::path& workDir, const std::function<void(const std::string&)>& onLine) {
#ifdef _WIN32
    std::wstring cmd;
    for (const auto& a : args) {
        if (!cmd.empty()) cmd += L' ';
        cmd += L'"' + widen(a) + L'"';
    }
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return -1;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul, si.hStdOutput = writeEnd, si.hStdError = writeEnd;
    PROCESS_INFORMATION pi{};
    const std::wstring dir = workDir.empty() ? std::wstring() : workDir.wstring();
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(readEnd);
        return -1;
    }
    LineSplitter lines{onLine};
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(readEnd, buf, sizeof buf, &got, nullptr) && got) lines.add(buf, got);
    lines.finish();
    CloseHandle(readEnd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
#else
    // posix_spawnp with the arguments as they are (no shell, so no quoting); the working folder is set in the child.
    if (args.empty()) return -1;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    // Both ends close in other children (another runProcess at the same time); dup2 gives this child its own 1 and 2.
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    if (!workDir.empty()) posix_spawn_file_actions_addchdir_np(&actions, workDir.c_str());
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int started = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (started != 0) {
        close(fds[0]);
        return -1;
    }
    LineSplitter lines{onLine};
    char buf[4096];
    for (ssize_t got; (got = read(fds[0], buf, sizeof buf)) != 0;) {
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        lines.add(buf, size_t(got));
    }
    lines.finish();
    close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // namespace stardraft
