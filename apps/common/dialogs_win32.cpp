#include "dialogs.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>

#include <algorithm>

namespace stardraft::platform {

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// "name\0patterns\0...\0" (the common dialogs' filter).
std::wstring filterText(const std::vector<FileFilter>& filters) {
    std::wstring out;
    for (const FileFilter& f : filters) out += wide(f.name) + L'\0' + wide(f.patterns) + L'\0';
    return out + L'\0';
}

// The common dialog with the flags; the buffer holds what it returns.
bool run(const FileDialog& d, bool save, DWORD flags, std::vector<wchar_t>& buf) {
    const std::wstring name = d.fileName.wstring();
    std::copy_n(name.c_str(), std::min(name.size(), buf.size() - 1), buf.begin());
    const std::wstring filter = filterText(d.filters), dir = d.initialDir.wstring(), title = wide(d.title), ext = wide(d.defaultExtension);
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = static_cast<HWND>(d.owner);
    ofn.lpstrFilter = d.filters.empty() ? nullptr : filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = DWORD(buf.size());
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrTitle = title.empty() ? nullptr : title.c_str();
    ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str();
    ofn.Flags = OFN_EXPLORER | flags;
    return (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) != FALSE;
}

}  // namespace

std::optional<std::filesystem::path> openFileDialog(const FileDialog& d) {
    std::vector<wchar_t> buf(MAX_PATH, 0);
    if (!run(d, false, OFN_FILEMUSTEXIST, buf)) return std::nullopt;
    return std::filesystem::path(buf.data());
}

std::vector<std::filesystem::path> openFilesDialog(const FileDialog& d) {
    std::vector<wchar_t> buf(32768, 0);
    std::vector<std::filesystem::path> out;
    if (!run(d, false, OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT, buf)) return out;
    // One file: the full path. Several: the folder, then each name.
    const std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (!*p) out.push_back(first);
    for (; *p; p += wcslen(p) + 1) out.push_back(std::filesystem::path(first) / p);
    return out;
}

std::optional<std::filesystem::path> saveFileDialog(const FileDialog& d) {
    std::vector<wchar_t> buf(MAX_PATH, 0);
    if (!run(d, true, OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN | (d.askToReplace ? OFN_OVERWRITEPROMPT : 0), buf)) return std::nullopt;
    return std::filesystem::path(buf.data());
}

std::optional<std::filesystem::path> pickFolderDialog(void* owner, const std::string& title) {
    std::optional<std::filesystem::path> out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS);
    dlg->SetTitle(wide(title).c_str());
    if (SUCCEEDED(dlg->Show(static_cast<HWND>(owner)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) out = std::filesystem::path(path), CoTaskMemFree(path);
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

}  // namespace stardraft::platform
