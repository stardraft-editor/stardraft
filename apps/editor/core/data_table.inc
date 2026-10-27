// The Data workspace's Table view: the model (portable, core.inc, after data_nav.inc). The entries are the rows and the
// fields are the columns: the name, then each field in the form's order. A field of flags is one column (the names of
// its flags that are on), and each of its flags can also show as a column of its own. A cell is a number, a dropdown's
// value (a number to type, its name shown), a yes/no value or a flag (a check box), a set of flags, or the unit's name
// (text). Here: the columns, a cell's text and how a typed text sets it, the selected cells, and the clipboard's and the
// CSV file's text. The view (views_data_table.inc) paints them; the program saves each change as one undo step.

bool dataTableViewOn() { return dataState().table; }

enum class DataCellKind { Name, Number, Pick, Bool, Bit, Flags };

struct DataTableColumn {
    DataCellKind kind = DataCellKind::Number;
    int field = -1;  // the field's index in its schema (-1: the name)
    int bit = -1;    // a flag's bit (Bit only)
};

const DatField* dataSchemaField(const DatSchema& sch, int index) {
    for (const auto& f : sch.fields)
        if (f.index == index) return &f;
    return nullptr;
}

// The columns of a .dat: the name, then the fields as the form shows them (each section in the schema's order, each
// section's fields in their order); a field of flags gives its column, then a column to each flag.
std::vector<DataTableColumn> dataTableColumns(const DatSchema& sch) {
    std::vector<DataTableColumn> out{{DataCellKind::Name, -1, -1}};
    for (int si = 0; si <= int(sch.sections.size()); ++si) {
        std::vector<const DatField*> fields;
        for (const auto& f : sch.fields)
            if (f.section == si) fields.push_back(&f);
        std::sort(fields.begin(), fields.end(), [](const DatField* a, const DatField* b) { return a->order < b->order; });
        for (const DatField* f : fields) {
            if (!f->flags.empty()) {
                out.push_back({DataCellKind::Flags, f->index, -1});
                for (size_t b = 0; b < f->flags.size(); ++b) out.push_back({DataCellKind::Bit, f->index, int(b)});
            } else {
                const DataCellKind k = fieldIsBool(sch.name, *f) ? DataCellKind::Bool : fieldIsDropdown(*f) ? DataCellKind::Pick : DataCellKind::Number;
                out.push_back({k, f->index, -1});
            }
        }
    }
    return out;
}

// A column's name in its header ("Hit Points", a flag's name), and the name of its group above it (the section; a flag's
// field).
std::wstring dataColumnTitle(const DatSchema& sch, const DataTableColumn& col) {
    if (col.kind == DataCellKind::Name) return sch.name == "strings" ? L"Text" : L"Name";
    const DatField* f = dataSchemaField(sch, col.field);
    if (!f) return L"";
    return col.kind == DataCellKind::Bit ? widen(f->flags[size_t(col.bit)]) : datLabel(f->name);
}
std::wstring dataColumnGroup(const DatSchema& sch, const DataTableColumn& col) {
    if (col.kind == DataCellKind::Name) return L"";
    const DatField* f = dataSchemaField(sch, col.field);
    if (!f) return L"";
    if (col.kind == DataCellKind::Bit) return datLabel(f->name);
    return f->section < int(sch.sections.size()) ? datLabel(sch.sections[size_t(f->section)]) : L"Other";
}
// A name as a column name: lower case, each run of letters and digits joined by "_" ("Hit Points" -> "hit_points").
std::wstring dataSnakeName(const std::string& name) {
    std::wstring out;
    bool gap = false;
    for (const char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            if (gap && !out.empty()) out += L'_';
            out += wchar_t(std::tolower(c)), gap = false;
        } else {
            gap = true;
        }
    }
    return out;
}
// A column's name in a CSV file and in a copy with headers: "name", the field's name in the schema as hit_points, or
// "<field>.<flag>". (The schema's names, not the shown ones: a file reads again in another language, and after a
// column's shown name changes.) The table's layout keeps its columns by these names too.
std::wstring dataColumnKey(const DatSchema& sch, const DataTableColumn& col) {
    if (col.kind == DataCellKind::Name) return sch.name == "strings" ? L"text" : L"name";
    const DatField* f = dataSchemaField(sch, col.field);
    if (!f) return L"";
    return dataSnakeName(f->name) + (col.kind == DataCellKind::Bit ? L"." + dataSnakeName(f->flags[size_t(col.bit)]) : L"");
}
// The bits of a flag field that have a name.
uint32_t dataNamedFlags(const DatField& f) {
    const size_t n = std::min<size_t>(f.flags.size(), 32);
    return n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1;
}
// The names of the flags that are on in `v` ("Building, Flyer"), or "None".
std::wstring dataFlagsText(const DatField& f, uint32_t v) {
    std::wstring out;
    for (size_t b = 0; b < f.flags.size() && b < 32; ++b)
        if ((v >> b) & 1) out += (out.empty() ? L"" : L", ") + widen(f.flags[b]);
    return out.empty() ? L"None" : out;
}

