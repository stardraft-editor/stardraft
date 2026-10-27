// The Map data page's Table view (portable, core.inc, after views_classic_settings.inc): a classic map's unit, upgrade
// and tech settings as the Data workspace's table shows a .dat (views_data_table.inc). A tab's entries are the rows and
// its value fields (Hit points, Shields, ... as the page's form has them) are the columns. The table's values are a copy
// of the page's working copy (gClassic): the game's values as the table's stock, the entry's values (the game's when it
// uses the defaults) as its values. A change in the table goes back into the working copy (the entry stops using the
// defaults, and uses them again when nothing differs), and the program puts it into the map (one undo step).

struct ClassicTableModel {
    DatSchema schema[kClassicTabCount];
    DatTable table[kClassicTabCount];
    Bytes built[kClassicTabCount];  // the values when the table was made from the working copy (a change is a difference from them)
    bool made[kClassicTabCount] = {};  // the table has its schema and values
};
ClassicTableModel gClassicTable;
ClassicHost* gClassicTableHost = nullptr;  // the page's host: the rows' names (the program sets it)

const char* const kClassicTableNames[kClassicTabCount] = {"map units", "map upgrades", "map techs"};

// The size of a field's value: hit points take 4 bytes (up to 0xFFFFFF), armor 1, the others 2.
int classicTableFieldSize(int tab, int f) { return tab == kClassicUnits ? (f == 0 ? 4 : f == 2 ? 1 : 2) : 2; }

// The tab's schema: one field per value field, each a run of the tab's entries (made again when the count changes: an
// original map has fewer upgrades and techs).
void classicTableSchema(int tab) {
    DatSchema& s = gClassicTable.schema[tab];
    const int n = classicEntryCount(tab);
    if (s.entries == n && !s.fields.empty()) return;
    gClassicTable.made[tab] = false;
    s = DatSchema{};
    s.name = kClassicTableNames[tab];
    s.entries = n;
    s.sections = {narrow(kClassicTabNames[tab])};
    uint32_t at = 0;
    for (int f = 0; f < classicFieldCount(tab); ++f) {
        DatField d;
        d.index = f, d.name = narrow(classicFieldName(tab, f)), d.size = classicTableFieldSize(tab, f);
        d.first = 0, d.last = n - 1, d.fileOffset = at, d.section = 0, d.order = f;
        at += uint32_t(d.size * n);
        s.fields.push_back(std::move(d));
    }
    s.fileSize = at;
}

// The values of the tab (the game's, or the entries' as the page shows them) laid out as its schema says.
Bytes classicTableBytes(int tab, bool game) {
    const DatSchema& s = gClassicTable.schema[tab];
    Bytes b(s.fileSize, 0);
    for (const DatField& f : s.fields)
        for (int e = 0; e < s.entries; ++e) {
            const uint32_t v = classicFieldValue(tab, e, f.index, game || classicUseDefault(tab, e));
            const size_t o = f.fileOffset + size_t(e) * size_t(f.size);
            for (int k = 0; k < f.size; ++k) b[o + size_t(k)] = uint8_t(v >> (8 * k));
        }
    return b;
}

// The tab's table, made again when the working copy changed (an undo, a change in the form, another map).
DatTable* classicTable(int tab) {
    if (!gClassic || tab < 0 || tab >= kClassicTabCount) return nullptr;
    classicTableSchema(tab);
    Bytes now = classicTableBytes(tab, false);
    if (!gClassicTable.made[tab] || now != gClassicTable.built[tab]) {
        gClassicTable.table[tab].setValues(gClassicTable.schema[tab], classicTableBytes(tab, true), now);
        gClassicTable.built[tab] = std::move(now);
        gClassicTable.made[tab] = true;
    }
    return &gClassicTable.table[tab];
}

// The table's changes into the working copy: each value that differs from the one the table was made with. True when
// a value changed.
bool classicTableCommit(int tab) {
    if (!gClassic || tab < 0 || tab >= kClassicTabCount || !gClassicTable.made[tab]) return false;
    const DatSchema& s = gClassicTable.schema[tab];
    const DatTable& t = gClassicTable.table[tab];
    if (gClassicTable.built[tab].size() != s.fileSize) return false;
    DatTable before;
    before.setValues(s, {}, gClassicTable.built[tab]);
    bool any = false;
    for (int e = 0; e < s.entries; ++e) {
        bool entry = false;
        for (const DatField& f : s.fields) {
            const uint32_t v = t.value(f, e);
            if (v == before.value(f, e)) continue;
            if (!entry && classicUseDefault(tab, e)) classicSetUseDefault(tab, e, false);  // (the game's values, then this one)
            classicSetField(tab, e, f.index, v);
            entry = any = true;
        }
        if (entry) classicFollowDefault(tab, e);
    }
    return any;
}

// A row's name: the page's (a unit's game name and its own name).
std::wstring classicTableRowName(const std::string& dat, int e) {
    for (int tab = 0; tab < kClassicTabCount; ++tab)
        if (dat == kClassicTableNames[tab]) return gClassicTableHost && gClassic ? gClassicTableHost->entryName(tab, e) : L"";
    return L"";
}
// Why a cell cannot change: a unit's shields, when the unit has none (as the form grays them).
std::wstring classicTableCellLocked(const std::string& dat, int field, int e) {
    if (dat == kClassicTableNames[kClassicUnits] && field == 1 && gClassicTableHost && !gClassicTableHost->hasShields(e)) return L"This unit has no shields.";
    return L"";
}
inline const bool kClassicTableNamesSet = (gDataMapRowName = classicTableRowName, gDataMapCellLocked = classicTableCellLocked, true);
