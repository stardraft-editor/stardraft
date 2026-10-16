#include "scroll_view.h"

#include <algorithm>
#include <cmath>

namespace stardraft::kit {

using draw::Canvas;
using draw::Rect;

namespace {

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, std::max(lo, hi))); }

bool same(const Rect& a, const Rect& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

// Sets `v`'s frame only when it changes (a frame change invalidates the view).
void place(draw::View& v, Rect r) {
    if (!same(v.frame(), r)) v.setFrame(r);
}

// Where `a`..`b` (a part of the content, `off` into a view `view` long) goes when the view shows it.
int bringIntoView(int off, int a, int b, int view) {
    if (b - a > view) return a;  // taller than the view: its top
    if (a < off) return a;
    if (b > off + view) return b - view;
    return off;
}

// The thumb along a bar `len` long: [a, b), for a content `total` long of which `view` is shown at `off`.
struct Span {
    int a, b;
};
Span thumbSpan(int len, int view, int total, int off) {
    const int inset = px(2), usable = std::max(0, len - 2 * inset);
    if (total <= view || usable <= 0) return {inset, inset + usable};
    const int minLen = std::min(px(24), usable);
    const int tl = clampInt(int((long long)usable * view / total), minLen, usable);
    const int maxOff = total - view;
    const int a = inset + int((long long)(usable - tl) * clampInt(off, 0, maxOff) / maxOff);
    return {a, a + tl};
}

}  // namespace

// The viewport: no painting of its own; it clips what is inside it. Events it does not use go to the view.
class ScrollView::Port : public draw::View {};

// A bar. Drawn as ui_kit.inc's scrollFace: a thumb in thumb (thumbHot under the mouse or while dragged), at rest only
// half as thick, on the strip's panel when lit.
class ScrollView::Bar : public draw::View {
public:
    Bar(ScrollView& owner, bool vertical) : owner_(owner), vertical_(vertical) {}

    void paint(Canvas& c) override {
        const Theme& th = theme();
        const bool lit = hot_ || drag_;
        const Rect track{0, 0, width(), height()};
        const Span s = span();
        const int inset = px(3), thick = px(6);
        Rect thumb = vertical_ ? Rect{track.right - inset - thick, s.a, track.right - inset, s.b}
                               : Rect{s.a, track.bottom - inset - thick, s.b, track.bottom - inset};
        if (lit) c.fill(track, th.panel);
        else if (vertical_) thumb.left = thumb.right - std::max(2, thick / 2);
        else thumb.top = thumb.bottom - std::max(2, thick / 2);
        c.roundBox(thumb, lit ? th.thumbHot : th.thumb, draw::kNone, px(3));
    }

    bool mouse(const draw::MouseEvent& e) override {
        using draw::MouseKind;
        const bool inside = e.pt.x >= 0 && e.pt.y >= 0 && e.pt.x < width() && e.pt.y < height();
        const int p = along(e.pt);
        switch (e.kind) {
        case MouseKind::Move:
            if (drag_) {
                // The thumb follows the mouse: its start moves by the same amount as the mouse.
                const int inset = px(2), usable = std::max(0, len() - 2 * inset), tl = span().b - span().a;
                const int maxOff = axisTotal() - owner_.axisView(vertical_);
                if (usable > tl && maxOff > 0) {
                    const long long num = (long long)(p - grab_ - inset) * maxOff;
                    owner_.setAxis(vertical_, int((num + (num >= 0 ? (usable - tl) / 2 : -(usable - tl) / 2)) / (usable - tl)));
                }
            } else if (inside != hot_) {
                hot_ = inside;
                invalidate();
            }
            return true;
        case MouseKind::Leave:
            if (hot_ && !drag_) hot_ = false, invalidate();
            return true;
        case MouseKind::Down:
        case MouseKind::DoubleClick: {
            if (e.button != 0) return false;
            owner_.refresh();
            if (!scrollable()) return false;
            const Span s = span();
            if (p >= s.a && p < s.b) {  // on the thumb: drag it
                drag_ = hot_ = true, grab_ = p - s.a;
                setCapture(true);
                invalidate();
            } else {  // on the strip: a page towards the mouse
                owner_.scrollPage(vertical_, p < s.a ? -1 : 1);
            }
            return true;
        }
        case MouseKind::Up:
            if (e.button != 0 || !drag_) return false;
            drag_ = false;
            setCapture(false);
            hot_ = inside;
            invalidate();
            return true;
        default: return false;  // the wheel goes on to the view
        }
    }

private:
    int len() const { return vertical_ ? height() : width(); }
    int along(draw::Point p) const { return vertical_ ? p.y : p.x; }
    int axisTotal() const { return owner_.axisTotal(vertical_); }
    bool scrollable() const { return axisTotal() > owner_.axisView(vertical_); }
    Span span() const { return thumbSpan(len(), owner_.axisView(vertical_), axisTotal(), owner_.axisOffset(vertical_)); }
    void setCapture(bool on) {
        if (auto* h = host()) h->capture(*this, on);
    }

