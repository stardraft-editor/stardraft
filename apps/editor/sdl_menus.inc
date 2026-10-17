// The SDL shell's menus, keys and commands (research/ports/MAC-BUILD-PLAN.md steps 5 and 6). The menu bar is the core's menu
// model (buildMenuModel, core/menu_model.inc) shown as kit menus (macOS: the system's menu bar, sdl_native_menus.inc);
// a click or a key runs command(id), the core's command path first. An item the SDL program cannot run yet is grayed,
// and its key does nothing. Ctrl in a shortcut is Cmd on macOS (platformShortcut: its own keys there). File > Open, Save and Save as use the system's file dialogs: they answer later, on another thread, so their
// result comes back to the UI thread through shellPost. Quit asks about unsaved changes (the save question, shellAskSaveChanges: the core message box).
// Included by main_sdl.cpp after sdl_pending.inc, before shell_sdl.inc.

#include "menu.h"

// The menus are made again on the UI thread when the state changed (shellMenusChanged) and no menu is open.
bool gMenusDirty = true;
// The shortcuts of the menu items (the menu model's keys), each with its command.
struct Shortcut {
    stardraft::KeyChord chord;
    int id = 0;
};
std::vector<Shortcut> gShortcuts;

// The Ctrl of a shortcut: Cmd on macOS.
#ifdef __APPLE__
constexpr unsigned kShortcutCtrl = draw::kSuper;
#else
constexpr unsigned kShortcutCtrl = draw::kCtrl;
#endif

