// The Data workspace's pins, its back and forward places, and its clipboard (portable, core.inc, after data_model.inc):
// the entries pinned to the top of a list (this user's settings), the places looked at (the arrows and the mouse's side
// buttons), an entry's or a field's values copied and pasted, and the items of the entry's and the field's menus. Each
// program shows the menus (the Windows program a Win32 menu, the SDL program a kit menu) and gives the parts below.

// The program's parts: a value's change saved (dataSaveChange, and what its windows show of it), the list's rows made
// again (with the pins), and the form and the list painted again.
bool dataSaved();
void dataRebuildList();
void dataPanesChanged();

// --- Pins: Data > right-click an entry > Pin to the top ----------------------------------------------------------------
// Pinned entries come first in their table's list, in the order they were pinned, for this user (the settings'
// DataPins/<table>: its entries comma-separated).

std::map<std::string, std::vector<int>> gDataPins;
bool gDataPinsLoaded = false;
int gDataPinnedShown = 0;  // how many of the list's first rows are pinned (a line under the last one)

std::string dataTabKey(int tab) {
    const auto& all = DatSchema::all();
    return tab >= 0 && tab < int(all.size()) ? all[size_t(tab)].name : tab == int(all.size()) ? "strings" : "buttons";
}
std::vector<int>& dataPins(int tab) {
    if (!gDataPinsLoaded) {
        gDataPinsLoaded = true;
        // (tests and snapshot runs: no pins, as the defaults)
        for (const std::wstring& name : gSelfTest || gSettingsOff ? std::vector<std::wstring>{} : prefs::names(L"DataPins")) {
            std::wstring value;
            if (!prefs::text(L"DataPins/" + name, value)) continue;
            auto& v = gDataPins[narrow(name)];
            const std::vector<int> parsed = dataPinsParse(value);
            v.insert(v.end(), parsed.begin(), parsed.end());
        }
    }
    return gDataPins[dataTabKey(tab)];
}
bool dataPinned(int tab, int e) {
    const auto& v = dataPins(tab);
    return std::find(v.begin(), v.end(), e) != v.end();
}
// Pins entry `e` of tab `tab` (or unpins it) and saves the pins. True when it is pinned now. (A classic map's Map data
// page pins its units, upgrades and techs here too: one user's pins in both lists.)
bool dataPinSave(int tab, int e) {
    const bool pin = dataPinToggle(dataPins(tab), e);
    if (!gSelfTest && !gSettingsOff)  // (tests and snapshot runs leave the user's settings alone)
        prefs::setText(L"DataPins/" + widen(dataTabKey(tab)), dataPinsText(dataPins(tab)));
    return pin;
}
// Pins entry `e` of the open tab (or unpins it), saves the pins and makes the list again; the status says which.
void dataTogglePin(int e) {
    const int tab = dataState().datTab;
    const bool pin = dataPinSave(tab, e);
    dataRebuildList();
    const std::wstring name = dataEntryName(gDataListProgram, tab, e);
    setMessage(pin ? tr(L"Pinned ") + name + tr(L" to the top of the list") : tr(L"Unpinned ") + name);
}
// The open tab's list rows: the filter's, with the pins first (gDataPinnedShown: how many are pinned).
std::vector<int> dataListShown(const std::wstring& search, bool changedOnly) {
    const DataListRows rows = dataListRows(gDataListProgram, dataState().datTab, search, changedOnly, dataPins(dataState().datTab));
    gDataPinnedShown = rows.pinnedShown;
    return rows.shown;
}

// --- Back and forward through the entries looked at --------------------------------------------------------------------

struct DataPlace {
    int tab = 0, entry = 0;
};
std::vector<DataPlace> gDataBack, gDataForward;
bool gDataTravelling = false;  // going back or forward: not a new place

