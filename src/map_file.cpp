#include "stardraft/map_file.h"

#include <StormLib.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>
#include <type_traits>

namespace stardraft {

namespace {

// StormLib takes TCHAR paths: the path's own character type (wchar_t on Windows with UNICODE, char elsewhere).
std::basic_string<TCHAR> nativePath(const std::filesystem::path& p) {
    static_assert(std::is_same_v<TCHAR, std::filesystem::path::value_type>, "build the library with UNICODE on Windows");
    return p.native();
}

std::string stormError(const std::string& what) {
    return what + " failed (StormLib error " + std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// StormLib names files missing from the listfile "File00000012.xxx". Their real
// names are unknown, so they cannot be written back under the right hash.
bool isPseudoName(const std::string& name) {
    if (name.size() < 12 || name.compare(0, 4, "File") != 0) return false;
    for (size_t i = 4; i < 12; ++i)
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
    return name.size() == 12 || name[12] == '.';
}

bool isInternalName(const std::string& name) {
    std::string n = lower(name);
    return n == "(listfile)" || n == "(attributes)" || n == "(signature)";
}

class Archive {
public:
    Archive() = default;
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;
    ~Archive() { close(); }

    // MPQ_OPEN_FORCE_MPQ_V1 makes StarCraft-style reading work on "protected" maps with
    // bogus header fields; StormLib then treats the archive as read-only.
    bool openRead(const std::filesystem::path& p) {
        return SFileOpenArchive(nativePath(p).c_str(), 0, MPQ_OPEN_FORCE_MPQ_V1 | STREAM_FLAG_READ_ONLY, &h_);
    }
    bool create(const std::filesystem::path& p, DWORD maxFiles) {
        return SFileCreateArchive(nativePath(p).c_str(), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE, maxFiles,
                                  &h_);
    }
    bool close() {
        bool ok = true;
        if (h_) ok = SFileCloseArchive(h_);
        h_ = nullptr;
        return ok;
    }
    HANDLE get() const { return h_; }

private:
    HANDLE h_ = nullptr;
};

bool readFromArchive(HANDLE mpq, const char* name, Bytes& out, std::string& error) {
    HANDLE file = nullptr;
    if (!SFileOpenFileEx(mpq, name, SFILE_OPEN_FROM_MPQ, &file)) {
        error = stormError(std::string("SFileOpenFileEx(") + name + ")");
        return false;
    }
    DWORD size = SFileGetFileSize(file, nullptr);
    if (size == SFILE_INVALID_SIZE) {
        SFileCloseFile(file);
        error = stormError("SFileGetFileSize");
        return false;
    }
    out.resize(size);
    DWORD read = 0;
    bool ok = SFileReadFile(file, out.data(), size, &read, nullptr) && read == size;
    SFileCloseFile(file);
    if (!ok) {
        error = stormError(std::string("SFileReadFile(") + name + ")");
        return false;
    }
    return true;
}

bool addToArchive(HANDLE mpq, const std::string& name, const Bytes& data, std::string& error) {
    HANDLE file = nullptr;
    DWORD size = static_cast<DWORD>(data.size());
    if (!SFileCreateFile(mpq, name.c_str(), 0, size, 0, MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, &file)) {
        error = stormError("SFileCreateFile(" + name + ")");
        return false;
    }
    // PKWARE implode: lossless, and what StarEdit uses / every StarCraft version reads.
    bool ok = size == 0 || SFileWriteFile(file, data.data(), size, MPQ_COMPRESSION_PKWARE);
    ok = SFileFinishFile(file) && ok;
    if (!ok) error = stormError("SFileWriteFile(" + name + ")");
    return ok;
}

}  // namespace

bool readMapFile(const std::filesystem::path& mapPath, Bytes& out, std::string& error, const char* nameInArchive) {
    Archive mpq;
    if (!mpq.openRead(mapPath)) {
        error = stormError("SFileOpenArchive");
        return false;
    }
    return readFromArchive(mpq.get(), nameInArchive, out, error);
}

bool readMapFiles(const std::filesystem::path& mapPath, std::vector<ArchiveFile>& out, std::string& error,
                  const std::vector<std::string>& extraNames, std::vector<std::string>* unreadable) {
    Archive mpq;
    if (!mpq.openRead(mapPath)) {
        error = stormError("SFileOpenArchive");
        return false;
    }

    std::vector<std::string> names;
    SFILE_FIND_DATA fd;
    if (HANDLE find = SFileFindFirstFile(mpq.get(), "*", &fd, nullptr)) {
        do {
            names.emplace_back(fd.cFileName);
        } while (SFileFindNextFile(find, &fd));
        SFileFindClose(find);
    }
    names.insert(names.end(), extraNames.begin(), extraNames.end());
    if (std::none_of(names.begin(), names.end(), [](const std::string& n) { return lower(n) == lower(kChkArchivePath); }))
        names.emplace_back(kChkArchivePath);

    std::set<std::string> seen;
    for (const auto& name : names) {
        if (isInternalName(name) || !seen.insert(lower(name)).second) continue;
        if (!SFileHasFile(mpq.get(), name.c_str())) continue;
        if (isPseudoName(name)) {
            if (unreadable) unreadable->push_back(name);
            continue;
        }
        ArchiveFile f{name, {}};
        if (!readFromArchive(mpq.get(), name.c_str(), f.data, error)) {
            if (lower(name) == lower(kChkArchivePath)) return false;
            if (unreadable) unreadable->push_back(name);
            error.clear();
            continue;
        }
        out.push_back(std::move(f));
    }
    return true;
}

bool writeMap(const std::filesystem::path& dstPath, const Bytes& chk, const std::vector<ArchiveFile>& otherFiles,
              std::string& error) {
    std::filesystem::path tmp = dstPath;
    tmp += ".tmp";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    std::vector<const ArchiveFile*> files;
    for (const auto& f : otherFiles)
        if (lower(f.name) != lower(kChkArchivePath) && !isInternalName(f.name)) files.push_back(&f);

    // Hash table size must be a power of two; leave room for the listfile.
    DWORD maxFiles = 16;
    while (maxFiles < files.size() + 4) maxFiles *= 2;

    {
        Archive mpq;
        if (!mpq.create(tmp, maxFiles)) {
            error = stormError("SFileCreateArchive");
            return false;
        }
        bool ok = addToArchive(mpq.get(), kChkArchivePath, chk, error);
        for (const auto* f : files)
            if (ok) ok = addToArchive(mpq.get(), f->name, f->data, error);
        if (!mpq.close() && ok) {
            error = stormError("SFileCloseArchive");
            ok = false;
        }
        if (!ok) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }

    std::filesystem::rename(tmp, dstPath, ec);
    if (ec) {
        error = "could not rename the file: " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace stardraft
