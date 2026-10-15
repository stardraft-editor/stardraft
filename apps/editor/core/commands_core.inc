// The portable part of commandBody (main.cpp): the checks before its switch, the commands that need no Win32 call
// (commands.inc's table and the recent lists). Returns true when the command is handled. Included by core.inc, after
// documents.inc (the workspaces, the tabs and the recent lists). The helpers below are defined in main.cpp (the Win32
// TU) until they move into the core themselves.

bool modeLayerHidden();
void modeLayerHiddenMessage();
bool mapCommand(int id);
bool briefCommand(int id);
void trigCopy(bool cut);
void trigPaste();
void trigDuplicate();
void dataCopySelected();
void dataPasteSelected();
void showQuickPicker(bool replace, bool open);
void setMessage(const std::wstring& m);
void shellMenusChanged();

bool commandPortable(AppState& app, int id) {
    // The active mode's layer is hidden (View > Layers): the edits of the map are refused.
    if (gWorkspace == Workspace::Map && modeLayerHidden() &&
        (id == ID_EDIT_SELECTALL || id == ID_EDIT_DELETE || id == ID_EDIT_CUT || id == ID_EDIT_COPY || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE))
        return modeLayerHiddenMessage(), true;
    // No map (a page tab): its commands do nothing, also from a shortcut or the top bar (their menu items are gray).
    if (!docOpen() && mapCommand(id)) return true;
    // A map page shows: the Edit commands are not for the map that does not show (undo and redo are).
    if (gWorkspace >= Workspace::Properties && (id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE ||
                                                id == ID_EDIT_DELETE || id == ID_EDIT_SELECTALL))
        return true;
    if (gWorkspace == Workspace::Data && (id == ID_EDIT_COPY || id == ID_EDIT_PASTE)) {  // the selected entry's values
        if (id == ID_EDIT_COPY) dataCopySelected();
        else dataPasteSelected();
        return true;
    }
    if (gWorkspace == Workspace::Briefing && (id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE || id == ID_EDIT_DELETE ||
                                              id == ID_EDIT_SELECTALL) && briefCommand(id))
        return true;
    if (gWorkspace == Workspace::Triggers && (id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE)) {
        if (id == ID_EDIT_PASTE) trigPaste();
        else if (id == ID_EDIT_DUPLICATE) trigDuplicate();
        else trigCopy(id == ID_EDIT_CUT);
        return true;
    }
    if (id == ID_QUICKOPEN) return shellShowQuickPicker(false, true), true;
    if ((id >= ID_RECENT_MAP && id < ID_RECENT_MAP + kRecentMax) || (id >= ID_RECENT_PROJECT && id < ID_RECENT_PROJECT + kRecentMax)) {
        const bool project = id >= ID_RECENT_PROJECT;
        auto& list = project ? gRecentProjects : gRecentMaps;
        const size_t i = size_t(id - (project ? ID_RECENT_PROJECT : ID_RECENT_MAP));
        if (i >= list.size()) return true;
        const fs::path p = list[i];
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            setMessage(app, p.wstring() + L" is gone; removed from the list");
            removeRecent(list, p), saveRecent(), refreshRecentMenus();
            return true;
        }
        if (project) openProject(p);
        else openAny(p);
        return true;
    }
    if (!commandInMode(id)) {  // a shortcut or a command of the other mode
        setMessage(app, classicMode() ? L"This tab is a classic map. This command needs a project. File > Project > Make this map a project makes one."
                                 : L"This command is for classic maps only");
        return true;
    }
    if (const auto it = gCommandIds.find(id); it != gCommandIds.end()) {  // a command of the table (commands.inc)
        gCommands.run(it->second);
        shellMenusChanged();
        app.dirty = true;
        return true;
    }
    return false;
}

// The commands that act on a map: a page tab (the New page, the start page) has none. The graphics quality (View >
// High definition ... Carbot) is a program setting, not one of them.
bool mapCommand(int id) {
    return id == ID_FILE_SAVE || id == ID_FILE_SAVEAS || (id >= ID_EDIT_UNDO && id <= ID_EDIT_DUPLICATE) ||
           (id >= ID_VIEW_LOCATIONS && id <= ID_PATHSIZE_LARGE) || (id >= ID_MODE_TERRAIN && id <= ID_MODE_SPRITES) ||
           (id >= ID_PLAYER_1 && id < ID_PLAYER_1 + 10) || id == ID_QUICKPICK || (id >= ID_PROJ_COMPILE && id <= ID_PROJ_FROMMAP) ||
           (id >= ID_WS_MAP && id <= ID_WS_FILES) || (id >= ID_DOCK_MINI_LEFT && id <= ID_DOCK_TOOLS_RIGHT) || id == ID_SCEN_PLAYERS ||
           id == ID_SCEN_CHECK || id == ID_SCEN_PROPS || id == ID_SCEN_CLASSIC || id == ID_SCEN_CLASSICTRIG || id == ID_VIEW_TRIGCODE;
}
