// A painted surface in a view (portable, core.inc): the kit's controls painted on the Canvas in one pass, each with its
// click area, as ui_kit.inc's Surface does for a Win32 window. A view keeps a Surface, calls begin() before its paint,
// paints with the functions below, and gives the Surface its mouse events, its cursor and its tips. Under the mouse:
// the part the mouse is on (a new part repaints the view). A part with a drag takes the mouse until it goes up.
// The keyboard (item_focus.inc): the parts with a click are the view's Tab stops, in the order they are added (add them
// in the order of the screen). The view gives the Surface its keys, its tabbedTo and tabWithin, and calls paintFocus
// after its paint: Enter and Space click the part with the focus, Left and Right move the focus, an outline shows it.

namespace kitui {

using kitpaint::Ico;
using State = kitpaint::ButtonState;
using kitpaint::Tone;

struct Hit {
    draw::Rect r;
    std::function<void(draw::Point)> drag;  // while the mouse is down and moves (and at the press, when there is no click)
    std::function<void()> click;            // a click without a drag
    std::wstring tip;
    draw::Cursor cursor = draw::Cursor::Hand;
    std::function<void()> release;  // after a drag, when the mouse goes up
};

// A list being reordered (reorderRow): which list, the row taken and where it goes now.
struct Reorder {
    std::string list;
    int from = -1, to = -1;
};

class Surface {
public:
    std::vector<Hit> hits;
    Reorder reorder;
    std::map<std::string, std::vector<draw::Rect>> reorderRows;  // this paint's rows of each list

    ItemFocus focus;  // the part with the keyboard focus: an index into stops()

    void begin() { hits.clear(); }
    void add(Hit h) { hits.push_back(std::move(h)); }
    // Whether the mouse is in `r` (as the view saw it last).
    bool hot(draw::Rect r) const { return in_ && r.contains(mouse_); }

    // The Tab stops: the hits with a click, in their order (as the last paint added them).
    std::vector<const Hit*> stops() const {
        std::vector<const Hit*> out;
        for (const Hit& h : hits)
            if (h.click && !h.r.empty()) out.push_back(&h);
        return out;
    }
    int stopCount() const { return int(stops().size()); }
    // The part with the focus (null: none).
    const Hit* focused() const {
        const auto s = stops();
        const int i = focus.index(int(s.size()));
        return i >= 0 ? s[size_t(i)] : nullptr;
    }
    // Tab gave `v` the focus: its first part (back: its last).
    void tabbedTo(draw::View& v, bool back) {
        focus.enter(back, stopCount());
        v.invalidate();
    }
    // Tab in `v`: its next part (back: the one before). false: none (the focus leaves the view).
    bool tabWithin(draw::View& v, bool back) {
        if (!focus.step(back, stopCount())) return false;
        v.invalidate();
        return true;
    }
    // The keys of `v` while it has the focus: Enter and Space click the part with the focus; Left and Right move it.
    bool key(draw::View& v, const draw::KeyEvent& e) {
        if (pressKey(e)) {
            const Hit* h = focused();
            if (!h) return false;
            const std::function<void()> click = h->click;  // (the click may repaint, which clears the hits)
            focus.keys = true;
            pressFromKeys(click);
            v.invalidate();
            return true;
        }
        if (e.down && !e.mods && (e.key == draw::Key::Left || e.key == draw::Key::Right) && focused()) {
            focus.move(e.key == draw::Key::Left ? -1 : 1, stopCount());
            v.invalidate();
            return true;
        }
        return false;
    }
    // After the view's paint: the outline on the part with the focus (when the focus came from the keyboard).
    void paintFocus(draw::Canvas& c, const draw::View& v) const {
        if (!focus.keys || !v.hasFocus())
            return;
        if (const Hit* h = focused()) paintFocusOutline(c, h->r);
    }

