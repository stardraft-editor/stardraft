#pragma once
// The host seam (research/ports/MAC-BUILD-PLAN.md step 2): what the editor's code asks of the window system, as free
// functions. The portable code calls these; each host implements them. The Windows host is shell_win32.inc (included
// by main.cpp), where each one is the line it replaced.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// The control kit's icon button: the start page's remove buttons (core/views_start.inc) need its declaration in every
// editor program, and this header is the one all three include before core.inc (the SDL program has no other use of it).
#include "icon_button.h"


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
// The time the process was created, on the steady clock (the start-up marks count from it; the host's clock).
std::chrono::steady_clock::time_point shellProcessStart();
// The program's version name, as the title shows it ("0.5.0.6").
std::wstring shellVersion();
// When this .exe was built: "built Oct 9 23:48", to tell builds apart (empty when unknown).
std::wstring shellBuildStamp();
// The window's title text.
void shellSetTitle(const std::wstring& title);
// The workspace rail exists (it is made with the window), and its window is shown or hidden.
bool shellHasRail();
void shellShowRail(bool show);
// The wait cursor (the hourglass) is shown until the next message that sets the cursor.
void shellWaitCursor();
// The minimap's client size, in device pixels.
void shellMinimapSize(int& w, int& h);
// The host's renderer shows the active editor's sources (its quality, sprites and tileset).
void shellRendererSources();
// The document tab strip and the workspace rail are drawn again (their states changed).
void shellRedrawTabs();
void shellInvalidateTabs();
void shellInvalidateWorkspaceBar();
// The tab strip's native window is updated now (a tab moved under the mouse), for the view `v` that is in it (the
// including file has apps/common/view.h).
void shellUpdateTabs(stardraft::draw::View& v);
// The sprites that the units' animations hold for the editor are dropped (a closed editor's sprites).
void shellForgetAnims();
// Posts a WM_COMMAND with the command id to the main window (it runs after the current message).
void shellPostCommand(int id);
// The map view takes the keyboard focus.
void shellFocusView();
// The panel tabs' right-click menu: a grayed title line, then Left and Right (the side is checked). Returns 1 (Left),
// 2 (Right), or 0 when none is picked.
int shellTabSideMenu(const std::wstring& title, int side);
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
// The window's dpi: scale() gives device pixels for it (96 is 100%).
unsigned shellDpi();
// Writes a line to the log (the status bar's message is logged too).
void shellLogLine(const std::wstring& line);
// Whether the status bar exists (updateStatus does nothing without it).
bool shellHasStatusBar();
// The sprites the renderer drew last frame (the status bar's last part).
int shellSpriteCount();
// The palette list: only row i is selected (-1: none), and its caret is on it.
void shellListSelect(int i);
// The palette list's selected rows (Locations: several can be selected); empty when none is.
std::vector<int> shellListSelection();
// The palette list's rows are replaced by these, and the list is drawn again.
void shellListFill(const std::vector<std::wstring>& rows);
// The palette search box's text (the Locations filter and the palette search).
std::wstring shellFilterText();
// The map view's window exists (the tool strip is laid out once it does).
bool shellViewShown();
// The tool strip shows the active mode's tools again.
void shellToolsChanged();
// A popup menu of `items` (the strip's brush shapes), with the entry `checked` marked; (x, y) is its place in the tool
// strip's coordinates. Returns the picked entry's index, or -1 when none is picked.
int shellPopupPick(const std::vector<std::wstring>& items, int checked, int x, int y);
// The owner (player) combo box shows player i.
void shellOwnerSelect(int i);
// The palette is drawn again (its selection or the player's colors changed).
void shellPaletteChanged();
// The palette's unit entries are made again (the mode or the list changed).
void shellPaletteRebuilt();
// The translation of an English text for the UI (ui_text.h's tr).
std::wstring shellTr(const wchar_t* english);
// The palette's picked values (Ctrl+click): the palette's own list, which the selection of several picks changes.
std::vector<int>& shellPaletteChosen();
// The folder of the unsaved projects (a new project nobody saved yet lives there); empty when the system has none.
std::filesystem::path shellUnsavedRoot();
// The settings file (%APPDATA%\StarDraft\settings.json; the environment variable STARDRAFT_SETTINGS gives another).
std::filesystem::path shellSettingsFile();
// A setting the core changed is saved now (the Windows host saves its options as they change, only what differs).
void shellSettingsChanged();
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
// Whether the file system ignores case (Windows: a/B.scx and a/b.scx are one file), for comparing paths.
bool shellPathsFoldCase();
// A map file was opened by itself: asks once whether maps open with StarDraft 2 (Windows only).
void shellMapFileOpened(const std::filesystem::path& file);