    ScrollView& owner_;
    bool vertical_;
    bool hot_ = false, drag_ = false;
    int grab_ = 0;  // where on the thumb the drag began, from its start
};

ScrollView::ScrollView() : port_(std::make_unique<Port>()), vBar_(std::make_unique<Bar>(*this, true)), hBar_(std::make_unique<Bar>(*this, false)) {
    add(*port_, {});
    add(*vBar_, {});
    add(*hBar_, {});
    vBar_->setVisible(false);
    hBar_->setVisible(false);
}

ScrollView::~ScrollView() = default;

void ScrollView::setContent(draw::View* c) {
    if (content_ == c) return;
    if (content_) port_->remove(*content_);
    content_ = c;
    if (c) port_->add(*c, {0, 0, c->width(), c->height()});  // add() sets the frame: keep the content's size
    refresh();
}

void ScrollView::setHorizontal(bool on) {
    horizontal_ = on;
    refresh();
}

int ScrollView::axisTotal(bool vertical) const {
    if (!content_) return 0;
    return vertical ? content_->height() : content_->width();
}

bool ScrollView::hasVerticalBar() const { return vBar_->visible(); }
bool ScrollView::hasHorizontalBar() const { return hBar_->visible(); }

int ScrollView::maxOffsetX() const { return std::max(0, axisTotal(false) - vw_); }
int ScrollView::maxOffsetY() const { return std::max(0, axisTotal(true) - vh_); }

void ScrollView::setOffset(int x, int y) {
    refresh();
    offX_ = clampInt(x, 0, maxOffsetX());
    offY_ = clampInt(y, 0, maxOffsetY());
    refresh();
}

void ScrollView::setAxis(bool vertical, int off) {
    if (vertical) setOffset(offX_, off);
    else setOffset(off, offY_);
}

void ScrollView::scrollBy(int dx, int dy) { setOffset(offX_ + dx, offY_ + dy); }

void ScrollView::scrollPage(bool vertical, int dir) { setAxis(vertical, axisOffset(vertical) + dir * axisView(vertical)); }

void ScrollView::scrollTo(Rect r) {
    refresh();
    setOffset(horizontal_ ? bringIntoView(offX_, r.left, r.right, vw_) : 0, bringIntoView(offY_, r.top, r.bottom, vh_));
}

void ScrollView::scrollTo(const draw::View& v) {
    refresh();
    if (!content_ || !content_->isAncestorOf(v)) return;
    const draw::Point o = v.origin(), co = content_->origin();
    scrollTo(Rect{o.x - co.x, o.y - co.y, o.x - co.x + v.width(), o.y - co.y + v.height()});
}

