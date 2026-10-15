// The Data workspace's entry form (portable, core.inc): where the parts of each field's row sit, from the form's
// metrics (the program scales them to the display), and the form itself as a view (DataFormView, below): its paint on
// the Canvas, its scroll and its click areas. The Windows form (main.cpp: DataFormProgram, dataFormProc) hosts it.

// The form's sizes in pixels (scaled by the program).
struct DataFormScale {
    int two = 0;      // the inset of a value box from its row
    int eight = 0;    // the gap after a label's check box, a value box and a flag's name
    int four = 0;     // the room kept at the right of a flag's cell
    int seven = 0;    // the inset of a value's text from its box
    int boolW = 0;    // the width of a yes/no value's cell
    int dropW = 0;    // the width of a dropdown's box
    int flagW = 0;    // the width of one flag in the grid
    int flagH = 0;    // the height of one flag's row in the grid
    int box = 0;      // the size of a check box
};

// The parts of a field's row (its label's box; the value's box, a yes/no value's cell and check box and text; a
// dropdown's box; a number's note).
struct DataFieldRow {
    draw::Rect label, box, dropBox, boolCell, boolCheck, boolText, note;
};

DataFieldRow dataFieldRow(int x0, int labelW, int valW, int y, int rowH, int colRight, const DataFormScale& s) {
    DataFieldRow r;
    r.label = {x0, y, x0 + labelW, y + rowH};
    r.box = {x0 + labelW, y + s.two, x0 + labelW + valW, y + rowH - s.two};
    r.dropBox = {x0 + labelW, y + s.two, x0 + labelW + s.dropW, y + rowH - s.two};
    r.boolCell = {x0 + labelW, y + s.two, x0 + labelW + s.boolW, y + rowH - s.two};
    const int cb = s.box;
    r.boolCheck = {r.boolCell.left, y + (rowH - cb) / 2, r.boolCell.left + cb, y + (rowH - cb) / 2 + cb};
    r.boolText = {r.boolCheck.right + s.eight, y, r.boolCell.right, y + rowH};
    r.note = {r.box.right + s.eight, y, colRight, y + rowH};
    return r;
}

// The value's text box: the value box with its inset (the number's arrows take the right end of it, the caller's).
draw::Rect dataValueText(draw::Rect box, const DataFormScale& s) {
    return {box.left + s.seven, box.top, box.right - s.seven, box.bottom};
}

// The flags' grid: how many flags go across (the column's width less the label, in flag widths; at least one).
int dataFlagCols(int colW, int labelW, const DataFormScale& s) { return std::max(1, (colW - labelW) / s.flagW); }

// The height of a flags grid of `count` flags across `cols` columns, with the gap under it.
int dataFlagGridHeight(size_t count, int cols, const DataFormScale& s) {
    return int((count + size_t(cols) - 1) / size_t(cols)) * s.flagH + s.eight;
}

struct DataFlagCell {
    draw::Rect cell, check, text;  // the click area of the flag (its row's cell), its check box and its name
};

DataFlagCell dataFlagCell(int x0, int labelW, int y, int cols, size_t b, const DataFormScale& s) {
    const int cx = x0 + labelW + int(b % size_t(cols)) * s.flagW, cy = y + int(b / size_t(cols)) * s.flagH;
    DataFlagCell f;
    f.check = {cx, cy + (s.flagH - s.box) / 2, cx + s.box, cy + (s.flagH - s.box) / 2 + s.box};
    f.cell = {cx, cy, cx + s.flagW - s.four, cy + s.flagH};
    f.text = {f.check.right + s.eight, cy, cx + s.flagW - s.four, cy + s.flagH};
    return f;
}

