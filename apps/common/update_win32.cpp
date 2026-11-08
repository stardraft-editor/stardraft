#include "update.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <zlib.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace stardraft::platform {

fs::path programFile() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return self;
}

const char* releaseAssetSuffix() { return "-win64.zip"; }

namespace {

uint32_t le(const unsigned char* p, int n) {
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

// The release's zip unpacked here (its files, stored or deflated; StormLib's zlib): no other program, so no tar.exe
// (not in Windows 10 before 1803) and no path in the system's code page (a user name in other letters). Only the
// files at the zip's top level, by their own names.
bool unzipOwn(const fs::path& zip, const fs::path& dir) {
    std::ifstream in(zip, std::ios::binary);
    if (!in) return false;
    const std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 22) return false;
    // (the end of the central directory: its signature in the last 64 KiB)
    size_t end = data.size() - 22;
    for (;; --end) {
        if (le(&data[end], 4) == 0x06054b50) break;
        if (end == 0 || data.size() - end > 65557) return false;
    }
    const uint32_t count = le(&data[end + 10], 2), at = le(&data[end + 16], 4);
    size_t p = at;
    int written = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 46 > data.size() || le(&data[p], 4) != 0x02014b50) return false;
        const uint32_t method = le(&data[p + 10], 2), packed = le(&data[p + 20], 4), size = le(&data[p + 24], 4);
        const uint32_t nameLen = le(&data[p + 28], 2), extraLen = le(&data[p + 30], 2), commentLen = le(&data[p + 32], 2), local = le(&data[p + 42], 4);
        if (p + 46 + nameLen > data.size()) return false;
        const std::string name(reinterpret_cast<const char*>(&data[p + 46]), nameLen);
        p += 46 + nameLen + extraLen + commentLen;
        if (name.empty() || name.back() == '/' || name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..") continue;
        if (size_t(local) + 30 > data.size() || le(&data[local], 4) != 0x04034b50) return false;
        const size_t start = local + 30 + le(&data[local + 26], 2) + le(&data[local + 28], 2);
        if (start + packed > data.size()) return false;
        std::vector<unsigned char> out(size);
        if (method == 0) {
            if (packed != size) return false;
            std::copy(data.begin() + std::ptrdiff_t(start), data.begin() + std::ptrdiff_t(start + size), out.begin());
        } else if (method == 8) {
            z_stream z{};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
            z.next_in = const_cast<unsigned char*>(&data[start]), z.avail_in = packed;
            z.next_out = out.data(), z.avail_out = size;
            const int r = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (r != Z_STREAM_END || z.total_out != size) return false;
        } else {
            return false;
        }
        // (the name: UTF-8 when the zip says so, else as the zip programs of Windows write ASCII names)
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, name.data(), int(name.size()), nullptr, 0);
        std::wstring wname(size_t(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.data(), int(name.size()), wname.data(), wlen);
        std::ofstream o(dir / wname, std::ios::binary | std::ios::trunc);
        if (!o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()))) return false;
        ++written;
    }
    return written > 0;
}

}  // namespace

bool unpackArchive(const fs::path& zip, const fs::path& dir) {
    if (unzipOwn(zip, dir)) return true;
    // Else tar.exe (in Windows 10 and later) unpacks zip files.
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
