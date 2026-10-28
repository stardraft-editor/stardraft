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

std::vector<std::pair<int, std::wstring>> railMenuItems();  // (core/views_rail.inc: the side bar's items)

// The two-key shortcuts of the menu items that have no key of their own (sdl_menus.inc: the first key, then the
// second: always one letter or digit, with no Shift), by the item's menu path in English: Ctrl+K the View menu's (but
// View > Layers and View > Floating palettes: Ctrl+Shift+L), Ctrl+J the Options menu's, Ctrl+Shift+K the others' (File,
// Tools, Help).
void assignMenuSequences(MenuModel& items, const std::wstring& path = {}) {
    static const std::map<std::wstring, std::wstring> kKeys = {
        {L"File/Project/Open project", L"Ctrl+Shift+K, O"},
        {L"File/Project/Make this map a project", L"Ctrl+Shift+K, M"},
        {L"File/Project/Import a project (experimental)", L"Ctrl+Shift+K, I"},
        {L"File/Project/Add script", L"Ctrl+Shift+K, S"},
        {L"File/Project/Open project folder", L"Ctrl+Shift+K, F"},
        {L"File/Project/Project settings", L"Ctrl+Shift+K, P"},
        {L"File/Exit", L"Ctrl+Shift+K, X"},
        {L"View/Graphics/High definition (2x, default)", L"Ctrl+K, E"},
        {L"View/Graphics/High definition (4x)", L"Ctrl+K, K"},
        {L"View/Graphics/Standard definition", L"Ctrl+K, S"},
        {L"View/Graphics/Carbot (cartooned)", L"Ctrl+K, C"},
        {L"View/Grid/Fine (8 pixels)", L"Ctrl+K, 1"},
        {L"View/Grid/Small (16 pixels)", L"Ctrl+K, 2"},
        {L"View/Grid/Normal (32 pixels)", L"Ctrl+K, 3"},
        {L"View/Grid/Large (64 pixels)", L"Ctrl+K, 4"},
        {L"View/Grid/Huge (128 pixels)", L"Ctrl+K, 5"},
        {L"View/Grid/Custom", L"Ctrl+K, 6"},
        {L"View/Overlays/Town areas", L"Ctrl+K, T"},
        {L"View/Overlays/Pathing/Regions", L"Ctrl+K, P"},
        {L"View/Overlays/Pathing/Connected areas", L"Ctrl+K, A"},
        {L"View/Overlays/Terrain flags/Walkable ground", L"Ctrl+K, W"},
        {L"View/Overlays/Terrain flags/Ground height", L"Ctrl+K, H"},
        {L"View/Overlays/Terrain flags/Ground that blocks the line of sight", L"Ctrl+K, B"},
        {L"View/Overlays/Terrain flags/Buildable ground", L"Ctrl+K, G"},
        {L"View/Overlays/Unit vision and ranges/All ranges", L"Ctrl+K, R"},
        {L"View/Overlays/Unit vision and ranges/Sight", L"Ctrl+K, V"},
        {L"View/Overlays/Unit vision and ranges/Target acquisition", L"Ctrl+K, Q"},
        {L"View/Overlays/Unit vision and ranges/Ground weapon", L"Ctrl+K, Y"},
        {L"View/Overlays/Unit vision and ranges/Air weapon", L"Ctrl+K, J"},
        {L"View/Overlays/Unit vision and ranges/Detection", L"Ctrl+K, D"},
        {L"View/Overlays/Unit vision and ranges/Psi field", L"Ctrl+K, I"},
        {L"View/Overlays/Turn all off", L"Ctrl+K, O"},
        {L"View/Layers/Terrain", L"Ctrl+Shift+L, T"},
        {L"View/Layers/Doodads", L"Ctrl+Shift+L, D"},
        {L"View/Layers/Sprites", L"Ctrl+Shift+L, S"},
        {L"View/Layers/Units", L"Ctrl+Shift+L, U"},
        {L"View/Layers/Map revealers", L"Ctrl+Shift+L, R"},
        {L"View/Layers/Creep", L"Ctrl+Shift+L, C"},
        {L"View/Layers/Locations", L"Ctrl+Shift+L, L"},
        {L"View/Animate units", L"Ctrl+K, N"},
        {L"View/Show minimap", L"Ctrl+K, M"},
        {L"View/Floating palettes/Terrain palette", L"Ctrl+Shift+L, 1"},
        {L"View/Floating palettes/Unit palette", L"Ctrl+Shift+L, 2"},
        {L"View/Floating palettes/Doodad palette", L"Ctrl+Shift+L, 3"},
        {L"View/Floating palettes/Sprite palette", L"Ctrl+Shift+L, 4"},
        {L"View/Floating palettes/Location list", L"Ctrl+Shift+L, 5"},
        {L"View/Units list", L"Ctrl+K, U"},
        {L"View/Data/Entry view", L"Ctrl+K, L"},
        {L"View/Triggers/Trigger code pane", L"Ctrl+K, X"},
        {L"Tools/Check that StarCraft can start the map", L"Ctrl+Shift+K, C"},
        {L"Tools/Check pathing (nooks and crannies)", L"Ctrl+Shift+K, H"},
        {L"Tools/Decorate the terrain/A few doodads", L"Ctrl+Shift+K, 1"},
        {L"Tools/Decorate the terrain/Some doodads", L"Ctrl+Shift+K, 2"},
        {L"Tools/Decorate the terrain/Many doodads", L"Ctrl+Shift+K, 3"},
        {L"Tools/Place map revealers", L"Ctrl+Shift+K, R"},
        {L"Tools/Remove all map revealers", L"Ctrl+Shift+K, E"},
        {L"Tools/Cosmonarchy/Try to repair the ISOM", L"Ctrl+Shift+K, Y"},
        {L"Tools/Cosmonarchy/Check the CRIG commands", L"Ctrl+Shift+K, K"},
        {L"Options/Units/Do not snap", L"Ctrl+J, 1"},
        {L"Options/Units/Snap to tiles (32 px)", L"Ctrl+J, 2"},
        {L"Options/Units/Snap to half tiles (16 px)", L"Ctrl+J, 3"},
        {L"Options/Units/Snap to quarter tiles (8 px)", L"Ctrl+J, 4"},
        {L"Options/Units/Snap buildings to tile edges", L"Ctrl+J, B"},
        {L"Options/Units/Place only where it fits", L"Ctrl+J, F"},
        {L"Options/Units/Move to the nearest place where it fits", L"Ctrl+J, N"},
        {L"Options/Units/Place anywhere", L"Ctrl+J, A"},
        {L"Options/Units/Can be on top of others", L"Ctrl+J, T"},
        {L"Options/Units/Keep resources 3 tiles from town halls", L"Ctrl+J, R"},
        {L"Options/Units/Randomize unit direction when placed", L"Ctrl+J, D"},
        {L"Options/Units/Dragged units go back to the last place where they fit", L"Ctrl+J, G"},
        {L"Options/Doodads/Place only where it fits", L"Ctrl+J, P"},
        {L"Options/Doodads/Move to the nearest place where it fits", L"Ctrl+J, M"},
        {L"Options/Doodads/Place anywhere", L"Ctrl+J, Y"},
        {L"Options/Sprites/Do not snap", L"Ctrl+J, 5"},
        {L"Options/Sprites/Snap to tiles (32 px)", L"Ctrl+J, 6"},
        {L"Options/Sprites/Snap to half tiles (16 px)", L"Ctrl+J, 7"},
        {L"Options/Sprites/Snap to quarter tiles (8 px)", L"Ctrl+J, 8"},
        {L"Options/Locations/Do not snap", L"Ctrl+J, Z"},
        {L"Options/Locations/Snap to tiles (32 px)", L"Ctrl+J, X"},
        {L"Options/Locations/Snap to half tiles (16 px)", L"Ctrl+J, C"},
        {L"Options/Locations/Snap to quarter tiles (8 px)", L"Ctrl+J, V"},
        {L"Options/Locations/Names in lines that fit in the location", L"Ctrl+J, L"},
        {L"Options/Terrain copy includes units", L"Ctrl+J, U"},
        {L"Options/Terrain copy includes doodads", L"Ctrl+J, O"},
        {L"Help/Report feedback", L"Ctrl+Shift+K, B"},
        {L"Help/Report an issue on GitHub", L"Ctrl+Shift+K, G"},
        {L"Help/Join Discord", L"Ctrl+Shift+K, D"},
        {L"Help/StarDraft 2 on GitHub", L"Ctrl+Shift+K, L"},
        {L"Help/What is new", L"Ctrl+Shift+K, W"},
        {L"Help/Check for updates", L"Ctrl+Shift+K, U"},
        {L"Help/About StarDraft 2", L"Ctrl+Shift+K, A"},
    };
    for (MenuItem& it : items) {
        std::wstring name;
        for (wchar_t c : it.label)
            if (c != L'&' && c != wchar_t(0x2026)) name += c;
        const std::wstring at = path.empty() ? name : path + L"/" + name;
        if (it.kind == MenuItem::Kind::Popup) assignMenuSequences(it.items, at);
        else if (it.kind == MenuItem::Kind::Command && it.shortcut.empty())
            if (auto k = kKeys.find(at); k != kKeys.end()) it.shortcut = k->second;
    }
}

