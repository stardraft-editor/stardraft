// The hover tooltip (portable, core.inc): a view's tip() or a painted part's tip, painted on the Canvas as the program's
// tooltips always looked (the native ones, which tipPaintProc painted): the control color, the text in the text color
// in the UI font, wrapped at 360 px, 9 px from the sides and 6 px from the top and bottom. The window it is in is a
// Tooltip window (Host::openWindow: no title, never active, the pop-ups' rounded corners and quiet border). The Windows
// host is tip_win32.inc (the delays, the place under the mouse).

// The shortcuts in a text as this system shows them (the shell's: on macOS the Mac symbols, "⇧⌘S" for Ctrl+Shift+S).
std::wstring shellKeysText(const std::wstring& s);

class TipView final : public draw::View {
public:
    std::wstring text;

    static constexpr int kMaxTextW = 360;          // the text wraps at this width (at 100 %)
    static constexpr int kMarginX = 9, kMarginY = 6;  // around the text (at 100 %)
    // The window is this much wider and higher than the text and its margins, as the native tooltip was (at any scale).
    static constexpr int kExtraW = 2, kExtraH = 3;

    // The window's size for the text, measured in the UI font.
    draw::Point measure(draw::Canvas& c) const {
        const draw::Font of = c.setFont(c.font({}));
        const draw::Point t = c.measureBox(text, scale(kMaxTextW), draw::kWrap);
        c.setFont(of);
        return {t.x + 2 * scale(kMarginX) + kExtraW, t.y + 2 * scale(kMarginY) + kExtraH};
    }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kCtl);
        const draw::Font of = c.setFont(c.font({}));
        c.text({scale(kMarginX), scale(kMarginY), width() - scale(kMarginX), height() - scale(kMarginY)}, text, kText, draw::kWrap);
        c.setFont(of);
    }
};

// The hover tooltips of a host that tells where the mouse is (the SDL program: SdlHost::setHoverHook). The rules are
// those of the Windows program's HoverTips (tip_win32.inc), with a wait of kWaitMs for the double-click time: the tip
// shows after the wait with the mouse on a part with a tip (a tenth of it when a tip showed a moment ago), under the
// mouse pointer; it stays while the mouse is on a part with the same text, goes when the text under the mouse changes,
// on a press, a wheel turn or a key, when the mouse leaves the window, and after ten times the wait.
class HoverTipper {
public:
    static constexpr int kWaitMs = 500;  // (Windows' double-click time by default)
    static constexpr int kBelow = 21;    // the tip's top below the pointer's hot spot (at 100 %; the arrow's height)

    // The mouse is at `pt` in `root` (a window's root view), on a part with the tip `t` (empty: none).
    void moved(draw::View& root, draw::Point pt, std::wstring t) {
        if (&root != owner_) hide(), cancel(), text_.clear(), popped_ = false, owner_ = &root;
        at_ = pt;
        if (t == text_) return;  // the same part (or still none): the tip stays, or its wait goes on
        const bool was = shown_;
        hide();
        text_ = std::move(t), popped_ = false;
        if (text_.empty()) return cancel();
        const bool recent = std::chrono::steady_clock::now() - hiddenAt_ < std::chrono::milliseconds(kWaitMs);
        start(was || recent ? std::max(1, kWaitMs / 10) : kWaitMs);
    }
    // The tip of `root` goes and waits for the next move (a press, a wheel turn, a key, the mouse left; the window
    // closed: then `root` is forgotten).
    void reset(draw::View& root) {
        if (&root != owner_) return;
        hide(), cancel();
        text_.clear(), popped_ = false, owner_ = nullptr;
    }
    // The tip window now shows this text (empty: none shows).
    const std::wstring& shownText() const { return shown_ ? view_.text : kNoText; }

private:
    inline static const std::wstring kNoText;

    void cancel() {
        if (timer_ && owner_ && owner_->host()) owner_->host()->cancelTimer(timer_);
        timer_ = 0, ++gen_;  // (a timer that could not be cancelled does nothing when it comes)
    }
    void start(int ms) {
        cancel();
        const int gen = gen_;
        if (owner_ && owner_->host()) timer_ = owner_->host()->timer(ms, [this, gen] {
            if (gen == gen_) tick();
        });
    }
    void hide() {
        if (shown_ && owner_ && owner_->host()) owner_->host()->closeWindow(view_);
        if (shown_) hiddenAt_ = std::chrono::steady_clock::now();
        shown_ = false;
    }
    void tick() {
        timer_ = 0;
        if (shown_) {  // the tip showed long enough
            hide();
            popped_ = true;  // (it shows again when the mouse is on another part)
            return;
        }
        if (popped_ || text_.empty() || !owner_ || !owner_->host()) return;
        open();
    }
    void open() {
        view_.text = shellKeysText(text_);  // (macOS: the Mac symbols of the shortcuts)
        draw::Point size{};
        if (draw::Canvas* c = shellMeasureCanvas()) size = view_.measure(*c);
        else size = {std::min(int(text_.size()) * scale(7), scale(TipView::kMaxTextW)) + 2 * scale(TipView::kMarginX) + TipView::kExtraW,
                     scale(16) + 2 * scale(TipView::kMarginY) + TipView::kExtraH};  // (an estimate: the UI font's letters)
        const int y = at_.y + scale(kBelow);
        owner_->host()->openWindow(*owner_, view_, draw::WindowSpec{draw::WindowKind::Tooltip, draw::Rect{at_.x, y, at_.x + size.x, y + size.y}, L""},
                                   [this] { shown_ = false; });
        shown_ = true;
        start(kWaitMs * 10);  // it goes after a while
    }

    draw::View* owner_ = nullptr;  // the root view the mouse is over (the tip's owner)
    draw::Point at_{};
    std::wstring text_;            // the tip under the mouse (shown, or waiting)
    bool shown_ = false, popped_ = false;
    int timer_ = 0, gen_ = 0;
    std::chrono::steady_clock::time_point hiddenAt_{};
    TipView view_;
};