// The shortcuts in a text as this system shows them. On macOS: the Mac symbols, in the Mac order (Control, Option,
// Shift, Command), with Cmd where Windows has Ctrl and Option where it has Alt: "Ctrl+Shift+S" is "⇧⌘S", "Del" "⌦",
// "Ctrl+click" "⌘-click". `whole`: the text is one shortcut (a menu's), so a key with no modifier changes too.
// Elsewhere the text stays as it is.
std::wstring shellKeysText(const std::wstring& s, bool whole) {
#ifdef __APPLE__
    static const std::pair<const wchar_t*, const wchar_t*> kKeys[] = {
        {L"mouse wheel", nullptr}, {L"wheel", nullptr}, {L"click", nullptr}, {L"drag", nullptr}, {L"Backspace", L"\x232B"}, {L"Plus", L"+"},
        {L"Minus", L"-"}, {L"Del", L"\x2326"}, {L"Tab", L"\x21E5"}, {L"Enter", L"\x21A9"}, {L"Esc", L"\x238B"}, {L"Home", L"\x2196"},
        {L"End", L"\x2198"}, {L"PgUp", L"\x21DE"}, {L"PgDn", L"\x21DF"}, {L"Left", L"\x2190"}, {L"Right", L"\x2192"}, {L"Up", L"\x2191"},
        {L"Down", L"\x2193"}, {L"Space", L"Space"}};
    auto word = [](wchar_t c) { return std::iswalnum(wint_t(c)) != 0; };
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        if (i == 0 || !word(s[i - 1])) {
            bool ctrl = false, shift = false, alt = false;
            size_t j = i;
            for (bool more = true; more;) {
                more = false;
                for (auto [name, flag] : {std::pair{L"Ctrl+", &ctrl}, std::pair{L"Cmd+", &ctrl}, std::pair{L"Shift+", &shift}, std::pair{L"Alt+", &alt}})
                    if (s.compare(j, std::wcslen(name), name) == 0 && j + std::wcslen(name) < s.size()) j += std::wcslen(name), *flag = more = true;
            }
            if (ctrl || shift || alt || whole) {
                std::wstring mods = std::wstring(alt ? L"\x2325" : L"") + (shift ? L"\x21E7" : L"") + (ctrl ? L"\x2318" : L"");
                std::wstring key;
                size_t end = j;
                for (const auto& [name, symbol] : kKeys) {
                    const size_t n = std::wcslen(name);
                    if (s.compare(j, n, name) == 0 && (j + n == s.size() || !word(s[j + n]))) {
                        if (!symbol && mods.empty()) break;  // (a "click" with no modifier is a word)
                        key = symbol ? std::wstring(symbol) : L"-" + std::wstring(name), end = j + n;
                        break;
                    }
                }
                if (key.empty() && j < s.size() && s[j] == L'F' && j + 1 < s.size() && std::iswdigit(wint_t(s[j + 1]))) {
                    end = j + 1;
                    while (end < s.size() && std::iswdigit(wint_t(s[end]))) ++end;
                    if (end < s.size() && word(s[end])) end = j;
                    else key = s.substr(j, end - j);
                }
                if (key.empty() && !mods.empty() && j < s.size() && s[j] > L' ' && (j + 1 == s.size() || !word(s[j + 1]) || !word(s[j])))
                    key = std::wstring(1, wchar_t(std::towupper(wint_t(s[j])))), end = j + 1;
                if (!key.empty()) {
                    out += mods + key;
                    i = end;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
#else
    (void)whole;
    return s;
#endif
}
std::wstring shellKeysText(const std::wstring& s) { return shellKeysText(s, false); }

// A shortcut as the menus show it (shellKeysText: the Mac symbols on macOS).
std::wstring shortcutText(const std::wstring& s) { return shellKeysText(s, true); }

// A menu command's shortcut on this system. macOS: Redo is Cmd+Shift+Z, Delete is Cmd+Delete (the Delete key of a Mac
// keyboard is Backspace), and Map properties has none (Cmd+M is the Window menu's Minimize).
std::wstring platformShortcut(int id, const std::wstring& s) {
#ifdef __APPLE__
    if (id == ID_EDIT_REDO) return L"Ctrl+Shift+Z";
    if (id == ID_EDIT_DELETE) return L"Ctrl+Backspace";
    if (id == ID_SCEN_PROPS) return {};
#else
    (void)id;
#endif
    return s;
}

// The view system's key of a shortcut's key name ("S", "0", "+", "Del", "F2"); false when the name is no key.
bool keyOfChord(const std::string& name, draw::Key& out) {
    if (name.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(name[0]);
        if (std::isalnum(c)) {
            out = static_cast<draw::Key>(std::toupper(c));
            return true;
        }
        switch (c) {
        case '+': out = draw::Key::Plus; return true;
        case '-': out = draw::Key::Minus; return true;
        case ',': out = draw::Key::Comma; return true;
        case '.': out = draw::Key::Period; return true;
        case '/': out = draw::Key::Slash; return true;
        case '`': out = draw::Key::Grave; return true;
        case '[': out = draw::Key::LeftBracket; return true;
        case ']': out = draw::Key::RightBracket; return true;
        case ' ': out = draw::Key::Space; return true;
        default: return false;
        }
    }
    if (name == "Del") out = draw::Key::Delete;
    else if (name == "Backspace") out = draw::Key::Backspace;
    else if (name == "Tab") out = draw::Key::Tab;
    else if (name == "Home") out = draw::Key::Home;
    else if (name == "End") out = draw::Key::End;
    else if (name == "Enter") out = draw::Key::Enter;
    else if (name == "Esc") out = draw::Key::Escape;
    else if (name == "Left") out = draw::Key::Left;  // (the arrows: --key in a snapshot)
    else if (name == "Right") out = draw::Key::Right;
    else if (name == "Up") out = draw::Key::Up;
    else if (name == "Down") out = draw::Key::Down;
    else if (name.size() >= 2 && name[0] == 'F' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        const int n = std::atoi(name.c_str() + 1);
        if (n < 1 || n > 12) return false;
        out = static_cast<draw::Key>(int(draw::Key::F1) + n - 1);
    } else {
        return false;
    }
    return true;
}

// On macOS the Control key is no shortcut's key (a shortcut's Ctrl is Cmd there): Control+T is not T. (The host gives
// Cmd as kSuper and also as kCtrl, so that the views' Ctrl keys are Cmd: the Control key itself is SDL's modifier state.)
bool chordIs(const stardraft::KeyChord& c, const draw::KeyEvent& e) {
    draw::Key k{};
    if (!keyOfChord(c.key, k) || k != e.key) return false;
#ifdef __APPLE__
    if (SDL_GetModState() & SDL_KMOD_CTRL) return false;
#endif
    return c.ctrl == ((e.mods & kShortcutCtrl) != 0) && c.shift == ((e.mods & draw::kShift) != 0) && c.alt == ((e.mods & draw::kAlt) != 0);
}

// The command of the table with this native id (nullptr: the item is not in the table).
const stardraft::Command* tableCommand(int id) {
    const auto it = gCommandIds.find(id);
    return it == gCommandIds.end() ? nullptr : gCommands.find(it->second);
}

bool sdlDataUndo(bool redo);  // (sdl_data.inc: undo and redo in the Data workspace)

bool sdlOptionsRuns(int id);     // (sdl_options.inc: the Program options commands)
bool sdlOptionsCommand(int id);

// Whether the SDL program runs the command now. The others are grayed, and their keys do nothing.
bool sdlUpdatesRuns(int id);  // (sdl_updates.inc: Look for updates on start, Crash reports, Maps)
bool sdlRuns(int id) {
    if (sdlOptionsRuns(id) || sdlUpdatesRuns(id)) return true;
    switch (id) {
    case ID_FILE_OPEN:
    case ID_FILE_SAVE:
    case ID_FILE_SAVEAS:
    case ID_FILE_EXIT:
    case ID_EDIT_UNDO:
    case ID_EDIT_REDO:
    case ID_WS_MAP:
    case ID_WS_DATA:
    case ID_WS_TRIGGERS:
    case ID_WS_ASSETS:
    case ID_WS_PROJECT:
    case ID_WS_BRIEFING:
    case ID_WS_FILES:
    case ID_SCEN_PROPS:  // the map's pages: the rail and the menus open them as the workspaces do
    case ID_SCEN_PLAYERS:
    case ID_SCEN_CLASSIC:
    case ID_SCEN_CLASSICTRIG:
    case ID_FILE_NEW:  // the New page (sdl_new_page.inc) and the program options / feedback (sdl_options.inc)
    case ID_PROJ_NEW:
    case ID_PROJ_OPEN:  // the project commands (sdl_project.inc)
    case ID_PROJ_FROMMAP:
    case ID_PROJ_ADDSCRIPT:
    case ID_PROJ_FOLDER:
    case ID_PROJ_SETTINGS:
    case ID_OPT_WINDOW:
    case ID_HELP_FEEDBACK:
    case ID_HELP_UPDATES:  // Check for updates (sdl_updates.inc)
    case ID_EDIT_RENAME:  // the location's rename and the quick picker (sdl_map_dialogs.inc)
    case ID_QUICKPICK: return true;
    case ID_HELP_ABOUT:  // (sdl_options.inc) and the links
    case ID_VIEW_KIT:
    case ID_HELP_DISCORD:
    case ID_HELP_GITHUB: return true;
    case ID_PROJ_COMPILE:  // Build (core/project_build.inc), the Log, the StarCraft folder and the shell's own commands below
    case ID_VIEW_LOG:
    case ID_OPT_INSTALL:
    case ID_SCEN_CHECK:
    case ID_RECENT_CLEAR_MAPS:
    case ID_RECENT_CLEAR_PROJECTS:
    case ID_DOCK_MINI_LEFT:
    case ID_DOCK_MINI_RIGHT:
    case ID_DOCK_TOOLS_LEFT:
    case ID_DOCK_TOOLS_RIGHT: return true;
    case ID_VIEW_TRIGCODE: return true;  // the Triggers workspace's code pane (sdl_triggers.inc)
    case ID_QUICKOPEN:  // the core's commands (core/commands_core.inc, core/map_edit_commands.inc)
    case ID_FILE_CLOSE:
    case ID_FILE_CLOSEALL:
    case ID_TAB_NEXT:
    case ID_TAB_PREV:
    case ID_HELP_ISSUE:
    case ID_EDIT_CUT:
    case ID_EDIT_COPY:
    case ID_EDIT_PASTE:
    case ID_EDIT_DUPLICATE:
    case ID_EDIT_DELETE:
    case ID_EDIT_SELECTALL:
    case ID_EDIT_TERRAINUNITS:
    case ID_EDIT_TERRAINDOODADS:
    case ID_OPT_LOCWRAP:
    case ID_OPT_STRICT:
    case ID_OPT_SNAPVALID:
    case ID_OPT_ILLEGAL:
    case ID_OPT_STACK:
    case ID_OPT_SNAPBUILDINGS:
    case ID_OPT_DRAGBACK:
    case ID_OPT_ANIMATE:
    case ID_DOODAD_FIT:
    case ID_DOODAD_NEAREST:
    case ID_DOODAD_ANYWHERE:
    case ID_GRID_CUSTOM:
    case ID_VIEW_TOWNS:  // View > Show town areas, and the other pathing overlays (core/path_overlay.inc)
    case ID_VIEW_WALK:
    case ID_VIEW_PATHS:
    case ID_PATHSIZE_SMALL:
    case ID_PATHSIZE_MEDIUM:
    case ID_PATHSIZE_LARGE: return true;
    default: break;
    }
    if (id >= ID_PLAYER_1 && id < ID_PLAYER_1 + 10) return true;  // the keys 1 to 0
    if (id >= ID_TOOL_KEY && id < ID_TOOL_KEY + 4) return true;   // V, B, A, F
    if (id >= ID_RAIL_KEY && id < ID_RAIL_KEY + 9) return true;   // Ctrl+1 to Ctrl+9
    if (id >= ID_SNAP_UNITS && id < ID_SNAP_UNITS + 12) return true;
    if (id >= ID_GRID_SIZE && id < ID_GRID_SIZE + int(std::size(kGridPresets))) return true;
    if (id >= ID_RECENT_MAP && id < ID_RECENT_MAP + kRecentMax) return true;
    if (id >= ID_RECENT_PROJECT && id < ID_RECENT_PROJECT + kRecentMax) return true;
    return gCommandIds.count(id) != 0;  // the Map workspace's table (registerMapCommands)
}

// Whether the item is enabled now: it runs, and it is there for the open tab (a map, its mode).
bool sdlEnabled(int id) {
    if (!sdlRuns(id)) return false;
    if (mapCommand(id) && !docOpen()) return false;
    if (!editInWorkspace(id)) return false;  // (the map's Edit commands in another workspace: core/commands_core.inc)
    // The start page or the New page over the open map (File > New, the tabs' +): the map's commands are gray, as on the
    // Windows program's page tabs (a Delete or Paste would change the map nobody sees).
    if (mapCommand(id) && (gStartOpen || gNewOpen) && gWorkspace == Workspace::Map) return false;
    // ...and the other workspaces (the side bar's items, Ctrl+1 to 9, View and Scenario's pages): the page stays until
    // Create or Cancel.
    if (newPageCovers() && ((id > ID_WS_MAP && id <= ID_WS_FILES) || id == ID_SCEN_PROPS || id == ID_SCEN_PLAYERS || id == ID_SCEN_CLASSIC ||
                            id == ID_SCEN_CLASSICTRIG))
        return false;
    if (id == ID_PROJ_COMPILE && gBuilding) return false;  // (a build runs)
    if (mapMenuGrayed(id)) return false;                    // (Paste with no copy: core/map_edit_commands.inc)
    return commandForMode(id, projectMode());
}

// The top tool bar's icons are enabled as the menu items are (core/views_topbar.inc).
bool commandEnabled(int id) { return sdlEnabled(id); }

// The menu item's text in the program's language (the tables have it with its access keys, as the Windows menus add
// it), without the access keys ('&').
std::wstring accessFree(std::wstring s) {
    s = tr(s);
    std::wstring out;  // (&& is an & of the text)
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] != L'&') out += s[i];
        else if (i + 1 < s.size() && s[i + 1] == L'&') out += L'&', ++i;
    return out;
}

// What "Report feedback on ..." names at `at` of `from` (the Windows feedbackThing): the name of the deepest view
// there that has one (its feedbackName, else its tip), else `pane`, else the workspace.
std::wstring sdlFeedbackOn(draw::View& from, draw::Point at, const std::wstring& pane) {
    draw::Point local;
    for (draw::View* v = from.viewAt(at, local); v; v = v == &from ? nullptr : v->parent()) {
        const draw::Point o = v->origin(), f = from.origin();
        if (std::wstring t = v->feedbackName({at.x + f.x - o.x, at.y + f.y - o.y}); !t.empty()) return feedbackShort(t);
    }
    return pane.empty() ? feedbackWorkspaceName(gWorkspace) : pane;
}

void showFeedbackSdl(const std::wstring& on);  // (sdl_options.inc)
void showFeedbackSdl(const std::wstring& on, draw::View* owner);

// --rclick in a snapshot (kit::Menu's test hook): the items of the menu that opens next go to the output ("menu:
// <label>"). With gMenuShotPick n >= 0, item n runs and the menu does not open; with -1, the menu opens and the
// snapshot shows it (the host paints it over the window), and gMenuShotOpen says so. -2: no snapshot click.
int gMenuShotPick = -2;
bool gMenuShotOpen = false;
bool sdlMenuShot(const std::vector<kit::MenuItem>& items) {
    if (gMenuShotPick == -2) return false;
    for (const kit::MenuItem& it : items) std::printf("menu: %s%s\n", it.separator ? "-" : narrow(it.label).c_str(), it.checked ? " (checked)" : "");
    const int n = std::exchange(gMenuShotPick, -2);
    if (n < 0) return gMenuShotOpen = true, false;
    if (n < int(items.size()) && items[size_t(n)].enabled && items[size_t(n)].run) items[size_t(n)].run();
    return true;
}

// "Report feedback on <on>..." at the end of a context menu, as every Windows context menu ends (popupMenu).
// owner: the root view of the window the menu opens in (the feedback dialog opens over it); null: the main window.
void sdlAddFeedbackItem(std::vector<kit::MenuItem>& items, const std::wstring& on, draw::View* owner = nullptr) {
    if (!items.empty()) items.push_back(kit::MenuItem{}), items.back().separator = true;
    kit::MenuItem fb;
    fb.label = shellTr(L"Report feedback on ") + on + L"\x2026";
    fb.icon = int(kFeedbackIcon);
    fb.iconColor = draw::Color(kFeedback);  // (as the Windows menus' feedbackBitmap)
    fb.run = [on, owner] { showFeedbackSdl(on, owner); };
    items.push_back(std::move(fb));
}
// A context menu (the Windows popupMenu): `items` at `at` of `from`, then the feedback item (on: sdlFeedbackOn).
bool sdlPopupMenu(draw::View& from, draw::Point at, std::vector<kit::MenuItem> items, const std::wstring& on, kit::MenuOptions opts) {
    draw::View* owner = &from;  // the window's root: the feedback dialog opens over that window (a dialog, a pop-out)
    while (owner->parent()) owner = owner->parent();
    sdlAddFeedbackItem(items, on, owner);
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    return kit::Menu::popup(from, at, items, kit::Menu::measure(items, &measure), std::move(opts));
}

// A right click that no view took in a dialog or a pop-out (Program options, About, the block help, a value's
// pop-out...; SdlHost's right-click hook): a menu with only "Report feedback on ...", as the Windows program's
// feedbackAfterRightClick, about the view there, else the window's title, else the workspace. Not when the click
// opened a menu, not on a text field (it has its own menu), not in the feedback dialog itself.
extern FeedbackView* gFbView;  // (sdl_options.inc)
void sdlWindowRightClick(draw::View& root, draw::Point at, const std::wstring& title) {
    if (kit::Menu::open() || (gFbView && &root == static_cast<draw::View*>(gFbView))) return;
    draw::Point local;
    for (draw::View* v = root.viewAt(at, local); v; v = v->parent())
        if (dynamic_cast<kit::TextField*>(v)) return;
    sdlPopupMenu(root, at, {}, sdlFeedbackOn(root, at, title.empty() ? std::wstring() : std::wstring(shellTr(title.c_str()))));
}

// A core menu model as kit items (a pick runs `pick` with the item's id; a check, a gray item as the model has them).
std::vector<kit::MenuItem> sdlModelItems(const MenuModel& model, std::function<void(int)> pick) {
    std::vector<kit::MenuItem> out;
    for (const MenuItem& it : model) {
        kit::MenuItem k;
        if (it.kind == MenuItem::Kind::Separator) k.separator = true;
        else {
            k.label = accessFree(it.label), k.shortcut = it.shortcut, k.checked = it.checked, k.enabled = !it.grayed && it.id != 0;
            const int id = it.id;
            k.run = [pick, id] { pick(id); };
        }
        out.push_back(std::move(k));
    }
    return out;
}

std::vector<kit::MenuItem> recentItems(bool projects) {
    const std::vector<std::wstring>& list = projects ? gRecentProjects : gRecentMaps;
    const int base = projects ? ID_RECENT_PROJECT : ID_RECENT_MAP;
    std::vector<kit::MenuItem> out;
    for (size_t i = 0; i < list.size() && i < size_t(kRecentMax); ++i) {
        kit::MenuItem k;
        k.label = list[i];
        const int id = base + int(i);
        k.run = [id] { command(id); };
        out.push_back(std::move(k));
    }
    if (out.empty()) {
        kit::MenuItem none;
        none.label = L"None";
        none.enabled = false;
        out.push_back(std::move(none));
    }
    return out;
}

// The core's menu model as kit items: a popup's items, a command's text, check and enabled state and its run.
std::vector<kit::MenuItem> toKit(const MenuModel& items) {
    std::vector<kit::MenuItem> out;
    for (const MenuItem& it : items) {
        kit::MenuItem k;
        switch (it.kind) {
        case MenuItem::Kind::Separator: k.separator = true; break;
        case MenuItem::Kind::Popup:
            k.label = accessFree(it.label);
            if (it.recent == "maps") k.sub = recentItems(false);
            else if (it.recent == "projects") k.sub = recentItems(true);
            else k.sub = toKit(it.items);
            break;
        case MenuItem::Kind::Command: {
            k.label = accessFree(it.label), k.shortcut = platformShortcut(it.id, it.shortcut);  // (the menus show it with shortcutText)
            const stardraft::Command* tc = tableCommand(it.id);
            k.checked = it.checked || (tc && tc->isChecked()) || mapMenuCheck(it.id) == 1;
            k.enabled = !it.grayed && sdlEnabled(it.id);
            const int id = it.id;
            k.run = [id] { command(id); };
            break;
        }
        }
        out.push_back(std::move(k));
    }
    return out;
}

// The shortcuts of the model's commands (a popup's items too).
void collectShortcuts(const MenuModel& items) {
    for (const MenuItem& it : items) {
        if (it.kind == MenuItem::Kind::Popup) collectShortcuts(it.items);
        else if (it.kind == MenuItem::Kind::Command && !it.shortcut.empty()) {
            // The item's key, and this system's when it has another (macOS: Cmd+Delete as well as Del; none for a
            // key that the system has, Cmd+M).
            const std::wstring own = platformShortcut(it.id, it.shortcut);
            if (own.empty()) continue;
            if (const auto c = stardraft::parseShortcut(narrow(it.shortcut))) gShortcuts.push_back({*c, it.id});
            if (own != it.shortcut)
                if (const auto c = stardraft::parseShortcut(narrow(own))) gShortcuts.push_back({*c, it.id});
        }
    }
}

bool sdlNativeMenusRebuild(const MenuModel& model);  // (sdl_native_menus.inc: the system's menu bar on macOS)
void sdlNativeMenusPoll();

// Makes the menu bar again from the model (its checks, its enabled items, the recent lists): the window's, and the
// system's when the program uses it (macOS).
void rebuildMenus() {
    if (!gRoot) return;
    const MenuModel model = buildMenuModel();
    gShortcuts.clear();
    collectShortcuts(model);
    std::vector<kit::MenuGroup> groups;
    for (const MenuItem& top : model) groups.push_back(kit::MenuGroup{accessFree(top.label), toKit(top.items)});
    gRoot->menus.setMenus(std::move(groups));
    gMenusDirty = !sdlNativeMenusRebuild(model);  // (a system menu that is open: again on a later frame)
}

// The frame hook's part: the menus are made again when they changed and none is open (an open menu keeps its items).
void menusIfDirty() {
    // Undo, Redo and Paste follow the edits (an edit does not say that the menus changed): the menus are made again
    // when one of them goes gray or comes back.
    static int grayed = -1;
    const int now = int(mapMenuGrayed(ID_EDIT_UNDO)) | int(mapMenuGrayed(ID_EDIT_REDO)) << 1 | int(mapMenuGrayed(ID_EDIT_PASTE)) << 2;
    if (now != grayed) grayed = now, gMenusDirty = true;
    sdlNativeMenusPoll();  // (the Window menu's tabs)
    if (gMenusDirty && gRoot && gRoot->menus.openIndex() < 0) rebuildMenus();
}

// A key that a shortcut runs (the window's keys, after the views that take them). True when one ran.
bool shortcutRun(const draw::KeyEvent& e) {
    if (!e.down || e.repeat) return false;
    // The program's keys first (core/map_edit_commands.inc: Ctrl+2 is the side bar's second item, as on Windows), then
    // the menus' other keys.
    for (const ProgramKey& k : kProgramKeys)
        if (const auto c = stardraft::parseShortcut(k.chord); c && chordIs(*c, e) && sdlEnabled(k.id)) {
            command(k.id);
            return true;
        }
    for (const Shortcut& s : gShortcuts)
        if (chordIs(s.chord, e) && sdlEnabled(s.id)) {
            command(s.id);
            return true;
        }
    return false;
}

// A key pressed in the window (main_sdl.cpp's --key, for a snapshot): "Ctrl+A", "5", "Ctrl+Shift+Z" (Ctrl is Cmd on macOS).
// --rclick x,y[:n]: the right button pressed and let go at (x, y) of the window (the panes' own handling), then the
// posted work; the menu that opens shows its items in the output and runs item n (sdlMenuShot), or with no n stays
// open in the picture. `in`: the snapshot's view (the window, or a dialog of --dialog with its title: a click that no
// view takes there gets the dialogs' Report feedback menu, sdlWindowRightClick).
void sdlRightClickShot(const std::string& spec, draw::View* in, const std::wstring& title) {
    int x = 0, y = 0, n = -1;
    draw::View* target = in ? in : gRoot;
    if (!target || std::sscanf(spec.c_str(), "%d,%d:%d", &x, &y, &n) < 2) return void(std::fprintf(stderr, "--rclick %s: not x,y[:n]\n", spec.c_str()));
    kit::Menu::setTestHook(sdlMenuShot);  // (every kit menu: a text field's too)
    target->attach(gHost);  // the views are in the host while the click runs: a menu that opens shows in the picture
    draw::Router router(*target);
    draw::MouseEvent e;
    e.button = 1, e.pt = {x, y};
    e.kind = draw::MouseKind::Down;
    gMenuShotPick = n;  // (before the press: a text field's menu opens on it)
    gMenuShotOpen = false;
    router.mouse(e);
    e.kind = draw::MouseKind::Up;
    if (!router.mouse(e) && target != gRoot) sdlWindowRightClick(*target, e.pt, title);
    runPosted();
    gMenuShotPick = -2;
    target->attach(nullptr);
}

void sdlInputLine(const std::string& what);

// The window that takes a snapshot's keys and text now (SdlHost::snapshotTop): a dialog or pop-out that an input
// opened, else the main window. (The mouse: SdlHost::snapshotMouse finds the window under the point.)
draw::View& sdlShotTarget(draw::Point& size) {
    size = {gRoot->width(), gRoot->height()};
    return gHost ? gHost->snapshotTop(*gRoot, size) : *gRoot;
}

// After a --key, --type or --ime: the view with the keyboard in the window that takes the keys now, on a line of its
// own: its kind and place (a dialog's Tab order), and for a text field its text, selection and composition, and
// whether the input method is on with its area at the caret (SDL_SetTextInputArea: the system's composition and
// candidate windows show there): "(WRONG)" when it is off or not at the caret.
bool gCaretCheck = false;  // --check caret (main_sdl.cpp)
void sdlFocusLine() {
    if (!gRoot || !gHost) return;
    draw::Point size;
    const draw::View& top = sdlShotTarget(size);
    gHost->snapshotLayout(top);  // (a field places its caret as it paints)
    const draw::View* f = gHost->focusedView(top);
    if (!f) return void(std::printf("  focus: none\n"));
    const draw::Point o = f->origin();
    std::string kind = "view", text;
    draw::Rect caret;
    bool typing = false;
    std::wstring comp;
    size_t a = 0, b = 0;
    if (const auto* t = dynamic_cast<const kit::TextField*>(f)) {
        kind = "text field", text = narrow(t->text()), caret = t->caretRect(), typing = true, comp = t->composition();
        a = t->model().selectionStart(), b = t->model().selectionEnd();
    } else if (const auto* r = dynamic_cast<const kit::RichField*>(f)) {
        kind = "rich field", text = narrow(r->plainText()), caret = r->caretRect(), typing = true, comp = r->composition();
        a = std::min(r->anchor(), r->caret()), b = std::max(r->anchor(), r->caret());
    } else if (const auto* bt = dynamic_cast<const kit::Button*>(f)) {
        kind = "button", text = narrow(bt->text());
    } else if (const auto* cb = dynamic_cast<const kit::ChoiceBox*>(f)) {
        kind = "check box", text = narrow(cb->label());
    } else if (dynamic_cast<const kit::Dropdown*>(f)) {
        kind = "dropdown";
    }
    std::string shown;  // (one line: a line end as \n)
    for (const char ch : text)
        if (ch == '\n') shown += "\\n";
        else if (ch != '\r') shown += ch;
    std::printf("  focus: %s at %d,%d \"%s\"", kind.c_str(), o.x, o.y, shown.c_str());
    if (gCaretCheck) {  // --check caret: the caret and the end of the drawn letters of its line (the caret at the end)
        int right = -1;
        if (const auto* t = dynamic_cast<const kit::TextField*>(f)) right = t->textRight();
        else if (const auto* r = dynamic_cast<const kit::RichField*>(f)) right = r->textRight();
        if (right >= 0)
            std::printf(" [caret at %d, the letters end at %d %s]", caret.left, right, std::abs(caret.left - right) <= 1 ? "(yes)" : "(WRONG)");
    }
    if (typing) {
        draw::Rect area;
        const bool on = gHost->textInputState(*f, area);
        const draw::Rect want(o.x + caret.left, o.y + caret.top, o.x + caret.right, o.y + caret.bottom);
        const bool at = area.left == want.left && area.top == want.top && area.bottom == want.bottom;
        std::printf(" selected %zu-%zu composing \"%s\"; input method %s at %d,%d-%d,%d, the caret at %d,%d-%d,%d %s", a, b,
                    narrow(comp).c_str(), on ? "on" : "off", area.left, area.top, area.right, area.bottom, want.left, want.top, want.right,
                    want.bottom, on && at ? "(yes)" : "(WRONG)");
    }
    std::printf("\n");
}

void sdlPressKey(const std::string& chord) {
    const auto c = stardraft::parseShortcut(chord);
    draw::KeyEvent e;
    if (!gRoot || !gHost || !c || !keyOfChord(c->key, e.key)) return void(std::fprintf(stderr, "--key %s: no such key\n", chord.c_str()));
    e.mods = (c->ctrl ? kShortcutCtrl : 0) | (c->shift ? draw::kShift : 0) | (c->alt ? draw::kAlt : 0);
    draw::Point size;
    draw::View& target = sdlShotTarget(size);
    gHost->snapshotKey(target, size.x, size.y, e);  // (the view with the focus first, then its parents: the window's keys)
    runPosted();
    sdlInputLine("key " + chord);
    sdlFocusLine();
    if (gStartView.shown()) std::printf("key %s: start page: keys on %s\n", chord.c_str(), gStartView.keysOn().c_str());
}

// --type <text> in a snapshot: the text typed into the view with the focus (a search box, a number entry), as the
// keyboard's text events give it.
void sdlTypeText(const std::string& text) {
    if (!gRoot || !gHost) return;
    draw::Point size;
    draw::View& target = sdlShotTarget(size);
    gHost->snapshotText(target, size.x, size.y, text);
    runPosted();
    sdlInputLine("type " + text);
    sdlFocusLine();
}

// --ime <text>[@<caret>] in a snapshot: an input method's composition (Korean, Chinese, Japanese) as SDL gives it
// (SDL_EVENT_TEXT_EDITING): the text is not committed; its caret is <caret> characters in (default: at its end). An
// empty text ends the composition (a cancel); --type commits it, as the input method's text event does.
void sdlImeText(const std::string& spec) {
    if (!gRoot || !gHost) return;
    std::string text = spec;
    int start = -1;
    if (const size_t at = spec.rfind('@'); at != std::string::npos && at + 1 < spec.size() &&
                                           spec.find_first_not_of("0123456789", at + 1) == std::string::npos)
        text = spec.substr(0, at), start = std::atoi(spec.c_str() + at + 1);
    if (start < 0) {  // (SDL counts the characters: the bytes that start one)
        start = 0;
        for (const char ch : text) start += (static_cast<unsigned char>(ch) & 0xC0) != 0x80;
    }
    draw::Point size;
    draw::View& target = sdlShotTarget(size);
    gHost->snapshotCompose(target, size.x, size.y, text, start);
    runPosted();
    sdlInputLine("ime " + spec);
    sdlFocusLine();
}

// The output line after a --key, --type, --click, --drag, --move or --wait (as the Windows self-test's log: Delete,
// then Undo, gives the units back): the units, the doodads, the selection (the first selected unit's place) and the
// status bar's text ("no map" in place of the counts when no map is open); then, on a line of its own, what the
// workspace holds (sdl_shot_state.inc: the map's parts, the Data entry's changed values, the selected trigger's
// blocks...). This is the only state line. Then a line "  ui: ..." with the window's own parts (sdl_shot_state.inc:
// sdlShotUi: the page shown, the side bars' widths, the cursor, the New page's choices).
std::string sdlShotState();
std::string sdlShotUi();
draw::Point gShotMouse{-1, -1};  // where the last --click, --drag or --move left the mouse (the ui line's cursor)
void sdlInputLine(const std::string& what) {
    if (!gState.ed || !gState.ed->isOpen()) {
        std::printf("%s: no map: %s\n", what.c_str(), narrow(gState.message).c_str());
    } else {
        const auto& units = gState.ed->units();
        const auto& sel = gState.ed->selection();
        std::string first;
        if (!sel.empty() && sel[0] >= 0 && size_t(sel[0]) < units.size())
            first = " (the first at " + std::to_string(units[size_t(sel[0])].x) + "," + std::to_string(units[size_t(sel[0])].y) + ")";
        std::printf("%s: %zu units, %zu doodads, %zu selected%s: %s\n", what.c_str(), units.size(), gState.ed->doodads().size(), sel.size(),
                    first.c_str(), narrow(gState.message).c_str());
    }
    if (const std::string state = sdlShotState(); !state.empty()) std::printf("  state: %s\n", state.c_str());
    std::printf("  ui: %s\n", sdlShotUi().c_str());
    if (gListPickShown) {  // a list picker is open: its search and the rows it shows
        const auto rows = gListPickShown->shownLabels();
        std::string s;
        for (size_t i = 0; i < rows.size() && i < 12; ++i) s += (i ? ", " : "") + narrow(rows[i]);
        std::printf("  picker: search \"%s\", %zu rows: %s\n", narrow(gListPickShown->searchField().text()).c_str(), rows.size(), s.c_str());
    }
}

void sdlMouseShot(const std::string& spec, char kind);
void sdlWheelShot(const std::string& spec);
void sdlPinchShot(const std::string& spec);
void sdlGestureShot(const std::string& spec, bool smartZoom);
std::string sdlScrollLine(int x, int y);
bool sdlScriptStep(char kind, const std::string& value);  // --script's own verbs (sdl_soak.inc)

// A snapshot's inputs (main_sdl.cpp: --key, --type, --click, --drag, --move, --wait; the option's letter k, t, c, d,
// m, w and its value) that have not run yet, in their order.
std::deque<std::pair<char, std::string>> gShotInputs;
int gShotInputCount = 0, gShotDeferredAt = 0;  // the inputs that ran; the input during which a dialog's answer waits (--pick defer:)
bool sdlShotDeferredAnswer();
bool sdlFileDialogOpen();
// Runs the next input (the main loop, or the loop of a modal window an input opened). False when there is none. A
// dialog's answer that waits (--pick defer:) comes after the input that follows the one that opened the dialog, or
// when there are no more inputs: its line is "wait dialog answer".
bool sdlShotInputNext() {
    if (sdlFileDialogOpen() && (gShotInputs.empty() || gShotInputCount >= gShotDeferredAt + 1) && sdlShotDeferredAnswer()) {
        runPosted();
        sdlInputLine("wait dialog answer");
        return true;
    }
    if (gShotInputs.empty()) return false;
    const auto [kind, value] = gShotInputs.front();
    gShotInputs.pop_front();
    ++gShotInputCount;
    if (sdlScriptStep(kind, value)) return true;
    if (kind == 'k') sdlPressKey(value);
    else if (kind == 't') sdlTypeText(value);
    else if (kind == 'i') sdlImeText(value);
    else if (kind == 'w') {  // --wait ms: the program's timers that are due in that time run (a page that reads the map again)
        if (gHost) gHost->snapshotTimers(std::atoi(value.c_str()));
        runPosted();
        sdlInputLine("wait " + value);
    }
    else if (kind == 'h') sdlWheelShot(value);
    else if (kind == 'p') sdlPinchShot(value);
    else if (kind == 'z' || kind == 'g') sdlGestureShot(value, kind == 'z');
    else sdlMouseShot(value, kind);  // 'c', 'd', 'l' or 'm'
    return true;
}

// --click x,y[:mods] and --drag x0,y0-x1,y1[:mods] in a snapshot (main_sdl.cpp): the left button as a user's mouse
// gives it, in the window's pixels: the mouse comes to the first point, the button goes down, (a drag: the mouse moves
// to the second point in steps, the button held), the button goes up. The events go the host's way for a window's
// (SdlHost::snapshotMouse), with the modifier keys held (Shift, Ctrl, Alt, joined with +; Ctrl is Cmd on macOS);
// Double in the mods: a double click (a second press and release, SDL's clicks 2). Repeat<n> in a click's mods: n quick
// clicks at the point, as SDL counts them (clicks 1, 2, ... n: the second and each next press is a double click to the
// views). Quick in a click's mods: the click comes quickly after the click before it (SDL's clicks 2, anywhere: the
// system counts it as a double click when it is near the first). Undo in a drag's mods: Ctrl+Z is pressed halfway,
// while the button is held. Right in a click's mods: the right button; Menu<n>: a right click, and the menu that opens
// runs its item n (its items go to the output, as --rclick's: sdlMenuShot).
// The points are the main window's, as a user's mouse is on the screen: a pop-out that an input opened takes the
// events when the first point is on it, and a press outside it closes it; a dialog takes them in its own pixels.
// --move x,y: only the mouse comes to the point (no button): a paste's copy follows it there. --lostdrag
// x0,y0-x1,y1[:mods]: a drag, but the window loses the focus (Alt+Tab) before the button goes up, and the release does
// not come (SDL_EVENT_WINDOW_FOCUS_LOST): the press ends there. `kind`: 'c', 'd', 'l' or 'm'.
void sdlMouseShot(const std::string& spec, char kind) {
    const bool lost = kind == 'l', drag = kind == 'd' || lost, moveOnly = kind == 'm';
    const std::string name = lost ? "lostdrag " : drag ? "drag " : moveOnly ? "move " : "click ";
    const size_t colon = spec.find(':');
    const std::string at = spec.substr(0, colon), mods = colon == std::string::npos ? "" : spec.substr(colon + 1);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const int got = drag ? std::sscanf(at.c_str(), "%d,%d-%d,%d", &x0, &y0, &x1, &y1) : std::sscanf(at.c_str(), "%d,%d", &x0, &y0);
    if (!gRoot || !gHost || got != (drag ? 4 : 2))
        return void(std::fprintf(stderr, "--%s%s: not %s[:mods]\n", name.c_str(), spec.c_str(), drag ? "x0,y0-x1,y1" : "x,y"));
    if (!drag) x1 = x0, y1 = y0;
    SDL_Keymod held = SDL_KMOD_NONE;
    bool twice = false, undoHalfway = false, right = false;
    int repeat = 1, menuPick = -2;
    Uint8 firstClicks = 1;
    std::stringstream in(mods);
    for (std::string m; std::getline(in, m, '+');) {
#ifdef __APPLE__
        if (m == "Ctrl") held = SDL_Keymod(held | SDL_KMOD_LGUI);
#else
        if (m == "Ctrl") held = SDL_Keymod(held | SDL_KMOD_LCTRL);
#endif
        else if (m == "Shift") held = SDL_Keymod(held | SDL_KMOD_LSHIFT);
        else if (m == "Alt") held = SDL_Keymod(held | SDL_KMOD_LALT);
        else if (m == "Double" && !drag) twice = true;
        else if (m == "Quick" && !drag && !moveOnly) firstClicks = 2;
        else if (m.rfind("Repeat", 0) == 0 && m.size() > 6 && !drag && !moveOnly) repeat = std::clamp(std::atoi(m.c_str() + 6), 1, 100);
        else if (m == "Undo" && drag) undoHalfway = true;
        else if (m == "Right" && !drag && !moveOnly) right = true;
        else if (m.rfind("Menu", 0) == 0 && m.size() > 4 && !drag && !moveOnly) right = true, menuPick = std::atoi(m.c_str() + 4);
        else return void(std::fprintf(stderr, "--%s%s: %s is not Shift, Ctrl, Alt, Double, Quick, Repeat<n>, Right, Menu<n> or Undo\n", name.c_str(), spec.c_str(), m.c_str()));
    }
    std::vector<SDL_Event> events;
    float px = float(x0), py = float(y0);
    auto move = [&](float x, float y, bool down) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = x, e.motion.y = y, e.motion.xrel = x - px, e.motion.yrel = y - py;
        e.motion.state = down ? SDL_BUTTON_LMASK : 0;
        events.push_back(e);
        px = x, py = y;
    };
    auto button = [&](bool down, float x, float y, Uint8 clicks = 1) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, e.button.down = down, e.button.clicks = clicks, e.button.x = x, e.button.y = y;
        events.push_back(e);
    };
    gHost->snapshotLayout(*gRoot);  // (the window paints between a user's inputs: the views are placed, a page's fields too)
    move(float(x0), float(y0), false);
    if (!moveOnly) {
        button(true, float(x0), float(y0), firstClicks);
        if (drag)
            for (int i = 1; i <= 10; ++i) {
                move(float(x0) + float(x1 - x0) * float(i) / 10, float(y0) + float(y1 - y0) * float(i) / 10, true);
                if (i == 5 && undoHalfway)
                    for (const bool down : {true, false}) {
                        SDL_Event k{};
                        k.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                        k.key.key = SDLK_Z, k.key.down = down;
#ifdef __APPLE__
                        k.key.mod = SDL_KMOD_LGUI;
#else
                        k.key.mod = SDL_KMOD_LCTRL;
#endif
                        events.push_back(k);
                    }
            }
        if (lost) {
            SDL_Event e{};
            e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            events.push_back(e);
        } else {
            button(false, float(x1), float(y1), firstClicks);
        }
        if (twice) button(true, float(x0), float(y0), 2), button(false, float(x0), float(y0), 2);
        for (int k = 2; k <= repeat; ++k) button(true, float(x0), float(y0), Uint8(std::min(k, 255))), button(false, float(x0), float(y0), Uint8(std::min(k, 255)));
    }
    const SDL_Keymod before = SDL_GetModState();
    SDL_SetModState(held);
    if (menuPick != -2) kit::Menu::setTestHook(sdlMenuShot), gMenuShotPick = menuPick;  // (Menu<n>: the menu's item n runs)
    gHost->snapshotMouse(*gRoot, events);
    SDL_SetModState(before);
    runPosted();
    if (menuPick != -2) gMenuShotPick = -2;
    gShotMouse = {x1, y1};
    sdlInputLine(name + spec);
}

