#pragma once
// The macOS menus of the SDL program (mac_menus.mm): the menu bar at the top of the screen (the application menu, the
// program's menus from the core's menu model, the Window menu) and the context menus as NSMenu at the pointer. The
// texts are UTF-8. The functions run on the UI thread. Only on macOS; sdl_native_menus.inc makes the items.
//
// The keys: the menu bar shows each command's key, but the keys of the program's menus go to the window (the view
// with the focus first, then the program's key table, as on Windows): a text field keeps Cmd+C and Cmd+V. Only the
// application menu's keys (Cmd+Q, Cmd+H, Option+Cmd+H, Cmd+,) and the Window menu's (Cmd+M, Cmd+`) are the menus'.

#include <string>
#include <vector>

struct MacMenuItem {
    std::string title;
    std::string key;     // the key equivalent (one character, UTF-8: "s", "\b", a function key's character); empty: none
    unsigned mods = 0;   // kMacCmd, kMacShift, kMacOption, kMacControl
    int tag = 0;         // what pick() gets (a command id, or a tab: kMacTabTag - n)
    bool checked = false, enabled = true, separator = false;
    std::vector<MacMenuItem> sub;  // a submenu's items
};
constexpr unsigned kMacCmd = 1, kMacShift = 2, kMacOption = 4, kMacControl = 8;
constexpr int kMacTabTag = -1000;  // the Window menu's tab n has the tag kMacTabTag - n

// The texts of the application menu and the Window menu, in the program's language.
struct MacMenuTexts {
    std::string app, about, settings, services, hide, hideOthers, showAll, quit;
    std::string window, minimize, zoom, nextTab, bringAll;
};

// Makes the menu bar: the application menu (about and settings give their tags), `menus` (the program's menus, each
// with its items in `sub`), then the Window menu (Minimize, Zoom, Next tab, the tabs, Bring all to front). A pick runs
// pick(tag) after the menu closed. Not while a menu is open (macMenuOpen): call it again later.
void macSetMenuBar(const MacMenuTexts& texts, int aboutTag, int settingsTag, int nextTabTag, const std::vector<MacMenuItem>& menus,
                   const std::vector<MacMenuItem>& tabs, void (*pick)(int tag));
// True while a menu of the menu bar or a context menu is open.
bool macMenuOpen();

// Opens `items` as a context menu at the pointer and waits until it closes. Returns the tag of the item that was
// picked, or INT_MIN for none.
int macPopupMenu(const std::vector<MacMenuItem>& items);

// The menu bar as text (--check menus): one line for each item, "Top > Item [> Sub]", then its key ("cmd+shift+S"),
// "(gray)" and "(checked)".
std::vector<std::string> macMenuBarLines();
