// --- Search boxes: kit text fields ------------------------------------------------------------------------------------
// The Locations filter (app.filter) and the palette search are kit text fields (apps/kit/text_field.h) in view windows
// (ui::kViewClass). The field fills the window; at its right an x clears it while there is text. Every change of the
// text calls onChange, as EN_CHANGE did: the typing, the paste, the x and setText (a native SetWindowText sent
// EN_CHANGE too), so the list refreshes at once.
// Included by main.cpp before rebuildList (after the tokens and the App).

// The kit's widgets draw with the editor's tokens and the window's scale (apps/kit/theme.h); the editor calls this when
// its colors, its theme or its DPI change.
void syncKitTheme() {
    stardraft::kit::Theme& t = stardraft::kit::theme();
    t.bg = kBg, t.panel = kPanel, t.ctl = kCtl, t.ctlHot = kCtlHot, t.ctlDown = kCtlDown;
    t.readOnly = kReadOnly, t.readOnlyLine = kReadOnlyLine;
    t.text = kText, t.dim = kDim, t.faint = kFaint;
    t.border = kBorder, t.borderHot = kBorderHot, t.borderStrong = kBorderStrong;
    t.accent = kAccent, t.accentBright = kAccentBright, t.accentLine = kAccentLine, t.accentFill = kAccentFill;
    t.sel = kSel, t.selHot = kSelHot, t.onText = kOnText;
    t.chip = kChip, t.chipLine = kChipLine, t.chipDim = kChipDim;
    t.thumb = kThumb, t.thumbHot = kThumbHot, t.rowLine = kRowLine, t.gripDot = kGripDot;
    t.rowHot = kRowHot, t.hot = kHot;
    t.good = kGood, t.goodFill = kGoodFill, t.goodLine = kGoodLine, t.warn = kWarn, t.bad = kBad;
    t.light = gLightTheme;
    t.dpi = app.dpi ? int(app.dpi) : 96;
}

class SearchBox final : public draw::View {
public:
    SearchBox() {
        field_.onChange = [this] {
            invalidate();
            if (onChange) onChange();
        };
        field_.onHoverEnd = [this] {  // the mouse left the field (over the x too): the x is no longer hot
            if (hot_) hot_ = false, invalidate();
        };
        add(field_, {0, 0, 0, 0});
    }
    std::function<void()> onChange;  // the text changed

    stardraft::kit::TextField& field() { return field_; }
    std::wstring text() const { return field_.text(); }
    void setText(std::wstring t) {  // onChange runs, as for an edit box's SetWindowText (EN_CHANGE)
        field_.setText(std::move(t));
        invalidate();
        if (onChange) onChange();
    }
    void setPlaceholder(std::wstring t) { field_.setPlaceholder(std::move(t)); }

    void resized() override {
        field_.setTrailing(std::max(1, height()));  // the x's square at the right edge
        field_.setFrame({0, 0, width(), height()});
    }
    void paintOver(draw::Canvas& c) override {
        if (field_.text().empty()) return;
        const draw::Rect b = clearBox();
        c.text(b, L"×", hot_ ? kText : kDim, draw::kCenter | draw::kMiddle);
    }
    bool mouse(const draw::MouseEvent& e) override {
        const bool on = !field_.text().empty() && clearBox().contains(e.pt);
        switch (e.kind) {
        case draw::MouseKind::Move:
        case draw::MouseKind::Leave:
            if (on != hot_) hot_ = on, invalidate();
            return false;
        case draw::MouseKind::Down:
            if (on && e.button == 0) {
                setText(L"");
                field_.focus();
                return true;
            }
            return false;
        default: return false;
        }
    }
    draw::Cursor cursor(draw::Point pt) override {
        return !field_.text().empty() && clearBox().contains(pt) ? draw::Cursor::Arrow : draw::Cursor::IBeam;
    }

private:
    draw::Rect clearBox() const {
        const int s = std::max(1, height());
        return {width() - s, 0, width(), height()};
    }

    stardraft::kit::TextField field_;  // the field covers the box, so it gets the mouse events (the x's hover too)
    bool hot_ = false;  // the pointer is over the x
};

SearchBox gFilterBox;     // the Locations filter
SearchBox gPalSearchBox;  // the palette search (Units, Doodads, Sprites)

// A kit dropdown in a view window: the palette's section box. The dropdown fills the window; its choice goes to onChange
// (the mouse wheel over the closed box steps it too, as the combo box's wheel did).
class DropdownBox final : public draw::View {
public:
    DropdownBox() { add(dropdown_, {0, 0, 0, 0}); }
    stardraft::kit::Dropdown& dropdown() { return dropdown_; }
    void resized() override { dropdown_.setFrame({0, 0, width(), height()}); }

private:
    stardraft::kit::Dropdown dropdown_;
};

DropdownBox gSectionBox;  // the palette's section box (Units, Doodads, Sprites)

// The message being dispatched is a mouse wheel one (set by the message loop around DispatchMessageW).
bool gWheelDispatch = false;

// Whether a search box's field has the keyboard (the message loop leaves its keys to it, as to an edit control).
bool searchBoxFocused() { return gFilterBox.field().hasFocus() || gPalSearchBox.field().hasFocus(); }
// A kit dropdown's list is open: it takes the typed letters, as an open combo box's list did.
bool dropdownListOpen() { return gSectionBox.dropdown().listOpen(); }
