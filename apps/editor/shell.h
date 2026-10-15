#pragma once
// The host seam (research/ports/MAC-BUILD-PLAN.md step 2): what the editor's code asks of the window system, as free
// functions. The portable code calls these; each host implements them. The Windows host is shell_win32.inc (included
// by main.cpp), where each one is the line it replaced.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>


// The panes are placed again for the window's size and the dock (computeLayout, core/layout.inc).
void shellRelayout();
// The menus and the top tool bar follow the state (enabled, checked, the Build label).
void shellMenusChanged();
// The status bar is drawn again (its texts changed).
void shellStatusChanged();
// Whether the Log panel's window is open (the Log shows the status bar's message then).
bool shellLogOpen();
// The UI font's LOGFONT height (negative: the character height), for the views' title sizes.
int shellUiFontLfHeight();
// The map view's client size, in device pixels.
void shellViewSize(int& w, int& h);
// Runs `fn` on the UI thread, later. Safe to call from any thread; `fn` runs once.
void shellPost(std::function<void()> fn);
// The map view's timer for a held spray (25 ms): on starts it, off stops it.
void shellSprayTimer(bool on);
// The map view asks for WM_MOUSELEAVE when the mouse enters it (the next move sets it up).
void shellTrackMouseLeave();
// Ctrl or Shift is down now (the keys as they are, not the message's): a location made by a release is inverted by it.
bool shellCtrlOrShiftDown();
// The palette's filter text is cleared (the palette shows all its entries). False when there is no filter field.
bool shellClearFilter();
// The settings file (%APPDATA%\StarDraft\settings.json; the environment variable STARDRAFT_SETTINGS gives another).
std::filesystem::path shellSettingsFile();
// The value kinds of the settings written by older builds (the registry: HKCU\Software\StarDraft).
enum class LegacyKind { Dword, Text, Texts, Bytes };
// The value `value` under the key `sub` ("" is the key itself; "Key/Sub" a subkey, with '/'): its bytes, when there is one.
bool shellLegacyValue(const std::wstring& sub, const std::wstring& value, LegacyKind kind, std::vector<uint8_t>& out);
// Appends the value names under the key `sub` that are not in `out` yet.
void shellLegacyNames(const std::wstring& sub, std::vector<std::wstring>& out);
// Deletes the value `value` under the key `sub`.
void shellLegacyRemove(const std::wstring& sub, const std::wstring& value);
// An error message box (the theme's message box) with the title.
void shellErrorBox(const std::wstring& text, const std::wstring& title);
// The File menu's recent list: its items are the labels (the first one is the newest), or `none` when there are none.
// projects: the Recent projects menu, else the Recent maps menu.
void shellRecentMenu(bool projects, const std::vector<std::wstring>& labels, const std::wstring& none);
// The quick picker (Quick open, or Replace with... for the selection).
void shellShowQuickPicker(bool replace, bool open);
// A map file was opened by itself: asks once whether maps open with StarDraft 2 (Windows only).
void shellMapFileOpened(const std::filesystem::path& file);
