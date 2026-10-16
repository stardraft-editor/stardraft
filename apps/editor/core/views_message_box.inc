// The message box (portable, core.inc): a question, an error or a note in the theme, with an icon at the left, the
// text wrapped at 420 px beside it and the buttons at the bottom right. The box is a view (MessageView) in a kit Dialog
// (no Dialog buttons and no insets: the view paints its own buttons, so the button with the focus is the Primary one).
// The Windows host is message_box.inc (themedMessageBox, a drop-in for MessageBoxW: the flags and the return values).
//
// The keys: Enter and Space press the button with the focus (first: the default button), Escape presses the cancel
// button, Tab and the arrow keys move the focus, and the letter after "&" in a button name presses that button (with
// Alt, the letters are underlined while Alt is down). The window's close box is the cancel button.

namespace msgbox {

enum class Icon { None, Error, Warning, Question, Info };

struct Button {
    std::wstring name;  // with "&" before the access key
    int id = 0;         // what runMessageBox returns when this button closes the box
    draw::Rect rc{};
};
struct Box {
    std::wstring text;
    Icon icon = Icon::None;
    std::vector<Button> buttons;
    int focus = 0;   // the button with the focus (Primary; Enter presses it)
    int hot = -1;    // under the mouse
    int down = -1;   // pressed with the mouse
    int cancel = 0;  // what Escape and the close box press
    int result = 0;
    bool done = false;
    draw::Rect textRc{}, iconRc{};
};

// The icon for the box's icon kind: an icon of the set (or a Segoe MDL2 glyph, for the information icon) and its color.
inline bool iconFor(Icon i, kitpaint::Ico& ico, draw::Color& c) {
    switch (i) {
    case Icon::Error: ico = UiIcon::Error, c = kBad; return true;
    case Icon::Warning: ico = UiIcon::Warning, c = kWarn; return true;
    case Icon::Question: ico = UiIcon::Help, c = kAccent; return true;
    case Icon::Info: ico = L"\xE946", c = kAccent; return true;  // Info
    default: return false;
    }
}

// The access key of a button name ("Do&n't save": n), lower case; 0: none.
inline wchar_t accessKey(const std::wstring& name) {
    for (size_t i = 0; i + 1 < name.size(); ++i)
        if (name[i] == L'&') {
            if (name[i + 1] == L'&') {
                ++i;
                continue;
            }
            return wchar_t(towlower(name[i + 1]));
        }
    return 0;
}

class MessageView final : public draw::View {
public:
    Box b;
    std::function<void()> shown;  // once, soon after the window shows: datashots and self-tests

    // The Dialog that the view is in (a button closes it).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }

    // Places the text, the icon and the buttons for the Canvas's font; returns the client size.
    draw::Point layout(draw::Canvas& c) {
        laidOut_ = true;
        const int pad = scale(stardraft::kit::kDialogPad), wrap = scale(420), bh = scale(stardraft::kit::kButtonH), gap = scale(stardraft::kit::kButtonGap);
        const draw::Font of = c.setFont(c.font({}));
        const draw::Point t = c.measureBox(b.text, wrap, draw::kWrap);
        int bw = 0;
        std::vector<int> widths;
        for (const Button& bt : b.buttons) {
            widths.push_back(std::max(scale(stardraft::kit::kButtonMinW), c.measureBox(bt.name, 0, draw::kPrefixHidden).x + scale(28)));
            bw += widths.back() + (bw ? gap : 0);
        }
        c.setFont(of);

        kitpaint::Ico ico = L"";
        draw::Color ic = kText;
        const bool icon = iconFor(b.icon, ico, ic);
        const int iconW = icon ? scale(32) : 0, iconGap = icon ? scale(12) : 0;
        const int textW = std::min<int>(wrap, t.x), textH = t.y;
        const int contentH = std::max(textH, iconW);
        const int w = std::max(pad + iconW + iconGap + textW + pad, pad + bw + pad);
        // The text is beside the icon, centered on it when it is one or two lines.
        const int ty = pad + (contentH - textH) / 2;
        b.iconRc = {pad, pad, pad + iconW, pad + iconW};
        b.textRc = {pad + iconW + iconGap, ty, pad + iconW + iconGap + textW, ty + textH};
        const int by = pad + contentH + scale(24);
        int x = w - pad;
        for (size_t k = b.buttons.size(); k-- > 0;) {
            b.buttons[k].rc = {x - widths[k], by, x, by + bh};
            x -= widths[k] + gap;
        }
        return {w, by + bh + pad};
    }

