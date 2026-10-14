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
constexpr int kMinSqueezedW = 40;  // when the tabs do not all fit, each is this wide at least
constexpr int kDragSlop = 6;    // the mouse moves this far before a tab is dragged
}  // namespace

void TabStrip::setTabs(std::vector<Tab> tabs) {
    tabs_ = std::move(tabs);
    sel_ = tabs_.empty() ? -1 : std::clamp(sel_, 0, count() - 1);
    hot_ = -1;
    hotClose_ = false;
    dirty_ = true;
    invalidate();
}

void TabStrip::setChanged(int i, bool changed) {
    if (i < 0 || i >= count() || tabs_[size_t(i)].changed == changed) return;
    tabs_[size_t(i)].changed = changed;
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

// Each tab's width. Segmented: equal parts of the strip. Tabs: as wide as the text needs (within a range); when they do
// not all fit, each is the same narrower width.
void TabStrip::measureWidths(draw::Canvas& c) {
    const int n = count(), w = width();
    widths_.assign(size_t(n), 0);
    if (n == 0) return;
    if (look_ == TabLook::Segmented) {
        for (int i = 0; i < n; ++i) widths_[size_t(i)] = w / n;
        widths_.back() += w - (w / n) * n;
        return;
    }
    int total = 0;
    for (int i = 0; i < n; ++i) {
        const Tab& t = tabs_[size_t(i)];
        const int text = t.text.empty() ? 0 : c.measure(t.text);
        const int natural = text + px(kPad) * 3 + px(kCloseW) + (t.icon >= 0 ? px(kIconPx) + px(kIconGap) : 0);
        widths_[size_t(i)] = std::clamp(natural, px(kMinTabW), px(kMaxTabW));
        total += widths_[size_t(i)];
    }
    if (total > w)
        for (int& tw : widths_) tw = std::max(px(kMinSqueezedW), w / n);
}

draw::Rect TabStrip::tabRect(int i) const {
    if (i < 0 || size_t(i) >= widths_.size() || widths_.size() != tabs_.size()) return {};
    int x = 0;
    for (int k = 0; k < i; ++k) x += widths_[size_t(k)];
    return {x, 0, x + widths_[size_t(i)], height()};
}

draw::Rect TabStrip::closeRect(int i) const {
    const draw::Rect r = tabRect(i);
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

void TabStrip::paint(draw::Canvas& c) {
    if (dirty_) measureWidths(c), dirty_ = false;
    const Theme& th = theme();
    const int n = count(), w = width(), h = height(), rad = px(kRadius);
    if (n == 0) return;
    if (look_ == TabLook::Segmented) c.roundBox({0, 0, w, h}, th.ctl, th.border, rad);
    else c.fill({0, h - px(1), w, h}, th.border);  // the line under the flat tabs (their bars cover it)
    for (int i = 0; i < n; ++i) {
        const draw::Rect r = tabRect(i);
        const bool sel = i == sel_, hot = i == hot_;
        if (look_ == TabLook::Segmented) {
            // Each segment's box is wider on its inner sides; the clip cuts it at the segment, so only the ends round.
            draw::Rect ext = r;
            if (i > 0) ext.left -= px(8);
            if (i + 1 < n) ext.right += px(8);
            c.pushClip(r);
            if (sel) c.roundBox(ext, th.sel, th.accentLine, rad);
            else if (hot) c.roundBox(ext, th.ctlHot, th.border, rad);
            c.popClip();
            if (i + 1 < n) c.fill({r.right - 1, r.top + 1, r.right, r.bottom - 1}, th.border);
        } else {
            if (sel) c.fill(r, th.ctl);
            else if (hot) c.fill(r, th.hot);
            if (sel) c.fill({r.left, r.bottom - px(2), r.right, r.bottom}, th.accent);
        }
        const Color col = look_ == TabLook::Segmented ? (sel ? th.accent : th.text) : (sel || hot ? th.text : th.dim);
        drawTab(c, i, r, col);
    }
    if (hasFocus() && sel_ >= 0) c.frame(tabRect(sel_), th.accent);
}

// The icon, the text and the x (or the dot) of tab `i` in its rectangle `r`.
void TabStrip::drawTab(draw::Canvas& c, int i, const draw::Rect& r, draw::Color col) const {
    const Theme& th = theme();
    const Tab& t = tabs_[size_t(i)];
    const int h = height(), is = px(kIconPx), gap = px(kIconGap), pad = px(kPad), rad = px(kRadius);
    const draw::Rect cr = closeRect(i);
    const int right = cr.left - px(2);  // the text ends before the x
    const int top = (h - is) / 2;
    if (look_ == TabLook::Segmented) {
        // The icon and the name together in the middle; the name is cut when it does not fit; without room for it, the
        // icon alone.
        const int tw = t.text.empty() ? 0 : c.measure(t.text);
        const int left = r.left + pad;
        const bool names = !t.text.empty() && (t.icon < 0 || is + gap + tw <= right - left);
        if (t.icon >= 0) {
            const int cw = is + (names ? gap + tw : 0);
            const int x0 = (r.left + right) / 2 - cw / 2;
            c.icon({x0, top, x0 + is, top + is}, t.icon, col, is);
            if (names) c.text({x0 + is + gap, 0, right, h}, t.text, col, draw::kLeft | draw::kMiddle | draw::kEllipsis);
        } else if (names) {
            c.text({left, 0, right, h}, t.text, col, draw::kCenter | draw::kMiddle | draw::kEllipsis);
        }
    } else {
        int x = r.left + pad;
        if (t.icon >= 0) {
            c.icon({x, top, x + is, top + is}, t.icon, col, is);
            x += is + gap;
        }
        if (!t.text.empty()) c.text({x, 0, right, h}, t.text, col, draw::kLeft | draw::kMiddle | draw::kEllipsis);
    }
    // The unsaved dot (while the mouse is not on the tab), else the x of a closable tab.
    const bool sel = i == sel_;
    const bool overTab = i == hot_;
    if (t.changed && !overTab) {
        const int d = px(8), cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
        c.ellipse({cx - d / 2, cy - d / 2, cx + d / 2 + 1, cy + d / 2 + 1}, sel ? th.text : th.dim, draw::kNone);
    } else if (t.closable) {
        const bool overClose = overTab && hotClose_;
        if (overClose) c.roundBox(cr, th.hot, draw::kNone, rad);
        c.text(cr, L"×", overClose ? th.text : th.dim, draw::kMiddle | draw::kCenter);
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
        if (dirty_) return;  // the owner changed the tabs in onMove
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
        setCapture(true);
        if (pressedClose_) {
            invalidate();
            return true;
        }
        drag_ = h.index;
        dragging_ = false;
        dragX_ = e.pt.x;
        selectByUser(h.index);
        return true;
    }
    case MouseKind::Up: {
        if (e.button != 0 || pressed_ < 0) return false;
        const int p = pressed_;
        const bool wasClose = pressedClose_;
        pressed_ = -1, pressedClose_ = false, drag_ = -1, dragging_ = false;
        setCapture(false);
        if (wasClose) {
            const Hit h = hitAt(e.pt);
            if (h.index == p && h.close && onClose) onClose(p);
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