// A yes/no value on the Canvas: the check box on the value's cell and "Yes", "No" or "Yes (n)" beside it (faint when
// read only, green when it differs from the game). `hot` tells whether the mouse is over a box of the form. The caller
// records the cell as a click area.
void paintDataBool(draw::Canvas& c, const DataFieldRow& fr, uint32_t v, bool ro, bool mod, const std::function<bool(draw::Rect)>& hot) {
    kitpaint::drawCheckMark(c, fr.boolCheck, v != 0, !ro && hot(fr.boolCell), !ro);
    const std::wstring label = v == 0 ? L"No" : v == 1 ? L"Yes" : L"Yes (" + std::to_wstring(v) + L")";
    c.text(fr.boolText, label, ro ? kFaint : mod ? kGood : kText, draw::kMiddle);
}

// A flag field's grid under its value, on the Canvas: each flag's check box and name (green when it differs from the
// game, lit when the mouse is over it). Each flag's click area goes to `hit(area, bit)`. Returns the grid's height.
int paintDataFlags(draw::Canvas& c, const DatField& f, const DatTable& t, int u, uint32_t v, int x0, int labelW, int y, int colW,
                   const DataFormScale& s, const std::function<bool(draw::Rect)>& hot, const std::function<void(draw::Rect, int)>& hit) {
    const int cols = dataFlagCols(colW, labelW, s);
    for (size_t b = 0; b < f.flags.size(); ++b) {
        const DataFlagCell fc = dataFlagCell(x0, labelW, y, cols, b, s);
        const bool set = (v >> b) & 1, bmod = ((t.stock(f, u) >> b) & 1) != set;
        kitpaint::drawCheckMark(c, fc.check, set, hot(fc.cell), true);
        c.text(fc.text, widen(f.flags[b]), bmod ? kGood : set || hot(fc.cell) ? kText : kDim, draw::kMiddle | draw::kEllipsis);
        hit(fc.cell, int(b));
    }
    return dataFlagGridHeight(f.flags.size(), cols, s);
}

// The form's metrics at the display's scale (the Windows form's sizes).
inline DataFormScale dataFormScale() {
    return {scale(2), scale(8), scale(4), scale(7), scale(110), scale(320), scale(190), scale(22), scale(14)};
}

// --- The form as a view ---------------------------------------------------------------------------------------------
// A .dat entry's form (DataFormView): the title, the Rename and String links, the sections in the schema's order with
// each section's fields (labels, value boxes, yes/no values, dropdowns, notes, flag grids), the scroll (the window's own
// bar) and the mouse on the fields. The parts that are still the program's (the pictures beside the fields, the Units
// display, the requirements, the map's players, the Strings and Buttons tabs, their click areas and tooltips) come in
// through DataFormViewHost, drawn on the same Canvas.

// A click area of a field, from the last paint (the view's coordinates).
struct DataFormHit {
    draw::Rect r;
    int field = 0;  // the field's index in its schema
    int bit = 0;    // -1 the value box; >=0 a flag; -2 a reference link; -3 the label; -4 a dropdown; -5 a yes/no value;
                    // -6 a number's up arrow; -7 its down arrow
};

