// The SDL shell's menus, keys and commands (research/ports/MAC-BUILD-PLAN.md steps 5 and 6). The menu bar is the core's menu
// model (buildMenuModel, core/menu_model.inc) shown as kit menus; a click or a key runs command(id), the core's command
// path first. An item the SDL program cannot run yet is grayed, and its key does nothing. Ctrl in a shortcut is Cmd on
// macOS. File > Open, Save and Save as use the system's file dialogs: they answer later, on another thread, so their
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

// A shortcut as the menus show it: on macOS Ctrl is Cmd and Alt is Option (Ctrl+Shift+S is Cmd+Shift+S there).
std::wstring shortcutText(std::wstring s) {
#ifdef __APPLE__
    for (const auto& [from, to] : {std::pair<std::wstring, std::wstring>{L"Ctrl+", L"Cmd+"}, {L"Alt+", L"Option+"}})
        for (size_t at = s.find(from); at != std::wstring::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
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
        case ' ': out = draw::Key::Space; return true;
        default: return false;
        }
    }
    if (name == "Del") out = draw::Key::Delete;
    else if (name == "Tab") out = draw::Key::Tab;
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
            k.label = accessFree(it.label), k.shortcut = shortcutText(it.shortcut);
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
        else if (it.kind == MenuItem::Kind::Command && !it.shortcut.empty())
            if (const auto c = stardraft::parseShortcut(narrow(it.shortcut))) gShortcuts.push_back({*c, it.id});
    }
}

// Makes the menu bar again from the model (its checks, its enabled items, the recent lists).
void rebuildMenus() {
    if (!gRoot) return;
    const MenuModel model = buildMenuModel();
    gShortcuts.clear();
    collectShortcuts(model);
    std::vector<kit::MenuGroup> groups;
    for (const MenuItem& top : model) groups.push_back(kit::MenuGroup{accessFree(top.label), toKit(top.items)});
    gRoot->menus.setMenus(std::move(groups));
    gMenusDirty = false;
}

