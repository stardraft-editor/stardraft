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
bool sdlRuns(int id) {
    if (sdlOptionsRuns(id)) return true;
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
    default: break;
    }
    if (id >= ID_RECENT_MAP && id < ID_RECENT_MAP + kRecentMax) return true;
    if (id >= ID_RECENT_PROJECT && id < ID_RECENT_PROJECT + kRecentMax) return true;
    return gCommandIds.count(id) != 0;  // the Map workspace's table (registerMapCommands)
}

// Whether the item is enabled now: it runs, and it is there for the open tab (a map, its mode).
bool sdlEnabled(int id) {
    if (!sdlRuns(id)) return false;
    if (mapCommand(id) && !docOpen()) return false;
    if (id == ID_PROJ_COMPILE && gBuilding) return false;  // (a build runs)
    return commandForMode(id, projectMode());
}

// The menu item's text in the program's language (the tables have it with its access keys, as the Windows menus add
// it), without the access keys ('&').
std::wstring accessFree(std::wstring s) {
    s = tr(s);
    std::erase(s, L'&');
    return s;
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
            k.checked = it.checked || (tc && tc->isChecked());
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
    if (gMenusDirty && gRoot && gRoot->menus.openIndex() < 0) rebuildMenus();
}

// A key that a shortcut runs (the window's keys, after the views that take them). True when one ran.
bool shortcutRun(const draw::KeyEvent& e) {
    if (!e.down || e.repeat) return false;
    for (const Shortcut& s : gShortcuts)
        if (chordIs(s.chord, e) && sdlEnabled(s.id)) {
            command(s.id);
            return true;
        }
    return false;
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
    shellPost([done, picked] { done(fs::path(picked)); });
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
void command(int id) {
    if ((id >= ID_RECENT_MAP && id < ID_RECENT_MAP + kRecentMax) || (id >= ID_RECENT_PROJECT && id < ID_RECENT_PROJECT + kRecentMax))
        return confirmReplace([id] { commandPortable(gState, id); });
    if (commandPortable(gState, id)) return;
    if (sdlProjectCommand(id)) return;
    if (sdlOptionsCommand(id)) return;  // (sdl_options.inc: the theme, the language, the program's options)
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
    case ID_HELP_GITHUB: shellOpenUrl(L"https://github.com/" + std::wstring(kRepoName)); return;
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
