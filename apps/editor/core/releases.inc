// The program's GitHub parts that do not depend on the window system: the repository and its issues token (the
// feedback's Send uses them too, feedback_send.inc), the newest release and
// whether it is newer than this build, the download and the placing of a release (Help > Check for updates), and the
// crash reports' texts and their sending at start. The Windows program's crash.inc and update.inc and the SDL
// program's sdl_updates.inc call them; what the user is asked stays in each program. The requests are apps/common's
// (http.h), the self-update's file steps too (update.h, crash_report.h).

#if __has_include("build_info.h")
#include "build_info.h"  // STARDRAFT_COMMIT, written on each build (cmake/build_info.cmake)
#endif
#ifndef STARDRAFT_VERSION_NAME
#define STARDRAFT_VERSION_NAME "dev"
#endif
#ifndef STARDRAFT_COMMIT
#define STARDRAFT_COMMIT "unknown"
#endif
#ifndef STARDRAFT_ISSUES_TOKEN
#define STARDRAFT_ISSUES_TOKEN ""
#endif

// The repository that gets the feedback and the crash reports.
constexpr const wchar_t* kIssueRepo = L"stardraft-editor/stardraft";

// A token that may only open issues on the repository. A build made here has none; the environment variable
// STARDRAFT_ISSUES_TOKEN gives one to test with.
std::string issueToken() {
    static const std::string t = [] {
        std::string v = STARDRAFT_ISSUES_TOKEN;
        if (v.empty())
            if (const char* e = std::getenv("STARDRAFT_ISSUES_TOKEN")) v = e;
        return v;
    }();
    return t;
}

// A JSON string of UTF-8 text.
std::string jsonText(const std::string& s) {
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

// The workspace's name in a report (the crash reports' and the feedback's "Workspace:" line), in English.
const wchar_t* reportWorkspaceName(Workspace w) {
    static const wchar_t* const names[] = {L"Map",      L"Data",       L"Triggers", L"Assets",  L"Project",          L"Briefing",
                                           L"Assets",   L"Help",       L"Properties", L"Players",  L"Classic triggers", L"Map data"};
    const int i = int(w);
    return i >= 0 && i < int(sizeof names / sizeof names[0]) ? names[i] : L"Map";
}

namespace releases {

// The requests (apps/common/http.h); a test puts its own answers here (sdl_updates.inc: --check release-token).
using HttpCall = std::function<stardraft::platform::HttpReply(const stardraft::platform::HttpRequest&, std::string*, const fs::path*)>;
inline HttpCall& httpCall() {
    static HttpCall call = [](const stardraft::platform::HttpRequest& q, std::string* text, const fs::path* file) {
        return stardraft::platform::httpRequest(q, text, file);
    };
    return call;
}

// A GET; api.github.com gets the token (more requests an hour than without one). The repository is public: a token
// that GitHub refuses (401: it expired or was revoked) gets the request again without it, so that Check for updates
// works in every build after the token ends. A redirect (a release asset is served from another host, with its own
// signed address) is followed without it. `file`: write the body there.
inline bool get(const std::string& url, std::string* body, const fs::path* file = nullptr, const char* accept = "application/vnd.github+json", int hops = 0,
                const std::string& token = issueToken()) {
    stardraft::platform::HttpRequest q;
    q.url = url, q.method = "GET", q.accept = accept, q.token = token, q.followRedirects = false;
    const stardraft::platform::HttpReply r = httpCall()(q, body, file);
    if (!r.location.empty() && hops < 5) return get(r.location, body, file, accept, hops + 1, token);
    if (r.status == 401 && !token.empty()) {
        if (body) body->clear();
        return get(url, body, file, accept, hops, std::string());
    }
    return r.status == 200 && r.complete;
}

// A string value after `"key":"` (from `from` on); "" when there is none.
inline std::string jsonString(const std::string& j, const std::string& key, size_t from = 0) {
    const std::string k = "\"" + key + "\":\"";
    const size_t a = j.find(k, from);
    if (a == std::string::npos) return {};
    const size_t b = j.find('"', a + k.size());
    return b == std::string::npos ? std::string() : j.substr(a + k.size(), b - a - k.size());
}

struct Release {
    std::string title, commit, zip, page;  // zip: the asset's API address (it downloads with the token)
    std::string download;                  // this system's download for the browser (its browser_download_url)
    std::string tag, published;            // v0.5.0.8; when it was published (2026-10-11T18:00:00Z)
    std::string notes;                     // the release notes as HTML (GitHub's body_html: the release page's text)
    bool prerelease = false;               // a preview (GitHub's pre-release)
};

// A string value after `"key":` (from `from` on) with its escapes decoded, as UTF-8; "" when there is none.
inline std::string jsonValue(const std::string& j, const std::string& key, size_t from = 0) {
    const std::string k = "\"" + key + "\":\"";
    const size_t a = j.find(k, from);
    if (a == std::string::npos) return {};
    std::string o;
    auto put = [&o](unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) o += char(0xC0 | cp >> 6), o += char(0x80 | (cp & 0x3F));
        else if (cp < 0x10000) o += char(0xE0 | cp >> 12), o += char(0x80 | (cp >> 6 & 0x3F)), o += char(0x80 | (cp & 0x3F));
        else o += char(0xF0 | cp >> 18), o += char(0x80 | (cp >> 12 & 0x3F)), o += char(0x80 | (cp >> 6 & 0x3F)), o += char(0x80 | (cp & 0x3F));
    };
    auto hex4 = [&j](size_t at) {
        unsigned v = 0;
        for (size_t i = at; i < at + 4 && i < j.size(); ++i) {
            const char ch = j[i];
            v = v * 16 + unsigned(ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : 0);
        }
        return v;
    };
    for (size_t i = a + k.size(); i < j.size(); ++i) {
        const char ch = j[i];
        if (ch == '"') break;
        if (ch != '\\' || i + 1 >= j.size()) {
            o += ch;
            continue;
        }
        const char e = j[++i];
        switch (e) {
        case 'n': o += '\n'; break;
        case 't': o += '\t'; break;
        case 'r': o += '\r'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'u': {
            unsigned cp = hex4(i + 1);
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < j.size() && j[i + 1] == '\\' && j[i + 2] == 'u') {
                const unsigned lo = hex4(i + 3);
                if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00), i += 6;
            }
            put(cp);
            break;
        }
        default: o += e; break;  // (\" \\ \/)
        }
    }
    return o;
}

