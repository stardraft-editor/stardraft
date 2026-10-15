// The hover tooltip (portable, core.inc): a view's tip() or a painted part's tip, painted on the Canvas as the program's
// tooltips always looked (the native ones, which tipPaintProc painted): the control color, the text in the text color
// in the UI font, wrapped at 360 px, 9 px from the sides and 6 px from the top and bottom. The window it is in is a
// Tooltip window (Host::openWindow: no title, never active, the pop-ups' rounded corners and quiet border). The Windows
// host is tip_win32.inc (the delays, the place under the mouse).

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
