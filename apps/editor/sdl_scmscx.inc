// File > Import from SCMSCX: a window that searches the public map archive scmscx.com and downloads a map from it.
// The search box finds maps as the user types (250 ms after the last key); each row shows the map's minimap, its
// scenario name (without its color codes), its file name, the file's last change and its upload time, and (when the
// map's information comes) its size, tileset, human players and file size. Download (or Enter, or a double click)
// writes the map into the user's Downloads folder (never over a file: "name (2).scx") and opens it as File > Open
// does. Errors (no network, an HTTP error, a file that is not a map) are a line in the window.
//
// The requests (apps/common/http.h) run on threads, never on the UI thread: a search on a thread of its own, the
// minimaps and the map information on a few worker threads (the rows that show, the newest request first). Their
// answers come back with shellPost. The window keeps the answers for the session: a search typed again, or a
// minimap seen again, needs no request. An answer to an older search is kept but not shown.
//
// The site's API (github.com/scmscx/scmscx.com, crates/bwmapserver/src/server.rs):
//   /api/uiv2/search/{query}?offset=N  {"total_results": n, "maps": [{id, scenario_name, filename, last_modified,
//                                       uploaded_time}]}, 300 rows from offset N (one row per map and file name)
//   /api/uiv2/search?sort=timeuploadednew  the same without a query: the newest maps
//   /api/uiv2/minimap/{id}             the minimap (PNG)
//   /api/uiv2/map_info/{id}            {"meta": {mpq_hash, mpq_size, ...}, "properties": {width, height, tileset},
//                                       "player_owners": [...], ...}
//   /api/maps/{mpq_hash}               the map file
// Times are seconds since 1970 (UTC).
//
// Tests: STARDRAFT_SCMSCX_DOWNLOADS sets the folder for the downloads (a test never writes into the real Downloads
// folder); --dialog scmscx:<query>[|<row>] (main_sdl.cpp) searches, waits for the rows and their minimaps, then
// downloads that row and opens the map, and prints the times.

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <ctime>
#include <cwchar>
#include <deque>
#include <unordered_map>
#ifdef _WIN32
#include <shlobj.h>  // SHGetKnownFolderPath: the Downloads folder
#endif

namespace scmscx {

namespace kit = stardraft::kit;

// The site. A test can give another (STARDRAFT_SCMSCX_SITE, an https address): one that does not answer shows the
// error line.
inline const std::string& site() {
    static const std::string& s = *new std::string([] {  // (never freed: the detached threads read it while the program ends)
        std::string e;
#ifdef _WIN32
        char* v = nullptr;
        size_t n = 0;
        if (_dupenv_s(&v, &n, "STARDRAFT_SCMSCX_SITE") == 0 && v) e = v;
        std::free(v);
#else
        if (const char* v = std::getenv("STARDRAFT_SCMSCX_SITE")) e = v;
#endif
        return e.rfind("https://", 0) == 0 ? e : std::string("https://scmscx.com");
    }());
    return s;
}
constexpr int kDebounceMs = 250;  // the search starts this long after the last key
constexpr int kThreads = 4;       // the worker threads for the minimaps and the map information
constexpr size_t kMaxWaiting = 40;  // the jobs that wait at most (the oldest go: rows that the user scrolled past)

bool gVerbose = false;  // (--dialog scmscx: the times go to the output)

// The test's folder for the downloads (STARDRAFT_SCMSCX_DOWNLOADS); empty when it is not set.
inline fs::path testFolder() {
#ifdef _WIN32
    wchar_t* e = nullptr;
    size_t n = 0;
    fs::path p;
    if (_wdupenv_s(&e, &n, L"STARDRAFT_SCMSCX_DOWNLOADS") == 0 && e && *e) p = fs::path(e);
    std::free(e);
    return p;
#else
    const char* e = std::getenv("STARDRAFT_SCMSCX_DOWNLOADS");
    return e && *e ? fs::path(e) : fs::path();
#endif
}
inline bool testFolderSet() { return !testFolder().empty(); }

// The folder for the downloads: the test's, else the system's Downloads folder.
inline fs::path downloadsFolder() {
    if (fs::path t = testFolder(); !t.empty()) return t;
#ifdef _WIN32
    PWSTR p = nullptr;
    fs::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p)) && p) folder = fs::path(p);
    if (p) CoTaskMemFree(p);
    if (!folder.empty()) return folder;
#endif
    return homeFolder() / "Downloads";
}

// The program's name and version, no personal data. A test's request has "norecord": the site does not count it as a
// download.
inline std::string userAgent() {
    std::string a = "StarDraft/" STARDRAFT_VERSION_NAME;
    static const bool test = testFolderSet();
    if (test) a += " norecord";
    return a;
}

struct Reply {
    int status = 0;  // 0: no answer (no network)
    bool ok = false;
    std::string body;
};
inline Reply get(const std::string& url, const char* accept) {
    stardraft::platform::HttpRequest q;
    q.url = url, q.accept = accept, q.userAgent = userAgent(), q.keepAlive = true;  // (one TLS handshake for the session)
    Reply r;
    const stardraft::platform::HttpReply h = stardraft::platform::httpRequest(q, &r.body);
    r.status = h.status, r.ok = h.status == 200 && h.complete;
    return r;
}

inline std::wstring errorText(int status) {
    if (status == 0) return tr(L"Cannot connect to scmscx.com. Check the internet connection.");
    return tr(L"scmscx.com sent an error (HTTP ") + std::to_wstring(status) + L").";
}

