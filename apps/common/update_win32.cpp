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

bool unpackArchive(const fs::path& zip, const fs::path& dir) {
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
        DeleteFileW(self.c_str());  // (what a copy from another drive left of it)
        if (!MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) return Replace::Broken;  // back as it was
        return Replace::NewNotPlaced;
    }
    return Replace::Done;
}

bool restoreProgram(const fs::path& self) {
    const fs::path old = oldProgramFile(self);
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    DeleteFileW(self.c_str());  // (the new program, which does not run)
    // (the old program is the running one: a running program's file can be renamed)
    return MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

fs::path failedProgramFile(const fs::path& self) { return fs::path(self).replace_extension(L".exe.failed"); }

bool rollBackUpdate(const fs::path& self) {
    const fs::path old = oldProgramFile(self), failed = failedProgramFile(self);
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    DeleteFileW(failed.c_str());
    // (the new program runs: its file can be renamed, not replaced)
    if (!MoveFileExW(self.c_str(), failed.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    if (MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    MoveFileExW(failed.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);  // (back as it was)
    return false;
}

bool startProgram(const fs::path& exe, const std::vector<std::string>& args) {
    std::wstring run = L"\"" + exe.wstring() + L"\"";
    for (const std::string& a : args) {  // each in quotes, as the C runtime reads a command line
        const int n = MultiByteToWideChar(CP_UTF8, 0, a.data(), int(a.size()), nullptr, 0);
        std::wstring w(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, a.data(), int(a.size()), w.data(), n);
        run += L" \"";
        size_t slashes = 0;
        for (wchar_t ch : w) {
            if (ch == L'"') run.append(slashes + 1, L'\\');  // (a quote, and the backslashes before it, escaped)
            slashes = ch == L'\\' ? slashes + 1 : 0;
            run += ch;
        }
        run.append(slashes, L'\\');  // (the backslashes before the closing quote)
        run += L"\"";
    }
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), run.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    return true;
}

}  // namespace stardraft::platform
