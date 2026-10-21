// The keyboard focus on the parts that a view paints itself (portable, core.inc): the top bar's buttons, the modes, the
// workspace rail, the document tabs, the side bar's tabs, the tool strip's buttons, the palette's items. Tab and
// Shift+Tab go from part to part before the focus leaves the view (View::tabWithin), Enter and Space press the part with
// the focus, and an accent outline shows it while the focus came from the keyboard (after a click, no outline shows, as
// on the kit's controls). Esc gives the keyboard back to the map (sdl_main_view.inc: MainView::key).

void shellFocusView();

// A press from the keyboard runs now: a part's click that gives the keyboard to the map (shellFocusView) keeps it on
// the part, so that Tab goes on from there.
inline int& keyPressDepth() {
    static int depth = 0;
    return depth;
}
inline bool keyPressRuns() { return keyPressDepth() > 0; }
// Runs `press` (a part's click) as a press from the keyboard.
inline void pressFromKeys(const std::function<void()>& press) {
    if (!press) return;
    ++keyPressDepth();
    press();
    --keyPressDepth();
}

// The outline of the part with the keyboard focus: a line in the accent, 2 px wide, with the kit's corners.
inline void paintFocusOutline(draw::Canvas& c, draw::Rect r) {
    c.roundBox(r, draw::kNone, kAccent, float(scale(stardraft::kit::kRadius)), std::max(1, scale(2)));
}

// Which part of a view has the keyboard focus: an index into the view's parts (in the order of the screen).
struct ItemFocus {
    int at = -1;        // the part with the focus (-1: none)
    bool keys = false;  // the focus came from the keyboard: the outline shows

    // Tab came into the view: its first part (back: its last).
    void enter(bool back, int count) {
        at = count > 0 ? (back ? count - 1 : 0) : -1;
        keys = true;
    }
    // Tab in the view: the next part (back: the part before). false: there is none (the focus leaves the view).
    bool step(bool back, int count) {
        if (at < 0 || at >= count) return false;
        const int next = at + (back ? -1 : 1);
        if (next < 0 || next >= count) return false;
        at = next, keys = true;
        return true;
    }
    // An arrow key: `delta` parts on, not past the ends. false: the focus did not move.
    bool move(int delta, int count) {
        if (count <= 0) return false;
        const int next = std::clamp((at < 0 ? 0 : at) + delta, 0, count - 1);
        if (next == at) return false;
        at = next, keys = true;
        return true;
    }
    // The part with the focus (-1: none, or no longer there).
    int index(int count) const { return at >= 0 && at < count ? at : -1; }
    // The outline shows on the part: the view has the focus from the keyboard.
    bool shows(const draw::View& v, int count) const { return keys && v.hasFocus() && index(count) >= 0; }
    // A press of the mouse on the view: no outline until the keyboard moves the focus again.
    void pressed() { keys = false; }
};

// Whether the key presses the part with the focus (Enter or Space, no modifier key).
inline bool pressKey(const draw::KeyEvent& e) {
    return e.down && !e.repeat && !(e.mods & (draw::kCtrl | draw::kAlt | draw::kSuper)) && (e.key == draw::Key::Enter || e.key == draw::Key::Space);
}

// A Tab stop on a part that a view paints itself among its kit controls (a link in a text, a row of color swatches): a
// child view in the part's place that paints only the focus outline. Enter and Space run `press`; Left and Right run
// `step` (-1 or 1: a choice in a row). The mouse, the cursor and the tip are the parent's (a click goes through).
class PartStop final : public draw::View {
public:
    std::function<void()> press;
    std::function<void(int)> step;
    std::function<draw::Rect()> outline;  // where the outline goes, in the stop's coordinates (none: the whole stop)
    std::wstring name;                    // for the tests' output (View::focusedItem)

    bool focusable() const override { return press || step; }
    bool focusOnClick() const override { return false; }
    void focusChanged(bool) override { invalidate(); }
    void tabbedTo(bool) override { keys_ = true, invalidate(); }
    void paint(draw::Canvas& c) override {
        if (keys_ && hasFocus()) paintFocusOutline(c, outline ? outline() : draw::Rect{0, 0, width(), height()});
    }
    bool key(const draw::KeyEvent& e) override {
        if (pressKey(e) && press) return keys_ = true, pressFromKeys(press), true;
        if (e.down && !e.mods && step && (e.key == draw::Key::Left || e.key == draw::Key::Right)) {
            step(e.key == draw::Key::Left ? -1 : 1);
            keys_ = true;
            invalidate();
            return true;
        }
        return false;
    }
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down) keys_ = false;
        return false;
    }
    draw::Cursor cursor(draw::Point p) override { return parent() ? parent()->cursor(toParent(p)) : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point p) override { return parent() ? parent()->tip(toParent(p)) : std::wstring(); }
    std::wstring focusedItem() const override { return name; }

private:
    draw::Point toParent(draw::Point p) const { return {p.x + frame().left, p.y + frame().top}; }
    bool keys_ = false;  // the focus came from Tab: the outline shows
};

// A view's children in the order of the screen (the rows top down, by their middles; in a row, left to right): Tab's
// order (View::tabChildren) for a view that places its children in rows where it paints them.
inline std::vector<draw::View*> byScreen(std::vector<draw::View*> v) {
    std::stable_sort(v.begin(), v.end(), [](const draw::View* a, const draw::View* b) {
        const draw::Rect fa = a->frame(), fb = b->frame();
        const int ma = fa.top + fa.bottom, mb = fb.top + fb.bottom;  // (twice the middle)
        return ma != mb ? ma < mb : fa.left < fb.left;
    });
    return v;
}
