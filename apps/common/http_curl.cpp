// httpRequest with the curl program (Linux and macOS have it). The token and the body go through files, never on the
// command line, where other users' programs could read them.

#include "http.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include "stardraft/process.h"

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

// A new folder that only this user can read; removed when it goes.
struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        std::error_code ec;
        for (int i = 0; i < 16 && path.empty(); ++i) {
            const fs::path p = fs::temp_directory_path(ec) / ("stardraft-http-" + std::to_string(rd()));
            if (fs::create_directory(p, ec)) path = p;
        }
        if (!path.empty()) fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
    }
    ~TempDir() {
        std::error_code ec;
        if (!path.empty()) fs::remove_all(path, ec);
    }
};

bool writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
    out.close();
    return bool(out);
}

}  // namespace

HttpReply httpRequest(const HttpRequest& request, std::string* text, const fs::path* file) {
    HttpReply r;
    TempDir tmp;
    if (tmp.path.empty()) return r;
    std::string headers = "Accept: " + request.accept + "\n";
    const bool github = request.url.rfind("https://api.github.com/", 0) == 0;
    if (github && !request.token.empty()) headers += "Authorization: Bearer " + request.token + "\n";
    if (!request.body.empty()) headers += "Content-Type: application/json\n";
    const fs::path headerFile = tmp.path / "headers", bodyFile = tmp.path / "body", outFile = tmp.path / "out";
    if (!writeText(headerFile, headers) || (!request.body.empty() && !writeText(bodyFile, request.body))) return r;
    std::vector<std::string> args = {"curl", "-sS", "--proto", "=https", "--connect-timeout", "30", "--max-time", "600",  // (no endless wait)
                                     "-X", request.method, "-H", "@" + headerFile.string(), "-o", outFile.string(),
                                     "-w", "%{http_code}\n%{redirect_url}\n"};
    if (request.followRedirects) args.push_back("-L");
    if (!request.body.empty()) args.push_back("--data-binary"), args.push_back("@" + bodyFile.string());
    args.push_back(request.url);
    std::vector<std::string> lines;
    const int code = runProcess(args, {}, [&](const std::string& l) { lines.push_back(l); });
    if (code != 0 || lines.empty()) return r;  // no reply (curl writes its error on the same output)
    r.status = std::atoi(lines[0].c_str());
    if (r.status == 0) return r;
    if (!request.followRedirects && r.status >= 300 && r.status < 400) {
        if (lines.size() > 1) r.location = lines[1];
        return r;
    }
    std::error_code ec;
    if (file) {
        if (r.status != 200) return r;
        fs::copy_file(outFile, *file, fs::copy_options::overwrite_existing, ec);
        r.complete = !ec;
        return r;
    }
    if (text) {
        std::ifstream in(outFile, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        *text += ss.str();
    }
    r.complete = true;
    return r;
}

}  // namespace stardraft::platform
