#include "stardraft/game_data.h"

#include <CascLib.h>

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
    ~Impl() {
        if (storage) CascCloseStorage(storage);
    }
};

GameData::GameData() : impl_(std::make_unique<Impl>()) {}
GameData::~GameData() = default;

bool GameData::open(const std::filesystem::path& installDir, std::string& error) {
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
    if (impl_->storage) CascCloseStorage(impl_->storage), impl_->storage = nullptr;
    impl_->source = std::move(source), impl_->loader = loader;
}

bool GameData::isOpen() const { return impl_->storage != nullptr || impl_->source; }

const UnitDataLoader* GameData::unitDataLoader() const { return impl_->loader; }

bool GameData::hasFile(const std::string& name, uint64_t* size) const {
    if (impl_->source) return impl_->source->hasFile(name, size);
    HANDLE file = nullptr;
    if (!impl_->storage || !CascOpenFile(impl_->storage, name.c_str(), 0, CASC_OPEN_BY_NAME, &file)) return false;
    ULONGLONG n = 0;
    const bool ok = CascGetFileSize64(file, &n);
    CascCloseFile(file);
    if (ok && size) *size = n;
    return ok;
}

bool GameData::readFile(const std::string& name, Bytes& out, std::string& error) const {
    if (impl_->source) return impl_->source->readFile(name, out, error);
    HANDLE file = nullptr;
    if (!CascOpenFile(impl_->storage, name.c_str(), 0, CASC_OPEN_BY_NAME, &file)) {
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

std::vector<std::string> GameData::listFiles() const {
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