// The UTF-8 bytes of `s` in a URL path or query (all but the unreserved letters as %XX).
inline std::string urlPart(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (const unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += char(ch);
        else o += '%', o += hex[ch >> 4], o += hex[ch & 15];
    }
    return o;
}
inline std::string searchUrl(const std::string& query, size_t offset) {
    std::string u = site() + (query.empty() ? "/api/uiv2/search?sort=timeuploadednew" : "/api/uiv2/search/" + urlPart(query) + "?sort=relevancy");
    return u + "&offset=" + std::to_string(offset);
}

// A number value after `"key":` (from `from` on); `none` when there is none (or null).
inline int64_t jsonNumber(const std::string& j, const std::string& key, int64_t none = -1, size_t from = 0) {
    const std::string k = "\"" + key + "\":";
    const size_t a = j.find(k, from);
    if (a == std::string::npos) return none;
    size_t i = a + k.size();
    const bool neg = i < j.size() && j[i] == '-';
    if (neg) ++i;
    if (i >= j.size() || !std::isdigit(static_cast<unsigned char>(j[i]))) return none;
    int64_t v = 0;
    for (; i < j.size() && std::isdigit(static_cast<unsigned char>(j[i])); ++i) v = v * 10 + (j[i] - '0');
    return neg ? -v : v;
}

// A game text as the window shows it: the color and line codes (the bytes under 0x20) left out.
inline std::wstring plainName(const std::string& utf8) {
    std::string t;
    for (const char ch : utf8)
        if (static_cast<unsigned char>(ch) >= 0x20) t += ch;
        else if (ch == '\n' || ch == '\t') t += ' ';
    std::wstring w = widen(t);
    while (!w.empty() && w.back() == L' ') w.pop_back();
    while (!w.empty() && w.front() == L' ') w.erase(w.begin());
    return w;
}

// A search result: a map under one of its file names.
struct Row {
    std::string id;
    std::wstring scenario, file;
    int64_t modified = -1, uploaded = -1;  // seconds since 1970; -1: not known
};
struct Page {
    int status = 0;
    bool ok = false;
    int64_t total = 0;
    std::vector<Row> rows;
};
inline Page parsePage(const Reply& r) {
    Page p;
    p.status = r.status, p.ok = r.ok;
    if (!r.ok) return p;
    const std::string j = releases::compactJson(r.body);
    const size_t maps = j.find("\"maps\":[");
    if (maps == std::string::npos) return p.ok = false, p;
    p.total = std::max<int64_t>(0, jsonNumber(j, "total_results", 0));
    for (const std::string& o : releases::jsonObjects(j.substr(maps + 7))) {
        Row row;
        row.id = releases::jsonValue(o, "id");
        if (row.id.empty()) continue;
        row.scenario = plainName(releases::jsonValue(o, "scenario_name"));
        row.file = plainName(releases::jsonValue(o, "filename"));
        row.modified = jsonNumber(o, "last_modified");
        row.uploaded = jsonNumber(o, "uploaded_time");
        p.rows.push_back(std::move(row));
    }
    return p;
}

// A map's information (/api/uiv2/map_info).
struct Info {
    std::string mpqHash;  // the map file's name on the site; "" while the site still reads the map
    int64_t mpqSize = 0;
    int width = 0, height = 0, tileset = -1, humans = 0;
};
inline bool parseInfo(const std::string& body, Info& info) {
    const std::string j = releases::compactJson(body);
    if (j.find("\"meta\":") == std::string::npos) return false;
    info.mpqHash = releases::jsonValue(j, "mpq_hash");
    info.mpqSize = std::max<int64_t>(0, jsonNumber(j, "mpq_size", 0));
    if (const size_t props = j.find("\"properties\":"); props != std::string::npos) {
        info.width = int(jsonNumber(j, "width", 0, props));
        info.height = int(jsonNumber(j, "height", 0, props));
        info.tileset = int(jsonNumber(j, "tileset", -1, props));
    }
    if (const size_t owners = j.find("\"player_owners\":["); owners != std::string::npos) {  // the 8 players' owners: 6 is human
        size_t i = owners + 17;
        for (int k = 0; k < 8 && i < j.size() && j[i] != ']'; ++k) {
            const int v = std::atoi(j.c_str() + i);
            if (v == 6) ++info.humans;
            const size_t comma = j.find_first_of(",]", i);
            if (comma == std::string::npos || j[comma] == ']') break;
            i = comma + 1;
        }
    }
    return true;
}

// The time in the local time zone, short ("2026-11-21 14:05"); "" when not known.
inline std::wstring shortTime(int64_t seconds) {
    if (seconds <= 0) return {};
    const std::time_t tt = std::time_t(seconds);
    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &tt) != 0) return {};
#else
    if (!localtime_r(&tt, &tm)) return {};
#endif
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

inline std::wstring sizeText(int64_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" B";
    if (bytes < 1024 * 1024) return std::to_wstring((bytes + 512) / 1024) + L" KB";
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buf;
}

inline const wchar_t* tilesetName(int t) {
    static const wchar_t* const names[] = {L"Badlands", L"Space platform", L"Installation", L"Ashworld", L"Jungle", L"Desert", L"Ice", L"Twilight"};
    return t >= 0 ? names[t % 8] : L"";
}

