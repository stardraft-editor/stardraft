// The Data workspace's Table view (portable, core.inc, after views_data_entry.inc): the open .dat as a table, in place of
// the entry's form. A row for each entry (the entries selected in the list when there are 2 or more, else every row of
// the list), a column for each field (core/data_table.inc). The id and the name stay at the left while the fields
// scroll. Select cells as in a spreadsheet (click, Shift, Ctrl, a column's header, a row's number, a drag), then type
// a value: it goes into every selected cell. Check boxes toggle with a click or Space; a dropdown's cell takes its
// number or the name of a value, and its arrow opens its list. Delete puts the game's values back, Ctrl+C and Ctrl+V
// copy and paste cells (tab-separated, as spreadsheets do), Ctrl+D copies the top cell down. Each change is one undo
// step. Import CSV and Export CSV are at the top. The program gives the table and saves through DataTableViewHost.

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
    // A dropdown's list under `box` (the view's coordinates) at the value `current`: true with the picked value.
    virtual bool choose(const DatField& f, uint32_t current, draw::Rect box, uint32_t& out) = 0;
    virtual void saved(const std::wstring& what) = 0;  // cells changed: the program saves them (one undo step) and says `what`
    virtual void open(int e) = 0;                      // a row's number double-clicked: the entry's form
    virtual void importCsv() = 0;
    virtual void exportCsv() = 0;
    virtual void status(const std::wstring& text) = 0;
    virtual bool travel(int button) = 0;               // the mouse's back (1) and forward (2) buttons
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
    }

    // The metrics (scaled).
    static int barH() { return scale(44); }   // the bar at the top: the title, Import CSV, Export CSV
    static int groupH() { return scale(20); }  // the headers' first row: the section (a flag's field)
    static int headH() { return scale(26); }   // the headers' second row: the column's name
    static int rowH() { return scale(26); }
    static int idW() { return scale(52); }

    // The table's place in the view: under the bar and the headers, left of the vertical bar, over the horizontal bar.
    draw::Rect body() const {
        const int sb = scale(stardraft::kit::kScrollBarW);
        return {0, barH() + groupH() + headH(), std::max(0, width() - sb), std::max(0, height() - sb)};
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
    const std::vector<DataTableColumn>& columns() {
        sync();
        return cols_;
    }

    void resized() override {
        const int sb = scale(stardraft::kit::kScrollBarW), top = barH() + groupH() + headH();
        vbar_.setFrame({width() - sb, top, width(), std::max(top, height() - sb)});
        hbar_.setFrame({0, height() - sb, std::max(0, width() - sb), height()});
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
    enum class Drag { None, Cells, Columns, Rows, Width };
    // A place in the view: a cell, a column's header, a row's number, or a column's right edge in the headers.
    struct Hit {
        int row = -1, col = -1;
        bool header = false, number = false, edge = false, check = false, arrow = false;
        draw::Rect cell;
    };

    void sync();
    int colW(size_t k) const;
    int frozenW() const { return idW() + (cols_.empty() ? 0 : colW(0)); }
    int scrollW() const;   // the width of the columns that scroll
    int colLeft(size_t k) const;  // a column's left edge in the view (scrolled)
    Hit hitAt(draw::Point pt) const;
    draw::Rect cellRect(int r, int c) const;
    void clampScroll();
    void reveal(int r, int c);
    void moveActive(int dr, int dc, bool shift);
    void beginEdit(const std::wstring& start, bool selectAll);
    void placeEntry();
    void toggleChecks(int r, int c);
    void setChecks(bool on);
    void resetSelected();
    void fillDown();
    void openPick(int r, int c);
    std::wstring copyText();
    void report(const std::wstring& verb, int changed, int bad, int locked);
    void paintCell(draw::Canvas& c, const DatTable& t, const DataTableColumn& col, int e, draw::Rect r, bool hot);

    DataTableViewHost& host_;
    stardraft::kit::ScrollBar vbar_{stardraft::kit::Axis::Vertical}, hbar_{stardraft::kit::Axis::Horizontal};
    DataNumberEntry entry_;
    std::string dat_;                   // the .dat the columns are of
    std::vector<DataTableColumn> cols_;
    std::vector<int> rows_;             // the rows as the last sync had them
    std::map<std::string, int> widths_;  // a column's width that a drag of its edge set (its .dat and key), unscaled
    DataTableSelection sel_;
    int sx_ = 0, sy_ = 0;               // the scroll in pixels
    bool editing_ = false;
    Drag drag_ = Drag::None;
    int dragCol_ = -1, dragX_ = 0, dragW0_ = 0;
    Hit hot_;
    draw::Rect importBtn_, exportBtn_;
};

inline void DataTableView::sync() {
    const DatTable* t = host_.table();
    const std::string dat = t ? t->schema().name : "";
    if (dat != dat_) {
        dat_ = dat;
        cols_ = t ? dataTableColumns(t->schema()) : std::vector<DataTableColumn>{};
        sel_ = {}, sx_ = 0, sy_ = 0;
        if (editing_) cancelEdit();
    }
    if (host_.rows() != rows_) {
        if (editing_) cancelEdit();
        rows_ = host_.rows();
        sel_.clamp(int(rows_.size()), int(cols_.size()));
    }
}

inline int DataTableView::colW(size_t k) const {
    const DataTableColumn& col = cols_[k];
    const auto it = widths_.find(dat_ + "\n" + std::to_string(col.field) + "." + std::to_string(col.bit));
    if (it != widths_.end()) return scale(it->second);
    switch (col.kind) {
    case DataCellKind::Name: return scale(220);
    case DataCellKind::Pick: return scale(180);
    case DataCellKind::Bool:
    case DataCellKind::Bit: return scale(72);
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
    if (c > 0) {
        int left = 0;
        for (int i = 1; i < c; ++i) left += colW(size_t(i));
        const int room = b.width() - frozenW();
        if (left < sx_) sx_ = left;
        if (left + colW(size_t(c)) > sx_ + room) sx_ = left + colW(size_t(c)) - room;
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
            if ((k == 0 || right > frozenW()) && std::abs(pt.x - right) <= scale(3)) h.edge = true, h.col = int(k);
        }
        return h;
    }
    const int r = (pt.y - b.top + sy_) / rowH();
    if (r < 0 || r >= int(rows_.size())) return h;
    h.row = r;
    if (pt.x < idW()) return h.number = true, h;
    if (col < 0) return h;
    h.cell = cellRect(r, col);
    const DataTableColumn& cc = cols_[size_t(col)];
    if (cc.kind == DataCellKind::Bool || cc.kind == DataCellKind::Bit) {
        const int bx = scale(14), cx = (h.cell.left + h.cell.right) / 2, cy = (h.cell.top + h.cell.bottom) / 2;
        h.check = pt.x >= cx - bx && pt.x <= cx + bx && pt.y >= cy - bx && pt.y <= cy + bx;
    }
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
        std::wstring name = tabEntryName(datTabOf(t.schema().name), e);
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
    case DataCellKind::Bool:
    case DataCellKind::Bit: {
        const bool on = col.kind == DataCellKind::Bit ? (v >> col.bit) & 1 : v != 0;
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
    // The bar: the .dat and how many rows, and which ones; Import CSV and Export CSV at the right.
    {
        const int bh = barH(), x0 = scale(12);
        const std::wstring rows = std::to_wstring(rows_.size()) + (rows_.size() == 1 ? L" row" : L" rows");
        const std::wstring what = !tp ? L"" : host_.rowsPicked() ? L"the entries selected in the list" : L"all entries of the list (select 2 or more entries in the list to show only those)";
        c.text({x0, 0, w, bh}, tp ? datTitle(tp->schema().name) + L"   " : L"", kText, draw::kMiddle);
        const int tw = tp ? c.measure(datTitle(tp->schema().name) + L"   ") : 0;
        const std::wstring line = tp ? rows + L" \x00B7 " + what : L"";
        exportBtn_ = kitpaint::smallButtonRect(c, 0, 0, bh, L"Export CSV\x2026");
        exportBtn_ = {w - scale(12) - exportBtn_.width(), exportBtn_.top, w - scale(12), exportBtn_.bottom};
        importBtn_ = kitpaint::smallButtonRect(c, 0, 0, bh, L"Import CSV\x2026");
        importBtn_ = {exportBtn_.left - scale(8) - importBtn_.width(), importBtn_.top, exportBtn_.left - scale(8), importBtn_.bottom};
        c.text({x0 + tw, 0, importBtn_.left - scale(12), bh}, line, kDim, draw::kMiddle | draw::kEllipsis);
        if (tp) {
            draw::Host* hh = host();
            const draw::Point m = hh ? hh->mouse(*this) : draw::Point{-1, -1};
            kitpaint::drawButtonFace(c, importBtn_, L"Import CSV\x2026", kitpaint::Tone::Normal, importBtn_.contains(m) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
            kitpaint::drawButtonFace(c, exportBtn_, L"Export CSV\x2026", kitpaint::Tone::Normal, exportBtn_.contains(m) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        } else {
            importBtn_ = exportBtn_ = {};
        }
        c.fill({0, bh - 1, w, bh}, kBorder);
    }
    if (!tp) {
        c.text({scale(18), barH() + scale(20), w - scale(18), h}, L"The Table view shows only the .dat tabs", kDim, draw::kWrap);
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
            paintCell(c, t, cols_[k], rows_[size_t(r)], cell, hot);
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
    const draw::Font headFont = c.font(kitpaint::smallFontDesc());
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
            const unsigned align = num ? draw::kRight : cols_[k].kind == DataCellKind::Bool || cols_[k].kind == DataCellKind::Bit ? draw::kCenter : draw::kLeft;
            c.text({head.left + scale(6), head.top, head.right - scale(6), head.bottom}, dataColumnTitle(sch, cols_[k]), selCol ? kText : kDim,
                   align | draw::kMiddle | draw::kEllipsis);
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
            widths_[dat_ + "\n" + std::to_string(col.field) + "." + std::to_string(col.bit)] = std::max(36, nw * 96 / std::max(1, scale(96)));
            clampScroll();
            invalidate();
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
        if (hit.row != hot_.row || hit.col != hot_.col || hit.check != hot_.check || hit.arrow != hot_.arrow || drag_ != Drag::None) hot_ = hit, invalidate();
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
        if (e.button == 0 && importBtn_.contains(e.pt)) return host_.importCsv(), true;
        if (e.button == 0 && exportBtn_.contains(e.pt)) return host_.exportCsv(), true;
        const Hit hit = hitAt(e.pt);
        const unsigned mods = e.mods & (draw::kCtrl | draw::kShift);
        if (e.button == 1) {  // the right button: a cell outside the selection becomes the selection
            if (hit.row >= 0 && hit.col >= 0 && !sel_.has(hit.row, hit.col)) sel_.click(hit.row, hit.col, 0);
            invalidate();
            return true;
        }
        if (hit.header && hit.edge) {
            drag_ = Drag::Width, dragCol_ = hit.col, dragX_ = e.pt.x, dragW0_ = colW(size_t(hit.col));
        } else if (hit.header && hit.col >= 0) {
            sel_.columns(hit.col, mods, int(rows_.size()));
            drag_ = Drag::Columns;
        } else if (hit.number) {
            if (e.kind == MouseKind::DoubleClick) return host_.open(rows_[size_t(hit.row)]), true;
            sel_.rows(hit.row, mods, int(cols_.size()));
            drag_ = Drag::Rows;
        } else if (hit.row >= 0 && hit.col >= 0) {
            const DataTableColumn& col = cols_[size_t(hit.col)];
            const bool wasActive = sel_.single() && sel_.row == hit.row && sel_.col == hit.col;
            if (!(hit.check && sel_.has(hit.row, hit.col) && !mods)) sel_.click(hit.row, hit.col, mods);
            if (hit.check && !mods) toggleChecks(hit.row, hit.col);  // (a click on the box: the selected boxes toggle with it)
            else if (hit.arrow && !mods && e.kind == MouseKind::Down) openPick(hit.row, hit.col);
            else if (e.kind == MouseKind::DoubleClick && col.kind == DataCellKind::Name && !dataNameString(dat_, rows_[size_t(hit.row)]))
                return host_.open(rows_[size_t(hit.row)]), true;
            else if ((e.kind == MouseKind::DoubleClick || wasActive) && col.kind != DataCellKind::Bool && col.kind != DataCellKind::Bit)
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
            drag_ = Drag::None;
            if (draw::Host* h = host()) h->capture(*this, false);
        }
        return e.button == 0;
    default: return false;
    }
}

inline draw::Cursor DataTableView::cursor(draw::Point pt) {
    const Hit hit = hitAt(pt);
    if (drag_ == Drag::Width || (hit.header && hit.edge)) return draw::Cursor::SizeWE;
    if (importBtn_.contains(pt) || exportBtn_.contains(pt) || hit.check || hit.arrow) return draw::Cursor::Hand;
    return draw::Cursor::Arrow;
}

inline std::wstring DataTableView::tip(draw::Point pt) {
    if (importBtn_.contains(pt)) return L"Import the values of a CSV file into this .dat file. The \"id\" column gives the entries. The first row gives the columns.";
    if (exportBtn_.contains(pt)) return L"Export the rows of the table, with all columns, as a CSV file";
    const Hit hit = hitAt(pt);
    const DatTable* t = host_.table();
    if (!t || hit.col < 0) return L"";
    const DataTableColumn& col = cols_[size_t(hit.col)];
    if (hit.header) {
        const std::wstring group = dataColumnGroup(t->schema(), col);
        return (group.empty() || col.kind != DataCellKind::Bit ? L"" : group + L": ") + dataColumnTitle(t->schema(), col) +
               L"\nA click selects the column. A drag of its right edge changes its width.";
    }
    if (hit.row < 0) return L"";
    const int e = rows_[size_t(hit.row)];
    if (const std::wstring why = dataCellLocked(*t, col, e); !why.empty()) return why;
    if (col.kind == DataCellKind::Pick) {
        const DatField* f = dataSchemaField(t->schema(), col.field);
        return f ? host_.note(*f, t->value(*f, e)) + L"   #" + std::to_wstring(t->value(*f, e)) : L"";
    }
    if (col.kind == DataCellKind::Name) return tabEntryName(datTabOf(t->schema().name), e);
    return L"";
}

inline void DataTableView::moveActive(int dr, int dc, bool shift) {
    sel_.move(dr, dc, shift, int(rows_.size()), int(cols_.size()));
    reveal(sel_.row, sel_.col);
}

inline bool DataTableView::key(const draw::KeyEvent& e) {
    if (!e.down) return false;
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
        if (e.key == draw::Key::Down) return openPick(sel_.row, sel_.col), true;  // (Alt+Down: the dropdown's list)
        return false;
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
        if (col.kind == DataCellKind::Bool || col.kind == DataCellKind::Bit) {
            if (e.key == draw::Key::Enter) moveActive(shift ? -1 : 1, 0, false);
            return true;
        }
        beginEdit(dataCellData(*host_.table(), col, rows_[size_t(sel_.row)]), true);
        return true;
    }
    case draw::Key::Space:
        if (ctrl) return sel_.columns(sel_.col, 0, int(rows_.size())), invalidate(), true;  // (as spreadsheets: Ctrl+Space the column,
        if (shift) return sel_.rows(sel_.row, 0, int(cols_.size())), invalidate(), true;   // Shift+Space the row)
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
    case 'C':
        if (draw::Host* h = host()) h->setClipboardText(narrow(copyText()));
        host_.status(L"Copied " + std::to_wstring(sel_.cells().size()) + L" cells");
        return true;
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
    if (col.kind == DataCellKind::Bool || col.kind == DataCellKind::Bit) {
        bool yes = false;  // (a check box: 1, 0, y, n, x set every selected box)
        if (dataReadYesNo(w, yes)) setChecks(yes);
        return true;
    }
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
    editing_ = true;
    const bool name = col.kind == DataCellKind::Name;
    entry_.field().setAlignRight(col.kind == DataCellKind::Number);
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
    const draw::Rect r{a.left + 1, a.top + 1, a.right - 1, a.bottom - 1};
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
    int changed = 0, bad = 0, locked = 0;
    for (const auto& [r, c] : sel_.cells()) {
        const DataCellSet s = dataCellSet(*t, cols_[size_t(c)], rows_[size_t(r)], text);
        changed += s == DataCellSet::Changed, bad += s == DataCellSet::NotValue, locked += s == DataCellSet::Locked;
    }
    report(L"Set", changed, bad, locked);
    invalidate();
}

// The check boxes among the selected cells: all on when the box of cell (r, c) is off, else all off.
inline void DataTableView::toggleChecks(int r, int c) {
    const DatTable* t = host_.table();
    if (!t || r < 0 || c < 0 || r >= int(rows_.size()) || c >= int(cols_.size())) return;
    const DataTableColumn& col = cols_[size_t(c)];
    bool on = true;
    if (col.kind == DataCellKind::Bool || col.kind == DataCellKind::Bit) on = dataCellData(*t, col, rows_[size_t(r)]) == L"0";
    setChecks(on);
}
inline void DataTableView::setChecks(bool on) {
    DatTable* et = host_.table();
    if (!et) return;
    int changed = 0, locked = 0;
    for (const auto& [r, c] : sel_.cells()) {
        const DataTableColumn& cc = cols_[size_t(c)];
        if (cc.kind != DataCellKind::Bool && cc.kind != DataCellKind::Bit) continue;
        const DataCellSet s = dataCellSet(*et, cc, rows_[size_t(r)], on ? L"1" : L"0");
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked;
    }
    report(on ? L"Checked" : L"Cleared", changed, 0, locked);
    invalidate();
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
inline void DataTableView::openPick(int r, int c) {
    DatTable* t = host_.table();
    if (!t || r < 0 || c < 0 || cols_[size_t(c)].kind != DataCellKind::Pick) return;
    const DatField* f = dataSchemaField(t->schema(), cols_[size_t(c)].field);
    const int e = rows_[size_t(r)];
    if (!f || !f->covers(e)) return;
    if (const std::wstring why = dataCellLocked(*t, cols_[size_t(c)], e); !why.empty()) return host_.status(why);
    uint32_t v = 0;
    if (!host_.choose(*f, t->value(*f, e), cellRect(r, c), v)) return;
    int changed = 0, locked = 0;
    for (const auto& [sr, sc] : sel_.cells()) {
        if (cols_[size_t(sc)].field != f->index) continue;
        const DataCellSet s = dataCellSet(*t, cols_[size_t(sc)], rows_[size_t(sr)], std::to_wstring(v));
        changed += s == DataCellSet::Changed, locked += s == DataCellSet::Locked;
    }
    report(L"Set", changed, 0, locked);
    invalidate();
}

// The selected cells as tab-separated text: the rectangle round them, a cell that is not selected empty.
inline std::wstring DataTableView::copyText() {
    const DatTable* t = host_.table();
    if (!t || rows_.empty() || cols_.empty()) return L"";
    const DataCellRange b = sel_.bounds();
    DataGrid grid;
    for (int r = b.r0; r <= b.r1; ++r) {
        grid.emplace_back();
        for (int c = b.c0; c <= b.c1; ++c) grid.back().push_back(sel_.has(r, c) ? dataCellData(*t, cols_[size_t(c)], rows_[size_t(r)]) : L"");
    }
    return dataGridText(grid, L'\t');
}

inline void DataTableView::paste(const std::wstring& text) {
    DatTable* t = host_.table();
    if (!t || rows_.empty() || cols_.empty()) return;
    const DataGrid grid = dataGridRead(text, L'\t');
    if (grid.empty()) return host_.status(L"The clipboard has no text to paste");
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
