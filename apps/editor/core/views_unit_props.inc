// Unit properties (double-click a unit; portable, core.inc): the UNIT entry's fields: owner, hit points / shields / energy
// (percent), resources, hangar, position and the state flags, each enabled only where the unit's type allows it. With
// several units selected the fields you change apply to all of them. "Edit unit type" opens the type in the Data
// workspace (a project). The dialog is a kit Dialog (Host::runModal); the host (main.cpp: showUnitProperties on Windows)
// gives the unit's picture, sets the owner window and opens the type in Data after it.

// The dialog's results (what its buttons return). Edit unit type applies the edits too.
constexpr int kUnitPropsOk = 1, kUnitPropsCancel = 2, kUnitPropsEditType = 3;
// The list's results (Discord feedback #126): a pick (the window shows the picked unit next) and a change of the map
// (Delete, undo, redo: the window again, at the map's first unit).
constexpr int kUnitPropsPick = 4, kUnitPropsRefresh = 5;

const wchar_t* const kUnitStateNames[5] = {L"Cloaked", L"Burrowed", L"Lifted", L"Hallucinated", L"Invincible"};

// What the fields hold when OK is clicked (the values as typed, before the limits).
struct UnitPropsValues {
    uint32_t owner = 0, hp = 0, shields = 0, energy = 0, resources = 0, hangar = 0, x = 0, y = 0;
    uint32_t unitType = 0;  // the unit ID (the type): the units become this type when it differs from the shown one
    uint16_t setBits = 0, clearBits = 0;  // the state flags turned on and off
};

// Puts the values into `units`: only what changed from the shown values (`was`, the double-clicked unit) goes to the
// units, so with several units selected the other values stay. The position changes only for one unit.
inline void unitPropsApply(Editor& ed, const std::vector<int>& units, const Unit& was, const UnitPropsValues& v) {
    const uint32_t hp = std::min(v.hp, 100u), sh = std::min(v.shields, 100u), en = std::min(v.energy, 100u), hang = std::min(v.hangar, 65535u);
    const bool single = units.size() == 1;
    ed.beginStroke();  // (the type, then the values: one undo step)
    if (v.unitType != was.type && v.unitType < ed.unitTypes().size()) {  // the units of each type they have now become the new type
        std::vector<uint16_t> from;
        for (int i : units)
            if (std::find(from.begin(), from.end(), ed.units()[size_t(i)].type) == from.end()) from.push_back(ed.units()[size_t(i)].type);
        for (uint16_t t : from) ed.replaceUnits(t, uint16_t(v.unitType), true);
    }
    ed.updateUnits(units, [&](Unit& u) {
        if (v.owner != was.owner && (u.validProps & UnitProp::Owner)) u.owner = uint8_t(v.owner);
        if (hp != was.hitPoints && (u.validProps & UnitProp::HitPoints)) u.hitPoints = uint8_t(hp);
        if (sh != was.shields && (u.validProps & UnitProp::Shields)) u.shields = uint8_t(sh);
        if (en != was.energy && (u.validProps & UnitProp::Energy)) u.energy = uint8_t(en);
        if (v.resources != was.resources && (u.validProps & UnitProp::Resources)) u.resources = v.resources;
        if (hang != was.hangar && (u.validProps & UnitProp::Hangar)) u.hangar = uint16_t(hang);
        u.state = uint16_t((u.state | (v.setBits & u.validState)) & ~v.clearBits);
        if (single) u.x = uint16_t(std::min(v.x, 65535u)), u.y = uint16_t(std::min(v.y, 65535u));
    });
    ed.endStroke();
}

