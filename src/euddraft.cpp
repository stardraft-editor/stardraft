#include "stardraft/euddraft.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdio>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace stardraft::euddraft {

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

// Runs a program (no console window, no input), calling `onLine` for each line of its output
// (stdout and stderr merged). Returns the exit code, or -1 if it could not start.
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
    std::string pending;
    char buf[4096];
    DWORD got = 0;
    auto flush = [&](bool all) {
        size_t start = 0;
        for (size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
            std::string line = pending.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
        }
        pending.erase(0, start);
        if (all && !pending.empty() && onLine) onLine(pending), pending.clear();
    };
    while (ReadFile(readEnd, buf, sizeof buf, &got, nullptr) && got) {
        pending.append(buf, got);
        flush(false);
    }
    flush(true);
    CloseHandle(readEnd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
#else
    std::string cmd = workDir.empty() ? std::string() : "cd '" + workDir.string() + "' && ";
    for (const auto& a : args) cmd += "'" + a + "' ";
    cmd += "2>&1 </dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    char line[4096];
    while (fgets(line, sizeof line, p)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (onLine) onLine(s);
    }
    int status = pclose(p);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::string capture(const std::vector<std::string>& args, int& code) {
    std::string out;
    code = runProcess(args, {}, [&](const std::string& l) { out += l + "\n"; });
    return out;
}

const char* platformTag() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

fs::path localData() {
#ifdef _WIN32
    if (const char* v = std::getenv("LOCALAPPDATA")) return v;
    return fs::temp_directory_path();
#else
    if (const char* v = std::getenv("XDG_DATA_HOME")) return v;
    if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local" / "share";
    return fs::temp_directory_path();
#endif
}

const char* exeName() {
#ifdef _WIN32
    return "euddraft.exe";
#else
    return "euddraft";
#endif
}

// Copies under a root: <root>/<anything>/euddraft(.exe), one level deep.
void collect(const fs::path& root, const std::string& source, std::vector<EuddraftInstall>& out) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    std::vector<EuddraftInstall> found;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const fs::path exe = e.path() / exeName();
        if (fs::exists(exe, ec)) found.push_back({exe, versionOf(exe), source});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return compareVersions(a.version, b.version) > 0; });
    out.insert(out.end(), found.begin(), found.end());
}

}  // namespace

fs::path managedRoot() { return localData() / "StarDraft" / "euddraft"; }

std::string versionOf(const fs::path& exe) {
    std::ifstream f(exe.parent_path() / "VERSION");
    std::string v;
    std::getline(f, v);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
    return v;
}

int compareVersions(const std::string& a, const std::string& b) {
    std::istringstream sa(a), sb(b);
    std::string pa, pb;
    while (true) {
        const bool ha = bool(std::getline(sa, pa, '.')), hb = bool(std::getline(sb, pb, '.'));
        if (!ha && !hb) return 0;
        const long va = ha ? std::atol(pa.c_str()) : 0, vb = hb ? std::atol(pb.c_str()) : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
}

std::vector<EuddraftInstall> installed(const fs::path& chosen) {
    std::vector<EuddraftInstall> out;
    std::error_code ec;
    if (!chosen.empty() && fs::exists(chosen, ec)) out.push_back({chosen, versionOf(chosen), "chosen"});
    collect(managedRoot(), "stardraft", out);
#ifdef _WIN32
    collect(localData() / "EUD_Editor" / "euddraft", "eud editor", out);
#endif
    return out;
}

std::optional<EuddraftInstall> find(const fs::path& chosen) {
    // A copy the user chose, else StarDraft's own newest. Other installs (EUD Editor's, the user's own
    // folders) are listed but never built with unless chosen: their versions are not ours to manage.
    for (const auto& i : installed(chosen))
        if (i.source == "chosen" || i.source == "stardraft") return i;
    return std::nullopt;
}

bool latestRelease(Release& out, std::string& error) {
    int code = 0;
    const std::string json = capture({"curl", "-sL", "-H", "Accept: application/vnd.github+json",
                                      "https://api.github.com/repos/armoha/euddraft/releases/latest"}, code);
    if (code != 0 || json.empty()) {
        error = "could not reach GitHub (curl exit " + std::to_string(code) + ")";
        return false;
    }
    std::smatch m;
    if (!std::regex_search(json, m, std::regex(R"re("tag_name"\s*:\s*"v?([0-9.]+)")re"))) {
        error = "no release found";
        return false;
    }
    out.version = m[1];
    // This platform's zip; older releases have only "euddraft<version>.zip" (Windows).
    const std::regex asset(R"re("browser_download_url"\s*:\s*"([^"]+\.zip)")re");
    std::string generic;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), asset); it != std::sregex_iterator(); ++it) {
        const std::string url = (*it)[1];
        if (url.find(std::string("-") + platformTag() + ".zip") != std::string::npos) out.zipUrl = url;
        else if (url.find("-linux") == std::string::npos && url.find("-macos") == std::string::npos &&
                 url.find("-windows") == std::string::npos)
            generic = url;
    }
#ifdef _WIN32
    if (out.zipUrl.empty()) out.zipUrl = generic;
#endif
    if (out.zipUrl.empty()) {
        error = std::string("release ") + out.version + " has no " + platformTag() + " download";
        return false;
    }
    return true;
}

bool install(const Release& release, EuddraftInstall& out, std::string& error, const std::function<void(const std::string&)>& progress) {
    std::error_code ec;
    const fs::path dir = managedRoot() / release.version, zip = managedRoot() / ("euddraft" + release.version + ".zip");
    fs::create_directories(dir, ec);
    if (progress) progress("Downloading " + release.zipUrl);
    if (runProcess({"curl", "-sSL", "-o", zip.string(), release.zipUrl}, {}, progress) != 0) {
        error = "the download did not complete";
        return false;
    }
    if (progress) progress("Unpacking into " + dir.string());
    if (runProcess({"tar", "-xf", zip.string(), "-C", dir.string()}, {}, progress) != 0) {
        error = "could not unpack " + zip.string();
        return false;
    }
    fs::remove(zip, ec);
    // Some zips hold a top folder ("euddraft0.9.10.9/"); find the exe one level down if so.
    fs::path exe = dir / exeName();
    if (!fs::exists(exe, ec))
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (fs::exists(e.path() / exeName(), ec)) {
                for (const auto& f : fs::directory_iterator(e.path(), ec)) fs::rename(f.path(), dir / f.path().filename(), ec);
                fs::remove(e.path(), ec);
                break;
            }
    if (!fs::exists(exe, ec)) {
        error = "the download holds no " + std::string(exeName());
        return false;
    }
    out = {exe, versionOf(exe), "stardraft"};
    if (out.version.empty()) out.version = release.version;
    return true;
}

BuildResult build(const fs::path& exe, const fs::path& edsFile, const std::function<void(const std::string&)>& onLine) {
    BuildResult r;
    r.exitCode = runProcess({exe.string(), edsFile.filename().string()}, edsFile.parent_path(), [&](const std::string& line) {
        r.log += line + "\n";
        // euddraft marks failures with "[Error]" (and Python tracebacks).
        if (line.find("[Error]") != std::string::npos || line.find("Traceback") != std::string::npos) r.errors.push_back(line);
        if (onLine) onLine(line);
    });
    r.ok = r.exitCode == 0 && r.errors.empty();
    return r;
}

}  // namespace stardraft::euddraft
