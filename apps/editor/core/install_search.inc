// Where a StarCraft: Remastered install may be on each system (portable, core.inc). On Windows the registry's entries
// come first (platform::registryInstallFolders, apps/common/install.h), then these folders. A candidate is a folder: the
// game data opens from it, or from a folder above it or below it that holds .build.info (installTries).

enum class InstallSystem { Windows, Mac, Linux };

// The folders of the entries of `root` (a Wine drive's folders, Steam's prefixes): root/<entry>/rest, the entries sorted.
void installFoldersIn(std::vector<fs::path>& out, const fs::path& root, const fs::path& rest) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        std::error_code ec2;
        if (e.is_directory(ec2)) entries.push_back(e.path());
    }
    std::sort(entries.begin(), entries.end());
    for (const fs::path& e : entries) out.push_back(e / rest);
}

// The install folders of a system, the first one first. `home` is the user's folder.
std::vector<fs::path> installCandidatesOf(InstallSystem sys, const fs::path& home) {
    std::vector<fs::path> out;
    if (sys == InstallSystem::Windows) {  // (the usual install folders; `home` is not used)
        for (const wchar_t* p : {L"C:\\Program Files (x86)\\StarCraft", L"C:\\Program Files\\StarCraft", L"C:\\Program Files (x86)\\Starcraft Remastered\\StarCraft",
                                 L"C:\\Program Files (x86)\\Starcraft Remastered", L"C:\\StarCraft", L"D:\\StarCraft", L"D:\\Program Files (x86)\\StarCraft"})
            out.push_back(p);
        return out;
    }
    const fs::path names[] = {"StarCraft", "StarCraft Remastered"};
    if (sys == InstallSystem::Mac) {
        // Battle.net installs to /Applications/StarCraft/ (with .build.info in it).
        for (const fs::path& n : names) out.push_back(fs::path("/Applications") / n), out.push_back(home / "Applications" / n);
        return out;
    }
    // Linux: Wine's default prefix, the Battle.net and Lutris folders in ~/Games, and Steam's Proton prefixes.
    const fs::path programs = fs::path("drive_c") / "Program Files (x86)";
    for (const fs::path& n : names) out.push_back(home / ".wine" / programs / n);
    for (const fs::path& n : names) installFoldersIn(out, home / "Games", programs / n);
    for (const fs::path& n : names) installFoldersIn(out, home / ".steam" / "steam" / "steamapps" / "compatdata", fs::path("pfx") / programs / n);
    for (const fs::path& n : names) installFoldersIn(out, home / ".local" / "share" / "Steam" / "steamapps" / "compatdata", fs::path("pfx") / programs / n);
    return out;
}

// The folders an install opens from, in order: the folder itself, then (when it has no .build.info) the folder above it
// and the folders below it that hold .build.info. The Windows program tries the same (tryInstall).
std::vector<fs::path> installTries(const fs::path& dir) {
    std::error_code ec;
    std::vector<fs::path> tries = {dir};
    if (!fs::exists(dir / ".build.info", ec)) {
        tries.push_back(dir.parent_path());
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / ".build.info", ec)) tries.push_back(e.path());
    }
    return tries;
}

// Options > StarCraft folder: the folder a pick opens from: the folder itself when it has .build.info, else the folder below
// it that has one (the last one listed), else the folder (its game data then says why it does not open).
fs::path installFolderOf(const fs::path& picked) {
    std::error_code ec;
    fs::path dir = picked;
    if (!fs::exists(dir / ".build.info", ec))
        for (const auto& e : fs::directory_iterator(picked, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / ".build.info", ec)) dir = e.path();
    return dir;
}
