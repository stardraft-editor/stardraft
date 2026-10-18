// The system's menus on macOS (mac_menus.mm): the menu bar at the top of the screen in place of the window's menu bar,
// made from the core's menu model as the window's menus are (sdl_menus.inc: rebuildMenus), and the kit's context menus
// as NSMenu at the pointer (kit::Menu::setNativeHook). On Windows and Linux the functions do nothing: the window has its
// own menu bar. --check menus prints the menu bar's items and keys (sdlMenuCheck). Included by main_sdl.cpp after
// sdl_menus.inc.

#ifdef __APPLE__
#include <climits>

#include "mac_menus.h"
#endif

// The menu bar is the system's (macOS: a run with a window, or a snapshot with --check menus): the window's menu bar
// is hidden then (sdl_main_view.inc).
bool gNativeMenuBar = false;
bool sdlNativeMenuBar() { return gNativeMenuBar; }

#ifdef __APPLE__

// A UTF-8 text of one code point.
std::string utf8Of(unsigned cp) {
    std::string s;
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) s += char(0xC0 | (cp >> 6)), s += char(0x80 | (cp & 0x3F));
    else s += char(0xE0 | (cp >> 12)), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
    return s;
}

// The key equivalent of a shortcut text ("Ctrl+Shift+S": "s" with Cmd and Shift; Ctrl is Cmd and Alt is Option). False
// when the text is no key.
bool macKeyOf(const std::wstring& shortcut, std::string& key, unsigned& mods) {
    const auto c = stardraft::parseShortcut(narrow(shortcut));
    if (!c) return false;
    mods = (c->ctrl ? kMacCmd : 0) | (c->shift ? kMacShift : 0) | (c->alt ? kMacOption : 0);
    const std::string& k = c->key;
    if (k.size() == 1) {
        key = std::string(1, char(std::tolower(static_cast<unsigned char>(k[0]))));
        return true;
    }
    // (NSDeleteFunctionKey, the arrows, Home, End, the pages and F1 are NSEvent's function key characters)
    static const std::pair<const char*, unsigned> kNames[] = {
        {"Del", 0xF728}, {"Backspace", 0x08}, {"Tab", 0x09}, {"Enter", 0x0D}, {"Esc", 0x1B}, {"Space", 0x20}, {"Left", 0xF702},
        {"Right", 0xF703}, {"Up", 0xF700}, {"Down", 0xF701}, {"Home", 0xF729}, {"End", 0xF72B}, {"PgUp", 0xF72C}, {"PgDn", 0xF72D}, {"Ins", 0xF727}};
    for (const auto& [name, cp] : kNames)
        if (k == name) return key = utf8Of(cp), true;
    if (k.size() >= 2 && k[0] == 'F') return key = utf8Of(0xF704 + unsigned(std::atoi(k.c_str() + 1) - 1)), true;
    return false;
}

// The items of the system's menu (no separator first, last or two together).
void macTidy(std::vector<MacMenuItem>& items) {
    std::vector<MacMenuItem> out;
    for (MacMenuItem& it : items) {
        if (it.separator && (out.empty() || out.back().separator)) continue;
        out.push_back(std::move(it));
    }
    while (!out.empty() && out.back().separator) out.pop_back();
    items = std::move(out);
}

MacMenuItem macSeparator() {
    MacMenuItem s;
    s.separator = true;
    return s;
}

// A recent list as the system's items (as recentItems: "None" when it is empty).
std::vector<MacMenuItem> macRecent(bool projects) {
    const std::vector<std::wstring>& list = projects ? gRecentProjects : gRecentMaps;
    std::vector<MacMenuItem> out;
    for (size_t i = 0; i < list.size() && i < size_t(kRecentMax); ++i) {
        MacMenuItem m;
        m.title = narrow(list[i]), m.tag = (projects ? ID_RECENT_PROJECT : ID_RECENT_MAP) + int(i);
        out.push_back(std::move(m));
    }
    if (out.empty()) {
        MacMenuItem none;
        none.title = narrow(tr(L"None")), none.enabled = false;
        out.push_back(std::move(none));
    }
    return out;
}

