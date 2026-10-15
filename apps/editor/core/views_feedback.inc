// Feedback (portable, core.inc): the dialog that "Report feedback on ..." opens. The text box (kit text field, several
// lines), the Send and Cancel buttons (kit buttons), and the painted rest: the glyph and the title, the note, the Discord
// link, the spinner while the feedback goes, and the thank-you with the issue's link. Send keeps the dialog open: the
// host writes the feedback and sends it (on a thread), then tells the view the result (done). Ctrl+Enter sends, Escape
// closes. The host (feedback.inc on Windows) writes and sends the files, opens the links and closes the window.

// The program's part of the feedback dialog (feedback.inc).
class FeedbackView;
class FeedbackHost {
public:
    virtual ~FeedbackHost() = default;
    // Writes the feedback and sends it: the host calls the view's done() when it is sent, or kept to go at the next start.
    virtual void send(FeedbackView& v, const std::wstring& typed) = 0;
    // The Discord server's link, and the issue's page (after Send).
    virtual void openDiscord() = 0;
    virtual void openIssue(const std::wstring& page) = 0;
    // Closes the dialog (Cancel, Close, Escape, the close box of the window).
    virtual void close() = 0;
    // The dialog's spinner: the host runs `run` after `ms` (the view asks again while it spins).
    virtual int timer(int ms, std::function<void()> run) = 0;
    virtual void cancelTimer(int id) = 0;
};

class FeedbackView final : public draw::View {
public:
    FeedbackView(FeedbackHost& host, std::wstring on) : host_(host), on_(std::move(on)) {
        text_.setMultiline(true), text_.setWrap(true);
        text_.onChange = [this] {
            send_.setEnabled(!text_.text().empty());
            invalidate();
        };
        text_.onEnter = [this] {  // Ctrl+Enter: Send, as the button does when there is text
            if (send_.enabled()) doSend();
        };
        add(text_, {0, 0, 0, 0});
        send_.setText(L"Send"), send_.setTone(stardraft::kit::Tone::Primary), send_.setEnabled(false);
        send_.onClick = [this] { doSend(); };
        cancel_.setText(L"Cancel");
        cancel_.onClick = [this] {
            if (stage_ == 0) typed_ = text_.text();
            host_.close();
        };
        add(send_, {0, 0, 0, 0}), add(cancel_, {0, 0, 0, 0});
    }

    // The text the user typed (the self-test reads it), and the state of the dialog.
    const std::wstring& on() const { return on_; }
    stardraft::kit::TextField& textField() { return text_; }
    std::wstring typed() const { return typed_.empty() ? text_.text() : typed_; }
    int stage() const { return stage_; }  // 0 typing, 1 sending, 2 done
    const std::wstring& page() const { return page_; }
    bool sendShown() const { return stage_ == 0; }
    bool issueShown() const { return stage_ == 2 && !issue_.empty(); }

    // The host's result: sent (number: the issue's number) or kept for the next start.
    void done(bool sent, long number, const std::wstring& page) {
        if (stage_ != 1) return;
        stopSpin();
        stage_ = 2;
        if (sent) {
            status_ = tr(L"Thank you for your feedback.");
            page_ = page;
            issue_ = number > 0 ? tr(L"Open it on GitHub (#") + std::to_wstring(number) + L")" : std::wstring();
        } else {
            status_ = L"StarDraft 2 could not send it now. It sends it when StarDraft 2 starts the next time.";
        }
        cancel_.setText(L"Close");
        invalidate();
    }
    // The host shows the sending state (before the thread answers) or a kept result at once.
    void sending(const std::wstring& status) {
        typed_ = text_.text();
        stage_ = 1;
        status_ = status;
        send_.setEnabled(false);
        startSpin();
        invalidate();
    }
    // Sets the status and the stage at once (a build without a token, the tests).
    void showStatus(const std::wstring& status) {
        status_ = status;
        invalidate();
    }
    // Tests: the text the user typed, and the spinner's dot.
    void setTyped(const std::wstring& t) {
        text_.setText(t);
        send_.setEnabled(!t.empty());
    }
    void setSpin(int s) { spin_ = s % 8; }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Leave) return false;
        if (e.kind == draw::MouseKind::Down && e.button == 0) {
            if (discordRect_.contains(e.pt)) return host_.openDiscord(), true;
            if (issueRect_.contains(e.pt) && !page_.empty()) return host_.openIssue(page_), true;
        }
        return false;
    }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape) return host_.close(), true;
        return false;
    }
    draw::Cursor cursor(draw::Point pt) override {
        return discordRect_.contains(pt) || (issueRect_.contains(pt) && !page_.empty()) ? draw::Cursor::Hand : draw::Cursor::Arrow;
    }

    ~FeedbackView() override { stopSpin(); }