// The layout is the old window's, in pixels at 100 %, from the window's client area (the content view starts 16 px in
// from the left and at the top edge, so its y is the window's). Rows of kFieldH (24 px), 8 px apart, under the picture
// and the name; the label column is 110 px, a field 110 px, a second column 244 px to the right of the first.
namespace unitPropsLayout {
constexpr int kPad = 16;                                    // the window's edge to the content (ui::kDialogPad)
constexpr int kRowH = 24, kGap = 8;                         // ui::kFieldH; between the rows
constexpr int kLabelW = 110, kBoxW = 110, kColW = kLabelW + kBoxW + 24;
constexpr int kTop = kPad + 52;                             // the first row: under the picture and the name
constexpr int kIcon = 40;                                   // the picture's box: kPad, kPad, 40 px square
constexpr int kTextX = kIcon + 12;                          // the name and the line under it (from the content's left)
constexpr int kNameY = kPad - 2, kNameB = kPad + 24, kSubY = kPad + 22, kSubB = kPad + 42;
constexpr int kStatesY = kTop + 4 * (kRowH + kGap) + kRowH + kGap + 6;  // the state check boxes, 3 to a row
constexpr int kStateStepX = 150, kStateColX = 232, kStateW = 200, kStateStepY = kRowH + 2;
constexpr int kContentW = kColW + kLabelW + kBoxW;          // 464: the window is 496 px wide inside
constexpr int kListW = 300, kListGap = 16;                  // the Units list: the left column; the fields start after it
constexpr int kRightX = kListW + kListGap;                  // the fields' column (and the picture, the name) from the left
constexpr int kButtonH = 28;                                // ui::kButtonH: Edit unit type, OK and Cancel
constexpr int kButtonsGap = 24;                             // Edit unit type to OK and Cancel
}  // namespace unitPropsLayout

// The Units list's rows (Discord feedback #126): a plain click or Enter picks a row (the window shows that unit next),
// Ctrl or Shift with a click only changes the selection. Delete, Ctrl+Z and Ctrl+Y go to the view (listKey).
struct UnitRowsView final : stardraft::kit::ListView {
    std::function<bool(const draw::KeyEvent&)> keyHook;  // true: the key is taken
    bool plainPick = false;                                // the last press was a plain click
    std::function<void(int)> onHeader;  // a click on a header cell: its column
    // The selected rows can be dragged to another place (the units' order) when the list is in index order: a press on
    // a selected row, a drag past a few points and a release. The line shows where they go; onDrop gets that row (the
    // count: the end). A press without a drag is a click, as before.
    bool draggable = false;
    std::function<void(int)> onDrop;
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && e.button == 0 && onHeader) {
            const int col = columnAt(e.pt);
            if (col >= 0) return onHeader(col), true;
        }
        if (draggable && e.kind == draw::MouseKind::Down && e.button == 0 && !(e.mods & (draw::kCtrl | draw::kShift))) {
            const int r = rowAt(e.pt);
            if (r >= 0 && isSelected(r)) return pressed_ = true, dragging_ = false, down_ = e, true;
        }
        if (pressed_ && e.kind == draw::MouseKind::Move) {
            const int dx = e.pt.x - down_.pt.x, dy = e.pt.y - down_.pt.y;
            if (!dragging_ && (dx * dx + dy * dy > 16)) dragging_ = true;
            if (dragging_) {
                dropAt_ = dropRowAt(e.pt);
                invalidate();
            }
            return true;
        }
        if (pressed_ && e.kind == draw::MouseKind::Up && e.button == 0) {
            pressed_ = false;
            if (dragging_) {
                const int at = dropAt_;
                dragging_ = false, dropAt_ = -1;
                invalidate();
                if (onDrop) onDrop(at);
                return true;
            }
            stardraft::kit::ListView::mouse(down_);  // (a click: the press, then the release, as the list gets them)
            return stardraft::kit::ListView::mouse(e);
        }
        if (e.kind == draw::MouseKind::Down && e.button == 0) plainPick = !(e.mods & (draw::kCtrl | draw::kShift));
        return stardraft::kit::ListView::mouse(e);
    }
    // Where a drag would put the rows: before row `r` (the count: after the last one).
    int dropRowAt(draw::Point pt) const {
        const int n = count();
        if (n == 0) return 0;
        const int r = rowAt(pt);
        if (r >= 0) {
            const draw::Rect rr = rowRect(r);
            return pt.y < (rr.top + rr.bottom) / 2 ? r : r + 1;
        }
        return pt.y < rowRect(0).top ? 0 : n;
    }
    void paint(draw::Canvas& c) override {
        stardraft::kit::ListView::paint(c);
        if (dragging_ && dropAt_ >= 0) {  // the line where the rows go
            const int y = dropAt_ < count() ? rowRect(dropAt_).top : rowRect(count() - 1).bottom;
            c.fill({0, y - 1, width(), y + 1}, stardraft::kit::theme().accent);
        }
    }
    bool pressed_ = false, dragging_ = false;
    int dropAt_ = -1;
    draw::MouseEvent down_{};
    bool key(const draw::KeyEvent& e) override {
        plainPick = false;
        if (keyHook && keyHook(e)) return true;
        return stardraft::kit::ListView::key(e);
    }
};