// A file name that the system takes: no path letters or control codes, a map's extension.
inline std::wstring safeFileName(const Row& row) {
    std::wstring n = row.file.empty() ? row.scenario : row.file;
    for (wchar_t& ch : n)
        if (ch < 0x20 || std::wcschr(L"<>:\"/\\|?*", ch)) ch = L'_';
    while (!n.empty() && (n.back() == L' ' || n.back() == L'.')) n.pop_back();
    while (!n.empty() && n.front() == L' ') n.erase(n.begin());
    if (n.size() > 150) n.resize(150);
    if (n.empty()) n = L"map";
    std::wstring low = n;
    std::transform(low.begin(), low.end(), low.begin(), ::towlower);
    const bool mapExt = low.size() > 4 && (low.compare(low.size() - 4, 4, L".scx") == 0 || low.compare(low.size() - 4, 4, L".scm") == 0);
    if (!mapExt) n += L".scx";
    // (Windows' device names: CON.scx is not a file there)
    std::wstring stem = low.substr(0, low.find(L'.'));
    static const wchar_t* const devices[] = {L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3", L"com4", L"lpt1", L"lpt2", L"lpt3"};
    for (const wchar_t* d : devices)
        if (stem == d) n = L"_" + n;
    return n;
}

// `name` in `folder`, or "name (2).ext", "name (3).ext"... when that file is there: never an existing file.
inline fs::path freePath(const fs::path& folder, const std::wstring& name) {
    std::error_code ec;
    fs::path p = folder / name;
    if (!fs::exists(p, ec)) return p;
    const fs::path f(name);
    const std::wstring stem = f.stem().wstring(), ext = f.extension().wstring();
    for (int k = 2; k < 10000; ++k) {
        p = folder / (stem + L" (" + std::to_wstring(k) + L")" + ext);
        if (!fs::exists(p, ec)) return p;
    }
    return folder / (stem + L" (" + std::to_wstring(std::time(nullptr)) + L")" + ext);
}

// A StarCraft map file is an MPQ archive: its header ("MPQ\x1A", or the user data's "MPQ\x1B") is at a multiple of
// 512 bytes.
inline bool looksLikeMap(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    char head[4];
    for (uint64_t at = 0; at < (uint64_t(64) << 20); at += 512) {
        in.seekg(std::streamoff(at));
        if (!in.read(head, 4)) return false;
        if (head[0] == 'M' && head[1] == 'P' && head[2] == 'Q' && (head[3] == '\x1A' || head[3] == '\x1B')) return true;
    }
    return false;
}

// The worker threads of the minimaps and the map information: the newest job first; the oldest waiting ones go when
// too many wait (their rows are out of view). Never freed: its threads live as long as the program.
class Pool {
public:
    // Adds `job` (`key`: its name); returns the keys of the jobs that went.
    std::vector<std::string> push(std::string key, std::function<void()> job) {
        std::vector<std::string> dropped;
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.emplace_front(std::move(key), std::move(job));
            while (q_.size() > kMaxWaiting) dropped.push_back(std::move(q_.back().first)), q_.pop_back();
            if (threads_ < kThreads) {
                ++threads_;
                std::thread([this] { run(); }).detach();
            }
        }
        cv_.notify_one();
        return dropped;
    }
    // The waiting jobs go (a new search); returns their keys.
    std::vector<std::string> clear() {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> keys;
        for (auto& j : q_) keys.push_back(std::move(j.first));
        q_.clear();
        return keys;
    }

private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return !q_.empty(); });
                job = std::move(q_.front().second);
                q_.pop_front();
            }
            job();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::pair<std::string, std::function<void()>>> q_;
    int threads_ = 0;
};
inline Pool& pool() {
    static Pool* p = new Pool;
    return *p;
}

inline double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// A download: the UI thread starts it; its thread sets the counts and reads `cancel`.
struct Download {
    std::atomic<bool> cancel{false};
    std::atomic<uint64_t> done{0}, total{0};
    std::chrono::steady_clock::time_point posted{};  // (its thread: the last progress it posted)
};

}  // namespace scmscx

class ScmscxView;
ScmscxView& scmscxView();