// The map's camera before and after a --wheel or --pinch at the window's point (x, y) (the CHECKS read it): the point
// in the map view, then each camera: the map's point at the view's top left and the zoom.
struct SdlCamera {
    float x, y, zoom;
};
SdlCamera sdlCamera() { return {gState.camX, gState.camY, gState.zoom}; }
void sdlCameraLine(const std::string& what, const SdlCamera& was, float x, float y) {
    const draw::Point o = gRoot ? gRoot->map.origin() : draw::Point{};
    std::printf("%s: at map %.1f,%.1f; camera %.2f,%.2f zoom %.4f -> %.2f,%.2f zoom %.4f\n", what.c_str(), double(x - float(o.x)), double(y - float(o.y)),
                double(was.x), double(was.y), double(was.zoom), double(gState.camX), double(gState.camY), double(gState.zoom));
}

// --wheel x,y,dx,dy[:precise][:mac][:mods] in a snapshot: the wheel at (x, y) in the main window's pixels, as SDL gives it:
// dy notches away from the user, dx to the right (fractions allowed). precise: a trackpad's two-finger scroll (macOS:
// hasPreciseScrollingDeltas), not a wheel's notches; only macOS tells them apart, so elsewhere (with no mac) it is a
// smooth wheel's scroll, as the system gives it (a wheel's). mac: as macOS gives it on any system (dx and dy in tenths of
// points; a precise one is exact: the lists move by its distance). mods: Shift, Ctrl, Alt joined with + (Ctrl is Cmd
// on macOS). The output's line says the map's camera, and the scroll position of the lists under the point.
void sdlWheelShot(const std::string& spec) {
    std::stringstream parts(spec);
    std::string at, word;
    std::getline(parts, at, ':');
    bool precise = false, mac = false;
    SDL_Keymod held = SDL_KMOD_NONE;
    while (std::getline(parts, word, ':')) {
        if (word == "precise" || word == "mac") {
            (word == "mac" ? mac : precise) = true;
            continue;
        }
        std::stringstream keys(word);
        for (std::string m; std::getline(keys, m, '+');) {
#ifdef __APPLE__
            if (m == "Ctrl") held = SDL_Keymod(held | SDL_KMOD_LGUI);
#else
            if (m == "Ctrl") held = SDL_Keymod(held | SDL_KMOD_LCTRL);
#endif
            else if (m == "Shift") held = SDL_Keymod(held | SDL_KMOD_LSHIFT);
            else if (m == "Alt") held = SDL_Keymod(held | SDL_KMOD_LALT);
            else return void(std::fprintf(stderr, "--wheel %s: %s is not precise, Shift, Ctrl or Alt\n", spec.c_str(), m.c_str()));
        }
    }
    float x = 0, y = 0, dx = 0, dy = 0;
    if (!gRoot || !gHost || std::sscanf(at.c_str(), "%f,%f,%f,%f", &x, &y, &dx, &dy) != 4)
        return void(std::fprintf(stderr, "--wheel %s: not x,y,dx,dy[:precise][:mods]\n", spec.c_str()));
    std::vector<SDL_Event> events(2);
    events[0].type = SDL_EVENT_MOUSE_MOTION;
    events[0].motion.x = x, events[0].motion.y = y;
    events[1].type = SDL_EVENT_MOUSE_WHEEL;
    events[1].wheel.x = dx, events[1].wheel.y = dy, events[1].wheel.mouse_x = x, events[1].wheel.mouse_y = y;
    events[1].wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    const SdlCamera was = sdlCamera();
    const SDL_Keymod before = SDL_GetModState();
    SDL_SetModState(held);
    const std::string scrolledBefore = sdlScrollLine(int(x), int(y));
#ifndef __APPLE__
    precise = precise && mac;  // (Windows and Linux have no hook: a scroll with fractions is a smooth wheel's)
#endif
    gHost->setPreciseScrollHook([precise] { return precise; });
    gHost->setMacScrollTest(mac);
    gHost->snapshotMouse(*gRoot, events);
    gHost->setMacScrollTest(false);
    gHost->setPreciseScrollHook(nullptr);
    SDL_SetModState(before);
    runPosted();
    sdlInputLine("wheel " + spec);
    sdlCameraLine("wheel " + spec, was, x, y);
    std::printf("wheel %s: scroll %s -> %s\n", spec.c_str(), scrolledBefore.c_str(), sdlScrollLine(int(x), int(y)).c_str());
}

