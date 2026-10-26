// The Data workspace's Table view: the model (portable, core.inc, after data_nav.inc). The entries are the rows and the
// fields are the columns: the name, then each field in the form's order, a field of flags a column for each flag. A cell
// is a number, a dropdown's value (a number to type, its name shown), a yes/no value or a flag (a check box), or the
// unit's name (text). Here: the columns, a cell's text and how a typed text sets it, the selected cells, and the
// clipboard's and the CSV file's text. The view (views_data_table.inc) paints them; the program saves each change as one
// undo step.

enum class DataCellKind { Name, Number, Pick, Bool, Bit };

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
// section's fields in their order); a field of flags gives a column to each flag.
std::vector<DataTableColumn> dataTableColumns(const DatSchema& sch) {
    std::vector<DataTableColumn> out{{DataCellKind::Name, -1, -1}};
    for (int si = 0; si <= int(sch.sections.size()); ++si) {
        std::vector<const DatField*> fields;
        for (const auto& f : sch.fields)
            if (f.section == si) fields.push_back(&f);
        std::sort(fields.begin(), fields.end(), [](const DatField* a, const DatField* b) { return a->order < b->order; });
        for (const DatField* f : fields) {
            if (!f->flags.empty()) {
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
    if (col.kind == DataCellKind::Name) return L"Name";
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
// A column's name in a CSV file: "name", the field's name in the schema, or "<field>: <flag>". (The schema's names, so
// that a file reads again in another language.)
std::wstring dataColumnKey(const DatSchema& sch, const DataTableColumn& col) {
    if (col.kind == DataCellKind::Name) return L"name";
    const DatField* f = dataSchemaField(sch, col.field);
    if (!f) return L"";
    return widen(f->name) + (col.kind == DataCellKind::Bit ? L": " + widen(f->flags[size_t(col.bit)]) : L"");
}

// The stat_txt.tbl string (1-based) that a Name cell changes: a unit's name (0: none, the name is not text to change).
// Other .dat files take their names from the game's lists or share their Label strings, so their names stay as they are.
int dataNameString(const std::string& dat, int e) {
    const int sid = dat == "units" ? entryNameString(dat, e) : 0;
    return sid > 0 && sid <= dataExtras().strings.count() ? sid : 0;
}

// Why the cell cannot change ("" when it can): a field the entry does not use, or a field that is read only.
std::wstring dataCellLocked(const DatTable& t, const DataTableColumn& col, int e) {
    const std::string& dat = t.schema().name;
    if (col.kind == DataCellKind::Name)
        return dataNameString(dat, e) ? L"" : L"The name comes from the game's list. Change the Label value to change the text that the game shows.";
    const DatField* f = dataSchemaField(t.schema(), col.field);
    if (!f || !f->covers(e)) return L"This entry does not use this value.";
    return datFieldReadOnly(dat, *f, e);
}

// A cell's value as data: what the clipboard and a CSV file get, and what a typed text is compared with. A number in
// the form's unit (hit points), a dropdown's number, 0 or 1 for a check box, the name. Empty: the entry does not use
// the field.
std::wstring dataCellData(const DatTable& t, const DataTableColumn& col, int e) {
    if (col.kind == DataCellKind::Name) {
        const int sid = dataNameString(t.schema().name, e);
        return sid ? widen(StatTxt::escape(dataExtras().strings.text(sid - 1))) : tabEntryName(datTabOf(t.schema().name), e);
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
    if (col.kind == DataCellKind::Bit || col.kind == DataCellKind::Bool) {
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

// Puts a CSV file's grid into the table: the rows by their "id", the columns by their names (the CSV names of
// dataColumnKey, or the names in the Table's headers; any case). An empty cell changes nothing. The caller saves the
// change (one undo step).
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
        for (size_t i = 0; i < cols.size() && colOf[k] < 0; ++i)
            if (dataLower(dataColumnKey(sch, cols[i])) == name || dataLower(dataColumnTitle(sch, cols[i])) == name ||
                (cols[i].kind == DataCellKind::Bit && dataLower(dataColumnGroup(sch, cols[i]) + L": " + dataColumnTitle(sch, cols[i])) == name))
                colOf[k] = int(i);
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
        for (size_t k = 0; k < row.size() && k < colOf.size(); ++k) {
            if (colOf[k] < 0) continue;
            const DataTableColumn& col = cols[size_t(colOf[k])];
            // (A name the game's list gives, as an export writes it, is not a change.)
            if (col.kind == DataCellKind::Name && !dataNameString(sch.name, int(id))) continue;
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