// The Strings tab as a table: a schema with no fields (its one column is the text, the Name kind) and as many entries
// as stat_txt.tbl has strings (they count from 1). The Table view shows it as it shows a .dat.
DatTable& dataStringsTable() {
    static DatSchema schema;
    static DatTable table;
    schema.name = "strings", schema.entries = dataExtras().strings.count() + 1;
    table.setSchemaOnly(schema);
    return table;
}

// The Table view's layout of each .dat, kept in the project (dat/table_view.json): the hidden columns, the flags that
// show as columns of their own, the columns' order and widths, and the sort. A column is its CSV name (dataColumnKey).
// The name column always shows, first.
struct DataTableLayout {
    std::set<std::wstring> hidden;
    std::set<std::wstring> bits;          // the flags that show as columns (a flag's column shows only when it is here)
    std::vector<std::wstring> order;      // the columns in the order they show (empty: the form's order)
    std::map<std::wstring, int> widths;   // a column's width that a drag set, in 96-dpi pixels
    std::wstring sortKey;                 // the column the rows are sorted by (empty: the default order, the list's)
    bool sortDown = false;                // descending
};
struct {
    const EudProject* project = nullptr;
    bool loaded = false;
    std::map<std::string, DataTableLayout> dats;
} gDataTableLayouts;
int gDataTableHiddenRev = 0;  // goes up with each change and each load (the view makes its columns again)

fs::path dataTableLayoutFile() {
    const EudProject* pr = dataState().project;
    return pr ? pr->folder() / "dat" / "table_view.json" : fs::path();
}
// The layout of a .dat (the project's file is read again when the project changes).
DataTableLayout& dataTableLayout(const std::string& dat) {
    auto& g = gDataTableLayouts;
    if (!g.loaded || g.project != dataState().project) {
        g.dats.clear(), g.loaded = true, g.project = dataState().project, ++gDataTableHiddenRev;
        if (const fs::path f = dataTableLayoutFile(); !f.empty()) {
            stardraft::Settings s(f);
            for (const std::string& name : s.names()) {
                const size_t dot = name.rfind('.');
                if (dot == std::string::npos) continue;
                DataTableLayout& l = g.dats[name.substr(0, dot)];
                const std::string what = name.substr(dot + 1);
                if (what == "sortDown") l.sortDown = s.number(name).value_or(0) != 0;
                else if (what == "sort") l.sortKey = widen(s.text(name).value_or(""));
                else if (const auto list = s.list(name))
                    for (const std::string& v : *list) {
                        if (what == "hidden") l.hidden.insert(widen(v));
                        else if (what == "bits") l.bits.insert(widen(v));
                        else if (what == "order") l.order.push_back(widen(v));
                        else if (const size_t eq = v.rfind('='); what == "widths" && eq != std::string::npos)
                            l.widths[widen(v.substr(0, eq))] = std::clamp(std::atoi(v.c_str() + eq + 1), 24, 4000);
                    }
            }
        }
    }
    return g.dats[dat];
}
// Writes the layout of a .dat into the project's file (no project: it stays until the program ends).
void dataTableLayoutSave(const std::string& dat) {
    ++gDataTableHiddenRev;
    const fs::path f = dataTableLayoutFile();
    if (f.empty()) return;
    const DataTableLayout& l = dataTableLayout(dat);
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    stardraft::Settings s(f);
    std::vector<std::string> hidden, bits, order, widths;
    for (const auto& k : l.hidden) hidden.push_back(narrow(k));
    for (const auto& k : l.bits) bits.push_back(narrow(k));
    for (const auto& k : l.order) order.push_back(narrow(k));
    for (const auto& [k, w] : l.widths) widths.push_back(narrow(k) + "=" + std::to_string(w));
    bool ok = true;
    auto put = [&](const std::string& what, bool has, stardraft::Settings::Value v) { ok &= has ? s.set(dat + "." + what, std::move(v)) : (!s.has(dat + "." + what) || s.remove(dat + "." + what)); };
    put("hidden", !hidden.empty(), hidden);
    put("bits", !bits.empty(), bits);
    put("order", !order.empty(), order);
    put("widths", !widths.empty(), widths);
    put("sort", !l.sortKey.empty(), narrow(l.sortKey));
    put("sortDown", l.sortDown, int64_t(1));
    if (!ok) setMessage(L"Could not save the table's layout: " + widen(s.lastError()));
}
std::set<std::wstring>& dataTableHidden(const std::string& dat) { return dataTableLayout(dat).hidden; }
void dataTableSetHidden(const std::string& dat, std::set<std::wstring> keys) {
    keys.erase(L"name"), keys.erase(L"text");
    dataTableHidden(dat) = std::move(keys);
    dataTableLayoutSave(dat);
}
// The column does not show: hidden, or a flag that is not one of the layout's flag columns. (The name always shows.)
bool dataColumnHidden(const DataTableLayout& l, const DatSchema& sch, const DataTableColumn& col) {
    if (col.kind == DataCellKind::Name) return false;
    const std::wstring key = dataColumnKey(sch, col);
    return col.kind == DataCellKind::Bit ? !l.bits.count(key) : l.hidden.count(key) != 0;
}
// Shows or hides a column (the caller saves the layout). A column that starts to show goes to the right end.
void dataColumnSetShown(DataTableLayout& l, const DatSchema& sch, const DataTableColumn& col, bool show) {
    if (col.kind == DataCellKind::Name || dataColumnHidden(l, sch, col) != show) return;
    const std::wstring key = dataColumnKey(sch, col);
    if (col.kind == DataCellKind::Bit) {
        if (show) l.bits.insert(key);
        else l.bits.erase(key);
    } else {
        if (show) l.hidden.erase(key);
        else l.hidden.insert(key);
    }
    if (!show) return;
    if (l.order.empty())  // (the form's order first, so that only this column moves)
        for (const DataTableColumn& c : dataTableColumns(sch)) l.order.push_back(dataColumnKey(sch, c));
    l.order.erase(std::remove(l.order.begin(), l.order.end(), key), l.order.end());
    l.order.push_back(key);
}