// The scroll positions of the scrolling views under the window's point (x, y), from the inside out (the CHECKS read
// them): the kit's lists and trees and the program's pages (a view's scroll is what it paints from).
std::string sdlScrollLine(int x, int y) {
    if (!gRoot) return "-";
    draw::Point local;
    std::string out;
    for (draw::View* v = gRoot->viewAt({x, y}, local); v; v = v->parent()) {
        int at = -1;
        if (auto* l = dynamic_cast<kit::ListView*>(v)) at = l->scrollTop();
        else if (auto* t = dynamic_cast<kit::TreeView*>(v)) at = t->scrollY();
        else if (v == &gStartView) at = gStartView.scrollPos();
        if (at >= 0) out += (out.empty() ? "" : ",") + std::to_string(at);
    }
    return out.empty() ? "-" : out;
}

// --smartzoom x,y and --swipe x,y,left|right in a snapshot: a trackpad's gesture with the mouse at (x, y), as macOS's
// monitor gives it (mac_input.mm: SdlHost::pushGesture): a smart zoom (a double tap with two fingers), or a swipe of
// three fingers (left: the next tab). The output's line says the map's camera and the tabs.
void sdlGestureShot(const std::string& spec, bool smartZoom) {
    const std::string name = smartZoom ? "smartzoom " : "swipe ";
    float x = 0, y = 0;
    char dir[16] = {};
    const int got = smartZoom ? std::sscanf(spec.c_str(), "%f,%f", &x, &y) : std::sscanf(spec.c_str(), "%f,%f,%15s", &x, &y, dir);
    const std::string d = dir;
    if (!gRoot || !gHost || got != (smartZoom ? 2 : 3) || (!smartZoom && d != "left" && d != "right"))
        return void(std::fprintf(stderr, "--%s%s: not %s\n", name.c_str(), spec.c_str(), smartZoom ? "x,y" : "x,y,left|right"));
    std::vector<SDL_Event> events(2);
    events[0].type = SDL_EVENT_MOUSE_MOTION;
    events[0].motion.x = x, events[0].motion.y = y;
    events[1] = kit_sdl::SdlHost::gestureEvent(smartZoom ? draw::MouseKind::SmartZoom : draw::MouseKind::Swipe, d == "left" ? -1 : d == "right" ? 1 : 0);
    const SdlCamera was = sdlCamera();
    gHost->snapshotMouse(*gRoot, events);
    runPosted();
    sdlInputLine(name + spec);
    sdlCameraLine(name + spec, was, x, y);
}

