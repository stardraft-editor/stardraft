// The Map data page (classic settings; roadmap L.3; classic maps only; included into main.cpp after map_properties.inc; map_pages.inc shows it in
// the main area): the map's own unit, upgrade and tech settings, as StarEdit's Unit, Upgrade and Tech settings edit
// them. Only for a classic map (a project's map keeps the sections; the Data workspace edits). Three tabs; each tab has a list (the entries with their
// icons) and the fields of the selected entry. "Use default" grays the fields and shows the game's values. Apply sets
// the three tabs as one undo step (Editor::setUnitSettings, setUpgradeSettings, setTechSettings write only what changed).

enum ClassicTab { kClassicUnits, kClassicUpgrades, kClassicTechs, kClassicTabCount };
const wchar_t* const kClassicTabNames[kClassicTabCount] = {L"Units", L"Upgrades", L"Technologies"};
const UiIcon kClassicTabIcons[kClassicTabCount] = {UiIcon::DatUnits, UiIcon::DatUpgrades, UiIcon::DatTech};
constexpr int kClassicPlayers = 8;  // players 1-8 show; 9-12 stay as they are

// Control ids on a page.
enum : int {
    kCsField = 100,         // + field (the value fields of the kind)
    kCsName = 120,          // units: the name (in place of the title while it is renamed)
    kCsRename = 121,        // units: the rename button after the title
    kCsUseDefault = 200,
    kCsPlayerDefault = 300,  // + player
    kCsPlayerA = 320,        // + player: units can build; upgrades max level; techs available
    kCsPlayerB = 340,        // + player: upgrades start level; techs researched
    kCsGlobalA = 360, kCsGlobalB = 361,
    kCsSearch = 400, kCsList = 401,
    kCsWeaponDamage = 500,  // + row
    kCsWeaponBonus = 510,   // + row
    kCsDim = 1,             // a dim label
};
constexpr int kCsWeaponRows = 3;

struct ClassicPage {
    HWND wnd = nullptr, search = nullptr, list = nullptr, title = nullptr, useDefault = nullptr, name = nullptr, rename = nullptr;
    std::string nameBefore;  // the name when the rename started (Esc gives it back)
    std::vector<HWND> fields;
    HWND weaponName[kCsWeaponRows]{}, weaponDamage[kCsWeaponRows]{}, weaponBonus[kCsWeaponRows]{};
    HWND globalA = nullptr, globalB = nullptr;
    HWND playerDefault[kClassicPlayers]{}, playerA[kClassicPlayers]{}, playerB[kClassicPlayers]{};
    std::vector<int> shown;  // the entries in the list, in order
    int entry = 0;
    int weapons[kCsWeaponRows]{};  // the weapons of the selected unit in the rows (-1: none)
};

struct ClassicDialog {
    UnitSettings units;
    UpgradeSettings upgrades;
    TechSettings techs;
    std::array<std::string, kSettingUnits> names;  // the units' map-string names, as edited ("" = the game's name)
    // The map's values when the page read them (a change is a difference from these).
    std::array<std::string, kSettingUnits> namesBefore;
    UnitSettings unitsBefore;
    UpgradeSettings upgradesBefore;
    TechSettings techsBefore;
    const SettingDefaults* def = nullptr;
    bool bw = true, filling = false;
    ClassicPage pages[kClassicTabCount];
    int tab = 0;  // the tab that shows: units, upgrades or tech
    HBRUSH readOnlyBrush = nullptr;  // while the page shows
};
ClassicDialog* gClassic = nullptr;     // the working copy of the page that shows (map_pages.inc)
std::wstring unitDisplayName(int u);   // main.cpp, after the Data workspace
void mapPageChanged();

int classicEntryCount(int tab) {
    const bool bw = gClassic->bw;
    return tab == kClassicUnits ? kSettingUnits : tab == kClassicUpgrades ? (bw ? kSettingUpgrades : kOriginalUpgrades) : (bw ? kSettingTechs : kOriginalTechs);
}