class ScmscxView final : public draw::View {
public:
    ScmscxView() {
        field_.setPlaceholder(tr(L"Search maps on scmscx.com"));
        field_.setMaxLength(200);
        field_.onChange = [this] { queryChanged(); };
        field_.onEnter = [this] {  // Enter: to the list (its first row), where Enter downloads
            if (rows_.empty()) return;
            if (list_.selection().empty()) list_.selectOnly(0);
            list_.focus();
            buttonsChanged();
        };
        add(field_, {0, 0, 0, 0});
        list_.setRowHeight(kRowH);
        list_.setEmptyHint(L"");
        list_.paintRow = [this](draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) { paintRow(c, i, r, s); };
        list_.onActivate = [this](int i) { download(i); };
        list_.onSelect = [this] { buttonsChanged(); };
        add(list_, {0, 0, 0, 0});
        download_.setText(tr(L"Download and open"));
        download_.setTone(stardraft::kit::Tone::Primary);
        download_.onClick = [this] {
            const auto sel = list_.selection();
            if (!sel.empty()) download(sel[0]);
        };
        cancel_.setText(tr(L"Cancel"));
        cancel_.onClick = [this] { cancelDownload(); };
        close_.setText(tr(L"Close"));
        close_.onClick = [this] {
            if (onClose) onClose();
        };
        add(download_, {0, 0, 0, 0}), add(cancel_, {0, 0, 0, 0}), add(close_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
        bar_.setVisible(false), cancel_.setVisible(false);
        buttonsChanged();
    }
    std::vector<draw::View*> tabChildren() const override {
        auto* self = const_cast<ScmscxView*>(this);
        return {&self->field_, &self->list_, &self->download_, &self->cancel_, &self->close_};
    }

    // The host's part: close the window; open the downloaded map.
    std::function<void()> onClose;
    std::function<void(const fs::path&)> onOpen;

    // The window opens: the search box has the keyboard; the first search (no query: the newest maps) starts.
    void opened() {
        focused_ = false;
        if (!searchedOnce_) {
            searchedOnce_ = true;
            search();
        }
        invalidate();
    }

    // Tests: the text typed in the search box (its change, as a key's), the state.
    void typeForTest(const std::wstring& t) {
        field_.setText(t);
        queryChanged();
    }
    void downloadForTest(int i) {
        list_.selectOnly(i);
        download(i);
    }
    bool searching() const { return searching_ || debounceTimer_ >= 0; }
    bool downloading() const { return dl_ != nullptr; }
    size_t rowCount() const { return rows_.size(); }
    int64_t total() const { return total_; }
    const std::wstring& status() const { return status_; }
    const std::wstring& searchStatus() const { return searchStatus_; }
    // The rows that show now (their first and last), and whether their minimaps have come (or failed).
    std::pair<int, int> visibleRows() const {
        if (rows_.empty() || list_.height() <= 0) return {0, -1};
        const int first = std::max(0, list_.rowAt({scale(4), 1}));
        int last = list_.rowAt({scale(4), list_.height() - 2});
        if (last < 0) last = int(rows_.size()) - 1;
        return {first, last};
    }
    bool visiblePreviewsDone() const {
        const auto [first, last] = visibleRows();
        for (int i = first; i <= last && i < int(rows_.size()); ++i) {
            const auto p = previews_.find(rows_[size_t(i)].id);
            if (p == previews_.end() || p->second.state < 2) return false;
        }
        return true;
    }
    // Asks for the minimaps and the information of the rows that show (each paint does it; a test, without one).
    void requestVisible() {
        const auto [first, last] = visibleRows();
        for (int i = last; i >= first; --i)  // (the last pushed runs first: the top rows)
            if (i < int(rows_.size())) want(rows_[size_t(i)].id);
        if (last >= int(rows_.size()) - 40) loadMore();
    }

    void resized() override { layout(); }
    void paint(draw::Canvas& c) override;
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape) {
            if (dl_) cancelDownload();
            else if (onClose) onClose();
            return true;
        }
        return false;
    }

    // --- Answers of the threads (on the UI thread, by shellPost) ----------------------------------------------------
    void gotPage(const std::string& url, std::string query, size_t offset, unsigned serial, scmscx::Page page) {
        if (page.ok) pages_[url] = page;
        if (offset > 0) {  // more rows of the search that shows
            if (query == query_ && offset == rows_.size()) {
                loadingMore_ = false;
                if (page.ok) append(page);
                else searchStatus_ = scmscx::errorText(page.status), searchBad_ = true, moreFailed_ = true;  // (no new try until the next search)
                invalidate();
            }
            return;
        }
        if (serial != serial_) return;  // an older search: kept, not shown
        searching_ = false;
        show(query, page);
        if (scmscx::gVerbose)
            std::printf("scmscx: \"%s\": %zu rows of %lld, %.0f ms after the last key (the request %.0f ms)\n", query.c_str(), rows_.size(), (long long)total_,
                        scmscx::msSince(typedAt_), scmscx::msSince(requestAt_));
    }
    void gotPreview(const std::string& id, bool ok, int w, int h, std::vector<uint8_t> rgba) {
        Preview& p = previews_[id];
        p.state = ok ? 2 : 3;
        if (ok) {
            p.bmp.resize(w, h);
            std::memcpy(p.bmp.pixels(), rgba.data(), size_t(w) * size_t(h) * 4);
            p.bmp.opaque = true;
            p.bmp.changed();
        }
        list_.invalidate();
    }
    void gotInfo(const std::string& id, bool ok, scmscx::Info info) {
        InfoEntry& e = infos_[id];
        e.state = ok ? 2 : 3;
        e.info = std::move(info);
        list_.invalidate();
    }
    void gotProgress(const std::shared_ptr<scmscx::Download>& job) {
        if (job != dl_) return;
        const uint64_t done = job->done, total = job->total;
        if (total > 0) bar_.setBusy(false), bar_.setValue(float(double(done) / double(total)));
        status_ = tr(L"Downloading\x2026 ") + scmscx::sizeText(int64_t(done)) + (total > 0 ? tr(L" of ") + scmscx::sizeText(int64_t(total)) : L"");
        statusBad_ = false;
        invalidate();
    }
    void gotDownload(const std::shared_ptr<scmscx::Download>& job, const fs::path& path, const std::wstring& error) {
        if (job != dl_) return;
        dl_.reset();
        bar_.setBusy(false), bar_.setVisible(false);
        if (scmscx::gVerbose)
            std::printf("scmscx: download %s in %.0f ms: %s\n", error.empty() ? "done" : "failed", scmscx::msSince(downloadAt_),
                        narrow(error.empty() ? path.wstring() : error).c_str());
        if (!error.empty()) {
            status_ = error, statusBad_ = true;
        } else {
            status_ = tr(L"Downloaded to ") + path.wstring(), statusBad_ = false;
        }
        layout(), buttonsChanged(), invalidate();
        if (error.empty() && onOpen) onOpen(path);
    }