// --pinch x,y,scale[:Alt] in a snapshot: a trackpad's pinch with the mouse at (x, y): it begins, the fingers move
// apart (scale more than 1) or together, it ends. Alt: held during it (the brush's size).
void sdlPinchShot(const std::string& spec) {
    float x = 0, y = 0, scale = 1;
    const size_t colon = spec.find(':');
    const std::string mods = colon == std::string::npos ? "" : spec.substr(colon + 1);
    if (!gRoot || !gHost || std::sscanf(spec.c_str(), "%f,%f,%f", &x, &y, &scale) != 3 || (!mods.empty() && mods != "Alt"))
        return void(std::fprintf(stderr, "--pinch %s: not x,y,scale[:Alt]\n", spec.c_str()));
    std::vector<SDL_Event> events(4);
    events[0].type = SDL_EVENT_MOUSE_MOTION;
    events[0].motion.x = x, events[0].motion.y = y;
    events[1].type = SDL_EVENT_PINCH_BEGIN;
    events[2].type = SDL_EVENT_PINCH_UPDATE;
    events[2].pinch.scale = scale;
    events[3].type = SDL_EVENT_PINCH_END;
    const SdlCamera was = sdlCamera();
    const SDL_Keymod before = SDL_GetModState();
    if (!mods.empty()) SDL_SetModState(SDL_Keymod(before | SDL_KMOD_LALT));
    gHost->snapshotMouse(*gRoot, events);
    SDL_SetModState(before);
    runPosted();
    sdlInputLine("pinch " + spec);
    sdlCameraLine("pinch " + spec, was, x, y);
}

