// Location properties (double-click a location, Enter, or double-click its row; portable, core.inc): the name, the
// edges in pixels as the location has them (Left, Top, Right, Bottom, typed freely: left more than right inverts it
// left and right, top more than bottom inverts it top and bottom), the Type list (Normal or which inversion: it shows
// the type the edges give, and a type picked in it swaps the edges to give it) and the elevations in the location (MRGN
// flags). OK sets all of them as one undo step
// (Editor::editLocation). The dialog is a kit Dialog (Host::runModal); the host (main.cpp: showLocationEdit on Windows)
// selects the location, sets the owner window and refreshes the list after it.

// The dialog's results (what its buttons return).
constexpr int kLocPropsOk = 1, kLocPropsCancel = 2;

const wchar_t* const kElevationNames[6] = {L"Low ground", L"Medium ground", L"High ground", L"Low air", L"Medium air", L"High air"};
const wchar_t* const kEdgeNames[4] = {L"Left", L"Top", L"Right", L"Bottom"};

// The Type list's items: the axes on which a location's edges are inverted (1 left and right, 2 top and bottom).
const wchar_t* const kLocationTypes[4] = {L"Normal", L"Inverted left and right", L"Inverted top and bottom", L"Inverted on both"};
inline int locationTypeOf(const Rect& edges) { return (edges.left > edges.right ? 1 : 0) | (edges.top > edges.bottom ? 2 : 0); }

// The size line and the error line for the edges as typed (each a number of up to 9 digits, or not a number), and the
// type they give (locationTypeOf; -1: not numbers). The error is empty when the edges are numbers; the size line is
// then the size in pixels and in tiles, else a dash.
struct LocPropsCheck {
    bool ok = false;
    Rect rect;  // the edges as typed (not put in order)
    int type = -1;
    std::wstring size, error;
};
inline LocPropsCheck locPropsCheck(const std::wstring (&edges)[4]) {
    LocPropsCheck c;
    int v[4];
    bool numbers = true;
    for (int k = 0; k < 4; ++k) {
        if (edges[k].empty() || edges[k].size() > 9) numbers = false;
        else v[k] = int(std::wcstol(edges[k].c_str(), nullptr, 10));
    }
    if (!numbers) c.error = L"Type a number in each edge";
    else c.rect = {v[0], v[1], v[2], v[3]}, c.type = locationTypeOf(c.rect);
    if (c.error.empty()) {
        const int w = std::abs(c.rect.right - c.rect.left), h = std::abs(c.rect.bottom - c.rect.top);
        auto tiles = [](int px) {
            wchar_t b[32];
            if (px % 32) std::swprintf(b, 32, L"%.1f", px / 32.0);
            else std::swprintf(b, 32, L"%d", px / 32);
            return std::wstring(b);
        };
        c.size = std::to_wstring(w) + L" x " + std::to_wstring(h) + L" pixels  \x00B7  " + tiles(w) + L" x " + tiles(h) + L" tiles";
        c.ok = true;
    } else {
        c.size = L"\x2014";
    }
    return c;
}

// Puts the edit into location `loc` (one undo step) and says what happened in the status bar.
inline void locPropsApply(Editor& ed, int loc, const Editor::LocationEdit& e) {
    std::string err;
    if (ed.editLocation(loc, e, &err)) setMessage(L"Changed the location \"" + widen(ed.locationName(loc)) + L"\"");
    else if (!err.empty()) setMessage(L"Could not change the location: " + widen(err));
}