// A table of a classic map's settings (the Map data page's Table view: classic_table.inc): its schema's name starts
// with "map ". Its rows' names come from the Map data page.
bool dataIsMapTable(const std::string& dat) { return dat.rfind("map ", 0) == 0; }
std::wstring (*gDataMapRowName)(const std::string& dat, int e) = nullptr;  // (classic_table.inc)
std::wstring (*gDataMapCellLocked)(const std::string& dat, int field, int e) = nullptr;  // why a map table's cell cannot change ("": it can)
// A row's name as the Name column shows it.
std::wstring dataRowName(const std::string& dat, int e) {
    if (dataIsMapTable(dat)) return gDataMapRowName ? gDataMapRowName(dat, e) : L"";
    return tabEntryName(datTabOf(dat), e);
}

// The stat_txt.tbl string (1-based) that a Name cell changes: a unit's name (0: none, the name is not text to change).
// Other .dat files take their names from the game's lists or share their Label strings, so their names stay as they are.
int dataNameString(const std::string& dat, int e) {
    const int sid = dat == "strings" ? e : dat == "units" ? entryNameString(dat, e) : 0;  // (a string: its own text)
    return sid > 0 && sid <= dataExtras().strings.count() ? sid : 0;
}

// Why the cell cannot change ("" when it can): a field the entry does not use, or a field that is read only.
std::wstring dataCellLocked(const DatTable& t, const DataTableColumn& col, int e) {
    const std::string& dat = t.schema().name;
    if (col.kind == DataCellKind::Name)
        return dataNameString(dat, e) ? L"" : dataIsMapTable(dat) ? L"Rename a unit in its form (a double click on its name opens it)."
                                                                  : L"The name comes from the game's list. Change the Label value to change the text that the game shows.";
    const DatField* f = dataSchemaField(t.schema(), col.field);
    if (!f || !f->covers(e)) return L"This entry does not use this value.";
    if (dataIsMapTable(dat)) return gDataMapCellLocked ? gDataMapCellLocked(dat, f->index, e) : L"";  // (a map's settings: not the game's memory)
    return datFieldReadOnly(dat, *f, e);
}

// A cell's value as data: what the clipboard and a CSV file get, and what a typed text is compared with. A number in
// the form's unit (hit points), a dropdown's number, 0 or 1 for a check box, the name. Empty: the entry does not use
// the field.
std::wstring dataCellData(const DatTable& t, const DataTableColumn& col, int e) {
    if (col.kind == DataCellKind::Name) {
        const int sid = dataNameString(t.schema().name, e);
        return sid ? widen(StatTxt::escape(dataExtras().strings.text(sid - 1))) : dataRowName(t.schema().name, e);
    }
    const DatField* f = dataSchemaField(t.schema(), col.field);
    if (!f || !f->covers(e)) return L"";
    const uint32_t v = t.value(*f, e);
    if (col.kind == DataCellKind::Bit) return (v >> col.bit) & 1 ? L"1" : L"0";
    if (col.kind == DataCellKind::Number) return fieldValueText(*f, v);
    return std::to_wstring(v);
}