// --command <name> in a snapshot: a command of the menus by its name in the command table (core/map_commands.inc:
// view.grid, view.ranges...) or one of the names below, as its menu item runs it. The output says whether the item is
// enabled (a gray item: "(WRONG)").
void sdlCommandShot(const std::string& name) {
    static const std::pair<const char*, int> kNames[] = {{"view.towns", ID_VIEW_TOWNS}, {"view.log", ID_VIEW_LOG}, {"scen.check", ID_SCEN_CHECK},
                                                         {"edit.undo", ID_EDIT_UNDO}, {"edit.redo", ID_EDIT_REDO},
                                                         {"project.open", ID_PROJ_OPEN}, {"project.addscript", ID_PROJ_ADDSCRIPT},
                                                         {"file.exit", ID_FILE_EXIT}, {"options.restart", ID_OPT_RESTART},
                                                         {"options.unitsnap.tiles", ID_SNAP_UNITS + 1}};  // (Options > Units > Snap to tiles)
    int id = -1;
    for (const auto& [native, cmd] : gCommandIds)
        if (cmd == name) id = native;
    for (const auto& [n, native] : kNames)
        if (name == n) id = native;
    if (id < 0) return void(std::printf("command %s: no such command (WRONG)\n", name.c_str()));
    if (!sdlEnabled(id)) return void(std::printf("command %s: the menu item is gray (WRONG)\n", name.c_str()));
    command(id);
    runPosted();
    pathOverlayPoll(true);  // (a pathing overlay: its analysis is done before the picture)
    std::printf("command %s: ran (yes): %s\n", name.c_str(), narrow(gState.message).c_str());
}