void ScrollView::refresh() {
    const int W = width(), H = height(), bw = px(kScrollBarW);
    if (!content_) {
        vw_ = W, vh_ = H, offX_ = offY_ = 0;
        place(*port_, {0, 0, W, H});
        vBar_->setVisible(false), hBar_->setVisible(false);
        return;
    }
    // The bars take room from each other: a vertical bar makes the viewport narrower, which can make the content
    // taller, which can need the vertical bar. Settles in a few passes.
    bool needV = false, needH = false;
    for (int pass = 0; pass < 4; ++pass) {
        const int vw = std::max(0, W - (needV ? bw : 0)), vh = std::max(0, H - (needH ? bw : 0));
        if (!horizontal_ && content_->width() != vw) content_->setSize(vw, content_->height());
        const bool v = content_->height() > vh, h = horizontal_ && content_->width() > vw;
        if (v == needV && h == needH) break;
        needV = v, needH = h;
    }
    vw_ = std::max(0, W - (needV ? bw : 0));
    vh_ = std::max(0, H - (needH ? bw : 0));
    if (!horizontal_ && content_->width() != vw_) content_->setSize(vw_, content_->height());
    offX_ = clampInt(offX_, 0, maxOffsetX());
    offY_ = clampInt(offY_, 0, maxOffsetY());
    place(*port_, {0, 0, vw_, vh_});
    place(*content_, {-offX_, -offY_, -offX_ + content_->width(), -offY_ + content_->height()});
    place(*vBar_, {vw_, 0, W, vh_});
    place(*hBar_, {0, vh_, vw_, H});
    if (vBar_->visible() != needV) vBar_->setVisible(needV);
    if (hBar_->visible() != needH) hBar_->setVisible(needH);
}

void ScrollView::paint(Canvas&) { refresh(); }

void ScrollView::paintOver(Canvas& c) {
    if (hasFocus() && focusable()) c.frame({0, 0, vw_, vh_}, theme().accent);
}

bool ScrollView::focusable() const {
    if (!content_) return true;
    std::vector<draw::View*> chain;
    content_->focusChain(chain);
    return chain.empty();
}

bool ScrollView::mouse(const draw::MouseEvent& e) {
    if (e.kind != draw::MouseKind::Wheel) return false;
    refresh();
    const long n = std::lround(double(e.wheel) * px(kRowH) * kWheelRows);
    const int step = int(n);
    const bool shift = (e.mods & draw::kShift) != 0;
    // A notch: up (positive) goes towards the top. Shift, or a content with no vertical room, turns it into a
    // horizontal scroll; a horizontal wheel goes right when positive.
    int dx = 0, dy = 0;
    if (e.exact) dx = -int(e.panX), dy = -int(e.panY);  // the system's exact distance on both axes (macOS's trackpad)
    else if (e.horizontal) dx = step;
    else if (shift || maxOffsetY() == 0) dx = -step;
    else dy = -step;
    const int bx = offX_, by = offY_;
    scrollBy(dx, dy);
    return offX_ != bx || offY_ != by;  // at the end: the view above may scroll (nested views)
}

bool ScrollView::key(const draw::KeyEvent& e) {
    using draw::Key;
    if (!e.down || (e.mods & (draw::kCtrl | draw::kAlt))) return false;
    refresh();
    const int line = px(kRowH), page = std::max(line, vh_ - line);
    const int bx = offX_, by = offY_;
    switch (e.key) {
    case Key::Up: scrollBy(0, -line); break;
    case Key::Down: scrollBy(0, line); break;
    case Key::PageUp: scrollBy(0, -page); break;
    case Key::PageDown: scrollBy(0, page); break;
    case Key::Home: setOffset(offX_, 0); break;
    case Key::End: setOffset(offX_, maxOffsetY()); break;
    case Key::Left: if (!horizontal_) return false; scrollBy(-line, 0); break;
    case Key::Right: if (!horizontal_) return false; scrollBy(line, 0); break;
    default: return false;
    }
    return offX_ != bx || offY_ != by;
}

}  // namespace stardraft::kit