// Before the open entry changes to (tab, entry): the open one is a place to go back to. False when nothing is recorded
// (the same entry, or back and forward go on).
bool dataPlaceRecord(int tab, int entry) {
    const DataModel& d = dataState();
    if (gDataTravelling || (tab == d.datTab && entry == d.entry)) return false;
    gDataBack.push_back({d.datTab, d.entry});
    if (gDataBack.size() > 200) gDataBack.erase(gDataBack.begin());
    gDataForward.clear();
    return true;
}
// Back (or forward): the place before (after) the open entry opens, through the program's tab switch and selection; the
// open entry is the place to go forward (back) to. False when there is no such place.
bool dataTravelTo(bool back, const std::function<void(int)>& showTab, const std::function<void(int)>& select) {
    auto& from = back ? gDataBack : gDataForward;
    auto& to = back ? gDataForward : gDataBack;
    if (from.empty()) return false;
    DataModel& d = dataState();
    const DataPlace p = from.back();
    from.pop_back();
    to.push_back({d.datTab, d.entry});
    gDataTravelling = true;
    // (Every tab's entry, the Strings and Buttons tabs' too: a place can be on them.)
    d.tabEntry.resize(std::max(d.tabEntry.size(), size_t(dataTabCount())), 0);
    if (p.tab != d.datTab && p.tab >= 0 && p.tab < int(d.tabEntry.size())) d.tabEntry[size_t(p.tab)] = p.entry;
    showTab(p.tab);
    select(p.entry);
    gDataTravelling = false;
    return true;
}

// --- The clipboard: an entry's values (every field it has), or one field's value ---------------------------------------
// Pasting goes to the same .dat: an entry onto another entry, a field onto the same field of another entry.

struct DatClipboard {
    std::string dat;
    int entry = -1;  // copied from (for the message)
    std::vector<std::pair<int, uint32_t>> values;  // field index, value
    int field = -1;                                // a single field's clipboard: its index (-1: none)
    uint32_t fieldValue = 0;
    std::string fieldDat;
};
DatClipboard gDatClip;

// Copy a unit's display (status panel, wireframes) and button set with its values (#11): Paste everything gives them
// to another unit. Its own wireframes and the game's panel are copied as that unit's, so the other unit looks the same.
struct UnitExtrasClip {
    int from = -1;  // -1: nothing copied
    UnitLooks::Unit looks;
    int buttonSet = -1;
} gUnitExtrasClip;
void extrasCopyUnit(int u) {
    DataExtrasModel& x = dataExtras();
    if (u < 0 || u >= UnitLooks::kUnits) return void(gUnitExtrasClip.from = -1);
    UnitLooks::Unit l = x.looks.unit(u);
    l.status = x.looks.status(u), l.display = x.looks.display(u);
    if (l.wireframe < 0 && u < UnitLooks::kWireframes) l.wireframe = u;
    if (l.groupWireframe < 0 && u < UnitLooks::kGroupWireframes) l.groupWireframe = u;
    if (l.transportWireframe < 0 && u < UnitLooks::kTransportWireframes) l.transportWireframe = u;
    gUnitExtrasClip = {u, l, x.buttons.unitSet(u)};
}
bool extrasCanPasteUnit(int to) { return gUnitExtrasClip.from >= 0 && to >= 0 && to < UnitLooks::kUnits && to != gUnitExtrasClip.from; }
// Into the project's copy only: the caller saves (dataSaved) and so makes the undo steps.
void extrasPasteUnit(int to) {
    if (!extrasCanPasteUnit(to)) return;
    DataExtrasModel& x = dataExtras();
    UnitLooks::Unit l = gUnitExtrasClip.looks;
    if (l.status == x.looks.stockStatus(to)) l.status = 0;  // the game's own for that unit: not a change
    if (l.display == x.looks.stockDisplay(to)) l.display = 0;
    if (l.wireframe == to) l.wireframe = -1;
    if (l.groupWireframe == to) l.groupWireframe = -1;
    if (l.transportWireframe == to) l.transportWireframe = -1;
    x.looks.set(to, l);
    if (gUnitExtrasClip.buttonSet >= 0) x.buttons.setUnitSet(to, gUnitExtrasClip.buttonSet);
}

void dataCopyEntry(int u) {
    const DatTable* t = curTable();
    if (!t) return;
    gDatClip.dat = t->schema().name, gDatClip.entry = u, gDatClip.values.clear();
    for (const auto& f : t->schema().fields)
        if (f.covers(u)) gDatClip.values.push_back({f.index, t->value(f, u)});
    if (gDatClip.dat == "units") extrasCopyUnit(u);
    setMessage(L"Copied " + entryName(gDatClip.dat, u) + L"'s values");
}