// The cell differs from the game's.
bool dataCellChanged(const DatTable& t, const DataTableColumn& col, int e) {
    if (col.kind == DataCellKind::Name) {
        const int sid = dataNameString(t.schema().name, e);
        return sid && dataExtras().strings.changed(sid - 1);
    }
    const DatField* f = dataSchemaField(t.schema(), col.field);
    if (!f || !f->covers(e)) return false;
    if (col.kind == DataCellKind::Bit) return ((t.value(*f, e) ^ t.stock(*f, e)) >> col.bit) & 1;
    return t.changed(*f, e);
}

std::wstring dataTrim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n"), b = s.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}

// A text as a yes/no value: 1, 0, yes, no, true, false, x (a mark), on, off. False: it is none of them.
bool dataReadYesNo(const std::wstring& text, bool& yes) {
    const std::wstring t = dataLower(text);
    if (t == L"1" || t == L"yes" || t == L"true" || t == L"x" || t == L"on" || t == L"y") return yes = true, true;
    if (t == L"0" || t == L"no" || t == L"false" || t == L"off" || t == L"n") return yes = false, true;
    return false;
}

// The result of a text put into a cell.
enum class DataCellSet { Changed, Same, NotValue, Locked, Empty };

// Puts `text` into a cell (typed, pasted or read from a CSV file). A number (decimal, or hex with 0x; hit points with
// decimals); a dropdown's number or the name of one of its values; a check box's 1, 0, yes or no; a unit's name.
// Empty text changes nothing. The value is kept inside what the field can hold. The caller saves the change.
DataCellSet dataCellSet(DatTable& t, const DataTableColumn& col, int e, const std::wstring& typed) {
    const std::wstring text = col.kind == DataCellKind::Name ? typed : dataTrim(typed);
    if (text.empty()) return DataCellSet::Empty;
    if (!dataCellLocked(t, col, e).empty()) return DataCellSet::Locked;
    if (col.kind == DataCellKind::Name) {
        const int sid = dataNameString(t.schema().name, e);
        const std::string now = dataExtras().strings.text(sid - 1), next = StatTxt::unescape(narrow(text));
        if (next == now) return DataCellSet::Same;
        dataExtras().strings.set(sid - 1, next);
        return DataCellSet::Changed;
    }
    const DatField& f = *dataSchemaField(t.schema(), col.field);
    const uint32_t old = t.value(f, e);
    uint32_t v = old;
    if (col.kind == DataCellKind::Flags) {
        // A set of flags: its number (decimal, or hex with 0x), or the names of the flags that are on ("Building, Flyer"
        // or "Building|Flyer"; "None": no flag), as the cell shows them. (Names: the bits without a name stay as they are.)
        if (dataReadEntry(text, f.maxValue(), v) != DataEntryRead::Number) {
            v = old & ~dataNamedFlags(f);
            if (dataLower(text) != L"none") {
                size_t at = 0;
                while (at <= text.size()) {
                    const size_t end = text.find_first_of(L",|", at);
                    const std::wstring part = dataLower(dataTrim(text.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at)));
                    bool found = part.empty();
                    for (size_t b = 0; b < f.flags.size() && b < 32 && !found; ++b)
                        if (dataLower(widen(f.flags[b])) == part) v |= 1u << b, found = true;
                    if (!found) return DataCellSet::NotValue;
                    if (end == std::wstring::npos) break;
                    at = end + 1;
                }
            }
        }
    } else if (col.kind == DataCellKind::Bit || col.kind == DataCellKind::Bool) {
        bool yes = false;
        if (dataReadYesNo(text, yes)) v = col.kind == DataCellKind::Bit ? (old & ~(1u << col.bit)) | (uint32_t(yes) << col.bit) : uint32_t(yes);
        else if (col.kind != DataCellKind::Bool || dataReadEntry(text, f.maxValue(), v) != DataEntryRead::Number) return DataCellSet::NotValue;  // (a yes/no byte holds any number)
    } else if (dataReadEntry(text, f.maxValue(), v, fieldShownScale(f)) != DataEntryRead::Number) {
        if (col.kind != DataCellKind::Pick) return DataCellSet::NotValue;
        // A dropdown: the name of one of its values ("Terran Marine", without the "   #0" the list adds).
        bool found = false;
        const std::wstring want = dataLower(text);
        for (const ListPickItem& it : dataDropdownItems(f)) {
            std::wstring label = it.label;
            if (const size_t hash = label.rfind(L"   #"); hash != std::wstring::npos) label.resize(hash);
            if (dataLower(label) == want) {
                v = uint32_t(std::stoul(it.value)), found = true;
                break;
            }
        }
        if (!found) return DataCellSet::NotValue;
    }
    if (v == old) return DataCellSet::Same;
    dataApplyValue(t, f, e, v);
    return t.value(f, e) == old ? DataCellSet::Same : DataCellSet::Changed;
}

