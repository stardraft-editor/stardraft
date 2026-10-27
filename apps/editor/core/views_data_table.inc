// The Data workspace's Table view (portable, core.inc, after views_data_entry.inc): the open .dat as a table, in place of
// the entry's form. A row for each entry (the entries selected in the list when there are 2 or more, else every row of
// the list), a column for each field (core/data_table.inc). The id and the name stay at the left while the fields
// scroll. Select cells as in a spreadsheet (click, Shift, Ctrl, a column's header, a row's number, a drag), then type
// a value: it goes into every selected cell. Check boxes toggle with a click or Space; a dropdown's cell takes its
// number or the name of a value, and its arrow opens its list; a field of flags is one wide column: its value in hex
// (to type), then a check box for each flag (a click changes that flag; Space opens the list of its flags). Delete puts the game's values back, Ctrl+C and Ctrl+V copy
// and paste cells (tab-separated, as spreadsheets do; the menu also copies with a row of column names, or as CSV, and a
// paste with a row of column names puts each column into the column it names), Ctrl+D copies the top cell down. "+10",
// "-10", "*1.5" or "/2" changes each selected number by that step. Each change is one undo step.
// The filter box and Changed at the top show only some columns; Import CSV and Export CSV are there too. The program gives the table and saves through DataTableViewHost.

class DataTableViewHost {
public:
    virtual ~DataTableViewHost() = default;
    virtual DatTable* table() = 0;                     // the open .dat (null: none)
    virtual const std::vector<int>& rows() = 0;        // the entries the table shows, in the list's order
    virtual bool rowsPicked() = 0;                     // the rows are the list's selected entries (not all of its rows)
    virtual int openEntry() = 0;                       // the entry open in the list (its row number is marked)
    virtual draw::Image icon(int e) = 0;               // an entry's picture (empty: none)
    virtual draw::Image refIcon(const std::string& dat, uint32_t v) = 0;
    virtual std::wstring note(const DatField& f, uint32_t v) = 0;  // the name of a dropdown's value
    virtual std::vector<ListPickItem> dropdownItems(const DatField& f) = 0;
    // A dropdown's list under `box` (the view's coordinates) at the value `current`, its search holding `search`: true
    // with the picked value.
    virtual bool choose(const DatField& f, uint32_t current, draw::Rect box, uint32_t& out, const std::wstring& search = L"") = 0;
    virtual void saved(const std::wstring& what) = 0;  // cells changed: the program saves them (one undo step) and says `what`
    virtual void open(int e) = 0;                      // a row's number double-clicked: the entry's form
    virtual void importCsv() = 0;
    virtual void exportCsv() = 0;
    virtual void status(const std::wstring& text) = 0;
    virtual bool travel(int button) = 0;               // the mouse's back (1) and forward (2) buttons
    // The game text pop-out under `box` (the view's coordinates): the text with its colors (the palette's swatches, Preview
    // and Raw). True with the new text.
    virtual bool editGameText(draw::Rect box, const std::wstring& title, std::string& text) = 0;
    // A menu at `pt` (the view's coordinates); a pick runs `pick` with the item's id.
    virtual void menu(draw::Point pt, const MenuModel& items, std::function<void(int)> pick) = 0;
    // The multi-pick list under `pt`: `checked` in, the checks out; false when it was canceled. With `toggle`, each click
    // on a row acts at once: `toggle` gets the row's value and its new check, and can change the checks; the list has no
    // Cancel then.
    virtual bool pickMany(const std::wstring& title, std::vector<ListPickItem> items, std::set<std::string>& checked, draw::Point pt,
                          std::function<void(const std::string&, bool, std::set<std::string>&)> toggle = {}) = 0;
};

class DataTableView final : public draw::View {
public:
    explicit DataTableView(DataTableViewHost& host) : host_(host) {
        vbar_.onScroll = [this](int pos) { sy_ = pos, invalidate(); };
        hbar_.onScroll = [this](int pos) { sx_ = pos, invalidate(); };
        add(vbar_, {0, 0, 0, 0});
        add(hbar_, {0, 0, 0, 0});
        add(entry_, {0, 0, 0, 0});
        entry_.setVisible(false);
        entry_.onCommit = [this] {
            commitEdit(true);
            focus();
        };
        entry_.onCancel = [this] { cancelEdit(); };
        entry_.onFocusLost = [this] { commitEdit(false); };
        entry_.onTab = [this](bool back) {
            commitEdit(false);
            moveActive(0, back ? -1 : 1, false);
            focus();
        };
        entry_.field().onChange = [this] { invalidate(); };  // (the other selected cells show the text as it is typed)
        add(filter_, {0, 0, 0, 0});
        filter_.setPlaceholder(L"Filter the columns");
        filter_.onChange = [this] { ++colsRev_, invalidate(); };
        filter_.onEnter = [this] { focus(); };
        filter_.onEscape = [this] {
            if (!filter_.text().empty()) filter_.setText(L""), ++colsRev_;
            focus();
            invalidate();
        };
    }

    // The metrics (scaled).
    static int barH() { return scale(44); }   // the bar at the top: the title, Import CSV, Export CSV
    static int groupH() { return scale(24); }  // the headers' first row: the section (a flag's field)
    static int headH() { return scale(28); }   // the headers' second row: the column's name
    static int rowH() { return scale(26); }
    static int idW() { return scale(52); }
    static int footH() { return scale(28); }  // the status bar at the bottom: the active cell's raw value, the selection's sums

    // The table's place in the view: under the bar and the headers, left of the vertical bar, over the horizontal bar.
    draw::Rect body() const {
        const int sb = scale(stardraft::kit::kScrollBarW);
        return {0, barH() + groupH() + headH(), std::max(0, width() - sb), std::max(0, height() - sb - footH())};
    }
    // The text box is open on a cell (the keys are its own).
    bool editing() const { return editing_; }
    // The open .dat or the rows changed: the columns again, the selection inside the table again.
    void refresh() {
        sync();
        invalidate();
    }
    // The text of the selected cells (the clipboard's), and the selection, for a check.
    std::wstring selectionText() {
        sync();
        return copyText();
    }
    DataTableSelection& selection() { return sel_; }
    stardraft::kit::TextField& columnFilter() { return filter_; }
    const std::vector<DataTableColumn>& columns() {
        sync();
        return cols_;
    }