    // The view's mouse events: true when used. `v` is the view (its host takes the capture during a drag).
    bool mouse(draw::View& v, const draw::MouseEvent& e) {
        switch (e.kind) {
        case draw::MouseKind::Move: {
            mouse_ = e.pt, in_ = true;
            if (grab_.down && grab_.drag) {
                grab_.moved = true, grab_.drag(e.pt);
                v.invalidate();
                return true;
            }
            draw::Rect part{};
            for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                if (it->r.contains(e.pt)) {
                    part = it->r;
                    break;
                }
            if (part.left != over_.left || part.top != over_.top || part.right != over_.right || part.bottom != over_.bottom) over_ = part, v.invalidate();
            return true;
        }
        case draw::MouseKind::Leave:
            in_ = false, over_ = {};
            v.invalidate();
            return true;
        case draw::MouseKind::Down:
        case draw::MouseKind::DoubleClick: {
            if (e.button != 0) return false;
            mouse_ = e.pt, in_ = true;
            v.focus();
            const Hit* h = at(e.pt);
            focus.pressed();  // (no outline after a click; Tab goes on from the part clicked)
            const auto s = stops();
            for (size_t i = 0; i < s.size(); ++i)
                if (s[i] == h) focus.at = int(i);
            if (h && h->drag) {  // a drag: until the mouse goes up (a click if it does not move)
                grab_.drag = h->drag, grab_.click = h->click, grab_.release = h->release, grab_.down = true, grab_.moved = false;
                if (v.host()) v.host()->capture(v, true);
                if (!grab_.click) grab_.drag(e.pt);
            } else if (h && h->click) {
                auto click = h->click;  // the click may repaint, which clears the hits
                click();
            }
            return true;
        }
        case draw::MouseKind::Up: {
            if (e.button != 0 || !grab_.down) return false;
            if (v.host()) v.host()->capture(v, false);
            auto g = std::move(grab_);
            grab_ = {};
            if (g.moved && g.release) g.release();
            else if (!g.moved && g.click) g.click();
            v.invalidate();
            return true;
        }
        default: return false;
        }
    }
    draw::Cursor cursor(draw::Point pt) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->r.contains(pt)) return it->cursor;
        return draw::Cursor::Arrow;
    }
    // The tip at `pt`: the first part with a tip that has the point (as a tooltip control finds its tool).
    std::wstring tip(draw::Point pt) const {
        for (const Hit& h : hits)
            if (!h.tip.empty() && h.r.contains(pt)) return h.tip;
        return {};
    }

private:
    const Hit* at(draw::Point pt) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->r.contains(pt) && (it->click || it->drag)) return &*it;
        return nullptr;
    }
    draw::Point mouse_{};
    bool in_ = false;
    draw::Rect over_{};
    struct {
        std::function<void(draw::Point)> drag;
        std::function<void()> click, release;
        bool down = false, moved = false;
    } grab_;
};

// --- The controls (each as ui_kit.inc's function of the same name) --------------------------------------------------

inline draw::Cursor usable(bool enabled) { return enabled ? draw::Cursor::Hand : draw::Cursor::Arrow; }

inline void button(draw::Canvas& c, Surface& s, draw::Rect r, const std::wstring& text, Tone tone, std::function<void()> click,
                   const std::wstring& tip = L"", bool enabled = true) {
    kitpaint::drawButtonFace(c, r, text, tone, !enabled ? State::Disabled : s.hot(r) ? State::Hot : State::Normal);
    s.add({r, nullptr, enabled ? std::move(click) : nullptr, tip, usable(enabled)});
}
inline int buttonWidth(draw::Canvas& c, const std::wstring& text) { return std::max(scale(stardraft::kit::kButtonMinW), c.measure(text) + scale(28)); }
// A row of buttons from the right edge (OK, Cancel: the first is the rightmost but one). Returns the left edge.
struct ButtonSpec {
    std::wstring text;
    Tone tone = Tone::Normal;
    std::function<void()> click;
    std::wstring tip;
};
inline int buttonsRight(draw::Canvas& c, Surface& s, int right, int y, const std::vector<ButtonSpec>& bs) {
    int x = right;
    for (auto it = bs.rbegin(); it != bs.rend(); ++it) {
        const int w = buttonWidth(c, it->text);
        button(c, s, {x - w, y, x, y + scale(stardraft::kit::kButtonH)}, it->text, it->tone, it->click, it->tip);
        x -= w + scale(stardraft::kit::kButtonGap);
    }
    return x + scale(stardraft::kit::kButtonGap);
}
// A small button: an action in a row, as wide as its text, in the middle of the row. Returns the right edge.
inline int smallButton(draw::Canvas& c, Surface& s, int x, int y, int h, const std::wstring& text, std::function<void()> click,
                       const std::wstring& tip = L"", Tone tone = Tone::Normal, bool enabled = true) {
    const int bh = std::min(h, scale(24)), w = c.measure(text) + scale(20);
    const draw::Rect r{x, y + (h - bh) / 2, x + w, y + (h - bh) / 2 + bh};
    const Tone t = tone == Tone::Primary ? Tone::Normal : tone;  // a small button is never the main action
    kitpaint::drawButtonFace(c, r, text, t, !enabled ? State::Disabled : s.hot(r) ? State::Hot : State::Normal);
    s.add({r, nullptr, enabled ? std::move(click) : nullptr, tip, usable(enabled)});
    return r.right;
}
// "Add ..." under a list. Returns the bottom. Not enabled: gray, no click; the tooltip says why.
inline int addRow(draw::Canvas& c, Surface& s, draw::Rect r, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"",
                  bool enabled = true) {
    kitpaint::drawAddRowFace(c, r, text, enabled && s.hot(r), enabled);
    s.add({r, nullptr, enabled ? std::move(click) : nullptr, tip, usable(enabled)});
    return r.bottom;
}
// A link: only to another entity; underlined under the mouse. Returns the right edge.
inline int link(draw::Canvas& c, Surface& s, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"",
                Tone tone = Tone::Primary) {
    const draw::Rect r{x, y, x + c.measure(text), y + h};
    kitpaint::drawLinkFace(c, r, text, tone, s.hot(r));
    s.add({r, nullptr, std::move(click), tip, draw::Cursor::Hand});
    return r.right;
}

