// The portable part of the commands: the checks before the switch, the commands that need no system call (the
// command table and the recent lists). Returns true when the command is handled. Included by core.inc, after
// documents.inc (the workspaces, the tabs and the recent lists). The helpers below are defined by the program
// (main_sdl.cpp and its sdl_*.inc files) until they move into the core themselves.

bool modeLayerHidden();
void modeLayerHiddenMessage();
bool mapCommand(int id);
bool briefCommand(int id);
void trigCopy(bool cut);
void trigPaste();
void trigDuplicate();
void dataCopySelected();
void dataPasteSelected();
void classicCopySelected();  // the Map data page's selected entry (sdl_map_pages.inc)
void classicPasteSelected();
void setMessage(const std::wstring& m);
void shellMenusChanged();

// The Edit commands that act on the map (Cut, Copy, Paste, Duplicate, Delete, Select all, Rename location), the quick
// picker (Q) and the player keys: the map's versions run only in the Map workspace. Another workspace runs its own
// version where it has one (as the Windows program's Edit menu followed the workspace): Data copies and pastes the
// selected entry's values and Q goes to an entry; Map data copies and pastes the selected entry's values; Triggers cuts,
// copies, pastes and duplicates triggers and blocks; Briefing edits its rows.
// Elsewhere (the Project page, Assets, the other map pages, and those commands that the workspace has not) they are gray and
// do nothing. (A text field with the keyboard takes its own Ctrl+C, Ctrl+V, Ctrl+A first.)
bool workspaceEditCommand(int id) {
    return id == ID_EDIT_CUT || id == ID_EDIT_COPY || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE || id == ID_EDIT_DELETE ||
           id == ID_EDIT_SELECTALL || id == ID_EDIT_RENAME || id == ID_QUICKPICK || (id >= ID_PLAYER_1 && id < ID_PLAYER_1 + 10);
}
bool editInWorkspace(int id) {
    if (!workspaceEditCommand(id)) return true;
    switch (gWorkspace) {
    case Workspace::Map: return true;
    case Workspace::Data: return id == ID_EDIT_COPY || id == ID_EDIT_PASTE || id == ID_QUICKPICK;  // (Q: go to an entry)
    case Workspace::ClassicSettings: return id == ID_EDIT_COPY || id == ID_EDIT_PASTE;  // (Map data: the selected entry's values)
    case Workspace::ClassicTriggers: return id == ID_QUICKPICK;  // (Q: Quick add, core/views_trigger_quick.inc)
    case Workspace::Help: return id == ID_QUICKPICK;  // (Q: the help's sections, sdl_help.inc)
    case Workspace::Triggers: return id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE || id == ID_QUICKPICK;
    case Workspace::Briefing:
        return id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE || id == ID_EDIT_DELETE ||
               id == ID_EDIT_SELECTALL;
    default: return false;
    }
}

void trigQuickAdd();
void campaignNew();
void campaignOpen();
void projectImport();
void campaignShow();
void trigFind();

bool commandPortable(AppState& app, int id) {
    // The active mode's layer is hidden (View > Layers): the edits of the map are refused.
    if (gWorkspace == Workspace::Map && modeLayerHidden() &&
        (id == ID_EDIT_SELECTALL || id == ID_EDIT_DELETE || id == ID_EDIT_CUT || id == ID_EDIT_COPY || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE))
        return modeLayerHiddenMessage(), true;
    // No map (a page tab): its commands do nothing, also from a shortcut or the top bar (their menu items are gray).
    if (!docOpen() && mapCommand(id)) return true;
    // Another workspace: the map's Edit commands are not for the map that does not show (undo and redo are).
    if (!editInWorkspace(id)) return true;
    if (gWorkspace == Workspace::Data && (id == ID_EDIT_COPY || id == ID_EDIT_PASTE)) {  // the selected entry's values
        if (id == ID_EDIT_COPY) dataCopySelected();
        else dataPasteSelected();
        return true;
    }
    if (gWorkspace == Workspace::ClassicSettings && (id == ID_EDIT_COPY || id == ID_EDIT_PASTE)) {
        if (id == ID_EDIT_COPY) classicCopySelected();
        else classicPasteSelected();
        return true;
    }
    if (gWorkspace == Workspace::Briefing && (id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE || id == ID_EDIT_DELETE ||
                                              id == ID_EDIT_SELECTALL))
        return briefCommand(id), true;  // (nothing for the map, also when the page has nothing to do)
    if (gWorkspace == Workspace::Triggers && (id == ID_EDIT_COPY || id == ID_EDIT_CUT || id == ID_EDIT_PASTE || id == ID_EDIT_DUPLICATE)) {
        if (id == ID_EDIT_PASTE) trigPaste();
        else if (id == ID_EDIT_DUPLICATE) trigDuplicate();
        else trigCopy(id == ID_EDIT_CUT);
        return true;
    }
    if (gWorkspace == Workspace::Triggers && id == ID_QUICKPICK) return trigQuickAdd(), true;
    if (gWorkspace == Workspace::Triggers && id == ID_TRIG_FIND) return trigFind(), true;  // (core/views_trigger_quick.inc)  // Q: Quick add (core/views_trigger_quick.inc)
    if (id == ID_QUICKOPEN) return shellShowQuickPicker(false, true), true;
    if (id == ID_CAMPAIGN_NEW) return campaignNew(), true;  // File > Campaign (core/views_campaign.inc)
    if (id == ID_CAMPAIGN_OPEN) return campaignOpen(), true;
    if (id == ID_PROJ_IMPORT) return projectImport(), true;
    if (id == ID_CAMPAIGN_SHOW) return campaignShow(), true;
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
    return id == ID_FILE_SAVE || id == ID_FILE_SAVEAS || id == ID_FILE_EXPORTIMAGE || (id >= ID_EDIT_UNDO && id <= ID_EDIT_DUPLICATE) ||
           (id >= ID_VIEW_LOCATIONS && id <= ID_PATHSIZE_LARGE) || (id >= ID_MODE_TERRAIN && id <= ID_MODE_SPRITES) ||
           (id >= ID_PLAYER_1 && id < ID_PLAYER_1 + 10) || id == ID_QUICKPICK || (id >= ID_PROJ_COMPILE && id <= ID_PROJ_FROMMAP) ||
           (id >= ID_WS_MAP && id <= ID_WS_HELP) || (id >= ID_DOCK_MINI_LEFT && id <= ID_DOCK_TOOLS_RIGHT) || id == ID_SCEN_PLAYERS ||
           id == ID_SCEN_CHECK || id == ID_SCEN_PATHCHECK || id == ID_SCEN_PROPS || id == ID_SCEN_CLASSIC || id == ID_SCEN_CLASSICTRIG || id == ID_VIEW_TRIGCODE || id == ID_DATA_TABLE || id == ID_DATA_ENTRY ||
           id == ID_REVEAL_PLACE || id == ID_REVEAL_REMOVE || id == ID_SIM_MINING || id == ID_SIM_COMBAT;
}