class DataFormViewHost {
public:
    virtual ~DataFormViewHost() = default;
    virtual const DatTable* table() = 0;             // the open .dat (null: it did not load, or there is no project)
    virtual DatTable* editTable() = 0;               // the same table, to change its values
    virtual int entry() = 0;                         // the open entry
    virtual int& scroll() = 0;                       // the form's scroll in pixels (kept by the program: a tab switch resets it)
    virtual std::set<std::string>& hotFields() = 0;  // the fields under the mouse (the form's and the pictures')
    virtual bool hot(draw::Rect r) = 0;              // the mouse is over r
    virtual bool projectOpen() = 0;                  // an EUD project is open (the text when no table loads)
    // The paint: before it (the program's click areas go), the Strings or Buttons tab (true: it is that tab, painted from
    // y down), and after it (what is drawn over the form).
    virtual void beforePaint() = 0;
    virtual bool paintExtraTab(draw::Canvas& c, int& y) = 0;
    virtual void afterPaint(draw::Canvas& c) = 0;
    // The entry's title: its picture (empty: none) and its kind's icon.
    virtual draw::Image entryIcon() = 0;
    virtual UiIcon tabIcon() = 0;
    // The stat_txt.tbl string that names the entry in the game (0: none, or no strings to change); Rename and the link.
    virtual int nameString() = 0;
    virtual void rename(int sid, draw::Rect link) = 0;  // (`link`: Rename, in the form's coordinates; the pop-out goes under it)
    virtual void openString(int sid) = 0;
    // A field's note (the name of its value) and why it cannot be changed (empty: it can).
    virtual std::wstring fieldNote(const DatField& f, uint32_t v) = 0;
    virtual std::wstring readOnlyWhy(const DatField& f) = 0;
    virtual draw::Image refIcon(const std::string& dat, uint32_t v) = 0;  // the picture of entry v of a .dat (empty: none)
    // The program's parts, each from y down; each returns the y under it.
    virtual int widgets(draw::Canvas& c, int section, int x, int y, int w) = 0;  // a section's pictures
    virtual void soundButton(draw::Canvas& c, draw::Rect r, int sfx) = 0;
    virtual int unitDisplay(draw::Canvas& c, int y) = 0;
    virtual int requirements(draw::Canvas& c, int x0, int y, int colW) = 0;
    virtual int mapPlayers(draw::Canvas& c, int x0, int y, int colW, int labelW, int rowH) = 0;
    // A click area of the program's kind (a link, a read-only row): its click (may be null), tip and cursor.
    virtual void area(draw::Rect r, std::function<void()> click, const std::wstring& tip, draw::Cursor cursor) = 0;
    // The mouse on the program's parts (they come first): a press (true: taken; the view then captures the mouse), a
    // move while pressed (true: taken), the release (lost: the mouse capture went before the button went up: a drag's
    // value is saved, a click does not run), the hover (true: the form repaints; `fields`: the fields the part
    // under the mouse shows), the cursor, the name feedback gives, and the wheel (true: taken).
    virtual bool widgetPress(draw::Point pt) = 0;
    virtual bool widgetDrag(draw::Point pt) = 0;
    virtual void widgetRelease(bool lost) = 0;
    virtual bool widgetHover(draw::Point pt, std::set<std::string>& fields) = 0;
    virtual bool widgetCursor(draw::Point pt, draw::Cursor& c) = 0;
    virtual std::wstring widgetName(draw::Point pt) = 0;
    virtual bool widgetWheel(draw::Point pt, float notches) = 0;
    // What the fields do.
    virtual void commitEdit() = 0;                                  // the number entry's value goes in
    virtual void beginEdit(int field, draw::Rect box) = 0;          // the number entry on a value box
    virtual void dropdown(const DatField& f, draw::Rect box) = 0;   // a dropdown's list, under its box
    virtual std::vector<ListPickItem> dropdownItems(const DatField& f) = 0;
    virtual void valueSet() = 0;                                    // after a value's change: the program saves it (an undo step) and repaints
    virtual void paintNow() = 0;                                    // the form's paint now (its click areas follow the last paint)
    virtual void goTo(const std::string& dat, int entry) = 0;       // a reference: open that entry
    virtual void fieldMenu(const DatField& f, draw::Point pt) = 0;  // the right button on a field (pt: the view's)
    virtual void status(const std::wstring& text) = 0;
    virtual bool travel(int button) = 0;                            // the mouse's back (1) and forward (2) buttons
};

class DataFormView final : public draw::View {
public:
    explicit DataFormView(DataFormViewHost& host) : host_(host) {}