// A step for the selected numbers, as it is typed: "+10", "-10", "*1.5" or "/2". True with its operator and its number.
bool dataReadStep(const std::wstring& typed, wchar_t& op, double& n) {
    const std::wstring text = dataTrim(typed);
    if (text.size() < 2 || !std::wcschr(L"+-*/", text[0])) return false;
    const std::wstring rest = dataTrim(text.substr(1));
    wchar_t* end = nullptr;
    n = std::wcstod(rest.c_str(), &end);
    if (rest.empty() || !end || *end != 0 || !std::isfinite(n) || n < 0 || (text[0] == L'/' && n == 0)) return false;
    op = text[0];
    return true;
}

// A number cell changed by a step: its value (in the unit that the form shows, as hit points) plus, minus, times or
// divided by n, rounded, from 0 to the field's highest value. A cell of another kind: NotValue. The caller saves.
DataCellSet dataCellStep(DatTable& t, const DataTableColumn& col, int e, wchar_t op, double n) {
    if (!dataCellLocked(t, col, e).empty()) return DataCellSet::Locked;
    if (col.kind != DataCellKind::Number) return DataCellSet::NotValue;
    const DatField& f = *dataSchemaField(t.schema(), col.field);
    const uint32_t old = t.value(f, e), k = std::max<uint32_t>(1, fieldShownScale(f));
    const double now = double(old) / k;
    const double next = op == L'+' ? now + n : op == L'-' ? now - n : op == L'*' ? now * n : now / n;
    const uint32_t v = stardraft::datRawFromShown(next, k, f.maxValue());
    if (v == old) return DataCellSet::Same;
    dataApplyValue(t, f, e, v);
    return t.value(f, e) == old ? DataCellSet::Same : DataCellSet::Changed;
}

// A column has a value that is not the game's, in any entry.
bool dataColumnChanged(const DatTable& t, const DataTableColumn& col) {
    for (int e = 0; e < int(t.schema().entries); ++e)
        if (dataCellChanged(t, col, e)) return true;
    return false;
}

// Puts the game's value back into a cell (Delete). The caller saves the change.
DataCellSet dataCellReset(DatTable& t, const DataTableColumn& col, int e) {
    if (!dataCellLocked(t, col, e).empty()) return DataCellSet::Locked;
    if (col.kind == DataCellKind::Name) {
        const int sid = dataNameString(t.schema().name, e);
        if (!dataExtras().strings.changed(sid - 1)) return DataCellSet::Same;
        dataExtras().strings.reset(sid - 1);
        return DataCellSet::Changed;
    }
    const DatField& f = *dataSchemaField(t.schema(), col.field);
    const uint32_t old = t.value(f, e), stock = t.stock(f, e);
    const uint32_t v = col.kind == DataCellKind::Bit ? (old & ~(1u << col.bit)) | (stock & (1u << col.bit)) : stock;
    if (v == old) return DataCellSet::Same;
    t.set(f, e, v);
    return DataCellSet::Changed;
}

// --- The selected cells ------------------------------------------------------------------------------------------------
// Rectangles of cells (rows and columns of the table as it shows them). A click selects one cell, Shift+click the
// rectangle from the anchor, Ctrl+click adds a rectangle; a column's header selects the column, a row's number the row.
// The active cell is where typing goes and where the arrow keys move from.

struct DataCellRange {
    int r0 = 0, c0 = 0, r1 = 0, c1 = 0;  // two corners (any order)
    int top() const { return std::min(r0, r1); }
    int bottom() const { return std::max(r0, r1); }
    int left() const { return std::min(c0, c1); }
    int right() const { return std::max(c0, c1); }
    bool has(int r, int c) const { return r >= top() && r <= bottom() && c >= left() && c <= right(); }
};

struct DataTableSelection {
    std::vector<DataCellRange> ranges{DataCellRange{}};
    int row = 0, col = 0;             // the active cell
    int anchorRow = 0, anchorCol = 0;  // where Shift selects from

