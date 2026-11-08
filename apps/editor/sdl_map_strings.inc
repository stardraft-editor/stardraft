// Map data's Strings tab on this system: the core's view (core/views_map_strings.inc: MapStringsView) as the fourth tab of
// the Data page of a classic map (an EUD project's strings are its Data workspace's). sdl_map_pages.inc makes the view
// with the page's other views and shows it in place of the Units, Upgrades and Technologies tabs. The view changes the
// map itself (each edit one undo step), so the page's copy of the other tabs reads the map again when one of them shows.
// A link of the References sidebar opens the use's page. Included by sdl_map_pages.inc.

bool gSdlStringsTab = false;  // Map data shows its Strings tab
void sdlMapDataShowUnit(int type);
void sdlShowStringsTab();  // (sdl_map_pages.inc)

// A use's page: the map's properties (the scenario, the forces), the unit's entry, the location on the map, the
// trigger's action, the briefing, the sound files. (After the click: the page's views go when the workspace changes.)
void sdlStringsGoTo(const StringUse& u) {
    shellPost([u] {
        switch (u.kind) {
        case StringUseKind::ScenarioName:
        case StringUseKind::ScenarioDescription:
        case StringUseKind::ForceName: return command(ID_SCEN_PROPS);
        case StringUseKind::UnitName: return sdlMapDataShowUnit(u.index);
        case StringUseKind::LocationName:
            setWorkspace(Workspace::Map);
            setMode(Mode::Locations);
            if (gState.ed) gState.ed->selectLocation(u.index);
            return setMessage(L"Selected location " + std::to_wstring(u.index + 1));
        case StringUseKind::SwitchName: return command(ID_SCEN_CLASSICTRIG);
        case StringUseKind::TriggerText:
        case StringUseKind::TriggerSound:
            command(ID_SCEN_CLASSICTRIG);
            return sdlClassicTrigSelect(std::to_string(u.index + 1) + ":1:" + std::to_string(u.action));
        case StringUseKind::SoundFile: return setWorkspace(Workspace::Files);
        case StringUseKind::BriefingText:
        case StringUseKind::BriefingSound: return setWorkspace(Workspace::Briefing);
        }
    });
}
