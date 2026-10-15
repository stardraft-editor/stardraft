#include "install.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace stardraft::platform {

std::vector<std::filesystem::path> registryInstallFolders() {
    std::vector<std::filesystem::path> out;
    auto reg = [&](HKEY root, const wchar_t* key, const wchar_t* value) {
        wchar_t buf[MAX_PATH] = L"";
        DWORD size = sizeof buf;
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buf, &size) == ERROR_SUCCESS && *buf) out.push_back(buf);
        size = sizeof buf;
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) == ERROR_SUCCESS && *buf) out.push_back(buf);
    };
    reg(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Blizzard Entertainment\\Starcraft", L"InstallPath");
    reg(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\StarCraft", L"InstallLocation");
    reg(HKEY_CURRENT_USER, L"SOFTWARE\\Blizzard Entertainment\\Starcraft", L"InstallPath");
    return out;
}

}  // namespace stardraft::platform
