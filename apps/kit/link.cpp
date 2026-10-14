#include "link.h"

#include <algorithm>

namespace stardraft::kit {

void Link::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const bool lit = hot_ || hasFocus();
    const Color col = tone_ == Tone::Quiet ? (lit ? th.text : th.dim) : toneText(tone_);
    const draw::Rect r{0, 0, width(), height()};
    c.text(r, text_, col, draw::kMiddle | draw::kEllipsis);
    if (!lit || tone_ == Tone::Quiet) return;
    // The line under the text: at the text's baseline, one pixel below it.
    const draw::FontMetrics tm = c.metrics();
    const int base = (r.top + r.bottom + tm.height) / 2 - tm.descent + px(1);
    const int w = std::min(width(), c.measure(text_));
    c.line(0, base, w, base, toneText(tone_));
}

bool Link::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    const bool inside = e.pt.x >= 0 && e.pt.y >= 0 && e.pt.x < width() && e.pt.y < height();
    switch (e.kind) {
    case MouseKind::Move:
        if (inside != hot_) hot_ = inside, invalidate();
        return true;
    case MouseKind::Leave:
        if (hot_ && !down_) hot_ = false, invalidate();
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
        if (e.button != 0) return false;
        down_ = true, hot_ = true;
        if (auto* h = host()) h->capture(*this, true);
        invalidate();
        return true;
    case MouseKind::Up:
        if (e.button != 0 || !down_) return false;
        down_ = false;
        if (auto* h = host()) h->capture(*this, false);
        hot_ = inside;
        invalidate();
        if (inside && onClick) onClick();
        return true;
    default: return false;
    }
}

bool Link::key(const draw::KeyEvent& e) {
    if (!e.down || (e.key != draw::Key::Space && e.key != draw::Key::Enter)) return false;
    if (onClick) onClick();
    return true;
}

}  // namespace stardraft::kit