// The objects of a JSON array (GitHub's list of releases), each as its own text, in their order.
inline std::vector<std::string> jsonObjects(const std::string& j) {
    std::vector<std::string> out;
    int depth = 0;
    bool text = false, escape = false;
    size_t start = 0;
    for (size_t i = 0; i < j.size(); ++i) {
        const char ch = j[i];
        if (text) {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') text = false;
            continue;
        }
        if (ch == '"') text = true;
        else if (ch == '{') {
            if (depth++ == 0) start = i;
        } else if (ch == '}' && depth > 0 && --depth == 0) out.push_back(j.substr(start, i - start + 1));
    }
    return out;
}

// The JSON without the spaces and line breaks between its values (GitHub indents its JSON for a request without a
// token: "name": "..."), so that jsonString finds "name":"...".
inline std::string compactJson(const std::string& j) {
    std::string o;
    o.reserve(j.size());
    bool text = false, escape = false;
    for (const char ch : j) {
        if (text) {
            o += ch;
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') text = false;
        } else if (ch == '"') {
            o += ch, text = true;
        } else if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            o += ch;
        }
    }
    return o;
}

// The release in GitHub's answer `json` (a release's JSON, indented or not): its title, commit, page and this system's
// download; false when it has no title.
inline bool parseRelease(const std::string& json, Release& r) {
    const std::string j = compactJson(json);
    r = Release{};
    r.title = jsonString(j, "name");  // the release's own name comes before its assets' names
    r.page = jsonString(j, "html_url");
    r.tag = jsonString(j, "tag_name"), r.published = jsonString(j, "published_at");
    r.notes = jsonValue(j, "body_html");
    r.prerelease = j.find("\"prerelease\":true") != std::string::npos;
    if (const size_t a = r.title.find("(build "); a != std::string::npos) r.commit = r.title.substr(a + 7, r.title.find(')', a) - a - 7);
    for (size_t p = 0; (p = j.find("\"browser_download_url\":\"", p)) != std::string::npos; ++p) {
        const std::string u = jsonString(j, "browser_download_url", p);
        // This system's download (StarDraft-<version>-win64.zip; Linux: the AppImage): a release can also have downloads
        // for other systems.
        static const std::string kOurs = stardraft::platform::releaseAssetSuffix();
        if (u.size() < kOurs.size() || u.compare(u.size() - kOurs.size(), kOurs.size(), kOurs) != 0) continue;
        r.download = u;
        // The asset's "url" (api.github.com/.../releases/assets/<id>) comes before its browser_download_url.
        for (size_t k = j.rfind("\"url\":\"", p); k != std::string::npos; k = k ? j.rfind("\"url\":\"", k - 1) : std::string::npos) {
            const std::string a = jsonString(j, "url", k);
            if (a.find("/releases/assets/") != std::string::npos) {
                r.zip = a;
                break;
            }
        }
    }
    return !r.title.empty();
}

