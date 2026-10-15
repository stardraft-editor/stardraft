// The Data workspace's model (portable, core.inc): the open tab and entry, the .dat tables with the project's edits, the
// extras (stat_txt.tbl's strings, the button sets, the requirements, the units' display) and their files, what each tab
// lists, the fields' notes, the dropdowns' values, the references to an entry, and the saves and undo steps of a value's
// change. Each program keeps one of each (dataState, dataExtras): the Windows program's are gData and gExtras (main.cpp,
// data_extras.inc), the SDL program's are in sdl_data.inc. The windows, the pictures and the menus stay the programs'.

// The workspace's state (the Windows program's DataView adds its windows).
struct DataModel {
    int datTab = 0;  // index into DatSchema::all(); units is 0
    std::vector<std::unique_ptr<DatTable>> tables;  // one per DatSchema::all(), in that order
    std::vector<int> tabEntry;                      // the entry each tab last had open
    const EudProject* project = nullptr;
    int entry = 0;
    std::vector<int> shown;  // entries in the list
    int listScroll = 0, formScroll = 0;  // (the form's height, click areas and section places are its view's)
    int edits = 0;      // goes up with each change (the references follow the values)
    int editing = -1;   // field whose value box holds the number entry
    DataEditHistory history;  // undo: each saved change is a step (core/data_edits.inc)
};

// The tables that are not .dat files: the strings, the button sets, the requirements and the units' display, each the
// game's with the project's edits (the Windows program's DataExtras adds the Strings tab's edit box).
struct DataExtrasModel {
    StatTxt strings;
    bool stringsLoaded = false;
    Requirements reqs;
    ButtonSets buttons;
    UnitLooks looks;
    std::string saved[4];   // the edits text as last saved: strings, buttons, requirements, unit display
    bool loadFailed[4] = {};  // the project's file did not load: it is not saved over or removed
    int button = 0;         // the selected button of the open set
    int textFor = -1;       // the string the edit box shows
    bool textDirty = false;
    bool rawText = false;   // the edit box shows the whole string: a button's hotkey and type characters too
};
enum { kExtraStrings, kExtraButtons, kExtraReqs, kExtraLooks, kExtraKinds };

// The program's state (the Windows program's gData and gExtras).
DataModel& dataState();
DataExtrasModel& dataExtras();
bool extrasLoadFailed();

DatTable* datTable(int tab) { return tab >= 0 && tab < int(dataState().tables.size()) ? dataState().tables[size_t(tab)].get() : nullptr; }

DatTable* curTable() { return datTable(dataState().datTab); }

int datTabOf(const std::string& dat) {
    const auto& all = DatSchema::all();
    if (dat == "strings") return int(all.size());
    if (dat == "buttons") return int(all.size()) + 1;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].name == dat) return int(i);
    return -1;
}

const DatField* curField(int index) {
    const DatTable* t = curTable();
    if (!t) return nullptr;
    for (const auto& f : t->schema().fields)
        if (f.index == index) return &f;
    return nullptr;
}

int datTabCount() { return int(DatSchema::all().size()); }
int stringsTab() { return datTabCount(); }
int buttonsTab() { return datTabCount() + 1; }
int dataTabCount() { return datTabCount() + 2; }
bool extraTab(int tab) { return tab >= datTabCount(); }

