// The Log (bottom of the window; View > Log, Ctrl+L): everything the status bar said, euddraft's build output and the
// start-up checks, newest last. Collapsed it is one line: the latest message. It opens by itself on an error; a click on
// its bar opens or closes it, and its top edge changes its height (gDock.logH). The model and its view are portable; the
// Windows program keeps its own panel (main.cpp's LogView, with the native edit) until the SDL program replaces it. The
// host places the view (computeLayout's log slot, logPaneHeight).

struct LogState {
    std::wstring text;  // the lines, each "hh:mm:ss  " (and "ERROR  ") before it, newest last
    std::wstring latest, latestTime;  // the latest line and when (hh:mm)
    std::chrono::steady_clock::time_point latestAt{};  // then (an older line shows dim: not a message from now)
    bool latestError = false, expanded = false;
    int errors = 0;
    bool sizing = false;  // its top edge is dragged
    int grabY = 0, grabH = 0;
};
LogState& logState() {
    static LogState s;
    return s;
}

// Adds a line with its time. False when it is not added: empty, or the same message again (an error is always added).
// The newest half stays when the text gets long.
bool logAdd(LogState& s, const std::wstring& line, bool error) {
    if (line.empty() || (!error && line == s.latest)) return false;
    if (s.text.size() > 400000) {
        const size_t cut = s.text.find(L'\n', s.text.size() / 2);
        s.text = L"(older lines dropped)\n" + (cut == std::wstring::npos ? std::wstring() : s.text.substr(cut + 1));
    }
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);
    wchar_t stamp[16] = L"", hm[8] = L"";
    if (t) std::swprintf(stamp, 16, L"%02d:%02d:%02d  ", t->tm_hour, t->tm_min, t->tm_sec), std::swprintf(hm, 8, L"%02d:%02d", t->tm_hour, t->tm_min);
    s.text += stamp + std::wstring(error ? L"ERROR  " : L"") + line + L"\n";
    s.latest = line, s.latestError = error;
    s.latestTime = hm, s.latestAt = std::chrono::steady_clock::now();
    if (error) ++s.errors;
    return true;
}

// The Log's height in the window: the bar when it is closed; open, the height that the user dragged, but the workspace
// keeps at least 200 pixels (with the top bar, the tabs and the status bar above and below it).
int logPaneHeight(int clientH, int topBarH, int tabsH, int statusH) {
    const LogState& s = logState();
    if (!s.expanded) return scale(24);
    const int room = clientH - topBarH - tabsH - statusH - scale(200);
    return std::clamp(scale(gDock.logH), scale(80), std::max(scale(80), room));
}

// The Log's view: the bar (the toggle, "Log", the error count, the time and the latest line) and, open, the lines under
// it in a read-only text (select and copy them).
class LogPaneView final : public draw::View {
public:
    LogPaneView() {
        text_.setMultiline(true), text_.setWrap(false), text_.setReadOnly(true), text_.setPlain(true);
        text_.setFont({draw::Face::Mono, 0});
        add(text_, draw::Rect(0, 0, 0, 0));
        text_.setVisible(false);
    }
    ~LogPaneView() override { remove(text_); }

    // The text again (a line was added) and the bar.
    void changed() {
        LogState& s = logState();
        if (text_.text() != s.text) text_.setText(s.text);  // (the caret at the end: the newest lines in view)
        dimLater();
        invalidate();
    }
    // Opens or closes the Log (the host places the panes again).
    void setExpanded(bool on) {
        LogState& s = logState();
        if (s.expanded == on) return;
        s.expanded = on;
        text_.setVisible(on);
        if (on) text_.setText(s.text);
        shellRelayout();
    }
    bool expanded() const { return logState().expanded; }

