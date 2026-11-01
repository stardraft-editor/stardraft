// Small recent-map pictures: disk and archive work belongs to one worker; bitmaps belong to the UI thread.
#include <condition_variable>
#include <deque>
#include <fstream>
#include <thread>

extern fs::path gInstallInUse;

class RecentPreviews {
    struct Entry {
        draw::Bitmap bitmap;
        std::wstring details = L"Loading preview\x2026";
        std::chrono::steady_clock::time_point checked{};
        bool pending = false, completed = false;
        uint64_t fingerprint = 0;
    };
    struct Job { fs::path path, install, cache; std::wstring key; };
    struct Result { Image image; std::wstring details; uint64_t fingerprint = 0; bool cacheHit = false; };
    struct Lifetime {};
public:
    RecentPreviews() : worker_([this] { work(); }) {}
    ~RecentPreviews() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; jobs_.clear(); }
        wake_.notify_one();
        worker_.join();
        alive_.reset();
    }
    draw::Bitmap* picture(const fs::path& path) {
        const auto key = path.lexically_normal().wstring();
        if (entries_.size() >= 64 && !entries_.count(key)) {
            auto oldest = entries_.end();
            for (auto it = entries_.begin(); it != entries_.end(); ++it)
                if (!it->second.pending && (oldest == entries_.end() || it->second.checked < oldest->second.checked)) oldest = it;
            if (oldest == entries_.end()) return nullptr;
            entries_.erase(oldest);
        }
        auto& e = entries_[key];
        const auto now = std::chrono::steady_clock::now();
        if (!e.pending && now - e.checked > std::chrono::seconds(2)) {
            e.pending = true, e.checked = now;
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push_back({path, cmbwMode() ? fs::path() : gInstallInUse, shellCacheFolder(), key});
            wake_.notify_one();
        }
        return e.bitmap.width() ? &e.bitmap : nullptr;
    }
    std::wstring details(const fs::path& path) const {
        const auto it = entries_.find(path.lexically_normal().wstring());
        return it == entries_.end() ? L"" : it->second.details;
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return jobs_.empty() && !working_; });
    }
    std::string check(const fs::path& original, const fs::path& install) {
        std::string out;
        int passed = 0, failed = 0;
        auto expect = [&](bool value, const char* text) {
            if (value) ++passed;
            else { ++failed; out += std::string("recent-previews: ") + text + " (WRONG)\n"; }
        };
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("stardraft-recent-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (ec || !fs::create_directory(dir, ec)) return "recent-previews: could not make test directory (WRONG)\n";
        const auto originalStamp = stamp(original);
        const auto file = dir / "map.scx";
        fs::copy_file(original, file, ec);
        if (ec) { fs::remove_all(dir, ec); return "recent-previews: provide an accessible recent map (WRONG)\n"; }
        const Job job{file, install, dir / "cache", file.wstring()};
        const auto cold = make(job);
        expect(!cold.cacheHit && !cold.image.rgba.empty(), "cold render");
        expect(cold.image.width > 0 && cold.image.width <= 256 && cold.image.height > 0 && cold.image.height <= 256, "bounded thumbnail dimensions");
        const auto warm = make(job);
        expect(warm.cacheHit, "warm disk cache hit");
        expect(cold.image.rgba == warm.image.rgba && cold.details == warm.details, "warm pixels and metadata preserved");
        { std::ofstream changed(file, std::ios::binary | std::ios::app); changed.put('x'); }
        const auto edited = make(job);
        expect(!edited.cacheHit && edited.fingerprint != cold.fingerprint, "file size/mtime invalidates cache");
        expect(edited.image.rgba == cold.image.rgba, "archive with trailing bytes still renders");
        for (fs::directory_iterator it(dir / "cache" / "recent-previews", ec), end; !ec && it != end; it.increment(ec)) {
            std::ofstream corrupt(it->path(), std::ios::binary | std::ios::trunc); corrupt << "broken";
        }
        const auto repaired = make(job);
        expect(!repaired.cacheHit && repaired.image.rgba == cold.image.rgba, "corrupt cache regenerates");
        fs::remove(file, ec);
        expect(make(job).image.rgba.empty(), "missing map leaves no stale picture");
        { std::ofstream broken(file, std::ios::binary); broken << "invalid map"; }
        expect(make(job).image.rgba.empty(), "invalid map leaves plain card");
        expect(stamp(original) == originalStamp, "source map untouched");
        fs::remove_all(dir, ec);
        return out + "recent-previews: " + std::to_string(passed) + " checks, " + std::to_string(failed) + " failed\n";
    }
private:
    static uint64_t hash(const std::string& s) {
        uint64_t h = 14695981039346656037ull;
        for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
        return h;
    }
    static std::string stamp(const fs::path& p) {
        std::error_code ec;
        const auto t = fs::last_write_time(p, ec);
        if (ec) return "missing";
        const auto size = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
        return std::to_string(int64_t(t.time_since_epoch().count())) + ":" + std::to_string(size);
    }
    static bool read(const fs::path& file, Result& result) {
        std::ifstream in(file, std::ios::binary);
        uint32_t head[4]{};
        if (!in.read(reinterpret_cast<char*>(head), sizeof head) || head[0] != 0x53525031 || !head[1] || !head[2] ||
            head[1] > 256 || head[2] > 256 || head[3] > 512) return false;
        std::string text(head[3], '\0');
        if (!in.read(text.data(), text.size())) return false;
        result.image.width = int(head[1]), result.image.height = int(head[2]);
        result.image.rgba.resize(size_t(head[1]) * head[2] * 4);
        if (!in.read(reinterpret_cast<char*>(result.image.rgba.data()), result.image.rgba.size())) return false;
        result.details = widen(text);
        return true;
    }
    static void write(const fs::path& file, const Result& result) {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        if (ec) return;
        const auto text = narrow(result.details);
        const uint32_t head[] = {0x53525031, uint32_t(result.image.width), uint32_t(result.image.height), uint32_t(text.size())};
        const fs::path tmp = file.wstring() + L"." + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".tmp";
        {
            std::ofstream out(tmp, std::ios::binary);
            out.write(reinterpret_cast<const char*>(head), sizeof head).write(text.data(), text.size())
               .write(reinterpret_cast<const char*>(result.image.rgba.data()), result.image.rgba.size());
            if (!out) { out.close(); fs::remove(tmp, ec); return; }
        }
        fs::rename(tmp, file, ec);
        if (ec) { fs::remove(file, ec); fs::rename(tmp, file, ec); }
        if (ec) fs::remove(tmp, ec);
        // Only this cache's pictures: cap old map revisions at about 32 MiB.
        std::vector<std::pair<fs::file_time_type, fs::path>> files;
        for (fs::directory_iterator it(file.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() != ".preview") continue;
            files.emplace_back(it->last_write_time(ec), it->path());
        }
        std::sort(files.begin(), files.end());
        for (size_t i = 0; i + 128 < files.size(); ++i) fs::remove(files[i].second, ec);
    }
    Result make(const Job& job) {
        Result result;
        fs::path map = job.path;
        std::string error;
        const bool project = fs::is_directory(map, errorCode_) || map.filename() == EudProject::kFileName;
        if (project) {
            EudProject p;
            if (!p.open(map, error)) return result;
            map = p.inputMap();
        }
        const std::string installKey = narrow(job.install.wstring()) + stamp(job.install / ".build.info");
        const std::string identity = narrow(map.lexically_normal().wstring()) + ":" + stamp(map) + ":" + installKey;
        result.fingerprint = hash(identity);
        const fs::path file = job.cache.empty() ? fs::path() : job.cache / "recent-previews" / (std::to_string(hash(identity)) + ".preview");
        if (!file.empty() && read(file, result)) { result.cacheHit = true; return result; }
        Bytes data;
        if (!readMapChk(map, data, error)) return result;
        const Chk chk = Chk::parse(data);
        const auto info = stardraft::preview::readInfo(chk);
        if (info.width <= 0 || info.height <= 0 || info.width > 512 || info.height > 512) return result;
        if (colors_.size() >= 16) colors_.clear();
        const auto colorKey = std::make_pair(installKey, int(info.era));
        auto colors = colors_.find(colorKey);
        if (colors == colors_.end()) {
            std::vector<uint32_t> table;
            if (!job.install.empty()) {
                if (openedInstall_ != installKey) {
                    source_ = std::make_unique<GameData>();
                    source_->open(job.install, error);
                    openedInstall_ = installKey;
                }
                Tileset ts;
                if (source_ && source_->isOpen() && ts.load(*source_, info.era, error)) table = stardraft::preview::tileColors(ts);
            }
            colors = colors_.emplace(colorKey, std::move(table)).first;
        }
        result.image = stardraft::preview::render(chk, colors->second.empty() ? nullptr : &colors->second, 256);
        if (result.image.width > 256 || result.image.height > 256) {
            Image small;
            const double scale = 256.0 / std::max(result.image.width, result.image.height);
            small.width = std::max(1, int(result.image.width * scale)), small.height = std::max(1, int(result.image.height * scale));
            small.rgba.resize(size_t(small.width) * small.height * 4);
            for (int y = 0; y < small.height; ++y) for (int x = 0; x < small.width; ++x) {
                const size_t from = (size_t(y * result.image.height / small.height) * result.image.width + x * result.image.width / small.width) * 4;
                std::copy_n(result.image.rgba.data() + from, 4, small.rgba.data() + (size_t(y) * small.width + x) * 4);
            }
            result.image = std::move(small);
        }
        result.details = std::to_wstring(info.width) + L" \xD7 " + std::to_wstring(info.height) + L" \xB7 " + kTilesetNames[info.era & 7];
        if (!file.empty() && !result.image.rgba.empty()) write(file, result);
        return result;
    }
    void work() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_) return;
                job = std::move(jobs_.front()); jobs_.pop_front(); working_ = true;
            }
            Result result;
            try { result = make(job); } catch (...) { }  // damaged or unavailable maps leave a plain card
            shellPost([this, weak = std::weak_ptr<Lifetime>(alive_), key = std::move(job.key), result = std::move(result)]() mutable {
                if (weak.expired()) return;
                auto it = entries_.find(key);
                if (it == entries_.end()) return;
                auto& entry = it->second;
                entry.pending = false;
                if (entry.completed && entry.fingerprint == result.fingerprint) return;
                entry.completed = true;
                entry.fingerprint = result.fingerprint;
                if (!result.image.rgba.empty()) {
                    auto& bitmap = entry.bitmap;
                    bitmap.resize(result.image.width, result.image.height);
                    std::memcpy(bitmap.pixels(), result.image.rgba.data(), result.image.rgba.size());
                    bitmap.opaque = true;
                    bitmap.changed();
                    entry.details = std::move(result.details);
                } else {
                    entry.bitmap.resize(0, 0), entry.details = L"Preview unavailable";
                }
                gStartView.invalidate();
            });
            { std::lock_guard<std::mutex> lock(mutex_); working_ = false; }
            idle_.notify_all();
        }
    }
    std::map<std::wstring, Entry> entries_;  // only the UI thread
    std::map<std::pair<std::string, int>, std::vector<uint32_t>> colors_;  // only the worker
    std::unique_ptr<GameData> source_;
    std::string openedInstall_;
    std::error_code errorCode_;
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<Job> jobs_;
    bool stopping_ = false, working_ = false;
    std::shared_ptr<Lifetime> alive_ = std::make_shared<Lifetime>();
    std::thread worker_;
};
RecentPreviews& recentPreviews() { static RecentPreviews store; return store; }
// Snapshot tests ask for a frame first (which queues visible cards), wait for its small pictures, then paint again.
void recentPreviewsWait() { recentPreviews().wait(); runPosted(); }

std::string recentPreviewsCheck() {
    if (gRecentMaps.empty()) return "recent-previews: provide a recent map (WRONG)\n";
    RecentPreviews test;
    return test.check(fs::path(gRecentMaps.front()), gInstallInUse);
}