    bool has(int r, int c) const {
        for (const auto& g : ranges)
            if (g.has(r, c)) return true;
        return false;
    }
    bool single() const { return ranges.size() == 1 && ranges[0].r0 == ranges[0].r1 && ranges[0].c0 == ranges[0].c1; }
    // A click on a cell (mods: draw::Mods).
    void click(int r, int c, unsigned mods) {
        if ((mods & draw::kShift) && !ranges.empty()) {
            ranges.back() = {anchorRow, anchorCol, r, c};
            row = r, col = c;
            return;
        }
        if (mods & draw::kCtrl) ranges.push_back({r, c, r, c});
        else ranges = {{r, c, r, c}};
        row = anchorRow = r, col = anchorCol = c;
    }
    // A column's header (`rows` rows): the column; Shift: the columns from the anchor's; Ctrl: one more column.
    void columns(int c, unsigned mods, int rows) {
        const int last = std::max(0, rows - 1);
        if ((mods & draw::kShift) && !ranges.empty()) {
            ranges.back() = {0, anchorCol, last, c};
            col = c;
            return;
        }
        const DataCellRange g{0, c, last, c};
        if (mods & draw::kCtrl) ranges.push_back(g);
        else ranges = {g};
        col = anchorCol = c, anchorRow = 0, row = std::clamp(row, 0, last);
    }
    // A row's number (`cols` columns): the row; Shift: the rows from the anchor's; Ctrl: one more row.
    void rows(int r, unsigned mods, int cols) {
        const int last = std::max(0, cols - 1);
        if ((mods & draw::kShift) && !ranges.empty()) {
            ranges.back() = {anchorRow, 0, r, last};
            row = r;
            return;
        }
        const DataCellRange g{r, 0, r, last};
        if (mods & draw::kCtrl) ranges.push_back(g);
        else ranges = {g};
        row = anchorRow = r, anchorCol = 0, col = std::clamp(col, 0, last);
    }
    void all(int rows, int cols) {
        ranges = {{0, 0, std::max(0, rows - 1), std::max(0, cols - 1)}};
        anchorRow = anchorCol = 0;
    }
    // The arrow keys: the active cell moves (kept inside the table); Shift makes the rectangle from the anchor to it.
    void move(int dr, int dc, bool shift, int rows, int cols) {
        row = std::clamp(row + dr, 0, std::max(0, rows - 1)), col = std::clamp(col + dc, 0, std::max(0, cols - 1));
        if (shift && !ranges.empty()) ranges.back() = {anchorRow, anchorCol, row, col};
        else ranges = {{row, col, row, col}}, anchorRow = row, anchorCol = col;
    }
    // After the table changed its size: everything inside it again.
    void clamp(int rows, int cols) {
        const int lr = std::max(0, rows - 1), lc = std::max(0, cols - 1);
        for (auto& g : ranges)
            g = {std::min(g.r0, lr), std::min(g.c0, lc), std::min(g.r1, lr), std::min(g.c1, lc)};
        row = std::min(row, lr), col = std::min(col, lc), anchorRow = std::min(anchorRow, lr), anchorCol = std::min(anchorCol, lc);
    }
    // Every selected cell once, row by row.
    std::vector<std::pair<int, int>> cells() const {
        std::set<std::pair<int, int>> seen;
        for (const auto& g : ranges)
            for (int r = g.top(); r <= g.bottom(); ++r)
                for (int c = g.left(); c <= g.right(); ++c) seen.insert({r, c});
        return {seen.begin(), seen.end()};
    }
    // The rectangle round every selected cell (what Copy takes).
    DataCellRange bounds() const {
        DataCellRange b{ranges[0].top(), ranges[0].left(), ranges[0].bottom(), ranges[0].right()};
        for (const auto& g : ranges) b = {std::min(b.r0, g.top()), std::min(b.c0, g.left()), std::max(b.r1, g.bottom()), std::max(b.c1, g.right())};
        return b;
    }
};

// --- Text of cells: the clipboard (tab-separated, as spreadsheets copy) and CSV files -------------------------------------

using DataGrid = std::vector<std::vector<std::wstring>>;

// The grid as text: cells separated by `sep`, rows by CR LF; a cell with the separator, a quote or a line end in
// quotes (a quote in it doubled).
std::wstring dataGridText(const DataGrid& grid, wchar_t sep) {
    std::wstring out;
    for (const auto& row : grid) {
        for (size_t k = 0; k < row.size(); ++k) {
            if (k) out += sep;
            const std::wstring& v = row[k];
            if (v.find_first_of(std::wstring{sep, L'"', L'\r', L'\n'}) == std::wstring::npos) {
                out += v;
                continue;
            }
            out += L'"';
            for (wchar_t ch : v) out += ch == L'"' ? L"\"\"" : std::wstring(1, ch);
            out += L'"';
        }
        out += L"\r\n";
    }
    return out;
}

