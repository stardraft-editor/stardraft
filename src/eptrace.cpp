#include "stardraft/eptrace.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace stardraft {
namespace eptrace {

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool readText(const fs::path& file, std::string& out) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

// epTrace writes Windows paths. As a path here: '\' is a separator on every system (on POSIX it is not one).
fs::path windowsPath(std::string file) {
#ifndef _WIN32
    std::replace(file.begin(), file.end(), '\\', '/');
#endif
    return fs::path(file);
}

// The same file whatever the case or the separators (Windows paths).
std::string fileKey(const std::string& file) {
    std::string k = windowsPath(file).lexically_normal().generic_string();
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return k;
}

}  // namespace

bool sameFile(const fs::path& a, const fs::path& b) { return fileKey(a.string()) == fileKey(b.string()); }

fs::path exe(const fs::path& euddraftExe) {
#ifdef _WIN32
    std::error_code ec;
    const fs::path p = euddraftExe.parent_path() / "epTrace.exe";
    return fs::is_regular_file(p, ec) ? p : fs::path{};
#else
    (void)euddraftExe;
    return {};  // epTrace.exe is a Windows program (it reads a running StarCraft.exe)
#endif
}

Report parse(const std::string& prof, const std::string& epmap) {
    // The .epmap: "H0: <hex>", "H1: <hex>", then " - <id> : file|function|line".
    std::map<std::pair<std::string, int>, std::string> functions;
    for (const auto& l : splitLines(epmap)) {
        const size_t colon = l.find(" : ");
        if (l.rfind(" - ", 0) != 0 || colon == std::string::npos) continue;
        const std::string entry = l.substr(colon + 3);
        const size_t b2 = entry.rfind('|');
        const size_t b1 = b2 == std::string::npos || b2 == 0 ? std::string::npos : entry.rfind('|', b2 - 1);
        if (b1 == std::string::npos) continue;
        std::string fn = entry.substr(b1 + 1, b2 - b1 - 1);
        if (fn.rfind("f_", 0) == 0) fn.erase(0, 2);
        const int line = std::atoi(entry.c_str() + b2 + 1);
        functions.try_emplace({fileKey(entry.substr(0, b1)), line}, fn);
    }

    // The .prof: for each script, [File "<path>"], a line of dashes, then each line of the script after
    // "[<samples> ]    " (or as many spaces when it has none).
    Report r;
    std::string file;
    int lineNo = 0;
    for (const auto& l : splitLines(prof)) {
        if (l.rfind("[File \"", 0) == 0) {
            const size_t end = l.rfind("\"]");
            file = end != std::string::npos && end > 7 ? l.substr(7, end - 7) : l.substr(7);
            lineNo = 0;
            ++r.files;
            continue;
        }
        if (file.empty() || l.empty() || l.find_first_not_of('-') == std::string::npos) continue;
        int64_t samples = 0;
        std::string code;
        if (l[0] == '[') {  // "[%7d ]    code"
            const size_t close = l.find(']');
            if (close == std::string::npos) continue;
            samples = std::atoll(l.c_str() + 1);
            code = l.size() > close + 5 ? l.substr(close + 5) : "";
        } else if (l.size() >= 14 && l.find_first_not_of(' ') >= 14) {
            code = l.substr(14);
        } else {
            continue;
        }
        ++lineNo;
        if (samples <= 0) continue;
        Line out;
        out.file = windowsPath(file), out.line = lineNo, out.samples = samples, out.code = code;
        out.code.erase(0, out.code.find_first_not_of(" \t"));
        if (auto it = functions.find({fileKey(file), lineNo}); it != functions.end()) out.function = it->second;
        r.total += samples;
        r.lines.push_back(std::move(out));
    }
    std::stable_sort(r.lines.begin(), r.lines.end(), [](const Line& a, const Line& b) { return a.samples > b.samples; });
    return r;
}

bool read(const fs::path& prof, const fs::path& epmap, Report& out, std::string& error) {
    std::string p, m;
    if (!readText(prof, p)) {
        error = "cannot read " + prof.string();
        return false;
    }
    if (!epmap.empty()) readText(epmap, m);  // optional: it names the functions
    out = parse(p, m);
    return true;
}

}  // namespace eptrace
}  // namespace stardraft
