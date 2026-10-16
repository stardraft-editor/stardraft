#include "file_types.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

// A value under HKCU\Software\Classes\<key>: the key's default value, or the named one. Written only when it changes;
// true then.
bool putClassesValue(const std::wstring& key, const std::wstring& value, const wchar_t* name = nullptr, DWORD type = REG_SZ) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + key).c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr)) return false;
    wchar_t now[1024] = L"";
    DWORD size = sizeof now, had = 0;
    const bool same = !RegQueryValueExW(k, name, nullptr, &had, reinterpret_cast<BYTE*>(now), &size) && had == type && value == now;
    if (!same) RegSetValueExW(k, name, 0, type, reinterpret_cast<const BYTE*>(value.c_str()), type == REG_NONE ? 0 : DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return !same;
}

const wchar_t* const kMapExtensions[] = {L".scm", L".scx"};

// The default value of HKCU\Software\Classes\<key> ("" when there is none).
std::wstring classesValue(const std::wstring& key) {
    wchar_t now[1024] = L"";
    DWORD size = sizeof now;
    if (RegGetValueW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + key).c_str(), nullptr, RRF_RT_REG_SZ, nullptr, now, &size)) return {};
    return now;
}

// A program that the user chose for the extension in Windows' Open with window, other than StarDraft 2.
bool userChoiceElsewhere(const wchar_t* ext) {
    // UserChoice, and UserChoiceLatest (where Windows 11 keeps it, often without UserChoice).
    for (const wchar_t* sub : {L"\\UserChoice", L"\\UserChoiceLatest"}) {
        wchar_t progId[256] = L"";
        DWORD size = sizeof progId;
        const std::wstring key = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\") + ext + sub;
        if (!RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"ProgId", RRF_RT_REG_SZ, nullptr, progId, &size) && std::wstring(progId) != L"StarDraft.Map")
            return true;
    }
    return false;
}

}  // namespace

void registerProjectFiles(const std::wstring& exe, bool keepOther) {
    auto put = [](const std::wstring& key, const std::wstring& value) { return putClassesValue(key, value); };
    bool changed = put(L".stardraft", L"StarDraft.Project");
    changed |= put(L"StarDraft.Project", L"StarDraft 2 project");
    if (keepOther && keepRegisteredProgram(classesValue(L"StarDraft.Project\\shell\\open\\command"), exe)) {
        if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return;
    }
    changed |= put(L"StarDraft.Project\\DefaultIcon", L"\"" + exe + L"\",1");  // the second icon: the document icon
    changed |= put(L"StarDraft.Project\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

std::wstring associationExe() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (fs::path(self).filename().wstring().rfind(L"stardraft-latest-", 0) != 0) return self;
    wchar_t launcher[MAX_PATH] = L"";  // the launcher says where it is
    std::error_code ec;
    if (GetEnvironmentVariableW(L"STARDRAFT_LAUNCHER", launcher, MAX_PATH) && fs::exists(launcher, ec)) return launcher;
    return self;
}

bool mapsOpenWithStarDraft() {
    for (const wchar_t* ext : kMapExtensions) {
        wchar_t now[256] = L"";
        DWORD size = sizeof now;
        if (RegGetValueW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext).c_str(), nullptr, RRF_RT_REG_SZ, nullptr, now, &size) ||
            std::wstring(now) != L"StarDraft.Map" || userChoiceElsewhere(ext))
            return false;
    }
    return true;
}

bool registerMapFiles(const std::wstring& exe, bool makeDefault, bool keepOther) {
    bool changed = putClassesValue(L"StarDraft.Map", L"StarDraft 2 map");
    if (makeDefault || !keepOther || !keepRegisteredProgram(classesValue(L"StarDraft.Map\\shell\\open\\command"), exe)) {
        changed |= putClassesValue(L"StarDraft.Map\\DefaultIcon", L"\"" + exe + L"\",1");
        changed |= putClassesValue(L"StarDraft.Map\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    }
    bool ours = true;
    for (const wchar_t* ext : kMapExtensions) {
        changed |= putClassesValue(std::wstring(ext) + L"\\OpenWithProgids", L"", L"StarDraft.Map", REG_NONE);
        if (makeDefault) changed |= putClassesValue(ext, L"StarDraft.Map");
        ours = ours && !userChoiceElsewhere(ext);
    }
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ours;
}

}  // namespace stardraft::platform
