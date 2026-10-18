// The panel tabs (portable, core.inc): moved from main.cpp. Each side of the window has a strip of tabs, one per mode
// docked there (the icon and the name), and a click shows that mode's panel. The instances (gPanelTabsView) are made in
// main.cpp, where the window is; the right-click menu (the side) is the host's (shellTabSideMenu, shell.h).

// Defined by the program (main_sdl.cpp and its sdl_*.inc files) until they move into the core.
extern Mode gPanelTab[2];   // the tab each side shows
extern Mode gLastOnSide[2];  // the mode last used on each side (left, right)
void saveDock();
void setMessage(const std::wstring& m);
void setMode(Mode m);

// The panel tabs as a view (apps/common/view.h): one strip per side (side 0: left, 1: right).
struct PanelTabsView final : stardraft::draw::View {
    using Canvas = stardraft::draw::Canvas;
    using Color = stardraft::draw::Color;
    using Cursor = stardraft::draw::Cursor;
    using MouseEvent = stardraft::draw::MouseEvent;
    using MouseKind = stardraft::draw::MouseKind;
    using Point = stardraft::draw::Point;
    using Rect = stardraft::draw::Rect;
    struct Tab {
        Rect r;
        Mode m;
    };
    int side = 0;           // 0: the left side, 1: the right
    std::vector<Tab> tabs;  // as the last paint put them, left to right
    int over = -1;          // the tab under the mouse (an index into tabs), -1: none

    // The modes docked on this side, in the order of the tabs.
    std::vector<Mode> modes() const {
        std::vector<Mode> out;
        for (Mode m : kModeOrder)
            if (gDock.toolsRight[int(m)] == (side == 1)) out.push_back(m);
        return out;
    }
    int tabAt(Point p) const {
        for (size_t k = 0; k < tabs.size(); ++k)
            if (tabs[k].r.contains(p)) return int(k);
        return -1;
    }
    void paint(Canvas& c) override {
        const Rect rc{0, 0, width(), height()};
        // The tab under the mouse now is hot (the host knows where the mouse is).
        const Point at = host() ? host()->mouse(*this) : Point{-1, -1};
        c.fill(rc, kBg);
        tabs.clear();
        const std::vector<Mode> ms = modes();
        static const UiIcon icons[kModeCount] = {UiIcon::Terrain, UiIcon::Units, UiIcon::Doodads, UiIcon::Locations, UiIcon::DatSprites};
        int widest = 0;
        for (Mode m : ms) widest = std::max<int>(widest, c.measure(kModeNames[int(m)]));
        const int n = std::max<int>(1, int(ms.size()));
        const int tw = rc.right / n, is = scale(16), gap = scale(5);
        const bool names = is + gap + widest + scale(12) <= tw;
        for (size_t k = 0; k < ms.size(); ++k) {
            const Mode m = ms[k];
            const Rect r{int(k) * tw, 0, k + 1 == ms.size() ? rc.right : int(k + 1) * tw, rc.bottom};
            // The tab of the active mode is the selected one; the panel that shows while another mode is active (Locations
            // has none) gets only a quiet line under it.
            const bool on = gPanelTab[side] == m, active = appState().mode == m, hot = r.contains(at);
            if (active) c.fill(r, kSel);
            else if (hot) c.fill(r, kRowHot);
            if (active) c.fill({r.left, r.bottom - scale(2), r.right, r.bottom}, kAccent);
            else if (on) c.fill({r.left, r.bottom - scale(1), r.right, r.bottom}, kBorderStrong);
            const Color col = active ? kAccent : on || hot ? kText : kDim;
            const int w = names ? is + gap + c.measure(kModeNames[int(m)]) : is, x0 = (r.left + r.right - w) / 2;
            c.icon({x0, r.top, x0 + is, r.bottom}, int(icons[int(m)]), col, is);
            if (names) c.text({x0 + is + gap, r.top, r.right, r.bottom}, kModeNames[int(m)], col, stardraft::draw::kMiddle);
            tabs.push_back({r, m});
        }
        c.fill({0, rc.bottom - 1, rc.right, rc.bottom}, kBorder);
    }
    bool mouse(const MouseEvent& e) override {
        switch (e.kind) {
        case MouseKind::Move: {
            const int k = tabAt(e.pt);
            if (k != over) over = k, invalidate();
            return true;
        }
        case MouseKind::Leave: over = -1, invalidate(); return true;
        case MouseKind::Down:
        case MouseKind::DoubleClick: {  // (the class has CS_DBLCLKS: a second click comes as a double click)
            if (e.button != 0) return false;
            const int k = tabAt(e.pt);
            if (k >= 0) {
                const Mode m = tabs[size_t(k)].m;
                setMode(m);
                shellFocusView();
            }
            return true;
        }
        case MouseKind::Up: {  // right-click: the tab's mode moves to the other side (its menu)
            if (e.button != 1) return false;
            const std::vector<Mode> ms = modes();
            if (ms.empty()) return true;
            const int tw = std::max(1, width() / int(ms.size()));
            const Mode m = ms[size_t(std::clamp<int>(e.pt.x / tw, 0, int(ms.size()) - 1))];
            const int pick = shellTabSideMenu(std::wstring(kModeNames[int(m)]) + L" panel", side, int(m));
            if (pick == 3) return shellFloatPalette(m, !paletteFloats(m)), true;
            if (pick == 4) return shellNewPalettePanel(int(m)), true;
            if (pick == 1 || pick == 2) {
                gDock.toolsRight[int(m)] = pick == 2;
                gLastOnSide[pick == 2 ? 1 : 0] = m;
                saveDock(), shellRelayout();
                setMessage(std::wstring(kModeNames[int(m)]) + L" panel on the " + (pick == 2 ? L"right" : L"left"));
            }
            return true;
        }
        case MouseKind::Wheel: {  // the next (or the one before) of this side's tabs, round again at the ends
            if (e.horizontal) return false;
            const std::vector<Mode> ms = modes();
            if (ms.empty()) return true;
            int k = int(std::find(ms.begin(), ms.end(), gPanelTab[side]) - ms.begin());
            const int n = int(ms.size());
            k = ((k + (e.wheel > 0 ? -1 : 1)) % n + n) % n;
            setMode(ms[size_t(k)]);
            return true;
        }
        default: return false;
        }
    }
    Cursor cursor(Point p) override { return tabAt(p) >= 0 ? Cursor::Hand : Cursor::Arrow; }
    std::wstring tip(Point p) override {
        const int k = tabAt(p);
        if (k < 0) return {};
        return std::wstring(kModeNames[int(tabs[size_t(k)].m)]) + L". Right-click: move it to the other side.";
    }
    bool hoverTip() const override { return true; }
};