bool dataCanPasteEntry() { return curTable() && !gDatClip.values.empty() && gDatClip.dat == curTable()->schema().name; }
// A unit: Paste everything: the values, the display (status panel, wireframes) and the button set of the copied unit.
bool dataCanPasteAll(int u) { return dataCanPasteEntry() && gDatClip.dat == "units" && extrasCanPasteUnit(u) && gDatClip.entry != u; }
void dataPasteAll(int u) {
    DatTable* t = curTable();
    if (!t || !dataCanPasteAll(u)) return;
    int n = 0, skipped = 0;
    for (const auto& [fi, v] : gDatClip.values) {
        if (fi >= 0 && fi < int(t->schema().fields.size()) && t->schema().fields[size_t(fi)].covers(u)) t->set(t->schema().fields[size_t(fi)], u, v), ++n;
        else ++skipped;
    }
    extrasPasteUnit(u);
    const bool saved = dataSaved();
    dataPanesChanged();
    if (saved)
        setMessage(tr(L"Pasted everything of ") + entryName("units", gDatClip.entry) + tr(L" onto ") + entryName("units", u) + L": " + std::to_wstring(n) +
                   tr(L" values, the display and the button set") +
                   (skipped ? L" (" + std::to_wstring(skipped) + tr(L" values that this unit does not have: not pasted)") : L""));
}

void dataPasteEntry(int u) {
    DatTable* t = curTable();
    if (!t || !dataCanPasteEntry()) return;
    int n = 0;
    for (const auto& [fi, v] : gDatClip.values)
        if (fi >= 0 && fi < int(t->schema().fields.size()) && t->schema().fields[size_t(fi)].covers(u)) t->set(t->schema().fields[size_t(fi)], u, v), ++n;
    const bool saved = dataSaved();
    dataPanesChanged();
    if (saved) setMessage(L"Pasted " + entryName(gDatClip.dat, gDatClip.entry) + L"'s values onto " + entryName(gDatClip.dat, u) + L" (" + std::to_wstring(n) + L" fields)");
}

void dataResetEntry(int u) {
    DatTable* t = curTable();
    if (!t) return;
    for (const auto& f : t->schema().fields)
        if (f.covers(u)) t->reset(f, u);
    const bool saved = dataSaved();
    dataPanesChanged();
    if (saved) setMessage(L"Reset " + entryName(t->schema().name, u) + L" to the game's values");
}

// Edit > Copy and Edit > Paste (Ctrl+C, Ctrl+V) in the Data workspace: the open entry's values.
void dataCopySelected() { dataCopyEntry(dataState().entry); }
void dataPasteSelected() { dataPasteEntry(dataState().entry); }

// --- The menus ----------------------------------------------------------------------------------------------------------

// An entry's menu (the right button over the list): its items' ids are what dataEntryMenuPick takes.
MenuModel dataEntryMenuModel(int u) {
    MenuModel m;
    const DatTable* t = curTable();
    if (!t) return m;
    bool changed = false;
    for (const auto& f : t->schema().fields) changed |= f.covers(u) && t->changed(f, u);
    auto item = [&m](int id, const std::wstring& label, const std::wstring& shortcut, bool enabled) {
        MenuItem it;
        it.id = id, it.label = label, it.shortcut = shortcut, it.grayed = !enabled;
        m.push_back(std::move(it));
    };
    auto separator = [&m] {
        MenuItem it;
        it.kind = MenuItem::Kind::Separator;
        m.push_back(std::move(it));
    };
    item(1, L"&Copy values", L"Ctrl+C", true);
    item(2, L"&Paste values" + (dataCanPasteEntry() ? L" of " + entryName(gDatClip.dat, gDatClip.entry) : std::wstring()), L"Ctrl+V", dataCanPasteEntry());
    if (t->schema().name == "units")
        item(5, tr(L"Paste &everything") + (dataCanPasteAll(u) ? tr(L" of ") + entryName("units", gDatClip.entry) : std::wstring()) + tr(L" (values, display, buttons)"), L"",
             dataCanPasteAll(u));
    separator();
    item(3, L"&Reset to the game's values", L"", changed);
    separator();
    item(4, dataPinned(dataState().datTab, u) ? L"Un&pin" : L"&Pin to the top", L"", true);
    return m;
}
void dataEntryMenuPick(int u, int pick) {
    if (pick == 1) dataCopyEntry(u);
    else if (pick == 2) dataPasteEntry(u);
    else if (pick == 3) dataResetEntry(u);
    else if (pick == 4) dataTogglePin(u);
    else if (pick == 5) dataPasteAll(u);
}