// The separator of a CSV file: a tab, a semicolon (spreadsheets in languages with a decimal comma save these) or a
// comma, whichever the first line has most of outside quotes.
wchar_t dataGridSeparator(const std::wstring& text) {
    int tabs = 0, semis = 0, commas = 0;
    bool quoted = false;
    for (wchar_t ch : text) {
        if (ch == L'"') quoted = !quoted;
        else if (!quoted && (ch == L'\n' || ch == L'\r')) break;
        else if (!quoted) tabs += ch == L'\t', semis += ch == L';', commas += ch == L',';
    }
    return tabs >= semis && tabs >= commas && tabs > 0 ? L'\t' : semis > commas ? L';' : L',';
}

// Text back into a grid (the rules of dataGridText; CR LF, LF or CR end a row). The empty line at the end goes.
DataGrid dataGridRead(const std::wstring& text, wchar_t sep) {
    DataGrid grid(1);
    std::wstring cell;
    bool quoted = false, any = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (quoted) {
            if (ch == L'"' && i + 1 < text.size() && text[i + 1] == L'"') cell += L'"', ++i;
            else if (ch == L'"') quoted = false;
            else cell += ch;
            continue;
        }
        if (ch == L'"' && cell.empty()) quoted = true, any = true;
        else if (ch == sep) grid.back().push_back(std::move(cell)), cell.clear(), any = true;
        else if (ch == L'\r' || ch == L'\n') {
            if (ch == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            grid.back().push_back(std::move(cell)), cell.clear();
            grid.emplace_back(), any = false;
        } else cell += ch, any = true;
    }
    if (any || !cell.empty()) grid.back().push_back(std::move(cell));
    if (grid.back().empty()) grid.pop_back();
    return grid;
}

// The table as a CSV file: a row of column names ("id", "name", the fields' names), then a row for each of `rows`.
DataGrid dataTableGrid(const DatTable& t, const std::vector<DataTableColumn>& cols, const std::vector<int>& rows) {
    DataGrid grid;
    std::vector<std::wstring> head{L"id"};
    for (const auto& col : cols) head.push_back(dataColumnKey(t.schema(), col));
    grid.push_back(std::move(head));
    for (int e : rows) {
        std::vector<std::wstring> row{std::to_wstring(e)};
        for (const auto& col : cols) row.push_back(dataCellData(t, col, e));
        grid.push_back(std::move(row));
    }
    return grid;
}

// What a CSV file's import did.
struct DataImportResult {
    std::wstring error;             // the file cannot be used (nothing changed): no "id" column, no rows
    int changed = 0, entries = 0;   // the values that changed, and the entries they are in
    int notValues = 0, locked = 0;  // the cells whose text is not a value of their column, and the cells that cannot change
    int noEntry = 0;                // the rows whose id is not an entry of the .dat
    std::vector<std::wstring> unknown;  // the columns that are not the .dat's (left out)
    std::wstring firstProblem;      // the first cell that did not go in: "Hit Points of 12: \"abc\" is not a value"
};

// The column (an index of `cols`, the .dat's columns) that a column name gives: its CSV name (dataColumnKey), the
// schema's name ("Hit Points", "<field>: <flag>", as older files have them) or the name in the Table's header; any case.
// -1: none.
int dataColumnNamed(const DatSchema& sch, const std::vector<DataTableColumn>& cols, const std::wstring& text) {
    const std::wstring name = dataLower(dataTrim(text));
    if (name.empty()) return -1;
    for (size_t i = 0; i < cols.size(); ++i)
        if (dataLower(dataColumnKey(sch, cols[i])) == name) return int(i);
    for (size_t i = 0; i < cols.size(); ++i) {
        const DatField* f = dataSchemaField(sch, cols[i].field);
        const std::wstring schemaName = !f ? L"" : widen(f->name) + (cols[i].kind == DataCellKind::Bit ? L": " + widen(f->flags[size_t(cols[i].bit)]) : L"");
        if ((!schemaName.empty() && dataLower(schemaName) == name) || dataLower(dataColumnTitle(sch, cols[i])) == name ||
            (cols[i].kind == DataCellKind::Bit && dataLower(dataColumnGroup(sch, cols[i]) + L": " + dataColumnTitle(sch, cols[i])) == name))
            return int(i);
    }
    return -1;
}
// The row is a row of column names: each of its cells is "id" or a column's name (dataColumnNamed), and one at least.
// (A paste's header: only the CSV names, "id" and the schema's names count, not the shown titles. A value such as
// "Building, Flyer", a flag field's cell, is not a header although "Building" is a flag column's title.)
bool dataGridIsHeader(const DatSchema& sch, const std::vector<std::wstring>& row) {
    const std::vector<DataTableColumn> cols = dataTableColumns(sch);
    int names = 0;
    for (const std::wstring& cell : row) {
        const std::wstring name = dataLower(dataTrim(cell));
        if (name.empty()) continue;
        bool known = name == L"id";
        for (size_t i = 0; i < cols.size() && !known; ++i) {
            const DatField* f = dataSchemaField(sch, cols[i].field);
            const std::wstring schemaName = !f ? L"" : widen(f->name) + (cols[i].kind == DataCellKind::Bit ? L": " + widen(f->flags[size_t(cols[i].bit)]) : L"");
            known = dataLower(dataColumnKey(sch, cols[i])) == name || (!schemaName.empty() && dataLower(schemaName) == name);
        }
        if (!known) return false;
        ++names;
    }
    return names > 0;
}

