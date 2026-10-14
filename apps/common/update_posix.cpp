#include "update.h"

#include <spawn.h>
#include <unistd.h>

#include <system_error>
#include <vector>

#include "stardraft/process.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace fs = std::filesystem;

namespace stardraft::platform {

fs::path programFile() {
#ifdef __APPLE__
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    const fs::path p = fs::canonical(buf, ec);
    return ec ? fs::path(buf) : p;
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

const char* releaseAssetSuffix() {
#ifdef __APPLE__
    return "-macos-universal.app.tar.gz";
#else
    return "-linux-x86_64.tar.gz";
#endif
}

bool unpackZip(const fs::path& zip, const fs::path& dir) {
#ifdef __APPLE__
    const std::vector<std::string> args = {"tar", "-xf", zip.string(), "-C", dir.string()};  // bsdtar reads zip files
#else
    const std::vector<std::string> args = {"unzip", "-o", "-q", zip.string(), "-d", dir.string()};
#endif
    return runProcess(args, {}, nullptr) == 0;
}

fs::path oldProgramFile(const fs::path& self) {
    fs::path old = self;
    old += ".old";
    return old;
}

Replace replaceProgram(const fs::path& self, const fs::path& fresh) {
    const fs::path old = oldProgramFile(self);
    std::error_code ec;
    fs::remove_all(old, ec);
    // (on these systems a running program's file can be renamed; the program goes on running from the old one)
    fs::rename(self, old, ec);
    if (ec) return Replace::OldNotMoved;
    fs::rename(fresh, self, ec);
    if (ec) {
        fs::copy(fresh, self, fs::copy_options::recursive, ec);  // (another file system)
        if (ec) {
            std::error_code back;
            fs::rename(old, self, back);  // back as it was
            return Replace::NewNotPlaced;
        }
    }
    fs::permissions(self, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    return Replace::Done;
}

bool startProgram(const fs::path& exe) {
    const std::string path = exe.string();
    char* argv[] = {const_cast<char*>(path.c_str()), nullptr};
    pid_t pid = 0;
    return posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv, environ) == 0;
}

}  // namespace stardraft::platform