    const std::vector<DataFormHit>& hits() const { return hits_; }
    int contentHeight() const { return height_; }                         // the form's height, from the last paint
    const std::vector<int>& sectionTops() const { return sectionTops_; }  // each section's place (content coordinates)
    // Scrolls to `pos` (kept inside the form).
    void scrollTo(int pos) {
        host_.scroll() = std::clamp(pos, 0, std::max(0, height_ - height()));
        invalidate();
    }
    // A field's value on the open entry (kept inside what the field can hold), saved with its undo step.
    void setValue(const DatField& f, uint32_t v) {
        if (DatTable* t = host_.editTable()) dataApplyValue(*t, f, host_.entry(), v);
        else return;
        host_.valueSet();
    }
    // Tab (delta 1) and Shift+Tab (-1) in the number entry on `field`'s value box: its value goes in, and the entry goes
    // to the next (previous) value box in the form's order, scrolled into view.
    void editNeighbour(int field, int delta);
    const DatField* field(int index) const {
        const DatTable* t = host_.table();
        if (!t) return nullptr;
        for (const auto& f : t->schema().fields)
            if (f.index == index) return &f;
        return nullptr;
    }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override;
    std::wstring feedbackName(draw::Point pt) override;
    bool vscroll(int code, int pos, int track, int page) override;
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, height_ - 1), page = height(), pos = host_.scroll();
        return true;
    }
    bool extraButton(int button) override { return (button == 1 || button == 2) && host_.travel(button); }
    void captureLost() override {  // (Alt+Tab during a drag: the value as it is goes in, with its undo step)
        if (std::exchange(pressed_, false)) host_.widgetRelease(true);
    }

private:
    void press(draw::Point pt, bool right);
    void hover(draw::Point pt);
    void wheel(const draw::MouseEvent& e);

    DataFormViewHost& host_;
    std::vector<DataFormHit> hits_;
    std::vector<int> sectionTops_;
    int height_ = 0;
    bool pressed_ = false;  // a press on a program's part: the mouse is captured until it goes up
    std::chrono::steady_clock::time_point lastScroll_{};
};

