#include "stardraft/game_data.h"

#include <CascLib.h>

#include <mutex>
#include <type_traits>

namespace stardraft {

namespace {

// CascLib takes TCHAR paths: the path's own character type (wchar_t on Windows with UNICODE, char elsewhere).
std::basic_string<TCHAR> nativePath(const std::filesystem::path& p) {
    static_assert(std::is_same_v<TCHAR, std::filesystem::path::value_type>, "build the library with UNICODE on Windows");
    return p.native();
}

std::string cascError(const std::string& what) {
    return what + " failed (CascLib error " + std::to_string(static_cast<unsigned long>(GetCascError())) + ")";
}

}  // namespace

struct GameData::Impl {
    HANDLE storage = nullptr;
    std::unique_ptr<FileSource> source;  // set: files from it, not CASC
    const UnitDataLoader* loader = nullptr;
    // (one read at a time: CascLib's and StormLib's handles are not for threads at once; the isom terrain editor makes its
    // maps on threads of their own, and a window's worker reads while the program does)
    std::recursive_mutex reading;
    // (readFileOffThread's handle and its lock; the install folder it opens)
    HANDLE workerStorage = nullptr;
    bool workerTried = false;
    std::filesystem::path installDir;
    std::mutex workerReading;
    // The worker's handle goes (it opens again at its next read); `dir`: the install folder it opens from now on.
    void closeWorker(const std::filesystem::path* dir = nullptr) {
        std::lock_guard<std::mutex> lock(workerReading);
        if (workerStorage) CascCloseStorage(workerStorage), workerStorage = nullptr;
        workerTried = false;
        if (dir) installDir = *dir;
    }
    ~Impl() {
        if (workerStorage) CascCloseStorage(workerStorage);
        if (storage) CascCloseStorage(storage);
    }
};

GameData::GameData() : impl_(std::make_unique<Impl>()) {}
GameData::~GameData() = default;

bool GameData::open(const std::filesystem::path& installDir, std::string& error) {
    impl_->closeWorker(&installDir);
    std::lock_guard<std::recursive_mutex> lock(impl_->reading);  // (a worker may read through the program's handle)
    impl_->source.reset(), impl_->loader = nullptr;
    if (impl_->storage) {
        CascCloseStorage(impl_->storage);
        impl_->storage = nullptr;
    }
    if (!CascOpenStorage(nativePath(installDir).c_str(), 0, &impl_->storage)) {
        impl_->storage = nullptr;
        error = cascError("CascOpenStorage");
        return false;
    }
    return true;
}

void GameData::open(std::unique_ptr<FileSource> source, const UnitDataLoader* loader) {
    const std::filesystem::path none;
    impl_->closeWorker(&none);
    std::lock_guard<std::recursive_mutex> lock(impl_->reading);
    if (impl_->storage) CascCloseStorage(impl_->storage), impl_->storage = nullptr;
    impl_->source = std::move(source), impl_->loader = loader;
}

bool GameData::isOpen() const { return impl_->storage != nullptr || impl_->source; }

const UnitDataLoader* GameData::unitDataLoader() const { return impl_->loader; }

bool GameData::hasFile(const std::string& name, uint64_t* size) const {
    std::lock_guard<std::recursive_mutex> lock(impl_->reading);
    if (impl_->source) return impl_->source->hasFile(name, size);
    HANDLE file = nullptr;
    if (!impl_->storage || !CascOpenFile(impl_->storage, name.c_str(), 0, CASC_OPEN_BY_NAME, &file)) return false;
    ULONGLONG n = 0;
    const bool ok = CascGetFileSize64(file, &n);
    CascCloseFile(file);
    if (ok && size) *size = n;
    return ok;
}

// A file of CASC storage `storage` (its caller holds the storage's lock).
static bool readCasc(HANDLE storage, const std::string& name, Bytes& out, std::string& error) {
    HANDLE file = nullptr;
    if (!CascOpenFile(storage, name.c_str(), 0, CASC_OPEN_BY_NAME, &file)) {
        error = cascError("CascOpenFile(" + name + ")");
        return false;
    }
    ULONGLONG size = 0;
    if (!CascGetFileSize64(file, &size)) {
        CascCloseFile(file);
        error = cascError("CascGetFileSize64(" + name + ")");
        return false;
    }
    out.resize(static_cast<size_t>(size));
    DWORD read = 0;
    bool ok = CascReadFile(file, out.data(), static_cast<DWORD>(size), &read) && read == size;
    CascCloseFile(file);
    if (!ok) {
        error = cascError("CascReadFile(" + name + ")");
        return false;
    }
    return true;
}

bool GameData::readFile(const std::string& name, Bytes& out, std::string& error) const {
    std::lock_guard<std::recursive_mutex> lock(impl_->reading);
    if (impl_->source) return impl_->source->readFile(name, out, error);
    return readCasc(impl_->storage, name, out, error);
}

bool GameData::readFileOffThread(const std::string& name, Bytes& out, std::string& error) const {
    {
        std::lock_guard<std::mutex> lock(impl_->workerReading);
        if (!impl_->workerTried && !impl_->installDir.empty()) {
            impl_->workerTried = true;
            if (!CascOpenStorage(nativePath(impl_->installDir).c_str(), 0, &impl_->workerStorage)) impl_->workerStorage = nullptr;
        }
        if (impl_->workerStorage) return readCasc(impl_->workerStorage, name, out, error);
    }
    return readFile(name, out, error);  // (no handle of its own: the program's, under its lock)
}

std::vector<std::string> GameData::listFiles() const {
    std::lock_guard<std::recursive_mutex> lock(impl_->reading);
    if (impl_->source) return impl_->source->listFiles();
    std::vector<std::string> names;
    CASC_FIND_DATA fd;
    HANDLE find = CascFindFirstFile(impl_->storage, "*", &fd, nullptr);
    if (!find) return names;
    do {
        names.emplace_back(fd.szFileName);
    } while (CascFindNextFile(find, &fd));
    CascFindClose(find);
    return names;
}

}  // namespace stardraft