// Puts a CSV file's grid into the table: the rows by their "id", the columns by their names (dataColumnNamed). An empty
// cell changes nothing. The caller saves the change (one undo step).
DataImportResult dataTableImport(DatTable& t, const DataGrid& grid) {
    DataImportResult res;
    const DatSchema& sch = t.schema();
    if (grid.empty()) return res.error = L"Could not import the file. It has no rows.", res;
    const std::vector<DataTableColumn> cols = dataTableColumns(sch);
    int idCol = -1;
    std::vector<int> colOf(grid[0].size(), -1);
    for (size_t k = 0; k < grid[0].size(); ++k) {
        const std::wstring name = dataLower(dataTrim(grid[0][k]));
        if (name.empty()) continue;
        if (name == L"id") {
            idCol = int(k);
            continue;
        }
        colOf[k] = dataColumnNamed(sch, cols, name);
        if (colOf[k] < 0) res.unknown.push_back(dataTrim(grid[0][k]));
    }
    if (idCol < 0) return res.error = L"Could not import the file. It has no \"id\" column. The first row must give the column names, with \"id\" for the entry numbers.", res;
    for (size_t r = 1; r < grid.size(); ++r) {
        const auto& row = grid[r];
        if (size_t(idCol) >= row.size() || dataTrim(row[size_t(idCol)]).empty()) continue;
        uint32_t id = 0;
        if (dataReadEntry(dataTrim(row[size_t(idCol)]), 0xFFFFFFFFu, id) != DataEntryRead::Number || id >= uint32_t(sch.entries)) {
            ++res.noEntry;
            continue;
        }
        bool any = false;
        // (A cell with the entry's value as it was before the row goes in is not a change: a flag field's column and its
        // flags' columns in one file then do not undo each other, whichever of them the user changed.)
        std::vector<std::wstring> before(row.size());
        for (size_t k = 0; k < row.size() && k < colOf.size(); ++k)
            if (colOf[k] >= 0) before[k] = dataCellData(t, cols[size_t(colOf[k])], int(id));
        for (size_t k = 0; k < row.size() && k < colOf.size(); ++k) {
            if (colOf[k] < 0) continue;
            const DataTableColumn& col = cols[size_t(colOf[k])];
            // (A name the game's list gives, as an export writes it, is not a change.)
            if (col.kind == DataCellKind::Name && !dataNameString(sch.name, int(id))) continue;
            if (dataTrim(row[k]) == dataTrim(before[k])) continue;
            const DataCellSet s = dataCellSet(t, col, int(id), row[k]);
            if (s == DataCellSet::Changed) ++res.changed, any = true;
            else if (s == DataCellSet::NotValue || s == DataCellSet::Locked) {
                ++(s == DataCellSet::NotValue ? res.notValues : res.locked);
                if (res.firstProblem.empty())
                    res.firstProblem = dataColumnTitle(sch, col) + L" of " + std::to_wstring(id) + L": \x201C" + dataTrim(row[k]) + L"\x201D " +
                                       (s == DataCellSet::NotValue ? L"is not a value of this column" : L"cannot change (" + dataCellLocked(t, col, int(id)) + L")");
            }
        }
        res.entries += any;
    }
    return res;
}

// A UTF-8 file's text as wide text (a byte order mark at its start goes).
std::wstring dataFileText(const std::string& bytes) {
    const bool bom = bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0;
    return widen(bom ? bytes.substr(3) : bytes);
}