private:
    static constexpr int kRowH = 76;  // a row at 100 %: the 64 px minimap and its margins

    struct Preview {
        int state = 0;  // 0 not asked, 1 asked, 2 here, 3 failed
        draw::Bitmap bmp;
    };
    struct InfoEntry {
        int state = 0;  // as Preview's
        scmscx::Info info;
    };

    void layout() {
        const int W = width(), H = height();
        if (W <= 0 || H <= 0) return;
        const int pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH), icon = scale(24);
        field_.setFrame({pad + icon, pad, W - pad, pad + bh});
        const int listTop = pad + bh + scale(28);
        const int bottomY = H - pad - bh;
        list_.setFrame({pad, listTop, W - pad, bottomY - scale(12)});
        // The buttons at the right edge: Close last; Download (or Cancel while a map downloads) to its left.
        const int cw = scale(88), dw = scale(156), gap = scale(stardraft::kit::kButtonGap);
        close_.setFrame({W - pad - cw, bottomY, W - pad, bottomY + bh});
        const draw::Rect left{W - pad - cw - gap - dw, bottomY, W - pad - cw - gap, bottomY + bh};
        download_.setFrame(left), cancel_.setFrame(left);
        download_.setVisible(dl_ == nullptr), cancel_.setVisible(dl_ != nullptr);
        bar_.setFrame({pad, bottomY + bh / 2 - scale(3), pad + scale(160), bottomY + bh / 2 + scale(3)});
        bar_.setVisible(dl_ != nullptr);
        statusRect_ = {pad + (dl_ ? scale(172) : 0), bottomY, left.left - gap, bottomY + bh};
        searchRect_ = {pad, pad + bh + scale(4), W - pad, listTop - scale(4)};
    }
    void buttonsChanged() {
        const auto sel = list_.selection();
        download_.setEnabled(!dl_ && !sel.empty() && sel[0] < int(rows_.size()));
    }

    std::string typedQuery() const {
        std::wstring q = field_.text();
        while (!q.empty() && std::iswspace(q.back())) q.pop_back();
        while (!q.empty() && std::iswspace(q.front())) q.erase(q.begin());
        return narrow(q);
    }
    // A key changed the search: it starts when no key comes for 250 ms.
    // A search that this session has the answer to shows at once.
    void queryChanged() {
        typedAt_ = std::chrono::steady_clock::now();
        if (debounceTimer_ >= 0 && gHost) gHost->cancelTimer(debounceTimer_);
        debounceTimer_ = -1;
        if (pages_.count(scmscx::searchUrl(typedQuery(), 0))) return search();
        debounceTimer_ = gHost ? gHost->timer(scmscx::kDebounceMs, [this] {
            debounceTimer_ = -1;
            search();
        })
                               : -1;
        if (debounceTimer_ < 0) search();  // (no host: at once)
    }
    void search() {
        const std::string query = typedQuery();
        ++serial_;  // (an answer to the search before this one is not shown)
        if (query == query_ && searchedRows_) {  // (the same words: the rows stay)
            searching_ = false;
            invalidate();
            return;
        }
        requestAt_ = std::chrono::steady_clock::now();
        for (const std::string& k : scmscx::pool().clear()) forgetJob(k);  // (the old rows' minimaps that wait)
        const std::string url = scmscx::searchUrl(query, 0);
        if (const auto hit = pages_.find(url); hit != pages_.end()) {  // asked before in this session
            searching_ = false;
            show(query, hit->second);
            if (scmscx::gVerbose) std::printf("scmscx: \"%s\": %zu rows (kept), %.0f ms after the last key\n", query.c_str(), rows_.size(), scmscx::msSince(typedAt_));
            return;
        }
        searching_ = true;
        searchStatus_ = tr(L"Searching\x2026"), searchBad_ = false;
        invalidate();
        const unsigned serial = serial_;
        std::thread([url, query, serial] {
            scmscx::Page page = scmscx::parsePage(scmscx::get(url, "application/json"));
            shellPost([url, query, serial, page = std::move(page)]() mutable { scmscxView().gotPage(url, query, 0, serial, std::move(page)); });
        }).detach();
    }
    void show(const std::string& query, const scmscx::Page& page) {
        query_ = query, searchedRows_ = true, loadingMore_ = false, moreFailed_ = false;
        rows_.clear();
        total_ = 0;
        list_.clearSelection();
        if (!page.ok) {
            searchStatus_ = scmscx::errorText(page.status), searchBad_ = true;
            searchedRows_ = false;  // (the same words again: a new request)
        } else {
            append(page);
        }
        list_.setScrollTop(0);
        if (!rows_.empty()) list_.selectOnly(0);
        list_.setEmptyHint(page.ok ? tr(L"No maps match the search.") : L"");
        buttonsChanged();
        invalidate();
    }
    void append(const scmscx::Page& page) {
        rows_.insert(rows_.end(), page.rows.begin(), page.rows.end());
        total_ = std::max<int64_t>(page.total, int64_t(rows_.size()));
        if (page.rows.empty()) total_ = int64_t(rows_.size());  // (no more rows: the end)
        list_.setCount(int(rows_.size()));
        searchBad_ = false;
        if (query_.empty()) searchStatus_ = tr(L"The newest maps on scmscx.com");
        else searchStatus_ = total_ == 1 ? tr(L"1 result") : std::to_wstring(total_) + tr(L" results");
        buttonsChanged();
    }
    // The next 300 rows, when the list comes near the end of the rows that are here.
    void loadMore() {
        if (loadingMore_ || moreFailed_ || searching_ || rows_.empty() || int64_t(rows_.size()) >= total_) return;
        const size_t offset = rows_.size();
        const std::string url = scmscx::searchUrl(query_, offset), query = query_;
        if (const auto hit = pages_.find(url); hit != pages_.end()) {
            append(hit->second);
            return;
        }
        loadingMore_ = true;
        const unsigned serial = serial_;
        std::thread([url, query, offset, serial] {
            scmscx::Page page = scmscx::parsePage(scmscx::get(url, "application/json"));
            shellPost([url, query, offset, serial, page = std::move(page)]() mutable { scmscxView().gotPage(url, query, offset, serial, std::move(page)); });
        }).detach();
    }

    // The minimap and the information of map `id`, when not asked yet.
    void want(const std::string& id) {
        Preview& p = previews_[id];
        if (p.state == 0) {
            p.state = 1;
            for (const std::string& k : scmscx::pool().push("m" + id, [id] {
                     const scmscx::Reply r = scmscx::get(scmscx::site() + "/api/uiv2/minimap/" + id, "image/png");
                     int w = 0, h = 0;
                     std::vector<uint8_t> rgba;
                     const bool ok = r.ok && shellDecodeImage(std::vector<uint8_t>(r.body.begin(), r.body.end()), w, h, rgba) && w > 0 && h > 0;
                     shellPost([id, ok, w, h, rgba = std::move(rgba)]() mutable { scmscxView().gotPreview(id, ok, w, h, std::move(rgba)); });
                 }))
                forgetJob(k);
        }
        InfoEntry& e = infos_[id];
        if (e.state == 0) {
            e.state = 1;
            for (const std::string& k : scmscx::pool().push("i" + id, [id] {
                     const scmscx::Reply r = scmscx::get(scmscx::site() + "/api/uiv2/map_info/" + id, "application/json");
                     scmscx::Info info;
                     const bool ok = r.ok && scmscx::parseInfo(r.body, info);
                     shellPost([id, ok, info = std::move(info)]() mutable { scmscxView().gotInfo(id, ok, std::move(info)); });
                 }))
                forgetJob(k);
        }
    }
    // A job that went before it ran: its row asks again when it shows.
    void forgetJob(const std::string& key) {
        if (key.size() < 2) return;
        const std::string id = key.substr(1);
        if (key[0] == 'm') {
            if (auto p = previews_.find(id); p != previews_.end() && p->second.state == 1) p->second.state = 0;
        } else if (auto e = infos_.find(id); e != infos_.end() && e->second.state == 1) {
            e->second.state = 0;
        }
    }

    void download(int i) {
        if (dl_ || i < 0 || i >= int(rows_.size())) return;
        const scmscx::Row row = rows_[size_t(i)];
        std::optional<scmscx::Info> known;
        if (const auto e = infos_.find(row.id); e != infos_.end() && e->second.state == 2 && !e->second.info.mpqHash.empty()) known = e->second.info;
        auto job = std::make_shared<scmscx::Download>();
        if (known && known->mpqSize > 0) job->total = uint64_t(known->mpqSize);
        dl_ = job;
        downloadAt_ = std::chrono::steady_clock::now();
        status_ = tr(L"Downloading\x2026"), statusBad_ = false;
        bar_.setValue(0), bar_.setBusy(true);
        layout(), buttonsChanged(), invalidate();
        const fs::path folder = scmscx::downloadsFolder();
        const std::wstring name = scmscx::safeFileName(row);
        std::thread([job, row, known, folder, name] {
            auto finish = [job](fs::path path, std::wstring error) {
                shellPost([job, path = std::move(path), error = std::move(error)] { scmscxView().gotDownload(job, path, error); });
            };
            scmscx::Info info;
            if (known) {
                info = *known;
            } else {
                const scmscx::Reply r = scmscx::get(scmscx::site() + "/api/uiv2/map_info/" + row.id, "application/json");
                if (job->cancel) return finish({}, tr(L"Download canceled."));
                if (!r.ok) return finish({}, scmscx::errorText(r.status));
                if (!scmscx::parseInfo(r.body, info)) return finish({}, tr(L"scmscx.com sent an answer that StarDraft cannot read."));
                if (info.mpqSize > 0) job->total = uint64_t(info.mpqSize);
            }
            if (info.mpqHash.empty()) return finish({}, tr(L"scmscx.com does not have this map file ready. Try again later."));
            std::error_code ec;
            fs::create_directories(folder, ec);
            if (!fs::is_directory(folder, ec)) return finish({}, tr(L"Cannot write the map into ") + folder.wstring());
            // The part that comes goes into the temporary folder (a download that the end of the program stops leaves
            // nothing in Downloads), then to its place.
            const fs::path temp = fs::temp_directory_path(ec);
            const fs::path part = scmscx::freePath(ec ? folder : temp, L"stardraft-scmscx-" + std::to_wstring(std::random_device{}()) + L".part");
            stardraft::platform::HttpRequest q;
            q.url = scmscx::site() + "/api/maps/" + scmscx::urlPart(info.mpqHash);
            q.accept = "application/octet-stream", q.userAgent = scmscx::userAgent(), q.keepAlive = true;
            const uint64_t expected = uint64_t(info.mpqSize);
            q.progress = [job, expected](uint64_t done, uint64_t total) {
                job->done = done;
                job->total = total > 0 ? total : expected;
                const auto now = std::chrono::steady_clock::now();
                if (now - job->posted > std::chrono::milliseconds(100)) {
                    job->posted = now;
                    shellPost([job] { scmscxView().gotProgress(job); });
                }
                return !job->cancel;
            };
            const stardraft::platform::HttpReply r = stardraft::platform::httpRequest(q, nullptr, &part);
            auto drop = [&part] {
                std::error_code e2;
                fs::remove(part, e2);
            };
            if (job->cancel) return drop(), finish({}, tr(L"Download canceled."));
            if (r.status != 200 || !r.complete) {
                drop();
                return finish({}, r.status == 200 || r.status == 0 ? (r.status == 0 ? scmscx::errorText(0) : tr(L"The download stopped before the end. Try again."))
                                                                   : scmscx::errorText(r.status));
            }
            if (!scmscx::looksLikeMap(part)) return drop(), finish({}, tr(L"The file from scmscx.com is not a StarCraft map."));
            const fs::path target = scmscx::freePath(folder, name);
            fs::rename(part, target, ec);
            if (ec) {  // (another drive: a copy; never over a file)
                ec.clear();
                fs::copy_file(part, target, fs::copy_options::none, ec);
                drop();
                if (ec) return finish({}, tr(L"Cannot write the map into ") + folder.wstring());
            }
            finish(target, {});
        }).detach();
    }
