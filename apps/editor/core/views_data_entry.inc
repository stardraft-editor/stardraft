// The Data form's number entry (portable, core.inc): the box that types a number over a field's value, a kit text field
// in a view window (the Win32 program's form holds it as a child window; the SDL program's form places it the same way).
// Enter commits, Escape cancels, Tab goes to the next field (Shift+Tab to the one before), and the focus leaving commits.
// The text's rules are here: decimal, or hex with 0x, and a value above the field's maximum is the maximum. The program
// stores the value and shows the "Not a number" status.

// What the typed text is: nothing (the field keeps its value), a number, or text that is not a number.
enum class DataEntryRead { Empty, Number, NotNumber };

// Reads the typed text as a value of at most `maxValue`. Decimal, or hex with 0x (wcstoull's base 0). With a `scale`
// above 1 (fieldShownScale: hit points), the text is in the shown unit, with decimals, and the value is it times that.
DataEntryRead dataReadEntry(const std::wstring& text, uint32_t maxValue, uint32_t& value, uint32_t scale = 1) {
    if (text.empty()) return DataEntryRead::Empty;
    if (scale > 1) {  // a value in the shown unit (hit points), decimals too: as stored
        wchar_t* end = nullptr;
        const double d = std::wcstod(text.c_str(), &end);
        if (!end || *end != 0 || d < 0) return DataEntryRead::NotNumber;
        value = stardraft::datRawFromShown(d, scale, maxValue);
        return DataEntryRead::Number;
    }
    wchar_t* end = nullptr;
    const unsigned long long v = std::wcstoull(text.c_str(), &end, 0);
    if (!end || *end != 0) return DataEntryRead::NotNumber;
    value = uint32_t(std::min<unsigned long long>(v, maxValue));
    return DataEntryRead::Number;
}

// The entry's view: the text field fills it, and the view paints the control's fill behind the text (the form paints the
// box's frame round it, as it did round the native edit box). The owner sets the callbacks.
class DataNumberEntry final : public draw::View {
public:
    DataNumberEntry() {
        field_.setPlain(true);
        field_.setAlignRight(true);
        field_.setFont({draw::Face::Mono, 0});
        field_.onEnter = [this] {
            if (onCommit) onCommit();
        };
        field_.onEscape = [this] {
            if (onCancel) onCancel();
        };
        field_.onFocusLost = [this] {
            if (onFocusLost) onFocusLost();
        };
        add(field_, {0, 0, 0, 0});
    }
    std::function<void()> onCommit;          // Enter
    std::function<void()> onCancel;          // Escape
    std::function<void()> onFocusLost;       // the focus went elsewhere
    std::function<void(bool back)> onTab;    // Tab (back: Shift+Tab)

    stardraft::kit::TextField& field() { return field_; }
    std::wstring text() const { return field_.text(); }
    void setText(std::wstring t) { field_.setText(std::move(t)); }
    void select() { field_.model().selectAll(); }
    void focus() { field_.focus(); }
    // A key press as the focused view gets it: the field first, then this view (Tab is this view's).
    void press(draw::Key k, unsigned mods = 0) {
        const draw::KeyEvent e{k, true, false, mods};
        if (!field_.key(e)) key(e);
    }

    void resized() override { field_.setFrame({0, 0, width(), height()}); }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, stardraft::kit::theme().ctl); }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Tab && !(e.mods & (draw::kCtrl | draw::kAlt))) {
            if (onTab) onTab((e.mods & draw::kShift) != 0);
            return true;
        }
        return false;
    }

private:
    stardraft::kit::TextField field_;
};
