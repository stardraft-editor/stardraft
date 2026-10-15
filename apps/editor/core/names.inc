// The names of the entries (portable, core.inc): moved from main.cpp. paletteItemName is the palette's label of a value
// (a terrain, a doodad, a sprite or a unit); entryName is the name of an entry of a .dat (the units' and the buttons' names
// come from the Data workspace, which stays in main.cpp).

AppState& shellState();
std::wstring unitDisplayName(int u);  // main.cpp (the Data workspace's names: the project's own names too)

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