inline void DataFormView::paint(draw::Canvas& c) {
    const draw::Font uiFont = c.setFont(c.font({draw::Face::Ui}));  // (the font the paint starts with: put back after each part)
    c.setFont(uiFont);
    const draw::Rect rc{0, 0, width(), height()};
    c.fill(rc, kBg);
    host_.beforePaint();
    hits_.clear();
    int& scrollPos = host_.scroll();
    const DataFormScale s = dataFormScale();
    const std::function<bool(draw::Rect)> hotRect = [this](draw::Rect r) { return host_.hot(r); };
    int y = scale(12) - scrollPos;
    const int x0 = scale(18), labelW = scale(230), valW = scale(110), rowH = scale(26);
    // Wide: the fields in a column on the left and each section's pictures beside them; narrow: the pictures above the
    // fields.
    const int full = std::max(scale(300), rc.right - 2 * x0);
    const bool twoCol = full >= scale(1150);
    const int colW = twoCol ? std::min(scale(650), full * 11 / 20) : full, colRight = x0 + colW;
    const DatTable* tp = host_.table();
    if (host_.paintExtraTab(c, y)) {
    } else if (!tp) {
        c.text({x0, scale(40), rc.right - x0, rc.bottom},
               host_.projectOpen() ? L"Could not read the .dat file of the game" :
                                     L"To edit .dat files, use an EUD project.\r\n\r\nFile > Project > New project makes one from the open map. "
                                     L"File > Project > Open project opens one.",
               kDim, draw::kWrap);
    } else {
        const DatTable& t = *tp;
        const std::string& dat = t.schema().name;
        const int u = host_.entry();
        // Title: icon, name, id.
        int changedHere = 0;
        for (const auto& f : t.schema().fields) changedHere += f.covers(u) && t.changed(f, u);
        const std::wstring name = entryName(dat, u);
        y = kitpaint::drawTitle(c, x0, y, rc.right - x0, host_.entryIcon(), true, host_.tabIcon(), name.empty() ? L"(unnamed)" : name,
                                widen(dat) + L".dat entry " + std::to_wstring(u) +
                                    (changedHere ? L"  \x00B7  " + std::to_wstring(changedHere) + L" changed" : L""),
                                false)
                .next;
        c.setFont(uiFont);
        // A link: its face, and its click area with the tip (the program's). Returns its right edge.
        auto link = [&](int lx, int ly, int lh, const std::wstring& text, std::function<void()> click, const std::wstring& tip) {
            const draw::Rect r{lx, ly, lx + c.measure(text), ly + lh};
            kitpaint::drawLinkFace(c, r, text, kitpaint::Tone::Primary, host_.hot(r));
            host_.area(r, std::move(click), tip, draw::Cursor::Hand);
            return r.right;
        };
        // The name in the game: Rename (its stat_txt.tbl string, with colors), and a link to that string in Strings.
        if (const int sid = host_.nameString(); sid > 0) {
            const int lx = x0 + scale(52), ly = y - scale(12);
            const draw::Rect renameLink{lx, ly, lx + c.measure(L"Rename"), ly + scale(22)};
            const int right = link(lx, ly, scale(22), L"Rename", [this, sid, renameLink] { host_.rename(sid, renameLink); },
                                   L"Change the name that the game shows. Each entry that uses this string gets the new name.");
            link(right + scale(16), ly, scale(22), L"String " + std::to_wstring(sid), [this, sid] { host_.openString(sid); },
                 L"Open this string in the Strings tab");
            y += scale(14);
        }
        int rightTop = y;  // where the right column is free
        // Sections in the schema's order, fields in each section's order.
        const auto& sch = t.schema();
        const int sections = int(sch.sections.size()) + 1;
        for (int si = 0; si < sections; ++si) {
            std::vector<const DatField*> fields;
            for (const auto& f : sch.fields)
                if (f.section == si) fields.push_back(&f);
            if (fields.empty()) continue;
            std::sort(fields.begin(), fields.end(), [](const DatField* a, const DatField* b) { return a->order < b->order; });
            // Section header.
            const std::wstring title = si < int(sch.sections.size()) ? datLabel(sch.sections[size_t(si)]) : L"Other";
            if (sectionTops_.size() <= size_t(si)) sectionTops_.resize(size_t(si) + 1, 0);
            sectionTops_[size_t(si)] = y + scrollPos;
            y = kitpaint::drawSectionHeader(c, x0, twoCol ? colRight : rc.right - x0, y, scale(stardraft::kit::kRowH), title);
            int widgetsBottom = y;
            if (twoCol) {  // the pictures, under what the right column already holds; none: the fields go on as they are
                const int wy = std::max(y, rightTop), wb = host_.widgets(c, si, colRight + scale(28), wy, full - colW - scale(28));
                if (wb > wy) widgetsBottom = wb, rightTop = wb + scale(14);
            } else {
                y = host_.widgets(c, si, x0 + labelW, y, full - labelW), y += y > widgetsBottom ? scale(14) : 0;
            }
            c.setFont(uiFont);
            auto& hotFields = host_.hotFields();
            for (const DatField* fp : fields) {
                const DatField& f = *fp;
                const bool has = f.covers(u), mod = has && t.changed(f, u);
                const uint32_t v = has ? t.value(f, u) : 0;
                const std::wstring readOnlyWhy = has ? host_.readOnlyWhy(f) : L"";
                const bool ro = !readOnlyWhy.empty();
                const DataFieldRow fr = dataFieldRow(x0, labelW, valW, y, rowH, colRight, s);
                if (ro) host_.area({x0, y, colRight, y + rowH}, nullptr, readOnlyWhy, draw::Cursor::No);
                if (hotFields.count(f.name)) c.fill({x0 - scale(8), y, colRight, y + rowH}, kSel);
                c.text(fr.label, datLabel(f.name), !has || ro ? kFaint : mod ? kGood : kDim, draw::kMiddle | draw::kEllipsis);
                if (!ro) hits_.push_back({fr.label, f.index, -3});
                // A yes/no value: a checkbox (click: 0 or 1).
                if (has && fieldIsBool(dat, f)) {
                    paintDataBool(c, fr, v, ro, mod, hotRect);
                    if (!ro) hits_.push_back({fr.boolCell, f.index, -5});
                    y += rowH;
                    continue;
                }
                // A reference or a choice: a dropdown (icon, name, number, the chevron). Anything else: a number to type.
                if (has && fieldIsDropdown(f)) {
                    const std::string refDat = fieldRefDat(f);
                    const draw::Rect vb = fr.dropBox;
                    const std::wstring note = host_.fieldNote(f, v);
                    kitpaint::drawPickFace(c, vb, note.empty() ? L"#" + std::to_wstring(v) : note, mod, !ro && host_.hot(vb),
                                           refDat.empty() ? draw::Image{} : host_.refIcon(refDat, v), int(v), !ro);
                    if (!ro) hits_.push_back({vb, f.index, -4});
                    const DatSchema* target = f.ref.empty() ? nullptr : DatSchema::find(f.ref);
                    if (target && v < uint32_t(target->entries)) {  // follow it
                        const draw::Rect gt = kitpaint::square(
                            {vb.right + scale(4), y + scale(2), vb.right + scale(4 + stardraft::kit::kIconBox), y + rowH - scale(2)});
                        kitpaint::drawIconFace(c, gt, UiIcon::OpenExternal, kitpaint::Tone::Primary, false, host_.hot(gt));  // open: jump to the entry
                        hits_.push_back({gt, f.index, -2});
                        if (refDat == "sfxdata") host_.soundButton(c, {gt.right + scale(4), gt.top, gt.right + scale(30), gt.bottom}, int(v));
                    }
                    y += rowH;
                    continue;
                }
                const draw::Rect vb = fr.box;
                kitpaint::drawFieldBox(c, vb,
                                       !has || ro ? kitpaint::Field::ReadOnly : mod ? kitpaint::Field::Changed : host_.hot(vb) ? kitpaint::Field::Hot : kitpaint::Field::Normal);
                const bool number = has && !ro && fieldIsNumber(dat, f);
                c.setFont(c.font({draw::Face::Mono}));
                draw::Rect vt = dataValueText(fr.box, s);
                if (number) vt.right = kitpaint::numberArrowsRect(vb).left;
                c.text(vt, has ? fieldValueText(f, v) : L"\x2014", !has || ro ? kFaint : mod ? kGood : kText, draw::kMiddle | draw::kRight);
                c.setFont(uiFont);
                if (number) {  // up and down (click, or the mouse wheel anywhere on the box)
                    draw::Rect up, down;
                    kitpaint::numberArrowHalves(vb, up, down);
                    kitpaint::drawNumberArrows(c, vb, host_.hot(vb), host_.hot(up), host_.hot(down));
                    hits_.push_back({up, f.index, -6});
                    hits_.push_back({down, f.index, -7});
                }
                if (has && !ro) hits_.push_back({vb, f.index, -1});
                const std::wstring note = has ? host_.fieldNote(f, v) : L"not used by this entry";
                if (!note.empty()) {
                    const draw::Rect nt = fr.note;
                    const DatSchema* target = f.ref.empty() ? nullptr : DatSchema::find(f.ref);
                    const bool isLink = has && target && v < uint32_t(target->entries);
                    c.text(nt, note, isLink ? kAccent : mod ? kGood : kDim, draw::kMiddle | draw::kEllipsis);
                    if (isLink) hits_.push_back({{nt.left, nt.top, std::min(nt.right, nt.left + c.measure(note)), nt.bottom}, f.index, -2});
                }
                y += rowH;
                // Flags: a grid of checkboxes under the value.
                if (has && !f.flags.empty())
                    y += paintDataFlags(c, f, t, u, v, x0, labelW, y, colW, s, hotRect,
                                        [this, &f](draw::Rect cell, int b) { hits_.push_back({cell, f.index, b}); });
            }
            if (twoCol) y = std::max(y, widgetsBottom);
            y += scale(14);
        }
        if (dat == "units") y = host_.unitDisplay(c, y);
        y = host_.requirements(c, x0, y, colW);
        y = host_.mapPlayers(c, x0, y + scale(8), colW, labelW, rowH);  // the map's settings for each player
        y += rowH;
    }
    height_ = y + scrollPos;
    if (const int most = std::max(0, height_ - rc.bottom); scrollPos > most)  // taller window: the bar hides, so no scroll is left
        scrollPos = most, invalidate();
    host_.afterPaint(c);
}