// The layout is the old window's, in pixels at 100 %, from the window's client area (the content view starts 16 px in
// from the left and at the top edge, so its y is the window's). Rows of kFieldH (24 px) at kFormRowH (32 px); a label
// column of 64 px, an edge box 104 px, the second edge column 192 px to the right of the first.
namespace locPropsLayout {
constexpr int kPad = 16;                                   // ui::kDialogPad
constexpr int kRowH = 24, kPitch = 32;                     // ui::kFieldH, ui::kFormRowH
constexpr int kLabelW = 64, kBoxW = 104, kColW = kLabelW + kBoxW + 24;
constexpr int kContentW = kColW + kLabelW + kBoxW;         // 360: the window is 392 px wide inside
constexpr int kEdgesY = kPad + kPitch;                     // 48
constexpr int kSizeY = kEdgesY + 2 * kPitch;               // 112
constexpr int kTypeY = kSizeY + kPitch;                    // 144: the type line (Normal, or which inversion)
constexpr int kHeadingY = kTypeY + kPitch + 8;             // 184: the elevations' heading, a line under it at 208
constexpr int kElevY = kHeadingY + 24 + 8;                 // 216: the elevations' check boxes, 3 to a row
constexpr int kElevStepX = kContentW / 3, kElevW = kElevStepX - 8;
constexpr int kButtonsY = kElevY + 2 * kPitch + 16;        // 296: OK and Cancel (ui::kButtonH, 28 px)
constexpr int kButtonH = 28;
constexpr int kWindowH = kButtonsY + kButtonH + kPad;      // 340
constexpr int kErrorW = kContentW - 2 * 88 - 8 - 8;        // the error line: up to OK (two buttons of ui::kButtonMinW)
}  // namespace locPropsLayout