// Bare: a glyph that lights up under the mouse. Boxed: a glyph in a button box; `on` marks a toggle that is on.
enum class Icon { Bare, Boxed };
inline void iconButton(draw::Canvas& c, Surface& s, draw::Rect r, Ico g, std::function<void()> click, const std::wstring& tip, Icon style = Icon::Bare,
                       bool on = false, bool enabled = true, Tone tone = Tone::Normal) {
    r = kitpaint::square(r);
    const bool over = enabled && s.hot(r);
    if (style == Icon::Boxed) {  // flat: a fill only when on (the accent) or under the mouse
        const float radius = float(scale(stardraft::kit::kRadius));
        if (on) c.roundBox(r, kSel, kAccentLine, radius);
        else if (over) c.roundBox(r, kCtlHot, draw::kNone, radius);
        kitpaint::drawGlyph(c, r, g, !enabled ? kFaint : on ? kAccentBright : tone == Tone::Danger ? kBad : over ? kText : kDim, 14);
    } else {
        kitpaint::drawIconFace(c, r, g, tone, on, over, enabled);
    }
    s.add({r, nullptr, enabled ? std::move(click) : nullptr, tip, usable(enabled)});
}

// The actions at a row's right edge, shown on the row under the mouse (or when `show`). Returns their left edge.
struct RowAction {
    Ico glyph = L"";
    std::wstring tip;
    std::function<void()> click;
    Tone tone = Tone::Normal;
    bool on = false;
    bool enabled = true;
};
inline int rowActions(draw::Canvas& c, Surface& s, draw::Rect row, const std::vector<RowAction>& actions, bool show) {
    s.add({row, nullptr, nullptr, L"", draw::Cursor::Arrow});  // the mouse entering the row repaints it
    const int w = scale(stardraft::kit::kIconBox), h = std::min<int>(row.bottom - row.top, scale(stardraft::kit::kIconBox));
    int x = row.right - scale(4) - w * int(actions.size());
    const int left = x;
    if (!show && !s.hot(row)) return left;
    const int y = (row.top + row.bottom - h) / 2;
    for (const auto& a : actions) {
        iconButton(c, s, {x, y, x + w, y + h}, a.glyph, a.click, a.tip, Icon::Bare, a.on, a.enabled, a.tone);
        x += w;
    }
    return left;
}
// The remove action of a row, shown on the row under the mouse.
inline void removeCross(draw::Canvas& c, Surface& s, draw::Rect row, draw::Rect at, std::function<void()> click, const std::wstring& tip) {
    s.add({row, nullptr, nullptr, L"", draw::Cursor::Arrow});
    if (!s.hot(row)) return;
    iconButton(c, s, at, UiIcon::Close, std::move(click), tip, Icon::Bare, false, true, Tone::Danger);
}

