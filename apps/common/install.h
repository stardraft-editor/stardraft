#pragma once

// Where the system says StarCraft: Remastered is. Windows: Blizzard's registry entries (install_win32.cpp); the other
// systems have no such entries (the editor's core searches the usual folders: core/install_search.inc).

#include <filesystem>
#include <vector>

namespace stardraft::platform {

// The install folders in the registry (Blizzard's InstallPath, the uninstall entry's InstallLocation), in that order;
// empty on other systems.
#ifdef _WIN32
std::vector<std::filesystem::path> registryInstallFolders();
#else
inline std::vector<std::filesystem::path> registryInstallFolders() { return {}; }
#endif

}  // namespace stardraft::platform
