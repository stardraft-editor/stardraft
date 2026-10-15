#pragma once

// The file types that open with StarDraft 2 (Windows: HKCU\Software\Classes, for this Windows user, no administrator
// needed; file_types_win32.cpp). project.stardraft opens with the program; maps (.scm, .scx) are in Explorer's Open
// with list, and the program a double-click opens when the user says so. What the user is asked stays in the program.

#include <string>

namespace stardraft::platform {

// project.stardraft opens with `exe`. Explorer is told when something changed.
void registerProjectFiles(const std::wstring& exe);

// The program that file types open: this one, or the launcher (StarDraft-latest.exe) for a copy that the launcher
// started (the copy is deleted later; the launcher starts the newest build with the file).
std::wstring associationExe();

// Whether a double-click on .scm and .scx maps opens StarDraft 2 (no other program that the user chose in Windows' own
// Open with window, a UserChoice, is in the way).
bool mapsOpenWithStarDraft();

// Maps (.scm, .scx): StarDraft 2 (`exe`) is in Explorer's Open with list for them, and with `makeDefault` it is the
// program a double-click opens. Windows keeps a program that the user chose in its own Open with window (a UserChoice):
// then only Settings > Default apps changes it, and this returns false.
bool registerMapFiles(const std::wstring& exe, bool makeDefault);

}  // namespace stardraft::platform