class UnitPropsView final : public draw::View {
public:
    UnitPropsView(Editor& ed, const Unit& first, const std::vector<int>& units, draw::Image icon, bool project) : first_(first), count_(units.size()), icon_(icon) {
        using stardraft::kit::LabelTone;
        ed_ = &ed;
        const Unit& u = first;
        // The Units list (the left column): the search box over the rows, a row for each unit of the map.
        search_.setPlaceholder(std::wstring(tr(L"Search the units")));
        search_.onChange = [this] { rebuild(); };
        list_.setMode(stardraft::kit::SelectMode::Multiple);
        setHeaders();
        list_.cell = [this](int row, size_t k) {
            if (row < 0 || row >= int(rows_.size()) || rows_[size_t(row)] >= int(ed_->units().size())) return std::wstring();
            return unitsListCell(*ed_, rows_[size_t(row)], k);
        };
        list_.onSelect = [this] { picked(); };
        list_.onHeader = [this](int col) { sortBy(col); };
        list_.onDrop = [this](int before) { droppedAt(before); };
        list_.onActivate = [this](int row) {
            if (row < 0 || row >= int(rows_.size())) return;
            selectFromList();
            unitsListCenter(rows_[size_t(row)]);
            pickAt(row);
        };
        list_.keyHook = [this](const draw::KeyEvent& e) { return listKey(e); };
        add(search_, {0, 0, 0, 0});
        add(list_, {0, 0, 0, 0});
        auto label = [&](stardraft::kit::Label& l, const wchar_t* t) {
            l.setText(tr(t)), l.setTone(LabelTone::Dim);  // (the native labels were the dim color)
            add(l, {0, 0, 0, 0});
        };
        auto number = [&](stardraft::kit::TextField& f, uint32_t v, bool on) {
            f.setText(std::to_wstring(v));
            f.setFont({draw::Face::Mono, 0}), f.setAlignRight(true);
            f.setTrailing(scale(2));  // (the native edit's right margin: the digits end where they did)
            f.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });  // ES_NUMBER
            if (!on) {  // a disabled edit box: read only, not a Tab stop, its text faint
                f.setReadOnly(true), f.setTabStop(false);
                f.setSpans({{0, size_t(-1) / 2, stardraft::kit::theme().faint, false}});
            }
            add(f, {0, 0, 0, 0});
        };
        label(labels_[0], L"Owner");
        std::vector<stardraft::kit::DropdownItem> players;
        for (int i = 0; i < 12; ++i) players.push_back({tr(L"Player ") + std::to_wstring(i + 1) + (i == 11 ? tr(L" (neutral)") : L""), -1, -1});
        owner_.setItems(players);
        owner_.setValue(std::min<int>(u.owner, 11));
        owner_.setEnabled((u.validProps & UnitProp::Owner) != 0);
        add(owner_, {0, 0, 0, 0});
        const bool single = units.size() == 1;
        label(labels_[1], L"Hit points %"), number(hp_, u.hitPoints, u.validProps & UnitProp::HitPoints);
        label(labels_[2], L"Resources"), number(resources_, u.resources, u.validProps & UnitProp::Resources);
        label(labels_[3], L"Shields %"), number(shields_, u.shields, u.validProps & UnitProp::Shields);
        label(labels_[4], L"Hangar"), number(hangar_, u.hangar, u.validProps & UnitProp::Hangar);
        label(labels_[5], L"Energy %"), number(energy_, u.energy, u.validProps & UnitProp::Energy);
        label(labels_[6], L"X (pixels)"), number(x_, u.x, single);
        label(labels_[7], L"Y (pixels)"), number(y_, u.y, single);
        label(labels_[8], L"Unit ID"), number(unitId_, u.type, true);
        for (int b = 0; b < 5; ++b) {  // every state; the ones this unit cannot have are greyed (the valid states of the game)
            auto cb = std::make_unique<stardraft::kit::CheckBox>(tr(kUnitStateNames[b]), ((u.state >> b) & 1) != 0);
            cb->setPlainLabel(true);  // (a native check box's label is the text color)
            cb->setEnabled(((u.validState >> b) & 1) != 0);
            add(*cb, {0, 0, 0, 0});
            state_[b] = std::move(cb);
        }
        // The unit's type: in a project, open it in the Data workspace; on a classic map, on the Map data page (its unit
        // settings: hit points, costs, names).
        type_.setText((project ? tr(L"Edit unit type in Data (") : tr(L"Edit unit type in Map data (")) + unitDisplayName(u.type) + L")");
        type_.onClick = [this] {
            if (dlg_) dlg_->close(kUnitPropsEditType);
        };
        add(type_, {0, 0, 0, 0});
        link_.setText(std::wstring(tr(L"Link selected units")));
        link_.onClick = [this] { linkSelected(); };
        add(link_, {0, 0, 0, 0});
        jump_.setText(std::wstring(tr(L"Jump to linked unit")));
        jump_.onClick = [this] { jumpToLinked(); };
        add(jump_, {0, 0, 0, 0});
        link_.setEnabled(false), jump_.setEnabled(false);  // (greyed until a link applies)
    }

    // The dialog this view is in (Edit unit type closes it).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }

    // The content's height at 100 % (the rows, the check boxes and Edit unit type).
    int contentHeight() const {
        using namespace unitPropsLayout;
        return typeY() + kButtonH;
    }

    // The unit a pick asked the window to show next (kUnitPropsPick).
    int nextUnit() const { return next_; }

    // What the fields hold (a call after OK).
    UnitPropsValues values() const {
        auto num = [](const stardraft::kit::TextField& f) { return uint32_t(std::wcstoul(f.text().c_str(), nullptr, 10)); };
        UnitPropsValues v;
        v.owner = uint32_t(std::max(0, owner_.value()));
        v.hp = num(hp_), v.shields = num(shields_), v.energy = num(energy_), v.resources = num(resources_), v.hangar = num(hangar_);
        v.x = num(x_), v.y = num(y_), v.unitType = num(unitId_);
        for (int b = 0; b < 5; ++b) {
            if (!state_[b]) continue;
            const bool on = state_[b]->on(), wasOn = (first_.state >> b) & 1;
            if (on && !wasOn) v.setBits |= uint16_t(1 << b);
            if (!on && wasOn) v.clearBits |= uint16_t(1 << b);
        }
        return v;
    }

    // For the self-tests: the fields, a state's check box (null: the unit cannot have it), and a close as a button does.
    stardraft::kit::TextField& hitPointsField() { return hp_; }
    stardraft::kit::Dropdown& ownerList() { return owner_; }
    stardraft::kit::CheckBox* stateBox(int b) { return b >= 0 && b < 5 ? state_[b].get() : nullptr; }
    void close(int result) {
        if (dlg_) dlg_->close(result);
    }

    // Report feedback: what is at `pt`: the owner's player, a field's label, a check box's or a button's text (as the
    // native controls gave their text).
    std::wstring feedbackName(draw::Point pt) override {
        auto in = [&](const draw::View& v) {
            const draw::Rect f = v.frame();
            return v.visible() && pt.x >= f.left && pt.x < f.right && pt.y >= f.top && pt.y < f.bottom;
        };
        if (in(owner_)) return owner_.value() >= 0 && owner_.value() < int(owner_.items().size()) ? owner_.items()[size_t(owner_.value())].label : L"";
        const stardraft::kit::TextField* fields[9] = {nullptr, &hp_, &resources_, &shields_, &hangar_, &energy_, &x_, &y_, &unitId_};
        for (int k = 0; k < 9; ++k)
            if (in(labels_[k]) || (fields[k] && in(*fields[k]))) return labels_[k].text();
        for (const auto& s : state_)
            if (s && in(*s)) return s->label();
        if (in(type_)) return type_.text();
        return {};
    }

    void paint(draw::Canvas& c) override {
        using namespace unitPropsLayout;
        c.fill({0, 0, width(), height()}, kBg);
        // The unit's picture and name over the fields.
        sync();
        refreshLinks();
        if (focusSearch_ && !focused_ && host()) focused_ = true, search_.field().focus();  // the search has the keyboard: type to find a unit
        const int rightPx = width() - scale(kContentW);  // (the fields' column: at the right edge, as resized lays it out)
        const draw::Rect pic{rightPx, scale(kPad), rightPx + scale(kIcon), scale(kPad + kIcon)};
        if (icon_) c.fitted(icon_, pic);
        else if (paintIcon) paintIcon(c, pic);  // (a host whose pictures are its own pixels: the SDL program)
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
        c.text({rightPx + scale(kTextX), scale(kNameY), scale(2000), scale(kNameB)}, unitDisplayName(first_.type), kText, draw::kLeft);
        c.setFont(of);
        const std::wstring sub = count_ > 1 ? std::to_wstring(count_) + tr(L" units selected. Changes apply to all of them.")
                                            : tr(L"Unit ") + std::to_wstring(first_.type) + tr(L"  \x00B7  serial ") + std::to_wstring(first_.serial);
        c.text({rightPx + scale(kTextX), scale(kSubY), scale(2000), scale(kSubB)}, sub, kDim, draw::kLeft);
    }

    void resized() override {
        using namespace unitPropsLayout;
        // The fields keep their width at the right edge; the Units list takes the rest of the window.
        const int rightPx = width() - scale(kContentW);
        auto put = [&](draw::View& v, int x, int y, int w, int h) { v.setFrame({rightPx + scale(x), scale(y), rightPx + scale(x + w), scale(y + h)}); };
        auto row = [](int k) { return kTop + k * (kRowH + kGap); };
        const int listW = std::max(scale(120), rightPx - scale(kListGap));
        search_.setFrame({0, scale(kPad), listW, scale(kPad + kRowH)});
        list_.setFrame({0, scale(kPad + kRowH + kGap), listW, std::max(scale(80), height() - scale(kPad + kRowH + kGap))});  // (the rows fill the window down to the buttons)
        auto pair = [&](int li, stardraft::kit::TextField& f, int x, int y) { put(labels_[li], x, y, kLabelW - 8, kRowH), put(f, x + kLabelW, y, kBoxW, kRowH); };
        put(labels_[0], 0, row(0), kLabelW - 8, kRowH);
        put(owner_, kLabelW, row(0), kColW + kBoxW, kRowH);
        pair(1, hp_, 0, row(1)), pair(2, resources_, kColW, row(1));
        pair(3, shields_, 0, row(2)), pair(4, hangar_, kColW, row(2));
        pair(5, energy_, 0, row(3));
        pair(6, x_, 0, row(4)), pair(7, y_, kColW, row(4));
        pair(8, unitId_, kColW, row(3));
        // The states: Invincible and Burrowed at the left, Hallucinated, Cloaked and Lifted at the right (SCMDraft's order).
        static const int stateCol[5] = {1, 0, 1, 1, 0}, stateRow[5] = {1, 1, 2, 0, 0};  // (by the state bit: Cloaked, Burrowed, Lifted, Hallucinated, Invincible)
        for (int b = 0; b < 5; ++b) put(*state_[b], stateCol[b] * kStateColX, kStatesY + stateRow[b] * kStateStepY, kStateW, kRowH);
        put(link_, 0, linkY(), kContentW / 2 - 4, kButtonH), put(jump_, kContentW / 2 + 4, linkY(), kContentW / 2 - 4, kButtonH);
        put(type_, 0, typeY(), kContentW, kButtonH);
    }

