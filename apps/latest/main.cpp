// StarDraft-latest.exe (in the repository's root): starts the newest build of StarDraft (stardraft-testing.exe, else
// stardraft.exe, in the build folder) as a copy of its own, so new builds can be made while it is open; earlier copies that are
// closed are deleted. Arguments (a map or a project) go to StarDraft. Pin it to Start or the taskbar. It also makes
// project.stardraft files open with it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;

// project.stardraft opens with `exe` (for this Windows user: HKCU\Software\Classes, no administrator needed).
// Written only when it changes; Explorer is told then.
inline void registerProjectFiles(const std::wstring& exe) {
    auto put = [](const std::wstring& key, const std::wstring& value) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + key).c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr)) return false;
        wchar_t now[1024] = L"";
        DWORD size = sizeof now;
        const bool same = !RegQueryValueExW(k, nullptr, nullptr, nullptr, reinterpret_cast<BYTE*>(now), &size) && value == now;
        if (!same) RegSetValueExW(k, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        return !same;
    };
    bool changed = put(L".stardraft", L"StarDraft.Project");
    changed |= put(L"StarDraft.Project", L"StarDraft project");
    changed |= put(L"StarDraft.Project\\DefaultIcon", L"\"" + exe + L"\",1");  // the second icon: the document icon
    changed |= put(L"StarDraft.Project\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR args, int) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
#ifdef STARDRAFT_BUILD_DIR
    const fs::path here = fs::path(STARDRAFT_BUILD_DIR).make_preferred();
#else
    const fs::path here = fs::path(self).parent_path();
#endif
    registerProjectFiles(self);  // projects open with the newest build
    fs::path src = here / L"stardraft-testing.exe";
    std::error_code ec;
    if (!fs::exists(src, ec)) src = here / L"stardraft.exe";
    if (!fs::exists(src, ec)) {
        MessageBoxW(nullptr, (L"No build of StarDraft is in " + here.wstring()).c_str(), L"StarDraft-latest", MB_ICONERROR);
        return 1;
    }
    wchar_t* local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return 1;
    const fs::path dir = fs::path(local) / L"StarDraft" / L"latest";
    CoTaskMemFree(local);
    fs::create_directories(dir, ec);
    // Copies that are closed go (a running one cannot be deleted: it stays).
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().filename().wstring().rfind(L"stardraft-latest-", 0) == 0 && e.path().extension() == L".exe") fs::remove(e.path(), ec);
    std::random_device rd;
    const fs::path run = dir / (L"stardraft-latest-" + std::to_wstring(rd()) + L".exe");
    if (!fs::copy_file(src, run, fs::copy_options::overwrite_existing, ec)) {
        MessageBoxW(nullptr, (L"Cannot copy " + src.wstring() + L": " + std::wstring(ec.message().begin(), ec.message().end())).c_str(), L"StarDraft-latest", MB_ICONERROR);
        return 1;
    }
    std::wstring cmd = L"\"" + run.wstring() + L"\"";
    if (args && *args) cmd += L" " + std::wstring(args);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    SetEnvironmentVariableW(L"STARDRAFT_LAUNCHER", self);  // the copy links maps and projects to this program, not to itself
    if (!CreateProcessW(run.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, here.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"Cannot start StarDraft", L"StarDraft-latest", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    return 0;
}
