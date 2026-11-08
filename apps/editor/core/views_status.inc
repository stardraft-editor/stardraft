// The status bar as a view (portable, core.inc): moved from main.cpp. A flat strip drawn in full, the panel color, a
// hairline on top and between the parts, no bevels. The map's parts (mode and tool, the mouse's place, the zoom, the
// selection, the mode's count, frames) show in the Map workspace only; the message shows only when there is no log line (which shows it
// already). The corner keeps room for the size grip, which the bar does not draw: over it, the window resizes from the
// corner (sizeGrip). The instance (gStatus) is made in main.cpp, where the app state is.

// Defined by the program (main_sdl.cpp and its sdl_*.inc files) until they move into the core.
int activePage();
// Program options > Updates > Show system diagnostics: the frames a second and the memory in use, at the right of
// the status bar in every workspace (the program keeps the text new, once a second).
bool diagnosticsOn();
void setDiagnostics(bool on);
inline std::wstring gDiagnosticsText;
void diagnosticsRefresh();  // (made again when a second has gone)

// The status bar's view: its texts are the app state's (AppState::statusText, the bar's seven parts).
struct StatusView final : stardraft::draw::View {
    explicit StatusView(AppState& s) : st(s) {}
    AppState& st;
    void paint(stardraft::draw::Canvas& c) override {
        using stardraft::draw::Rect;
        using stardraft::draw::kEllipsis;
        using stardraft::draw::kMiddle;
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);
        c.fill({0, 0, w, 1}, kBorder);
        bool docOpen();
        bool newPageCovers();
        // (a page: Start, the map pages, the New page over a map; no map open: no map parts)
        const bool map = gWorkspace == Workspace::Map && !activePage() && docOpen() && !newPageCovers();
        const int pad = scale(8);
        auto part = [&](int l, int r, int i, COLORREF col) {  // (i -1: the diagnostics)
            c.text({l + pad, 1, r - pad, h}, i < 0 ? gDiagnosticsText : st.statusText[i], col, kMiddle | kEllipsis);
        };
        auto sep = [&](int x) { c.fill({x, scale(4), x + 1, h - scale(3)}, kBorder); };
        int x = scale(4), right = w - scale(16);  // room for the size grip
        if (diagnosticsOn()) diagnosticsRefresh();
        if (diagnosticsOn() && !gDiagnosticsText.empty()) {
            const int dw = c.measure(gDiagnosticsText) + 2 * pad;
            if (right - dw > x + scale(200)) {
                part(right - dw, right, -1, kDim);
                sep(right - dw);
                right -= dw;
            }
        }
        if (map) {
            // (mode and tool, the mouse's place, the zoom, the selection, the mode's count)
            const int parts[5] = {0, 1, 2, 3, 6}, mins[5] = {150, 200, 110, 100, 100};
            for (int k = 0; k < 5; ++k) {
                const int i = parts[k];
                if (i == 6 && st.statusText[i].empty()) continue;
                const int pw = std::max(scale(mins[k]), c.measure(st.statusText[i]) + 2 * pad);
                if (i == 6 && x + pw > right) continue;  // (a narrow window: no count, not over the size grip)
                part(x, x + pw, i, i == 0 ? kText : kDim);
                x += pw;
                sep(x);
            }
#ifndef NDEBUG  // frames and sprites: for developers (debug builds), not in the release's status bar
            const int fw = std::max(scale(130), c.measure(st.statusText[5]) + 2 * pad);
            if (right - fw > x) {
                part(right - fw, right, 5, kDim);
                sep(right - fw);
                right -= fw;
            }
#endif
        }
        if (!shellLogOpen() && right > x) part(x, right, 4, kText);
    }
    bool sizeGrip(stardraft::draw::Point p) override { return p.x >= width() - scale(16) && p.y >= height() - scale(16); }
};
