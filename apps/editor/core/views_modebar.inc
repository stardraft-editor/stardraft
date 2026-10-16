// The modes' segmented control (portable, core.inc): moved from main.cpp. Its instance (gModeBarView) is made in main.cpp,
// where the top tool bar is; its place in the bar is modeBarRect (main.cpp).

// Defined by the program (main_sdl.cpp and its sdl_*.inc files) until they move into the core.
void setMode(Mode m);

// The box's corner radius: the kit's kRadius (main.cpp's ui namespace).
constexpr int kModeBoxRadius = 4;

// The modes' segmented control: one view in the top tool bar, its segments side by side in the display order
// (kModeOrder) with a line between them. Each segment is part of one box, rounded only at the two ends; the chosen
// mode is primary. A click picks the segment's mode (on the release, while the mouse is still on it, as a button);
// the tip is the mode's name. The icon alone when the names do not fit.
struct ModeBarView final : stardraft::draw::View {
    using Canvas = stardraft::draw::Canvas;
    using Point = stardraft::draw::Point;
    using MouseEvent = stardraft::draw::MouseEvent;
    using MouseKind = stardraft::draw::MouseKind;
    int held = -1;        // the segment (its place in kModeOrder) the mouse is down on
    bool inside = false;  // the mouse is still on that segment
    // The segment under `p` (a place in kModeOrder), -1 outside the bar.
    int segmentAt(Point p) const {
        const int bw = width() / kModeCount;
        if (bw <= 0 || p.x < 0 || p.y < 0 || p.x >= width() || p.y >= height()) return -1;
        return std::min(p.x / bw, kModeCount - 1);
    }
    RECT segmentRect(int k) const {
        const int bw = width() / kModeCount;
        return {k * bw, 0, k + 1 == kModeCount ? width() : (k + 1) * bw, height()};
    }
    void paint(Canvas& c) override {
        const int w = width(), h = height(), bw = width() / kModeCount;
        static const UiIcon icons[kModeCount] = {UiIcon::Terrain, UiIcon::Units, UiIcon::Doodads, UiIcon::Locations, UiIcon::DatSprites};
        c.fill({0, 0, w, h}, kPanel);  // they sit on the top tool bar
        const stardraft::draw::Font prev = c.setFont(c.font({stardraft::draw::Face::Ui, 0}));  // the UI font (the message font)
        const int is = scale(16), gap = scale(5), over = scale(8);
        int widest = 0;  // all five with their names, or all five as icons only
        for (const wchar_t* n : kModeNames) widest = std::max<int>(widest, c.measure(n));
        for (int k = 0; k < kModeCount; ++k) {
            const int i = int(kModeOrder[k]);
            const bool active = i == int(appState().mode);
            const bool down = held == k && inside;
            const RECT r = segmentRect(k);
            c.pushClip(r);
            c.fill(r, kPanel);
            // (The box is drawn wider on the inner sides; the segment's clip cuts it.)
            RECT seg = r;
            if (k > 0) seg.left -= over;
            if (k + 1 < kModeCount) seg.right += over;
            c.roundBox(seg, active ? kSel : down ? kCtlDown : kCtl, active ? kAccentLine : kBorder, float(scale(kModeBoxRadius)));
            if (k + 1 < kModeCount) c.fill({r.right - 1, r.top + 1, r.right, r.bottom - 1}, kBorder);  // the line to the next segment
            // The mode's icon, then its name; the icon alone when the name does not fit.
            const stardraft::draw::Color col = active ? kAccent : kText;
            const int nameW = c.measure(kModeNames[i]);
            const bool showName = is + gap + widest + scale(10) <= r.right - r.left;
            const int x0 = showName ? r.left + (r.right - r.left - (is + gap + nameW)) / 2 : r.left + (r.right - r.left - is) / 2;
            c.icon({x0, r.top, x0 + is, r.bottom}, int(icons[i]), col, is);
            if (showName) c.text({x0 + is + gap, r.top, r.right, r.bottom}, kModeNames[i], col, stardraft::draw::kMiddle);
            c.popClip();
        }
        c.setFont(prev);
    }
    bool mouse(const MouseEvent& e) override {
        switch (e.kind) {
        case MouseKind::Down:
        case MouseKind::DoubleClick:  // (a button takes a fast second press as a press)
            if (e.button != 0) return false;
            focus();  // as the old button: the press takes the focus (a tool-strip number box saves before the mode changes)
            held = segmentAt(e.pt);
            if (held < 0) return false;
            inside = true;
            if (stardraft::draw::Host* h = host()) h->capture(*this, true);
            invalidate();
            return true;
        case MouseKind::Move:
            if (held < 0) return false;
            if ((segmentAt(e.pt) == held) != inside) inside = !inside, invalidate();
            return true;
        case MouseKind::Up: {
            if (e.button != 0 || held < 0) return false;
            const int k = held;
            const bool pick = segmentAt(e.pt) == k;  // released off it: no click
            held = -1, inside = false;
            if (stardraft::draw::Host* h = host()) h->capture(*this, false);
            invalidate();
            if (pick) {
                setMode(Mode(int(kModeOrder[k])));
                shellFocusView();
            }
            return true;
        }
        default: return false;
        }
    }
    void captureLost() override {
        held = -1, inside = false;
        invalidate();
    }
    std::wstring tip(Point p) override {
        const int k = segmentAt(p);
        return k < 0 ? std::wstring() : std::wstring(kModeNames[int(kModeOrder[k])]);
    }
    bool hoverTip() const override { return true; }
};