private:
    // The index of the unit this window shows (by its serial), -1 when it is gone.
    int shownUnit() const {
        for (int i = 0; i < int(ed_->units().size()); ++i)
            if (ed_->units()[size_t(i)].serial == first_.serial) return i;
        return -1;
    }
    // The units the list has selected, in the list's order.
    std::vector<int> selectedUnits() const {
        std::vector<int> out;
        for (int r = 0; r < int(rows_.size()); ++r)
            if (list_.isSelected(r)) out.push_back(rows_[size_t(r)]);
        return out;
    }
    // Link selected units is on for two selected units that can be linked; Jump to linked unit for a shown unit with a link.
    void refreshLinks() {
        const std::vector<int> sel = selectedUnits();
        const bool link = sel.size() == 2 && ed_->canLinkUnits(sel[0], sel[1]);
        const bool jump = ed_->linkedUnit(shownUnit()) >= 0;
        if (link != linkOn_) link_.setEnabled(link), linkOn_ = link;
        if (jump != jumpOn_) jump_.setEnabled(jump), jumpOn_ = jump;
    }
    // Link selected units: the two selected units are linked (one undo step); the rows follow.
    void linkSelected() {
        const std::vector<int> sel = selectedUnits();
        if (sel.size() == 2 && ed_->linkUnits(sel[0], sel[1])) rebuild();
    }
    // Jump to linked unit: the window shows the linked unit next, and the view goes to it.
    void jumpToLinked() {
        const int other = ed_->linkedUnit(shownUnit());
        if (other < 0) return;
        next_ = other;
        unitsListCenter(other);
        closeWith(kUnitPropsPick);
    }

    // The header: the five columns, the sorted one with its arrow. The list is in the index's order at first.
    void setHeaders() {
        static const wchar_t* const names[5] = {L"Unit type", L"Owner", L"X", L"Y", L"Index"};
        static const int widths[5] = {0, 70, 52, 52, 46};
        static const stardraft::kit::ColAlign aligns[5] = {stardraft::kit::ColAlign::Left, stardraft::kit::ColAlign::Left,
                                                           stardraft::kit::ColAlign::Right, stardraft::kit::ColAlign::Right,
                                                           stardraft::kit::ColAlign::Right};
        std::vector<stardraft::kit::Column> cols;
        for (int k = 0; k < 5; ++k) {
            std::wstring name(tr(names[k]));
            if (k == sortCol_) name += sortDesc_ ? L" \u25BC" : L" \u25B2";
            cols.push_back({name, widths[k], aligns[k]});
        }
        list_.setColumns(cols);
        list_.draggable = sortCol_ == 4 && !sortDesc_;  // (the rows move in the index's order only)
    }
    // The selected rows go before row `before` (the end when past the last one): the units' order in the map (one undo step).
    void droppedAt(int before) {
        std::vector<int> units;
        for (int r = 0; r < int(rows_.size()); ++r)
            if (list_.isSelected(r)) units.push_back(rows_[size_t(r)]);
        if (units.empty()) return;
        const int target = before < int(rows_.size()) ? rows_[size_t(before)] : int(ed_->units().size());
        if (ed_->moveUnits(units, target) > 0) rebuild();
    }
