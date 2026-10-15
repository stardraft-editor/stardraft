// A line of text with OK and Cancel (portable, core.inc): a kit text field in a kit Dialog, for the systems that have no
// native prompt (shellPromptText on SDL; the Windows program keeps its native prompt, main.cpp's prompt). Enter gives OK,
// Escape and the window's close box give Cancel.

// The dialog's results (what its buttons return).
constexpr int kPromptOk = 1, kPromptCancel = 2;

// The layout is the Windows prompt's (its template, 220 x 56 dialog units at Segoe UI 9 pt: 1.5 px across and 2 px down
// at 100 %), in pixels at 100 %. The field is 306 px wide, 16 px from the top (the kit field is 24 px high, centered
// on the native 28 px box); OK and Cancel are 75 x 32, 9 px apart, 12 px from the right and 16 px from the bottom.
namespace promptLayout {
constexpr int kDialogW = 330, kDialogH = 112;
constexpr int kFieldH = 24, kBoxH = 28;  // the kit field in the native edit's box
constexpr int kButtonW = 75;
}  // namespace promptLayout

// The dialog's content: the text field. The buttons are the Dialog's.
class PromptView final : public draw::View {
public:
    explicit PromptView(const std::wstring& text) {
        field_.setText(text);
        add(field_, {0, 0, 0, 0});
    }
    std::wstring text() const { return field_.text(); }

    void paint(draw::Canvas& c) override {
        if (!focused_ && host()) focused_ = true, field_.focus(), field_.model().selectAll(), field_.invalidate();  // the text has the keyboard, selected
        c.fill({0, 0, width(), height()}, kBg);
    }
    void resized() override {
        using namespace promptLayout;
        const int top = (scale(kBoxH) - scale(kFieldH)) / 2;
        field_.setFrame({0, top, width(), top + scale(kFieldH)});
    }

private:
    stardraft::kit::TextField field_;
    bool focused_ = false;
};

// Runs the dialog from `from` (the program's root view). True after OK: `text` is what was typed.
inline bool runPromptDialog(draw::View& from, const std::wstring& title, std::wstring& text) {
    using namespace promptLayout;
    PromptView view(text);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"OK"), kPromptOk, true, false, false, kButtonW});
    buttons.push_back({tr(L"Cancel"), kPromptCancel, false, true, false, kButtonW});
    dlg.setButtons(std::move(buttons));
    dlg.setInsets({12, 16, 12, 16, 64 - 16 - kBoxH, 9});  // the buttons' top is 64 px down: the field's box, then the gap
    dlg.setCenterOnWorkArea(true);                        // DS_CENTER
    const int r = dlg.run(from, title, draw::Point{scale(kDialogW), scale(kDialogH)});
    if (r != kPromptOk) return false;
    text = view.text();
    return true;
}
