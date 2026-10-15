// Classic triggers' number pop-out (the value of a parameter, as a number, with OK and Cancel; portable, core.inc): a
// label and a number field in a kit Dialog. OK gives the typed digits; Cancel and the window's close box leave the
// value as it was. The text pop-out (the game text editor) is still the native one (classic_triggers.inc). The dialog
// is a kit Dialog (Host::runModal); the host (classic_triggers.inc on Windows) sets the owner and applies the value.

// The dialog's results (what its buttons return).
constexpr int kValueOk = 1, kValueCancel = 2;

// The layout is the native pop-out's (its template, 240 x 92 dialog units at Segoe UI 9 pt: 1.5 px across and 2 px down
// at 100 %), in pixels at 100 %, from the content's top left corner. The Dialog's insets put the content 12 px from the
// left edge and 8 px from the top; the buttons sit 15 px from the right edge and 16 px from the bottom, 9 px apart.
namespace valueLayout {
constexpr int kDialogW = 360, kDialogH = 184;  // 240 x 92 dialog units
constexpr int kLabelX = 0, kLabelY = 10, kLabelW = 165;  // the label's box, from the content (window 12, 18)
constexpr int kFieldX = 168, kFieldY = 10, kFieldW = 150, kFieldH = 24;  // window x 180, y 18; the kit field is 24 px high
constexpr int kButtonW = 75;
}  // namespace valueLayout

// The pop-out's content: the label and the number field. The buttons are the Dialog's.
class CtValueView final : public draw::View {
public:
    CtValueView(const std::wstring& label, const std::wstring& number) {
        label_.setText(label);
        label_.setTone(stardraft::kit::LabelTone::Dim);  // (the native label's text was the dim color)
        field_.setText(number);
        field_.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });  // ES_NUMBER
        add(label_, {0, 0, 0, 0});
        add(field_, {0, 0, 0, 0});
    }

    // The dialog this view is in (a Cancel from the self-test's picture closes it).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }

    // The digits in the field (a call after OK).
    std::wstring number() const { return field_.text(); }
    // Closes the dialog as Cancel does (the self-test's picture closes it).
    void cancel() {
        if (dlg_) dlg_->close(kValueCancel);
    }
    // The number field (the self-test types into it).
    stardraft::kit::TextField& numberField() { return field_; }

    void paint(draw::Canvas& c) override {
        if (!focused_ && host()) focused_ = true, field_.focus();  // the number has the keyboard, selected, when the dialog opens
        if (focused_ && !selected_) selected_ = true, field_.model().selectAll(), field_.invalidate();
        c.fill({0, 0, width(), height()}, kBg);
    }

    void resized() override {
        using namespace valueLayout;
        label_.setFrame({scale(kLabelX), scale(kLabelY), scale(kLabelX + kLabelW), scale(kLabelY + kFieldH)});
        field_.setFrame({scale(kFieldX), scale(kFieldY), scale(kFieldX + kFieldW), scale(kFieldY + kFieldH)});
    }

private:
    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label label_;
    stardraft::kit::TextField field_;
    bool focused_ = false, selected_ = false;
};

// Runs the pop-out from `from` (the program's anchor view). Returns the button that closed it: kValueOk (`number` holds
// the digits) or kValueCancel. `shown` is the content while the dialog is open, so the host can find its window.
inline int runValueDialog(draw::View& from, const std::wstring& title, const std::wstring& label, const std::wstring& current, std::wstring& number,
                          CtValueView*& shown) {
    CtValueView view(label, current);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    view.setDialog(&dlg);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Set"), kValueOk, true, false, false, valueLayout::kButtonW});
    buttons.push_back({tr(L"Cancel"), kValueCancel, false, true, false, valueLayout::kButtonW});
    dlg.setButtons(std::move(buttons));
    dlg.setInsets({12, 8, 15, 16, 0, 9});  // the native template's margins; the buttons 9 px apart
    dlg.setCenterOnWorkArea(true);         // DS_CENTER
    shown = &view;
    const int r = dlg.run(from, title, draw::Point{scale(valueLayout::kDialogW), scale(valueLayout::kDialogH)});
    shown = nullptr;
    if (r == kValueOk) number = view.number();
    return r;
}