public:
    // The order as a click on a header gives it (the snapshots: --dialog unitslist:<column>[:desc]).
    void setSort(int col, bool desc) { sortCol_ = col, sortDesc_ = desc, setHeaders(), rebuild(); }

private:
    // A click on a header: the list is sorted by its column; the same column again turns the order.
    void sortBy(int col) {
        if (col == sortCol_) sortDesc_ = !sortDesc_;
        else sortCol_ = col, sortDesc_ = false;
        setHeaders();
        rebuild();
    }
    // The rows' order: by the sorted column (ties keep the index's order).
    void sortRows() {
        const Editor& ed = *ed_;
        const bool desc = sortDesc_;
        std::stable_sort(rows_.begin(), rows_.end(), [&](int a, int b) {
            const Unit& ua = ed.units()[size_t(a)];
            const Unit& ub = ed.units()[size_t(b)];
            bool less = false, equal = false;
            switch (sortCol_) {
            case 0: {
                const std::wstring na = unitDisplayName(ua.type), nb = unitDisplayName(ub.type);
                less = na < nb, equal = na == nb;
                break;
            }
            case 1: less = ua.owner < ub.owner, equal = ua.owner == ub.owner; break;
            case 2: less = ua.x < ub.x, equal = ua.x == ub.x; break;
            case 3: less = ua.y < ub.y, equal = ua.y == ub.y; break;
            default: less = a < b, equal = a == b; break;
            }
            if (equal) return a < b;
            return desc ? !less : less;
        });
    }

    // The rows for the map now and the search (again when either changed).
    void rebuild() { rev_ = ~uint64_t(0), sync(); }
    void sync() {
        const uint64_t rev = ed_->revision();
        if (rev == rev_ && ed_->units().size() == seen_) return;
        rev_ = rev, seen_ = ed_->units().size();
        rows_ = unitsListRows(*ed_, search_.text());
        sortRows();
        list_.setCount(int(rows_.size()));
        list_.clearSelection();
        for (int r = 0; r < int(rows_.size()); ++r)
            if (ed_->isSelected(rows_[size_t(r)])) list_.toggle(r);
        list_.invalidate();
    }
    // The map's selection is the list's picked rows; the units the list does not show (the search hides them) keep theirs.
    void selectFromList() {
        std::vector<int> keep;
        for (int u : ed_->selection())
            if (std::find(rows_.begin(), rows_.end(), u) == rows_.end()) keep.push_back(u);
        ed_->clearSelection();
        for (int u : keep) ed_->select(u, true);
        for (int r = 0; r < int(rows_.size()); ++r)
            if (list_.isSelected(r)) ed_->select(rows_[size_t(r)], true);
    }
    void closeWith(int result) {
        if (dlg_) dlg_->close(result);
    }
    // A pick: the window closes and opens again with the picked unit (its edits are not kept).
    void pickAt(int row) {
        next_ = rows_[size_t(row)];
        closeWith(kUnitPropsPick);
    }
    // A selection change in the list: the map's selection follows and the view goes to the cursor's unit; a plain click picks.
    void picked() {
        selectFromList();
        const int cur = list_.cursor();
        if (cur < 0 || cur >= int(rows_.size())) return;
        unitsListCenter(rows_[size_t(cur)]);
        if (list_.plainPick) list_.plainPick = false, pickAt(cur);
    }
    // Delete deletes the selected units; Ctrl+Z and Ctrl+Y are the map's undo and redo. Then the window opens again.
    void deleteSelected() {
        selectFromList();
        if (ed_->selection().empty()) return;
        ed_->deleteSelection();
        closeWith(kUnitPropsRefresh);
    }
    void undoRedo(bool redo) {
        if (redo ? !ed_->canRedo() : !ed_->canUndo()) return;
        if (redo) ed_->redo();
        else ed_->undo();
        closeWith(kUnitPropsRefresh);
    }
    bool listKey(const draw::KeyEvent& e) {
        if (!e.down) return false;
        if (e.key == draw::Key::Delete && !e.mods) return deleteSelected(), true;
        const bool ctrl = (e.mods & draw::kCtrl) != 0;
        if (ctrl && int(e.key) == 'Z') return undoRedo(false), true;
        if (ctrl && int(e.key) == 'Y') return undoRedo(true), true;
        return false;
    }

    int linkY() const {
        using namespace unitPropsLayout;
        return kStatesY + 3 * kStateStepY + kGap;
    }
    int typeY() const {
        using namespace unitPropsLayout;
        return linkY() + kButtonH + kGap;
    }

    Unit first_;
    size_t count_ = 1;
    draw::Image icon_;