// The core's menu model as the system's items: the texts, checks, gray items and keys of the window's menus (toKit).
// About, Program options and Exit are in the application menu on macOS (About StarDraft 2, Program options, Quit).
std::vector<MacMenuItem> macItems(const MenuModel& model) {
    std::vector<MacMenuItem> out;
    for (const MenuItem& it : model) {
        MacMenuItem m;
        switch (it.kind) {
        case MenuItem::Kind::Separator: m.separator = true; break;
        case MenuItem::Kind::Popup:
            m.title = narrow(accessFree(it.label));
            m.sub = it.recent == "maps" ? macRecent(false) : it.recent == "projects" ? macRecent(true) : macItems(it.items);
            if (m.sub.empty()) continue;
            break;
        case MenuItem::Kind::Command: {
            if (it.id == ID_FILE_EXIT || it.id == ID_OPT_WINDOW || it.id == ID_HELP_ABOUT) continue;
            m.title = narrow(accessFree(it.label)), m.tag = it.id;
            const stardraft::Command* tc = tableCommand(it.id);
            m.checked = it.checked || (tc && tc->isChecked()) || mapMenuCheck(it.id) == 1;
            m.enabled = !it.grayed && sdlEnabled(it.id);
            if (!macKeyOf(platformShortcut(it.id, it.shortcut), m.key, m.mods)) m.key.clear(), m.mods = 0;
            // A key with no modifier (S: Sprites mode, V B A F, Q, Del) is not the system menu's: the menu bar takes it
            // before the window, also while a text field or the quick picker types. The window's keys run it
            // (shortcutRun), only when nothing types. (The F keys stay.)
            if (!m.mods && !m.key.empty() && !(static_cast<unsigned char>(m.key[0]) == 0xEF && m.key.size() == 3 && m.key >= utf8Of(0xF704) && m.key <= utf8Of(0xF726)))
                m.key.clear();
            break;
        }
        }
        out.push_back(std::move(m));
    }
    macTidy(out);
    return out;
}

// The kit's menu items as the system's: each item's tag is its place in `flat` (a submenu's items too).
std::vector<MacMenuItem> macPopupItems(const std::vector<kit::MenuItem>& items, std::vector<const kit::MenuItem*>& flat) {
    std::vector<MacMenuItem> out;
    for (const kit::MenuItem& it : items) {
        MacMenuItem m;
        if (it.separator) m.separator = true;
        else {
            m.title = narrow(it.label), m.checked = it.checked, m.enabled = it.enabled;
            if (!it.sub.empty()) m.sub = macPopupItems(it.sub, flat);
            else {
                m.tag = int(flat.size());
                flat.push_back(&it);
                if (!macKeyOf(it.shortcut, m.key, m.mods)) m.key.clear(), m.mods = 0;
            }
        }
        out.push_back(std::move(m));
    }
    macTidy(out);
    return out;
}

// True while the program waits for a dialog: a kit box (the host's modal loop), or a system dialog or a sheet on a
// window (macDialogOpen). The posted work runs while a sheet waits, so the menu bar's commands must not.
bool macMenusWait() { return (gHost && gHost->modalOpen()) || macDialogOpen(); }

// A pick of the menu bar: a command, or a tab of the Window menu (after the menu closed: posted). Not while a dialog
// is open (macMenusWait): the program waits for it, as the window's menus do. The posted pick looks again: a dialog
// can open before it runs.
void macMenuPick(int tag) {
    if (macMenusWait()) return;
    shellPost([tag] {
        if (macMenusWait()) return;
        if (tag <= kMacTabTag) {
            const int i = kMacTabTag - tag;
            if (i >= 0 && i < int(gDocs.size()) && i != gActive) activateDoc(i);
            gMenusDirty = true;
            return;
        }
        command(tag);
    });
}

std::string macSignature(const std::vector<MacMenuItem>& items) {
    std::string s;
    for (const MacMenuItem& m : items)
        s += m.title + '\x1f' + m.key + char('0' + m.mods) + char('0' + m.checked) + char('0' + m.enabled) + char('0' + m.separator) +
             std::to_string(m.tag) + '{' + macSignature(m.sub) + "}\x1e";
    return s;
}

// The Window menu's tabs: each open tab's title (the active one checked).
std::vector<MacMenuItem> macTabs() {
    std::vector<MacMenuItem> tabs;
    for (int i = 0; i < int(gDocs.size()); ++i) {
        MacMenuItem m;
        m.title = narrow(docTitle(i)), m.tag = kMacTabTag - i, m.checked = i == gActive;
        tabs.push_back(std::move(m));
    }
    return tabs;
}

#endif

// The menu bar that the system has now (macSignature of its items): the same items are not made again.
std::string gMacMenuSig;
void sdlNativeMenusReset() { gMacMenuSig.clear(); }

