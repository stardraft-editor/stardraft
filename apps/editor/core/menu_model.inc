// The menu bar as data (research/ports/MAC-BUILD-PLAN.md step 5): each menu's items in order, with their texts, ids and
// flags. main.cpp's buildMenu makes the Win32 menu from it (toHmenu); the kit's MenuBar can show the same model.

struct MenuItem {
    enum class Kind : uint8_t { Command, Separator, Popup };
    Kind kind = Kind::Command;
    std::wstring label;     // the text without the shortcut; an & marks the access key
    std::wstring shortcut;  // shown after the label (Ctrl+N): empty for none
    int id = 0;             // a command's id (0 for a separator or a popup)
    bool checked = false, grayed = false;
    bool reason = false;    // a grayed item whose text says why it cannot be changed (the host marks it)
    std::string recent;     // a popup the host fills at run time: "maps" or "projects" (the recent lists)
    std::vector<MenuItem> items;  // a popup's items
};

using MenuModel = std::vector<MenuItem>;

// The command's Win32 id (the menu and accelerator ids of the command table, core/map_commands.inc).
int nativeCommandId(const std::string& id) {
    for (const auto& [native, name] : gCommandIds)
        if (name == id) return native;
    return 0;
}

// The menu item's text: the label, then a tab and the shortcut.
std::wstring commandMenuText(const stardraft::Command& c) {
    return widen(c.label) + (c.shortcut.empty() ? L"" : L"\t" + widen(c.shortcut));
}

// The menu bar as data: each menu's items in order (core/menu_model.inc). The Mode menu and the View items come from the
// command table (commands.inc); the recent lists, the feedback glyph and the icons are the Win32 menu's own (buildMenu).
void menuCommand(MenuModel& m, const char* id) {
    if (const stardraft::Command* c = gCommands.find(id)) {
        MenuItem it;
        it.id = nativeCommandId(id), it.label = widen(c->label), it.shortcut = widen(c->shortcut);
        m.push_back(std::move(it));
    }
}
void menuCommands(MenuModel& m, const char* menu) {
    for (const stardraft::Command* c : gCommands.inMenu(menu)) {
        MenuItem it;
        it.id = nativeCommandId(c->id), it.label = widen(c->label), it.shortcut = widen(c->shortcut);
        m.push_back(std::move(it));
    }
}
MenuItem menuItem(int id, std::wstring label, std::wstring shortcut = {}, bool grayed = false, bool checked = false, bool reason = false) {
    MenuItem it;
    it.id = id, it.label = std::move(label), it.shortcut = std::move(shortcut);
    it.grayed = grayed, it.checked = checked, it.reason = reason;
    return it;
}
MenuItem menuSeparator() {
    MenuItem it;
    it.kind = MenuItem::Kind::Separator;
    return it;
}
MenuItem menuPopup(std::wstring label, MenuModel items, std::string recent = {}) {
    MenuItem it;
    it.kind = MenuItem::Kind::Popup;
    it.label = std::move(label), it.items = std::move(items), it.recent = std::move(recent);
    return it;
}

