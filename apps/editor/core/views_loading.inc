// The map's loading view (portable, core.inc): while a map opens, the workspace is blank but for the file's name, a
// progress bar and the steps done so far with their times (the open's log). The map opens on the main thread: the host
// draws the window after each step (shellLoadingFrame), so the view shows while the work goes on.

void shellLoadingFrame();  // (the host: lays the window out and draws it now; nothing in a test run without a window)

struct MapLoading {
    bool on = false;
    std::wstring title;               // "Opening vterran01.scx"
    float done = 0;                   // the part of the open done, 0 to 1
    std::vector<std::wstring> lines;  // each step done: its name and its time
};
MapLoading gLoading;

void loadingStart(const std::wstring& file) {
    gLoading = {};
    gLoading.on = true;
    gLoading.title = tr(L"Opening ") + file;
    shellLoadingFrame();
}
// A step done (its time in ms); `done`: the part of the open done after it.
void loadingStep(std::wstring what, double ms, float done) {
    if (!gLoading.on) return;
    if (!what.empty()) what[0] = wchar_t(std::towupper(what[0]));
    gLoading.lines.push_back(what + L"   " + std::to_wstring(std::lround(ms)) + L" ms");
    gLoading.done = done;
    shellLoadingFrame();
}
void loadingEnd() { gLoading.on = false; }

class LoadingView final : public draw::View {
public:
    LoadingView() { add(bar_, {}); }
    std::wstring feedbackName(draw::Point) override { return tr(L"Map loading"); }
    void resized() override {
        const Col k = column();
        bar_.setFrame({k.x, k.barY, k.x + k.w, k.barY + scale(6)});
    }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        const Col k = column();
        const int rowH = scale(stardraft::kit::kRowH);
        c.text({k.x, k.barY - scale(8) - rowH, k.x + k.w, k.barY - scale(8)}, gLoading.title, kText, draw::kMiddle | draw::kEllipsis);
        bar_.setValue(gLoading.done);
        int y = k.barY + scale(6) + scale(12);
        for (const std::wstring& l : gLoading.lines) {
            c.text({k.x, y, k.x + k.w, y + rowH}, l, kDim, draw::kMiddle | draw::kEllipsis);
            y += rowH;
        }
    }

private:
    struct Col {
        int x, w, barY;
    };
    // The column in the middle: the title, the bar under it, the steps under the bar (a third down the view).
    Col column() const {
        const int w = std::min(scale(520), std::max(scale(160), width() - 2 * scale(24)));
        return {(width() - w) / 2, w, std::max(scale(60), height() / 3)};
    }
    stardraft::kit::ProgressBar bar_;
};