// --- File dialogs -----------------------------------------------------------------------------------------------------

// A system file dialog's request: its result goes to `done` (an empty path when canceled) on the UI thread.
struct DialogCall {
    std::function<void(const fs::path&)> done;
};

// SDL's callback: another thread. The result is posted to the UI thread; the call's memory goes with it.
void dialogDone(void* data, const char* const* list, int) {
    std::unique_ptr<DialogCall> call(static_cast<DialogCall*>(data));
    const std::string picked = list && list[0] ? list[0] : std::string();
    const std::function<void(const fs::path&)> done = call->done;
    shellPost([done, picked] { done(picked.empty() ? fs::path() : fs::path(widen(picked))); });  // (UTF-8)
}

// The window the dialogs belong to: the main window (nullptr when there is none). (SDL_GetWindows' first window is the
// newest one: a menu that just closed, or a tooltip.)
SDL_Window* dialogWindow() { return gHost ? gHost->mainWindow() : nullptr; }

// The file and folder dialogs that are open. While one is open the program waits for it, as for a sheet: no window
// takes input (a click, a key, a tab switch, Close all, a quit), and no file opens from the system.
int gFileDialogsOpen = 0;
bool sdlFileDialogOpen() { return gFileDialogsOpen > 0; }
void sdlFileDialogBegins() {
    ++gFileDialogsOpen;
    if (gHost) gHost->setSheetOpen(true);
}
void sdlFileDialogEnds() {
    if (gFileDialogsOpen <= 0) return;
    --gFileDialogsOpen;
    if (gHost) gHost->setSheetOpen(false);
}
// The dialog's answer, on SDL's thread: the program takes input again (on the UI thread, before the answer's own work).
struct FileDialogWrap {
    SDL_DialogFileCallback answered;
    void* data;
};
void fileDialogAnswered(void* p, const char* const* list, int filter) {
    std::unique_ptr<FileDialogWrap> w(static_cast<FileDialogWrap*>(p));
    shellPost([] { sdlFileDialogEnds(); });
    w->answered(w->data, list, filter);
}

// A snapshot's dialog answer that waits (--pick defer:<path>): it comes after the next input, as a user's answer comes
// while the program runs. sdlShotDeferredAnswer gives it (true when there was one).
std::function<void()> gShotDeferred;
bool sdlShotDeferredAnswer() {
    if (!gShotDeferred) return false;
    const std::function<void()> f = std::exchange(gShotDeferred, {});
    f();
    return true;
}

// A snapshot's file and folder dialogs (main_sdl.cpp: --pick): none shows. Each answers at once with the next --pick
// in order (a path; "cancel", or none left: canceled), and the output says what it asked: the kind, the title, the
// filters and where it starts.
bool gShotDialogs = false;
std::deque<std::string> gShotPicks;

// Where a file or folder dialog starts, as SDL takes it (UTF-8): `folder`, or the nearest folder above it that is there
// (Windows' dialog does not open in a folder that is not there), with a separator at its end (a folder: SDL's dialogs on
// Windows and macOS take the last part of a path without one as the file's name), then `fileName` (a save dialog's).
// Empty when there is no such folder and no name: the system chooses.
std::string sdlDialogLocation(const fs::path& folder, const std::wstring& fileName = {}) {
    std::error_code ec;
    fs::path dir = folder;
    while (!dir.empty() && !fs::is_directory(dir, ec)) {
        const fs::path up = dir.parent_path();
        if (up == dir) dir.clear();
        else dir = up;
    }
    std::wstring where = dir.wstring();
    if (!where.empty() && where.back() != L'/' && where.back() != fs::path::preferred_separator) where += fs::path::preferred_separator;
    return narrow(where + fileName);
}

// Shows the system's file or folder dialog (asynchronous: `answered` gets the list, on any thread). `location` from
// sdlDialogLocation; `filters` must stay until the answer.
void sdlFileDialog(SDL_FileDialogType type, SDL_DialogFileCallback answered, void* data, const SDL_DialogFileFilter* filters,
                   int filterCount, const std::string& location, const std::string& title, bool many = false) {
    if (gShotDialogs) {
        static const char* const kKinds[] = {"open", "save", "folder"};
        std::string list;
        for (int i = 0; i < filterCount; ++i) list += std::string(i ? " | " : "") + filters[i].name + " (" + filters[i].pattern + ")";
        std::string answer = gShotPicks.empty() ? std::string("cancel") : gShotPicks.front();
        if (!gShotPicks.empty()) gShotPicks.pop_front();
        std::printf("dialog: %s, title \"%s\", filters %s, starts at \"%s\": %s\n", kKinds[int(type) % 3], title.c_str(),
                    list.empty() ? "none" : list.c_str(), location.c_str(), answer.c_str());
        sdlFileDialogBegins();
        auto* wrap = new FileDialogWrap{answered, data};
        const bool later = answer.rfind("defer:", 0) == 0;
        if (later) answer = answer.substr(6);
        auto give = [wrap, answer] {
            const char* const none[] = {nullptr};
            const char* const picked[] = {answer.c_str(), nullptr};
            fileDialogAnswered(wrap, answer == "cancel" ? none : picked, 0);  // (SDL: an empty list is a cancel)
        };
        if (later) gShotDeferred = give, gShotDeferredAt = gShotInputCount;
        else give();
        return;
    }
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, dialogWindow());
    if (filters && filterCount > 0) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, filterCount);
    }
    if (!location.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
    // (the title says what to pick: the dialog's title bar on Windows and Linux, the panel's message on macOS)
    if (!title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, many);
    sdlFileDialogBegins();
    SDL_ShowFileDialogWithProperties(type, fileDialogAnswered, new FileDialogWrap{answered, data}, props);
    SDL_DestroyProperties(props);
}

// Opens the system's open or save dialog (asynchronous: `done` runs later, on the UI thread).
void showFileDialog(bool save, std::function<void(const fs::path&)> done) {
    static const SDL_DialogFileFilter kOpen[] = {{"Maps and projects", "scx;scm;chk;stardraft;eds"},
                                                 {"StarCraft maps", "scx;scm"},
                                                 {"StarDraft 2 projects", "stardraft;eds"},
                                                 {"Scenario data", "chk"},
                                                 {"All files", "*"}};
    static const SDL_DialogFileFilter kSave[] = {{"StarCraft maps", "scx;scm"}, {"Scenario data", "chk"}, {"All files", "*"}};
    auto* call = new DialogCall{std::move(done)};
    if (save) {
        // The map's own folder and name; a new map: Untitled.scx where the system chooses.
        const fs::path path = gState.ed ? gState.ed->path() : fs::path();
        const std::string where = path.empty() ? std::string("Untitled.scx") : sdlDialogLocation(path.parent_path(), path.filename().wstring());
        sdlFileDialog(SDL_FILEDIALOG_SAVEFILE, dialogDone, call, kSave, 3, where, "");
    } else {
        sdlFileDialog(SDL_FILEDIALOG_OPENFILE, dialogDone, call, kOpen, 5, "", "");
    }
}

// --- Save and quit ----------------------------------------------------------------------------------------------------

// Writes the open map to `path` (core's saveMapFile); an error is shown in a message box. True when it was saved.
// `asked`: the map that asked (a dialog's answer): when it is no longer the open map, nothing is written.
bool saveTo(const fs::path& path, const Editor* asked = nullptr) {
    std::string err;
    if (saveMapFileOf(asked ? asked : gState.ed.get(), path, err)) return true;
    shellErrorBox(widen(err), shellTr(L"Could not save the map"));  // (the core message box, as on Windows)
    return false;
}

