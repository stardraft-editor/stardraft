#include "crash_report.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "http.h"

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

std::string json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\', o += char(ch);
        else if (ch == '\n') o += "\\n";
        else if (ch == '\r') continue;
        else if (ch == '\t') o += "\\t";
        else if (ch < 0x20) continue;
        else o += char(ch);
    }
    return o + "\"";
}

std::string urlEncode(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += char(ch);
        else {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", ch);
            o += b;
        }
    }
    return o;
}

HttpReply github(const std::string& url, const std::string& method, const std::string& token, const std::string& body, std::string* text) {
    HttpRequest q;
    q.url = url, q.method = method, q.accept = "application/vnd.github+json", q.token = token, q.body = body;
    return httpRequest(q, text);
}

}  // namespace

std::string crashTitle(const std::string& report) {
    const size_t a = report.find("Exception ");
    std::string e = a == std::string::npos ? "a crash" : report.substr(a, report.find('\n', a) - a);
    if (const size_t r = e.find(", reading"); r != std::string::npos) e.resize(r);  // the address it read differs each time
    if (const size_t r = e.find(", writing"); r != std::string::npos) e.resize(r);
    return "Crash: " + e;
}

bool sendCrashReport(const fs::path& dir, const std::string& repo, const std::string& token) {
    std::error_code ec;
    if (token.empty() || fs::exists(dir / "sent", ec)) return false;
    std::ifstream in(dir / "report.txt", std::ios::binary);
    std::string report((std::istreambuf_iterator<char>(in)), {});
    if (report.empty()) return false;
    if (report.size() > 60000) report.resize(60000);
    const std::string t = crashTitle(report), body = "```\n" + report + "```\n";
    // The same crash: a comment on its open issue; a new one: a new issue.
    std::string found;
    const HttpReply search = github("https://api.github.com/search/issues?q=" + urlEncode("repo:" + repo + " is:issue is:open in:title \"" + t + "\""),
                                    "GET", token, "", &found);
    if (!search.complete) return false;
    const size_t num = found.find("\"number\":");
    int status = 0;
    if (search.status == 200 && num != std::string::npos && found.find("\"total_count\":0") == std::string::npos) {
        const std::string n = std::to_string(std::atoi(found.c_str() + num + 9));
        status = github("https://api.github.com/repos/" + repo + "/issues/" + n + "/comments", "POST", token, "{\"body\":" + json(body) + "}", nullptr).status;
    } else {
        status = github("https://api.github.com/repos/" + repo + "/issues", "POST", token,
                        "{\"title\":" + json(t) + ",\"body\":" + json(body) + ",\"labels\":[\"crash\"]}", nullptr).status;
    }
    // 201: sent, even when the reply's body was cut off (it is not used; a retry would post the report again).
    if (status != 201) return false;
    markCrashReport(dir, "sent");
    return true;
}

std::vector<fs::path> unsentCrashReports(const fs::path& folder) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (fs::exists(e.path() / "report.txt", ec) && !fs::exists(e.path() / "sent", ec)) out.push_back(e.path());
    return out;
}

void markCrashReport(const fs::path& dir, const std::string& reason) { std::ofstream(dir / "sent") << reason; }

}  // namespace stardraft::platform
