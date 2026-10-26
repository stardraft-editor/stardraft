// The Data workspace's tab strip (portable, core.inc): the layout of the .dat tabs above the entry list. Each tab is its
// icon, its name and its entry count; the strip drops the counts, then the icons, when the tabs do not fit before the
// back and forward arrows. The program measures the text (its font) and gives the metrics in pixels; the strip's paint,
// its hot tab and the click stay in main.cpp (dataWndProc), which reads the boxes from here.

// One tab's measured text: its name's width, and its count's width (the count as the strip would show it).
struct DataTabText {
    int nameW = 0;
    int countW = 0;
};

// The strip's metrics in pixels (scaled by the program).
struct DataTabMetrics {
    int startX = 0;      // the first tab's left edge
    int reserved = 0;    // the room kept for the back and forward arrows (taken off the strip's width)
    int iconRoom = 0;    // the room one tab's icon takes in the fit test
    int iconW = 0;       // the icon and the gap after it (when the icons show)
    int pad = 0;         // a tab's padding around its name and count
    int gap = 0;         // the gap after each tab
};

// The layout: whether the counts and the icons show, and each tab's box (left and right edges, in order).
struct DataTabLayout {
    bool showCounts = true;
    bool showIcons = true;
    std::vector<std::pair<int, int>> boxes;  // (left, right) per tab
};

// What fits before the arrows: the names with their pads and gaps, the counts, and the icons; without the counts
// first, then without the icons too. A hidden count is 0 wide.
DataTabLayout dataTabLayout(const std::vector<DataTabText>& tabs, int width, const DataTabMetrics& m) {
    DataTabLayout out;
    int names = 0, counts = 0;
    for (const DataTabText& t : tabs) {
        names += t.nameW + m.pad + m.gap, counts += t.countW;
    }
    const int room = width - m.reserved, icons = int(tabs.size()) * m.iconRoom;
    if (names + counts + icons > room) out.showCounts = false;
    if (names + icons > room) out.showIcons = false;
    int x = m.startX;
    for (const DataTabText& t : tabs) {
        const int iw = out.showIcons ? m.iconW : 0;
        const int cw = out.showCounts ? t.countW : 0;
        const int right = x + iw + t.nameW + cw + m.pad;
        out.boxes.push_back({x, right});
        x = right + m.gap;
    }
    return out;
}

// The .dat's tab title and the field helpers (moved from main.cpp unchanged: the SDL program's Data form uses them too).
// A .dat's tab title: "units" -> "Units", "techdata" -> "Tech data", "sfxdata" -> "SFX data".
std::wstring datTitle(const std::string& name) {
    static const std::map<std::string, std::wstring> titles = {
        {"units", L"Units"},     {"weapons", L"Weapons"},   {"flingy", L"Flingy"},     {"sprites", L"Sprites"},  {"images", L"Images"},
        {"upgrades", L"Upgrades"}, {"techdata", L"Tech data"}, {"orders", L"Orders"}, {"sfxdata", L"SFX data"}, {"portdata", L"Port data"}};
    auto it = titles.find(name);
    return it != titles.end() ? it->second : widen(name);
}

// A field that the form shows and takes in the game's unit, not as the .dat stores it: hit points (units.dat keeps
// 1/256 of a hit point). The stored values in one shown unit; 1: as stored.
uint32_t fieldShownScale(const DatField& f) { return stardraft::datShownScale(f); }  // (dat_units.h: shared with the CLI)

std::wstring fieldValueText(const DatField& f, uint32_t v) {
    if (const uint32_t k = fieldShownScale(f); k > 1) return widen(stardraft::datShownText(k, v));  // whole ones, or with the part of one

    if (!f.flags.empty()) {
        wchar_t b[16];
        std::swprintf(b, 16, f.size == 4 ? L"0x%08X" : f.size == 2 ? L"0x%04X" : L"0x%02X", v);
        return b;
    }
    return std::to_wstring(v);
}

// The .dat a field's value is an entry of ("icons" for command icons); "" when it is not a reference.
std::string fieldRefDat(const DatField& f) {
    if (!f.ref.empty()) return f.ref;
    if (f.name == "Icon" || f.name == "Highlight") return "icons";
    if (f.name == "Iscript ID") return "iscript";  // named (EUD Editor's list), not a .dat
    if (f.name == "Rank/Sublabel") return "ranks";  // stat_txt.tbl's titles, not a .dat
    return "";
}
bool fieldIsDropdown(const DatField& f) { return f.flags.empty() && (!fieldRefDat(f).empty() || !f.valueNames.empty()); }

// A yes/no value: a checkbox. These are the bytes the game reads as bools (OpenBW data_types.h, and how
// bwgame.h tests them); any value not 0 is yes. By .dat, the field indexes:
//   units 6 has_shield, 57 is_broodwar; upgrades 11 is_broodwar; techdata 9 the Brood War flag (OpenBW reads it
//   with the byte before it and does not use it; StarEdit reads it as yes/no); sprites 3 visible;
//   images 1 has_directional_frames, 2 is_clickable, 3 has_iscript_animations, 4 always_visible;
//   orders 1 targets_enemies, 4 valid_for_turret, 6 can_be_interrupted, 7 unk7 (an order in the queue waits),
//   8 can_be_queued, 10 can_be_obstructed.
bool fieldIsBool(const std::string& dat, const DatField& f) {
    static const std::map<std::string, std::set<int>> kBools = {
        {"units", {6, 57}},   {"upgrades", {11}},           {"techdata", {9}},
        {"sprites", {3}},     {"images", {1, 2, 3, 4}},     {"orders", {1, 4, 6, 7, 8, 10}},
    };
    const auto it = kBools.find(dat);
    return it != kBools.end() && f.flags.empty() && f.size == 1 && it->second.count(f.index);
}

bool fieldIsNumber(const std::string& dat, const DatField& f) { return f.flags.empty() && !fieldIsDropdown(f) && !fieldIsBool(dat, f); }