class LocationPropsView final : public draw::View {
public:
    // `edges`: the location's edges as it has them (Editor::locationEdges). The Anywhere location keeps its edges (its
    // Type list is off).
    LocationPropsView(const std::wstring& name, const Rect& edges, uint16_t excluded, bool anywhere, bool focusName)
        : focusName_(focusName) {
        using stardraft::kit::LabelTone;
        auto label = [&](stardraft::kit::Label& l, const wchar_t* t) {
            l.setText(tr(t)), l.setTone(LabelTone::Dim);  // (the native labels were the dim color)
            add(l, {0, 0, 0, 0});
        };
        label(labels_[0], L"Name");
        name_.setText(name);
        name_.setMaxLength(200);
        add(name_, {0, 0, 0, 0});
        const int vals[4] = {edges.left, edges.top, edges.right, edges.bottom};
        for (int k = 0; k < 4; ++k) {
            label(labels_[1 + k], kEdgeNames[k]);
            stardraft::kit::TextField& f = edge_[k];
            f.setText(std::to_wstring(vals[k]));
            f.setFont({draw::Face::Mono, 0}), f.setAlignRight(true);
            f.setTrailing(scale(2));  // (the native edit's right margin: the digits end where they did)
            f.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });  // ES_NUMBER
            f.setMaxLength(7);
            f.onChange = [this] { check(); };
            add(f, {0, 0, 0, 0});
        }
        label(labels_[5], L"Size");
        size_.setTone(LabelTone::Dim);
        add(size_, {0, 0, 0, 0});
        // Type: Normal, or the edges that are inverted (an inverted location holds air units only). It follows the
        // edges as they are typed; a type picked in it swaps the edges (their numbers stay) to give that type.
        label(labels_[6], L"Type");
        std::vector<stardraft::kit::DropdownItem> types;
        for (const wchar_t* t : kLocationTypes) types.push_back({tr(t), -1, -1});
        type_.setItems(types);
        type_.setValue(locationTypeOf(edges));
        type_.setEnabled(!anywhere);
        type_.onChange = [this](int t) { pickType(t); };
        add(type_, {0, 0, 0, 0});
        for (int k = 0; k < 6; ++k) {
            elev_[k].setLabel(tr(kElevationNames[k]));
            elev_[k].setOn(!(excluded & (1 << k)));
            elev_[k].setPlainLabel(true);  // (a native check box's label is the text color)
            add(elev_[k], {0, 0, 0, 0});
        }
        error_.setTone(LabelTone::Bad);
        add(error_, {0, 0, 0, 0});
        check();
    }

    // The dialog this view is in (OK turns off while the edges are wrong).
    void setDialog(stardraft::kit::Dialog* d) {
        dlg_ = d;
        check();
    }

    // Whether the edges are right (OK may close the dialog).
    bool valid() const { return valid_; }
    // What the fields hold (a call after OK).
    Editor::LocationEdit values() const {
        Editor::LocationEdit e;
        e.name = narrow(name_.text());
        e.rect = rect_, e.asTyped = true;
        for (int k = 0; k < 6; ++k)
            if (!elev_[k].on()) e.excluded |= uint16_t(1 << k);
        return e;
    }

    // For the self-tests: an edge's text (as typed), an elevation's check box, the type line, whether OK is on, and a
    // press of OK (it does nothing while the edges are wrong) or Cancel.
    void setEdge(int k, int v) {
        edge_[k].setText(std::to_wstring(v));
        check();
    }
    stardraft::kit::CheckBox& elevationBox(int k) { return elev_[k]; }
    stardraft::kit::Dropdown& typeList() { return type_; }
    std::wstring edgesText() const { return edge_[0].text() + L"," + edge_[1].text() + L"," + edge_[2].text() + L"," + edge_[3].text(); }
    std::wstring typeText() const {
        const int t = type_.value();
        return t >= 0 && t < int(type_.items().size()) ? type_.items()[size_t(t)].label : L"";
    }
    bool okOn() const { return valid_; }
    void press(int result) {
        if (dlg_ && (result != kLocPropsOk || valid_)) dlg_->close(result);
    }

    // Report feedback: what is at `pt`: a field's label, the type line, a check box's text (as the native controls gave
    // their text).
    std::wstring feedbackName(draw::Point pt) override {
        auto in = [&](const draw::View& v) {
            const draw::Rect f = v.frame();
            return v.visible() && pt.x >= f.left && pt.x < f.right && pt.y >= f.top && pt.y < f.bottom;
        };
        if (in(type_)) return typeText();
        if (in(labels_[0]) || in(name_)) return labels_[0].text();
        for (int k = 0; k < 4; ++k)
            if (in(labels_[1 + k]) || in(edge_[k])) return labels_[1 + k].text();
        if (in(labels_[5]) || in(size_)) return labels_[5].text();
        if (in(labels_[6])) return labels_[6].text();
        for (const auto& b : elev_)
            if (in(b)) return b.label();
        if (in(error_)) return error_.text();
        using namespace locPropsLayout;
        if (pt.y >= scale(kHeadingY) && pt.y < scale(kHeadingY + 24)) return tr(L"Elevations in the location");
        return {};
    }

    void paint(draw::Canvas& c) override {
        using namespace locPropsLayout;
        if (focusName_ && !focused_ && host()) focused_ = true, name_.focus(), name_.model().selectAll(), name_.invalidate();  // the name has the keyboard, selected
        c.fill({0, 0, width(), height()}, kBg);
        // The elevations' heading: the text, and a hairline under it.
        c.text({0, scale(kHeadingY), scale(kContentW), scale(kHeadingY + 24)}, tr(L"Elevations in the location"), kText, draw::kLeft | draw::kMiddle);
        c.fill({0, scale(kHeadingY + 24), scale(kContentW), scale(kHeadingY + 24) + 1}, kBorder);
    }

    void resized() override {
        using namespace locPropsLayout;
        auto put = [](draw::View& v, int x, int y, int w, int h) { v.setFrame({scale(x), scale(y), scale(x + w), scale(y + h)}); };
        put(labels_[0], 0, kPad, kLabelW - 8, kRowH);
        put(name_, kLabelW, kPad, kContentW - kLabelW, kRowH);
        for (int k = 0; k < 4; ++k) {
            const int x = (k % 2) * kColW, y = kEdgesY + (k / 2) * kPitch;
            put(labels_[1 + k], x, y, kLabelW - 8, kRowH);
            put(edge_[k], x + kLabelW, y, kBoxW, kRowH);
        }
        put(labels_[5], 0, kSizeY, kLabelW - 8, kRowH);
        put(size_, kLabelW, kSizeY, kContentW - kLabelW, kRowH);
        put(labels_[6], 0, kTypeY, kLabelW - 8, kRowH);
        put(type_, kLabelW, kTypeY, kContentW - kLabelW, kRowH);
        for (int k = 0; k < 6; ++k) put(elev_[k], (k % 3) * kElevStepX, kElevY + (k / 3) * kPitch, kElevW, kRowH);
        put(error_, 0, kButtonsY + (kButtonH - kRowH) / 2, kErrorW, kRowH);
    }