fs::path extrasFile(const EudProject& pr, int k) {
    static const char* names[] = {"strings.json", "buttons.json", "requirements.json", "unit_display.json"};
    return pr.folder() / "dat" / names[k];
}
std::string extrasText(int k) {
    return k == kExtraStrings ? dataExtras().strings.editsText()
           : k == kExtraButtons ? dataExtras().buttons.editsText()
           : k == kExtraLooks   ? dataExtras().looks.editsText()
                                : dataExtras().reqs.editsText();
}
bool setExtrasText(int k, const std::string& text, std::string& err) {
    return k == kExtraStrings ? dataExtras().strings.setEditsText(text, err)
           : k == kExtraButtons ? dataExtras().buttons.setEditsText(text, err)
           : k == kExtraLooks   ? dataExtras().looks.setEditsText(text, err)
                                : dataExtras().reqs.setEditsText(text, err);
}
bool extrasSaveFile(int k, const fs::path& f, std::string& err) {
    if (dataExtras().loadFailed[k]) return err = f.string() + " did not load: not saved, so nothing in it is lost", false;
    return k == kExtraStrings ? dataExtras().strings.saveEdits(f, err)
           : k == kExtraButtons ? dataExtras().buttons.saveEdits(f, err)
           : k == kExtraLooks   ? dataExtras().looks.saveEdits(f, err)
                                : dataExtras().reqs.saveEdits(f, err);
}

// The project's strings, button sets and requirements: the game's, plus the project's edits.
void extrasLoad(const EudProject* pr) {
    std::string err;
    if (!dataExtras().stringsLoaded) dataExtras().stringsLoaded = dataExtras().strings.load(shellState().data, err);
    dataExtras().strings.setEdits({});
    dataExtras().reqs = Requirements();
    dataExtras().buttons = ButtonSets();
    dataExtras().looks = UnitLooks();
    dataExtras().button = 0, dataExtras().textFor = -1;
    for (int k = 0; k < kExtraKinds; ++k) {
        dataExtras().saved[k].clear();
        dataExtras().loadFailed[k] = false;
        if (!pr) continue;
        const fs::path f = extrasFile(*pr, k);
        const bool ok = k == kExtraStrings ? dataExtras().strings.loadEdits(f, err)
                        : k == kExtraButtons ? dataExtras().buttons.loadEdits(f, err)
                        : k == kExtraLooks   ? dataExtras().looks.loadEdits(f, err)
                                             : dataExtras().reqs.loadEdits(f, err);
        dataExtras().loadFailed[k] = !ok;
        if (!ok) shellErrorBox(widen(err), L"Could not read " + f.filename().wstring()), err.clear();
        dataExtras().saved[k] = extrasText(k);
    }
}

// The stat_txt.tbl string (1-based) that names an entry in the game: a unit's (its id + 1), or the Label of a weapon,
// upgrade, technology or order; 0 for none (the other .dat files have no name in the game).
int datValue(const std::string& dat, const std::string& name, int e, int fallback = 0);
int entryNameString(const std::string& dat, int e) {
    if (dat == "units") return e >= 0 && e < 228 ? e + 1 : 0;
    if (dat == "weapons" || dat == "upgrades" || dat == "techdata" || dat == "orders") return datValue(dat, "Label", e, 0);
    return 0;
}

// Name of entry `v` of the .dat a reference points into: "None" past the last entry.
std::wstring refName(const std::string& dat, uint32_t v) {
    const DatSchema* sc = DatSchema::find(dat);
    if (sc && v >= uint32_t(sc->entries)) return L"None";
    return entryName(dat, int(v));
}

bool datHasIcons(const std::string& dat) { return dat == "units" || dat == "icons" || dat == "weapons" || dat == "upgrades" || dat == "techdata" || dat == "orders"; }

// The game's tables, each with the project's edits on top (dat/<name>.dat). A game table that does not load is null;
// err gets the first such error. No message box: this can run from a paint.
std::vector<std::unique_ptr<DatTable>> loadDatTables(const EudProject& pr, std::string& err) {
    std::vector<std::unique_ptr<DatTable>> out;
    std::string first;
    for (const auto& sc : DatSchema::all()) {
        auto t = std::make_unique<DatTable>();
        if (!t->load(shellState().data, sc, err)) {
            shellLogError(widen(err));
            setMessage(widen(err));
            if (first.empty()) first = err;
            t.reset();
        } else if (!t->loadEdits(pr.datFile(sc.name), err)) {
            shellErrorBox(widen(err), L"Could not read the project's " + widen(sc.name) + L".dat");
            err.clear();
        }
        out.push_back(std::move(t));
    }
    err = first;
    return out;
}