public:
    // The window closed: a download that runs stops (its map does not open).
    void closed() { cancelDownload(); }

private:
    void cancelDownload() {
        if (!dl_) return;
        dl_->cancel = true;
        status_ = tr(L"Canceling\x2026"), statusBad_ = false;
        invalidate();
    }

    void paintRow(draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s);

    stardraft::kit::TextField field_;
    stardraft::kit::ListView list_;
    stardraft::kit::Button download_, cancel_, close_;
    stardraft::kit::ProgressBar bar_;
    bool focused_ = false, searchedOnce_ = false;

    // The search that shows: its words (UTF-8), its rows, the number of rows the site has for it.
    std::string query_;
    bool searchedRows_ = false;
    std::vector<scmscx::Row> rows_;
    int64_t total_ = 0;
    unsigned serial_ = 0;  // the newest search (an answer to an older one is not shown)
    bool searching_ = false, loadingMore_ = false, moreFailed_ = false;
    int debounceTimer_ = -1;
    std::chrono::steady_clock::time_point typedAt_{}, requestAt_{}, downloadAt_{};
    std::wstring searchStatus_, status_;
    bool searchBad_ = false, statusBad_ = false;
    draw::Rect searchRect_{}, statusRect_{};

    // The session's answers: the searches (by their address), the minimaps and the information (by the map's id).
    std::unordered_map<std::string, scmscx::Page> pages_;
    std::unordered_map<std::string, Preview> previews_;
    std::unordered_map<std::string, InfoEntry> infos_;

    std::shared_ptr<scmscx::Download> dl_;  // the download that runs
};

