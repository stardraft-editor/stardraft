// The Map data page's views (portable, core.inc; classic maps only): the map's own unit, upgrade and tech settings, as
// StarEdit's Unit, Upgrade and Tech settings edit them. Three tabs; each tab is a view with a list (the entries with their
// icons, a search field over it) and the fields of the selected entry. "Use default" grays nothing: an entry takes the
// game's values when nothing differs from them. The values are the page's working copy (ClassicDialog); the program puts
// a change into the map (ClassicHost, map_pages.inc) and loads and applies the copy (classic_settings.inc).

enum ClassicTab { kClassicUnits, kClassicUpgrades, kClassicTechs, kClassicTabCount };
const wchar_t* const kClassicTabNames[kClassicTabCount] = {L"Units", L"Upgrades", L"Technologies"};
const UiIcon kClassicTabIcons[kClassicTabCount] = {UiIcon::DatUnits, UiIcon::DatUpgrades, UiIcon::DatTech};
constexpr int kClassicPlayers = 8;  // players 1-8 show; 9-12 stay as they are
constexpr int kCsWeaponRows = 3;

// The page's values: the working copy of the map's unit, upgrade and tech settings, and the selected entry of each tab.
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
    bool bw = true;
    int tab = 0;                       // the tab that shows: units, upgrades or tech
    int entry[kClassicTabCount] = {};  // the selected entry of each tab
};
ClassicDialog* gClassic = nullptr;  // the working copy of the page that shows (map_pages.inc)