// Loads (or reloads, for another project) the tables: the game's .dat files plus the project's edits.
// A project file of the open tables or extras did not load: nothing is saved over it. Build reads the files again
// (writeDatEdits), and when they load then, the workspace loads them too.
bool dataLoadFailed() {
    for (const auto& t : dataState().tables)
        if (t && t->editsFailed()) return true;
    return extrasLoadFailed();
}

void dataLoadForProject() {
    const EudProject* pr = activeProject();
    if (pr == dataState().project && (!dataState().tables.empty() || !pr)) return;
    dataState().entry = 0, dataState().listScroll = dataState().formScroll = 0;
    dataState().project = pr;
    dataState().tables.clear();
    dataState().tabEntry.assign(size_t(dataTabCount()), 0);
    extrasLoad(pr);
    if (!pr) return;
    std::string err;
    dataState().tables = loadDatTables(*pr, err);
    dataState().history.reset(dataState().tables);
    ++dataState().edits;  // other tables: the caches keyed on the edit count (sprites, pictures, references) read them again
}

std::wstring tabTitle(int tab) {
    if (tab == stringsTab()) return L"Strings";
    if (tab == buttonsTab()) return L"Buttons";
    return tab >= 0 && tab < datTabCount() ? datTitle(DatSchema::all()[size_t(tab)].name) : L"";
}
// A tab's icon (the Data group of the UI icons).
UiIcon datIcon(const std::string& dat) {
    static const std::map<std::string, UiIcon> icons = {
        {"units", UiIcon::DatUnits},     {"weapons", UiIcon::DatWeapons}, {"flingy", UiIcon::DatFlingy},   {"sprites", UiIcon::DatSprites},
        {"images", UiIcon::DatImages},   {"upgrades", UiIcon::DatUpgrades}, {"techdata", UiIcon::DatTech}, {"orders", UiIcon::DatOrders},
        {"sfxdata", UiIcon::DatSfx},     {"portdata", UiIcon::DatPortraits}, {"strings", UiIcon::DatStrings}, {"buttons", UiIcon::DatButtons}};
    const auto it = icons.find(dat);
    return it != icons.end() ? it->second : UiIcon::Data;
}
UiIcon tabIcon(int tab) {
    if (tab == stringsTab()) return UiIcon::DatStrings;
    if (tab == buttonsTab()) return UiIcon::DatButtons;
    return tab >= 0 && tab < datTabCount() ? datIcon(DatSchema::all()[size_t(tab)].name) : UiIcon::Data;
}
// Entries of a tab. Strings count from 1, as the game's references to them do (0 is none).
int tabEntries(int tab) {
    if (tab == stringsTab()) return dataExtras().strings.count() + 1;
    if (tab == buttonsTab()) return ButtonSets::kSets;
    return tab >= 0 && tab < datTabCount() ? DatSchema::all()[size_t(tab)].entries : 0;
}
int tabFirstEntry(int tab) { return tab == stringsTab() ? 1 : 0; }
int tabChangedCount(int tab) {
    if (tab == stringsTab()) return dataExtras().strings.changedCount();
    if (tab == buttonsTab()) return dataExtras().buttons.changedCount();
    const DatTable* t = datTable(tab);
    return t ? t->changedCount() : 0;
}

// The string's text for a list, the button prefix and the control bytes left out (core views_data_strings.inc).
std::wstring statString(int id) {  // stat_txt.tbl string `id` (1-based), as the project has it
    return id >= 1 && id <= dataExtras().strings.count() ? stringLine(dataExtras().strings.text(id - 1)) : L"";
}
std::wstring projectUnitName(int u) {  // a unit's name string (u + 1) when the project changed it, else ""
    return u >= 0 && u < dataExtras().strings.count() && dataExtras().strings.changed(u) ? statString(u + 1) : L"";
}

// A set's name (views_data_buttons.inc: the units' sets, the others by the button that opens them).
std::wstring buttonSetName(int s) { return buttonSetNameFrom(dataExtras().buttons, s, statString); }