public:
    // The picture's paint when the host has no Image for it (it draws its own pixels into the box).
    std::function<void(draw::Canvas&, draw::Rect)> paintIcon;

private:
    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label labels_[9];
    stardraft::kit::Dropdown owner_;
    stardraft::kit::TextField hp_, resources_, shields_, hangar_, energy_, x_, y_, unitId_;
    std::unique_ptr<stardraft::kit::CheckBox> state_[5];
    int shown_ = 0;
    stardraft::kit::Button type_;
    stardraft::kit::Button link_, jump_;  // Nydus / add-on link (below the states)
    bool linkOn_ = false, jumpOn_ = false;
    Editor* ed_ = nullptr;
    SearchBox search_;
    UnitRowsView list_;
    std::vector<int> rows_;             // the unit of each row
    uint64_t rev_ = ~uint64_t(0);       // the map's revision and units' count the rows are of
    size_t seen_ = 0;
    bool focusSearch_ = true, focused_ = false;
    int next_ = -1;                     // the unit a pick shows next
    int sortCol_ = 4;                   // the rows' order: the column (4: the index, the map's order), and descending
    bool sortDesc_ = false;
};

// Runs Unit properties from `from` (the program's anchor view) for `units`, showing `first` (the double-clicked unit)
// with its picture `icon`. OK and Edit unit type put the changed values into the units (one undo step); Cancel and the
// window's close box change nothing. Returns the button that closed it. `shown` is the content while the dialog is
// open, so the host can find its window.
// The dialog around `view`: its buttons and insets. Returns the window's size (the snapshots show it without a window).
inline draw::Point unitPropsDialogSetup(stardraft::kit::Dialog& dlg, UnitPropsView& view) {
    using namespace unitPropsLayout;
    dlg.setContent(view);
    view.setDialog(&dlg);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"OK"), kUnitPropsOk, true, false, false, 0});
    buttons.push_back({tr(L"Cancel"), kUnitPropsCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setInsets({kPad, 0, kPad, kPad, kButtonsGap, 8});  // the content from the top edge (it places the picture at kPad)
    const int h = view.contentHeight() + kButtonsGap + kButtonH + kPad;
    return draw::Point{scale(kRightX + kContentW + 2 * kPad), scale(h)};
}

// The window's place and size (kept between the openings: resizable, at `place` in screen points when `placed`, `size` in
// pixels when it is set). `closing` runs when the window closes, before it goes.
struct UnitPropsWindow {
    bool placed = false;
    draw::Point place{}, size{};
    std::function<void()> closing;
};

inline int runUnitPropsDialog(draw::View& from, Editor& ed, const std::vector<int>& units, const Unit& first, draw::Image icon, bool project,
                              UnitPropsView*& shown, std::function<void(draw::Canvas&, draw::Rect)> paintIcon = {}, int* next = nullptr,
                              const UnitPropsWindow* win = nullptr) {
    UnitPropsView view(ed, first, units, icon, project);
    view.paintIcon = std::move(paintIcon);
    stardraft::kit::Dialog dlg;
    draw::Point size = unitPropsDialogSetup(dlg, view);
    if (win) {
        dlg.setResizable(true);
        if (win->placed) dlg.setPlace(win->place);
        if (win->size.x > 0 && win->size.y > 0) size = win->size;
        dlg.closing = win->closing;
    }
    shown = &view;
    const int r = dlg.run(from, tr(L"Unit properties"), size);
    shown = nullptr;
    if (r == kUnitPropsPick && next) *next = view.nextUnit();
    if (r == kUnitPropsOk || r == kUnitPropsEditType) unitPropsApply(ed, units, first, view.values());
    return r;
}