private:
    void doSend() {
        const std::wstring t = text_.text();
        if (t.empty() || stage_ != 0) return;
        typed_ = t;
        host_.send(*this, t);  // the host calls sending() and done()
    }
    void startSpin() {
        stopSpin();
        spinTimer_ = host_.timer(90, [this] {
            spin_ = (spin_ + 1) % 8;
            invalidate();
            if (stage_ == 1) startSpin();
        });
    }
    void stopSpin() {
        if (spinTimer_ >= 0) host_.cancelTimer(spinTimer_), spinTimer_ = -1;
    }

    FeedbackHost& host_;
    std::wstring on_;
    stardraft::kit::TextField text_;
    stardraft::kit::Button send_, cancel_;
    std::wstring typed_, status_, page_, issue_;
    int stage_ = 0;
    bool focused_ = false;  // the text box got the keyboard (on the first paint in a window)
    int spin_ = 0;
    int spinTimer_ = -1;
    draw::Rect discordRect_{}, issueRect_{}, statusRect_{};
};

void FeedbackView::paint(draw::Canvas& c) {
    if (!focused_ && host()) focused_ = true, text_.focus();  // the text box has the keyboard when the dialog opens
    const int pad = scale(stardraft::kit::kDialogPad), w = scale(560), editH = scale(200), bh = scale(stardraft::kit::kButtonH);
    const int W = width(), H = height();
    c.fill({0, 0, W, H}, kBg);
    // The glyph and the title ("Feedback on <the thing>").
    c.icon({scale(12), scale(10), scale(12) + scale(20), scale(10) + scale(20)}, int(kFeedbackIcon), kFeedback, scale(16));
    c.text({pad + scale(26), pad + scale(1), pad + w, pad + scale(21)}, tr(L"Feedback on ") + on_, kText, draw::kMiddle | draw::kEllipsis);
    int y = pad + scale(24);
    const draw::Rect editR{pad, y, pad + w, y + editH};
    const bool typing = stage_ == 0;
    if (typing) {
        if (text_.visible() != true) text_.setVisible(true);
        if (text_.frame().left != editR.left || text_.frame().top != editR.top || text_.frame().right != editR.right || text_.frame().bottom != editR.bottom)
            text_.setFrame(editR);
    } else if (text_.visible()) {
        text_.setVisible(false);
    }
    // After Send: the status (with the spinner at its left) and the issue's link, where the text box was.
    statusRect_ = {pad + scale(28), y + editH / 2 - scale(22), pad + w, y + editH / 2 - scale(2)};
    const draw::Rect issueR{pad + scale(28), y + editH / 2 + scale(2), pad + w, y + editH / 2 + scale(22)};
    if (!typing) {
        c.text(statusRect_, status_, kText, draw::kMiddle | draw::kEllipsis);
        if (stage_ == 1) {  // the spinner: 8 dots in a ring, one lit, left of the status
            const int cx = statusRect_.left - scale(16), cy = (statusRect_.top + statusRect_.bottom) / 2, rr = scale(8), dot = std::max(1, scale(1));
            for (int i = 0; i < 8; ++i) {
                const double a = i * 3.14159265 / 4;
                const int x = cx + int(std::lround(rr * std::cos(a))), yy = cy + int(std::lround(rr * std::sin(a)));
                const int age = (spin_ - i + 8) % 8;  // 0: lit
                c.fill({x - dot, yy - dot, x + dot + 1, yy + dot + 1}, age == 0 ? kAccent : age < 3 ? kDim : kBorderStrong);
            }
        }
        if (stage_ == 2 && !issue_.empty()) {
            issueRect_ = issueR;
            c.text(issueR, issue_, page_.empty() ? kText : kAccent, draw::kMiddle | draw::kEllipsis);
        } else {
            issueRect_ = {};
        }
    } else {
        issueRect_ = {};
    }
    y += editH + scale(8);
    // The note.
    c.text({pad, y, pad + w, y + scale(36)},
           L"Feedback is anonymous. It goes on the StarDraft Discord server for review. Do not include sensitive or private information. Ctrl+Enter sends it.",
           kDim, draw::kWrap);
    y += scale(38);
    // The Discord link: its width is its text's.
    const std::wstring discord = tr(L"Open the Discord server");
    discordRect_ = {pad, y, pad + c.measure(discord) + scale(4), y + scale(20)};
    c.text(discordRect_, discord, kAccent, draw::kMiddle);
    y += scale(20) + scale(20);
    // Send and Cancel, at the right edge: Cancel last, Send one gap to its left.
    const int cw = cancel_.preferredWidth(c), sw = send_.preferredWidth(c), gap = scale(stardraft::kit::kButtonGap);
    const draw::Rect cr{pad + w - cw, y, pad + w, y + bh};
    const draw::Rect sr{cr.left - gap - sw, y, cr.left - gap, y + bh};
    send_.setVisible(stage_ == 0), cancel_.setVisible(true);
    if (stage_ == 0) {
        if (send_.frame().left != sr.left || send_.frame().top != sr.top || send_.frame().right != sr.right || send_.frame().bottom != sr.bottom)
            send_.setFrame(sr);
    }
    if (cancel_.frame().left != cr.left || cancel_.frame().top != cr.top || cancel_.frame().right != cr.right || cancel_.frame().bottom != cr.bottom)
        cancel_.setFrame(cr);
}