// The entries of a .dat (or "buttons", "icons", "strings") for a list picker (views_data_buttons.inc: dataEntryItems).
std::vector<ListPickItem> entryItems(const std::string& kind, bool none = false) {
    return dataEntryItems(kind, none, DataPickNames{buttonSetName, statString, dataExtras().strings.count(), datHasIcons});
}
// A step's or a button's value as its field shows it.
std::wstring argName(const char* kind, int v) { return dataArgName(kind, v, buttonSetName); }

bool tabEntryChanged(int tab, int e) {
    if (tab == stringsTab()) return e >= 1 && dataExtras().strings.changed(e - 1);
    if (tab == buttonsTab()) return e >= 0 && e < ButtonSets::kSets && dataExtras().buttons.changed(e);
    const DatTable* t = datTable(tab);
    bool req = false;
    if (t) {
        const std::string& dat = t->schema().name;
        for (int k = 0; k < Requirements::kKinds; ++k)
            if (Requirements::kind(k).dat == dat && e < Requirements::kind(k).entries) req |= dataExtras().reqs.changed(k, e);
    }
    return (t && t->entryChanged(e)) || req;
}
std::wstring tabEntryName(int tab, int e) {
    if (tab == stringsTab()) return statString(e);
    if (tab == buttonsTab()) return buttonSetName(e);
    const DatTable* t = datTable(tab);
    return t ? entryName(t->schema().name, e) : L"";
}

bool extrasLoadFailed() {
    for (int k = 0; k < kExtraKinds; ++k)
        if (dataExtras().loadFailed[k]) return true;
    return false;
}

// Saves and records an undo step for whatever changed (the .dat tables do theirs in dataSaved). False when a save
// failed (its error is the status message).
bool extrasSaved() {
    bool ok = true;
    std::string err;
    for (int k = 0; k < kExtraKinds; ++k) {
        const std::string now = extrasText(k);
        if (now == dataExtras().saved[k]) continue;
        const int tab = k == kExtraStrings ? stringsTab() : k == kExtraButtons ? buttonsTab() : dataState().datTab;
        dataState().history.recordExtra(tab, dataState().entry, k + 1, Bytes(dataExtras().saved[k].begin(), dataExtras().saved[k].end()), Bytes(now.begin(), now.end()));
        dataExtras().saved[k] = now;
        if (dataState().project) {
            const fs::path f = extrasFile(*dataState().project, k);
            if (!extrasSaveFile(k, f, err)) setMessage(widen(err)), ok = false;
        }
    }
    return ok;
}

// units.dat Rank/Sublabel: the title under the unit's name in the status panel, from stat_txt.tbl ("None" for 0).
std::wstring rankName(uint32_t v) {
    if (!v) return L"None";
    std::wstring t = shellState().ed && v < 256 ? statString(UnitLooks::kRankString + int(v)) : L"";
    if (const size_t nl = t.find_first_of(L"\r\n"); nl != std::wstring::npos) t.resize(nl);
    return (t.empty() ? L"(no title)" : t) + (v < uint32_t(UnitLooks::kRanks) ? L"" : L"   (not a title)");
}