private:
    // The size and error lines from the edges; OK is off while they are wrong.
    void check() {
        std::wstring t[4];
        for (int k = 0; k < 4; ++k) t[k] = edge_[k].text();
        const LocPropsCheck c = locPropsCheck(t);
        valid_ = c.ok, rect_ = c.rect;
        size_.setText(c.size);
        if (c.type >= 0) type_.setValue(c.type);  // (the list follows the edges; setValue does not call onChange)
        error_.setText(tr(c.error));
        if (dlg_) dlg_->setEnabledFor(kLocPropsOk, valid_);
    }

    // A type picked in the list: the edges swap where they must to give it (left and right, top and bottom); their
    // numbers stay. Edges that are the same cannot be inverted: the list shows the type they give.
    void pickType(int t) {
        const LocPropsCheck c = [&] {
            std::wstring e[4];
            for (int k = 0; k < 4; ++k) e[k] = edge_[k].text();
            return locPropsCheck(e);
        }();
        if (c.type < 0) return check();
        auto swapEdges = [this](int a, int b) {
            const std::wstring x = edge_[a].text();
            edge_[a].setText(edge_[b].text()), edge_[b].setText(x);
        };
        if (((t & 1) != 0) != (c.rect.left > c.rect.right) && c.rect.left != c.rect.right) swapEdges(0, 2);
        if (((t & 2) != 0) != (c.rect.top > c.rect.bottom) && c.rect.top != c.rect.bottom) swapEdges(1, 3);
        check();
    }

    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label labels_[7], size_, error_;
    stardraft::kit::TextField name_, edge_[4];
    stardraft::kit::Dropdown type_;
    stardraft::kit::CheckBox elev_[6];
    Rect rect_;
    bool valid_ = false, focusName_ = true, focused_ = false;
};

// Runs Location properties of location `loc` from `from` (the program's anchor view). OK puts the edit into the
// location (one undo step; it is off while the edges are wrong); Cancel and the window's close box change nothing.
// `focusName`: the name has the keyboard, selected (not in a background run). Returns the button that closed it.
// `shown` is the content while the dialog is open, so the host can find its window.
// The dialog around `view`: its buttons, the OK rule and the insets. Returns the window's size (the snapshots show it
// without a window).
inline draw::Point locPropsDialogSetup(stardraft::kit::Dialog& dlg, LocationPropsView& view) {
    using namespace locPropsLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"OK"), kLocPropsOk, true, false, false, 0});
    buttons.push_back({tr(L"Cancel"), kLocPropsCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setAllow([&view](int r) { return r != kLocPropsOk || view.valid(); });
    view.setDialog(&dlg);
    // The content from the top edge to the bottom edge (the error line is beside the buttons); the buttons kPad from the
    // right and bottom edges, 8 px apart.
    dlg.setInsets({kPad, 0, kPad, kPad, -(kButtonH + kPad), 8});
    return draw::Point{scale(kContentW + 2 * kPad), scale(kWindowH)};
}

inline LocationPropsView* gLocPropsShown = nullptr;  // the dialog while it is open (a snapshot's state line says its type line)
inline int runLocationPropsDialog(draw::View& from, Editor& ed, int loc, bool focusName, LocationPropsView*& shown) {
    using namespace locPropsLayout;
    LocationPropsView view(widen(ed.locationName(loc)), ed.locationEdges(loc), ed.locationElevation(loc), loc == kAnywhereLocation, focusName);
    stardraft::kit::Dialog dlg;
    const draw::Point size = locPropsDialogSetup(dlg, view);
    shown = gLocPropsShown = &view;
    const int r = dlg.run(from, tr(L"Location properties"), size);
    shown = gLocPropsShown = nullptr;
    if (r == kLocPropsOk && view.valid()) locPropsApply(ed, loc, view.values());
    return r;
}