inline bool DataFormView::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move:
        if (pressed_ && host_.widgetDrag(e.pt)) return true;
        hover(e.pt);
        return true;
    case MouseKind::Leave:
        host_.hotFields().clear();
        invalidate();  // what shows only under the mouse goes
        return true;
    case MouseKind::Down:
        if (e.button != 0) return false;
        press(e.pt, false);
        return true;
    case MouseKind::Up:
        if (e.button == 0) {
            if (pressed_) {
                pressed_ = false;
                if (draw::Host* h = host()) h->capture(*this, false);
                host_.widgetRelease(false);
            }
            return true;
        }
        if (e.button != 1) return false;
        press(e.pt, true);
        return true;
    case MouseKind::Wheel:
        if (e.horizontal) return false;
        wheel(e);
        return true;
    default: return false;
    }
}

inline void DataFormView::press(draw::Point pt, bool right) {
    if (draw::Host* h = host()) h->focus(*this);
    if (!right && host_.widgetPress(pt)) {
        pressed_ = true;
        if (draw::Host* h = host()) h->capture(*this, true);
        return;
    }
    for (const DataFormHit& hit : hits_) {
        if (!hit.r.contains(pt)) continue;
        const DataFormHit h = hit;  // (an action can paint the form again)
        const DatField* f = field(h.field);
        const DatTable* table = host_.table();
        if (!f || !table) break;
        if (right) {  // a field (its label or value): copy, paste, reset
            host_.fieldMenu(*f, pt);
            break;
        }
        const uint32_t v = table->value(*f, host_.entry());
        if (h.bit == -1) host_.beginEdit(h.field, h.r);
        else if (h.bit == -4) host_.dropdown(*f, h.r);
        else if (h.bit == -5) setValue(*f, v ? 0 : 1);
        else if (h.bit == -6 || h.bit == -7) {  // a number's arrows: one up or down (Shift: 10)
            host_.commitEdit();
            const int64_t step = (host() && (host()->modifiers() & draw::kShift) ? 10 : 1) * (h.bit == -6 ? 1 : -1);
            setValue(*f, uint32_t(std::clamp<int64_t>(int64_t(table->value(*f, host_.entry())) + step, 0, int64_t(f->maxValue()))));
        } else if (h.bit >= 0) setValue(*f, v ^ (1u << h.bit));
        else if (h.bit == -2) host_.goTo(f->ref, int(v));
        break;
    }
}