// File > Save (the map's own file, or a new one when it has none) and Save as (always asks). `done` gets whether the map
// was saved; it runs later when a dialog answers.
bool sdlSaveUnsavedProject();  // (shell_sdl.inc: Save for an unsaved project, as on Windows)
void saveOpenMap(bool as, std::function<void(bool)> done = {}) {
    if (!gState.ed || !gState.ed->isOpen()) {
        if (done) done(false);
        return;
    }
    typingFlush();  // a text being typed (a map page's field, the Briefing's text) is saved too
    if (isUnsavedProject(activeProject())) {
        const bool ok = sdlSaveUnsavedProject();
        if (done) done(ok);
        return;
    }
    const fs::path path = gState.ed->path();
    if (!as && !path.empty() && !saveNeedsOtherPlace(path)) {
        const bool ok = saveTo(path);
        if (done) done(ok);
        return;
    }
    if (!as && !path.empty()) setMessage(L"StarDraft 2 cannot write to the map's folder. Select another place for the map.");
    const Editor* asked = gState.ed.get();  // (the answer saves this map, not the tab that is active then)
    showFileDialog(true, [done, asked](const fs::path& picked) {
        fs::path p = picked;
        if (!p.empty() && !p.has_extension()) p += ".scx";  // a name without an extension is a StarCraft map
        const bool ok = !p.empty() && saveTo(p, asked);
        if (done) done(ok);
    });
}

void quitNow() {
    if (gShotDialogs) std::printf("quit: StarDraft 2 ends\n");  // (a snapshot: the run goes on, the output says it)
    if (gHost) gHost->quit();
}

// True while the quit's questions are open: their boxes. A quit request then waits for them.
bool gQuitPrompt = false;
// While the quit's questions are open: what a Save that asks for a file (an untitled map) runs once it saved: the
// questions go on, and then the quit.
std::function<void()> gQuitRetry;

// Before StarDraft ends (quit, Restart now, an update's install): each tab with changes is asked about (Save, Do not
// save, Cancel: core/views_tabs.inc, confirmAllDocs). `then` runs when StarDraft may end.
void confirmQuit(std::function<void()> then) {
    if (gQuitPrompt || sdlFileDialogOpen()) return;
    gQuitPrompt = true;
    gQuitRetry = [then] { confirmQuit(then); };
    const bool ok = !gRoot || confirmAllDocs();
    gQuitRetry = {};
    gQuitPrompt = false;
    if (ok) then();
}

// Quit (File > Exit, the window's close, the system's quit).
void requestQuit() { confirmQuit(quitNow); }

// Before a map opens (the active tab is asked about): Save, Do not save or Cancel. `then` runs when the map may go on:
// after Do not save, or after a Save that saved.
void confirmReplace(std::function<void()> then) {
    if (!gRoot || !gState.ed || !gState.ed->isOpen() || !gState.ed->modified()) return then();
    const int r = shellAskSaveChanges(false);  // (the save question: shell_sdl.inc)
    if (r == 0) saveOpenMap(false, [then](bool ok) {
        if (ok) then();
    });
    else if (r == 1) then();
}

// --- Commands ---------------------------------------------------------------------------------------------------------

// Runs a command by its id: the core's command path first (the table, the recent lists, the checks of the tab's mode),
// then the commands the core leaves to the shell.
bool sdlProjectCommand(int id);  // (sdl_project.inc: Open project, New EUD project, Add a script, the folder, the settings)
bool sdlUpdatesCommand(int id);  // (sdl_updates.inc: Check for updates, Look for updates on start, Crash reports, Maps)
void commandSdl(int id);
void command(int id) {
    commandSdl(id);
    saveSettingsTable();  // an option the command changed stays for the next start (core/app_settings.inc)
}
void commandSdl(int id) {
    if ((id >= ID_RECENT_MAP && id < ID_RECENT_MAP + kRecentMax) || (id >= ID_RECENT_PROJECT && id < ID_RECENT_PROJECT + kRecentMax))
        return confirmReplace([id] { commandPortable(gState, id); });
    if (commandPortable(gState, id)) return;
    if (commandMapEdit(gState, id)) return;  // the Map's Edit and Options commands, the keys, Close (core/map_edit_commands.inc)
    if (sdlProjectCommand(id)) return;
    if (sdlOptionsCommand(id)) return;  // (sdl_options.inc: the theme, the language, the program's options)
    if (sdlUpdatesCommand(id)) return;
    switch (id) {
    case ID_FILE_OPEN: confirmReplace([] {
            showFileDialog(false, [](const fs::path& p) {
                if (!p.empty()) openAny(p);
            });
        });
        return;
    case ID_FILE_SAVE: saveOpenMap(false); return;
    case ID_FILE_SAVEAS: saveOpenMap(true); return;
    case ID_FILE_EXIT: requestQuit(); return;
    case ID_EDIT_UNDO:
    case ID_EDIT_REDO:
        if (gWorkspace == Workspace::Data) {  // the Data workspace's own steps (sdl_data.inc), as the Windows program has them
            sdlDataUndo(id == ID_EDIT_REDO);
            return;
        }
        if (gWorkspace == Workspace::Triggers) {  // the Triggers workspace's own steps (core/triggers_model.inc)
            trigUndo(id == ID_EDIT_REDO);
            return;
        }
        if (gState.ed && gState.ed->isOpen()) {
            if (gState.ed->inStroke()) return setMessage(L"Release the mouse button to undo or redo");
            typingFlush();  // a text being typed on a map page is a step of its own first: Undo takes it back
            const std::vector<Doodad> doodads = gState.ed->doodads();
            const std::vector<MapSprite> sprites = gState.ed->sprites();
            if (id == ID_EDIT_REDO) gState.ed->redo();
            else gState.ed->undo();
            keepItemSelection(gState, doodads, sprites);
            gState.terrainRev = 0;
            rebuildList(gState);
            gState.dirty = true;
            updateTitle();
        }
        return;
    case ID_WS_ASSETS: setWorkspace(Workspace::Files); return;  // Assets is part of Files
    case ID_SCEN_PROPS: setWorkspace(Workspace::Properties); return;
    case ID_SCEN_PLAYERS: setWorkspace(Workspace::Players); return;
    case ID_SCEN_CLASSIC: setWorkspace(Workspace::ClassicSettings); return;
    case ID_SCEN_CLASSICTRIG: setWorkspace(Workspace::ClassicTriggers); return;
    case ID_FILE_NEW: openNewPageSdl(-1); return;  // the start page
    case ID_PROJ_NEW: openNewPageSdl(1); return;   // the New page, an EUD project's choices
    case ID_OPT_WINDOW: showOptionsSdl(); return;
    case ID_HELP_FEEDBACK: showFeedbackSdl(L"StarDraft 2"); return;
    case ID_EDIT_RENAME:
        if (gState.ed && gState.ed->isOpen()) renameSelectedLocation();
        return;
    case ID_QUICKPICK: shellShowQuickPicker(false, false); return;
    case ID_HELP_ABOUT: showAboutSdl(); return;
    case ID_VIEW_KIT: showKitGallerySdl(); return;
    case ID_HELP_DISCORD: shellOpenUrl(kDiscordInvite); return;
    case ID_HELP_GITHUB: shellOpenUrl(L"https://github.com/" + std::wstring(kIssueRepo)); return;
    case ID_PROJ_COMPILE: compileProject(); return;
    case ID_VIEW_LOG: gLogPane.setExpanded(!gLogPane.expanded()); return;
    case ID_OPT_INSTALL: shellChooseInstallFolder(); return;
    case ID_SCEN_CHECK: {
        const std::wstring problems = playabilityText();
        if (problems.empty()) setMessage(L"StarCraft can start this map");
        else if (gRoot)
            msgbox::ask(*gRoot, shellTr(L"Check that StarCraft can start the map"), shellTr(L"StarCraft will not start this map:") + L"\r\n\r\n" + problems,
                        {L"OK"}, msgbox::Icon::Warning);
        return;
    }
    case ID_RECENT_CLEAR_MAPS:
    case ID_RECENT_CLEAR_PROJECTS:
        (id == ID_RECENT_CLEAR_MAPS ? gRecentMaps : gRecentProjects).clear();
        saveRecent();
        gMenusDirty = true;
        return;
    case ID_DOCK_MINI_LEFT:
    case ID_DOCK_MINI_RIGHT:
        gDock.minimapRight = id == ID_DOCK_MINI_RIGHT, gDock.minimapHidden = false;
        saveDock(), shellRelayout();
        return;
    case ID_DOCK_TOOLS_LEFT:
    case ID_DOCK_TOOLS_RIGHT:
        gDock.toolsRight[int(gState.mode)] = id == ID_DOCK_TOOLS_RIGHT;
        saveDock(), shellRelayout();
        setMessage(std::wstring(kModeNames[int(gState.mode)]) + L" toolbar on the " + (id == ID_DOCK_TOOLS_RIGHT ? L"right" : L"left"));
        return;
    case ID_VIEW_TRIGCODE:  // shows or hides the code pane
        if (activeProject()) setWorkspace(Workspace::Triggers), showTriggerCode();
        return;
    default:
        // (the workspaces in their order; a command this system does not run is gray in the menus, and its key does nothing)
        if (id >= ID_WS_MAP && id <= ID_WS_FILES) return setWorkspace(Workspace(id - ID_WS_MAP));
        return;
    }
}
