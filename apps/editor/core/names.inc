// The names of the entries (portable, core.inc): moved from main.cpp. paletteItemName is the palette's label of a value
// (a terrain, a doodad, a sprite or a unit); entryName is the name of an entry of a .dat (the units' and the buttons' names
// come from the Data workspace, which stays in main.cpp).

AppState& shellState();
std::wstring projectUnitName(int u);  // core/data_model.inc: the project's name of the unit when its string is changed, else ""

// A unit's game name with its custom name `c`, in the format of Program options > Unit names.
std::wstring formatUnitName(const std::wstring& game, const std::wstring& c) {
    if (c.empty() || c == game || gDock.unitNames == 0) return game;
    return gDock.unitNames == 1 ? game + L" (" + c + L")" : gDock.unitNames == 2 ? c + L" (" + game + L")" : c;
}
// A unit's name with its custom name (the map's unit settings), as Program options > Unit names says (feedback #68).
// The custom names are read once for each version of the map.
std::wstring withCustomName(const std::wstring& game, int u) {
    AppState& app = shellState();
    if (!app.ed || u < 0 || u >= kSettingUnits || gDock.unitNames == 0) return game;
    static const Editor* forEd = nullptr;
    static uint64_t forRev = ~0ull;
    static std::vector<std::wstring> custom;
    if (forEd != app.ed.get() || forRev != app.ed->revision()) {
        forEd = app.ed.get(), forRev = app.ed->revision();
        custom.assign(kSettingUnits, L"");
        const UnitSettings s = app.ed->unitSettings();
        for (int i = 0; i < kSettingUnits; ++i)
            if (!s.units[size_t(i)].useDefault && s.units[size_t(i)].nameString) custom[size_t(i)] = widen(app.ed->unitSettingName(i));
    }
    return formatUnitName(game, custom[size_t(u)]);
}
// A unit's name: the game's, or the project's own, with its subtitle and its custom name (the Data workspace's names too).
std::wstring unitDisplayName(int u) {
    AppState& app = shellState();
    if (!app.ed || u < 0 || u >= int(app.ed->unitTypes().size())) return L"";
    const UnitType& t = app.ed->unitTypes()[uint16_t(u)];
    // The project's name when its string is changed (Data > Strings, or Rename at the top of the unit's form).
    const std::wstring own = projectUnitName(u);
    return withCustomName((own.empty() ? widen(t.name) : own) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")"), u);
}

std::wstring paletteItemName(int v) {
    AppState& app = shellState();
    if (!app.ed || v < 0) return L"";
    if (app.mode == Mode::Terrain) {
        const IsomTerrainTable* t = IsomTerrainTable::forEra(app.ed->era());
        return t ? widen(t->terrainName(uint16_t(v))) : L"";
    }
    if (app.mode == Mode::Doodads) {
        const DoodadType* d = app.ed->doodadTypes().find(uint16_t(v));
        return d ? widen(app.ed->unitTypes().statText(d->groupString - 1u)) + L"  #" + std::to_wstring(v) : L"";
    }
    if (app.mode == Mode::Sprites) return entryName("sprites", v) + L"  #" + std::to_wstring(v);
    const UnitType& t = app.ed->unitTypes()[uint16_t(v)];
    return widen(t.name) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")");
}
// The name of an entry: units from stat_txt.tbl, the rest from the generated lists ("" when unnamed).
std::wstring buttonSetName(int s);
std::wstring entryName(const std::string& dat, int v) {
    if (dat == "units") return unitDisplayName(v);
    if (dat == "buttons") return v >= 0 && v < ButtonSets::kSets ? buttonSetName(v) : L"";
    const auto& names = DatSchema::entryNames(dat);
    return v >= 0 && v < int(names.size()) ? widen(names[size_t(v)]) : L"";
}