void ScmscxView::paint(draw::Canvas& c) {
    if (!focused_ && host()) focused_ = true, field_.focus();  // the search box has the keyboard when the window opens
    layout();
    const int W = width(), H = height(), pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH);
    c.fill({0, 0, W, H}, kBg);
    c.icon({pad, pad, pad + scale(20), pad + bh}, int(UiIcon::Search), kDim, scale(16));
    c.text(searchRect_, searchStatus_, searchBad_ ? kBad : kDim, draw::kMiddle | draw::kEllipsis);
    c.text(statusRect_, status_, statusBad_ ? kBad : kDim, draw::kMiddle | draw::kEllipsis);
    requestVisible();
}

void ScmscxView::paintRow(draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) {
    if (i < 0 || i >= int(rows_.size())) return;
    const scmscx::Row& row = rows_[size_t(i)];
    if (s.selected) {
        c.fill(r, kSel);
        c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
    } else if (s.hot) {
        c.fill(r, kRowHot);
    }
    want(row.id);
    // The minimap: a square at the left (its own shape inside it: a map that is wider than high is lower).
    const int m = scale(6), side = r.height() - 2 * m;
    const draw::Rect box{r.left + scale(8), r.top + m, r.left + scale(8) + side, r.top + m + side};
    const auto p = previews_.find(row.id);
    if (p != previews_.end() && p->second.state == 2 && p->second.bmp.width() > 0) {
        draw::Bitmap& b = p->second.bmp;
        const int bw = b.width(), bhh = b.height();
        const double k = std::min(double(side) / bw, double(side) / bhh);
        const int dw = std::max(1, int(bw * k)), dh = std::max(1, int(bhh * k));
        const draw::Rect dst{box.left + (side - dw) / 2, box.top + (side - dh) / 2, box.left + (side - dw) / 2 + dw, box.top + (side - dh) / 2 + dh};
        c.bitmap(b, {0, 0, bw, bhh}, dst, true);
    } else {
        c.fill(box, kPanel);
        c.icon(box, int(UiIcon::Map), kFaint, scale(20));
    }
    c.frame(box, kBorder);
    // The texts: the scenario name, the file name, then the times and the map's information.
    const int x = box.right + scale(12), right = r.right - scale(8);
    const int lh = (side) / 3;
    c.text({x, box.top, right, box.top + lh}, row.scenario.empty() ? tr(L"(no name)") : row.scenario, s.selected ? kAccent : kText, draw::kMiddle | draw::kEllipsis);
    c.text({x, box.top + lh, right, box.top + 2 * lh}, row.file.empty() ? tr(L"(no file name)") : row.file, kDim, draw::kMiddle | draw::kEllipsis);
    std::wstring line;
    auto part = [&line](const std::wstring& t) {
        if (t.empty()) return;
        if (!line.empty()) line += L"  \x00B7  ";
        line += t;
    };
    if (const std::wstring t = scmscx::shortTime(row.modified); !t.empty()) part(tr(L"Changed ") + t);
    if (const std::wstring t = scmscx::shortTime(row.uploaded); !t.empty()) part(tr(L"Uploaded ") + t);
    if (const auto e = infos_.find(row.id); e != infos_.end() && e->second.state == 2) {
        const scmscx::Info& in = e->second.info;
        if (in.width > 0 && in.height > 0) part(std::to_wstring(in.width) + L" x " + std::to_wstring(in.height));
        if (in.tileset >= 0) part(tr(scmscx::tilesetName(in.tileset)));
        if (in.humans > 0) part(in.humans == 1 ? tr(L"1 player") : std::to_wstring(in.humans) + tr(L" players"));
        if (in.mpqSize > 0) part(scmscx::sizeText(in.mpqSize));
    }
    const draw::Font of = c.setFont(c.font(kitpaint::smallFontDesc()));
    c.text({x, box.top + 2 * lh, right, box.bottom}, line, kDim, draw::kMiddle | draw::kEllipsis);
    c.setFont(of);
}