// What follows the value: the name of a choice or of the entry a reference points at, or a note.
std::wstring fieldNote(const DatField& f, uint32_t v) {
    if (!f.valueNames.empty()) return v < f.valueNames.size() ? f.valueNames[v].empty() ? L"" : widen(f.valueNames[v]) : L"(unnamed)";
    if (!f.ref.empty()) {
        const std::wstring n = refName(f.ref, v);
        return n.empty() ? widen(f.ref) + L" #" + std::to_wstring(v) : n;
    }
    if (f.name == "Iscript ID") {
        const auto& names = DatSchema::entryNames("iscript");
        return v < names.size() ? widen(names[v]) : L"";
    }
    if (f.name == "Rank/Sublabel") return rankName(v);
    if (f.name == "Icon" || f.name == "Highlight") {
        if (v == 0xFFFF) return L"None";
        const auto& icons = DatSchema::entryNames("icons");
        return v < icons.size() ? L"icon: " + widen(icons[v]) : L"";
    }
    if (f.name == "Label" && shellState().ed) return v ? L"\x201C" + statString(int(v)) + L"\x201D" : L"None";
    if (f.name == "Hit Points") {
        wchar_t b[48];
        std::swprintf(b, 48, L"= %g hit points", v / 256.0);
        return b;
    }
    if (f.name == "Shield Amount" || f.name == "Mineral Cost" || f.name == "Vespene Cost") return L"";
    if (f.name == "Build Time" || f.name == "Weapon Cooldown" || f.name == "Remove After" || f.name == "Research Time Base" ||
        f.name == "Research Time Factor" || f.name == "Resarch Time") {
        wchar_t b[48];
        std::swprintf(b, 48, L"= %g s (fastest)", v / 24.0);
        return b;
    }
    return L"";
}

// A field a map cannot change, and why ("" when it can). From the offset table of the EUD book
// (armoha.github.io/eud-book/offset_table.html): the fields marked "Read Only" there; Sound File of a sound the game
// loads before the map's code runs (Preload); and fields with no address in memory.
std::wstring datFieldReadOnly(const std::string& dat, const DatField& f, int entry) {
    static const std::set<std::pair<std::string, std::string>> readOnly = {
        {"sprites", "Health Bar"},     {"sprites", "Sel.Circle Image"}, {"sprites", "Sel.Circle Offset"}, {"images", "GRP File"},
        {"images", "Shield Overlay"},  {"images", "Attack Overlay"},    {"images", "Damage Overlay"},    {"images", "Special Overlay"},
        {"images", "Landing Dust Overlay"}, {"images", "Lift-Off Overlay"}};
    if (readOnly.count({dat, f.name})) return L"Read only. A change to this value has no effect in StarCraft: Remastered.";
    if (const DatTable* t = datTable(datTabOf(dat)); t && t->pointerField(f) && t->soundPreloaded(entry))
        return L"Read only for this sound. The game loads sounds with the Preload flag before the map's code runs, so a "
               L"change to the file has no effect.";
    if (!f.memory) return L"Read only. StarDraft 2 does not know where the game keeps this value.";
    return L"";
}

// The game's graphics as the project's edits make them, for the Data workspace and its pictures (the map keeps its
// own copy): set again from sprites.dat and images.dat after each change.
UnitSprites& dataSprites() {
    static std::unique_ptr<UnitSprites> own;
    static int appliedAt = -1;
    if (!own) {
        own = std::make_unique<UnitSprites>();
        std::string err;
        own->load(shellState().data, err);
        appliedAt = -1;
    }
    if (appliedAt != dataState().edits) {
        const DatTable* sprites = datTable(datTabOf("sprites"));
        const DatTable* images = datTable(datTabOf("images"));
        if (sprites && images) own->applyEdits(sprites->bytes(), images->bytes());
        appliedAt = dataState().edits;
    }
    return *own;
}

const DatField* datFieldNamed(const std::string& dat, const std::string& name) {
    const DatTable* t = datTable(datTabOf(dat));
    if (!t) return nullptr;
    for (const auto& f : t->schema().fields)
        if (f.name == name) return &f;
    return nullptr;
}
// A value shown other than it is: what a trigger block would make it (its pop-out shows the result).
struct {
    std::string dat, field;
    int entry = -1;
    long long value = 0;
} gDatOverride;

// A value of another entry (any .dat), or `fallback` if the field does not cover it.
int datValue(const std::string& dat, const std::string& name, int e, int fallback) {
    if (gDatOverride.entry == e && gDatOverride.entry >= 0 && gDatOverride.field == name && gDatOverride.dat == dat) return int(gDatOverride.value);
    const DatTable* t = datTable(datTabOf(dat));
    const DatField* f = datFieldNamed(dat, name);
    return t && f && f->covers(e) ? int(t->value(*f, e)) : fallback;
}