    // The window is made (the Dialog gives the view its size then, before the window shows): the box takes the
    // keyboard, and `shown` runs from the window's first timer. (A window off the screen, in a background run, gets
    // no paint until it is captured.)
    void resized() override {
        if (started_ || !host() || width() <= 0) return;
        started_ = true;
        focus();
        if (shown) host()->timer(0, shown);
    }

    void paint(draw::Canvas& c) override {
        if (!laidOut_) layout(c);
        c.fill({0, 0, width(), height()}, kBg);
        const bool alt = host() && (host()->modifiers() & draw::kAlt) != 0;
        kitpaint::Ico ico = L"";
        draw::Color ic = kText;
        if (iconFor(b.icon, ico, ic)) {
            if (ico.icon >= 0) c.icon(b.iconRc, ico.icon, ic, scale(32));
            else kitpaint::drawGlyph(c, b.iconRc, ico, ic, 26);
        }
        c.text(b.textRc, b.text, kText, draw::kWrap);
        // The buttons: the kit's face, then the name with its access key underlined while Alt is down.
        for (size_t k = 0; k < b.buttons.size(); ++k) {
            const Button& bt = b.buttons[k];
            const kitpaint::Tone tone = int(k) == b.focus ? kitpaint::Tone::Primary : kitpaint::Tone::Normal;
            const kitpaint::ButtonState s = int(k) == b.down && b.down == b.hot ? kitpaint::ButtonState::Down : int(k) == b.hot ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal;
            kitpaint::drawButtonFace(c, bt.rc, L"", tone, s);
            c.text(bt.rc, bt.name, kitpaint::toneText(tone), draw::kCenter | draw::kMiddle | (alt ? draw::kPrefix : draw::kPrefixHidden));
        }
    }

    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move: {
            const int h = hitButton(e.pt);
            if (h != b.hot) b.hot = h, invalidate();
            return true;
        }
        case draw::MouseKind::Leave:
            if (b.hot != -1) b.hot = -1, invalidate();
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick:  // (a quick second click is this)
            if (e.button != 0) return false;
            b.down = hitButton(e.pt);
            if (b.down >= 0 && host()) host()->capture(*this, true), b.focus = b.down, invalidate();
            return true;
        case draw::MouseKind::Up: {
            if (e.button != 0) return false;
            const int d = b.down;
            b.down = -1;
            if (host()) host()->capture(*this, false);
            if (d >= 0 && hitButton(e.pt) == d) press(d);
            else invalidate();
            return true;
        }
        default: return false;
        }
    }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down || b.done) return false;
        if (e.mods & draw::kAlt) {  // Alt+letter: the access key (Windows sends it as a system key)
            const int k = int(e.key);
            if (!((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'))) return false;
            return pressAccess(wchar_t(towlower(wchar_t(k))));
        }
        const int n = int(b.buttons.size());
        switch (e.key) {
        case draw::Key::Enter:
        case draw::Key::Space: press(b.focus); return true;
        case draw::Key::Escape: press(b.cancel); return true;
        case draw::Key::Tab:
        case draw::Key::Left:
        case draw::Key::Right:
        case draw::Key::Up:
        case draw::Key::Down: {
            const bool back = e.key == draw::Key::Left || e.key == draw::Key::Up || (e.key == draw::Key::Tab && (e.mods & draw::kShift) != 0);
            b.focus = (b.focus + (back ? n - 1 : 1)) % n;
            invalidate();
            return true;
        }
        default: return false;  // (letters come as text)
        }
    }

    bool text(const draw::TextEvent& e) override {
        const std::wstring w = widen(e.utf8);
        if (w.size() == 1) pressAccess(wchar_t(towlower(w[0])));
        return true;
    }

    draw::Cursor cursor(draw::Point) override { return b.hot != -1 ? draw::Cursor::Hand : draw::Cursor::Arrow; }

    // Ends the box with button k; false when it is ended already or k is no button.
    bool choose(int k) {
        if (b.done || k < 0 || k >= int(b.buttons.size())) return false;
        b.result = b.buttons[size_t(k)].id, b.done = true;
        return true;
    }

private:
    int hitButton(draw::Point p) const {
        for (size_t k = 0; k < b.buttons.size(); ++k) {
            const draw::Rect& r = b.buttons[k].rc;
            if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return int(k);
        }
        return -1;
    }
    void press(int k) {
        if (choose(k) && dlg_) dlg_->close(k);
    }
    bool pressAccess(wchar_t c) {
        if (!c || b.done) return false;
        for (size_t k = 0; k < b.buttons.size(); ++k)
            if (accessKey(b.buttons[k].name) == c) {
                press(int(k));
                return true;
            }
        return false;
    }

    stardraft::kit::Dialog* dlg_ = nullptr;
    bool laidOut_ = false, started_ = false;
};

// The host's own message box (macOS: an alert sheet on the window, sdl_window.inc): it shows `box` from `from` and
// returns the index of the button (in box.buttons) that closed it, or -1 when it shows none (then the box is this
// one). Not for a box with a `shown` (datashots and self-tests drive this one).
inline std::function<int(draw::View& from, const std::wstring& caption, const Box& box)>& hostBox() {
    static std::function<int(draw::View&, const std::wstring&, const Box&)> fn;
    return fn;
}

// Shows the box from `from` (a view in the window that owns it) and returns when it closes: the id of the button that
// closed it (the window's close box: the cancel button's). `measure` sizes the window for the text and the buttons
// (nullptr: the host's shellMeasureCanvas, else an estimate from the UI font's letters; the view places its parts on
// its first paint then). `shown` runs
// once, soon after the box shows, with the box's view (datashots and self-tests find its window with it).
inline int runMessageBox(draw::View& from, const std::wstring& caption, Box box, draw::Canvas* measure,
                         std::function<void(MessageView&)> shown = {}) {
    if (box.buttons.empty()) box.buttons.push_back({L"OK", 0});
    box.focus = std::clamp(box.focus, 0, int(box.buttons.size()) - 1);
    box.cancel = std::clamp(box.cancel, 0, int(box.buttons.size()) - 1);
    if (!shown && hostBox())
        if (const int k = hostBox()(from, caption, box); k >= 0 && k < int(box.buttons.size())) return box.buttons[size_t(k)].id;
    MessageView view;
    view.b = std::move(box);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    dlg.setInsets({0, 0, 0, 0, 0, 0});  // the view is the whole window: it paints its own buttons
    view.setDialog(&dlg);
    if (shown) view.shown = [&view, shown] { shown(view); };
    draw::Point size{};
    if (!measure) measure = shellMeasureCanvas();  // (the host's, when it has one)
    if (measure) size = view.layout(*measure);
    else {  // the estimate (the UI font's letters 7 px wide, its lines 16 px high, at 100 %)
        const int pad = scale(stardraft::kit::kDialogPad), wrap = scale(420), iconW = view.b.icon == Icon::None ? 0 : scale(32);
        int longest = 0, lines = 0;
        size_t start = 0;
        while (true) {
            const size_t nl = view.b.text.find(L'\n', start);
            const int w = int((nl == std::wstring::npos ? view.b.text.size() : nl) - start) * scale(7);
            longest = std::max(longest, w), lines += std::max(1, (w + wrap - 1) / wrap);
            if (nl == std::wstring::npos) break;
            start = nl + 1;
        }
        int bw = 0;
        for (const Button& bt : view.b.buttons)
            bw += std::max(scale(stardraft::kit::kButtonMinW), int(bt.name.size()) * scale(7) + scale(28)) + (bw ? scale(stardraft::kit::kButtonGap) : 0);
        const int contentH = std::max(lines * scale(16), iconW);
        size = {std::max(pad + iconW + (iconW ? scale(12) : 0) + std::min(wrap, longest) + pad, pad + bw + pad),
                pad + contentH + scale(24) + scale(stardraft::kit::kButtonH) + pad};
    }
    dlg.run(from, caption, size);
    view.choose(view.b.cancel);  // (the close box, or no window: the cancel button)
    return view.b.result;
}

// The box with the buttons `names` from left to right: the first has the focus (Enter), the last is the cancel one
// (Escape, the close box). Returns the index of the button that closed it. (The SDL program's questions and errors.)
// makeBox: that box, with the focus on button `focus` (1: the second, as MB_DEFBUTTON2), for runMessageBox or a snapshot.
inline Box makeBox(const std::wstring& text, const std::vector<std::wstring>& names, Icon icon, int focus = 0) {
    Box b;
    b.text = text, b.icon = icon;
    for (size_t k = 0; k < names.size(); ++k) b.buttons.push_back({names[k], int(k)});
    b.cancel = std::max(0, int(names.size()) - 1);
    b.focus = std::clamp(focus, 0, b.cancel);
    return b;
}
inline int ask(draw::View& from, const std::wstring& caption, const std::wstring& text, const std::vector<std::wstring>& names, Icon icon,
               draw::Canvas* measure = nullptr) {
    return runMessageBox(from, caption, makeBox(text, names, icon), measure);
}

}  // namespace msgbox