// The newest release; false when GitHub cannot be reached.
inline bool latest(Release& r, const std::string& token = issueToken()) {
    std::string j;
    return get("https://api.github.com/repos/" + narrow(kIssueRepo) + "/releases/latest", &j, nullptr, "application/vnd.github+json", 0, token) &&
           parseRelease(j, r);
}

// A version's numbers ("v0.5.0.8", "0.5.0.8": 0 5 0 8); none when it is not only numbers and dots ("0.5 Alpha": a build
// that is not a release's).
inline std::vector<int> versionNumbers(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<int> out;
    int n = -1;
    for (const char ch : v) {
        if (ch >= '0' && ch <= '9') n = (n < 0 ? 0 : n * 10) + (ch - '0');
        else if (ch == '.' && n >= 0) out.push_back(n), n = -1;
        else return {};
    }
    if (n < 0) return {};
    out.push_back(n);
    return out;
}

// "2026-10-11T18:00:00Z" as seconds since 1970 (0: not such a time).
inline long long isoSeconds(const std::string& t) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(t.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6 || mo < 1 || mo > 12) return 0;
    y -= mo <= 2;  // (days from the civil date: H. Hinnant's algorithm)
    const long long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const long long doy = (153LL * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + s;
}

#ifndef STARDRAFT_COMMIT_TIME
#define STARDRAFT_COMMIT_TIME 0  // (no git: unknown)
#endif
// Whether the release is newer than this build. A release build (a version of numbers) compares the numbers. Another
// build (a CI build of a commit, a build made here) is older than the release when its commit is older than the
// release's publishing: a build of a later commit is never offered the older release (its Install would go back).
inline bool newerThan(const Release& r, const std::string& version, const std::string& commit, long long commitTime) {
    if (r.commit.empty() || r.commit == commit) return false;
    const std::vector<int> mine = versionNumbers(version), theirs = versionNumbers(r.tag);
    if (!mine.empty() && !theirs.empty()) return std::lexicographical_compare(mine.begin(), mine.end(), theirs.begin(), theirs.end());
    const long long published = isoSeconds(r.published);
    if (commitTime > 0 && published > 0) return published > commitTime;
    return true;
}
inline bool newer(const Release& r) { return newerThan(r, STARDRAFT_VERSION_NAME, STARDRAFT_COMMIT, (long long)STARDRAFT_COMMIT_TIME); }

// --- The release notes (the release notes window, views_changelog.inc) ------------------------------------------------

// GitHub's Accept type that gives a release's notes as HTML too (body_html).
inline const char* const kReleaseHtml = "application/vnd.github.html+json";

// The newest release of a track: the stable one (GitHub's latest release, the default), or with previews also the
// newest pre-release. `json`: GitHub's answer for that release (for the cache). False when GitHub cannot be reached.
inline bool newest(Release& r, bool previews, std::string* json = nullptr) {
    std::string j;
    const std::string repo = "https://api.github.com/repos/" + narrow(kIssueRepo);
    if (!previews) {
        if (!get(repo + "/releases/latest", &j, nullptr, kReleaseHtml) || !parseRelease(j, r)) return false;
        if (json) *json = j;
        return true;
    }
    if (!get(repo + "/releases?per_page=10", &j, nullptr, kReleaseHtml)) return false;
    for (const std::string& o : jsonObjects(j)) {  // (newest first; a draft is not a release)
        if (compactJson(o).find("\"draft\":true") != std::string::npos || !parseRelease(o, r)) continue;
        if (json) *json = o;
        return true;
    }
    return false;
}

