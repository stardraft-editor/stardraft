// The map's loading view (portable, core.inc): while a map opens or is made (a file, a project's map, File > New, a new
// Cosmonarchy map), the workspace is blank but for what is done ("Opening x.scx"), a progress bar and the steps done so
// far with their times (the log). The map is made on the main thread: the host draws the window after each step
// (shellLoadingFrame) once the work has taken a moment (a quick open shows no view), and at the end it goes (loadingEnd).

void shellLoadingFrame();  // (the host: lays the window out and draws it now; nothing in a test run without a window)
void shellLoadingDone();   // (the host: lays the window out without the view)

struct MapLoading {
    bool on = false;
    std::wstring title, file;         // what is done ("Opening the map…") and the map's file (none for a new map)
    float done = 0;                   // the part of the open done, 0 to 1
    std::vector<std::wstring> lines;  // each step done: what it did ("Loading the units…") and its time
    std::wstring current;             // the step that runs now (the last line, with no time)
    std::chrono::steady_clock::time_point started;
};
MapLoading gLoading;

// `title`: what is done ("Opening the map…", "Making a new map…"); `file`: the map's file name.
void loadingStart(const std::wstring& title, const std::wstring& file = {}) {
    gLoading = {};
    gLoading.on = true;
    gLoading.title = title, gLoading.file = file;
    gLoading.started = std::chrono::steady_clock::now();
    shellLoadingFrame();
}
// A step done (its time in ms); `done`: the part of the open done after it; `next`: the step that runs now (none: empty).
void loadingStep(const std::wstring& what, double ms, float done, const std::wstring& next = {}) {
    if (!gLoading.on) return;
    gLoading.lines.push_back(what + L"   " + std::to_wstring(std::lround(ms)) + L" ms");
    gLoading.done = done;
    gLoading.current = next;
    shellLoadingFrame();
}
// The step that starts now (the last line until it is done).
void loadingNext(const std::wstring& what) {
    if (!gLoading.on) return;
    gLoading.current = what;
    shellLoadingFrame();
}
void loadingEnd() {
    if (!gLoading.on) return;
    gLoading.on = false;
    shellLoadingDone();
}
// Each step of the editor's open (Editor::onOpenStep, in its order) a step of the view, the bar from `from` to `to`; the
// first one starts now.
void loadingSteps(Editor& ed, float from, float to) {
    static const std::pair<const char*, const wchar_t*> kSteps[] = {{"read the scenario", L"Reading the map file\x2026"},
                                                                  {"parse the scenario", L"Reading the map's data\x2026"},
                                                                  {"unit types and sprites", L"Loading the units\x2026"},
                                                                  {"terrain, tileset, units", L"Loading the terrain\x2026"}};
    constexpr int n = int(std::size(kSteps));
    auto at = std::make_shared<int>(0);
    ed.onOpenStep = [at, from, to](const char* what, double ms) {
        int k = 0;
        while (k < n && std::strcmp(kSteps[k].first, what) != 0) ++k;
        const std::wstring name = k < n ? std::wstring(tr(kSteps[k].second)) : widen(what);
        const std::wstring next = k + 1 < n ? std::wstring(tr(kSteps[k + 1].second)) : std::wstring();
        loadingStep(name, ms, std::min(to, from + (to - from) * float(++*at) / n), next);
    };
    loadingNext(tr(kSteps[0].second));
}
// `work` as one step of the view ("Loading the game data…": the last line while it runs, then with its time); `done`:
// the bar after it. Returns what `work` returns.
template <class Work>
auto loadingTimed(const std::wstring& what, float done, Work&& work) {
    loadingNext(what);
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
        const int fileRows = gLoading.file.empty() ? 0 : 1;  // (the title, then the file's name, over the bar)
        const int ty = k.barY - scale(8) - rowH * (1 + fileRows);
        c.text({k.x, ty, k.x + k.w, ty + rowH}, gLoading.title, kText, draw::kMiddle | draw::kEllipsis);
        if (fileRows) c.text({k.x, ty + rowH, k.x + k.w, ty + 2 * rowH}, gLoading.file, kDim, draw::kMiddle | draw::kEllipsis);
        bar_.setValue(gLoading.done);
        int y = k.barY + scale(6) + scale(12);
        for (const std::wstring& l : gLoading.lines) {
            c.text({k.x, y, k.x + k.w, y + rowH}, l, kDim, draw::kMiddle | draw::kEllipsis);
            y += rowH;
        }
        if (!gLoading.current.empty()) c.text({k.x, y, k.x + k.w, y + rowH}, gLoading.current, kText, draw::kMiddle | draw::kEllipsis);
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
