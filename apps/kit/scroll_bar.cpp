#include "scroll_bar.h"

#include <algorithm>

namespace stardraft::kit {

void ScrollBar::setRange(int total, int page) {
    total_ = std::max(0, total);
    page_ = std::max(0, page);
    pos_ = std::clamp(pos_, 0, maxPos());
    invalidate();
}

void ScrollBar::setPos(int pos) {
    const int p = std::clamp(pos, 0, maxPos());
    if (p != pos_) pos_ = p, invalidate();
}

void ScrollBar::moveTo(int pos) {
    const int before = pos_;
    setPos(pos);
    if (pos_ != before && onScroll) onScroll(pos_);
}

draw::Rect ScrollBar::thumb() const {
    if (!scrollable()) return {};
    // The thumb's length is the visible share of the track, but not shorter than the minimum.
    const int track = length();
    const int len = std::min(track, std::max(px(kScrollMinThumb), int((long long)track * page_ / total_)));
    const int travel = track - len;
    const int off = int(((long long)travel * pos_ + maxPos() / 2) / maxPos());
    const int inset = px(kScrollInset);
    if (axis_ == Axis::Vertical) return {inset, off, width() - inset, off + len};
    return {off, inset, off + len, height() - inset};
}

void ScrollBar::paint(draw::Canvas& c) {
    if (!scrollable()) return;
    const Theme& th = theme();
    // A lit bar shows its track (the panel color), then the thumb at full width; at rest, the thumb's far half only.
    draw::Rect t = thumb();
    if (lit()) c.fill({0, 0, width(), height()}, th.panel);
    else if (axis_ == Axis::Vertical) t.left = t.right - std::max(2, t.width() / 2);
    else t.top = t.bottom - std::max(2, t.height() / 2);
    c.roundBox(t, lit() ? th.thumbHot : th.thumb, draw::kNone, px(3));
}

// The thumb follows the mouse: where the thumb's start would be, the position it stands for.
void ScrollBar::drag(draw::Point p) {
    const int track = length();
    const draw::Rect t = thumb();
    const int len = axis_ == Axis::Vertical ? t.height() : t.width();
    const int travel = track - len;
    if (travel <= 0) {
        moveTo(0);
        return;
    }
    const int start = std::clamp(along(p) - grab_, 0, travel);
    moveTo(int(((long long)start * maxPos() + travel / 2) / travel));
}

bool ScrollBar::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    const bool inside = e.pt.x >= 0 && e.pt.y >= 0 && e.pt.x < width() && e.pt.y < height();
    switch (e.kind) {
    case MouseKind::Move:
        if (dragging_) {
            drag(e.pt);
            return true;
        }
        if (inside != hot_) hot_ = inside, invalidate();
        return true;
    case MouseKind::Leave:
        if (hot_ && !dragging_) hot_ = false, invalidate();
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick: {
        if (e.button != 0 || !scrollable()) return false;
        // The thumb is hit along the bar's axis only: its inset from the bar's edges across it is no track.
        const draw::Rect t = thumb();
        const int thumbStart = axis_ == Axis::Vertical ? t.top : t.left;
        const int thumbEnd = axis_ == Axis::Vertical ? t.bottom : t.right;
        const int at = along(e.pt);
        if (at >= thumbStart && at < thumbEnd) {  // take the thumb: it follows the mouse from here
            dragging_ = true;
            grab_ = at - thumbStart;
            if (auto* h = host()) h->capture(*this, true);
            invalidate();
        } else {  // the track: one page toward the click
            const int page = page_ > 0 ? page_ : 1;
            moveTo(at < thumbStart ? pos_ - page : pos_ + page);
        }
        return true;
    }
    case MouseKind::Up:
        if (e.button != 0 || !dragging_) return false;
        dragging_ = false;
        if (auto* h = host()) h->capture(*this, false);
        hot_ = inside;
        invalidate();
        return true;
    default: return false;
    }
}

}  // namespace stardraft::kit