    void resized() override {
        const int sb = scale(stardraft::kit::kScrollBarW), top = barH() + groupH() + headH();
        const int foot = height() - footH();
        vbar_.setFrame({width() - sb, top, width(), std::max(top, foot - sb)});
        hbar_.setFrame({0, foot - sb, std::max(0, width() - sb), foot});
        placeEntry();
    }
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    bool text(const draw::TextEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override;
    bool hoverTip() const override { return true; }
    bool sameItem(draw::Point a, draw::Point b) const override {  // (a double click: two clicks on one cell)
        const Hit x = hitAt(a), y = hitAt(b);
        return x.row == y.row && x.col == y.col && x.number == y.number && x.header == y.header;
    }
    std::wstring tip(draw::Point pt) override;
    bool focusable() const override { return true; }
    bool typesText() const override { return true; }  // (letters start a cell's text, not the window's shortcuts)
    bool extraButton(int button) override { return (button == 1 || button == 2) && host_.travel(button); }
    void captureLost() override { drag_ = Drag::None; }

    // Commits the text box (if open): its text goes into every selected cell. `down`: Enter, the active cell goes down
    // when only one cell is selected.
    void commitEdit(bool down);
    void cancelEdit() {
        if (!editing_) return;
        editing_ = false;
        entry_.setVisible(false);
        focus();
        invalidate();
    }
    // The text into every selected cell; one undo step. (The checks and the text box's commit use it.)
    void setSelected(const std::wstring& text);
    // Pastes a tab-separated text at the selection (one value: into every selected cell).
    void paste(const std::wstring& text);
    void focus() {
        if (draw::Host* h = host()) h->focus(*this);
    }

private:
    enum class Drag { None, Cells, Columns, Rows, Width, Move };
    // A place in the view: a cell, a column's header, a row's number, or a column's right edge in the headers.
    struct Hit {
        int row = -1, col = -1;
        bool header = false, number = false, edge = false, check = false, arrow = false;
        int bit = -1;  // a field of flags: the flag's box (or its header) under the point
        draw::Rect cell;
    };
    // A field of flags' column: its value in hex, then a box for each flag, as wide as the flag's name (measured in
    // paint: flagWs_).
    int flagHexW() const { return scale(92); }
    int flagW(int field, size_t b) const {
        const auto it = flagWs_.find(field);
        return it != flagWs_.end() && b < it->second.size() ? it->second[b] : scale(30);
    }
    int flagsW(const DatField& f) const {
        int w = flagHexW();
        for (size_t b = 0; b < f.flags.size() && b < 32; ++b) w += flagW(f.index, b);
        return w;
    }
    int flagAt(const DatField& f, int dx) const {  // the flag whose box is at dx from the column's left (-1: the hex)
        int x = flagHexW();
        for (size_t b = 0; b < f.flags.size() && b < 32; ++b) {
            if (dx >= x && dx < x + flagW(f.index, b)) return int(b);
            x += flagW(f.index, b);
        }
        return -1;
    }
    void measureFlags(draw::Canvas& c, const DatSchema& sch);
    void toggleFlag(int r, int c, int bit);

    void sync();
    int colW(size_t k) const;
    int frozenW() const { return idW() + (cols_.empty() ? 0 : colW(0)); }
    int scrollW() const;   // the width of the columns that scroll
    int colLeft(size_t k) const;  // a column's left edge in the view (scrolled)
    Hit hitAt(draw::Point pt) const;
    draw::Rect cellRect(int r, int c) const;
    void clampScroll();
    void reveal(int r, int c);
    void revealColumn(int c);
    void moveActive(int dr, int dc, bool shift);
    void beginEdit(const std::wstring& start, bool selectAll);
    void placeEntry();
    void toggleChecks(int r, int c);
    void setChecks(bool on);
    void resetSelected();
    void fillDown();
    void fitColumns(draw::Canvas& c, const DatTable& t);  // the columns in fitDue_: as wide as their widest text
    void setChangedOnly(bool on);
    bool filtered() const { return changedOnly_ || !filter_.text().empty(); }  // (some columns do not show)
    void openPick(int r, int c, const std::wstring& search = L"");
    void openFlags(int r, int c);  // a field of flags: the list of its flags to check (Space, Alt+Down)
    void editName(const std::string& start);  // a name or a string: the game text pop-out, with its colors
    void sortBy(int c, bool down);  // c < 0: the list's order again
    int dropIndex(int x) const;      // the column a moved column goes before, for the mouse at x
    void moveColumns(const std::vector<int>& cols, int before);
    void contextMenu(const Hit& hit, draw::Point pt);
    void pickColumns(draw::Point pt);
    void hideColumns(const std::vector<int>& cols);
    std::vector<int> selectedColumns(int c) const;  // the columns that are selected whole, with c (else c only)
    enum class CopyAs { Values, Headers, Csv };
    std::wstring copyText(CopyAs as = CopyAs::Values);
    void copy(CopyAs as);
    void report(const std::wstring& verb, int changed, int bad, int locked);
    void paintCell(draw::Canvas& c, const DatTable& t, const DataTableColumn& col, int e, draw::Rect r, bool hot);
    void paintFoot(draw::Canvas& c, const DatTable& t);

    DataTableViewHost& host_;
    std::map<int, std::vector<int>> flagWs_;  // each field of flags: the widths of its flags' boxes (measureFlags)
    std::string flagWsDat_;
    int flagWsScale_ = 0;
    stardraft::kit::ScrollBar vbar_{stardraft::kit::Axis::Vertical}, hbar_{stardraft::kit::Axis::Horizontal};
    DataNumberEntry entry_;
    stardraft::kit::TextField filter_;  // the columns that show: their names have this text
    bool changedOnly_ = false;          // only the columns with a value that is not the game's
    int colsRev_ = 0, colsRevMade_ = 0;  // the filter changed: the columns again
    std::vector<int> fitDue_;           // the columns that the next paint makes as wide as their text
    std::string dat_;                   // the .dat the columns are of
    std::vector<DataTableColumn> cols_;  // the columns that show (the .dat's, less the hidden ones)
    int hiddenRev_ = -1;                // gDataTableHiddenRev when cols_ was made
    std::vector<int> rows_;             // the rows as they show (sorted when a sort is on)
    std::vector<int> hostRows_;         // the rows in the list's order, as the last sync had them
    std::wstring sortKey_;              // the column the rows are sorted by (its CSV name; empty: the list's order)
    bool sortDown_ = false;             // high to low
    bool sortDue_ = false;              // the sort changed: the rows are sorted again at the next sync
    DataTableSelection sel_;
    int sx_ = 0, sy_ = 0;               // the scroll in pixels
    bool editing_ = false;
    Drag drag_ = Drag::None;
    int dragCol_ = -1, dragX_ = 0, dragW0_ = 0;
    int dropAt_ = -1;  // a column's move: the column it goes before (cols_.size(): after the last one); -1 none yet
    Hit hot_;
    draw::Rect importBtn_, exportBtn_, columnsBtn_, changedBtn_;
};

inline void DataTableView::sync() {
    const DatTable* t = host_.table();
    const std::string dat = t ? t->schema().name : "";
    if (t) dataTableLayout(dat);  // (another project: its layouts are read, and the revision goes up)
    if (dat != dat_ || hiddenRev_ != gDataTableHiddenRev || colsRevMade_ != colsRev_) {
        if (dat != dat_) sel_ = {}, sx_ = 0, sy_ = 0;
        dat_ = dat, hiddenRev_ = gDataTableHiddenRev, colsRevMade_ = colsRev_;
        cols_.clear();
        if (t) {
            const DataTableLayout& l = dataTableLayout(dat);
            // The filter: the text in the column's name or its section; Changed: a value that is not the game's. (The
            // name always shows.)
            const std::wstring want = dataLower(dataTrim(filter_.text()));
            auto shows = [&](const DataTableColumn& col) {
                if (col.kind == DataCellKind::Name) return true;
                if (dataColumnHidden(l, t->schema(), col)) return false;
                if (!want.empty()) {  // (a field of flags: its flags' names too)
                    std::wstring names = dataColumnGroup(t->schema(), col) + L" " + dataColumnTitle(t->schema(), col);
                    if (const DatField* f = col.kind == DataCellKind::Flags ? dataSchemaField(t->schema(), col.field) : nullptr)
                        for (const std::string& n : f->flags) names += L" " + widen(n);
                    if (dataLower(names).find(want) == std::wstring::npos) return false;
                }
                return !changedOnly_ || dataColumnChanged(*t, col);
            };
            for (const DataTableColumn& col : dataTableColumns(t->schema()))
                if (shows(col)) cols_.push_back(col);
            // The layout's order: the columns it names in its order, the others after them in the form's order; the name first.
            if (!l.order.empty() && cols_.size() > 1) {
                auto at = [&](const DataTableColumn& col) {
                    const auto it = std::find(l.order.begin(), l.order.end(), dataColumnKey(t->schema(), col));
                    return it == l.order.end() ? l.order.size() : size_t(it - l.order.begin());
                };
                std::stable_sort(cols_.begin() + 1, cols_.end(), [&](const DataTableColumn& a, const DataTableColumn& b) { return at(a) < at(b); });
            }
            if (sortKey_ != l.sortKey || sortDown_ != l.sortDown) sortKey_ = l.sortKey, sortDown_ = l.sortDown, sortDue_ = true;
        }
        sel_.clamp(int(rows_.size()), int(cols_.size()));
        if (editing_) cancelEdit();
    }
    if (host_.rows() != hostRows_ || sortDue_) {
        if (editing_) cancelEdit();
        hostRows_ = host_.rows();
        rows_ = hostRows_;
        sortDue_ = false;
        // The sort: by the column's value (a number as it is stored, a name as text); the cells that the entry does not use
        // go last. Equal values keep the list's order. (An edit does not sort again: the rows stay where they are.)
        const DataTableColumn* by = nullptr;
        for (const auto& col : cols_)
            if (t && !sortKey_.empty() && dataColumnKey(t->schema(), col) == sortKey_) by = &col;
        if (by && t) {
            const DataTableColumn col = *by;
            const DatField* f = col.kind == DataCellKind::Name ? nullptr : dataSchemaField(t->schema(), col.field);
            auto key = [&](int e) -> std::pair<int, double> {  // (unused, value)
                if (!f) return {0, 0};
                if (!f->covers(e)) return {1, 0};
                const uint32_t v = t->value(*f, e);
                return {0, double(v)};
            };
            const bool down = sortDown_;
            std::stable_sort(rows_.begin(), rows_.end(), [&](int a, int b) {
                if (!f) {
                    const std::wstring x = dataLower(dataRowName(dat_, a)), y = dataLower(dataRowName(dat_, b));
                    return down ? y < x : x < y;
                }
                const auto ka = key(a), kb = key(b);
                if (ka.first != kb.first) return ka.first < kb.first;
                return down ? kb.second < ka.second : ka.second < kb.second;
            });
        }
        sel_.clamp(int(rows_.size()), int(cols_.size()));
    }
}

inline int DataTableView::colW(size_t k) const {
    const DataTableColumn& col = cols_[k];
    if (const DatTable* t = host_.table(); t && t->schema().name == dat_) {
        if (col.kind == DataCellKind::Flags)  // (as wide as its hex and its boxes)
            if (const DatField* f = dataSchemaField(t->schema(), col.field)) return flagsW(*f);
        const auto& widths = dataTableLayout(dat_).widths;
        if (const auto it = widths.find(dataColumnKey(t->schema(), col)); it != widths.end()) return scale(it->second);
    }
    switch (col.kind) {
    case DataCellKind::Name: return scale(dat_ == "strings" ? 760 : 220);  // (a string's text: wide)
    case DataCellKind::Pick: return scale(180);
    case DataCellKind::Flags: return scale(240);
    case DataCellKind::Bool: return scale(72);
    default: return scale(96);
    }
}
inline int DataTableView::scrollW() const {
    int w = 0;
    for (size_t k = 1; k < cols_.size(); ++k) w += colW(k);
    return w;
}
inline int DataTableView::colLeft(size_t k) const {
    if (k == 0) return idW();
    int x = frozenW() - sx_;
    for (size_t i = 1; i < k; ++i) x += colW(i);
    return x;
}
inline draw::Rect DataTableView::cellRect(int r, int c) const {
    const draw::Rect b = body();
    const int x = colLeft(size_t(c)), y = b.top + r * rowH() - sy_;
    return {x, y, x + colW(size_t(c)), y + rowH()};
}

inline void DataTableView::clampScroll() {
    const draw::Rect b = body();
    sy_ = std::clamp(sy_, 0, std::max(0, int(rows_.size()) * rowH() - b.height()));
    sx_ = std::clamp(sx_, 0, std::max(0, scrollW() - (b.width() - frozenW())));
}

// Scrolls so that cell (r, c) shows.
inline void DataTableView::reveal(int r, int c) {
    const draw::Rect b = body();
    const int top = r * rowH();
    if (top < sy_) sy_ = top;
    if (top + rowH() > sy_ + b.height()) sy_ = top + rowH() - b.height();
    revealColumn(c);
}
// Scrolls sideways so that column c shows (the frozen name column always shows).
inline void DataTableView::revealColumn(int c) {
    if (c > 0 && c < int(cols_.size())) {
        int left = 0;
        for (int i = 1; i < c; ++i) left += colW(size_t(i));
        const int room = body().width() - frozenW();
        // (a field of flags: its hex shows, the left of a column that can be wider than the view)
        const int need = cols_[size_t(c)].kind == DataCellKind::Flags ? std::min(colW(size_t(c)), flagHexW()) : colW(size_t(c));
        if (left < sx_) sx_ = left;
        if (left + need > sx_ + room) sx_ = left + need - room;
    }
    clampScroll();
    invalidate();
}

inline DataTableView::Hit DataTableView::hitAt(draw::Point pt) const {
    Hit h;
    const draw::Rect b = body();
    const int headTop = barH() + groupH();
    if (pt.x >= b.right || pt.y >= b.bottom || pt.y < headTop) return h;
    // The column under x: the frozen ones first, then the ones that scroll (under the frozen ones: hidden).
    int col = -1;
    if (pt.x >= idW() && pt.x < frozenW() && !cols_.empty()) col = 0;
    else if (pt.x >= frozenW())
        for (size_t k = 1; k < cols_.size(); ++k) {
            const int x = colLeft(k);
            if (pt.x >= x && pt.x < x + colW(k)) col = int(k);
        }
    h.col = col;
    if (pt.y < b.top) {  // the headers: a column's name, or its right edge (drag: its width)
        h.header = true;
        for (size_t k = 0; k < cols_.size(); ++k) {
            const int right = colLeft(k) + colW(k);
            if ((k == 0 || right > frozenW()) && cols_[k].kind != DataCellKind::Flags && std::abs(pt.x - right) <= scale(3)) h.edge = true, h.col = int(k);
        }
        if (!h.edge && col >= 0 && cols_[size_t(col)].kind == DataCellKind::Flags)  // (a flag's name over its box)
            if (const DatTable* t = host_.table())
                if (const DatField* f = dataSchemaField(t->schema(), cols_[size_t(col)].field)) h.bit = flagAt(*f, pt.x - colLeft(size_t(col)));
        return h;
    }
    const int r = (pt.y - b.top + sy_) / rowH();
    if (r < 0 || r >= int(rows_.size())) return h;
    h.row = r;
    if (pt.x < idW()) return h.number = true, h;
    if (col < 0) return h;
    h.cell = cellRect(r, col);
    const DataTableColumn& cc = cols_[size_t(col)];
    if (cc.kind == DataCellKind::Bool) {
        const int bx = scale(14), cx = (h.cell.left + h.cell.right) / 2, cy = (h.cell.top + h.cell.bottom) / 2;
        h.check = pt.x >= cx - bx && pt.x <= cx + bx && pt.y >= cy - bx && pt.y <= cy + bx;
    }
    if (cc.kind == DataCellKind::Flags)  // (a flag's box: the whole width under its name)
        if (const DatTable* t = host_.table())
            if (const DatField* f = dataSchemaField(t->schema(), cc.field)) h.bit = flagAt(*f, pt.x - h.cell.left), h.check = h.bit >= 0;
    if (cc.kind == DataCellKind::Pick) h.arrow = pt.x >= h.cell.right - scale(22);
    return h;
}

inline void DataTableView::paintCell(draw::Canvas& c, const DatTable& t, const DataTableColumn& col, int e, draw::Rect r, bool hot) {
    const std::wstring locked = dataCellLocked(t, col, e);
    const bool mod = dataCellChanged(t, col, e);
    const draw::Rect in{r.left + scale(6), r.top, r.right - scale(6), r.bottom};
    if (col.kind == DataCellKind::Name) {
        const draw::Image img = host_.icon(e);
        draw::Rect tr = in;
        if (img) c.fitted(img, {in.left, r.top + scale(3), in.left + rowH() - scale(6), r.bottom - scale(3)}), tr.left += rowH();
        std::wstring name = dataRowName(t.schema().name, e);
        c.text(tr, name.empty() ? L"(unnamed)" : name, mod ? kGood : name.empty() ? kDim : kText, draw::kMiddle | draw::kEllipsis);
        return;
    }
    const DatField* f = dataSchemaField(t.schema(), col.field);
    if (!f || !f->covers(e)) {
        c.text(in, L"\x2014", kFaint, draw::kMiddle | draw::kCenter);
        return;
    }
    const uint32_t v = t.value(*f, e);
    const bool ro = !locked.empty();
    switch (col.kind) {
    case DataCellKind::Bool: {
        const bool on = v != 0;
        const int bx = scale(14), cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        const draw::Rect box{cx - bx / 2, cy - bx / 2, cx - bx / 2 + bx, cy - bx / 2 + bx};
        if (mod) c.roundBox({box.left - scale(3), box.top - scale(3), box.right + scale(3), box.bottom + scale(3)}, kGoodFill, kGoodLine, float(scale(3)));
        kitpaint::drawCheckMark(c, box, on, hot && !ro, !ro);
        return;
    }
    case DataCellKind::Pick: {
        const std::string refDat = fieldRefDat(*f);
        const draw::Image img = refDat.empty() ? draw::Image{} : host_.refIcon(refDat, v);
        draw::Rect tr{in.left, r.top, r.right - scale(22), r.bottom};
        if (img) c.fitted(img, {tr.left - scale(2), r.top + scale(4), tr.left + scale(16), r.bottom - scale(4)}), tr.left += scale(20);
        const std::wstring note = host_.note(*f, v);
        c.text(tr, note.empty() ? L"#" + std::to_wstring(v) : note, ro ? kFaint : mod ? kGood : kText, draw::kMiddle | draw::kEllipsis);
        if (!ro) kitpaint::drawGlyph(c, {r.right - scale(20), r.top, r.right - scale(6), r.bottom}, UiIcon::Dropdown, hot ? kText : kFaint, 9);
        return;
    }
    case DataCellKind::Flags: {  // (the value in hex, then each flag's box; a flag that is not the game's is lit)
        const draw::Font of = c.setFont(c.font({draw::Face::Mono}));
        c.text({in.left, r.top, r.left + flagHexW() - scale(8), r.bottom}, dataFlagsHex(*f, v), ro ? kFaint : mod ? kGood : v ? kText : kDim, draw::kMiddle | draw::kRight);
        c.setFont(of);
        const uint32_t stock = t.stock(*f, e);
        int x = r.left + flagHexW();
        for (size_t b = 0; b < f->flags.size() && b < 32; ++b) {
            const int fw = flagW(f->index, b);
            c.fill({x, r.top + scale(4), x + 1, r.bottom - scale(4)}, kBorder, 110);
            const int bx = scale(14), cx = x + fw / 2, cy = (r.top + r.bottom) / 2;
            const draw::Rect box{cx - bx / 2, cy - bx / 2, cx - bx / 2 + bx, cy - bx / 2 + bx};
            if (((v ^ stock) >> b) & 1) c.roundBox({box.left - scale(3), box.top - scale(3), box.right + scale(3), box.bottom + scale(3)}, kGoodFill, kGoodLine, float(scale(3)));
            kitpaint::drawCheckMark(c, box, (v >> b) & 1, hot && hot_.bit == int(b) && !ro, !ro);
            x += fw;
        }
        return;
    }
    default: {
        const draw::Font of = c.setFont(c.font({draw::Face::Mono}));
        c.text(in, fieldValueText(*f, v), ro ? kFaint : mod ? kGood : kText, draw::kMiddle | draw::kRight);
        c.setFont(of);
    }
    }
}

inline void DataTableView::paint(draw::Canvas& c) {
    sync();
    clampScroll();
    const draw::Font uiFont = c.setFont(c.font({draw::Face::Ui}));
    c.setFont(uiFont);
    const int w = width(), h = height();
    c.fill({0, 0, w, h}, kBg);
    const DatTable* tp = host_.table();
    if (tp) measureFlags(c, tp->schema());
    if (tp && !fitDue_.empty()) {
        fitColumns(c, *tp);
        c.setFont(uiFont);
        clampScroll();
    }
    // The bar: the .dat and how many rows, and which ones; Import CSV and Export CSV at the right.
    {
        const int bh = barH(), x0 = scale(12);
        const std::wstring rows = std::to_wstring(rows_.size()) + (rows_.size() == 1 ? L" row" : L" rows");
        const std::wstring what = !tp ? L"" : host_.rowsPicked()                    ? L"the entries selected in the list"
                                 : dataIsMapTable(tp->schema().name) ? L"all entries of the tab"
                                                                     : L"all entries of the list (select 2 or more entries in the list to show only those)";
        c.text({x0, 0, w, bh}, tp ? datTitle(tp->schema().name) + L"   " : L"", kText, draw::kMiddle);
        const int tw = tp ? c.measure(datTitle(tp->schema().name) + L"   ") : 0;
        const std::wstring line = tp ? rows + L" \x00B7 " + what : L"";
        exportBtn_ = kitpaint::smallButtonRect(c, 0, 0, bh, L"Export CSV\x2026");
        exportBtn_ = {w - scale(12) - exportBtn_.width(), exportBtn_.top, w - scale(12), exportBtn_.bottom};
        importBtn_ = kitpaint::smallButtonRect(c, 0, 0, bh, L"Import CSV\x2026");
        importBtn_ = {exportBtn_.left - scale(8) - importBtn_.width(), importBtn_.top, exportBtn_.left - scale(8), importBtn_.bottom};
        columnsBtn_ = kitpaint::smallButtonRect(c, 0, 0, bh, L"Columns\x2026");
        columnsBtn_ = {importBtn_.left - scale(8) - columnsBtn_.width(), columnsBtn_.top, importBtn_.left - scale(8), columnsBtn_.bottom};
        changedBtn_ = kitpaint::smallButtonRect(c, 0, 0, bh, L"Changed");
        changedBtn_ = {columnsBtn_.left - scale(16) - changedBtn_.width(), changedBtn_.top, columnsBtn_.left - scale(16), changedBtn_.bottom};
        // (the filter box left of Changed, narrower when the view is narrow)
        const int fw = std::clamp(changedBtn_.left - scale(8) - (x0 + tw) - scale(120), scale(90), scale(200));
        const draw::Rect fr = tp ? draw::Rect{changedBtn_.left - scale(8) - fw, changedBtn_.top, changedBtn_.left - scale(8), changedBtn_.bottom} : draw::Rect{};
        if (fr.left != filter_.frame().left || fr.top != filter_.frame().top || fr.right != filter_.frame().right || fr.bottom != filter_.frame().bottom)
            filter_.setFrame(fr);
        filter_.setVisible(tp != nullptr);
        c.text({x0 + tw, 0, fr.left - scale(12), bh}, line, kDim, draw::kMiddle | draw::kEllipsis);
        if (tp) {
            draw::Host* hh = host();
            const draw::Point m = hh ? hh->mouse(*this) : draw::Point{-1, -1};
            kitpaint::drawButtonFace(c, changedBtn_, L"Changed", changedOnly_ ? kitpaint::Tone::Primary : kitpaint::Tone::Normal,
                                     changedBtn_.contains(m) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
            kitpaint::drawButtonFace(c, columnsBtn_, L"Columns\x2026", kitpaint::Tone::Normal, columnsBtn_.contains(m) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
            kitpaint::drawButtonFace(c, importBtn_, L"Import CSV\x2026", kitpaint::Tone::Normal, importBtn_.contains(m) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
            kitpaint::drawButtonFace(c, exportBtn_, L"Export CSV\x2026", kitpaint::Tone::Normal, exportBtn_.contains(m) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        } else {
            importBtn_ = exportBtn_ = columnsBtn_ = changedBtn_ = {};
        }
        c.fill({0, bh - 1, w, bh}, kBorder);
    }
    if (!tp) {
        c.text({scale(18), barH() + scale(20), w - scale(18), h}, L"The Table view shows the .dat tabs and Strings", kDim, draw::kWrap);
        return;
    }
    const DatTable& t = *tp;
    const DatSchema& sch = t.schema();
    const draw::Rect b = body();
    const int rh = rowH(), headTop = barH() + groupH(), fz = frozenW();
    const int open = host_.openEntry();
    draw::Host* hh = host();
    const draw::Point mouse = hh ? hh->mouse(*this) : draw::Point{-1, -1};
    const bool focused = hasFocus() || editing_;

    // A column's cells and headers, clipped to the part of the table it is in (the frozen part or the part that scrolls).
    auto columnSpan = [&](size_t k) { return k == 0 ? draw::Rect{idW(), 0, fz, h} : draw::Rect{fz, 0, b.right, h}; };
    const int first = sy_ / rh, last = std::min(int(rows_.size()), (sy_ + b.height()) / rh + 1);

    // The cells: the selection's fill, then the cell, the lines between them.
    for (size_t k = 0; k < cols_.size(); ++k) {
        const int x = colLeft(k), cw = colW(k);
        const draw::Rect span = columnSpan(k);
        if (x + cw <= span.left || x >= span.right) continue;
        c.pushClip({std::max(x, span.left), b.top, std::min(x + cw, span.right), b.bottom});
        for (int r = first; r < last; ++r) {
            const draw::Rect cell{x, b.top + r * rh - sy_, x + cw, b.top + (r + 1) * rh - sy_};
            if (sel_.has(r, int(k))) c.fill(cell, focused ? kSel : kRowHot);
            const bool hot = cell.contains(mouse);
            if (editing_ && sel_.has(r, int(k)) && !(r == sel_.row && int(k) == sel_.col) && dataCellLocked(t, cols_[k], rows_[size_t(r)]).empty()) {
                // (the text being typed, in each other selected cell: Enter puts it there)
                const draw::Font of = c.setFont(c.font({cols_[k].kind == DataCellKind::Number || cols_[k].kind == DataCellKind::Flags ? draw::Face::Mono : draw::Face::Ui}));
                c.text({cell.left + scale(6), cell.top, cell.right - scale(6), cell.bottom}, entry_.text(), kAccent,
                       draw::kMiddle | draw::kEllipsis | (cols_[k].kind == DataCellKind::Number ? draw::kRight : draw::kLeft));
                c.setFont(of);
            } else {
                paintCell(c, t, cols_[k], rows_[size_t(r)], cell, hot);
            }
            c.fill({cell.left, cell.bottom - 1, cell.right, cell.bottom}, kBorder, 110);
            c.fill({cell.right - 1, cell.top, cell.right, cell.bottom}, kBorder, 110);
        }
        c.popClip();
    }
    // The active cell: an outline in the accent color.
    if (sel_.row < int(rows_.size()) && sel_.col < int(cols_.size()) && !rows_.empty()) {
        const draw::Rect a = cellRect(sel_.row, sel_.col);
        const draw::Rect span = columnSpan(size_t(sel_.col));
        c.pushClip({span.left, b.top, span.right, b.bottom});
        c.roundBox(a, draw::kNone, focused ? kAccent : kBorderStrong, 0.0f, std::max(1, scale(2)));
        c.popClip();
    }
    // The row numbers: the open entry's with the accent bar, a selected row's lit.
    c.pushClip({0, b.top, idW(), b.bottom});
    for (int r = first; r < last; ++r) {
        const draw::Rect cell{0, b.top + r * rh - sy_, idW(), b.top + (r + 1) * rh - sy_};
        const int e = rows_[size_t(r)];
        bool any = false;
        for (const auto& g : sel_.ranges) any |= r >= g.top() && r <= g.bottom();
        c.fill(cell, any ? kCtlHot : kPanel);
        if (e == open) c.fill({0, cell.top, scale(2), cell.bottom}, kAccent);
        c.text({0, cell.top, idW() - scale(8), cell.bottom}, std::to_wstring(e), e == open ? kAccent : kDim, draw::kMiddle | draw::kRight);
        c.fill({0, cell.bottom - 1, idW(), cell.bottom}, kBorder, 110);
    }
    c.popClip();
    // The line at the frozen part's right edge.
    c.fill({fz - 1, barH(), fz, b.bottom}, kBorderStrong);

    // The headers: the groups (a section, a flag's field) over the column names; a selected column's name is lit.
    c.fill({0, barH(), b.right, b.top}, kPanel);
    const draw::Font headFont = uiFont;  // (the headers in the cells' size: one size in the whole table)
    for (int pass = 0; pass < 2; ++pass) {  // (the columns that scroll, then the frozen one over them)
        for (size_t k = pass ? 0 : 1; pass ? k < std::min<size_t>(1, cols_.size()) : k < cols_.size(); ++k) {
            const int x = colLeft(k), cw = colW(k);
            const draw::Rect span = columnSpan(k);
            if (x + cw <= span.left || x >= span.right) continue;
            c.pushClip({std::max(x, span.left), barH(), std::min(x + cw, span.right), b.top});
            const draw::Rect head{x, headTop, x + cw, b.top};
            bool selCol = false;
            for (const auto& g : sel_.ranges) selCol |= int(k) >= g.left() && int(k) <= g.right();
            if (selCol) c.fill(head, kCtlHot);
            c.setFont(headFont);
            const bool num = cols_[k].kind == DataCellKind::Number;
            const unsigned align = num ? draw::kRight : cols_[k].kind == DataCellKind::Bool ? draw::kCenter : draw::kLeft;
            const DatField* flagsOf = cols_[k].kind == DataCellKind::Flags ? dataSchemaField(sch, cols_[k].field) : nullptr;
            const bool sorted = !sortKey_.empty() && dataColumnKey(sch, cols_[k]) == sortKey_;
            if (sorted)  // (the sort's arrow at the header's left: up low to high, down high to low)
                kitpaint::drawGlyph(c, {head.left + scale(2), head.top, head.left + scale(14), head.bottom}, sortDown_ ? UiIcon::ChevronDown : UiIcon::ChevronUp, kAccent, 7);
            c.text({head.left + scale(sorted ? 16 : 6), head.top, flagsOf ? head.left + flagHexW() - scale(6) : head.right - scale(6), head.bottom},
                   dataColumnTitle(sch, cols_[k]), sorted || selCol ? kText : kDim, align | draw::kMiddle | draw::kEllipsis);
            if (flagsOf) {  // (each flag's name over its box)
                int fx = head.left + flagHexW();
                for (size_t fb = 0; fb < flagsOf->flags.size() && fb < 32; ++fb) {
                    const int fw = flagW(flagsOf->index, fb);
                    const bool lit = hot_.col == int(k) && hot_.bit == int(fb);
                    c.fill({fx, head.top + scale(6), fx + 1, head.bottom - scale(6)}, kBorder, 110);
                    c.text({fx + scale(3), head.top, fx + fw - scale(3), head.bottom}, widen(flagsOf->flags[fb]), lit || selCol ? kText : kDim,
                           draw::kCenter | draw::kMiddle | draw::kEllipsis);
                    fx += fw;
                }
            }
            // The group's name over the first column of its run (the run's line at its left).
            const std::wstring group = dataColumnGroup(sch, cols_[k]);
            const bool starts = k <= 1 || group != dataColumnGroup(sch, cols_[k - 1]);
            if (!group.empty() && starts) {
                int runW = 0;
                for (size_t j = k; j < cols_.size() && dataColumnGroup(sch, cols_[j]) == group; ++j) runW += colW(j);
                c.popClip();
                c.pushClip({std::max(x, span.left), barH(), span.right, headTop});
                c.text({x + scale(6), barH(), x + runW - scale(6), headTop}, group, kFaint, draw::kMiddle | draw::kEllipsis);
                c.popClip();
                c.pushClip({std::max(x, span.left), barH(), std::min(x + cw, span.right), b.top});
                c.fill({x, barH() + scale(4), x + 1, b.top}, kBorder);
            }
            c.fill({head.right - 1, headTop + scale(4), head.right, head.bottom - scale(4)}, kBorder);
            c.setFont(uiFont);
            c.popClip();
        }
    }
    c.setFont(headFont);
    c.text({0, headTop, idW() - scale(8), b.top}, L"Entry", kDim, draw::kMiddle | draw::kRight);
    c.setFont(uiFont);
    c.fill({0, b.top - 1, b.right, b.top}, kBorder);
    c.fill({fz - 1, barH(), fz, b.top}, kBorderStrong);
    if (rows_.empty())
        c.text({scale(18), b.top + scale(16), b.right - scale(18), b.bottom}, L"No entries. Clear the search box or the Changed filter of the list.", kDim, draw::kWrap);
    else if (cols_.size() <= 1 && filtered())
        c.text({fz + scale(18), b.top + scale(16), b.right - scale(18), b.bottom},
               changedOnly_ && filter_.text().empty() ? L"No column has a changed value." : L"No column has this text in its name. Clear the filter box or Changed.", kDim,
               draw::kWrap);

    if (drag_ == Drag::Move && dropAt_ >= 1) {  // (a column's move: a line where it goes)
        const int x = dropAt_ < int(cols_.size()) ? colLeft(size_t(dropAt_)) : colLeft(cols_.size() - 1) + colW(cols_.size() - 1);
        if (x >= fz - 1) c.fill({x - scale(1), barH(), x + scale(2), b.bottom}, kAccent);
    }
    paintFoot(c, t);
    // The bars follow the table's size.
    vbar_.setRange(int(rows_.size()) * rh, b.height());
    vbar_.setPos(sy_);
    hbar_.setRange(scrollW(), std::max(1, b.width() - fz));
    hbar_.setPos(sx_);
    placeEntry();
}

inline bool DataTableView::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    sync();
    switch (e.kind) {
    case MouseKind::Move: {
        if (drag_ == Drag::Width) {
            const int nw = std::max(scale(36), dragW0_ + e.pt.x - dragX_);
            const DataTableColumn& col = cols_[size_t(dragCol_)];
            if (const DatTable* t = host_.table()) dataTableLayout(dat_).widths[dataColumnKey(t->schema(), col)] = std::max(36, nw * 96 / std::max(1, scale(96)));
            clampScroll();
            invalidate();
            return true;
        }
        if (drag_ == Drag::Move) {
            const int at = dropIndex(e.pt.x);
            if (at != dropAt_) dropAt_ = at, invalidate();
            return true;
        }
        const Hit hit = hitAt(e.pt);
        if (drag_ == Drag::Cells && hit.row >= 0 && hit.col >= 0) {
            if (hit.row != sel_.row || hit.col != sel_.col) sel_.click(hit.row, hit.col, draw::kShift), reveal(hit.row, hit.col);
        } else if (drag_ == Drag::Columns && hit.col >= 0) {
            if (hit.col != sel_.col) sel_.columns(hit.col, draw::kShift, int(rows_.size()));
        } else if (drag_ == Drag::Rows && hit.row >= 0) {
            if (hit.row != sel_.row) sel_.rows(hit.row, draw::kShift, int(cols_.size()));
        }
        if (hit.row != hot_.row || hit.col != hot_.col || hit.check != hot_.check || hit.arrow != hot_.arrow || hit.bit != hot_.bit || drag_ != Drag::None) hot_ = hit, invalidate();
        if (e.pt.y < barH()) invalidate();  // (the buttons light)
        return true;
    }
    case MouseKind::Leave:
        hot_ = {};
        invalidate();
        return true;
    case MouseKind::Wheel: {
        const bool across = e.horizontal || (e.mods & draw::kShift);
        const int d = int(std::lround(e.exact ? (across ? e.panX : e.panY) : e.wheel * float(rowH() * 3)));
        if (across) sx_ -= e.horizontal && !e.exact ? -d : d;
        else sy_ -= d;
        clampScroll();
        invalidate();
        return true;
    }
    case MouseKind::Down:
    case MouseKind::DoubleClick: {
        if (e.button != 0 && e.button != 1) return false;
        if (editing_) commitEdit(false);
        focus();
        if (e.button == 0 && columnsBtn_.contains(e.pt)) return pickColumns({columnsBtn_.left, columnsBtn_.bottom + scale(2)}), true;
        if (e.button == 0 && changedBtn_.contains(e.pt)) return setChangedOnly(!changedOnly_), true;
        if (e.button == 0 && importBtn_.contains(e.pt)) return host_.importCsv(), true;
        if (e.button == 0 && exportBtn_.contains(e.pt)) return host_.exportCsv(), true;
        const Hit hit = hitAt(e.pt);
        const unsigned mods = e.mods & (draw::kCtrl | draw::kShift);
        if (e.button == 1) {  // the right button: what it is on becomes the selection (unless it is selected), then its menu
            if (hit.header && hit.col >= 0) {
                bool whole = false;
                for (int c : selectedColumns(hit.col)) whole |= c == hit.col && selectedColumns(hit.col).size() > 1;
                if (!whole) sel_.columns(hit.col, 0, int(rows_.size()));
            } else if (hit.number && hit.row >= 0) {
                if (!sel_.has(hit.row, 0)) sel_.rows(hit.row, 0, int(cols_.size()));
            } else if (hit.row >= 0 && hit.col >= 0 && !sel_.has(hit.row, hit.col)) {
                sel_.click(hit.row, hit.col, 0);
            }
            invalidate();
            if ((hit.header && hit.col >= 0) || hit.row >= 0) contextMenu(hit, e.pt);
            return true;
        }
        if (hit.header && hit.edge && e.kind == MouseKind::DoubleClick) {  // (as spreadsheets: the width of its text)
            fitDue_ = selectedColumns(hit.col);
            invalidate();
            return true;
        } else if (hit.header && hit.edge) {
            drag_ = Drag::Width, dragCol_ = hit.col, dragX_ = e.pt.x, dragW0_ = colW(size_t(hit.col));
        } else if (hit.header && hit.col >= 0) {
            if (e.kind == MouseKind::DoubleClick && !mods) {  // (a double click: ascending, then descending, then the default order)
                const bool same = !sortKey_.empty() && dataColumnKey(host_.table()->schema(), cols_[size_t(hit.col)]) == sortKey_;
                if (same && sortDown_) return sortBy(-1, false), true;
                return sortBy(hit.col, same), true;
            }
            const std::vector<int> picked = selectedColumns(hit.col);
            const bool whole = std::find(picked.begin(), picked.end(), hit.col) != picked.end() && sel_.has(0, hit.col) &&
                               sel_.has(std::max(0, int(rows_.size()) - 1), hit.col) && rows_.size() > 0;
            if (whole && !mods && hit.col > 0 && cols_[size_t(hit.col)].kind != DataCellKind::Name && !filtered()) {
                drag_ = Drag::Move, dragCol_ = hit.col, dropAt_ = -1;  // (a selected column's header: drag it to its new place)
            } else {
                sel_.columns(hit.col, mods, int(rows_.size()));
                drag_ = Drag::Columns;
            }
        } else if (hit.header && e.pt.x < idW()) {  // ("Entry": the list's order)
            if (!sortKey_.empty()) sortBy(-1, false);
            return true;
        } else if (hit.number) {
            if (e.kind == MouseKind::DoubleClick) return host_.open(rows_[size_t(hit.row)]), true;
            sel_.rows(hit.row, mods, int(cols_.size()));
            drag_ = Drag::Rows;
        } else if (hit.row >= 0 && hit.col >= 0) {
            const DataTableColumn& col = cols_[size_t(hit.col)];
            const bool wasActive = sel_.single() && sel_.row == hit.row && sel_.col == hit.col;
            if (!(hit.check && sel_.has(hit.row, hit.col) && !mods)) sel_.click(hit.row, hit.col, mods);
            if (hit.check && !mods && hit.bit >= 0) toggleFlag(hit.row, hit.col, hit.bit);  // (a flag's box: that flag of the selected cells)
            else if (hit.check && !mods) toggleChecks(hit.row, hit.col);  // (a click on the box: the selected boxes toggle with it)
            else if (hit.arrow && !mods && e.kind == MouseKind::Down) openPick(hit.row, hit.col);
            else if (e.kind == MouseKind::DoubleClick && col.kind == DataCellKind::Name && !dataNameString(dat_, rows_[size_t(hit.row)]))
                return host_.open(rows_[size_t(hit.row)]), true;
            else if ((e.kind == MouseKind::DoubleClick || wasActive) && col.kind == DataCellKind::Pick && !mods)
                openPick(hit.row, hit.col);
            else if ((e.kind == MouseKind::DoubleClick || wasActive) && col.kind != DataCellKind::Bool)
                beginEdit(dataCellData(*host_.table(), col, rows_[size_t(hit.row)]), true);
            else drag_ = Drag::Cells;
        } else {
            return true;
        }
        if (drag_ != Drag::None)
            if (draw::Host* h = host()) h->capture(*this, true);
        invalidate();
        return true;
    }
    case MouseKind::Up:
        if (drag_ != Drag::None) {
            const Drag was = std::exchange(drag_, Drag::None);
            if (draw::Host* h = host()) h->capture(*this, false);
            if (was == Drag::Width) dataTableLayoutSave(dat_);  // (the width is kept in the project)
            if (was == Drag::Move && dropAt_ >= 0) moveColumns(selectedColumns(dragCol_), dropAt_);
            dropAt_ = -1;
            invalidate();
        }
        return e.button == 0;
    default: return false;
    }
}

inline draw::Cursor DataTableView::cursor(draw::Point pt) {
    const Hit hit = hitAt(pt);
    if (drag_ == Drag::Width || (hit.header && hit.edge)) return draw::Cursor::SizeWE;
    if (drag_ == Drag::Move) return draw::Cursor::SizeAll;
    if (importBtn_.contains(pt) || exportBtn_.contains(pt) || columnsBtn_.contains(pt) || changedBtn_.contains(pt) || hit.check || hit.arrow)
        return draw::Cursor::Hand;
    return draw::Cursor::Arrow;
}

inline std::wstring DataTableView::tip(draw::Point pt) {
    if (importBtn_.contains(pt)) return L"Import the values of a CSV file into this .dat file. The \"id\" column gives the entries. The first row gives the columns.";
    if (exportBtn_.contains(pt)) return L"Export the rows and the columns that show as a CSV file";
    if (columnsBtn_.contains(pt)) return L"Select the columns that show. A right click on a column's header can hide it.";
    if (changedBtn_.contains(pt)) return L"Show only the columns that have a value that is not the game's";
    const Hit hit = hitAt(pt);
    const DatTable* t = host_.table();
    if (!t || hit.col < 0) return L"";
    const DataTableColumn& col = cols_[size_t(hit.col)];
    if (hit.header) {
        if (const DatField* f = col.kind == DataCellKind::Flags ? dataSchemaField(t->schema(), col.field) : nullptr; f && hit.bit >= 0) {
            wchar_t mask[16];
            std::swprintf(mask, 16, L"0x%X", 1u << hit.bit);
            return datLabel(f->name) + L": " + widen(f->flags[size_t(hit.bit)]) + L" (bit " + std::to_wstring(hit.bit) + L", " + mask + L")";
        }
        return dataColumnTitle(t->schema(), col) +
               L"\nA click selects the column, a drag of a selected column moves it. A double click sorts by it: ascending, descending, then the default order. A drag of its right edge changes its width, a double click on the edge fits the width to the text. The right button hides it.";
    }
    if (hit.row < 0) return L"";
    const int e = rows_[size_t(hit.row)];
    if (const std::wstring why = dataCellLocked(*t, col, e); !why.empty()) return why;
    if (col.kind == DataCellKind::Pick) {
        const DatField* f = dataSchemaField(t->schema(), col.field);
        return f ? host_.note(*f, t->value(*f, e)) + L"   #" + std::to_wstring(t->value(*f, e)) : L"";
    }
    if (col.kind == DataCellKind::Flags) {
        const DatField* f = dataSchemaField(t->schema(), col.field);
        if (!f) return L"";
        if (hit.bit >= 0) return widen(f->flags[size_t(hit.bit)]) + (((t->value(*f, e) >> hit.bit) & 1) ? L": on" : L": off");
        return dataFlagsText(*f, t->value(*f, e));
    }
    if (col.kind == DataCellKind::Name) return dataRowName(t->schema().name, e);
    return L"";
}

inline void DataTableView::moveActive(int dr, int dc, bool shift) {
    sel_.move(dr, dc, shift, int(rows_.size()), int(cols_.size()));
    reveal(sel_.row, sel_.col);
}

inline bool DataTableView::key(const draw::KeyEvent& e) {
    if (!e.down || filter_.hasFocus()) return false;  // (the filter box's keys that it does not use: not the table's)
    if (editing_) {  // (the keys that the text box does not use)
        const bool moves = e.key == draw::Key::Up || e.key == draw::Key::Down || e.key == draw::Key::PageUp || e.key == draw::Key::PageDown;
        if (!moves || (e.mods & (draw::kShift | draw::kAlt))) return !(e.mods & draw::kCtrl);  // (Ctrl+S and the other menu keys go on)
        commitEdit(false);
        focus();
    }
    sync();
    if (rows_.empty() || cols_.empty()) return false;
    const bool ctrl = (e.mods & draw::kCtrl) != 0, shift = (e.mods & draw::kShift) != 0;
    if (e.mods & draw::kAlt) {
        if (e.key != draw::Key::Down) return false;  // (Alt+Down: the dropdown's list, or the list of flags)
        if (cols_[size_t(sel_.col)].kind == DataCellKind::Flags) openFlags(sel_.row, sel_.col);
        else openPick(sel_.row, sel_.col);
        return true;
    }
    const int page = std::max(1, body().height() / rowH() - 1);
    switch (e.key) {
    case draw::Key::Up: moveActive(ctrl ? -sel_.row : -1, 0, shift); return true;
    case draw::Key::Down: moveActive(ctrl ? int(rows_.size()) : 1, 0, shift); return true;
    case draw::Key::Left: moveActive(0, ctrl ? -sel_.col : -1, shift); return true;
    case draw::Key::Right: moveActive(0, ctrl ? int(cols_.size()) : 1, shift); return true;
    case draw::Key::PageUp: moveActive(-page, 0, shift); return true;
    case draw::Key::PageDown: moveActive(page, 0, shift); return true;
    case draw::Key::Home: moveActive(ctrl ? -sel_.row : 0, -sel_.col, shift); return true;
    case draw::Key::End: moveActive(ctrl ? int(rows_.size()) : 0, int(cols_.size()), shift); return true;
    case draw::Key::Tab:
        if (ctrl) return false;
        moveActive(0, shift ? -1 : 1, false);
        return true;
    case draw::Key::Enter:
    case draw::Key::F2: {
        const DataTableColumn& col = cols_[size_t(sel_.col)];
        if (col.kind == DataCellKind::Bool) {
            if (e.key == draw::Key::Enter) moveActive(shift ? -1 : 1, 0, false);
            return true;
        }
        if (col.kind == DataCellKind::Pick) return openPick(sel_.row, sel_.col), true;
        beginEdit(dataCellData(*host_.table(), col, rows_[size_t(sel_.row)]), true);
        return true;
    }
    case draw::Key::Space:
        if (ctrl) return sel_.columns(sel_.col, 0, int(rows_.size())), invalidate(), true;  // (as spreadsheets: Ctrl+Space the column,
        if (shift) return sel_.rows(sel_.row, 0, int(cols_.size())), invalidate(), true;   // Shift+Space the row)
        if (cols_[size_t(sel_.col)].kind == DataCellKind::Flags) return openFlags(sel_.row, sel_.col), true;
        toggleChecks(sel_.row, sel_.col);
        return true;
    case draw::Key::Delete:
    case draw::Key::Backspace: resetSelected(); return true;
    case draw::Key::Escape: return false;
    default: break;
    }
    if (!ctrl) return false;
    switch (int(e.key)) {
    case 'A': sel_.all(int(rows_.size()), int(cols_.size())), invalidate(); return true;
    case 'C': copy(CopyAs::Values); return true;
    case 'V':
        if (draw::Host* h = host()) paste(widen(h->clipboardText()));
        return true;
    case 'D': fillDown(); return true;
    default: return false;
    }
}

inline bool DataTableView::text(const draw::TextEvent& e) {
    if (editing_ || e.utf8.empty() || rows_.empty() || cols_.empty()) return false;
    const std::wstring w = widen(e.utf8);
    if (w == L" " || w[0] < 0x20) return false;
    const DataTableColumn& col = cols_[size_t(sel_.col)];
    if (col.kind == DataCellKind::Bool) {
        bool yes = false;  // (a check box: 1, 0, y, n, x set every selected box)
        if (dataReadYesNo(w, yes)) setChecks(yes);
        return true;
    }
    if (col.kind == DataCellKind::Pick) return openPick(sel_.row, sel_.col, w), true;  // (the list, searching for what is typed)
    beginEdit(w, false);
    return true;
}

inline void DataTableView::beginEdit(const std::wstring& start, bool selectAll) {
    const DatTable* t = host_.table();
    if (!t || rows_.empty()) return;
    const DataTableColumn& col = cols_[size_t(sel_.col)];
    // A cell that cannot change takes no text, unless other selected cells can.
    if (sel_.single()) {
        if (const std::wstring why = dataCellLocked(*t, col, rows_[size_t(sel_.row)]); !why.empty()) return host_.status(why);
    }
    reveal(sel_.row, sel_.col);
    if (col.kind == DataCellKind::Name) {  // (a game text: the pop-out with the colors; a typed letter starts a new text)
        const int sid = dataNameString(dat_, rows_[size_t(sel_.row)]);
        if (!sid) return host_.status(dataCellLocked(*t, col, rows_[size_t(sel_.row)]));
        return editName(selectAll ? dataExtras().strings.text(sid - 1) : narrow(start));
    }
    editing_ = true;
    const bool name = col.kind == DataCellKind::Name;
    entry_.field().setAlignRight(col.kind == DataCellKind::Number || col.kind == DataCellKind::Flags);
    entry_.field().setFont({name || col.kind == DataCellKind::Pick ? draw::Face::Ui : draw::Face::Mono, 0});
    entry_.setText(start);
    entry_.setVisible(true);
    placeEntry();
    entry_.focus();
    if (selectAll) entry_.select();
    else entry_.field().model().setCaret(start.size());
    invalidate();
}

inline void DataTableView::placeEntry() {
    if (!editing_) return;
    const draw::Rect a = cellRect(sel_.row, sel_.col);
    const bool hex = sel_.col < int(cols_.size()) && cols_[size_t(sel_.col)].kind == DataCellKind::Flags;  // (a field of flags: over its hex)
    const draw::Rect r{a.left + 1, a.top + 1, hex ? a.left + flagHexW() - 1 : a.right - 1, a.bottom - 1};
    const draw::Rect cur = entry_.frame();
    if (cur.left != r.left || cur.top != r.top || cur.right != r.right || cur.bottom != r.bottom) entry_.setFrame(r);
}

inline void DataTableView::commitEdit(bool down) {
    if (!editing_) return;
    editing_ = false;
    const std::wstring text = entry_.text();
    entry_.setVisible(false);
    setSelected(text);
    if (down && sel_.single()) moveActive(1, 0, false);
    invalidate();
}

inline void DataTableView::report(const std::wstring& verb, int changed, int bad, int locked) {
    std::wstring s = changed ? verb + L" " + std::to_wstring(changed) + (changed == 1 ? L" value" : L" values") : L"No value changed";
    if (bad) s += L"; " + std::to_wstring(bad) + (bad == 1 ? L" is not a value of its column" : L" are not values of their columns");
    if (locked) s += L"; " + std::to_wstring(locked) + L" cannot change";
    if (changed) host_.saved(s);
    else host_.status(s);
}

inline void DataTableView::setSelected(const std::wstring& text) {
    DatTable* t = host_.table();
    if (!t || dataTrim(text).empty()) return;
    // "+10", "-10", "*1.5", "/2": each number changes by the step (a name stays as it is).
    wchar_t op = 0;
    double n = 0;
    const bool step = dataReadStep(text, op, n);
    int changed = 0, bad = 0, locked = 0;
    for (const auto& [r, c] : sel_.cells()) {
        const DataTableColumn& col = cols_[size_t(c)];
        if (step && col.kind == DataCellKind::Name) continue;
        const DataCellSet s = step && col.kind != DataCellKind::Name ? dataCellStep(*t, col, rows_[size_t(r)], op, n)
                                                                      : dataCellSet(*t, col, rows_[size_t(r)], text);
        changed += s == DataCellSet::Changed, bad += s == DataCellSet::NotValue, locked += s == DataCellSet::Locked;
    }
    report(step ? L"Changed" : L"Set", changed, bad, locked);
    invalidate();
}

// The check boxes among the selected cells: all on when the box of cell (r, c) is off, else all off.
inline void DataTableView::toggleChecks(int r, int c) {
    const DatTable* t = host_.table();
    if (!t || r < 0 || c < 0 || r >= int(rows_.size()) || c >= int(cols_.size())) return;
    const DataTableColumn& col = cols_[size_t(c)];
    bool on = true;
    if (col.kind == DataCellKind::Bool) on = dataCellData(*t, col, rows_[size_t(r)]) == L"0";
    setChecks(on);
}
inline void DataTableView::setChecks(bool on) {
    DatTable* et = host_.table();
    if (!et) return;
    int changed = 0, locked = 0;
    for (const auto& [r, c] : sel_.cells()) {
        const DataTableColumn& cc = cols_[size_t(c)];
        if (cc.kind != DataCellKind::Bool) continue;
        const DataCellSet s = dataCellSet(*et, cc, rows_[size_t(r)], on ? L"1" : L"0");
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked;
    }
    report(on ? L"Checked" : L"Cleared", changed, 0, locked);
    invalidate();
}

// A flag's box in cell (r, c): that flag on in every selected cell of the field when it is off in this one, else off;
// one undo step. (The hex shows the new value.)
inline void DataTableView::toggleFlag(int r, int c, int bit) {
    DatTable* t = host_.table();
    if (!t || r < 0 || c < 0 || r >= int(rows_.size()) || c >= int(cols_.size()) || cols_[size_t(c)].kind != DataCellKind::Flags || bit < 0 || bit >= 32) return;
    const DatField* f = dataSchemaField(t->schema(), cols_[size_t(c)].field);
    const int e = rows_[size_t(r)];
    if (!f || !f->covers(e)) return;
    if (const std::wstring why = dataCellLocked(*t, cols_[size_t(c)], e); !why.empty()) return host_.status(why);
    const uint32_t mask = 1u << bit;
    const bool on = !(t->value(*f, e) & mask);
    int changed = 0, locked = 0;
    for (const auto& [sr, sc] : sel_.cells()) {
        if (cols_[size_t(sc)].kind != DataCellKind::Flags || cols_[size_t(sc)].field != f->index) continue;
        const int se = rows_[size_t(sr)];
        if (!f->covers(se)) continue;
        const uint32_t old = t->value(*f, se);
        const DataCellSet s = dataCellSet(*t, cols_[size_t(sc)], se, dataFlagsHex(*f, on ? old | mask : old & ~mask));
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked;
    }
    report((on ? L"Checked " : L"Cleared ") + widen(f->flags[size_t(bit)]) + L" in", changed, 0, locked);
    invalidate();
}

// The width of each flag's box: its name in the header, within limits (measured again for another .dat or scale).
inline void DataTableView::measureFlags(draw::Canvas& c, const DatSchema& sch) {
    if (flagWsDat_ == sch.name && flagWsScale_ == scale(100)) return;
    flagWs_.clear(), flagWsDat_ = sch.name, flagWsScale_ = scale(100);
    const draw::Font was = c.setFont(c.font({draw::Face::Ui}));
    for (const DatField& f : sch.fields) {
        if (f.flags.empty()) continue;
        std::vector<int>& ws = flagWs_[f.index];
        for (size_t b = 0; b < f.flags.size() && b < 32; ++b) ws.push_back(std::clamp(c.measure(widen(f.flags[b])) + scale(14), scale(30), scale(130)));
    }
    c.setFont(was);
}

inline void DataTableView::resetSelected() {
    DatTable* t = host_.table();
    if (!t) return;
    int changed = 0, locked = 0;
    for (const auto& [r, c] : sel_.cells()) {
        const DataCellSet s = dataCellReset(*t, cols_[size_t(c)], rows_[size_t(r)]);
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked && sel_.single();
    }
    if (changed) host_.saved(L"Reset " + std::to_wstring(changed) + (changed == 1 ? L" value" : L" values") + L" to the game's");
    else host_.status(locked ? L"This value cannot change" : L"The selected values are the game's");
    invalidate();
}

// Ctrl+D: the top cell of each selected column into the selected cells under it.
inline void DataTableView::fillDown() {
    DatTable* t = host_.table();
    if (!t) return;
    int changed = 0, bad = 0, locked = 0;
    for (const auto& g : sel_.ranges)
        for (int c = g.left(); c <= g.right(); ++c) {
            const std::wstring top = dataCellData(*t, cols_[size_t(c)], rows_[size_t(g.top())]);
            for (int r = g.top() + 1; r <= g.bottom(); ++r) {
                const DataCellSet s = dataCellSet(*t, cols_[size_t(c)], rows_[size_t(r)], top);
                changed += s == DataCellSet::Changed, bad += s == DataCellSet::NotValue, locked += s == DataCellSet::Locked;
            }
        }
    report(L"Filled down", changed, bad, locked);
    invalidate();
}

// A dropdown's list for the active cell: the picked value goes into every selected cell of the same field.
inline void DataTableView::openPick(int r, int c, const std::wstring& search) {
    DatTable* t = host_.table();
    if (!t || r < 0 || c < 0 || cols_[size_t(c)].kind != DataCellKind::Pick) return;
    const DatField* f = dataSchemaField(t->schema(), cols_[size_t(c)].field);
    const int e = rows_[size_t(r)];
    if (!f || !f->covers(e)) return;
    if (const std::wstring why = dataCellLocked(*t, cols_[size_t(c)], e); !why.empty()) return host_.status(why);
    uint32_t v = 0;
    if (!host_.choose(*f, t->value(*f, e), cellRect(r, c), v, search)) return;
    int changed = 0, locked = 0;
    for (const auto& [sr, sc] : sel_.cells()) {
        if (cols_[size_t(sc)].field != f->index) continue;
        const DataCellSet s = dataCellSet(*t, cols_[size_t(sc)], rows_[size_t(sr)], std::to_wstring(v));
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked;
    }
    report(L"Set", changed, 0, locked);
    invalidate();
}

// A field of flags: the list of its flags, the active cell's checked. OK puts them into every selected cell of that
// field; one undo step.
inline void DataTableView::openFlags(int r, int c) {
    DatTable* t = host_.table();
    if (!t || r < 0 || c < 0 || r >= int(rows_.size()) || c >= int(cols_.size()) || cols_[size_t(c)].kind != DataCellKind::Flags) return;
    const DatField* f = dataSchemaField(t->schema(), cols_[size_t(c)].field);
    const int e = rows_[size_t(r)];
    if (!f || !f->covers(e)) return;
    if (const std::wstring why = dataCellLocked(*t, cols_[size_t(c)], e); !why.empty()) return host_.status(why);
    const uint32_t v = t->value(*f, e);
    std::vector<ListPickItem> items;
    std::set<std::string> checked;
    for (size_t b = 0; b < f->flags.size() && b < 32; ++b) {
        items.push_back({widen(f->flags[b]), L"", std::to_string(b)});
        if ((v >> b) & 1) checked.insert(std::to_string(b));
    }
    const draw::Rect cell = cellRect(r, c);
    if (!host_.pickMany(datLabel(f->name), std::move(items), checked, {cell.left, cell.bottom})) return;
    uint32_t next = 0;
    for (const std::string& b : checked) next |= 1u << std::stoi(b);
    const uint32_t named = dataNamedFlags(*f);
    int changed = 0, locked = 0;
    for (const auto& [sr, sc] : sel_.cells()) {
        if (cols_[size_t(sc)].kind != DataCellKind::Flags || cols_[size_t(sc)].field != f->index) continue;
        const int se = rows_[size_t(sr)];
        const uint32_t keep = f->covers(se) ? t->value(*f, se) & ~named : 0;  // (each cell's bits without a name stay)
        const DataCellSet s = dataCellSet(*t, cols_[size_t(sc)], se, dataFlagsHex(*f, keep | next));
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked;
    }
    report(L"Set", changed, 0, locked);
    invalidate();
}

// The selected cells as text: the rectangle round them, a cell that is not selected empty. Values: tab-separated, as
// spreadsheets copy. Headers: a first row with the columns' names (hit_points: dataColumnKey) and a first column with
// the ids, as Export CSV writes them (a paste of it puts each value into its entry and its column). Csv: the same,
// separated by commas.
inline std::wstring DataTableView::copyText(CopyAs as) {
    const DatTable* t = host_.table();
    if (!t || rows_.empty() || cols_.empty()) return L"";
    const DataCellRange b = sel_.bounds();
    const bool names = as != CopyAs::Values;
    DataGrid grid;
    if (names) {
        grid.push_back({L"id"});
        for (int c = b.left(); c <= b.right(); ++c) grid.back().push_back(dataColumnKey(t->schema(), cols_[size_t(c)]));
    }
    for (int r = b.top(); r <= b.bottom(); ++r) {
        grid.emplace_back();
        if (names) grid.back().push_back(std::to_wstring(rows_[size_t(r)]));
        for (int c = b.left(); c <= b.right(); ++c) grid.back().push_back(sel_.has(r, c) ? dataCellData(*t, cols_[size_t(c)], rows_[size_t(r)]) : L"");
    }
    return dataGridText(grid, as == CopyAs::Csv ? L',' : L'\t');
}
inline void DataTableView::copy(CopyAs as) {
    if (draw::Host* h = host()) h->setClipboardText(narrow(copyText(as)));
    const std::wstring n = std::to_wstring(sel_.cells().size());
    host_.status(as == CopyAs::Csv ? L"Copied " + n + L" cells as CSV" : as == CopyAs::Headers ? L"Copied " + n + L" cells with headers" : L"Copied " + n + L" cells");
}

// Pastes text at the selection. One value: into every selected cell. A block of values (tab-separated): from the
// selection's top left cell. A block with a row of column names (Copy with headers, Copy as CSV, a CSV file's text; tabs,
// commas or semicolons): each value into the column that its name gives, also when the columns show in another order
// or not at all; with an "id" column, into the entry that it gives (as Import CSV), else into the rows from the
// selection's top row.
inline void DataTableView::paste(const std::wstring& text) {
    DatTable* t = host_.table();
    if (!t || rows_.empty() || cols_.empty()) return;
    const DatSchema& sch = t->schema();
    DataGrid grid = dataGridRead(text, L'\t');
    if (text.find(L'\t') == std::wstring::npos) {  // (no tab: a CSV text with its column names, if its first row has them)
        const DataGrid csv = dataGridRead(text, dataGridSeparator(text));
        if (csv.size() > 1 && csv[0].size() > 1 && dataGridIsHeader(sch, csv[0])) grid = csv;
    }
    if (grid.empty()) return host_.status(L"The clipboard has no text to paste");
    if (grid.size() > 1 && dataGridIsHeader(sch, grid[0])) {
        int idCol = -1;
        for (size_t k = 0; k < grid[0].size(); ++k)
            if (dataLower(dataTrim(grid[0][k])) == L"id") idCol = int(k);
        if (idCol >= 0) {  // (the entries by their ids: as Import CSV)
            const DataImportResult res = dataTableImport(*t, grid);
            report(L"Pasted", res.changed, res.notValues, res.locked);
            invalidate();
            return;
        }
        const std::vector<DataTableColumn> all = dataTableColumns(sch);
        std::vector<int> colOf;
        for (const std::wstring& name : grid[0]) colOf.push_back(dataColumnNamed(sch, all, name));
        const int top = sel_.bounds().top();
        int changed = 0, bad = 0, locked = 0;
        for (size_t i = 1; i < grid.size() && top + int(i) - 1 < int(rows_.size()); ++i)
            for (size_t j = 0; j < grid[i].size() && j < colOf.size(); ++j) {
                if (colOf[j] < 0) continue;
                const DataTableColumn& col = all[size_t(colOf[j])];
                const int e = rows_[size_t(top) + i - 1];
                if (col.kind == DataCellKind::Name && !dataNameString(sch.name, e)) continue;  // (a name from the game's list: not a value)
                const DataCellSet s = dataCellSet(*t, col, e, grid[i][j]);
                changed += s == DataCellSet::Changed, bad += s == DataCellSet::NotValue, locked += s == DataCellSet::Locked;
            }
        report(L"Pasted", changed, bad, locked);
        invalidate();
        return;
    }
    if (grid.size() == 1 && grid[0].size() == 1) return setSelected(grid[0][0]);  // one value: into every selected cell
    // A block: from the selection's top left cell, as far as the table goes.
    const DataCellRange b = sel_.bounds();
    int changed = 0, bad = 0, locked = 0;
    for (size_t i = 0; i < grid.size() && b.r0 + int(i) < int(rows_.size()); ++i)
        for (size_t j = 0; j < grid[i].size() && b.c0 + int(j) < int(cols_.size()); ++j) {
            const DataCellSet s = dataCellSet(*t, cols_[size_t(b.c0) + j], rows_[size_t(b.r0) + i], grid[i][j]);
            changed += s == DataCellSet::Changed, bad += s == DataCellSet::NotValue, locked += s == DataCellSet::Locked;
        }
    sel_.ranges = {{b.r0, b.c0, std::min(int(rows_.size()) - 1, b.r0 + int(grid.size()) - 1), std::min(int(cols_.size()) - 1, b.c0 + int(grid[0].size()) - 1)}};
    report(L"Pasted", changed, bad, locked);
    invalidate();
}

// The columns that are selected from the top row to the bottom one, with column c; c only when c is not one of them.
inline std::vector<int> DataTableView::selectedColumns(int c) const {
    std::set<int> out;
    for (const auto& g : sel_.ranges)
        if (g.top() == 0 && g.bottom() >= int(rows_.size()) - 1)
            for (int k = g.left(); k <= g.right(); ++k) out.insert(k);
    if (!out.count(c)) return {c};
    return {out.begin(), out.end()};
}

// The right button's menu: Reset (the selected cells), Copy and Paste; on a row's number Open the entry; on a
// column's header Hide, Show all columns and Columns.
inline void DataTableView::contextMenu(const Hit& hit, draw::Point pt) {
    const DatTable* t = host_.table();
    if (!t || rows_.empty() || cols_.empty()) return;
    MenuModel m;
    auto item = [&m](int id, const std::wstring& label, const std::wstring& shortcut = L"", bool enabled = true) {
        MenuItem it;
        it.id = id, it.label = label, it.shortcut = shortcut, it.grayed = !enabled;
        m.push_back(std::move(it));
    };
    auto separator = [&m] {
        MenuItem it;
        it.kind = MenuItem::Kind::Separator;
        m.push_back(std::move(it));
    };
    bool changed = false;
    const std::vector<std::pair<int, int>> cells = sel_.cells();
    for (const auto& [r, c] : cells) changed |= dataCellChanged(*t, cols_[size_t(c)], rows_[size_t(r)]);
    item(1, L"&Reset to the game's values", L"Del", changed);
    separator();
    item(2, L"&Copy", L"Ctrl+C");
    item(14, L"Copy &with headers");
    item(16, L"Copy as C&SV");
    item(3, L"&Paste", L"Ctrl+V");
    item(15, L"Fill &down", L"Ctrl+D", sel_.bounds().bottom() > sel_.bounds().top());
    const std::vector<int> hide = hit.header ? selectedColumns(hit.col) : std::vector<int>{};
    if (hit.number) {
        separator();
        item(4, L"&Open the entry's form");
    }
    if (hit.header) {
        separator();
        const bool nameOnly = hide.size() == 1 && cols_[size_t(hide[0])].kind == DataCellKind::Name;
        item(5, hide.size() > 1 ? L"&Hide " + std::to_wstring(hide.size()) + L" columns" : L"&Hide the column", L"", !nameOnly);
        item(6, L"&Show all columns", L"", !dataTableHidden(dat_).empty());
        item(11, L"Hide all c&olumns", L"", cols_.size() > 1);
        item(12, L"Default column o&rder", L"", !dataTableLayout(dat_).order.empty());
        item(13, hide.size() > 1 ? L"&Fit " + std::to_wstring(hide.size()) + L" column widths" : L"&Fit the column width");
        MenuItem only;
        only.id = 17, only.label = L"Only the c&hanged columns", only.checked = changedOnly_;
        m.push_back(only);
        item(7, L"C&olumns\x2026");
        separator();
        const bool sortedHere = !sortKey_.empty() && dataColumnKey(t->schema(), cols_[size_t(hit.col)]) == sortKey_;
        MenuItem asc, desc, def;
        asc.id = 8, asc.label = L"Sort &ascending", asc.checked = sortedHere && !sortDown_;
        desc.id = 9, desc.label = L"Sort &descending", desc.checked = sortedHere && sortDown_;
        def.id = 10, def.label = L"D&efault order", def.checked = sortKey_.empty();
        m.push_back(asc), m.push_back(desc), m.push_back(def);
    }
    const int openRow = hit.row, sortCol = hit.col;
    host_.menu(pt, m, [this, hide, openRow, sortCol, pt](int pick) {
        if (pick == 1) resetSelected();
        else if (pick == 2) copy(CopyAs::Values);
        else if (pick == 14) copy(CopyAs::Headers);
        else if (pick == 16) copy(CopyAs::Csv);
        else if (pick == 15) fillDown();
        else if (pick == 13) fitDue_ = hide, invalidate();
        else if (pick == 17) setChangedOnly(!changedOnly_);
        else if (pick == 3) {
            if (draw::Host* h = host()) paste(widen(h->clipboardText()));
        } else if (pick == 4 && openRow >= 0 && openRow < int(rows_.size())) host_.open(rows_[size_t(openRow)]);
        else if (pick == 5) hideColumns(hide);
        else if (pick == 6) {
            dataTableSetHidden(dat_, {});
            host_.status(L"Showed all columns");
            refresh();
        } else if (pick == 7) pickColumns(pt);
        else if (pick == 8 || pick == 9) sortBy(sortCol, pick == 9);
        else if (pick == 10) sortBy(-1, false);
        else if (pick == 12) {
            dataTableLayout(dat_).order.clear();
            dataTableLayoutSave(dat_);
            host_.status(L"Put the columns back in the default order");
            refresh();
        } else if (pick == 11) {
            std::vector<int> all;
            for (int k = 0; k < int(cols_.size()); ++k) all.push_back(k);
            hideColumns(all);
        }
    });
}

inline void DataTableView::hideColumns(const std::vector<int>& cols) {
    const DatTable* t = host_.table();
    if (!t) return;
    DataTableLayout& l = dataTableLayout(dat_);
    int n = 0;
    for (int c : cols)
        if (c >= 0 && c < int(cols_.size()) && cols_[size_t(c)].kind != DataCellKind::Name) dataColumnSetShown(l, t->schema(), cols_[size_t(c)], false), ++n;
    dataTableLayoutSave(dat_);
    host_.status(n == 1 ? L"Hid 1 column (Columns\x2026 shows it again)" : L"Hid " + std::to_wstring(n) + L" columns (Columns\x2026 shows them again)");
    refresh();
}

// Columns…: the list of the .dat's columns (in sections: the field's section, a flag's field), the ones that show
// checked. A click acts at once: a column that starts to show goes to the right end of the table (so the columns show
// in the order of the clicks), a column that is unchecked goes. "All columns" and "None" at the top show every column
// or only the name.
inline void DataTableView::pickColumns(draw::Point pt) {
    const DatTable* t = host_.table();
    if (!t) return;
    const DatSchema& sch = t->schema();
    const std::string dat = dat_;
    std::vector<ListPickItem> items{{L"All columns", L"", "\x01all"}, {L"None (only the name)", L"", "\x01none"}};
    // The checks as the layout has them now (after each click too).
    auto checks = [&sch, dat]() {
        std::set<std::string> out;
        const DataTableLayout& l = dataTableLayout(dat);
        for (const DataTableColumn& col : dataTableColumns(sch))
            if (col.kind != DataCellKind::Name && !dataColumnHidden(l, sch, col)) out.insert(narrow(dataColumnKey(sch, col)));
        return out;
    };
    for (const DataTableColumn& col : dataTableColumns(sch)) {
        if (col.kind == DataCellKind::Name) continue;
        items.push_back({dataColumnTitle(sch, col), dataColumnGroup(sch, col), narrow(dataColumnKey(sch, col))});
    }
    std::set<std::string> checked = checks();
    auto toggle = [this, &sch, dat, checks](const std::string& value, bool on, std::set<std::string>& now) {
        DataTableLayout& l = dataTableLayout(dat);
        const std::vector<DataTableColumn> all = dataTableColumns(sch);
        std::wstring what;
        if (value == "\x01all") {
            l.hidden.clear();
            what = L"Showed all columns";
        } else if (value == "\x01none") {
            for (const DataTableColumn& col : all) dataColumnSetShown(l, sch, col, false);
            what = L"Hid all columns";
        } else {
            for (const DataTableColumn& col : all)
                if (narrow(dataColumnKey(sch, col)) == value) {
                    dataColumnSetShown(l, sch, col, on);
                    what = (on ? L"Showed " : L"Hid ") + dataColumnTitle(sch, col);
                }
        }
        dataTableLayoutSave(dat);
        now = checks();
        host_.status(what);
        refresh();
        if (on && value != "\x01all")  // a column that shows: at the right end, scrolled to (it may be past the edge)
            for (int c = 0; c < int(cols_.size()); ++c)
                if (narrow(dataColumnKey(sch, cols_[size_t(c)])) == value) return revealColumn(c);
    };
    host_.pickMany(L"Columns that show", std::move(items), checked, pt, toggle);
}

// The status bar under the table: at the left the active cell (its column, its entry, the value as the .dat stores
// it, and what it means); at the right the selection: how many cells, rows and columns, and of the numbers among
// them the sum, the average, the lowest and the highest (as the form shows them: hit points in hit points), or how
// many check boxes are on.
inline void DataTableView::paintFoot(draw::Canvas& c, const DatTable& t) {
    const int w = width(), h = height(), top = h - footH();
    c.fill({0, top, w, h}, kPanel);
    c.fill({0, top, w, top + 1}, kBorder);
    if (rows_.empty() || cols_.empty() || sel_.row >= int(rows_.size()) || sel_.col >= int(cols_.size())) return;
    const DatSchema& sch = t.schema();
    const DataTableColumn& col = cols_[size_t(sel_.col)];
    const int e = rows_[size_t(sel_.row)];
    std::wstring left = dataColumnTitle(sch, col) + L" of " + std::to_wstring(e);
    if (const DatField* f = col.kind == DataCellKind::Name ? nullptr : dataSchemaField(sch, col.field); f && f->covers(e)) {
        const uint32_t v = t.value(*f, e);
        wchar_t hex[16];
        std::swprintf(hex, 16, L"0x%X", v);
        left += L":  raw " + std::to_wstring(v) + L" (" + hex + L")";
        if (col.kind == DataCellKind::Flags) left += L" = " + dataFlagsText(*f, v);
        else if (col.kind == DataCellKind::Number && fieldShownScale(*f) > 1) left += L" = " + fieldValueText(*f, v);
        else if (col.kind == DataCellKind::Pick) left += L" = " + host_.note(*f, v);
        if (t.changed(*f, e)) left += L"  \x00B7  game's " + std::to_wstring(t.stock(*f, e));
    } else if (col.kind == DataCellKind::Name) {
        left += L":  " + dataRowName(sch.name, e);
    } else {
        left += L":  not used by this entry";
    }
    // The selection: its counts, then the sums of its numbers (or its check boxes).
    const std::vector<std::pair<int, int>> cells = sel_.cells();
    std::set<int> rowsSel, colsSel;
    double sum = 0, lo = 0, hi = 0;
    int nums = 0, boxes = 0, on = 0;
    for (const auto& [r, k] : cells) {
        rowsSel.insert(r), colsSel.insert(k);
        const DataTableColumn& cc = cols_[size_t(k)];
        const std::wstring v = dataCellData(t, cc, rows_[size_t(r)]);
        if (v.empty() || cc.kind == DataCellKind::Name) continue;
        if (cc.kind == DataCellKind::Bool) {
            ++boxes, on += v != L"0";
            continue;
        }
        if (cc.kind == DataCellKind::Pick || cc.kind == DataCellKind::Flags) continue;  // (a choice or a set of flags is not a quantity)
        const double d = std::wcstod(v.c_str(), nullptr);
        lo = nums ? std::min(lo, d) : d, hi = nums ? std::max(hi, d) : d, sum += d, ++nums;
    }
    auto num = [](double d) {
        wchar_t b[32];
        std::swprintf(b, 32, L"%.3f", d);
        std::wstring s = b;
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
        return s;
    };
    std::wstring right;
    if (cells.size() > 1) {
        right = std::to_wstring(cells.size()) + L" cells  \x00B7  " + std::to_wstring(rowsSel.size()) + (rowsSel.size() == 1 ? L" row" : L" rows") +
                L"  \x00B7  " + std::to_wstring(colsSel.size()) + (colsSel.size() == 1 ? L" column" : L" columns");
        if (nums > 1)
            right += L"     Sum " + num(sum) + L"  \x00B7  Average " + num(sum / nums) + L"  \x00B7  Lowest " + num(lo) + L"  \x00B7  Highest " + num(hi);
        if (boxes) right += L"     " + std::to_wstring(on) + L" of " + std::to_wstring(boxes) + L" checked";
    }
    const draw::Font of = c.setFont(c.font({draw::Face::Ui}));
    const int rw = right.empty() ? 0 : std::min(w / 2, c.measure(right) + scale(12));
    c.text({scale(12), top, w - rw - scale(16), h}, left, kDim, draw::kMiddle | draw::kEllipsis);
    if (rw) c.text({w - rw - scale(12), top, w - scale(12), h}, right, kText, draw::kMiddle | draw::kRight | draw::kEllipsis);
    c.setFont(of);
}

// Sorts the rows by column c (down: high to low); c < 0: the list's order again. The selection goes to the first cell.
inline void DataTableView::sortBy(int c, bool down) {
    const DatTable* t = host_.table();
    if (!t) return;
    if (c >= 0 && c < int(cols_.size())) {
        sortKey_ = dataColumnKey(t->schema(), cols_[size_t(c)]), sortDown_ = down;
        host_.status(L"Sorted by " + dataColumnTitle(t->schema(), cols_[size_t(c)]) + (down ? L", descending" : L", ascending"));
    } else {
        sortKey_.clear(), sortDown_ = false;
        host_.status(L"Sorted in the default order (the list's)");
    }
    sortDue_ = true;
    DataTableLayout& l = dataTableLayout(dat_);
    l.sortKey = sortKey_, l.sortDown = sortDown_;
    dataTableLayoutSave(dat_);
    sync();
    const int col = sel_.col;
    sel_ = {};
    sel_.click(0, std::min(col, std::max(0, int(cols_.size()) - 1)), 0);
    sy_ = 0;
    invalidate();
}

// The column that a moved column goes before for the mouse at x: the nearest edge between two columns that scroll
// (1: the first of them; cols_.size(): after the last).
inline int DataTableView::dropIndex(int x) const {
    int best = 1, bestD = INT_MAX;
    for (size_t k = 1; k <= cols_.size(); ++k) {
        const int edge = k < cols_.size() ? colLeft(k) : colLeft(k - 1) + colW(k - 1);
        if (const int d = std::abs(x - edge); d < bestD) bestD = d, best = int(k);
    }
    return best;
}

// Moves the columns `cols` (indexes of cols_) before column `before`, in their order; the layout keeps the new order.
inline void DataTableView::moveColumns(const std::vector<int>& cols, int before) {
    const DatTable* t = host_.table();
    if (!t || cols.empty()) return;
    std::vector<DataTableColumn> moved, rest;
    int at = 0;
    for (int k = 0; k < int(cols_.size()); ++k) {
        if (k == before) at = int(rest.size());
        if (std::find(cols.begin(), cols.end(), k) != cols.end() && k > 0) moved.push_back(cols_[size_t(k)]);
        else rest.push_back(cols_[size_t(k)]);
    }
    if (before >= int(cols_.size())) at = int(rest.size());
    at = std::max(1, at);  // (the name stays first)
    rest.insert(rest.begin() + at, moved.begin(), moved.end());
    // The layout's order: the columns that show, then the hidden ones as they were.
    DataTableLayout& l = dataTableLayout(dat_);
    std::vector<std::wstring> order;
    for (const auto& col : rest) order.push_back(dataColumnKey(t->schema(), col));
    for (const DataTableColumn& col : dataTableColumns(t->schema()))
        if (const std::wstring k = dataColumnKey(t->schema(), col); std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
    l.order = std::move(order);
    dataTableLayoutSave(dat_);
    sync();
    sel_.ranges = {{0, at, std::max(0, int(rows_.size()) - 1), at + int(moved.size()) - 1}};
    sel_.row = 0, sel_.col = sel_.anchorCol = at, sel_.anchorRow = 0;
    host_.status(moved.size() == 1 ? L"Moved the column" : L"Moved " + std::to_wstring(moved.size()) + L" columns");
    invalidate();
}

// A name (a unit's) or a string: the game text pop-out under the active cell, with the palette's colors, Preview and
// Raw. Its text goes into each selected cell of that kind; one undo step.
inline void DataTableView::editName(const std::string& start) {
    DatTable* t = host_.table();
    if (!t) return;
    std::string text = start;
    const std::wstring title = dat_ == "strings" ? L"String " + std::to_wstring(rows_[size_t(sel_.row)]) : L"Name in the game";
    if (!host_.editGameText(cellRect(sel_.row, sel_.col), title, text)) return;
    int changed = 0, locked = 0;
    for (const auto& [r, c] : sel_.cells()) {
        if (cols_[size_t(c)].kind != DataCellKind::Name) continue;
        const int sid = dataNameString(dat_, rows_[size_t(r)]);
        if (!sid) {
            ++locked;
            continue;
        }
        if (dataExtras().strings.text(sid - 1) == text) continue;
        dataExtras().strings.set(sid - 1, text);
        ++changed;
    }
    report(L"Set", changed, 0, locked);
    focus();
    invalidate();
}

// Changed: only the columns that have a value that is not the game's (in any entry), or all of them again.
inline void DataTableView::setChangedOnly(bool on) {
    changedOnly_ = on, ++colsRev_;
    sync();
    host_.status(on ? L"Showed only the columns with changed values (" + std::to_wstring(cols_.size() > 0 ? cols_.size() - 1 : 0) + L")"
                    : L"Showed the columns again");
    invalidate();
}

// Makes the columns in fitDue_ as wide as their widest text (the header's and each row's); the project keeps the widths.
inline void DataTableView::fitColumns(draw::Canvas& c, const DatTable& t) {
    const std::vector<int> cols = std::exchange(fitDue_, {});
    const DatSchema& sch = t.schema();
    const draw::Font ui = c.font({draw::Face::Ui}), mono = c.font({draw::Face::Mono});
    const draw::Font was = c.setFont(ui);
    DataTableLayout& l = dataTableLayout(dat_);
    for (int k : cols) {
        if (k < 0 || k >= int(cols_.size())) continue;
        const DataTableColumn& col = cols_[size_t(k)];
        if (col.kind == DataCellKind::Flags) continue;  // (as wide as its hex and its boxes)
        const DatField* f = col.kind == DataCellKind::Name ? nullptr : dataSchemaField(sch, col.field);
        c.setFont(ui);
        int w = c.measure(dataColumnTitle(sch, col)) + scale(12) + (dataColumnKey(sch, col) == sortKey_ ? scale(10) : 0);
        for (int e : rows_) {
            if (col.kind == DataCellKind::Name) {
                c.setFont(ui);
                w = std::max(w, c.measure(dataRowName(dat_, e)) + scale(12) + (dat_ == "strings" ? 0 : rowH()));
            } else if (f && f->covers(e)) {
                const uint32_t v = t.value(*f, e);
                if (col.kind == DataCellKind::Pick) {
                    c.setFont(ui);
                    const std::wstring note = host_.note(*f, v);
                    w = std::max(w, c.measure(note.empty() ? L"#" + std::to_wstring(v) : note) + scale(34) + (fieldRefDat(*f).empty() ? 0 : scale(20)));
                } else if (col.kind == DataCellKind::Number) {
                    c.setFont(mono);
                    w = std::max(w, c.measure(fieldValueText(*f, v)) + scale(12));
                } else {
                    w = std::max(w, scale(36));
                }
            }
        }
        w = std::clamp(w, scale(36), scale(900));
        l.widths[dataColumnKey(sch, col)] = std::max(36, w * 96 / std::max(1, scale(96)));
    }
    c.setFont(was);
    dataTableLayoutSave(dat_);
    host_.status(cols.size() == 1 ? L"Fitted the column width" : L"Fitted " + std::to_wstring(cols.size()) + L" column widths");
}
