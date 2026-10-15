#pragma once
// The host seam (research/ports/MAC-BUILD-PLAN.md step 2): what the editor's code asks of the window system, as free
// functions. The portable code calls these; each host implements them. The Windows host is shell_win32.inc (included
// by main.cpp), where each one is the line it replaced.

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
// The palette list's rows are replaced by these, and the list is drawn again.
void shellListFill(const std::vector<std::wstring>& rows);
// The palette search box's text (the Locations filter and the palette search).
std::wstring shellFilterText();
// The map view's window exists (the tool strip is laid out once it does).
bool shellViewShown();
// The tool strip shows the active mode's tools again.
void shellToolsChanged();
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
