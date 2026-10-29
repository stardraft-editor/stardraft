// The map's loading view (portable, core.inc): while a map opens or is made (a file, a project's map, File > New, a new
// Cosmonarchy map), the workspace is blank but for what is done ("Opening x.scx"), a progress bar and the steps done so
// far with their times (the log). The map is made on the main thread: the host draws the window after each step
// (shellLoadingFrame), so the view shows while the work goes on; at the end it goes (loadingEnd).

void shellLoadingFrame();  // (the host: lays the window out and draws it now; nothing in a test run without a window)
void shellLoadingDone();   // (the host: lays the window out without the view)

struct MapLoading {
    bool on = false;
    std::wstring title;               // "Opening vterran01.scx"
    float done = 0;                   // the part of the open done, 0 to 1
    std::vector<std::wstring> lines;  // each step done: its name and its time
};
MapLoading gLoading;

// `title`: what is done ("Opening x.scx", "Making a new map").
void loadingStart(const std::wstring& title) {
    gLoading = {};
    gLoading.on = true;
    gLoading.title = title;
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
void loadingEnd() {
    if (!gLoading.on) return;
    gLoading.on = false;
    shellLoadingDone();
}
// Each step of the editor's open (Editor::onOpenStep) a step of the view, the bar from `from` to `to`.
void loadingSteps(Editor& ed, float from, float to) {
    auto n = std::make_shared<int>(0);
    ed.onOpenStep = [n, from, to](const char* what, double ms) { loadingStep(widen(what), ms, std::min(to, from + (to - from) * float(++*n) / 4)); };
}
// `work` as one step of the view (its time); `done`: the bar after it. Returns what `work` returns.
template <class Work>
auto loadingTimed(const std::wstring& what, float done, Work&& work) {
    const auto t0 = std::chrono::steady_clock::now();
    auto r = work();
    loadingStep(what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), done);
    return r;
}

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