// The values a dropdown offers, in its order.
std::vector<ListPickItem> dataDropdownItems(const DatField& f) {
    std::vector<ListPickItem> items;
    const std::string dat = fieldRefDat(f);
    if (!f.valueNames.empty()) {
        for (size_t i = 0; i < f.valueNames.size(); ++i) items.push_back({widen(f.valueNames[i].empty() ? "(unnamed)" : f.valueNames[i]), L"", std::to_string(i)});
    } else if (dat == "iscript") {  // the scripts iscript.bin has, by their names
        const auto& names = DatSchema::entryNames("iscript");
        for (uint32_t id : dataSprites().scriptIds())
            items.push_back({(id < names.size() ? widen(names[id]) : L"(unnamed)") + L"   #" + std::to_wstring(id), L"", std::to_string(id)});
    } else if (dat == "ranks") {  // the game's titles (stat_txt.tbl)
        for (int v = 0; v < UnitLooks::kRanks; ++v) items.push_back({rankName(uint32_t(v)) + L"   #" + std::to_wstring(v), L"", std::to_string(v)});
    } else if (dat == "icons") {
        const auto& names = DatSchema::entryNames("icons");
        items.push_back({L"None", L"", std::to_string(0xFFFF)});
        for (size_t i = 0; i < names.size(); ++i) items.push_back({widen(names[i]) + L"   #" + std::to_wstring(i), L"", std::to_string(i), "icons", int(i)});
    } else {
        const DatSchema* sc = DatSchema::find(dat);
        const int n = sc ? sc->entries : 0;
        const bool icons = datHasIcons(dat);
        if (f.maxValue() >= uint32_t(n)) items.push_back({L"None", L"", std::to_string(n)});
        for (int i = 0; i < n; ++i) {
            const std::wstring name = entryName(dat, i);
            items.push_back({(name.empty() ? L"(unnamed)" : name) + L"   #" + std::to_wstring(i), L"", std::to_string(i), icons ? dat : "", icons ? i : -1});
        }
    }
    return items;
}

// References: every entry of any .dat whose fields point at the open one, grouped by its .dat. Click a name to
// go there.
struct DataRef {
    std::string dat;
    int entry;
    std::vector<std::string> via;
};
const std::vector<DataRef>& dataRefs(const std::string& dat, int e) {
    static std::vector<DataRef> refs;
    static std::tuple<std::string, int, int> key{"", -1, -1};
    if (key == std::make_tuple(dat, e, dataState().edits)) return refs;
    key = {dat, e, dataState().edits};
    refs.clear();
    for (const auto& sc : DatSchema::all()) {
        const DatTable* t = datTable(datTabOf(sc.name));
        if (!t) continue;
        for (int i = 0; i < sc.entries; ++i)
            for (const auto& f : t->schema().fields) {
                // The selection circle is a number from image 561 on, not an image: it points nowhere here.
                if (f.ref != dat || !f.covers(i) || f.name == "Sel.Circle Image" || int(t->value(f, i)) != e) continue;
                if (sc.name == dat && i == e) continue;
                if (refs.empty() || refs.back().dat != sc.name || refs.back().entry != i) refs.push_back({sc.name, i, {}});
                refs.back().via.push_back(f.name);
            }
    }
    auto add = [&](const std::string& d, int i, const std::string& via) {
        for (auto& r : refs)
            if (r.dat == d && r.entry == i) return r.via.push_back(via);
        refs.push_back({d, i, {via}});
    };
    if (dat == "strings") {  // a string: the unit it names, the fields that show it, the buttons that show it
        if (e >= 1 && e <= 228) add("units", e - 1, "Name");
        for (const auto& sc : DatSchema::all()) {
            const DatTable* t = datTable(datTabOf(sc.name));
            if (!t) continue;
            for (const auto& f : t->schema().fields)
                if (f.name == "Label" || f.name == "Target Error Message")
                    for (int i = f.first; i <= f.last; ++i)
                        if (int(t->value(f, i)) == e) add(sc.name, i, f.name);
        }
        for (int b = 0; b < ButtonSets::kSets; ++b)
            for (const Button& btn : dataExtras().buttons.set(b))
                if (btn.enabledString == e || btn.disabledString == e) add("buttons", b, "Text");
    } else {
        // The requirement lists that name this entry, and the buttons whose condition or action does.
        for (int k = 0; k < Requirements::kKinds; ++k)
            for (int i = 0; i < Requirements::kind(k).entries; ++i)
                for (const RequirementOp& r : dataExtras().reqs.list(k, i).ops) {
                    const char* kind = requirementOpArgKind(r);
                    if (kind && kind == dat && requirementOpValue(r) == e) {
                        add(Requirements::kind(k).dat, i, "Requirements");
                        break;
                    }
                }
        for (int b = 0; b < ButtonSets::kSets; ++b)
            for (const Button& btn : dataExtras().buttons.set(b)) {
                const ButtonFunctionDef* c = ButtonSets::condition(btn.condition);
                const ButtonFunctionDef* a = ButtonSets::action(btn.action);
                if ((c && c->arg && c->arg == dat && btn.conditionArg == e) || (a && a->arg && a->arg == dat && btn.actionArg == e)) {
                    if (!(dat == "buttons" && b == e)) add("buttons", b, "Button");
                    break;
                }
            }
    }
    return refs;
}

