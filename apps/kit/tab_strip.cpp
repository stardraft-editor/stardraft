#include "tab_strip.h"

#include <algorithm>
#include <cstdlib>

namespace stardraft::kit {

namespace {
constexpr int kPad = 10;        // at 100 %: the space at a tab's sides
constexpr int kCloseW = 16;     // the x (or the dot's place)
constexpr int kIconPx = 16;
constexpr int kIconGap = 5;
constexpr int kMinTabW = 90;    // a tab's width: its text, within these
constexpr int kMaxTabW = 260;
constexpr int kSegGap = 4;      // between the segments of a Segmented row (CONTROLS.md)
constexpr int kDragSlop = 6;    // the mouse moves this far before a tab is dragged
}  // namespace

void TabStrip::setTabs(std::vector<Tab> tabs) {
    tabs_ = std::move(tabs);
    sel_ = tabs_.empty() ? -1 : std::clamp(sel_, 0, count() - 1);
    hot_ = -1;
    hotClose_ = false;
    // A press or drag was on the old tabs: it ends (the mouse is still held, so the strip keeps it until the release).
    pressed_ = -1;
    pressedClose_ = false;
    drag_ = -1;
    dragging_ = false;
    dirty_ = true;
    invalidate();
}

void TabStrip::setChanged(int i, bool changed) {
    if (i < 0 || i >= count() || tabs_[size_t(i)].changed == changed) return;
    tabs_[size_t(i)].changed = changed;
    dirty_ = true;  // (a tab with no x gets the dot's room, or loses it)
    invalidate();
}

void TabStrip::select(int i) {
    if (i < 0 || i >= count() || i == sel_) return;
    sel_ = i;
    invalidate();
}

void TabStrip::selectByUser(int i) {
    if (i == sel_) return;
    select(i);
    if (onSelect) onSelect(i);
}

// Each tab's width. Segmented: equal segments, a gap of 4 px between them (the gap is counted in the tab's width
// except for the last). Tabs: as wide as the text needs (within a range; with `fitText`, a tab with no x and no dot
// has no room for them and no minimum); when they do not all fit, each is the same narrower width (no scrolling: the
// tabs are narrow, but every one can be clicked).
void TabStrip::measureWidths(draw::Canvas& c) {
    const int n = count(), w = width();
    widths_.assign(size_t(n), 0);
    if (n == 0) return;
    if (look_ == TabLook::Segmented) {
        const int gap = px(kSegGap);
        const int inner = std::max(0, w - gap * (n - 1));
        for (int i = 0; i < n; ++i) widths_[size_t(i)] = inner / n + (i + 1 < n ? gap : 0);
        widths_.back() += inner - (inner / n) * n;
        return;
    }
    int total = 0;
    for (int i = 0; i < n; ++i) {
        const Tab& t = tabs_[size_t(i)];
        const int text = t.text.empty() ? 0 : c.measure(t.text);
        const bool fit = fitText && !t.closable && !t.changed;
        const int natural = text + px(kPad) * 3 + (fit ? 0 : px(kCloseW)) + (t.icon >= 0 ? px(kIconPx) + px(kIconGap) : 0);
        widths_[size_t(i)] = std::clamp(natural, fit ? 0 : px(kMinTabW), px(kMaxTabW));
        total += widths_[size_t(i)];
    }
    if (total > w)
        for (int& tw : widths_) tw = std::max(1, w / n);
}

draw::Rect TabStrip::tabRect(int i) const {
    if (i < 0 || size_t(i) >= widths_.size() || widths_.size() != tabs_.size()) return {};
    int x = 0;
    for (int k = 0; k < i; ++k) x += widths_[size_t(k)];
    return {x, 0, x + widths_[size_t(i)], height()};
}

draw::Rect TabStrip::boxRect(int i) const {
    draw::Rect r = tabRect(i);
    if (look_ == TabLook::Segmented && i + 1 < count()) r.right -= px(kSegGap);
    return r;
}

draw::Rect TabStrip::closeRect(int i) const {
    const draw::Rect r = boxRect(i);
    const int cw = px(kCloseW), top = (height() - cw) / 2;
    const int left = r.right - px(kPad) / 2 - cw;
    return {left, top, left + cw, top + cw};
}

// Which tab is at `p` (the view's coordinates): none until the first paint has measured the tabs.
TabStrip::Hit TabStrip::hitAt(draw::Point p) const {
    if (dirty_ || p.y < 0 || p.y >= height()) return {};
    for (int i = 0; i < count(); ++i) {
        const draw::Rect r = tabRect(i);
        if (p.x >= r.left && p.x < r.right) return {i, closeRect(i).contains(p)};
    }
    return {};
}

void TabStrip::setCapture(bool on) {
    if (auto* h = host()) h->capture(*this, on);
}

// The mouse capture went elsewhere (or the system took it) before the release: the press and the drag end here, so
// plain mouse moves do not drag a tab.
void TabStrip::captureLost() {
    capturing_ = false;
    pressed_ = -1;
    pressedClose_ = false;
    drag_ = -1;
    dragging_ = false;
    invalidate();
}

void TabStrip::paint(draw::Canvas& c) {
    if (dirty_) measureWidths(c), dirty_ = false;
    const Theme& th = theme();
    const int n = count(), w = width(), h = height(), rad = px(kRadius);
    if (n == 0) return;
    if (look_ != TabLook::Segmented) c.fill({0, h - px(1), w, h}, th.border);  // the line under the flat tabs (their bars cover it)
    for (int i = 0; i < n; ++i) {
        const draw::Rect box = boxRect(i);
        const bool sel = i == sel_, hot = i == hot_;
        if (look_ == TabLook::Segmented) {
            // A button: the chosen one Primary, the others as the normal button (hot when the mouse is on it).
            if (sel) c.roundBox(box, th.sel, th.accentLine, rad);
            else if (hot) c.roundBox(box, th.ctlHot, th.borderHot, rad);
            else c.roundBox(box, th.ctl, th.border, rad);
        } else {
            if (sel) c.fill(box, th.ctl);
            else if (hot) c.fill(box, th.hot);
            if (sel) c.fill({box.left, box.bottom - px(2), box.right, box.bottom}, th.accent);
        }
        const Color col = look_ == TabLook::Segmented ? (sel ? th.accent : th.text) : (sel || hot ? th.text : th.dim);
        drawTab(c, i, box, col);
    }
    if (hasFocus() && sel_ >= 0) c.frame(boxRect(sel_), th.accent);
}

// The icon, the text and the x (or the dot) of tab `i` in its box `box`.
void TabStrip::drawTab(draw::Canvas& c, int i, const draw::Rect& box, draw::Color col) const {
    const Theme& th = theme();
    const Tab& t = tabs_[size_t(i)];
    const int h = height(), is = px(kIconPx), gap = px(kIconGap), pad = px(kPad), rad = px(kRadius);
    const draw::Rect cr = closeRect(i);
    const bool closeMark = t.changed || t.closable;
    // A tab without an x or a dot uses its whole box: its icon and text are in the middle (in both looks). A flat tab
    // with an x has them at its left; the text ends before the x's place.
    const int right = closeMark ? cr.left - px(2) : box.right - pad;
    const int top = (h - is) / 2;
    if (look_ == TabLook::Segmented || !closeMark) {
        // The icon and the name together in the middle; the name is cut when it does not fit; without room for it, the
        // icon alone.
        const int tw = t.text.empty() ? 0 : c.measure(t.text);
        const int left = box.left + pad;
        const bool names = !t.text.empty() && (t.icon < 0 || is + gap + tw <= right - left);
        if (t.icon >= 0) {
            const int cw = is + (names ? gap + tw : 0);
            const int x0 = (left + right) / 2 - cw / 2;
            c.icon({x0, top, x0 + is, top + is}, t.icon, col, is);
            if (names) c.text({x0 + is + gap, 0, right, h}, t.text, col, draw::kLeft | draw::kMiddle | draw::kEllipsis);
        } else if (names) {
            c.text({left, 0, right, h}, t.text, col, draw::kCenter | draw::kMiddle | draw::kEllipsis);
        }
    } else {
        int x = box.left + pad;
        if (t.icon >= 0) {
            c.icon({x, top, x + is, top + is}, t.icon, col, is);
            x += is + gap;
        }
        if (!t.text.empty()) c.text({x, 0, right, h}, t.text, col, draw::kLeft | draw::kMiddle | draw::kEllipsis);
    }
    // The unsaved dot (while the mouse is not on the tab), else the x of a closable tab. The x is a glyph written as
    // an escape, so the source's encoding does not matter.
    const bool sel = i == sel_;
    const bool overTab = i == hot_;
    if (t.changed && !overTab) {
        const int d = px(8), cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
        c.ellipse({cx - d / 2, cy - d / 2, cx + d / 2 + 1, cy + d / 2 + 1}, sel ? th.text : th.dim, draw::kNone);
    } else if (t.closable) {
        const bool overClose = overTab && hotClose_;
        if (overClose) c.roundBox(cr, th.hot, draw::kNone, rad);
        c.text(cr, L"\u00d7", overClose ? th.text : th.dim, draw::kMiddle | draw::kCenter);
    }
}

// Moves the dragged tab toward the mouse's x, past each tab whose middle it has passed.
void TabStrip::dragTo(int x) {
    if (drag_ < 0 || drag_ >= count() || dirty_) return;
    if (!dragging_) {
        if (std::abs(x - dragX_) <= px(kDragSlop)) return;
        dragging_ = true;
    }
    int cur = drag_;
    for (;;) {
        int to = -1;
        for (int i = 0; i < count() && to < 0; ++i) {
            if (i == cur) continue;
            const draw::Rect r = tabRect(i);
            const int mid = (r.left + r.right) / 2;
            if (i < cur ? x < mid : x >= mid) to = i;
        }
        if (to < 0 || to == cur) break;
        moveTab(cur, to);
        cur = to;
        if (dirty_) return;  // the owner changed the tabs in onMove (the drag ended with them)
    }
    drag_ = cur;
}

void TabStrip::moveTab(int from, int to) {
    Tab moved = std::move(tabs_[size_t(from)]);
    tabs_.erase(tabs_.begin() + from);
    tabs_.insert(tabs_.begin() + to, std::move(moved));
    if (widths_.size() == tabs_.size()) {  // (not measured since the last change of the tabs: nothing to move)
        const int w = widths_[size_t(from)];
        widths_.erase(widths_.begin() + from);
        widths_.insert(widths_.begin() + to, w);
    }
    // The selection stays on its tab.
    if (sel_ == from) sel_ = to;
    else if (from < sel_ && to >= sel_) --sel_;
    else if (from > sel_ && to <= sel_) ++sel_;
    hot_ = -1;
    hotClose_ = false;
    invalidate();
    if (onMove) onMove(from, to);
}

bool TabStrip::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: {
        if (drag_ >= 0) {
            if (!pressedClose_) dragTo(e.pt.x);
            return true;
        }
        const Hit h = hitAt(e.pt);
        if (h.index != hot_ || h.close != hotClose_) {
            hot_ = h.index;
            hotClose_ = h.close;
            invalidate();
        }
        return true;
    }
    case MouseKind::Leave:
        if (hot_ >= 0 && drag_ < 0) hot_ = -1, hotClose_ = false, invalidate();
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick: {
        const Hit h = hitAt(e.pt);
        if (e.button == 2) {  // a middle click closes a closable tab
            if (h.index >= 0 && tabs_[size_t(h.index)].closable && onClose) onClose(h.index);
            return h.index >= 0;
        }
        if (e.button != 0 || h.index < 0) return false;
        pressed_ = h.index;
        pressedClose_ = h.close && tabs_[size_t(h.index)].closable;
        capturing_ = true;
        setCapture(true);
        if (pressedClose_) {
            invalidate();
            return true;
        }
        drag_ = movable ? h.index : -1;
        dragging_ = false;
        dragX_ = e.pt.x;
        selectByUser(h.index);  // (the owner may change the tabs here: nothing of the strip is used after it)
        return true;
    }
    case MouseKind::Up: {
        if (e.button != 0 || !capturing_) return false;
        const int p = pressed_;
        const bool wasClose = pressedClose_;
        capturing_ = false;
        pressed_ = -1, pressedClose_ = false, drag_ = -1, dragging_ = false;
        setCapture(false);
        if (wasClose) {
            const Hit h = hitAt(e.pt);
            if (h.index == p && h.close && onClose) {
                const std::weak_ptr<int> alive = alive_;
                onClose(p);
                if (alive.expired()) return true;  // the owner destroyed the strip
            }
        }
        invalidate();
        return true;
    }
    default: return false;
    }
}

bool TabStrip::key(const draw::KeyEvent& e) {
    if (!e.down || count() == 0) return false;
    switch (e.key) {
    case draw::Key::Left:
        if (sel_ > 0) selectByUser(sel_ - 1);
        return true;
    case draw::Key::Right:
        if (sel_ + 1 < count()) selectByUser(sel_ + 1);
        return true;
    case draw::Key::Home: selectByUser(0); return true;
    case draw::Key::End: selectByUser(count() - 1); return true;
    default: return false;
    }
}

draw::Cursor TabStrip::cursor(draw::Point p) { return hitAt(p).index >= 0 ? draw::Cursor::Hand : draw::Cursor::Arrow; }

std::wstring TabStrip::tip(draw::Point p) {
    const Hit h = hitAt(p);
    return h.index >= 0 ? tabs_[size_t(h.index)].tip : std::wstring{};
}

}  // namespace stardraft::kit
