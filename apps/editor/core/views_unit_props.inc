// Unit properties (double-click a unit; portable, core.inc): the UNIT entry's fields: owner, hit points / shields / energy
// (percent), resources, hangar, position and the state flags, each enabled only where the unit's type allows it. With
// several units selected the fields you change apply to all of them. "Edit unit type" opens the type in the Data
// workspace (a project). The dialog is a kit Dialog (Host::runModal); the host (main.cpp: showUnitProperties on Windows)
// gives the unit's picture, sets the owner window and opens the type in Data after it.

// The dialog's results (what its buttons return). Edit unit type applies the edits too.
constexpr int kUnitPropsOk = 1, kUnitPropsCancel = 2, kUnitPropsEditType = 3;

const wchar_t* const kUnitStateNames[5] = {L"Cloaked", L"Burrowed", L"In transit", L"Hallucinated", L"Invincible"};

// What the fields hold when OK is clicked (the values as typed, before the limits).
struct UnitPropsValues {
    uint32_t owner = 0, hp = 0, shields = 0, energy = 0, resources = 0, hangar = 0, x = 0, y = 0;
    uint16_t setBits = 0, clearBits = 0;  // the state flags turned on and off
};

// Puts the values into `units`: only what changed from the shown values (`was`, the double-clicked unit) goes to the
// units, so with several units selected the other values stay. The position changes only for one unit.
inline void unitPropsApply(Editor& ed, const std::vector<int>& units, const Unit& was, const UnitPropsValues& v) {
    const uint32_t hp = std::min(v.hp, 100u), sh = std::min(v.shields, 100u), en = std::min(v.energy, 100u), hang = std::min(v.hangar, 65535u);
    const bool single = units.size() == 1;
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
constexpr int kStateStepX = 150, kStateW = 145, kStateStepY = kRowH + 2;
constexpr int kContentW = kColW + kLabelW + kBoxW;          // 464: the window is 496 px wide inside
constexpr int kButtonH = 28;                                // ui::kButtonH: Edit unit type, OK and Cancel
constexpr int kButtonsGap = 24;                             // Edit unit type to OK and Cancel
}  // namespace unitPropsLayout

class UnitPropsView final : public draw::View {
public:
    UnitPropsView(const Unit& first, const std::vector<int>& units, draw::Image icon, bool project) : first_(first), count_(units.size()), icon_(icon) {
        using stardraft::kit::LabelTone;
        const Unit& u = first;
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
        for (int b = 0; b < 5; ++b) {  // only the states this unit can have
            if (!((u.validState >> b) & 1)) continue;
            auto cb = std::make_unique<stardraft::kit::CheckBox>(tr(kUnitStateNames[b]), ((u.state >> b) & 1) != 0);
            cb->setPlainLabel(true);  // (a native check box's label is the text color)
            add(*cb, {0, 0, 0, 0});
            state_[b] = std::move(cb);
            ++shown_;
        }
        // The unit's type: in a project, open it in the Data workspace; on a classic map, on the Map data page (its unit
        // settings: hit points, costs, names).
        type_.setText((project ? tr(L"Edit unit type in Data (") : tr(L"Edit unit type in Map data (")) + unitDisplayName(u.type) + L")");
        type_.onClick = [this] {
            if (dlg_) dlg_->close(kUnitPropsEditType);
        };
        add(type_, {0, 0, 0, 0});
    }

    // The dialog this view is in (Edit unit type closes it).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }

    // The content's height at 100 % (the rows, the check boxes and Edit unit type).
    int contentHeight() const {
        using namespace unitPropsLayout;
        return typeY() + kButtonH;
    }