MenuModel buildMenuModel() {
    MenuModel project{menuItem(ID_PROJ_NEW, L"&New EUD project", L"Ctrl+Shift+N"),
                      menuItem(ID_PROJ_OPEN, L"&Open project\x2026"),
                      menuItem(ID_PROJ_FROMMAP, L"&Make this map a project\x2026"),
                      menuItem(ID_PROJ_IMPORT, L"&Import a project (experimental)\x2026"),  // EUD Editor 2, euddraft (roadmap R52)
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
                   // (File > Campaign, roadmap R25: not offered until the campaign editor is ready; core/views_campaign.inc)
                   menuSeparator(),
                   menuItem(ID_FILE_SAVE, L"&Save", L"Ctrl+S"),
                   menuItem(ID_FILE_SAVEAS, L"Save &as\x2026", L"Ctrl+Shift+S"),
                   menuItem(ID_FILE_EXPORTIMAGE, L"&Export map image\x2026", L"Ctrl+Shift+E"),
                   menuSeparator(),
                   menuItem(ID_FILE_CLOSE, L"&Close", L"Ctrl+W"),
                   menuItem(ID_FILE_CLOSEALL, L"Close a&ll", L"Ctrl+Shift+W"),
                   menuSeparator(),
                   menuItem(ID_FILE_EXIT, L"E&xit")};
    if (cmbwMode()) {  // (Cosmonarchy BW mode: maps only; File > New makes a Cosmonarchy map, no projects)
        std::erase_if(file, [&project](const MenuItem& it) { return it.kind == MenuItem::Kind::Popup && (it.recent == "projects" || it.items.size() == project.size()); });
        if (!file.empty()) file.front().label = L"&New Cosmonarchy map";
    }
    MenuModel edit{menuItem(ID_EDIT_UNDO, L"&Undo", L"Ctrl+Z"),
                   menuItem(ID_EDIT_REDO, L"&Redo", L"Ctrl+Y"),
                   menuItem(ID_EDIT_HISTORY, L"Undo &history\x2026", L"Ctrl+Shift+H"),
                   menuSeparator(),
                   menuItem(ID_EDIT_CUT, L"Cu&t", L"Ctrl+X"),
                   menuItem(ID_EDIT_COPY, L"&Copy", L"Ctrl+C"),
                   menuItem(ID_EDIT_PASTE, L"&Paste", L"Ctrl+V"),
                   menuItem(ID_EDIT_DUPLICATE, L"&Duplicate", L"Ctrl+D"),
                   menuItem(ID_TRIG_FIND, L"&Find in triggers\x2026", L"Ctrl+Shift+F"),
                   menuItem(ID_EDIT_DELETE, L"&Delete", L"Del"),
                   menuItem(ID_EDIT_SELECTALL, L"Select &all", L"Ctrl+A"),
                   menuItem(ID_EDIT_RENAME, L"Re&name location", L"F2")};
    // The Tools menu: the checks of the map, doodads where they fit (roadmap R37), the map revealers and the simulation.
    MenuModel tools{menuItem(ID_SCEN_CHECK, L"&Check that StarCraft can start the map")};
    {
        menuCommand(tools, "scen.checkPathing");
        tools.push_back(menuSeparator());
        MenuModel decorate;
        menuCommands(decorate, "Edit/Decorate");
        tools.push_back(menuPopup(L"&Decorate the terrain", decorate));
        tools.push_back(menuSeparator());  // the map revealers (Discord feedback #125)
        menuCommand(tools, "edit.revealers.place");
        menuCommand(tools, "edit.revealers.removeAll");
#ifdef STARDRAFT_SIMULATION
        tools.push_back(menuSeparator());  // (experimental: the game simulated)
        menuCommand(tools, "edit.simulate.mining");
        menuCommand(tools, "edit.simulate.combat");
#endif
    }
    // The View menu in groups: the graphics, the grid, what shows on the map (overlays, layers), the zoom, the panels,
    // the workspaces.
    MenuModel view;
    {
        MenuModel graphics;
        for (const char* id : {"view.hd2", "view.hd", "view.sd", "view.carbot"}) menuCommand(graphics, id);
        view.push_back(menuPopup(L"G&raphics", graphics));
    }
    view.push_back(menuSeparator());
    {  // Grid: Show grid, then the grid's sizes (GitHub feedback #87)
        MenuModel grid;
        menuCommand(grid, "view.grid");
        grid.push_back(menuSeparator());
        for (int k = 0; k < int(std::size(kGridPresets)); ++k)
            grid.push_back(menuItem(ID_GRID_SIZE + k, std::wstring(kGridPresets[k].name) + L" (" + std::to_wstring(kGridPresets[k].px) + L" pixels)"));
        grid.push_back(menuSeparator());
        grid.push_back(menuItem(ID_GRID_CUSTOM, L"&Custom\x2026"));
        view.push_back(menuPopup(L"&Grid", grid));
    }
    view.push_back(menuSeparator());
    {  // what shows over the map: the town areas, the pathing (core/pathing_layer.inc), the terrain flags
       // (core/terrain_flags_layer.inc), the ranges of the selected units
        // (first the overlays that are on or off, then those with views: a submenu each, whose item that is on turns off
        // when it is clicked again; then Turn all off)
        MenuModel overlays;
        overlays.push_back(menuItem(ID_VIEW_TOWNS, L"Town a&reas"));
        menuCommands(overlays, "View/Fog");  // (core/fog_layer.inc)
        overlays.push_back(menuSeparator());
        MenuModel pathing, flags;
        menuCommands(pathing, "View/Pathing");
        overlays.push_back(menuPopup(L"&Pathing", pathing));
        menuCommands(flags, "View/Terrain flags");
        overlays.push_back(menuPopup(L"&Terrain flags", flags));
        {  // the sight and the weapon ranges of the selected units (Units mode), all or each kind (GitHub feedback #109)
            MenuModel ranges;
            menuCommand(ranges, "view.ranges");
            ranges.push_back(menuSeparator());
            menuCommands(ranges, "View/Unit vision and ranges");
            overlays.push_back(menuPopup(L"&Unit vision and ranges", ranges));
        }
        overlays.push_back(menuSeparator());
        menuCommand(overlays, "view.overlays.off");
        view.push_back(menuPopup(L"&Overlays", overlays));
    }
    {  // the map's layers that show (GitHub feedback #117)
        MenuModel layers;
        menuCommands(layers, "View/Layers");
        layers.push_back(menuItem(ID_VIEW_LOCATIONS, L"&Locations"));
        view.push_back(menuPopup(L"La&yers", layers));
    }
    view.push_back(menuItem(ID_OPT_ANIMATE, L"A&nimate units"));  // (the units' idle animations on the map)
    view.push_back(menuSeparator());
    for (const char* id : {"view.zoomIn", "view.zoomOut", "view.actualSize", "view.fit"}) menuCommand(view, id);
    view.push_back(menuSeparator());
    view.push_back(menuItem(ID_QUICKPICK, L"&Quick picker\x2026", L"Q"));
    view.push_back(menuItem(ID_COMMANDS, L"Find a &command\x2026", L"Ctrl+Shift+P"));  // (each menu item, on or off: sdl_menus.inc)
    view.push_back(menuSeparator());
    menuCommand(view, "view.minimap");
    {  // each mode's palette in a window of its own (Roadmap R40: core/dock_prefs.inc, kFloatPalNames)
        MenuModel floating;
        menuCommands(floating, "View/Floating palettes");
        view.push_back(menuPopup(L"Floating &palettes", floating));
    }
    menuCommand(view, "view.unitsList");  // (the Units mode's side bar: the list of the units; Discord feedback #126)
    view.push_back(menuItem(ID_VIEW_LOG, L"&Log", L"Ctrl+M"));
    {  // each workspace's own views, in a submenu with its name (gray outside it)
        MenuModel data{menuItem(ID_DATA_ENTRY, L"&Entry view"), menuItem(ID_DATA_TABLE, L"&Table view", L"Ctrl+E")};
        view.push_back(menuPopup(L"&Data", data));
        MenuModel triggers{menuItem(ID_VIEW_TRIGCODE, L"Trigger &code pane")};
        view.push_back(menuPopup(L"T&riggers", triggers));
    }
#ifndef NDEBUG  // the control kit is for developers: only in debug builds (the kit: datashot opens it in any build)
    view.push_back(menuItem(ID_VIEW_KIT, L"Control &kit"));
#endif
    view.push_back(menuSeparator());
    {  // the side bar's items, in its order, with their keys (Ctrl+n: the n-th item; core/views_rail.inc)
        MenuModel workspaces;
        int n = 0;
        for (const auto& [id, name] : railMenuItems()) {
            ++n;
            workspaces.push_back(menuItem(id, L"&" + name, n <= 9 ? L"Ctrl+" + std::to_wstring(n) : L""));
        }
        view.push_back(menuPopup(L"&Workspaces", workspaces));
    }
    MenuModel mode;
    menuCommands(mode, "Mode");
    // (No Scenario menu: Map properties, Players and forces, Map data and Classic triggers are the side bar's items
    // (their keys: Ctrl+1 to Ctrl+9; Players and forces is part of Properties), and the checks are in Tools.)
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
    units.push_back(menuItem(ID_OPT_RESDIST, L"Keep &resources 3 tiles from town halls"));
    units.push_back(menuItem(ID_OPT_RANDOMFACING, L"&Randomize unit direction when placed"));
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
    MenuModel help{menuItem(ID_HELP_DOCS, L"&Documentation", L"F1"),
                   menuSeparator(),
                   menuItem(ID_HELP_FEEDBACK, L"Report &feedback\x2026"),
                   menuItem(ID_HELP_ISSUE, L"Report an &issue on GitHub"),
                   menuItem(ID_HELP_DISCORD, L"Join &Discord"),
                   menuItem(ID_HELP_GITHUB, L"StarDraft 2 on &GitHub"),
                   menuSeparator(),
                   menuItem(ID_HELP_WHATSNEW, L"&What is new"),
                   menuItem(ID_HELP_UPDATES, L"Check for &updates"),
                   menuItem(ID_HELP_ABOUT, L"&About StarDraft 2")};
    if (cmbwMode()) {  // (Cosmonarchy BW mode: its own tools, a submenu at the end of Tools)
        MenuModel cosmonarchy;
        menuCommand(cosmonarchy, "edit.cmbw.repairIsom");
        menuCommand(cosmonarchy, "scen.cmbw.checkComments");
        tools.push_back(menuSeparator());
        tools.push_back(menuPopup(L"&Cosmonarchy", cosmonarchy));
    }
    MenuModel bar{menuPopup(L"&File", file), menuPopup(L"&Edit", edit), menuPopup(L"&View", view), menuPopup(L"&Mode", mode),
                  menuPopup(L"&Tools", tools), menuPopup(L"&Options", options), menuPopup(L"&Help", help)};
    assignMenuSequences(bar);
    return bar;
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
