#pragma once

// The system's file and folder dialogs. Windows: the common dialogs (dialogs_win32.cpp); the new shell uses SDL's on
// other systems. Each call waits for the dialog. Texts are UTF-8.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace stardraft::platform {

struct FileFilter {
    std::string name;      // "StarCraft maps (*.scx;*.scm)"
    std::string patterns;  // "*.scx;*.scm"
};

struct FileDialog {
    void* owner = nullptr;              // the window it belongs to (HWND on Windows)
    std::vector<FileFilter> filters;    // in this order; the first is chosen
    std::filesystem::path initialDir;   // the system's choice when empty
    std::filesystem::path fileName;     // the name (or path) in the name box at first
    std::string defaultExtension;       // added to a typed name without one ("scx")
    std::string title;                  // the system's when empty
    bool askToReplace = false;          // save: ask before an existing file is replaced
};

std::optional<std::filesystem::path> openFileDialog(const FileDialog& d);
std::vector<std::filesystem::path> openFilesDialog(const FileDialog& d);  // several files; empty when canceled
std::optional<std::filesystem::path> saveFileDialog(const FileDialog& d);
std::optional<std::filesystem::path> pickFolderDialog(void* owner, const std::string& title);

}  // namespace stardraft::platform
