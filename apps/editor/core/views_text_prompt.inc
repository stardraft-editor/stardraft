// The text prompt (Rename location, the grid, a sound's name, a memory address; portable, core.inc): one text field
// with OK and Cancel, in a kit Dialog (Host::runModal). The text is selected and has the keyboard when it opens; Enter
// is OK and Escape is Cancel. The host (main.cpp: prompt on Windows) sets the owner window.

// The dialog's results (what its buttons return).
constexpr int kPromptOk = 1, kPromptCancel = 2;

// The layout is the old native dialog's (its template, 220 x 56 dialog units at Segoe UI 9 pt: 7 x 15 base units, so a
// dialog unit is 1.75 px across and 1.875 px down at 100 %), in pixels at 100 % from the window's client area. Its
// buttons were 30 px high (16 dialog units), so they are the view's own, not the Dialog's (28 px).
namespace promptLayout {
constexpr int kW = 385, kH = 105;                            // 220 x 56
constexpr int kFieldX = 14, kFieldY = 15, kFieldW = 357, kFieldH = 26;  // 8, 8, 204 x 14
constexpr int kOkX = 186, kCancelX = 284, kButtonY = 60, kButtonW = 88, kButtonH = 30;  // 106 and 162, 32; 50 x 16
constexpr size_t kMaxText = 511;                             // (the old dialog's 512-character buffer)
}  // namespace promptLayout

class TextPromptView final : public draw::View {
public:
    TextPromptView(const std::wstring& text, bool focusText)
        : ok_(tr(L"OK"), stardraft::kit::Tone::Primary, [this] { close(kPromptOk); }),
          cancel_(tr(L"Cancel"), stardraft::kit::Tone::Normal, [this] { close(kPromptCancel); }),
          focusText_(focusText) {
        field_.setText(text.substr(0, promptLayout::kMaxText));
        field_.setMaxLength(promptLayout::kMaxText);
        field_.onEnter = [this] { close(kPromptOk); };
        field_.onEscape = [this] { close(kPromptCancel); };
        add(field_, {0, 0, 0, 0});
        add(ok_, {0, 0, 0, 0});
        add(cancel_, {0, 0, 0, 0});
    }

    // The dialog this view is in (the buttons close it).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }
    // The text in the field (a call after OK).
    const std::wstring& text() const { return field_.text(); }
    // The text field (the self-tests type into it).
    stardraft::kit::TextField& field() { return field_; }
    // Closes the dialog as a button does (kPromptOk or kPromptCancel).
    void close(int result) {
        if (dlg_) dlg_->close(result);
    }

    // Enter is OK and Escape is Cancel where the focused view does not take the key (a focused button takes Enter).
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Enter) return close(kPromptOk), true;
        if (e.key == draw::Key::Escape) return close(kPromptCancel), true;
        return false;
    }

    void paint(draw::Canvas& c) override {
        if (focusText_ && !focused_ && host()) focused_ = true, field_.focus(), field_.model().selectAll(), field_.invalidate();  // the text has the keyboard, selected
        c.fill({0, 0, width(), height()}, kBg);
    }

    void resized() override {
        using namespace promptLayout;
        auto put = [](draw::View& v, int x, int y, int w, int h) { v.setFrame({scale(x), scale(y), scale(x + w), scale(y + h)}); };
        put(field_, kFieldX, kFieldY, kFieldW, kFieldH);
        put(ok_, kOkX, kButtonY, kButtonW, kButtonH);
        put(cancel_, kCancelX, kButtonY, kButtonW, kButtonH);
    }

private:
    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::TextField field_;
    stardraft::kit::Button ok_, cancel_;
    bool focusText_ = true, focused_ = false;
};

// Runs the prompt from `from` (the program's anchor view) with `title`, showing `text`. OK puts the field's text into
// `text` and returns true; Cancel, Escape and the window's close box leave it and return false. `focusText`: the text
// has the keyboard, selected (not in a background run). `shown` is the content while the dialog is open, so the host
// can find its window.
inline bool runTextPrompt(draw::View& from, const std::wstring& title, std::wstring& text, bool focusText, TextPromptView*& shown) {
    using namespace promptLayout;
    TextPromptView view(text, focusText);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    view.setDialog(&dlg);
    dlg.setInsets({0, 0, 0, 0, 0, 0});  // the view places the field and the buttons itself
    dlg.setCenterOnWorkArea(true);      // DS_CENTER
    shown = &view;
    const int r = dlg.run(from, title, draw::Point{scale(kW), scale(kH)});
    shown = nullptr;
    if (r != kPromptOk) return false;
    text = view.text();
    return true;
}