    // What the fields hold (a call after OK).
    UnitPropsValues values() const {
        auto num = [](const stardraft::kit::TextField& f) { return uint32_t(std::wcstoul(f.text().c_str(), nullptr, 10)); };
        UnitPropsValues v;
        v.owner = uint32_t(std::max(0, owner_.value()));
        v.hp = num(hp_), v.shields = num(shields_), v.energy = num(energy_), v.resources = num(resources_), v.hangar = num(hangar_);
        v.x = num(x_), v.y = num(y_);
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
        const stardraft::kit::TextField* fields[8] = {nullptr, &hp_, &resources_, &shields_, &hangar_, &energy_, &x_, &y_};
        for (int k = 0; k < 8; ++k)
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
        const draw::Rect pic{0, scale(kPad), scale(kIcon), scale(kPad + kIcon)};
        if (icon_) c.fitted(icon_, pic);
        else if (paintIcon) paintIcon(c, pic);  // (a host whose pictures are its own pixels: the SDL program)
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
        c.text({scale(kTextX), scale(kNameY), scale(2000), scale(kNameB)}, unitDisplayName(first_.type), kText, draw::kLeft);
        c.setFont(of);
        const std::wstring sub = count_ > 1 ? std::to_wstring(count_) + tr(L" units selected. Changes apply to all of them.")
                                            : tr(L"Unit ") + std::to_wstring(first_.type) + tr(L"  \x00B7  serial ") + std::to_wstring(first_.serial);
        c.text({scale(kTextX), scale(kSubY), scale(2000), scale(kSubB)}, sub, kDim, draw::kLeft);
    }

    void resized() override {
        using namespace unitPropsLayout;
        const int x0 = 0, x1 = kColW;
        auto row = [](int k) { return kTop + k * (kRowH + kGap); };
        auto put = [](draw::View& v, int x, int y, int w, int h) { v.setFrame({scale(x), scale(y), scale(x + w), scale(y + h)}); };
        auto pair = [&](int li, stardraft::kit::TextField& f, int x, int y) { put(labels_[li], x, y, kLabelW - 8, kRowH), put(f, x + kLabelW, y, kBoxW, kRowH); };
        put(labels_[0], x0, row(0), kLabelW - 8, kRowH);
        put(owner_, x0 + kLabelW, row(0), kColW + kBoxW, kRowH);
        pair(1, hp_, x0, row(1)), pair(2, resources_, x1, row(1));
        pair(3, shields_, x0, row(2)), pair(4, hangar_, x1, row(2));
        pair(5, energy_, x0, row(3));
        pair(6, x_, x0, row(4)), pair(7, y_, x1, row(4));
        int shown = 0;
        for (int b = 0; b < 5; ++b) {
            if (!state_[b]) continue;
            put(*state_[b], x0 + (shown % 3) * kStateStepX, kStatesY + (shown / 3) * kStateStepY, kStateW, kRowH);
            ++shown;
        }
        put(type_, x0, typeY(), kContentW, kButtonH);
    }

private:
    int typeY() const {
        using namespace unitPropsLayout;
        return kStatesY + ((shown_ + 2) / 3) * kStateStepY + kGap;
    }

    Unit first_;
    size_t count_ = 1;
    draw::Image icon_;

public:
    // The picture's paint when the host has no Image for it (it draws its own pixels into the box).
    std::function<void(draw::Canvas&, draw::Rect)> paintIcon;

private:
    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label labels_[8];
    stardraft::kit::Dropdown owner_;
    stardraft::kit::TextField hp_, resources_, shields_, hangar_, energy_, x_, y_;
    std::unique_ptr<stardraft::kit::CheckBox> state_[5];
    int shown_ = 0;
    stardraft::kit::Button type_;
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
    return draw::Point{scale(kContentW + 2 * kPad), scale(h)};
}

inline int runUnitPropsDialog(draw::View& from, Editor& ed, const std::vector<int>& units, const Unit& first, draw::Image icon, bool project,
                              UnitPropsView*& shown, std::function<void(draw::Canvas&, draw::Rect)> paintIcon = {}) {
    UnitPropsView view(first, units, icon, project);
    view.paintIcon = std::move(paintIcon);
    stardraft::kit::Dialog dlg;
    const draw::Point size = unitPropsDialogSetup(dlg, view);
    shown = &view;
    const int r = dlg.run(from, tr(L"Unit properties"), size);
    shown = nullptr;
    if (r == kUnitPropsOk || r == kUnitPropsEditType) unitPropsApply(ed, units, first, view.values());
    return r;
}