// A field's menu (the right button over a field of the form): copy, paste and reset its value.
MenuModel dataFieldMenuModel(const DatField& f) {
    MenuModel m;
    const DatTable* t = curTable();
    if (!t) return m;
    const int u = dataState().entry;
    const bool canPaste = gDatClip.field == f.index && gDatClip.fieldDat == t->schema().name && f.covers(u);
    auto item = [&m](int id, const std::wstring& label, bool enabled) {
        MenuItem it;
        it.id = id, it.label = label, it.grayed = !enabled;
        m.push_back(std::move(it));
    };
    item(1, L"&Copy " + datLabel(f.name, false), f.covers(u));
    item(2, L"&Paste " + datLabel(f.name, false) + (canPaste ? L" (" + fieldValueText(f, gDatClip.fieldValue) + L")" : std::wstring()), canPaste);
    MenuItem sep;
    sep.kind = MenuItem::Kind::Separator;
    m.push_back(std::move(sep));
    item(3, L"&Reset to the game's value", f.covers(u) && t->changed(f, u));
    return m;
}
void dataFieldMenuPick(const DatField& f, int pick) {
    DatTable* t = curTable();
    if (!t) return;
    const int u = dataState().entry;
    if (pick == 1) {
        gDatClip.field = f.index, gDatClip.fieldDat = t->schema().name, gDatClip.fieldValue = t->value(f, u);
        setMessage(L"Copied " + datLabel(f.name, false) + L": " + fieldValueText(f, gDatClip.fieldValue));
    } else if (pick == 2) {
        t->set(f, u, gDatClip.fieldValue);
        if (dataSaved()) setMessage(L"Pasted " + datLabel(f.name, false));
    } else if (pick == 3) {
        t->reset(f, u);
        if (dataSaved()) setMessage(L"Reset " + datLabel(f.name, false) + L" to the game's value");
    }
    dataPanesChanged();
}

// --- The entries' pictures ----------------------------------------------------------------------------------------------
// What picture an entry has; each program makes it from the game's art (the Windows program's unitPaletteBitmap, the
// SDL program's paletteBitmap).
struct DataPicture {
    enum Kind { None, Unit, IconFrame } kind = None;
    int value = 0;  // the unit, or the command icon's frame
};
// An entry's picture: units their command icon (or sprite); weapons, upgrades and tech their Icon field; orders their
// Highlight icon. The rest have none.
DataPicture dataEntryPicture(int tab, int e) {
    const DatTable* t = datTable(tab);
    if (!t) return {};
    const std::string& dat = t->schema().name;
    if (dat == "units") return {DataPicture::Unit, e};
    const char* field = dat == "orders" ? "Highlight" : (dat == "weapons" || dat == "upgrades" || dat == "techdata") ? "Icon" : nullptr;
    if (!field) return {};
    for (const auto& f : t->schema().fields)
        if (f.name == field && f.covers(e)) {
            const uint32_t frame = t->value(f, e);
            return frame < 0xFFFF ? DataPicture{DataPicture::IconFrame, int(frame)} : DataPicture{};
        }
    return {};
}
// The picture of entry `v` of a .dat ("icons": a command icon frame).
DataPicture dataRefPicture(const std::string& dat, uint32_t v) {
    if (dat == "units") return v < 228 ? DataPicture{DataPicture::Unit, int(v)} : DataPicture{};
    if (dat == "icons") return v < 0xFFFF ? DataPicture{DataPicture::IconFrame, int(v)} : DataPicture{};
    if (dat == "weapons" || dat == "upgrades" || dat == "techdata" || dat == "orders") return dataEntryPicture(datTabOf(dat), int(v));
    return {};
}
// A value's picture by its kind (a button's or a requirement's value: a .dat, "icons" or "buttons", a set: its unit's).
DataPicture dataArgPicture(const char* kind, int v) {
    if (!kind) return {};
    const std::string k = kind;
    if (k == "buttons") return v >= 0 && v < 228 ? DataPicture{DataPicture::Unit, v} : DataPicture{};
    return datHasIcons(k) ? dataRefPicture(k, uint32_t(v)) : DataPicture{};
}
void dataGoTo(const std::string& dat, int e);  // (the program's: the entry in its tab)
// The open button of a value's pick box: goes to entry `v` of `kind` (a .dat, "buttons" or "strings"); null: none.
std::function<void()> openEntry(const char* kind, int v) {
    if (!kind) return nullptr;
    const std::string k = kind;
    return [k, v] { dataGoTo(k, v); };
}
// A list row's picture: the Strings tab has none; a button set has its unit's.
DataPicture dataTabEntryPicture(int tab, int e) {
    if (tab == stringsTab()) return {};
    if (tab == buttonsTab()) return e < 228 ? dataEntryPicture(datTabOf("units"), e) : DataPicture{};
    return dataEntryPicture(tab, e);
}
