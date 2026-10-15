// A value's pop-out in a snapshot (--trigpop, --classictrig with a value): a snapshot has no windows of its own, so
// the pop-outs' seams (sdl_triggers.inc, sdl_classic_triggers.inc) put the pop-out that the value opens here in place of
// running it, while gPopShot.wanted is set, and the snapshot shows it over the window where it opens, with a 1 px border
// (and a dialog's title bar). Included by main_sdl.cpp before the workspaces' files.

class PopShotFrame final : public draw::View {
public:
    // `content` shows in the frame (owned); `inner` is what it holds when it is a dialog (owned too); `title`: a dialog's.
    PopShotFrame(std::unique_ptr<draw::View> content, std::unique_ptr<draw::View> inner, const std::wstring& title)
        : inner_(std::move(inner)), content_(std::move(content)), title_(title) {
        add(*content_, {0, 0, 0, 0});
    }
    int titleH() const { return title_.empty() ? 0 : scale(30); }
    void resized() override { content_->setFrame({1, 1 + titleH(), std::max(2, width() - 1), std::max(2, height() - 1)}); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBorder);
        if (title_.empty()) return;
        c.fill({1, 1, width() - 1, 1 + titleH()}, kPanel);
        c.text({scale(12), 1, width() - scale(12), 1 + titleH()}, title_, kText, draw::kMiddle | draw::kEllipsis);
    }

private:
    std::unique_ptr<draw::View> inner_;    // (a dialog's content goes after the dialog)
    std::unique_ptr<draw::View> content_;
    std::wstring title_;
};

struct PopShot {
    bool wanted = false;                  // the next pop-out goes into the picture
    std::unique_ptr<PopShotFrame> frame;  // the pop-out that went (its place: the frame's)
};
PopShot gPopShot;

// The pop-out `content` (`size` its window's inner size) at `at` in the main window, kept in it (as a pop-out is kept
// on the work area).
void popShotTake(std::unique_ptr<draw::View> content, draw::Point at, draw::Point size, const std::wstring& title = L"",
                 std::unique_ptr<draw::View> inner = nullptr) {
    auto f = std::make_unique<PopShotFrame>(std::move(content), std::move(inner), title);
    const int w = size.x + 2, h = size.y + 2 + f->titleH();
    const int rw = gRoot ? gRoot->width() : w, rh = gRoot ? gRoot->height() : h;
    const int x = std::clamp(at.x, 0, std::max(0, rw - w)), y = std::clamp(at.y, 0, std::max(0, rh - h));
    f->setFrame({x, y, x + w, y + h});
    gPopShot.frame = std::move(f);
}
// Runs `open` (a value's click) with the pop-outs going into the picture; true when one went, over the root view.
bool popShotOpen(const std::function<void()>& open) {
    gPopShot.wanted = true;
    open();
    gPopShot.wanted = false;
    if (!gPopShot.frame || !gRoot) return false;
    const draw::Rect r = gPopShot.frame->frame();
    gRoot->add(*gPopShot.frame, r);
    return true;
}