inline void DataFormView::editNeighbour(int editing, int delta) {
    std::vector<int> boxes;  // fields with a value box, in the order the form shows them
    for (const auto& h : hits_)
        if (h.bit == -1) boxes.push_back(h.field);
    const auto it = std::find(boxes.begin(), boxes.end(), editing);
    host_.commitEdit();
    if (it == boxes.end() || boxes.empty()) return;
    const int next = boxes[size_t((int(it - boxes.begin()) + delta + int(boxes.size())) % int(boxes.size()))];
    auto boxOf = [&](draw::Rect& r) {
        host_.paintNow();  // the hits follow the last paint
        for (const auto& h : hits_)
            if (h.bit == -1 && h.field == next) return r = h.r, true;
        return false;
    };
    draw::Rect r;
    if (!boxOf(r)) return;
    if (r.top < 0 || r.bottom > height()) {
        scrollTo(host_.scroll() + (r.top < 0 ? r.top - scale(40) : r.bottom - height() + scale(40)));
        if (!boxOf(r)) return;
    }
    host_.beginEdit(next, r);
}

inline void DataFormView::hover(draw::Point pt) {
    std::set<std::string> now;
    if (host_.widgetHover(pt, now)) invalidate();
    if (now.empty())
        for (const auto& h : hits_)
            if (h.r.contains(pt) || (pt.y >= h.r.top && pt.y < h.r.bottom && h.bit == -3 && pt.x < h.r.right + scale(560))) {
                if (const DatField* f = field(h.field)) now.insert(f->name);
                break;
            }
    auto& hot = host_.hotFields();
    if (now != hot) {
        hot = std::move(now);
        invalidate();
    }
}