int classicEntryCount(int tab) {
    const bool bw = gClassic->bw;
    return tab == kClassicUnits ? kSettingUnits : tab == kClassicUpgrades ? (bw ? kSettingUpgrades : kOriginalUpgrades) : (bw ? kSettingTechs : kOriginalTechs);
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

// The number in a field's text (empty: 0; over 32 bits: the largest).
uint32_t classicNumberOf(const std::wstring& t) {
    return t.empty() ? 0 : uint32_t(std::min<unsigned long long>(wcstoull(t.c_str(), nullptr, 10), 0xFFFFFFFFull));
}

// The program's part of the page (map_pages.inc: ClassicProgram): the map's changes, the names and the game text pop-out.
class ClassicHost {
public:
    virtual ~ClassicHost() = default;
    // A value changed by a check box, a list choice or a rename: the program puts it into the map (one undo step).
    virtual void changed() = 0;
    // A text typed in a field: it goes into the map when the typing stops.
    virtual void typed() = 0;
    // The name of an entry, as the list and the title show it (a unit: its game name and its custom name).
    virtual std::wstring entryName(int tab, int e) = 0;
    // A unit's game name (the rename's hint) and whether it has shields.
    virtual std::wstring unitGameName(int u) = 0;
    virtual bool hasShields(int u) = 0;
    // The entry's icon in `box` (its picture, when it has one).
    virtual void drawIcon(draw::Canvas& c, int tab, int e, draw::Rect box) = 0;
    // The game text pop-out for a unit's name, under `at` (in the page's coordinates); false when it is cancelled.
    virtual bool editUnitName(int u, std::string& text, draw::Point at) = 0;
    // The width of t in the page's font (the columns are measured with it).
    virtual int textWidth(const std::wstring& t) = 0;
    // The page's content is w x h (the program sizes the window it is in).
    virtual void fitSize(int tab, int w, int h) = 0;
};

// One value field: a kit text field. Its text's color is the page's: the changed color, the disabled and the read-only ones
// (the spans of the text; the look is set again only when it differs).
struct ClassicNumber {
    stardraft::kit::TextField field;
    draw::Color look = draw::kNone;  // the color the spans were set to
    bool looked = false;             // the spans were set for the text as it is now
};

// One tab's page: its list of entries (a search field over it), the title (and the rename button), and the fields of the
// selected entry. The page's rows are the Map data page's (the same places as the windows it was).
class ClassicPageView final : public draw::View {
public:
    ClassicPageView(ClassicHost& host, ClassicDialog& d, int tab) : host_(host), d_(d), tab_(tab) {
        search_.setPlaceholder(L"Search");
        search_.onChange = [this] { fillList(); };
        add(search_, {0, 0, 0, 0});
        list_.setRowHeight(28);
        list_.label = [this](int i) { return host_.entryName(tab_, shown_[size_t(i)]); };
        list_.paintRow = [this](draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) { paintRow(c, i, r, s); };
        list_.onSelect = [this] {
            const std::vector<int> sel = list_.selection();
            if (sel.empty() || sel[0] < 0 || sel[0] >= int(shown_.size())) return;
            d_.entry[tab_] = shown_[size_t(sel[0])];
            fill();
        };
        add(list_, {0, 0, 0, 0});
        for (int f = 0; f < classicFieldCount(tab_); ++f) {
            ClassicNumber& n = fields_[size_t(f)];
            setNumberLook(n);
            n.field.onChange = [this, f] { fieldEdited(f); };
            n.field.onFocusLost = [this] { fill(); };  // the value as kept (a number over its limit: the limit)
            add(n.field, {0, 0, 0, 0});
        }
        if (tab_ == kClassicUnits) {  // the name (the rename button) after the fields, as the old Name box
            rename_.setText(L"\x270E"), rename_.setTone(stardraft::kit::Tone::Normal);
            rename_.setTip(L"Rename the unit (empty: the game's name)");
            rename_.onClick = [this] { renameUnit(); };
            add(rename_, {0, 0, 0, 0});
        }
        // The players' defaults, and the players' values (numbers for upgrades, check boxes for units and techs).
        const std::wstring aLabel = tab_ == kClassicUnits ? L"Can build" : tab_ == kClassicUpgrades ? L"Max level" : L"Available";
        const std::wstring bLabel = tab_ == kClassicUpgrades ? L"Start level" : L"Researched";
        // Tab goes in the Win32 page's order: the Default row (left to right in a row), players 1-8, then the weapons.
        if (tab_ == kClassicUpgrades) {
            setNumberLook(globalA_), setNumberLook(globalB_);
            globalA_.field.onChange = [this] { upgradeLevelEdited(true, true, 0); };
            globalB_.field.onChange = [this] { upgradeLevelEdited(true, false, 0); };
            globalA_.field.onFocusLost = globalB_.field.onFocusLost = [this] { fill(); };
            add(globalB_.field, {0, 0, 0, 0}), add(globalA_.field, {0, 0, 0, 0});  // the left column first (Start, then Max)
        } else {
            globalCheckA_.setLabel(aLabel), globalCheckA_.onChange = [this](bool on) { checkChanged(kCheckGlobalA, 0, on); };
            add(globalCheckA_, {0, 0, 0, 0});
            if (tab_ == kClassicTechs) {
                globalCheckB_.setLabel(bLabel), globalCheckB_.onChange = [this](bool on) { checkChanged(kCheckGlobalB, 0, on); };
                add(globalCheckB_, {0, 0, 0, 0});
            }
        }
        for (int p = 0; p < kClassicPlayers; ++p) {
            pDefault_[size_t(p)].setLabel(L"Default");
            pDefault_[size_t(p)].onChange = [this, p](bool on) { checkChanged(kCheckPlayerDefault, p, on); };
            add(pDefault_[size_t(p)], {0, 0, 0, 0});
            if (tab_ == kClassicUpgrades) {
                ClassicNumber& a = pA_[size_t(p)];
                ClassicNumber& b = pB_[size_t(p)];
                setNumberLook(a), setNumberLook(b);
                a.field.onChange = [this, p] { upgradeLevelEdited(false, true, p); };
                b.field.onChange = [this, p] { upgradeLevelEdited(false, false, p); };
                a.field.onFocusLost = b.field.onFocusLost = [this] { fill(); };
                add(b.field, {0, 0, 0, 0}), add(a.field, {0, 0, 0, 0});  // the left column first (Start, then Max)
            } else {
                pCheckA_[size_t(p)].setLabel(aLabel), pCheckA_[size_t(p)].onChange = [this, p](bool on) { checkChanged(kCheckPlayerA, p, on); };
                add(pCheckA_[size_t(p)], {0, 0, 0, 0});
                if (tab_ == kClassicTechs) {
                    pCheckB_[size_t(p)].setLabel(bLabel), pCheckB_[size_t(p)].onChange = [this, p](bool on) { checkChanged(kCheckPlayerB, p, on); };
                    add(pCheckB_[size_t(p)], {0, 0, 0, 0});
                }
            }
        }
        if (tab_ == kClassicUnits) {  // the unit's weapons: the damage, then the bonus, of each weapon
            for (int k = 0; k < kCsWeaponRows; ++k)
                for (int b = 0; b < 2; ++b) {
                    ClassicNumber& n = b ? weaponBonus_[size_t(k)] : weaponDamage_[size_t(k)];
                    setNumberLook(n);
                    n.field.onChange = [this, k, b] { weaponEdited(k, b == 0); };
                    n.field.onFocusLost = [this] { fill(); };
                    add(n.field, {0, 0, 0, 0});
                }
        }
        fillList();
        fill();
    }

    // The page's size (its content: the window is as big as it, made before its first paint).
    int preferredWidth() { return rows().width; }
    int preferredHeight() { return rows().height; }

    // The entries in the list, and the entry shown.
    int shownCount() const { return int(shown_.size()); }
    int entry() const { return d_.entry[tab_]; }
    // Selects an entry (tests, and the list): the list shows it, and its fields.
    void select(int e) {
        d_.entry[tab_] = std::clamp(e, 0, classicEntryCount(tab_) - 1);
        fill();
    }
    // The widgets a test or a change through the page drives: a text's setText and then its onChange (a typed text), a
    // check box's setOn and then its onChange (a click).
    stardraft::kit::TextField& field(int f) { return fields_[size_t(f)].field; }
    stardraft::kit::TextField& weaponDamage(int k) { return weaponDamage_[size_t(k)].field; }
    stardraft::kit::TextField& weaponBonus(int k) { return weaponBonus_[size_t(k)].field; }
    stardraft::kit::CheckBox& playerDefault(int p) { return pDefault_[size_t(p)]; }
    stardraft::kit::CheckBox& playerA(int p) { return pCheckA_[size_t(p)]; }  // units and techs: can build, available
    stardraft::kit::CheckBox& playerB(int p) { return pCheckB_[size_t(p)]; }  // techs: researched
    stardraft::kit::TextField& playerLevelA(int p) { return pA_[size_t(p)].field; }  // upgrades: max level
    stardraft::kit::TextField& playerLevelB(int p) { return pB_[size_t(p)].field; }  // upgrades: start level
    stardraft::kit::CheckBox& globalA() { return globalCheckA_; }
    stardraft::kit::CheckBox& globalB() { return globalCheckB_; }
    stardraft::kit::TextField& globalLevelA() { return globalA_.field; }
    stardraft::kit::TextField& globalLevelB() { return globalB_.field; }
    stardraft::kit::Button& renameButton() { return rename_; }
    stardraft::kit::TextField& search() { return search_.field(); }
    bool renameShown() const { return tab_ == kClassicUnits; }
    // Use default: on takes the game's values, off the entry's own (the page's Use default, as a change does it).
    void setUseDefault(bool on) {
        const int e = d_.entry[tab_];
        if (classicUseDefault(tab_, e) == on) return;
        classicSetUseDefault(tab_, e, on);
        fill();
        host_.changed();
    }
    // The unit's name is `text` (empty: the game's name), as the rename's pop-out sets it: a name of its own makes the
    // entry's own values.
    void setName(const std::string& text) {
        const int e = d_.entry[tab_];
        if (classicUseDefault(tab_, e)) classicSetUseDefault(tab_, e, false);
        d_.names[size_t(e)] = text;
        classicFollowDefault(tab_, e);
        fill();
        list_.invalidate();
        host_.changed();
    }
    // The title as the page shows it (the entry's name).
    std::wstring title() const { return host_.entryName(tab_, d_.entry[tab_]); }
    // A text field of the page has the keyboard (the program's shortcuts wait for it).
    bool typingField() {
        if (search_.field().hasFocus()) return true;
        for (const ClassicNumber& n : fields_)
            if (n.field.hasFocus()) return true;
        for (const ClassicNumber& n : weaponDamage_)
            if (n.field.hasFocus()) return true;
        for (const ClassicNumber& n : weaponBonus_)
            if (n.field.hasFocus()) return true;
        for (const ClassicNumber& n : pA_)
            if (n.field.hasFocus()) return true;
        for (const ClassicNumber& n : pB_)
            if (n.field.hasFocus()) return true;
        return globalA_.field.hasFocus() || globalB_.field.hasFocus();
    }

    // The list's place: all of its frame's area (the rows start under the frame's outer 2 px, as the native list's did).
    static draw::Rect listRect(int pad, int top, int w, int h) { return {pad, top, pad + w, top + h}; }
    // The list's frame, over the list: the rounded 2 px ring (the native list's frame, which covered its outer 2 px).
    void paintOver(draw::Canvas& c) override {
        const Rows l = rows();
        c.roundBox(listRect(l.pad, l.listTop, l.listW, l.listH), draw::kNone, kBorder, float(scale(stardraft::kit::kRadius)), scale(1) + 1);
    }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        const Rows l = rows();
        if (width() != l.width || height() != l.height) host_.fitSize(tab_, l.width, l.height);  // (its own size: a paint again)
        place(search_, {l.pad, l.pad, l.pad + l.listW, l.pad + l.rowH});
        place(list_, listRect(l.pad, l.listTop, l.listW, l.listH));
        if (scrollToRow_ >= 0 && list_.height() > 0) {  // the selected row: in view, once the list has its size
            list_.ensureVisible(scrollToRow_);
            scrollToRow_ = -1;
        }
        // The title (the page's title font), and the rename button after its text.
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
        c.text({l.x0, l.pad, l.right, l.pad + l.titleH}, title(), kText, draw::kEllipsis);
        if (renameShown()) {
            const int bs = scale(26), tw = c.measure(title());
            const int left = std::min(l.x0 + tw + scale(10), l.right - bs), top = l.pad + (l.titleH - bs) / 2;
            place(rename_, {left, top, left + bs, top + bs});
        }
        c.setFont(of);
        // The dim headings (kText) and the labels (kDim), each heading with a line under it.
        for (const Text& t : l.headings) c.text({t.left, t.top, t.left + t.width, t.top + l.rowH}, t.text, kText, draw::kEllipsis);
        for (const auto& [bottom, span] : underlines(l)) c.fill({span.first, bottom, std::max(span.second, width() - l.pad), bottom + 1}, kBorder);
        for (const Text& t : l.labels) c.text({t.left, t.top, t.left + t.width, t.top + l.rowH}, t.text, kDim, draw::kEllipsis);
        // The weapons' names (the rows that have a weapon).
        if (tab_ == kClassicUnits)
            for (int k = 0; k < kCsWeaponRows; ++k) {
                if (weapons_.size() <= size_t(k) || weapons_[size_t(k)] < 0) continue;
                const int y = l.weaponTop + k * l.step + scale(4);
                c.text({l.x0, y, l.x0 + l.labelW + scale(130), y + l.rowH}, weaponLabel_[size_t(k)], kText, draw::kEllipsis);
            }
        // The fields and the check boxes: where the rows put them.
        for (int f = 0; f < classicFieldCount(tab_); ++f) {
            const int y = l.formTop + f * l.step;
            place(fields_[size_t(f)].field, {l.x0 + l.labelW, y, l.x0 + l.labelW + l.numW, y + l.rowH});
        }
        for (int k = 0; k < kCsWeaponRows && tab_ == kClassicUnits; ++k) {
            const int y = l.weaponTop + k * l.step, dx = l.x0 + l.labelW + scale(140);
            place(weaponDamage_[size_t(k)].field, {dx, y, dx + l.numW, y + l.rowH});
            place(weaponBonus_[size_t(k)].field, {dx + l.numW + l.pad, y, dx + l.numW * 2 + l.pad, y + l.rowH});
        }
        const int ax = l.x1 + l.nameColW + l.defColW + (tab_ == kClassicUpgrades ? l.aColW : 0);
        const int bx = l.x1 + l.nameColW + l.defColW + (tab_ == kClassicUpgrades ? 0 : l.aColW);
        for (int p = 0; p < kClassicPlayers; ++p) {
            const int y = l.playersTop + p * l.step;
            place(pDefault_[size_t(p)], {l.x1 + l.nameColW, y, l.x1 + l.nameColW + l.defColW, y + l.rowH});
            if (tab_ == kClassicUpgrades) {
                place(pA_[size_t(p)].field, {ax, y, ax + scale(60), y + l.rowH});
                place(pB_[size_t(p)].field, {bx, y, bx + scale(60), y + l.rowH});
            } else {
                place(pCheckA_[size_t(p)], {ax, y, ax + l.aColW, y + l.rowH});
                place(pCheckB_[size_t(p)], {bx, y, bx + l.aColW, y + l.rowH});
            }
        }
        const int gy = l.playersTop - l.step;
        if (tab_ == kClassicUpgrades) {
            place(globalA_.field, {ax, gy, ax + scale(60), gy + l.rowH});
            place(globalB_.field, {bx, gy, bx + scale(60), gy + l.rowH});
        } else {
            place(globalCheckA_, {ax, gy, ax + l.aColW, gy + l.rowH});
            place(globalCheckB_, {bx, gy, bx + l.aColW, gy + l.rowH});
        }
        syncLooks();
    }

private:
    // A piece of text in the page: its place and what it says.
    struct Text {
        int left, top, width;
        std::wstring text;
    };
    // The page's rows (the same at each paint). The width of the Default column is the text's in the page's font.
    struct Rows {
        int pad, rowH, gap, step, listW, listH, listTop, x0, labelW, numW, x1, nameColW, defColW, aColW, right, titleH;
        int formTop, playersTop, weaponTop, width, height;
        std::vector<Text> headings;  // the dim headings (each has a line under it)
        std::vector<Text> labels;    // the labels of the fields, the players and the weapons
    };
    Rows rows() {
        Rows l{};
        l.pad = scale(stardraft::kit::kDialogPad), l.rowH = scale(stardraft::kit::kFieldH), l.gap = scale(8), l.step = l.rowH + l.gap;
        l.listW = scale(250), l.listH = scale(470), l.listTop = l.pad + l.rowH + l.gap, l.titleH = scale(30);
        l.x0 = l.pad + l.listW + l.pad * 2, l.labelW = scale(130), l.numW = scale(90), l.x1 = l.x0 + l.labelW + l.numW + l.pad * 3;
        const int defW = host_.textWidth(L"Default");
        l.nameColW = std::max<int>(scale(80), defW + scale(8)), l.defColW = std::max<int>(scale(90), defW + scale(34));
        l.aColW = tab_ == kClassicUpgrades ? scale(100) : scale(110);
        l.right = l.x1 + l.nameColW + l.defColW + l.aColW * 2;
        l.formTop = l.pad + scale(36) + l.gap;  // under the title (the "Use default" row is not shown)
        const bool swapAB = tab_ == kClassicUpgrades;  // upgrades: the max level is the first column, the start level the second
        const int headTop = l.formTop - l.step, dy = scale(4);  // the players' heading row: above the first field
        auto heading = [&](const std::wstring& t, int x, int y, int w) { l.headings.push_back({x, y + dy, w, t}); };
        auto label = [&](const std::wstring& t, int x, int y, int w) { l.labels.push_back({x, y + dy, w, t}); };
        for (int f = 0; f < classicFieldCount(tab_); ++f) label(classicFieldName(tab_, f), l.x0, l.formTop + f * l.step, l.labelW);
        int y = l.formTop + classicFieldCount(tab_) * l.step;
        heading(L"Players", l.x1, headTop, l.nameColW);
        if (tab_ == kClassicUpgrades) {
            heading(L"Max level", l.x1 + l.nameColW + l.defColW + (swapAB ? l.aColW : 0), headTop, l.aColW);
            heading(L"Start level", l.x1 + l.nameColW + l.defColW + (swapAB ? 0 : l.aColW), headTop, l.aColW);
        }
        label(L"Default", l.x1, l.formTop, l.nameColW);
        l.playersTop = l.formTop + l.step;
        for (int p = 0; p < kClassicPlayers; ++p) label(L"Player " + std::to_wstring(p + 1), l.x1, l.playersTop + p * l.step, l.nameColW);
        y = std::max(y, l.playersTop + kClassicPlayers * l.step);
        if (tab_ == kClassicUnits) {  // the unit's weapons (base damage, and the damage each upgrade level adds)
            y += l.gap;
            heading(L"Weapon", l.x0, y, l.labelW);
            heading(L"Damage", l.x0 + l.labelW + scale(140), y, l.numW);
            heading(L"Bonus", l.x0 + l.labelW + scale(140) + l.numW + l.pad, y, l.numW);
            y += l.step;
            l.weaponTop = y;
            y += kCsWeaponRows * l.step;
        }
        l.height = std::max(y, l.listTop + l.listH) + l.pad;
        l.width = l.right + l.pad;
        return l;
    }
    // The lines under the headings: per bottom, the left and right edges of the headings on that row.
    static std::map<int, std::pair<int, int>> underlines(const Rows& l) {
        std::map<int, std::pair<int, int>> out;
        for (const Text& h : l.headings) {
            const int bottom = h.top + l.rowH;
            auto it = out.find(bottom);
            if (it == out.end()) out[bottom] = {h.left, h.left + h.width};
            else it->second = {std::min(it->second.first, h.left), std::max(it->second.second, h.left + h.width)};
        }
        return out;
    }
    // A child's frame, set only when it differs (a set repaints).
    void place(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    static bool isDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
    void setNumberLook(ClassicNumber& n) {
        n.field.setAlignRight(true), n.field.setFont({draw::Face::Mono, 0}), n.field.setFilter(isDigit);
    }

    // The check boxes' changes: the page's change of the entry's values.
    enum CheckKind { kCheckGlobalA, kCheckGlobalB, kCheckPlayerDefault, kCheckPlayerA, kCheckPlayerB };
    void checkChanged(CheckKind kind, int p, bool on) {
        ClassicDialog& d = d_;
        const int e = d.entry[tab_];
        const size_t i = size_t(e), pp = size_t(p);
        if (tab_ == kClassicUnits) {
            UnitSettings::Availability& a = d.units.availability[i];
            if (kind == kCheckGlobalA) a.global = on;
            else if (kind == kCheckPlayerDefault) a.usesDefault[pp] = on, a.player[pp] = a.global;  // the player takes the default's value
            else if (kind == kCheckPlayerA) a.player[pp] = on;
        } else if (tab_ == kClassicUpgrades) {
            UpgradeSettings::Levels& l = d.upgrades.levels[i];
            if (kind == kCheckPlayerDefault) l.usesDefault[pp] = on, l.max[pp] = l.defaultMax, l.start[pp] = l.defaultStart;
        } else {
            TechSettings::Players& t = d.techs.players[i];
            if (kind == kCheckGlobalA) t.defaultAvailable = on;
            else if (kind == kCheckGlobalB) t.defaultResearched = on;
            else if (kind == kCheckPlayerDefault) t.usesDefault[pp] = on, t.available[pp] = t.defaultAvailable, t.researched[pp] = t.defaultResearched;
            else if (kind == kCheckPlayerA) t.available[pp] = on;
            else if (kind == kCheckPlayerB) t.researched[pp] = on;
        }
        fill();
        host_.changed();
    }
    // A field of the entry: its value (a typed text goes into the map when the typing stops).
    void fieldEdited(int f) {
        const int e = d_.entry[tab_];
        fields_[size_t(f)].looked = false;
        if (classicUseDefault(tab_, e)) classicSetUseDefault(tab_, e, false);  // an edit: the entry's own values
        classicSetField(tab_, e, f, classicNumberOf(fields_[size_t(f)].field.text()));
        classicFollowDefault(tab_, e);
        invalidate();
        list_.invalidate();
        host_.typed();
    }
    // A weapon's damage or bonus of the selected unit (the rows show the unit's weapons).
    void weaponEdited(int k, bool damage) {
        const int e = d_.entry[tab_];
        ClassicNumber& n = damage ? weaponDamage_[size_t(k)] : weaponBonus_[size_t(k)];
        n.looked = false;
        if (weapons_.size() <= size_t(k) || weapons_[size_t(k)] < 0) return;
        if (classicUseDefault(tab_, e)) classicSetUseDefault(tab_, e, false);
        const size_t w = size_t(weapons_[size_t(k)]);
        const uint16_t v = uint16_t(std::min<uint32_t>(classicNumberOf(n.field.text()), 0xFFFF));
        if (damage) d_.units.weapons[w].damage = v;
        else d_.units.weapons[w].bonus = v;
        classicFollowDefault(tab_, e);
        invalidate();
        list_.invalidate();
        host_.typed();
    }
    // An upgrade's level: the default (the players that use it show it) or a player's own.
    void upgradeLevelEdited(bool global, bool isA, int p) {
        const int e = d_.entry[tab_];
        UpgradeSettings::Levels& l = d_.upgrades.levels[size_t(e)];
        ClassicNumber& n = global ? (isA ? globalA_ : globalB_) : (isA ? pA_[size_t(p)] : pB_[size_t(p)]);
        n.looked = false;
        const uint8_t v = uint8_t(std::min<uint32_t>(classicNumberOf(n.field.text()), 255));
        if (global) {
            (isA ? l.defaultMax : l.defaultStart) = v;
            for (int q = 0; q < kClassicPlayers; ++q)  // the players that use the defaults show them
                if (l.usesDefault[size_t(q)]) {
                    ClassicNumber& shown = isA ? pA_[size_t(q)] : pB_[size_t(q)];
                    shown.field.setText(std::to_wstring(v)), shown.looked = false;
                }
        } else if (!l.usesDefault[size_t(p)]) {
            (isA ? l.max : l.start)[size_t(p)] = v;
        }
        invalidate();
        list_.invalidate();
        host_.typed();
    }
    // Rename: the game text pop-out under the title (the game's colors); empty: the game's name.
    void renameUnit() {
        const int e = d_.entry[tab_];
        std::string text = classicUseDefault(tab_, e) ? "" : d_.names[size_t(e)];
        const Rows l = rows();
        if (host_.editUnitName(e, text, {l.x0, l.pad + l.titleH + scale(4)})) setName(text);
    }
    // What feedback names at a point of the page: the entry, and the field's row and column as the page's labels say
    // them (the Players column: Player N; the upgrades' columns: Max level, Start level).
    std::wstring feedbackName(draw::Point pt) override {
        const std::wstring n = title();
        auto at = [&](const draw::View& v) { return v.visible() && v.frame().contains(pt); };
        std::wstring row, col;
        bool column = false;
        for (int f = 0; f < classicFieldCount(tab_); ++f)
            if (at(fields_[size_t(f)].field)) row = classicFieldName(tab_, f);
        for (int k = 0; k < kCsWeaponRows; ++k)
            if (at(weaponDamage_[size_t(k)].field) || at(weaponBonus_[size_t(k)].field)) row = weaponLabel_[size_t(k)];
        for (int p = 0; p < kClassicPlayers; ++p) {
            const std::wstring player = L"Player " + std::to_wstring(p + 1);
            if (at(pDefault_[size_t(p)])) row = player;
            if (at(pA_[size_t(p)].field) || at(pCheckA_[size_t(p)]) || at(pB_[size_t(p)].field) || at(pCheckB_[size_t(p)])) {
                row = player, column = true;
                col = (tab_ == kClassicUpgrades && (at(pB_[size_t(p)].field))) ? L"Start level" : tab_ == kClassicUpgrades ? L"Max level" : L"";
            }
        }
        if (at(globalCheckA_) || at(globalCheckB_) || at(globalA_.field) || at(globalB_.field)) {
            row = L"Default", column = at(globalA_.field) || at(globalB_.field) || at(globalCheckA_) || at(globalCheckB_);
            col = (at(globalB_.field) || at(globalCheckB_)) ? (tab_ == kClassicUpgrades ? L"Start level" : L"Researched") : (tab_ == kClassicUpgrades ? L"Max level" : L"");
        }
        if (at(rename_) || at(search_) || at(list_)) return n;
        std::wstring field = row;
        if (column && !col.empty()) field += L", " + col;
        if (field.empty()) return n;
        return n.empty() ? field : n + L": " + field;
    }

    // The list: the entries whose name has the search text.
    void fillList() {
        std::wstring q = search_.text();
        for (wchar_t& c : q) c = wchar_t(towlower(c));
        shown_.clear();
        for (int e = 0; e < classicEntryCount(tab_); ++e) {
            std::wstring n = host_.entryName(tab_, e);
            for (wchar_t& c : n) c = wchar_t(towlower(c));
            if (q.empty() || n.find(q) != std::wstring::npos) shown_.push_back(e);
        }
        list_.setCount(int(shown_.size()));
        selectRow(true);
        list_.invalidate();
    }
    // The list's selection is the entry (when it is shown). `inView`: its row is brought into view (the list's new
    // rows, the page's first show); a value's change does not scroll the list (the old list kept its place).
    void selectRow(bool inView) {
        const auto at = std::find(shown_.begin(), shown_.end(), d_.entry[tab_]);
        if (at == shown_.end()) {
            list_.clearSelection();
            return;
        }
        list_.selectOnly(int(at - shown_.begin()));
        if (inView) scrollToRow_ = int(at - shown_.begin());  // in view when the list has its size (the next paint)
    }
    // Shows the selected entry's values in the page's fields.
    void fill() {
        const int e = d_.entry[tab_];
        const size_t i = size_t(e);
        const bool def = classicUseDefault(tab_, e);
        selectRow(false);
        for (int f = 0; f < classicFieldCount(tab_); ++f) {
            ClassicNumber& n = fields_[size_t(f)];
            n.field.setText(std::to_wstring(classicFieldValue(tab_, e, f, def)));
            n.field.setReadOnly(false);
            n.looked = false;
        }
        if (tab_ == kClassicUnits) {
            fields_[1].field.setReadOnly(!host_.hasShields(e));  // shields count only for units that have them
            fields_[1].field.setTabStop(host_.hasShields(e));    // (a unit without them: a disabled box, not a Tab stop)
            // The unit's weapons: its own, then its turret's (the rows that it does not use are not shown).
            const std::vector<int> ws = classicUnitWeapons(e);
            weapons_.assign(kCsWeaponRows, -1);
            for (size_t k = 0; k < ws.size() && k < size_t(kCsWeaponRows); ++k) weapons_[k] = ws[k];
            for (int k = 0; k < kCsWeaponRows; ++k) {
                const bool has = weapons_[size_t(k)] >= 0;
                ClassicNumber& dmg = weaponDamage_[size_t(k)];
                ClassicNumber& bon = weaponBonus_[size_t(k)];
                dmg.field.setVisible(has), bon.field.setVisible(has);
                dmg.looked = bon.looked = false;
                if (!has) {
                    weaponLabel_[size_t(k)] = L"";
                    continue;
                }
                const size_t w = size_t(weapons_[size_t(k)]);
                const UnitSettings::Weapon& v = def ? d_.def->units.weapons[w] : d_.units.weapons[w];
                weaponLabel_[size_t(k)] = entryName("weapons", int(w)) + L": " + tr(classicWeaponTargets(e, int(w)));
                dmg.field.setText(std::to_wstring(v.damage)), bon.field.setText(std::to_wstring(v.bonus));
                dmg.field.setReadOnly(false), bon.field.setReadOnly(false);
            }
            const UnitSettings::Availability& a = d_.units.availability[i];
            globalCheckA_.setOn(a.global);
            for (int p = 0; p < kClassicPlayers; ++p) {
                const bool uses = a.usesDefault[size_t(p)];
                pDefault_[size_t(p)].setOn(uses);
                pCheckA_[size_t(p)].setOn(uses ? a.global : a.player[size_t(p)]), pCheckA_[size_t(p)].setEnabled(!uses);
            }
        } else if (tab_ == kClassicUpgrades) {
            const UpgradeSettings::Levels& l = d_.upgrades.levels[i];
            globalA_.field.setText(std::to_wstring(l.defaultMax)), globalB_.field.setText(std::to_wstring(l.defaultStart));
            globalA_.looked = globalB_.looked = false;
            for (int p = 0; p < kClassicPlayers; ++p) {
                const bool uses = l.usesDefault[size_t(p)];
                pDefault_[size_t(p)].setOn(uses);
                pA_[size_t(p)].field.setText(std::to_wstring(uses ? l.defaultMax : l.max[size_t(p)]));
                pB_[size_t(p)].field.setText(std::to_wstring(uses ? l.defaultStart : l.start[size_t(p)]));
                pA_[size_t(p)].field.setReadOnly(uses), pB_[size_t(p)].field.setReadOnly(uses);
                pA_[size_t(p)].looked = pB_[size_t(p)].looked = false;
            }
        } else {
            const TechSettings::Players& t = d_.techs.players[i];
            globalCheckA_.setOn(t.defaultAvailable), globalCheckB_.setOn(t.defaultResearched);
            for (int p = 0; p < kClassicPlayers; ++p) {
                const bool uses = t.usesDefault[size_t(p)];
                pDefault_[size_t(p)].setOn(uses);
                pCheckA_[size_t(p)].setOn(uses ? t.defaultAvailable : t.available[size_t(p)]), pCheckA_[size_t(p)].setEnabled(!uses);
                pCheckB_[size_t(p)].setOn(uses ? t.defaultResearched : t.researched[size_t(p)]), pCheckB_[size_t(p)].setEnabled(!uses);
            }
        }
        invalidate();
        list_.invalidate();
    }

    // The colors of the values: a changed value in the changed color, a disabled or read-only one dim (as the page showed
    // them). Set again only when the color differs (the spans are set again with it).
    void syncLooks() {
        const int e = d_.entry[tab_];
        for (int f = 0; f < classicFieldCount(tab_); ++f) {
            draw::Color want = draw::kNone;
            if (tab_ == kClassicUnits && f == 1 && !host_.hasShields(e)) want = kFaint;
            else if (classicFieldChanged(tab_, e, f)) want = kGood;
            look(fields_[size_t(f)], want);
        }
        if (tab_ == kClassicUnits)
            for (int k = 0; k < kCsWeaponRows; ++k) {
                if (weapons_.size() <= size_t(k) || weapons_[size_t(k)] < 0) continue;
                const size_t w = size_t(weapons_[size_t(k)]);
                const bool own = !classicUseDefault(tab_, e);
                look(weaponDamage_[size_t(k)], own && d_.units.weapons[w].damage != d_.def->units.weapons[w].damage ? kGood : draw::kNone);
                look(weaponBonus_[size_t(k)], own && d_.units.weapons[w].bonus != d_.def->units.weapons[w].bonus ? kGood : draw::kNone);
            }
        if (tab_ == kClassicUpgrades)
            for (int p = 0; p < kClassicPlayers; ++p) {
                const bool uses = d_.upgrades.levels[size_t(e)].usesDefault[size_t(p)];
                look(pA_[size_t(p)], uses ? kDim : draw::kNone);
                look(pB_[size_t(p)], uses ? kDim : draw::kNone);
            }
    }
    void look(ClassicNumber& n, draw::Color want) {
        if (n.looked && n.look == want) return;
        n.looked = true, n.look = want;
        std::vector<stardraft::kit::Span> spans;
        if (want != draw::kNone && !n.field.text().empty()) spans.push_back({0, n.field.text().size(), want});
        n.field.setSpans(std::move(spans));
    }

    // The entry as a row: its icon, its name, and the changed dot (the row of an owner-drawn list: the same look).
    void paintRow(draw::Canvas& c, int i, draw::Rect r, stardraft::kit::RowState s) {
        const int e = shown_[size_t(i)];
        const bool sel = s.selected;
        const int ring = scale(1) + 1;
        c.fill(r, kCtl);
        if (sel) {
            c.fill(r, kSel);
            c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
            c.fill({r.left + ring, r.top, r.left + ring + scale(2), r.bottom}, kAccent);
        }
        const draw::Rect box{r.left + scale(6), r.top + scale(2), r.left + scale(30), r.bottom - scale(2)};
        host_.drawIcon(c, tab_, e, box);
        const draw::Rect t{box.right + scale(8), r.top, r.right - scale(16), r.bottom};
        c.text(t, host_.entryName(tab_, e), sel ? kAccent : kText, draw::kMiddle | draw::kEllipsis);
        if (classicEntryChanged(tab_, e)) {
            const int cx = r.right - scale(9), cy = (r.top + r.bottom) / 2, rr = scale(3);
            c.ellipse({cx - rr, cy - rr, cx + rr + 1, cy + rr + 1}, kGood, draw::kNone);
        }
    }

    ClassicHost& host_;
    ClassicDialog& d_;
    int tab_ = 0;
    SearchBox search_;
    stardraft::kit::ListView list_;
    std::vector<int> shown_;  // the entries in the list, in order
    int scrollToRow_ = -1;    // the selected row, to bring in view at the next paint (-1: none)
    stardraft::kit::Button rename_;
    ClassicNumber fields_[6];
    ClassicNumber weaponDamage_[kCsWeaponRows], weaponBonus_[kCsWeaponRows];
    std::vector<int> weapons_;  // the weapon of each weapon row (-1: none)
    std::wstring weaponLabel_[kCsWeaponRows];
    ClassicNumber globalA_, globalB_;
    ClassicNumber pA_[kClassicPlayers], pB_[kClassicPlayers];
    stardraft::kit::CheckBox globalCheckA_, globalCheckB_;
    stardraft::kit::CheckBox pDefault_[kClassicPlayers], pCheckA_[kClassicPlayers], pCheckB_[kClassicPlayers];
};
