#include "update.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

namespace stardraft::platform {

fs::path programFile() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return self;
}

const char* releaseAssetSuffix() { return "-win64.zip"; }

bool unpackZip(const fs::path& zip, const fs::path& dir) {
    // tar.exe (in Windows 10 and later) unpacks zip files.
    wchar_t sys[MAX_PATH] = L"";
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + (fs::path(sys) / L"tar.exe").wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    DWORD code = 1;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    }
    return code == 0;
}

fs::path oldProgramFile(const fs::path& self) { return fs::path(self).replace_extension(L".exe.old"); }

Replace replaceProgram(const fs::path& self, const fs::path& fresh) {
    const fs::path old = oldProgramFile(self);
    std::error_code ec;
    fs::remove(old, ec);
    // (a running program can be renamed on Windows, not deleted)
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) return Replace::OldNotMoved;
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);  // back as it was
        return Replace::NewNotPlaced;
    }
    return Replace::Done;
}

bool startProgram(const fs::path& exe) {
    std::wstring run = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), run.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    return true;
}

}  // namespace stardraft::platform
