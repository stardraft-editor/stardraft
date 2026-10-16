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
    return "-macos-arm64.dmg";  // (the macos job's disk image, tools/ci/mac_dist.sh: Check for updates opens it in the browser)
#else
    return "-linux-x86_64.tar.gz";
#endif
}

bool unpackArchive(const fs::path& archive, const fs::path& dir) {
    const bool zip = archive.extension() == ".zip";
#ifdef __APPLE__
    (void)zip;
    const std::vector<std::string> args = {"tar", "-xf", archive.string(), "-C", dir.string()};  // bsdtar reads both
#else
    const std::vector<std::string> args = zip ? std::vector<std::string>{"unzip", "-o", "-q", archive.string(), "-d", dir.string()}
                                              : std::vector<std::string>{"tar", "-xzf", archive.string(), "-C", dir.string()};
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
            fs::remove_all(self, back);  // (what was copied of it)
            fs::rename(old, self, back);  // back as it was
            return back ? Replace::Broken : Replace::NewNotPlaced;
        }
    }
    fs::permissions(self, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    return Replace::Done;
}

bool restoreProgram(const fs::path& self) {
    const fs::path old = oldProgramFile(self);
    std::error_code ec;
    if (!fs::exists(old, ec)) return false;
    fs::remove_all(self, ec);  // (the new program, which does not run)
    fs::rename(old, self, ec);
    return !ec;
}

bool startProgram(const fs::path& exe, const std::vector<std::string>& args) {
    const std::string path = exe.string(), dir = exe.parent_path().string();
    std::vector<char*> argv = {const_cast<char*>(path.c_str())};
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, dir.c_str());  // (its folder the working folder, as on Windows)
    pid_t pid = 0;
    const bool started = posix_spawn(&pid, path.c_str(), &actions, nullptr, argv.data(), environ) == 0;
    posix_spawn_file_actions_destroy(&actions);
    return started;
}

}  // namespace stardraft::platform