ScmscxView& scmscxView() {
    static ScmscxView* v = new ScmscxView;  // (the session's answers stay while the program runs; posted answers find it)
    return *v;
}

bool gScmscxOpen = false;

// File > Import from SCMSCX: the window (not modal: the editor works while it is open). A downloaded map opens in a
// tab, and the window closes.
void showScmscxSdl() {
    if (!gRoot || !gHost) return;
    ScmscxView& v = scmscxView();
    if (gScmscxOpen) return;
    gScmscxOpen = true;
    v.onClose = [] {
        scmscxView().closed();
        if (gScmscxOpen && gHost) gHost->closeWindow(scmscxView());
    };
    v.onOpen = [](const fs::path& p) {
        if (!gScmscxOpen) return;  // (the window closed while the download ended: the map stays in the folder)
        if (gHost) gHost->closeWindow(scmscxView());
        openAny(p);
    };
    v.opened();
    draw::WindowSpec spec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(780), scale(640)), L"Import from SCMSCX"};
    spec.resizable = true;
    gHost->openWindow(*gRoot, v, spec, [] {
        gScmscxOpen = false;
        scmscxView().closed();
    });
}

// --dialog scmscx:<query>[|<row>] (main_sdl.cpp): the window searches `query` (as typed: the 250 ms wait, then the
// request), waits for the rows and the minimaps of the rows that show, then (with a row) downloads that row into
// STARDRAFT_SCMSCX_DOWNLOADS and opens the map. The times go to the output.
draw::View* scmscxShot(const std::string& spec, int width, int height) {
    ScmscxView& v = scmscxView();
    scmscx::gVerbose = true;
    std::string query = spec;
    int row = -1;
    if (const size_t bar = spec.find('|'); bar != std::string::npos) query = spec.substr(0, bar), row = std::atoi(spec.c_str() + bar + 1);
    v.setFrame({0, 0, width, height});
    v.onOpen = [](const fs::path& p) {
        openAny(p);
        const AppState& app = shellState();
        if (app.ed && app.ed->isOpen())
            std::printf("scmscx: opened %s: %zu units, %ux%u tiles\n", narrow(p.filename().wstring()).c_str(), app.ed->units().size(), unsigned(app.ed->terrain().width),
                        unsigned(app.ed->terrain().height));
        else std::printf("scmscx: the map did not open (WRONG)\n");
    };
    auto pump = [&v](auto done, int ms) {
        const auto t0 = std::chrono::steady_clock::now();
        while (!done() && scmscx::msSince(t0) < ms) {
            if (gHost) gHost->snapshotTimers(5);
            runPosted();
            v.requestVisible();
        }
        return done();
    };
    const auto t0 = std::chrono::steady_clock::now();
    v.typeForTest(widen(query));
    const bool rows = pump([&v] { return !v.searching(); }, 30000);
    const double rowsMs = scmscx::msSince(t0);
    const bool previews = rows && pump([&v] { return v.visiblePreviewsDone(); }, 30000);
    const auto [first, last] = v.visibleRows();
    std::printf("scmscx: search \"%s\": %zu rows of %lld (%s) in %.0f ms from the key; minimaps of rows %d to %d %s in %.0f ms from the key\n", query.c_str(), v.rowCount(),
                (long long)v.total(), narrow(v.searchStatus()).c_str(), rowsMs, first, last, previews ? "shown" : "not all shown (WRONG)", scmscx::msSince(t0));
    if (!rows || v.rowCount() == 0) std::printf("scmscx: no rows (WRONG)\n");
    // The same search again: from the session's answers.
    if (rows && row < 0) {
        const auto t1 = std::chrono::steady_clock::now();
        v.typeForTest(widen(query + "x"));
        pump([&v] { return !v.searching(); }, 30000);
        v.typeForTest(widen(query));
        pump([&v] { return !v.searching(); }, 30000);
        std::printf("scmscx: \"%sx\" then \"%s\" again: %zu rows in %.0f ms\n", query.c_str(), query.c_str(), v.rowCount(), scmscx::msSince(t1));
    }
    if (row >= 0 && size_t(row) < v.rowCount()) {
        v.downloadForTest(row);
        pump([&v] { return !v.downloading(); }, 120000);
        std::printf("scmscx: status: %s\n", narrow(v.status()).c_str());
    }
    return &v;
}
