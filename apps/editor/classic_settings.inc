// The Map data page (classic settings; roadmap L.3; classic maps only; included into main.cpp after map_properties.inc; map_pages.inc shows it in
// the main area): the map's own unit, upgrade and tech settings, as StarEdit's Unit, Upgrade and Tech settings edit
// them. Only for a classic map (a project's map keeps the sections; the Data workspace edits). The page's views are in the
// core (core/views_classic_settings.inc): three tabs, each a list of the entries and the fields of the selected entry.
// This file is the program's part: the copy from the map and the apply (Apply sets the three tabs as one undo step:
// Editor::setUnitSettings, setUpgradeSettings, setTechSettings write only what changed), the entries' names and icons,
// and the game text pop-out for a unit's name.

// The entry's name in the list and the title: a unit's game name and its custom name (Program options > Unit names).
std::wstring classicEntryName(int tab, int e) {
    if (tab == kClassicUnits && app.ed && e >= 0 && e < int(app.ed->unitTypes().size())) {
        const UnitType& t = app.ed->unitTypes()[uint16_t(e)];
        const std::wstring game = widen(t.name) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")");
        const bool own = !gClassic->units.units[size_t(e)].useDefault;
        return formatUnitName(game, own ? gameTextPlain(gClassic->names[size_t(e)]) : L"");  // (its colors: in the name's editor)
    }
    std::wstring n = entryName(tab == kClassicUpgrades ? "upgrades" : "techdata", e);
    return n.empty() ? L"(" + std::to_wstring(e) + L")" : n;
}

const UnitPalette::Bitmap* classicEntryIcon(int tab, int e) {
    const SettingDefaults& def = *gClassic->def;
    if (tab == kClassicUnits) return unitPaletteBitmap(e, Mode::Units);
    const uint16_t icon = tab == kClassicUpgrades ? def.upgradeIcon[size_t(e)] : def.techIcon[size_t(e)];
    return icon < 0xFFFF ? unitPaletteBitmap(int(icon), Mode::Units, true) : nullptr;
}

void mapPageChanged();
void mapPageTyped();
int mapPageScrollX();
int mapPageScrollY();
void mapPageScrollTo(int x, int y);
bool editGameText(const std::wstring& title, std::string& text, POINT at);  // main.cpp: the game text pop-out (colors, Preview and Raw)

// The program's part of the Map data page's views (core/views_classic_settings.inc: ClassicHost): the map's changes, the
// names, the icons and the game text pop-out of a unit's name (map_pages.inc shows the page).
class ClassicProgram final : public ClassicHost {
public:
    HWND wnd[kClassicTabCount] = {};  // each tab's page window (the views' windows, in the page's scroll view)
    void changed() override { mapPageChanged(); }
    void typed() override { mapPageTyped(); }
    std::wstring entryName(int tab, int e) override { return classicEntryName(tab, e); }
    std::wstring unitGameName(int u) override { return widen(app.ed->unitTypes()[uint16_t(u)].name); }
    bool hasShields(int u) override { return app.ed->unitTypes()[uint16_t(u)].hasShields; }
    void drawIcon(draw::Canvas& c, int tab, int e, draw::Rect box) override {
        if (const UnitPalette::Bitmap* pic = classicEntryIcon(tab, e); pic && pic->bmp) c.fitted(ui::imageOf(pic), box);
    }
    bool editUnitName(int u, std::string& text, draw::Point at) override {
        POINT p{at.x, at.y};
        ClientToScreen(wnd[kClassicUnits], &p);  // the pop-out under the title
        return editGameText(::tr(L"Unit name   (empty: ") + unitGameName(u) + L")", text, p);
    }
    int textWidth(const std::wstring& t) override {
        HDC dc = GetDC(app.main);
        HGDIOBJ of = SelectObject(dc, app.font);
        SIZE sz{};
        GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &sz);
        SelectObject(dc, of);
        ReleaseDC(app.main, dc);
        return sz.cx;
    }
    void fitSize(int tab, int w, int h) override {
        if (!wnd[tab]) return;
        SetWindowPos(wnd[tab], nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        mapPageScrollTo(mapPageScrollX(), mapPageScrollY());  // the page under it, and the scroll range
    }
};
ClassicProgram gClassicProgram;

// The page's working copy from the map. False: the game's .dat files do not read (no page).
bool classicLoad(ClassicDialog& d) {
    const SettingDefaults* def = app.ed->settingDefaults();
    if (!def) return false;
    d.def = def;
    d.bw = settingsBroodWar(app.ed->chk());
    d.units = app.ed->unitSettings(), d.upgrades = app.ed->upgradeSettings(), d.techs = app.ed->techSettings();
    for (int u = 0; u < kSettingUnits; ++u) d.names[size_t(u)] = app.ed->unitSettingName(u);
    d.namesBefore = d.names, d.unitsBefore = d.units, d.upgradesBefore = d.upgrades, d.techsBefore = d.techs;
    return true;
}

// Something to apply: the copy differs from the map's values when the page read them.
bool classicChanged(const ClassicDialog& d) {
    return d.def && (d.names != d.namesBefore || d.units != d.unitsBefore || d.upgrades != d.upgradesBefore || d.techs != d.techsBefore);
}

// Apply: the changed kinds as one undo step (Editor::setUnitSettings, setUpgradeSettings, setTechSettings write only
// what changed).
bool classicApply(ClassicDialog& d) {
    std::vector<std::pair<int, std::string>> names;
    for (int u = 0; u < kSettingUnits; ++u)
        if (d.names[size_t(u)] != d.namesBefore[size_t(u)]) names.emplace_back(u, d.names[size_t(u)]);
    const bool units = d.units != app.ed->unitSettings() || !names.empty(), upgrades = d.upgrades != app.ed->upgradeSettings(),
               techs = d.techs != app.ed->techSettings();
    const int kinds = int(units) + int(upgrades) + int(techs);
    if (!kinds) {
        setMessage(L"Map data: no change");
        return true;
    }
    if (kinds > 1) app.ed->beginStroke();
    if (units) app.ed->setUnitSettings(d.units, names);
    if (upgrades) app.ed->setUpgradeSettings(d.upgrades);
    if (techs) app.ed->setTechSettings(d.techs);
    if (kinds > 1) app.ed->endStroke();
    updateTitle();
    if (units) rebuildUnitPalette();  // the custom names in the palette (Program options > Unit names)
    app.dirty = true;
    setMessage(L"Set the map data");
    return true;
}
