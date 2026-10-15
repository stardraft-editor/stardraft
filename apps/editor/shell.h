#pragma once
// The host seam (research/ports/MAC-BUILD-PLAN.md step 2): what the editor's code asks of the window system, as free
// functions. The portable code calls these; each host implements them. The Windows host is shell_win32.inc (included
// by main.cpp), where each one is the line it replaced.

#include <functional>

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