// Makes the system's menu bar from the model (when it is the system's). False: a menu is open now; try again later.
bool sdlNativeMenusRebuild(const MenuModel& model) {
#ifdef __APPLE__
    if (!gNativeMenuBar) return true;
    if (macMenuOpen()) return false;
    std::vector<MacMenuItem> menus;
    for (const MenuItem& top : model) {
        MacMenuItem m;
        m.title = narrow(accessFree(top.label)), m.sub = macItems(top.items);
        menus.push_back(std::move(m));
    }
    const std::vector<MacMenuItem> tabs = macTabs();
    MacMenuTexts t;
    auto text = [](const wchar_t* s) { return narrow(accessFree(s)); };
    t.app = "StarDraft 2", t.about = text(L"&About StarDraft 2"), t.settings = text(L"&Program options\x2026"), t.services = text(L"Services");
    t.hide = text(L"Hide StarDraft 2"), t.hideOthers = text(L"Hide others"), t.showAll = text(L"Show all"), t.quit = text(L"Quit StarDraft 2");
    t.window = text(L"Window"), t.minimize = text(L"Minimize"), t.zoom = text(L"Zoom"), t.nextTab = text(L"Next tab");
    t.bringAll = text(L"Bring all to front");
    const std::string sig = macSignature(menus) + '\x1d' + macSignature(tabs) + '\x1d' + t.about + t.settings + t.hide + t.window;
    if (sig == gMacMenuSig) return true;
    macSetMenuBar(t, ID_HELP_ABOUT, ID_OPT_WINDOW, ID_TAB_NEXT, menus, tabs, macMenuPick);
    gMacMenuSig = sig;
#else
    (void)model;
#endif
    return true;
}

// The frame's check of the Window menu's tabs: a tab that opened, closed, was renamed or became active makes the menus
// again (the tab strip does not tell the menus).
void sdlNativeMenusPoll() {
#ifdef __APPLE__
    if (!gNativeMenuBar) return;
    static std::wstring last;
    std::wstring now = std::to_wstring(gActive);
    for (int i = 0; i < int(gDocs.size()); ++i) now += L'\x1f' + docTitle(i);
    if (now != last) last = now, gMenusDirty = true;
#endif
}

// The system's menus from here on (macOS): the menu bar (not in a snapshot, except with --check menus) and, with
// `popups`, the context menus (a run with a window).
void sdlUseNativeMenus(bool popups) {
#ifdef __APPLE__
    gNativeMenuBar = true;
    gMenusDirty = true;
    if (popups)
        kit::Menu::setNativeHook([](draw::View&, draw::Point, const std::vector<kit::MenuItem>& items, const kit::MenuItem*& picked) {
            std::vector<const kit::MenuItem*> flat;
            const std::vector<MacMenuItem> mac = macPopupItems(items, flat);
            const int tag = macPopupMenu(mac);
            picked = tag >= 0 && tag < int(flat.size()) ? flat[size_t(tag)] : nullptr;
            return true;
        });
#else
    (void)popups;
#endif
}

// --check menus (after a snapshot): the menu bar's items as text, one line each: "menubar: File > Save [cmd+S]" from
// the system's menu bar (macOS), or "menubar: File > Save [Ctrl+S]" from the window's. Then how a tooltip shows the
// shortcuts ("tip: ...").
void sdlMenuCheckLines(const std::vector<kit::MenuItem>& items, const std::string& path) {
    for (const kit::MenuItem& it : items) {
        if (it.separator) continue;
        const std::string at = path + " > " + narrow(it.label);
        if (!it.sub.empty()) {
            sdlMenuCheckLines(it.sub, at);
            continue;
        }
        std::string line = "menubar: " + at;
        if (!it.shortcut.empty()) line += " [" + narrow(it.shortcut) + "]";
        if (!it.enabled) line += " (gray)";
        if (it.checked) line += " (checked)";
        std::printf("%s\n", line.c_str());
    }
}
void sdlMenuCheck() {
    rebuildMenus();
#ifdef __APPLE__
    if (gNativeMenuBar) {
        const std::vector<std::string> lines = macMenuBarLines();
        std::printf("menubar: the system's (%zu lines)%s\n", lines.size(), lines.empty() ? " (WRONG)" : "");
        for (const std::string& l : lines) std::printf("menubar: %s\n", l.c_str());
    }
#endif
    if (!gNativeMenuBar && gRoot) {
        std::printf("menubar: the window's (%s)\n", gRoot->menus.visible() ? "shown" : "hidden (WRONG)");
        for (const kit::MenuGroup& g : gRoot->menus.menus()) {
            std::printf("menubar: %s\n", narrow(g.label).c_str());
            sdlMenuCheckLines(g.items, narrow(g.label));
        }
    } else if (gRoot && gRoot->menus.visible()) {
        std::printf("menubar: the window's menu bar shows too (WRONG)\n");
    }
    std::printf("tip: %s\n", narrow(shellKeysText(L"Zoom in (Ctrl+Plus); Ctrl+Shift+S; Ctrl+click; Alt+drag; Del")).c_str());
}
