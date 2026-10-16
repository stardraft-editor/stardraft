#pragma once

// The file types that open with StarDraft 2 (Windows: HKCU\Software\Classes, for this Windows user, no administrator
// needed; file_types_win32.cpp). project.stardraft opens with the program; maps (.scm, .scx) are in Explorer's Open
// with list, and the program a double-click opens when the user says so. What the user is asked stays in the program.

#include <filesystem>
#include <string>

namespace stardraft::platform {

// The program of a file type's open command ("\"C:\Tools\stardraft.exe\" \"%1\"": C:\Tools\stardraft.exe).
inline std::wstring commandProgram(const std::wstring& command) {
    if (command.empty()) return {};
    if (command[0] == L'"') {
        const size_t end = command.find(L'"', 1);
        return end == std::wstring::npos ? command.substr(1) : command.substr(1, end - 1);
    }
    return command.substr(0, command.find(L' '));
}
// At a start that only keeps the file types (not the user's Yes): a command that opens another copy of StarDraft 2 that
// is still there stays (a copy run once from Downloads or a USB stick does not take the maps from the installed one).
// False: the command is none, opens `exe`, or opens a program that is not there any more: it is `exe`'s now.
inline bool keepRegisteredProgram(const std::wstring& command, const std::wstring& exe) {
    const std::wstring now = commandProgram(command);
    std::error_code ec;
    if (now.empty() || std::filesystem::path(now) == std::filesystem::path(exe)) return false;
    return std::filesystem::is_regular_file(std::filesystem::path(now), ec);
}

// project.stardraft opens with `exe`. Explorer is told when something changed. `keepOther`: a command that opens
// another copy that is still there stays (keepRegisteredProgram).
void registerProjectFiles(const std::wstring& exe, bool keepOther = false);

// The program that file types open: this one, or the launcher (StarDraft-latest.exe) for a copy that the launcher
// started (the copy is deleted later; the launcher starts the newest build with the file).
std::wstring associationExe();

// Whether a double-click on .scm and .scx maps opens StarDraft 2 (no other program that the user chose in Windows' own
// Open with window, a UserChoice, is in the way).
bool mapsOpenWithStarDraft();

// Maps (.scm, .scx): StarDraft 2 (`exe`) is in Explorer's Open with list for them, and with `makeDefault` it is the
// program a double-click opens. Windows keeps a program that the user chose in its own Open with window (a UserChoice):
// then only Settings > Default apps changes it, and this returns false. `keepOther`: as registerProjectFiles (the start;
// not with makeDefault).
bool registerMapFiles(const std::wstring& exe, bool makeDefault, bool keepOther = false);

}  // namespace stardraft::platform