MenuModel buildMenuModel() {
    MenuModel project{menuItem(ID_PROJ_NEW, L"&New EUD project", L"Ctrl+Shift+N"),
                      menuItem(ID_PROJ_OPEN, L"&Open project\x2026"),
                      menuItem(ID_PROJ_FROMMAP, L"&Make this map a project\x2026"),
                      menuSeparator(),
                      menuItem(ID_PROJ_COMPILE, L"&Build", L"Ctrl+B"),
                      menuItem(ID_PROJ_ADDSCRIPT, L"&Add script\x2026"),
                      menuItem(ID_PROJ_FOLDER, L"Open project &folder"),
                      menuSeparator(),
                      menuItem(ID_PROJ_SETTINGS, L"Project &settings")};
    MenuModel file{menuItem(ID_FILE_NEW, L"&New project", L"Ctrl+N"),
                   menuItem(ID_FILE_OPEN, L"&Open\x2026", L"Ctrl+O"),
                   menuItem(ID_QUICKOPEN, L"&Quick open\x2026", L"Ctrl+Shift+O"),
                   menuPopup(L"Recent &maps", {}, "maps"),  // (the lists are filled by refreshRecentMenus)
                   menuPopup(L"Recent pro&jects", {}, "projects"),
                   menuSeparator(),
                   menuPopup(L"&Project", project),  // EUD projects: new, open, build, settings
                   menuSeparator(),
                   menuItem(ID_FILE_SAVE, L"&Save", L"Ctrl+S"),
                   menuItem(ID_FILE_SAVEAS, L"Save &as\x2026", L"Ctrl+Shift+S"),
                   menuItem(ID_FILE_EXPORTIMAGE, L"&Export map image\x2026", L"Ctrl+Shift+E"),
                   menuSeparator(),
                   menuItem(ID_FILE_CLOSE, L"&Close", L"Ctrl+W"),
                   menuItem(ID_FILE_CLOSEALL, L"Close a&ll", L"Ctrl+Shift+W"),
                   menuSeparator(),
                   menuItem(ID_FILE_EXIT, L"E&xit")};
    MenuModel edit{menuItem(ID_EDIT_UNDO, L"&Undo", L"Ctrl+Z"),
                   menuItem(ID_EDIT_REDO, L"&Redo", L"Ctrl+Y"),
                   menuItem(ID_EDIT_HISTORY, L"Undo &history\x2026", L"Ctrl+Shift+H"),
                   menuSeparator(),
                   menuItem(ID_EDIT_CUT, L"Cu&t", L"Ctrl+X"),
                   menuItem(ID_EDIT_COPY, L"&Copy", L"Ctrl+C"),
                   menuItem(ID_EDIT_PASTE, L"&Paste", L"Ctrl+V"),
                   menuItem(ID_EDIT_DUPLICATE, L"&Duplicate", L"Ctrl+D"),
                   menuItem(ID_QUICKPICK, L"&Quick picker (the mode's units, terrains, doodads, sprites or locations; in Data, any entry)\x2026", L"Q"),
                   menuItem(ID_EDIT_DELETE, L"&Delete", L"Del"),
                   menuItem(ID_EDIT_SELECTALL, L"Select &all", L"Ctrl+A"),
                   menuItem(ID_EDIT_RENAME, L"Re&name location", L"F2")};
    MenuModel view;
    for (const char* id : {"view.hd2", "view.hd", "view.sd", "view.carbot"}) menuCommand(view, id);
    view.push_back(menuSeparator());
    menuCommand(view, "view.locations");
    menuCommand(view, "view.grid");
    {  // the grid's size (GitHub feedback #87)
        MenuModel sizes;
        for (int k = 0; k < int(std::size(kGridPresets)); ++k)
            sizes.push_back(menuItem(ID_GRID_SIZE + k, std::wstring(kGridPresets[k].name) + L" (" + std::to_wstring(kGridPresets[k].px) + L" pixels)"));
        sizes.push_back(menuSeparator());
        sizes.push_back(menuItem(ID_GRID_CUSTOM, L"&Custom\x2026"));
        view.push_back(menuPopup(L"Grid si&ze", sizes));
    }
    view.push_back(menuItem(ID_VIEW_TOWNS, L"Show town a&reas"));
    menuCommand(view, "view.ranges");
    menuCommand(view, "view.minimap");
    menuCommand(view, "view.floatPalette");
    {  // the map's layers that show (GitHub feedback #117)
        MenuModel layers;
        menuCommands(layers, "View/Layers");
        layers.push_back(menuItem(ID_VIEW_LOCATIONS, L"&Locations"));
        view.push_back(menuPopup(L"La&yers", layers));
    }
    view.push_back(menuSeparator());
    for (const char* id : {"view.zoomIn", "view.zoomOut", "view.actualSize", "view.fit"}) menuCommand(view, id);
    view.push_back(menuItem(ID_VIEW_LOG, L"&Log", L"Ctrl+L"));
    view.push_back(menuItem(ID_VIEW_TRIGCODE, L"Trigger &code pane"));
#ifndef NDEBUG  // the control kit is for developers: only in debug builds (the kit: datashot opens it in any build)
    view.push_back(menuItem(ID_VIEW_KIT, L"Control &kit"));
#endif
    view.push_back(menuSeparator());
    view.push_back(menuItem(ID_WS_MAP, L"&Map workspace", L"Ctrl+1"));
    view.push_back(menuItem(ID_WS_DATA, L"&Data workspace", L"Ctrl+2"));
    view.push_back(menuItem(ID_WS_TRIGGERS, L"&Triggers workspace", L"Ctrl+3"));
    view.push_back(menuItem(ID_WS_BRIEFING, L"&Briefing workspace"));  // (Ctrl+n: the side bar's n-th item)
    view.push_back(menuItem(ID_WS_FILES, L"&Assets workspace"));
    view.push_back(menuItem(ID_WS_PROJECT, L"&Project page"));
    MenuModel mode;
    menuCommands(mode, "Mode");
    MenuModel scenario{menuItem(ID_SCEN_PROPS, L"&Map properties (title, size, tileset)", L"Ctrl+M"),
                       menuItem(ID_SCEN_PLAYERS, L"&Players and forces", L"Ctrl+P"),
                       menuItem(ID_SCEN_CLASSIC, L"Map &data (units, upgrades, technologies)"),
                       menuItem(ID_SCEN_CLASSICTRIG, L"Classic &triggers"),
                       menuItem(ID_SCEN_CHECK, L"&Check that StarCraft can start the map")};
    // Options: the snap and placement rules of each kind (the same items for each; a rule that does not change is
    // grayed), the terrain copy, then the program options (the window).
    auto snapItems = [](int first) {
        return MenuModel{menuItem(first, L"&Do not snap"), menuItem(first + 1, L"Snap to &tiles (32 px)"),
                         menuItem(first + 2, L"Snap to &half tiles (16 px)"), menuItem(first + 3, L"Snap to &quarter tiles (8 px)"), menuSeparator()};
    };
    auto placeItems = [](int fit, int nearest, int anywhere) {
        return MenuModel{menuItem(fit, L"Place &only where it fits"), menuItem(nearest, L"&Move to the nearest place where it fits"),
                         menuItem(anywhere, L"Place &anywhere"), menuSeparator()};
    };
    auto append = [](MenuModel& to, MenuModel&& from) {
        for (MenuItem& it : from) to.push_back(std::move(it));
    };
    MenuModel units, doodads, sprites, locations;
    append(units, snapItems(ID_SNAP_UNITS));
    units.pop_back();  // (the separator goes after the buildings' snap)
    units.push_back(menuItem(ID_OPT_SNAPBUILDINGS, L"Snap &buildings to tile edges"));
    units.push_back(menuSeparator());
    append(units, placeItems(ID_OPT_STRICT, ID_OPT_SNAPVALID, ID_OPT_ILLEGAL));
    units.push_back(menuItem(ID_OPT_STACK, L"&Can be on top of others"));
    units.push_back(menuItem(ID_OPT_DRAGBACK, L"Dragged units go &back to the last place where they fit"));
    doodads.push_back(menuItem(ID_FIXED_DOODAD_SNAP, L"Snap to tiles (32 px): always, a doodad is made of tiles", {}, true, true, true));
    doodads.push_back(menuSeparator());
    append(doodads, placeItems(ID_DOODAD_FIT, ID_DOODAD_NEAREST, ID_DOODAD_ANYWHERE));
    doodads.push_back(menuItem(ID_FIXED_DOODAD_STACK, L"Can be on top of others: no, a doodad replaces the tiles under it", {}, true, false, true));
    append(sprites, snapItems(ID_SNAP_SPRITES));
    sprites.push_back(menuItem(ID_FIXED_SPRITE_PLACE, L"Place anywhere: always", {}, true, true, true));
    sprites.push_back(menuSeparator());
    sprites.push_back(menuItem(ID_FIXED_SPRITE_STACK, L"Can be on top of others: always", {}, true, true, true));
    append(locations, snapItems(ID_SNAP_LOCATIONS));
    locations.push_back(menuItem(ID_FIXED_LOC_PLACE, L"Place anywhere: always", {}, true, true, true));
    locations.push_back(menuSeparator());
    locations.push_back(menuItem(ID_FIXED_LOC_STACK, L"Can be on top of others: always", {}, true, true, true));
    locations.push_back(menuSeparator());
    locations.push_back(menuItem(ID_OPT_LOCWRAP, L"&Names in lines that fit in the location"));
    MenuModel options{menuPopup(L"&Units", units),
                      menuPopup(L"&Doodads", doodads),
                      menuPopup(L"&Sprites", sprites),
                      menuPopup(L"&Locations", locations),
                      menuSeparator(),
                      menuItem(ID_EDIT_TERRAINUNITS, L"Terrain copy includes u&nits"),
                      menuItem(ID_EDIT_TERRAINDOODADS, L"Terrain copy includes d&oodads"),
                      menuSeparator(),
                      menuItem(ID_OPT_WINDOW, L"&Program options\x2026", L"Ctrl+,")};
    MenuModel help{menuItem(ID_HELP_FEEDBACK, L"Report &feedback\x2026"),
                   menuItem(ID_HELP_ISSUE, L"Report an &issue on GitHub"),
                   menuItem(ID_HELP_DISCORD, L"Join &Discord"),
                   menuItem(ID_HELP_GITHUB, L"StarDraft 2 on &GitHub"),
                   menuSeparator(),
                   menuItem(ID_HELP_WHATSNEW, L"&What is new"),
                   menuItem(ID_HELP_UPDATES, L"Check for &updates"),
                   menuItem(ID_HELP_ABOUT, L"&About StarDraft 2")};
    return MenuModel{menuPopup(L"&File", file), menuPopup(L"&Edit", edit), menuPopup(L"&View", view), menuPopup(L"&Mode", mode),
                     menuPopup(L"&Scenario", scenario), menuPopup(L"&Options", options), menuPopup(L"&Help", help)};
}

// The dock menu of the minimap (mini) or of the active mode's toolbar (a right click on it): its name (grayed), then
// Left and Right (the side it is on checked) and, for the minimap, Hide. `right`: it is on the right now.
MenuModel dockMenuModel(bool mini, bool right, Mode mode) {
    const std::wstring what = mini ? L"Minimap" : std::wstring(kModeNames[int(mode)]) + L" toolbar";
    MenuModel m{menuItem(0, what, {}, true), menuSeparator(),
                menuItem(mini ? ID_DOCK_MINI_LEFT : ID_DOCK_TOOLS_LEFT, L"Left", {}, false, !right),
                menuItem(mini ? ID_DOCK_MINI_RIGHT : ID_DOCK_TOOLS_RIGHT, L"Right", {}, false, right)};
    if (mini) m.push_back(menuItem(ID_VIEW_MINIMAP, L"Hide"));
    return m;
}