// The Data list's model host (core/data_list.inc): the tables, the entries' names and the changed flags.
class DataListProgram final : public DataListHost {
public:
    bool hasTable(int tab) override { return datTable(tab) != nullptr || extraTab(tab); }
    int entries(int tab) override { return tabEntries(tab); }
    int firstEntry(int tab) override { return tabFirstEntry(tab); }
    bool changed(int tab, int e) override { return tabEntryChanged(tab, e); }
    std::wstring name(int tab, int e) override { return tabEntryName(tab, e); }
};
DataListProgram gDataListProgram;

// A value's change is saved: the edit count goes up (the references and the pictures follow it), the extras' and the
// open table's files are written, and each change is an undo step. False when a save failed (its error is the status
// message).
bool dataSaveChange() {
    DataModel& d = dataState();
    ++d.edits;
    bool ok = extrasSaved();
    std::string err;
    if (const DatTable* t = curTable(); t && d.project && !t->saveEdits(d.project->datFile(t->schema().name), err)) setMessage(widen(err)), ok = false;
    if (const DatTable* t = curTable()) d.history.recordTable(d.datTab, d.entry, t->bytes());
    return ok;
}

// Undo (redo: redo) in the Data workspace: the step's table, or its extra's text, as it was, saved to the project's file.
// False when there is no step (or its table is not open). `s` is the step; `err` a save's error (empty: saved).
bool dataApplyUndo(bool redo, DataEditStep& s, std::string& err) {
    DataModel& d = dataState();
    DataExtrasModel& x = dataExtras();
    if (!d.history.take(redo, s)) return false;
    if (s.extra) {
        const int k = s.extra - 1;
        const Bytes& b = redo ? s.after : s.before;
        std::string textErr;
        setExtrasText(k, std::string(b.begin(), b.end()), textErr);
        x.saved[k] = extrasText(k);
        x.textFor = -1;
        if (d.project) extrasSaveFile(k, extrasFile(*d.project, k), err);
        d.history.done(redo, s);
        ++d.edits;
        return true;
    }
    DatTable* t = datTable(s.tab);
    if (!t) return false;
    t->setBytes(redo ? s.after : s.before);
    ++d.edits;
    d.history.tableSaved(s.tab, t->bytes());
    d.history.done(redo, s);
    if (d.project && !t->saveEdits(d.project->datFile(t->schema().name), err) && err.empty()) err = "Could not save " + t->schema().name + ".dat";
    return true;
}