// The release with the tag `tag` (v0.6.0). False when GitHub cannot be reached or has no such release.
inline bool byTag(const std::string& tag, Release& r, std::string* json = nullptr) {
    std::string j;
    if (tag.empty() || !get("https://api.github.com/repos/" + narrow(kIssueRepo) + "/releases/tags/" + tag, &j, nullptr, kReleaseHtml) || !parseRelease(j, r))
        return false;
    if (json) *json = j;
    return true;
}

// The tag of this build's release: v and the version ("" for a build that is not a release's: its version is not only
// numbers).
inline std::string ownTag(const std::string& version = STARDRAFT_VERSION_NAME) {
    if (versionNumbers(version).empty()) return {};
    return version[0] == 'v' ? version : "v" + version;
}

// The tag of a release page's address ("" when `url` is not a release page of this program's repository): a link to
// another release opens its notes in the release notes window.
inline std::string releasePageTag(const std::string& url) {
    const std::string prefix = "https://github.com/" + narrow(kIssueRepo) + "/releases/tag/";
    if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) return {};
    std::string tag = url.substr(prefix.size());
    if (const size_t cut = tag.find_first_of("?#/"); cut != std::string::npos) tag.erase(cut);
    return tag;
}

// The release notes that StarDraft 2 keeps (the cache folder's releases/): GitHub's answer for each release by its tag,
// and the newest release of each track, for when GitHub cannot be reached. `dir` empty: none (a test run).
inline std::string keptName(std::string s) {
    for (char& ch : s)
        if (!std::isalnum((unsigned char)ch) && ch != '.' && ch != '-' && ch != '_') ch = '_';
    return s;
}
inline void keepRelease(const fs::path& dir, const std::string& name, const std::string& json) {
    if (dir.empty() || json.empty() || name.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path f = dir / (keptName(name) + ".json"), part = dir / (keptName(name) + ".json.part");
    {
        std::ofstream out(part, std::ios::binary);
        if (!out) return;
        out.write(json.data(), std::streamsize(json.size()));
        if (!out) return;
    }
    fs::rename(part, f, ec);
}
inline bool keptRelease(const fs::path& dir, const std::string& name, Release& r) {
    if (dir.empty() || name.empty()) return false;
    std::ifstream in(dir / (keptName(name) + ".json"), std::ios::binary);
    if (!in) return false;
    const std::string j((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseRelease(j, r);
}
// The cache's name of a track's newest release.
inline std::string newestName(bool previews) { return previews ? "newest-previews" : "newest-stable"; }

// The newest release when it is newer than this build (the update check finds it at start and each hour); the top
// bar's Update button shows it. Set on the UI thread.
inline std::optional<Release>& available() {
    static std::optional<Release> r;
    return r;
}

// A release build: the one CI makes (the others are built here, by a developer).
inline bool releaseBuild() { return !issueToken().empty(); }

// The program's file name in a release's download: stardraft.exe on Windows, stardraft on the other systems. On Windows
// it is the SDL program; an installed Win32 stardraft.exe (a release before the switch) updates to it in the same way
// (the zip holds stardraft.exe only, which has its fonts and icons in it: shell_sdl_win.inc, embeddedFile).
inline std::wstring releaseProgramName() {
#ifdef _WIN32
    return L"stardraft.exe";
#else
    return L"stardraft";
#endif
}

// Whether this program can put the release in its own place: a release build whose file has the release program's
// name (not stardraft-testing.exe), and the release has
// this system's download. Else the user gets the release page.
// (Windows only for now: the macOS and Linux downloads hold an app folder, not one program file.)
inline bool installable(const fs::path& self, const Release& r) {
#ifndef _WIN32
    (void)self, (void)r;
    return false;
#endif
    std::wstring a = self.filename().wstring(), b = releaseProgramName();
    for (auto* s : {&a, &b})
        for (wchar_t& ch : *s) ch = wchar_t(std::towlower(ch));  // (Windows: the file name's case does not matter)
    return releaseBuild() && a == b && !r.zip.empty();
}

// How this program is packaged: by itself (stardraft.exe, the macOS app), as an AppImage, or as a Flatpak.
enum class Packaged { None, AppImage, Flatpak };

// What Check for updates does with the newest release: UpToDate (nothing), Install (this program takes the download's
// program: a release build on Windows), Download (the browser gets this system's download: macOS's disk image, as the
// app cannot put a new app in its place; a new AppImage, that the user puts in place of the old one), Package (a
// Flatpak: its package manager gets the new release), or Page (a build made here, or a release with no download for
// this system: the release page).
enum class UpdateWay { UpToDate, Install, Download, Package, Page };
inline UpdateWay updateWay(const fs::path& self, const Release& r, Packaged packaged) {
    if (!newer(r)) return UpdateWay::UpToDate;
    if (packaged == Packaged::AppImage) return r.download.empty() ? UpdateWay::Page : UpdateWay::Download;
    if (packaged == Packaged::Flatpak) return UpdateWay::Package;
    if (installable(self, r)) return UpdateWay::Install;
#ifdef _WIN32
    return UpdateWay::Page;  // (a file with another name, as stardraft-testing.exe: the release page, as before)
#else
    return releaseBuild() && !r.download.empty() ? UpdateWay::Download : UpdateWay::Page;
#endif
}

// Unpacks a release's archive into `dir` (made if it is not there): the new program, or empty with the reason in `why`.
inline fs::path unpackRelease(const fs::path& archive, const fs::path& dir, std::wstring& why) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path fresh = dir / releaseProgramName();
    if (!stardraft::platform::unpackArchive(archive, dir) || !fs::exists(fresh, ec)) return why = L"Could not unpack the download.", fs::path();
    return fresh;
}

// Downloads the release's archive into `dir` (made empty first) and unpacks it there: the new program, or empty with
// the reason in `why`.
inline fs::path downloadRelease(const Release& r, const fs::path& dir, std::wstring& why) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path zip = dir / "update.zip";
    if (!get(r.zip, nullptr, &zip, "application/octet-stream")) return why = L"The download did not complete.", fs::path();
    return unpackRelease(zip, dir, why);
}

// Puts the new program in place of `self`: empty when it is done, else the reason.
inline std::wstring placeRelease(const fs::path& self, const fs::path& fresh) {
    using stardraft::platform::Replace;
    switch (stardraft::platform::replaceProgram(self, fresh)) {
    case Replace::OldNotMoved: return L"Could not replace the program file in " + self.parent_path().wstring() + L".";
    case Replace::NewNotPlaced: return L"Could not put the new program in " + self.parent_path().wstring() + L".";
    case Replace::Broken:
        return tr(L"Could not put the new program in ") + self.parent_path().wstring() + tr(L". The old program is ") +
               stardraft::platform::oldProgramFile(self).wstring() + L".";
    case Replace::Done: break;
    }
    return {};
}

// Puts the new program in place of `self` and starts it (`start`): empty when the new program runs (this one then
// ends), else the reason. A new program that does not start (a damaged download) goes, and the old program is in its
// place again: this one goes on.
inline std::wstring installRelease(const fs::path& self, const fs::path& fresh, const std::function<bool(const fs::path&)>& start) {
    if (const std::wstring failed = placeRelease(self, fresh); !failed.empty()) return failed;
    if (start(self)) return {};
    if (stardraft::platform::restoreProgram(self)) return L"The new program did not start. StarDraft 2 did not change.";
    return tr(L"The new program did not start. The old program is ") + stardraft::platform::oldProgramFile(self).wstring() + L".";
}

// The arguments of the new program after an update: it waits until this program ended (--restart, as Restart now
// does), then the old program's file goes (removeOldProgram).
inline std::vector<std::string> restartArguments(std::uint32_t pid) { return {"--restart", std::to_string(pid)}; }

// At start, once the program works here (its graphics device is made): the program file of an earlier update
// (stardraft.exe.old) goes, and a new program that an update gave back (stardraft.exe.failed); true when there is no
// old one now.
inline bool removeOldProgram(const fs::path& self) {
    std::error_code ec;
    const fs::path old = stardraft::platform::oldProgramFile(self);
    fs::remove(old, ec);
    fs::remove(stardraft::platform::failedProgramFile(self), ec);
    return !fs::exists(old, ec);
}

// The new program of an update that cannot make its graphics device on this computer (no Direct3D 12 or Vulkan): the
// program before the update goes back in its place and starts with the same options. Empty when there was no update
// to give back; else what the user is told.
inline std::wstring rollBackUpdate(const fs::path& self, const std::vector<std::string>& args, const std::string& why) {
    std::error_code ec;
    if (self.empty() || !fs::exists(stardraft::platform::oldProgramFile(self), ec) || !stardraft::platform::rollBackUpdate(self)) return {};
    const bool started = stardraft::platform::startProgram(self, args);
    return L"StarDraft 2 could not start the new release on this computer: it could not use the video card (" + widen(why) +
           L").\n\nThe release before the update is in its place again" + (started ? L" and starts now." : L". Start StarDraft 2 again.");
}

// Program options > Updates > Look for updates: the status bar's text after a change.
inline std::wstring updateCheckMessage(bool on) {
    return on ? L"StarDraft 2 looks for a newer release at start and each hour" : L"StarDraft 2 does not look for newer releases";
}

// --- Crash reports ---------------------------------------------------------------------------------------------------

// Program options > Crash reports (0 ask, 1 send, 2 do not send): the status bar's text after a change.
inline std::wstring crashModeMessage(int mode) {
    return mode == 1 ? L"StarDraft 2 sends an anonymous report when it crashes"
           : mode == 2 ? L"StarDraft 2 does not send crash reports (a report stays on this computer)"
                       : L"After a crash, StarDraft 2 asks at the next start if it can send the report";
}

// The report's first lines: the program, its build and commit, the system and the video card.
inline std::string crashHeader(const std::string& build, const std::wstring& gpu) {
    return "StarDraft " STARDRAFT_VERSION_NAME ", " + build + ", commit " STARDRAFT_COMMIT "\n" + stardraft::platform::systemName() + ", video card: " +
           narrow(gpu) + "\n";
}

// What the crashed program says: `mode` is Options > Crash reports (0 ask at the next start, 1 send, 2 do not send).
inline std::wstring crashStoppedText(const fs::path& dir, int mode) {
    if (mode == 1) return L"StarDraft 2 stopped because of an error. StarDraft 2 sends a crash report to the developers so that they can repair the error.";
    return L"StarDraft 2 stopped because of an error. The crash report is in " + dir.wstring() +
           (mode == 2 ? L" (crash reports are off)." : L". At the next start, StarDraft 2 asks if you want to send the crash report.");
}

// The question at the next start (Options > Crash reports: Ask), about the reports in `folder`.
inline std::wstring crashAskText(const fs::path& folder) {
    return L"StarDraft 2 stopped because of an error the last time. Send the crash report to the developers, so that they can "
           L"repair the error?\n\nThe report has the version, the operating system, the video card, where it crashed and the last "
           L"log lines. The log lines can have the names of maps and folders (your user folder shows as ~). The report is public "
           L"on GitHub. It has no maps. It is in " + folder.wstring() + L".";
}

// Sends one report to GitHub; true when it arrived (apps/common/crash_report.cpp). `claimed`: the crashed program took
// the folder for this sender (claimCrashReport), so that the next start does not send it too.
inline bool sendCrash(const fs::path& dir, bool claimed = false) {
    return stardraft::platform::sendCrashReport(dir, narrow(kIssueRepo), issueToken(), claimed);
}

// At start, on a thread, with Options > Crash reports: Send (mode 1): the reports in `folder` that did not go yet (no
// network then). Reports older than 30 days go away.
inline void sendPendingCrashes(const fs::path& folder, int mode) {
    if (issueToken().empty() || mode != 1 || folder.empty()) return;
    std::thread([folder] {
        std::error_code ec;
        const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
        for (const auto& e : fs::directory_iterator(folder, ec)) {
            if (fs::last_write_time(e.path(), ec) < old) fs::remove_all(e.path(), ec);
            else if (fs::exists(e.path() / "report.txt", ec) && !fs::exists(e.path() / "sent", ec)) sendCrash(e.path());
        }
    }).detach();
}

}  // namespace releases
