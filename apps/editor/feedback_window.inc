// The feedback dialog's window (included by main.cpp after options_window.inc; feedback.inc has the rest: the menu item,
// the files, the sending). The dialog is the view core/views_feedback.inc, which runs modal from the program's anchor
// (Host::runModal): the window is a Dialog frame over the main window, or over the window the feedback is from (Unit
// properties, a location, Program options; GitHub feedback #97). Send keeps it open while the feedback goes.

// The program's part of the dialog: writing and sending the feedback, the links, closing, the spinner's timer.
class FeedbackProgram final : public FeedbackHost {
public:
    void send(FeedbackView& v, const std::wstring& typed) override {
        const fs::path file = feedbackWrite(v.on(), typed);  // the file waits there if it cannot go now
        const FeedbackView* sentFrom = &v;  // the dialog that sent it (and its open): the result goes only there
        const unsigned serial = gFbSerial;
        feedbackSend(v, file, [sentFrom, serial]() -> FeedbackView* { return gFbView && gFbView == sentFrom && gFbSerial == serial ? gFbView : nullptr; });
    }
    void openDiscord() override { ShellExecuteW(nullptr, L"open", kDiscordInvite, nullptr, nullptr, SW_SHOWNORMAL); }
    void openIssue(const std::wstring& page) override { ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    void close() override { feedbackClose(); }
    int timer(int ms, std::function<void()> run) override {
        stardraft::draw::Host* h = gFbView ? gFbView->host() : nullptr;
        return h ? h->timer(ms, std::move(run)) : -1;
    }
    void cancelTimer(int id) override {
        stardraft::draw::Host* h = gFbView ? gFbView->host() : nullptr;
        if (h && id >= 0) h->cancelTimer(id);
    }
};
FeedbackProgram gFeedbackProgram;

// The dialog's window (nullptr: it is not open).
HWND feedbackWindow() { return gFbView ? ui::Win32Host::windowOf(*gFbView) : nullptr; }

// Closes the dialog (its window goes; the modal loop returns).
void feedbackClose() {
    if (gFbView) gMainAnchor.host.closeWindow(*gFbView);
}

void showFeedbackDialog(const std::wstring& on) {
    FeedbackView view(gFeedbackProgram, on);
    gFbView = &view;
    ++gFbSerial;
    // From a window over the main window (Unit properties): that one waits too, and is active again after.
    const HWND from = GetActiveWindow();
    const HWND over = from && from != app.main && IsWindowEnabled(from) ? from : nullptr;
    const bool mainOn = IsWindowEnabled(app.main) != FALSE;
    EnableWindow(app.main, FALSE);
    gMainAnchor.host.ownerWindow(over ? over : app.main);
    if (gFeedbackHook) shellPost([] {  // the hook runs once the dialog is open (the modal loop runs the post)
        if (gFbView && gFeedbackHook) gFeedbackHook(*gFbView);
    });
    gMainAnchor.host.runModal(gMainAnchor.view, view, stardraft::draw::WindowSpec{stardraft::draw::WindowKind::Dialog,
                                                                                  stardraft::draw::Rect{0, 0, scale(592), scale(370)}, L"Report feedback"});
    gFbView = nullptr;
    if (mainOn) EnableWindow(app.main, TRUE);
    if (!gBackground) SetForegroundWindow(over ? over : app.main);
    gMainAnchor.host.ownerWindow(app.main);
}
