// --- Search boxes: kit text fields ------------------------------------------------------------------------------------
// The Locations filter (app.filter) and the palette search are kit text fields (apps/kit/text_field.h) in view windows
// (ui::kViewClass). The field fills the window; at its right an x clears it while there is text. The text's changes
// call onChange (as EN_CHANGE did), and so does the x: a native clear sent no EN_CHANGE, so the list did not refresh
// (a deliberate change). setText does not call onChange (as SetWindowText did not).
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
        add(field_, {0, 0, 0, 0});
    }
    std::function<void()> onChange;  // the user changed the text

    stardraft::kit::TextField& field() { return field_; }
    std::wstring text() const { return field_.text(); }
    void setText(std::wstring t) { field_.setText(std::move(t)); }  // no onChange
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
                field_.setText(L"");
                field_.focus();
                invalidate();
                if (onChange) onChange();
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

    stardraft::kit::TextField field_;
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

// Whether a search box's field has the keyboard (the message loop leaves its keys to it, as to an edit control).
bool searchBoxFocused() { return gFilterBox.field().hasFocus() || gPalSearchBox.field().hasFocus(); }
// A kit dropdown's list is open: it takes the typed letters, as an open combo box's list did.
bool dropdownListOpen() { return gSectionBox.dropdown().listOpen(); }
