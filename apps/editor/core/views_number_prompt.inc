// The number form (portable, core.inc): one or more numbers, each a row with its name, a field, its unit and its range,
// with OK and Cancel, in a kit Dialog (Host::runModal), as the text prompt (views_text_prompt.inc). docs/dev/PATTERNS.md,
// "Menus and pickers": a typed number is a field with its range and its unit, never a text prompt. OK refuses a value
// out of its range: its range turns red and its field gets the keyboard. Enter is OK and Escape is Cancel.

// (NumberAsk, one number of the form: shell.h)

class NumberPromptView final : public draw::View {
public:
    static constexpr int kRowH = 34, kTop = 15, kLabelW = 120, kFieldW = 96, kPad = 14, kButtonW = 88, kButtonH = 30;
    explicit NumberPromptView(std::vector<NumberAsk>& asks)
        : asks_(asks), ok_(tr(L"OK"), stardraft::kit::Tone::Primary, [this] { commit(); }),
          cancel_(tr(L"Cancel"), stardraft::kit::Tone::Normal, [this] { close(kPromptCancel); }), bad_(asks.size(), false) {
        for (size_t i = 0; i < asks_.size(); ++i) {
            fields_.push_back(std::make_unique<stardraft::kit::TextField>());
            stardraft::kit::TextField& f = *fields_[i];
            const NumberAsk& a = asks_[i];
            f.setText(text(a.value, a.decimals));
            f.setMaxLength(12);
            f.setFilter([neg = a.min < 0, dec = a.decimals > 0](wchar_t c) { return (c >= L'0' && c <= L'9') || (neg && c == L'-') || (dec && (c == L'.' || c == L',')); });
            f.onEnter = [this] { commit(); };
            f.onEscape = [this] { close(kPromptCancel); };
            f.onChange = [this, i] {
                if (bad_[i]) bad_[i] = false, invalidate();
            };
            add(f, {0, 0, 0, 0});
        }
        add(ok_, {0, 0, 0, 0});
        add(cancel_, {0, 0, 0, 0});
    }
    // The dialog's size at 100 %.
    static draw::Point sizeFor(size_t n) { return {385, kTop + int(n) * kRowH + 12 + kButtonH + 15}; }
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }
    void close(int result) {
        if (dlg_) dlg_->close(result);
    }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Enter) return commit(), true;
        if (e.key == draw::Key::Escape) return close(kPromptCancel), true;
        return false;
    }
    void paint(draw::Canvas& c) override {
        if (!focused_ && host() && !fields_.empty()) focused_ = true, fields_[0]->focus(), fields_[0]->model().selectAll(), fields_[0]->invalidate();
        c.fill({0, 0, width(), height()}, kBg);
        for (size_t i = 0; i < asks_.size(); ++i) {
            const int y = scale(kTop + int(i) * kRowH), h = scale(28);
            const NumberAsk& a = asks_[i];
            c.text({scale(kPad), y, scale(kPad + kLabelW), y + h}, a.label, kText, draw::kMiddle | draw::kEllipsis);
            const int ux = scale(kPad + kLabelW + kFieldW + 8);
            if (!a.unit.empty()) c.text({ux, y, ux + scale(70), y + h}, a.unit, kDim, draw::kMiddle);
            const std::wstring range = text(a.min, a.decimals) + tr(L" to ") + text(a.max, a.decimals);
            c.text({ux + scale(a.unit.empty() ? 0 : 70), y, width() - scale(kPad), y + h}, range, bad_[i] ? kBad : kFaint, draw::kMiddle | draw::kRight);
        }
    }
    void resized() override {
        for (size_t i = 0; i < fields_.size(); ++i) {
            const int y = kTop + int(i) * kRowH + 1;
            fields_[i]->setFrame({scale(kPad + kLabelW), scale(y), scale(kPad + kLabelW + kFieldW), scale(y + 26)});
        }
        const int by = kTop + int(fields_.size()) * kRowH + 12;
        ok_.setFrame({width() - scale(kPad + 2 * kButtonW + 10), scale(by), width() - scale(kPad + kButtonW + 10), scale(by + kButtonH)});
        cancel_.setFrame({width() - scale(kPad + kButtonW), scale(by), width() - scale(kPad), scale(by + kButtonH)});
    }

private:
    static std::wstring text(double v, int decimals) {
        if (decimals <= 0) return std::to_wstring(std::llround(v));
        wchar_t b[48];
        std::swprintf(b, 48, L"%.*f", decimals, v);
        std::wstring s = b;
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
        return s;
    }
    // OK: each value in its range goes into `asks`; else its range turns red and its field gets the keyboard.
    void commit() {
        std::vector<double> got(asks_.size());
        for (size_t i = 0; i < asks_.size(); ++i) {
            std::wstring t = fields_[i]->text();
            std::replace(t.begin(), t.end(), L',', L'.');
            wchar_t* end = nullptr;
            const double v = std::wcstod(t.c_str(), &end);
            const double step = std::pow(10.0, asks_[i].decimals);
            const double r = std::round(v * step) / step;
            if (t.empty() || end == t.c_str() || !(r >= asks_[i].min && r <= asks_[i].max)) {
                bad_[i] = true, invalidate();
                fields_[i]->focus(), fields_[i]->model().selectAll(), fields_[i]->invalidate();
                return;
            }
            got[i] = r;
        }
        for (size_t i = 0; i < asks_.size(); ++i) asks_[i].value = got[i];
        close(kPromptOk);
    }
    std::vector<NumberAsk>& asks_;
    std::vector<std::unique_ptr<stardraft::kit::TextField>> fields_;  // (a field each: kit views do not move)
    stardraft::kit::Button ok_, cancel_;
    std::vector<bool> bad_;
    stardraft::kit::Dialog* dlg_ = nullptr;
    bool focused_ = false;
};

// Runs the form from `from` with `title`. OK puts the values into `asks` and returns true; Cancel, Escape and the close
// box leave them and return false.
inline bool runNumberPrompt(draw::View& from, const std::wstring& title, std::vector<NumberAsk>& asks) {
    if (asks.empty()) return false;
    NumberPromptView view(asks);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    view.setDialog(&dlg);
    dlg.setInsets({0, 0, 0, 0, 0, 0});
    dlg.setCenterOnWorkArea(true);
    const draw::Point s = NumberPromptView::sizeFor(asks.size());
    return dlg.run(from, title, draw::Point{scale(s.x), scale(s.y)}) == kPromptOk;
}