std::wstring formatUnitName(const std::wstring& game, const std::wstring& c);
std::wstring classicEntryName(int tab, int e) {
    if (tab == kClassicUnits && app.ed && e >= 0 && e < int(app.ed->unitTypes().size())) {  // with the page's name (Program options > Unit names)
        const UnitType& t = app.ed->unitTypes()[uint16_t(e)];
        const std::wstring game = widen(t.name) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")");
        const bool own = !gClassic->units.units[size_t(e)].useDefault;
        return formatUnitName(game, own ? widen(gClassic->names[size_t(e)]) : L"");
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

// Is the entry different from the game's values (a dot in the list)?
bool classicEntryChanged(int tab, int e) {
    const ClassicDialog& d = *gClassic;
    const size_t i = size_t(e);
    auto anyOverride = [](const std::array<bool, kSettingPlayers>& usesDefault) {
        for (int p = 0; p < kClassicPlayers; ++p)  // the players that show (9-12 are often set in maps, and unused)
            if (!usesDefault[size_t(p)]) return true;
        return false;
    };
    if (tab == kClassicUnits) return !d.units.units[i].useDefault || !d.units.availability[i].global || anyOverride(d.units.availability[i].usesDefault);
    if (tab == kClassicUpgrades) {
        const auto &l = d.upgrades.levels[i], &dl = d.def->upgrades.levels[i];
        return !d.upgrades.upgrades[i].useDefault || l.defaultMax != dl.defaultMax || l.defaultStart != dl.defaultStart || anyOverride(l.usesDefault);
    }
    // Techs: the defaults for all players have no game value to compare with (maps differ in what starts researched).
    return !d.techs.techs[i].useDefault || anyOverride(d.techs.players[i].usesDefault);
}

// The value fields of each kind: their names, and the values (of the map, or the game's).
int classicFieldCount(int tab) { return tab == kClassicUnits ? 6 : tab == kClassicUpgrades ? 6 : 4; }
const wchar_t* classicFieldName(int tab, int f) {
    static const wchar_t* const units[] = {L"Hit points", L"Shields", L"Armor", L"Build time", L"Minerals", L"Gas"};
    static const wchar_t* const upgrades[] = {L"Minerals", L"Minerals per level", L"Gas", L"Gas per level", L"Time", L"Time per level"};
    static const wchar_t* const techs[] = {L"Minerals", L"Gas", L"Time", L"Energy"};
    return tab == kClassicUnits ? units[f] : tab == kClassicUpgrades ? upgrades[f] : techs[f];
}
uint32_t classicFieldMax(int tab, int f) {
    if (tab == kClassicUnits) return f == 0 ? 0xFFFFFF : f == 2 ? 255 : 0xFFFF;
    return 0xFFFF;
}
uint32_t classicFieldValue(int tab, int e, int f, bool game) {
    const ClassicDialog& d = *gClassic;
    const size_t i = size_t(e);
    if (tab == kClassicUnits) {
        const UnitSettings::Unit& u = (game ? d.def->units : d.units).units[i];
        const uint32_t v[] = {u.hitPoints >> 8, u.shields, u.armor, u.buildTime, u.minerals, u.gas};
        return v[f];
    }
    if (tab == kClassicUpgrades) {
        const UpgradeSettings::Upgrade& g = (game ? d.def->upgrades : d.upgrades).upgrades[i];
        const uint32_t v[] = {g.minerals, g.mineralsPerLevel, g.gas, g.gasPerLevel, g.time, g.timePerLevel};
        return v[f];
    }
    const TechSettings::Tech& t = (game ? d.def->techs : d.techs).techs[i];
    const uint32_t v[] = {t.minerals, t.gas, t.time, t.energy};
    return v[f];
}
void classicSetField(int tab, int e, int f, uint32_t v) {
    ClassicDialog& d = *gClassic;
    const size_t i = size_t(e);
    v = std::min(v, classicFieldMax(tab, f));
    if (tab == kClassicUnits) {
        UnitSettings::Unit& u = d.units.units[i];
        switch (f) {
        case 0: u.hitPoints = v << 8 | (u.hitPoints & 0xFF); break;  // whole hit points; the fraction stays
        case 1: u.shields = uint16_t(v); break;
        case 2: u.armor = uint8_t(v); break;
        case 3: u.buildTime = uint16_t(v); break;
        case 4: u.minerals = uint16_t(v); break;
        case 5: u.gas = uint16_t(v); break;
        }
    } else if (tab == kClassicUpgrades) {
        UpgradeSettings::Upgrade& g = d.upgrades.upgrades[i];
        uint16_t* p[] = {&g.minerals, &g.mineralsPerLevel, &g.gas, &g.gasPerLevel, &g.time, &g.timePerLevel};
        *p[f] = uint16_t(v);
    } else {
        TechSettings::Tech& t = d.techs.techs[i];
        uint16_t* p[] = {&t.minerals, &t.gas, &t.time, &t.energy};
        *p[f] = uint16_t(v);
    }
}

// The weapons a unit shows (its own, then its turret's), each once.
std::vector<int> classicUnitWeapons(int u) {
    std::vector<int> out;
    const int limit = gClassic->bw ? kSettingWeapons : kOriginalWeapons;
    for (uint8_t w : gClassic->def->unitWeapons[size_t(u)])
        if (w < limit && std::find(out.begin(), out.end(), int(w)) == out.end()) out.push_back(w);
    return out;
}
// What the unit's weapon `w` attacks: its slots (ground, air, and its turret's) tell it (GitHub feedback #92).
const wchar_t* classicWeaponTargets(int u, int w) {
    const auto& slots = gClassic->def->unitWeapons[size_t(u)];
    const bool ground = slots[0] == w || slots[2] == w, air = slots[1] == w || slots[3] == w;
    return ground && air ? L"ground and air" : air ? L"air" : L"ground";
}
// Does a unit other than `u` with its own settings use weapon `w`? (Then the weapon's values stay when `u` changes.)
bool classicWeaponUsedByOther(int w, int u) {
    for (int o = 0; o < kSettingUnits; ++o)
        if (o != u && !gClassic->units.units[size_t(o)].useDefault) {
            const std::vector<int> ws = classicUnitWeapons(o);
            if (std::find(ws.begin(), ws.end(), w) != ws.end()) return true;
        }
    return false;
}

// "Use default" on or off: the entry takes the game's values (the ones the fields showed), so the map's old values
// (often from an older patch) do not come back.
void classicSetUseDefault(int tab, int e, bool on) {
    ClassicDialog& d = *gClassic;
    const size_t i = size_t(e);
    if (tab == kClassicUnits) {
        UnitSettings::Unit& u = d.units.units[i];
        const UnitSettings::Unit& g = d.def->units.units[i];
        const uint16_t name = u.nameString;
        u = g, u.useDefault = on, u.nameString = name;
        if (on) d.names[i].clear();
        for (int w : classicUnitWeapons(e))
            if (!classicWeaponUsedByOther(w, e)) d.units.weapons[size_t(w)] = d.def->units.weapons[size_t(w)];
    } else if (tab == kClassicUpgrades) {
        d.upgrades.upgrades[i] = d.def->upgrades.upgrades[i], d.upgrades.upgrades[i].useDefault = on;
    } else {
        d.techs.techs[i] = d.def->techs.techs[i], d.techs.techs[i].useDefault = on;
    }
}

bool classicUseDefault(int tab, int e);  // (below)
// Does the entry differ from the game's values (a field, its weapons, a unit's name)? (Shown in the changed color.)
bool classicFieldChanged(int tab, int e, int f) { return !classicUseDefault(tab, e) && classicFieldValue(tab, e, f, false) != classicFieldValue(tab, e, f, true); }
bool classicDiffers(int tab, int e) {
    const ClassicDialog& d = *gClassic;
    const int n = tab == kClassicUnits ? 6 : tab == kClassicUpgrades ? 6 : 4;
    for (int f = 0; f < n; ++f)
        if (classicFieldValue(tab, e, f, false) != classicFieldValue(tab, e, f, true)) return true;
    if (tab == kClassicUnits) {
        if (!d.names[size_t(e)].empty()) return true;
        for (int w : classicUnitWeapons(e)) {
            const auto& a = d.units.weapons[size_t(w)];
            const auto& b = d.def->units.weapons[size_t(w)];
            if (a.damage != b.damage || a.bonus != b.bonus) return true;
        }
    }
    return false;
}
// After an edit: the entry uses the game's values exactly when nothing differs (Use default follows the fields).
void classicFollowDefault(int tab, int e) {
    if (!classicUseDefault(tab, e) && !classicDiffers(tab, e)) classicSetUseDefault(tab, e, true);
}

bool classicUseDefault(int tab, int e) {
    const ClassicDialog& d = *gClassic;
    return tab == kClassicUnits ? d.units.units[size_t(e)].useDefault : tab == kClassicUpgrades ? d.upgrades.upgrades[size_t(e)].useDefault
                                                                                              : d.techs.techs[size_t(e)].useDefault;
}

void classicSetReadOnly(HWND edit, bool ro) {
    SendMessageW(edit, EM_SETREADONLY, ro, 0);
    InvalidateRect(edit, nullptr, TRUE);
}
void classicCheck(HWND b, bool on, bool enabled = true) {
    SendMessageW(b, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(b, enabled);
}
bool classicChecked(HWND b) { return SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void classicSetNumber(HWND edit, uint32_t v) { SetWindowTextW(edit, std::to_wstring(v).c_str()); }
uint32_t classicNumber(HWND edit) {
    const std::wstring t = windowText(edit);
    return t.empty() ? 0 : uint32_t(std::min<unsigned long long>(wcstoull(t.c_str(), nullptr, 10), 0xFFFFFFFFull));
}

// Shows the selected entry's values in the page's fields.
void classicFill(int tab) {
    ClassicDialog& d = *gClassic;
    ClassicPage& pg = d.pages[tab];
    const int e = pg.entry;
    const size_t i = size_t(e);
    d.filling = true;
    SetWindowTextW(pg.title, classicEntryName(tab, e).c_str());
    if (pg.rename && GetFocus() != pg.name) {  // the title with the rename button after it (GitHub feedback #91)
        RECT tr;
        GetWindowRect(pg.title, &tr);
        MapWindowPoints(nullptr, pg.wnd, reinterpret_cast<POINT*>(&tr), 2);
        HDC dc = GetDC(pg.title);
        HGDIOBJ of = SelectObject(dc, ui::titleFont());
        SIZE ts{};
        const std::wstring text = windowText(pg.title);
        GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &ts);
        SelectObject(dc, of), ReleaseDC(pg.title, dc);
        const int bs = scale(26);
        SetWindowPos(pg.rename, nullptr, std::min<int>(tr.left + ts.cx + scale(10), tr.right - bs), tr.top + (tr.bottom - tr.top - bs) / 2, bs, bs,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(pg.title, SW_SHOW), ShowWindow(pg.name, SW_HIDE);
    }
    const bool def = classicUseDefault(tab, e);
    classicCheck(pg.useDefault, def);
    for (int f = 0; f < int(pg.fields.size()); ++f) {
        classicSetNumber(pg.fields[size_t(f)], classicFieldValue(tab, e, f, def));
        classicSetReadOnly(pg.fields[size_t(f)], false);
    }
    if (tab == kClassicUnits) {
        EnableWindow(pg.fields[1], app.ed->unitTypes()[uint16_t(e)].hasShields);  // shields count only for units that have them
        const std::wstring game = widen(app.ed->unitTypes()[uint16_t(e)].name);
        SetWindowTextW(pg.name, def ? L"" : widen(d.names[i]).c_str());  // empty: the game's name (its cue)
        SendMessageW(pg.name, EM_SETCUEBANNER, TRUE, LPARAM(game.c_str()));
        classicSetReadOnly(pg.name, false);
        const std::vector<int> ws = classicUnitWeapons(e);
        for (int k = 0; k < kCsWeaponRows; ++k) {
            const bool has = k < int(ws.size());
            pg.weapons[k] = has ? ws[size_t(k)] : -1;
            for (HWND h : {pg.weaponName[k], pg.weaponDamage[k], pg.weaponBonus[k]}) ShowWindow(h, has ? SW_SHOW : SW_HIDE);
            if (!has) continue;
            const size_t w = size_t(ws[size_t(k)]);
            const UnitSettings::Weapon& v = def ? d.def->units.weapons[w] : d.units.weapons[w];
            SetWindowTextW(pg.weaponName[k], (entryName("weapons", int(w)) + L": " + tr(classicWeaponTargets(e, int(w)))).c_str());
            classicSetNumber(pg.weaponDamage[k], v.damage), classicSetNumber(pg.weaponBonus[k], v.bonus);
            classicSetReadOnly(pg.weaponDamage[k], false), classicSetReadOnly(pg.weaponBonus[k], false);
        }
        const UnitSettings::Availability& a = d.units.availability[i];
        classicCheck(pg.globalA, a.global);
        for (int p = 0; p < kClassicPlayers; ++p) {
            const bool uses = a.usesDefault[size_t(p)];
            classicCheck(pg.playerDefault[p], uses);
            classicCheck(pg.playerA[p], uses ? a.global : a.player[size_t(p)], !uses);
        }
    } else if (tab == kClassicUpgrades) {
        const UpgradeSettings::Levels& l = d.upgrades.levels[i];
        classicSetNumber(pg.globalA, l.defaultMax), classicSetNumber(pg.globalB, l.defaultStart);
        for (int p = 0; p < kClassicPlayers; ++p) {
            const bool uses = l.usesDefault[size_t(p)];
            classicCheck(pg.playerDefault[p], uses);
            classicSetNumber(pg.playerA[p], uses ? l.defaultMax : l.max[size_t(p)]);
            classicSetNumber(pg.playerB[p], uses ? l.defaultStart : l.start[size_t(p)]);
            classicSetReadOnly(pg.playerA[p], uses), classicSetReadOnly(pg.playerB[p], uses);
        }
    } else {
        const TechSettings::Players& t = d.techs.players[i];
        classicCheck(pg.globalA, t.defaultAvailable), classicCheck(pg.globalB, t.defaultResearched);
        for (int p = 0; p < kClassicPlayers; ++p) {
            const bool uses = t.usesDefault[size_t(p)];
            classicCheck(pg.playerDefault[p], uses);
            classicCheck(pg.playerA[p], uses ? t.defaultAvailable : t.available[size_t(p)], !uses);
            classicCheck(pg.playerB[p], uses ? t.defaultResearched : t.researched[size_t(p)], !uses);
        }
    }
    d.filling = false;
    InvalidateRect(pg.list, nullptr, FALSE);
}

// The list: the entries whose name has the search text.
void classicFillList(int tab) {
    ClassicDialog& d = *gClassic;
    ClassicPage& pg = d.pages[tab];
    std::wstring q = windowText(pg.search);
    for (wchar_t& c : q) c = wchar_t(towlower(c));
    pg.shown.clear();
    for (int e = 0; e < classicEntryCount(tab); ++e) {
        std::wstring n = classicEntryName(tab, e);
        for (wchar_t& c : n) c = wchar_t(towlower(c));
        if (q.empty() || n.find(q) != std::wstring::npos) pg.shown.push_back(e);
    }
    SendMessageW(pg.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(pg.list, LB_RESETCONTENT, 0, 0);
    for (int e : pg.shown) SendMessageW(pg.list, LB_ADDSTRING, 0, LPARAM(e));
    const auto at = std::find(pg.shown.begin(), pg.shown.end(), pg.entry);
    SendMessageW(pg.list, LB_SETCURSEL, at == pg.shown.end() ? WPARAM(-1) : WPARAM(at - pg.shown.begin()), 0);
    SendMessageW(pg.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(pg.list, nullptr, TRUE);
}

// Selects an entry (tests, and the list).
void classicSelect(int tab, int e) {
    ClassicPage& pg = gClassic->pages[tab];
    pg.entry = std::clamp(e, 0, classicEntryCount(tab) - 1);
    const auto at = std::find(pg.shown.begin(), pg.shown.end(), pg.entry);
    if (at != pg.shown.end()) SendMessageW(pg.list, LB_SETCURSEL, WPARAM(at - pg.shown.begin()), 0);
    classicFill(tab);
}

// The name box: Enter keeps the name, Esc gives the name before back; both leave the box (the title shows again).
LRESULT CALLBACK classicNameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETDLGCODE && (wp == VK_RETURN || wp == VK_ESCAPE)) return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        ClassicPage& pg = gClassic->pages[kClassicUnits];
        if (wp == VK_ESCAPE) SetWindowTextW(h, widen(pg.nameBefore).c_str());
        SetFocus(pg.list);
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;  // no beep
    return DefSubclassProc(h, msg, wp, lp);
}

// A field changed: into the settings of the selected entry.
void classicCommand(int tab, int id, int code) {
    ClassicDialog& d = *gClassic;
    ClassicPage& pg = d.pages[tab];
    const int e = pg.entry;
    const size_t i = size_t(e);
    if (id == kCsSearch && code == EN_CHANGE) return classicFillList(tab);
    if (id == kCsList && code == LBN_SELCHANGE) {
        const LRESULT k = SendMessageW(pg.list, LB_GETCURSEL, 0, 0);
        if (k >= 0 && k < LRESULT(pg.shown.size())) pg.entry = pg.shown[size_t(k)], classicFill(tab);
        return;
    }
    if (id == kCsRename && code == BN_CLICKED) {  // the name box in place of the title
        RECT tr;
        GetWindowRect(pg.title, &tr);
        MapWindowPoints(nullptr, pg.wnd, reinterpret_cast<POINT*>(&tr), 2);
        pg.nameBefore = classicUseDefault(tab, e) ? "" : d.names[i];
        RECT fr{};  // as wide as the fields under it
        GetWindowRect(pg.fields[0], &fr);
        MapWindowPoints(nullptr, pg.wnd, reinterpret_cast<POINT*>(&fr), 2);
        SetWindowPos(pg.name, nullptr, tr.left, tr.top + (tr.bottom - tr.top - scale(ui::kFieldH)) / 2, fr.right - tr.left, scale(ui::kFieldH),
                     SWP_NOZORDER | SWP_SHOWWINDOW);
        ShowWindow(pg.title, SW_HIDE), ShowWindow(pg.rename, SW_HIDE);
        SetFocus(pg.name);
        SendMessageW(pg.name, EM_SETSEL, 0, -1);
        return;
    }
    if (d.filling) return;
    if (code == EN_KILLFOCUS) return classicFill(tab);  // the value as kept (a number over its limit: the limit)
    const bool entryField = id == kCsName || (id >= kCsField && id < kCsField + int(pg.fields.size())) ||
                            (id >= kCsWeaponDamage && id < kCsWeaponDamage + kCsWeaponRows) || (id >= kCsWeaponBonus && id < kCsWeaponBonus + kCsWeaponRows);
    if (code == EN_CHANGE && entryField && classicUseDefault(tab, e)) classicSetUseDefault(tab, e, false);  // an edit: the entry's own values
    if (code == EN_CHANGE) {
        if (id >= kCsField && id < kCsField + int(pg.fields.size())) {
            classicSetField(tab, e, id - kCsField, classicNumber(pg.fields[size_t(id - kCsField)]));
        } else if (id == kCsName) {
            d.names[i] = narrow(windowText(pg.name));
        } else if (id >= kCsWeaponDamage && id < kCsWeaponDamage + kCsWeaponRows && pg.weapons[id - kCsWeaponDamage] >= 0) {
            d.units.weapons[size_t(pg.weapons[id - kCsWeaponDamage])].damage = uint16_t(std::min<uint32_t>(classicNumber(GetDlgItem(pg.wnd, id)), 0xFFFF));
        } else if (id >= kCsWeaponBonus && id < kCsWeaponBonus + kCsWeaponRows && pg.weapons[id - kCsWeaponBonus] >= 0) {
            d.units.weapons[size_t(pg.weapons[id - kCsWeaponBonus])].bonus = uint16_t(std::min<uint32_t>(classicNumber(GetDlgItem(pg.wnd, id)), 0xFFFF));
        } else if (tab == kClassicUpgrades) {
            UpgradeSettings::Levels& l = d.upgrades.levels[i];
            const uint8_t v = uint8_t(std::min<uint32_t>(classicNumber(GetDlgItem(pg.wnd, id)), 255));
            if (id == kCsGlobalA) l.defaultMax = v;
            else if (id == kCsGlobalB) l.defaultStart = v;
            else if (id >= kCsPlayerA && id < kCsPlayerA + kClassicPlayers && !l.usesDefault[size_t(id - kCsPlayerA)]) l.max[size_t(id - kCsPlayerA)] = v;
            else if (id >= kCsPlayerB && id < kCsPlayerB + kClassicPlayers && !l.usesDefault[size_t(id - kCsPlayerB)]) l.start[size_t(id - kCsPlayerB)] = v;
            if (id == kCsGlobalA || id == kCsGlobalB) {  // the players that use the defaults show them
                d.filling = true;
                for (int p = 0; p < kClassicPlayers; ++p)
                    if (l.usesDefault[size_t(p)]) classicSetNumber(id == kCsGlobalA ? pg.playerA[p] : pg.playerB[p], v);
                d.filling = false;
            }
        }
        if (entryField) {
            classicFollowDefault(tab, e);
            InvalidateRect(GetDlgItem(pg.wnd, id), nullptr, TRUE);  // its color (changed or not)
        }
        InvalidateRect(pg.list, nullptr, FALSE);
        return;
    }
    if (code != BN_CLICKED) return;
    const bool on = classicChecked(GetDlgItem(pg.wnd, id));
    const int p = id >= kCsPlayerDefault && id < kCsPlayerDefault + kClassicPlayers ? id - kCsPlayerDefault
                  : id >= kCsPlayerA && id < kCsPlayerA + kClassicPlayers   ? id - kCsPlayerA
                  : id >= kCsPlayerB && id < kCsPlayerB + kClassicPlayers   ? id - kCsPlayerB
                                                                              : -1;
    const size_t pp = size_t(std::max(p, 0));
    const bool playerDefault = id >= kCsPlayerDefault && id < kCsPlayerDefault + kClassicPlayers;
    if (id == kCsUseDefault) classicSetUseDefault(tab, e, on);
    else if (tab == kClassicUnits) {
        UnitSettings::Availability& a = d.units.availability[i];
        if (id == kCsGlobalA) a.global = on;
        else if (playerDefault) a.usesDefault[pp] = on, a.player[pp] = a.global;  // the player takes the default's value
        else if (p >= 0) a.player[pp] = on;
    } else if (tab == kClassicUpgrades) {
        UpgradeSettings::Levels& l = d.upgrades.levels[i];
        if (playerDefault) l.usesDefault[pp] = on, l.max[pp] = l.defaultMax, l.start[pp] = l.defaultStart;
    } else {
        TechSettings::Players& t = d.techs.players[i];
        if (id == kCsGlobalA) t.defaultAvailable = on;
        else if (id == kCsGlobalB) t.defaultResearched = on;
        else if (playerDefault) t.usesDefault[pp] = on, t.available[pp] = t.defaultAvailable, t.researched[pp] = t.defaultResearched;
        else if (id >= kCsPlayerA && id < kCsPlayerA + kClassicPlayers) t.available[pp] = on;
        else if (p >= 0) t.researched[pp] = on;
    }
    classicFill(tab);
}

LRESULT CALLBACK classicPageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicDialog* d = gClassic;
    const int tab = int(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_FEEDBACK_NAME: {  // the entry, and the field under the mouse: the label on its row (and its column's head)
        auto& q = *reinterpret_cast<FeedbackQuery*>(lp);
        HWND c = ChildWindowFromPointEx(hwnd, q.pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!c || c == hwnd) return 0;
        RECT cr;
        GetWindowRect(c, &cr);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&cr), 2);
        std::wstring row, col;
        int rowGap = INT_MAX, colGap = INT_MAX;
        for (HWND k = GetWindow(hwnd, GW_CHILD); k; k = GetWindow(k, GW_HWNDNEXT)) {
            wchar_t cls[16] = L"";
            GetClassNameW(k, cls, 16);
            if (_wcsicmp(cls, WC_STATICW) || !IsWindowVisible(k)) continue;
            RECT r;
            GetWindowRect(k, &r);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
            const std::wstring text = windowText(k);
            if (text.empty()) continue;
            if (r.bottom > cr.top && r.top < cr.bottom && r.right <= cr.left + 2 && cr.left - r.right < rowGap) rowGap = cr.left - r.right, row = text;
            if (r.left < cr.right && r.right > cr.left && r.bottom <= cr.top + 2 && cr.top - r.bottom < colGap) colGap = cr.top - r.bottom, col = text;
        }
        const int id = GetDlgCtrlID(c);
        const bool inColumn = (id >= kCsPlayerA && id < kCsPlayerB + kClassicPlayers) || id == kCsGlobalA || id == kCsGlobalB;
        std::wstring n = d && d->pages[size_t(tab)].title ? windowText(d->pages[size_t(tab)].title) : L"";
        std::wstring field = row.empty() ? windowText(c) : row;
        if (inColumn && !col.empty()) field += L", " + col;
        q.name = field.empty() ? n : n.empty() ? field : n + L": " + field;
        return 0;
    }
    case WM_CREATE: {
        const int t = int(INT_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, t);
        ClassicPage& pg = d->pages[t];
        const int keep = pg.entry;  // made again (the page showed before): the same entry
        pg = ClassicPage{};
        pg.entry = keep;
        pg.wnd = hwnd;
        const int pad = scale(ui::kDialogPad), rowH = scale(ui::kFieldH), gap = scale(8), step = rowH + gap;  // fields 24, rows 32 apart
        const int listW = scale(250), x0 = pad + listW + pad * 2, labelW = scale(130), numW = scale(90);
        const int x1 = x0 + labelW + numW + pad * 3;  // the players column
        const int nameColW = scale(80), defColW = scale(90), aColW = t == kClassicUpgrades ? scale(100) : scale(110);
        const int right = x1 + nameColW + defColW + aColW * 2;
        auto font = [](HWND h, HFONT f = nullptr) { SendMessageW(h, WM_SETFONT, WPARAM(f ? f : app.font), TRUE); };
        auto label = [&](const std::wstring& s, int x, int y, int w, bool dim = false) {
            HWND h = CreateWindowExW(0, WC_STATICW, s.c_str(), WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS, x, y + scale(4), w, rowH, hwnd,
                                     HMENU(INT_PTR(dim ? kCsDim : 0)), app.inst, nullptr);
            font(h);
            return h;
        };
        auto edit = [&](int id, int x, int y, int w, bool number = true) {
            // Numbers right-aligned in the monospace font, as the Data form has them.
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (number ? ES_NUMBER | ES_RIGHT : 0), x, y,
                                     w, rowH, hwnd, HMENU(INT_PTR(id)), app.inst, nullptr);
            font(h, number ? monoFont() : nullptr);
            darkControl(h, L"DarkMode_CFD");
            return h;
        };
        auto check = [&](int id, const wchar_t* s, int x, int y, int w) {
            HWND h = CreateWindowExW(0, WC_BUTTONW, s, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, w, rowH, hwnd, HMENU(INT_PTR(id)),
                                     app.inst, nullptr);
            font(h);
            darkControl(h, L"DarkMode_Explorer");  // painted by the kit
            return h;
        };
        // The list, with a search field over it.
        pg.search = edit(kCsSearch, pad, pad, listW, false);
        SendMessageW(pg.search, EM_SETCUEBANNER, TRUE, LPARAM(L"Search")); addSearchClear(pg.search);  // an x clears it
        const int listTop = pad + rowH + gap, listH = scale(470);
        pg.list = CreateWindowExW(0, WC_LISTBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                                  pad, listTop, listW, listH, hwnd, HMENU(INT_PTR(kCsList)), app.inst, nullptr);
        font(pg.list);
        darkControl(pg.list, L"DarkMode_Explorer");
        // The entry: its name, Use default, the value fields.
        int y = pad;
        pg.title = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS, x0, y, right - x0, scale(30), hwnd, nullptr,
                                   app.inst, nullptr);
        font(pg.title, ui::titleFont());
        y += scale(36);
        // Use default: not shown; it follows the fields (the game's values when nothing differs from them).
        pg.useDefault = check(kCsUseDefault, L"Use default", x0, y, scale(200));
        ShowWindow(pg.useDefault, SW_HIDE);
        y += gap;
        const int formTop = y;
        for (int f = 0; f < classicFieldCount(t); ++f) {
            label(classicFieldName(t, f), x0, y, labelW);
            pg.fields.push_back(edit(kCsField + f, x0 + labelW, y, numW));
            y += step;
        }
        if (t == kClassicUnits) {  // the name: renamed in place of the title (its button)
            pg.name = edit(kCsName, x0, pad, scale(320), false);
            ShowWindow(pg.name, SW_HIDE);
            SetWindowSubclass(pg.name, classicNameProc, 0, 0);
            pg.rename = CreateWindowExW(0, WC_BUTTONW, L"\x270E", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, x0, pad, scale(26), scale(26), hwnd,
                                        HMENU(INT_PTR(kCsRename)), app.inst, nullptr);
            font(pg.rename);
            darkControl(pg.rename, L"DarkMode_Explorer");
            HWND tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr,
                                        app.inst, nullptr);
            styleTips(tips);
            TOOLINFOW ti{sizeof ti};
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwnd, ti.uId = UINT_PTR(pg.rename);
            wchar_t tipText[] = L"Rename the unit (empty: the game's name)";
            ti.lpszText = tipText;
            SendMessageW(tips, TTM_ADDTOOLW, 0, LPARAM(&ti));
        }
        // Players: the default for all, and players 1-8 (each uses the default, or its own values).
        int py = formTop - step;
        const wchar_t* const heads[kClassicTabCount][2] = {{L"Can build", L""}, {L"Max level", L"Start level"}, {L"Available", L"Researched"}};
        // Upgrades: Start level, then Max level (from the lowest level to the highest; the values stay A: max, B: start).
        const bool swapAB = t == kClassicUpgrades;
        label(L"Players", x1, py, nameColW, true);
        if (t == kClassicUpgrades) {
            label(heads[t][0], x1 + nameColW + defColW + (swapAB ? aColW : 0), py, aColW, true),
                label(heads[t][1], x1 + nameColW + defColW + (swapAB ? 0 : aColW), py, aColW, true);
        }
        py += step;
        label(L"Default", x1, py, nameColW);
        const int ax = x1 + nameColW + defColW + (swapAB ? aColW : 0), bx = x1 + nameColW + defColW + (swapAB ? 0 : aColW);
        if (t == kClassicUpgrades) {
            if (swapAB) pg.globalB = edit(kCsGlobalB, bx, py, scale(60)), pg.globalA = edit(kCsGlobalA, ax, py, scale(60));  // Tab: left to right
            else pg.globalA = edit(kCsGlobalA, ax, py, scale(60)), pg.globalB = edit(kCsGlobalB, bx, py, scale(60));
        } else {
            pg.globalA = check(kCsGlobalA, heads[t][0], ax, py, aColW);
            if (t == kClassicTechs) pg.globalB = check(kCsGlobalB, heads[t][1], bx, py, aColW);
        }
        py += step;
        for (int p = 0; p < kClassicPlayers; ++p) {
            label(L"Player " + std::to_wstring(p + 1), x1, py, nameColW);
            pg.playerDefault[p] = check(kCsPlayerDefault + p, L"Default", x1 + nameColW, py, defColW);
            if (t == kClassicUpgrades) {
                if (swapAB) pg.playerB[p] = edit(kCsPlayerB + p, bx, py, scale(60)), pg.playerA[p] = edit(kCsPlayerA + p, ax, py, scale(60));
                else pg.playerA[p] = edit(kCsPlayerA + p, ax, py, scale(60)), pg.playerB[p] = edit(kCsPlayerB + p, bx, py, scale(60));
            } else {
                pg.playerA[p] = check(kCsPlayerA + p, heads[t][0], ax, py, aColW);
                if (t == kClassicTechs) pg.playerB[p] = check(kCsPlayerB + p, heads[t][1], bx, py, aColW);
            }
            py += step;
        }
        y = std::max(y, py);
        if (t == kClassicUnits) {  // the unit's weapons (base damage, and the damage each upgrade level adds)
            y += gap;
            label(L"Weapon", x0, y, labelW, true), label(L"Damage", x0 + labelW + scale(140), y, numW, true);
            label(L"Bonus", x0 + labelW + scale(140) + numW + pad, y, numW, true);
            y += step;
            for (int k = 0; k < kCsWeaponRows; ++k) {
                pg.weaponName[k] = label(L"", x0, y, labelW + scale(130));
                pg.weaponDamage[k] = edit(kCsWeaponDamage + k, x0 + labelW + scale(140), y, numW);
                pg.weaponBonus[k] = edit(kCsWeaponBonus + k, x0 + labelW + scale(140) + numW + pad, y, numW);
                y += step;
            }
        }
        const int h = std::max(y, listTop + listH) + pad;
        SetWindowPos(hwnd, nullptr, 0, 0, right + pad, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        classicFillList(t);
        classicSelect(t, keep);
        return 0;
    }
    case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight = UINT(scale(28));
        return TRUE;
    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (!d || di->itemID == UINT(-1)) return TRUE;
        const int e = int(di->itemData);
        const bool sel = (di->itemState & ODS_SELECTED) != 0;
        ui::ownerRow(di->hDC, di->rcItem, sel);
        SetBkMode(di->hDC, TRANSPARENT);
        HGDIOBJ of = SelectObject(di->hDC, app.font);
        RECT r = di->rcItem;
        const RECT box{r.left + scale(6), r.top + scale(2), r.left + scale(30), r.bottom - scale(2)};
        if (const UnitPalette::Bitmap* pic = classicEntryIcon(tab, e); pic && pic->bmp) drawFitted(di->hDC, *pic, box);
        r.left = box.right + scale(8), r.right -= scale(16);
        SetTextColor(di->hDC, ui::rowText(sel));
        DrawTextW(di->hDC, classicEntryName(tab, e).c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (classicEntryChanged(tab, e)) ui::changedDot(di->hDC, di->rcItem.right - scale(9), (di->rcItem.top + di->rcItem.bottom) / 2);
        SelectObject(di->hDC, of);
        return TRUE;
    }
    case WM_COMMAND:
        if (d) classicCommand(tab, LOWORD(wp), HIWORD(wp)), mapPageChanged();
        return 0;
    case WM_PAINT: {
        // The column headings (kCsDim) as section headings (ui::sectionHeader): one line under each row of them.
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        std::map<int, std::pair<int, int>> rows;  // top -> left, right
        for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            if (GetDlgCtrlID(c) == kCsDim && IsWindowVisible(c)) {
                RECT r;
                GetWindowRect(c, &r);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
                auto it = rows.find(r.bottom);
                if (it == rows.end()) rows[r.bottom] = {r.left, r.right};
                else it->second = {std::min<int>(it->second.first, r.left), std::max<int>(it->second.second, r.right)};
            }
        RECT rc;
        GetClientRect(hwnd, &rc);
        for (const auto& [bottom, lr] : rows)  // just under the headings (they paint over their own rectangles)
            fillAlpha(dc, {lr.first, bottom, std::max<int>(lr.second, int(rc.right) - scale(ui::kDialogPad)), bottom + 1}, kBorder, 255);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc = HDC(wp);
        const HWND c = HWND(lp);
        wchar_t cls[16] = L"";
        GetClassNameW(c, cls, 16);
        const bool isEdit = !_wcsicmp(cls, WC_EDITW);
        if (isEdit && msg == WM_CTLCOLORSTATIC) {  // a field that shows the game's value
            SetTextColor(dc, IsWindowEnabled(c) ? kDim : kFaint);
            SetBkColor(dc, kReadOnly);
            return LRESULT(d && d->readOnlyBrush ? d->readOnlyBrush : app.ctlBrush);
        }
        // Labels dim (ui::fieldLabel); the headings, the title, the weapons' names and the fields in the text color.
        const ClassicPage* pgc = d ? &d->pages[tab] : nullptr;
        const bool value = pgc && (c == pgc->title || std::find(std::begin(pgc->weaponName), std::end(pgc->weaponName), c) != std::end(pgc->weaponName));
        SetTextColor(dc, !IsWindowEnabled(c) ? kFaint : msg == WM_CTLCOLORSTATIC && !value && GetDlgCtrlID(c) != kCsDim ? kDim : kText);
        if (pgc && msg == WM_CTLCOLOREDIT && IsWindowEnabled(c)) {  // a value that differs from the game's: the changed color
            const int cid = GetDlgCtrlID(c), e = pgc->entry;
            bool changed = false;
            if (cid >= kCsField && cid < kCsField + int(pgc->fields.size())) changed = classicFieldChanged(tab, e, cid - kCsField);
            else if (cid == kCsName) changed = !d->names[size_t(e)].empty() && !classicUseDefault(tab, e);
            else if (cid >= kCsWeaponDamage && cid < kCsWeaponDamage + kCsWeaponRows && pgc->weapons[cid - kCsWeaponDamage] >= 0) {
                const size_t w = size_t(pgc->weapons[cid - kCsWeaponDamage]);
                changed = !classicUseDefault(tab, e) && d->units.weapons[w].damage != d->def->units.weapons[w].damage;
            } else if (cid >= kCsWeaponBonus && cid < kCsWeaponBonus + kCsWeaponRows && pgc->weapons[cid - kCsWeaponBonus] >= 0) {
                const size_t w = size_t(pgc->weapons[cid - kCsWeaponBonus]);
                changed = !classicUseDefault(tab, e) && d->units.weapons[w].bonus != d->def->units.weapons[w].bonus;
            }
            if (changed) SetTextColor(dc, kGood);
        }
        const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        SetBkColor(dc, field ? kCtl : kBg);
        return LRESULT(field ? app.ctlBrush : app.bgBrush);
    }
    case WM_ERASEBKGND: {  // the theme's ground (the class brush is the one of the theme at the first open)
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(HDC(wp), &rc, app.bgBrush);
        return 1;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

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