inline void DataFormView::wheel(const draw::MouseEvent& e) {
    if (host_.widgetWheel(e.pt, e.wheel)) return;
    host_.commitEdit();
    const int delta = int(std::lround(e.wheel * 120));  // (the system's wheel units)
    // Over a dropdown: the wheel steps through its values (up: the one above in the list). Not while the form scrolls: a
    // dropdown that comes under the mouse then does not change. Over a number: one up or down (Shift: 10).
    const auto now = std::chrono::steady_clock::now();
    if (now - lastScroll_ > std::chrono::milliseconds(500))
        for (const auto& h : hits_) {
            if (h.bit == -1 && h.r.contains(e.pt)) {
                const DatField* f = field(h.field);
                const DatTable* t = host_.table();
                if (!f || !t || !fieldIsNumber(t->schema().name, *f)) continue;
                const int64_t step = ((e.mods & draw::kShift) ? 10 : 1) * (delta > 0 ? 1 : -1);
                setValue(*f, uint32_t(std::clamp<int64_t>(int64_t(t->value(*f, host_.entry())) + step, 0, int64_t(f->maxValue()))));
                return;
            }
            if (h.bit != -4 || !h.r.contains(e.pt)) continue;
            const DatField* f = field(h.field);
            const DatTable* t = host_.table();
            if (!f || !t) break;
            const std::vector<ListPickItem> items = host_.dropdownItems(*f);
            const std::string cur = std::to_string(t->value(*f, host_.entry()));
            int i = -1;
            for (size_t k = 0; k < items.size(); ++k)
                if (items[k].value == cur) i = int(k);
            const int steps = delta / 120, next = std::clamp(i < 0 ? 0 : i - (steps ? steps : delta > 0 ? 1 : -1), 0, int(items.size()) - 1);
            if (!items.empty() && next != i) {
                setValue(*f, uint32_t(std::stoul(items[size_t(next)].value)));
                host_.status(datLabel(f->name) + L": " + items[size_t(next)].label);
            }
            return;
        }
    lastScroll_ = now;
    scrollTo(host_.scroll() - delta * scale(26) * 3 / 120);
}

inline draw::Cursor DataFormView::cursor(draw::Point pt) {
    draw::Cursor c = draw::Cursor::Arrow;
    if (host_.widgetCursor(pt, c)) return c;
    for (const auto& h : hits_)
        if (h.r.contains(pt) && h.bit != -3)  // a dropdown (-4) is a hand too
            return h.bit == -1 ? draw::Cursor::IBeam : h.bit <= -6 ? draw::Cursor::Arrow : draw::Cursor::Hand;
    return draw::Cursor::Arrow;
}

inline std::wstring DataFormView::feedbackName(draw::Point pt) {
    std::wstring n = host_.widgetName(pt);  // a preview's part: the fields it shows, else its tip
    if (!n.empty()) return n;
    for (const auto& h : hits_)  // a field: its name
        if (h.r.contains(pt))
            if (const DatField* f = field(h.field)) n = datLabel(f->name);
    return n;
}

inline bool DataFormView::vscroll(int code, int pos, int track, int page) {
    host_.commitEdit();
    // The window's bar (the SB_* requests): a line is a row, a page is the visible height.
    int s = host_.scroll();
    switch (code) {
    case 0: s -= scale(26); break;  // SB_LINEUP
    case 1: s += scale(26); break;  // SB_LINEDOWN
    case 2: s -= page; break;       // SB_PAGEUP
    case 3: s += page; break;       // SB_PAGEDOWN
    case 4:                         // SB_THUMBPOSITION
    case 5: s = track; break;       // SB_THUMBTRACK
    case 6: s = 0; break;           // SB_TOP
    case 7: s = 1 << 30; break;     // SB_BOTTOM
    default: (void)pos; break;
    }
    scrollTo(s);
    return true;
}
