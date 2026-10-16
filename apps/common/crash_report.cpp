#include "crash_report.h"

#include <cctype>
#include <chrono>
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

namespace {
const char* const kClaim = "sending";
bool sendClaimed(const fs::path& dir, const std::string& repo, const std::string& token);
}  // namespace

bool claimCrashReport(const fs::path& dir) {
    std::error_code ec;
    const fs::path mark = dir / kClaim;
    if (fs::create_directory(mark, ec)) return true;  // (making a folder: only one program can)
    if (!fs::is_directory(mark, ec)) return false;
    const auto age = fs::file_time_type::clock::now() - fs::last_write_time(mark, ec);
    if (ec || age < std::chrono::minutes(10)) return false;
    fs::remove(mark, ec);  // a sender that stopped: the next takes it
    return fs::create_directory(mark, ec);
}

void releaseCrashReport(const fs::path& dir) {
    std::error_code ec;
    fs::remove(dir / kClaim, ec);
}

bool sendCrashReport(const fs::path& dir, const std::string& repo, const std::string& token, bool claimed) {
    std::error_code ec;
    if (token.empty() || fs::exists(dir / "sent", ec)) {
        if (claimed) releaseCrashReport(dir);
        return false;
    }
    if (!claimed && !claimCrashReport(dir)) return false;  // another sender sends it now
    const bool sent = !fs::exists(dir / "sent", ec) && sendClaimed(dir, repo, token);  // (it may have finished just before the claim)
    releaseCrashReport(dir);
    return sent;
}

namespace {
bool sendClaimed(const fs::path& dir, const std::string& repo, const std::string& token) {
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
}  // namespace

std::vector<fs::path> unsentCrashReports(const fs::path& folder) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (!fs::exists(e.path() / "report.txt", ec) || fs::exists(e.path() / "sent", ec)) continue;
        if (fs::is_directory(e.path() / kClaim, ec)) {  // a sender has it now (unless it stopped 10 minutes ago)
            const auto age = fs::file_time_type::clock::now() - fs::last_write_time(e.path() / kClaim, ec);
            if (!ec && age < std::chrono::minutes(10)) continue;
        }
        out.push_back(e.path());
    }
    return out;
}

std::string withoutUserFolder(const std::string& text, const std::string& home) {
    auto fold = [](char ch) { return ch == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(ch))); };
    std::string h = home;
    while (h.size() > 1 && (h.back() == '/' || h.back() == '\\')) h.pop_back();
    if (h.size() < 3) return text;
    std::string out;
    out.reserve(text.size());
    // The folder: "~" (the part after it stays).
    for (size_t i = 0; i < text.size();) {
        bool at = i + h.size() <= text.size();
        for (size_t k = 0; at && k < h.size(); ++k) at = fold(text[i + k]) == fold(h[k]);
        if (at) {
            out += '~';
            i += h.size();
        } else {
            out += text[i++];
        }
    }
    // The account's name elsewhere (a path that names it in another way: D:\Name\maps): "<user>", where it is a word.
    const size_t cut = h.find_last_of("/\\");
    const std::string user = cut == std::string::npos ? std::string() : h.substr(cut + 1);
    if (user.size() < 3) return out;
    auto word = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || (static_cast<unsigned char>(ch) & 0x80); };
    std::string done;
    for (size_t i = 0; i < out.size();) {
        bool at = i + user.size() <= out.size() && (i == 0 || !word(out[i - 1])) && (i + user.size() == out.size() || !word(out[i + user.size()]));
        for (size_t k = 0; at && k < user.size(); ++k) at = fold(out[i + k]) == fold(user[k]);
        if (at) {
            done += "<user>";
            i += user.size();
        } else {
            done += out[i++];
        }
    }
    return done;
}

void markCrashReport(const fs::path& dir, const std::string& reason) { std::ofstream(dir / "sent") << reason; }

}  // namespace stardraft::platform