    void resized() override {
        const int bar = scale(24);
        text_.setFrame(draw::Rect(scale(6), bar + scale(2), std::max(scale(6), width() - scale(4)), std::max(bar + scale(2), height())));
    }
    void paint(draw::Canvas& c) override {
        const LogState& s = logState();
        const int w = width(), bar = scale(24);
        c.fill({0, 0, w, height()}, kBg);  // (the lines' background)
        c.fill({0, 0, w, bar}, kPanel);
        c.fill({0, 0, w, 1}, kBorder);  // the line at the top
        // A chevron from the icon set: down closes the log, up opens it.
        c.icon({scale(8), 0, scale(24), bar}, int(s.expanded ? UiIcon::ChevronDown : UiIcon::ChevronUp), kDim, scale(16));
        c.text({scale(30), 0, scale(80), bar}, tr(L"Log"), kDim, draw::kMiddle);
        int x = scale(80);
        if (s.errors) {
            const std::wstring e = std::to_wstring(s.errors) + tr(s.errors == 1 ? L" error" : L" errors");
            c.text({x, 0, x + scale(90), bar}, e, kBad, draw::kMiddle);
            x += scale(90);
        }
        if (!s.latestTime.empty()) {  // its time, small and dim: a log line (the status bar says what is now)
            c.text({x, 0, x + scale(44), bar}, s.latestTime, kFaint, draw::kMiddle);
            x += scale(44);
        }
        const bool fresh = std::chrono::steady_clock::now() - s.latestAt < std::chrono::seconds(10);  // older: dim
        c.text({x, 0, w - scale(10), bar}, s.latest, s.latestError ? kBad : fresh ? kText : kDim, draw::kMiddle | draw::kEllipsis);
        if (keyFocus_ && hasFocus()) paintFocusOutline(c, {scale(2), scale(2), scale(80), bar - scale(2)});  // (the chevron and "Log")
    }

    // The keyboard: the bar is one Tab stop; Enter and Space open or close the Log.
    bool focusable() const override { return true; }
    bool focusOnClick() const override { return false; }  // (a click leaves the keyboard where it is)
    void focusChanged(bool) override { invalidate(); }
    void tabbedTo(bool) override { keyFocus_ = true, invalidate(); }
    bool key(const draw::KeyEvent& e) override {
        if (!hasFocus() || !pressKey(e)) return false;  // (not a key of the Log's text)
        keyFocus_ = true;
        pressFromKeys([this] { setExpanded(!expanded()); });
        return true;
    }
    std::wstring focusedItem() const override { return keyFocus_ ? (expanded() ? L"Log (open)" : L"Log") : L""; }
    bool mouse(const draw::MouseEvent& e) override {
        LogState& s = logState();
        // The window's coordinates: the panel moves as it grows.
        const int y = e.pt.y + origin().y;
        switch (e.kind) {
        case draw::MouseKind::Move:
            if (s.sizing) {
                const int h = s.grabH - (y - s.grabY);
                const int logH = std::clamp(h * 96 / int(std::max(1u, shellDpi())), 80, 1200);
                if (logH != gDock.logH) gDock.logH = logH, shellRelayout();
            }
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick:
            if (e.button != 0) return false;
            keyFocus_ = false;
            if (s.expanded && e.pt.y < scale(5)) {  // the top edge: drag to change the height
                s.sizing = true, s.grabY = y, s.grabH = height();
                if (draw::Host* h = host()) h->capture(*this, true);
            } else if (e.pt.y < scale(24))
                setExpanded(!s.expanded);
            return true;
        case draw::MouseKind::Up:
            if (e.button != 0) return false;
            if (s.sizing)
                if (draw::Host* h = host()) h->capture(*this, false);
            return true;
        default: return false;
        }
    }
    draw::Cursor cursor(draw::Point p) override {
        const LogState& s = logState();
        return s.sizing || (s.expanded && p.y < scale(5)) ? draw::Cursor::SizeNS : draw::Cursor::Hand;
    }
    void captureLost() override {  // the drag ends (the mouse went up, or the system took the mouse)
        LogState& s = logState();
        if (s.sizing) s.sizing = false, gDock.logH = std::clamp(height() * 96 / int(std::max(1u, shellDpi())), 80, 1200), saveDock();
    }

    stardraft::kit::TextField& lines() { return text_; }

private:
    // The latest line turns dim 10.5 seconds after it came (a message from before is not "now").
    void dimLater() {
        draw::Host* h = host();
        if (!h) return;
        if (dimTimer_) h->cancelTimer(dimTimer_);
        dimTimer_ = h->timer(10500, [this] {
            dimTimer_ = 0;
            invalidate();
        });
    }
    stardraft::kit::TextField text_;
    int dimTimer_ = 0;
    bool keyFocus_ = false;  // the focus came from Tab: the outline shows
};