// A row of a list that can be reordered (`index` from 0, in order): its grip at the left, shown on the row under the
// mouse; dragging the grip moves the row, and `moved(from, to)` gets where it was and where it goes (an index in the
// list without it). The caller paints the row first.
inline void reorderRow(draw::Canvas& c, Surface& s, const std::string& list, draw::Rect row, int index, std::function<void(int from, int to)> moved,
                       bool can = true) {
    auto& rows = s.reorderRows[list];
    if (index == 0) rows.clear();
    rows.push_back(row);
    const bool dragging = s.reorder.list == list && s.reorder.from >= 0;
    if (dragging && s.reorder.from == index) c.fill(row, kSel, 160);
    const draw::Rect g{row.left, row.top, row.left + scale(18), row.bottom};
    if (can && (s.hot(row) || (dragging && s.reorder.from == index)))
        kitpaint::drawGrip(c, row.left + scale(5), (row.top + row.bottom) / 2 - scale(6), dragging && s.reorder.from == index);
    if (!can) return;
    Surface* sp = &s;
    s.add({g,
           [sp, list, index](draw::Point pt) {
               const auto& rs = sp->reorderRows[list];
               int to = int(rs.size());
               for (size_t i = 0; i < rs.size(); ++i)
                   if (pt.y < (rs[i].top + rs[i].bottom) / 2) {
                       to = int(i);
                       break;
                   }
               sp->reorder = {list, index, to};
           },
           nullptr, L"Drag to move", draw::Cursor::SizeAll,
           [sp, moved] {
               const Reorder r = std::exchange(sp->reorder, {});
               if (r.from < 0 || r.to == r.from || r.to == r.from + 1) return;
               moved(r.from, r.to > r.from ? r.to - 1 : r.to);
           }});
}
// After the list's rows: the line where the dragged row goes.
inline void reorderDropLine(draw::Canvas& c, Surface& s, const std::string& list) {
    if (s.reorder.list != list || s.reorder.from < 0) return;
    const auto& rs = s.reorderRows[list];
    if (rs.empty()) return;
    const int y = s.reorder.to < int(rs.size()) ? rs[size_t(s.reorder.to)].top : rs.back().bottom;
    kitpaint::drawDropLine(c, rs.front().left, rs.front().right, y);
}

// A check box with its label; the whole is the click area. Returns the right edge.
inline int check(draw::Canvas& c, Surface& s, draw::Rect r, const std::wstring& label, bool on, std::function<void()> click, const std::wstring& tip = L"",
                 bool changed = false, bool enabled = true) {
    const int right = kitpaint::drawCheck(c, r, label, on, enabled && s.hot(r), changed, enabled);
    s.add({r, nullptr, enabled ? std::move(click) : nullptr, tip, usable(enabled)});
    return right;
}

// A dropdown: icon, name, the number on the right, the chevron; the glyph that opens the entry when `open` is set.
inline void pickBox(draw::Canvas& c, Surface& s, draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click,
                    const std::wstring& tip = L"", draw::Image icon = {}, int number = -1, std::function<void()> open = nullptr, bool enabled = true) {
    const bool over = enabled && s.hot(r);
    kitpaint::drawPickFace(c, r, text, changed, over, icon, number, enabled);
    if (enabled) s.add({r, nullptr, [r, click] { click(r); }, tip, draw::Cursor::Hand});
    else s.add({r, nullptr, nullptr, tip, draw::Cursor::No});
    if (open)
        iconButton(c, s, {r.right + scale(4), r.top, r.right + scale(4 + stardraft::kit::kIconBox), r.bottom}, UiIcon::OpenExternal, std::move(open), L"Open it",
                   Icon::Bare, false, true, Tone::Primary);
}

// A title: big text, and a line under it in the dim color; "Reset" after it when changed and `reset` is given.
inline int title(draw::Canvas& c, Surface& s, int x, int y, int right, draw::Image icon, const std::wstring& text, const std::wstring& sub, bool changed = false,
                 std::function<void()> reset = nullptr) {
    const kitpaint::TitleLayout t = kitpaint::drawTitle(c, x, y, right, icon, false, UiIcon{}, text, sub, changed);
    if (t.hasLine && changed && reset)
        link(c, s, t.line.left + c.measure(t.lineText), t.line.top, t.line.bottom - t.line.top, L"Reset", std::move(reset), L"Put back the game's values");
    return t.next;
}

}  // namespace kitui