// The frame hook's part: the menus are made again when they changed and none is open (an open menu keeps its items).
void menusIfDirty() {
    // Undo, Redo and Paste follow the edits (an edit does not say that the menus changed): the menus are made again
    // when one of them goes gray or comes back.
    static int grayed = -1;
    const int now = int(mapMenuGrayed(ID_EDIT_UNDO)) | int(mapMenuGrayed(ID_EDIT_REDO)) << 1 | int(mapMenuGrayed(ID_EDIT_PASTE)) << 2;
    if (now != grayed) grayed = now, gMenusDirty = true;
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

void sdlPressKey(const std::string& chord) {
    const auto c = stardraft::parseShortcut(chord);
    draw::KeyEvent e;
    if (!gRoot || !c || !keyOfChord(c->key, e.key)) return void(std::fprintf(stderr, "--key %s: no such key\n", chord.c_str()));
    e.mods = (c->ctrl ? kShortcutCtrl : 0) | (c->shift ? draw::kShift : 0) | (c->alt ? draw::kAlt : 0);
    gRoot->key(e);
    runPosted();
    sdlInputLine("key " + chord);
}

// The output line after a --key, --click or --drag (as the Windows self-test's log: Delete, then Undo, gives the units
// back): the units, the doodads, the selection (the first selected unit's place) and the status bar's text.
void sdlInputLine(const std::string& what) {
    if (!gState.ed || !gState.ed->isOpen()) return;
    const auto& units = gState.ed->units();
    const auto& sel = gState.ed->selection();
    std::string first;
    if (!sel.empty() && sel[0] >= 0 && size_t(sel[0]) < units.size())
        first = " (the first at " + std::to_string(units[size_t(sel[0])].x) + "," + std::to_string(units[size_t(sel[0])].y) + ")";
    std::printf("%s: %zu units, %zu doodads, %zu selected%s: %s\n", what.c_str(), units.size(), gState.ed->doodads().size(), sel.size(),
                first.c_str(), narrow(gState.message).c_str());
}

// --click x,y[:mods] and --drag x0,y0-x1,y1[:mods] in a snapshot (main_sdl.cpp): the left button as a user's mouse
// gives it, in the window's pixels: the mouse comes to the first point, the button goes down, (a drag: the mouse moves
// to the second point in steps, the button held), the button goes up. The events go the host's way for a window's
// (SdlHost::snapshotInput), with the modifier keys held (Shift, Ctrl, Alt, joined with +; Ctrl is Cmd on macOS).
void sdlMouseShot(const std::string& spec, bool drag, int width, int height) {
    const std::string name = drag ? "drag " : "click ";
    const size_t colon = spec.find(':');
    const std::string at = spec.substr(0, colon), mods = colon == std::string::npos ? "" : spec.substr(colon + 1);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const int got = drag ? std::sscanf(at.c_str(), "%d,%d-%d,%d", &x0, &y0, &x1, &y1) : std::sscanf(at.c_str(), "%d,%d", &x0, &y0);
    if (!gRoot || !gHost || got != (drag ? 4 : 2))
        return void(std::fprintf(stderr, "--%s%s: not %s[:mods]\n", name.c_str(), spec.c_str(), drag ? "x0,y0-x1,y1" : "x,y"));
    if (!drag) x1 = x0, y1 = y0;
    SDL_Keymod held = SDL_KMOD_NONE;
    std::stringstream in(mods);
    for (std::string m; std::getline(in, m, '+');) {
#ifdef __APPLE__
        if (m == "Ctrl") held = SDL_Keymod(held | SDL_KMOD_LGUI);
#else
        if (m == "Ctrl") held = SDL_Keymod(held | SDL_KMOD_LCTRL);
#endif
        else if (m == "Shift") held = SDL_Keymod(held | SDL_KMOD_LSHIFT);
        else if (m == "Alt") held = SDL_Keymod(held | SDL_KMOD_LALT);
        else return void(std::fprintf(stderr, "--%s%s: %s is not Shift, Ctrl or Alt\n", name.c_str(), spec.c_str(), m.c_str()));
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
    auto button = [&](bool down, float x, float y) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = SDL_BUTTON_LEFT, e.button.down = down, e.button.clicks = 1, e.button.x = x, e.button.y = y;
        events.push_back(e);
    };
    move(float(x0), float(y0), false);
    button(true, float(x0), float(y0));
    if (drag)
        for (int i = 1; i <= 10; ++i)
            move(float(x0) + float(x1 - x0) * float(i) / 10, float(y0) + float(y1 - y0) * float(i) / 10, true);
    button(false, float(x1), float(y1));
    const SDL_Keymod before = SDL_GetModState();
    SDL_SetModState(held);
    gHost->snapshotInput(*gRoot, width, height, events);
    SDL_SetModState(before);
    runPosted();
    sdlInputLine(name + spec);
}

// --command <name> in a snapshot: a command of the menus by its name in the command table (core/map_commands.inc:
// view.grid, view.ranges...) or one of the names below, as its menu item runs it. The output says whether the item is
// enabled (a gray item: "(WRONG)").
void sdlCommandShot(const std::string& name) {
    static const std::pair<const char*, int> kNames[] = {{"view.towns", ID_VIEW_TOWNS}, {"view.log", ID_VIEW_LOG}, {"scen.check", ID_SCEN_CHECK},
                                                         {"edit.undo", ID_EDIT_UNDO}, {"edit.redo", ID_EDIT_REDO}};
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

// The window the dialogs belong to: the program's first window (nullptr when there is none).
SDL_Window* dialogWindow() {
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    SDL_Window* win = wins && n > 0 ? wins[0] : nullptr;
    SDL_free(wins);
    return win;
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
    SDL_Window* win = dialogWindow();
    if (save) {
        static std::string where;  // (SDL reads the location while the dialog is open: it lives on)
        where = gState.ed && !gState.ed->path().empty() ? narrow(gState.ed->path().wstring()) : std::string("Untitled.scx");
        SDL_ShowSaveFileDialog(dialogDone, call, win, kSave, 3, where.empty() ? nullptr : where.c_str());
    } else {
        SDL_ShowOpenFileDialog(dialogDone, call, win, kOpen, 5, nullptr, false);
    }
}

// --- Save and quit ----------------------------------------------------------------------------------------------------

// Writes the open map to `path` (core's saveMapFile); an error is shown in a message box. True when it was saved.
bool saveTo(const fs::path& path) {
    std::string err;
    if (saveMapFile(path, err)) return true;
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
    if (isUnsavedProject(activeProject())) {
        const bool ok = sdlSaveUnsavedProject();
        if (done) done(ok);
        return;
    }
    if (!as && !gState.ed->path().empty()) {
        const bool ok = saveTo(gState.ed->path());
        if (done) done(ok);
        return;
    }
    showFileDialog(true, [done](const fs::path& picked) {
        fs::path p = picked;
        if (!p.empty() && !p.has_extension()) p += ".scx";  // a name without an extension is a StarCraft map
        const bool ok = !p.empty() && saveTo(p);
        if (done) done(ok);
    });
}

void quitNow() {
    if (gHost) gHost->quit();
}

// Quit (File > Exit, the window's close, the system's quit): a map with changes is asked about first: Save (it quits once
// saved), Do not save, or Cancel.
// True while the quit prompt is open: its box, or the Save it started (a dialog). A quit request then waits for it.
bool gQuitPrompt = false;

void requestQuit() {
    if (gQuitPrompt) return;
    if (!gRoot || !gState.ed || !gState.ed->isOpen() || !gState.ed->modified()) return quitNow();
    gQuitPrompt = true;
    const int r = shellAskSaveChanges(false);  // (the save question: shell_sdl.inc)
    if (r == 0) saveOpenMap(false, [](bool ok) {
        gQuitPrompt = false;
        if (ok) quitNow();
    });
    else if (r == 1) quitNow();
    else gQuitPrompt = false;
}

// Before a map replaces the open one (SDL has one tab): Save, Do not save or Cancel. `then` runs when the map may go on:
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
            if (id == ID_EDIT_REDO) gState.ed->redo();
            else gState.ed->undo();
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
