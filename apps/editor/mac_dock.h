#pragma once
// The Dock, Finder and the Settings window on macOS (mac_dock.mm). The SDL program calls these functions on every
// system: on Windows and Linux they do nothing (the bodies below), so the callers need no #ifdef.
//
// - The Dock icon's menu (applicationDockMenu: of the application's delegate, SDL's, which has none: mac_app_delegate.mm adds it) lists the recent
//   maps and projects; a click on one opens it (`open`). A file that is in the system's recent documents
//   (NSDocumentController: the Dock lists them below) is not in it again. The maps that Finder opens with the program, and those that
//   are dropped on the Dock icon, are SDL's application:openFile: (an SDL_EVENT_DROP_FILE, as a drop on the window).
// - While Build runs, the Dock icon shows a bar that moves; when the build ends with errors, the icon has a badge with
//   their number and, when the program is not the active one, it jumps once. The badge goes when the program becomes
//   active again or the next build starts.
// - Program options is a Settings window: a toolbar with one item for each tab (its label and an SF Symbols icon), the
//   window's title is the tab's name, and the window's height follows the tab's content (animated, the top stays).
//
// The texts and paths are UTF-8. The functions run on the UI thread.

#include <functional>
#include <string>
#include <vector>

// A recent map or project in the Dock icon's menu: the name it shows and the path that opens.
struct MacDockItem {
    std::string title, path;
    bool project = false;  // the projects come after the maps, below a line
    bool enabled = true;   // false while a dialog waits for an answer: the item shows but cannot be clicked
};
// A tab of the Settings window: its toolbar label and its icon (an SF Symbols name).
struct MacSettingsTab {
    std::string label, symbol;
};

#ifdef __APPLE__
void macDockSetup(std::function<std::vector<MacDockItem>()> items, std::function<void(const std::string& path)> open);
// Build started (running) or ended (not running; errors: the Log's errors of the build, 0 when it built).
void macDockBuild(bool running, int errors);
// What the Dock and Finder parts set up, as lines (--check dock): the delegate's file opens, the Dock menu's items,
// the tile's bar and badge (shown, then taken away again: no jump), the Settings toolbar's items for `tabs`.
std::string macDockCheck(const std::vector<MacSettingsTab>& tabs);
// The Settings window (`nsWindow`: the NSWindow of the options' SDL window): its own window (not a child of the main
// window), the toolbar with `tabs` (`selected` chosen), no minimize and zoom buttons. A click on an item calls `chosen`.
void macSettingsToolbar(void* nsWindow, const std::vector<MacSettingsTab>& tabs, int selected, std::function<void(int)> chosen);
// The window's content height in points (the top of the window stays; `animate`: as a tab change of a Settings window).
void macSettingsContentHeight(void* nsWindow, double points, bool animate);
#else
inline void macDockSetup(std::function<std::vector<MacDockItem>()>, std::function<void(const std::string&)>) {}
inline void macDockBuild(bool, int) {}
inline std::string macDockCheck(const std::vector<MacSettingsTab>&) { return "dock: not on this system (macOS only)\n"; }
inline void macSettingsToolbar(void*, const std::vector<MacSettingsTab>&, int, std::function<void(int)>) {}
inline void macSettingsContentHeight(void*, double, bool) {}
#endif
