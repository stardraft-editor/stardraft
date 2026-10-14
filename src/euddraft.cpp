#include "stardraft/euddraft.h"

#include "stardraft/process.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>


namespace fs = std::filesystem;

namespace stardraft::euddraft {

namespace {

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
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) return fs::path(h) / "Library" / "Application Support";
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
#if defined(_WIN32) || defined(__APPLE__)
    const std::vector<std::string> unpack = {"tar", "-xf", zip.string(), "-C", dir.string()};  // bsdtar reads zip files
#else
    const std::vector<std::string> unpack = {"unzip", "-o", "-q", zip.string(), "-d", dir.string()};  // GNU tar does not
#endif
    if (runProcess(unpack, {}, progress) != 0) {
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
#ifndef _WIN32
    fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
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
