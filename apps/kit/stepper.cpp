#include "stepper.h"

#include <algorithm>

namespace stardraft::kit {

namespace {
constexpr int kCircle = 26;  // at 100 %: the circle
constexpr int kTextX = 40;   // the name's left edge, from the stepper's left edge
}

int Stepper::stepAt(draw::Point pt) const {
    if (pt.x < 0 || pt.x >= width() || pt.y < 0) return -1;
    const int i = pt.y / px(kStepH);
    return i < int(steps_.size()) ? i : -1;
}

void Stepper::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const int d = px(kCircle);
    for (int i = 0; i < int(steps_.size()); ++i) {
        const int y = i * px(kStepH);
        const draw::Rect circle{0, y, d, y + d};
        if (i + 1 < int(steps_.size()))  // the line to the next circle
            c.fill({d / 2 - px(1), y + d + px(4), d / 2 + px(1), y + px(kStepH) - px(4)}, i < current_ ? th.accentFill : th.border);
        if (i < current_) {
            c.ellipse(circle, i == hot_ ? th.accent : th.accentFill, th.accentFill);
            const draw::Point mark[3] = {{circle.left + d * 28 / 100, circle.top + d * 52 / 100},
                                         {circle.left + d * 44 / 100, circle.top + d * 68 / 100},
                                         {circle.left + d * 74 / 100, circle.top + d * 34 / 100}};
            c.polyline(mark, 3, th.onText, std::max(1, px(2)));
        } else if (i == current_) {
            c.ellipse(circle, th.accentFill, th.accent);
            c.text(circle, std::to_wstring(i + 1), th.onText, draw::kCenter | draw::kMiddle);
        } else {
            c.ellipse(circle, th.panel, th.borderStrong);
            c.text(circle, std::to_wstring(i + 1), th.dim, draw::kCenter | draw::kMiddle);
        }
        const int tx = px(kTextX), lineH = px(20);
        c.text({tx, y - px(2), width(), y - px(2) + lineH}, steps_[size_t(i)].name, i == current_ || i == hot_ ? th.text : th.dim,
               draw::kMiddle | draw::kEllipsis);
        c.text({tx, y + px(17), width(), y + px(17) + lineH}, steps_[size_t(i)].note, th.dim, draw::kMiddle | draw::kEllipsis);
    }
}

bool Stepper::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    const int at = stepAt(e.pt);
    const int clickable = at >= 0 && at < current_ ? at : -1;
    switch (e.kind) {
    case MouseKind::Move:
        if (clickable != hot_) hot_ = clickable, invalidate();
        return true;
    case MouseKind::Leave:
        if (hot_ != -1) hot_ = -1, invalidate();
        return true;
    case MouseKind::Down:
        if (e.button != 0 || clickable < 0) return false;
        down_ = clickable;
        if (auto* h = host()) h->capture(*this, true);
        return true;
    case MouseKind::Up: {
        if (e.button != 0 || down_ < 0) return false;
        const int was = down_;
        down_ = -1;
        if (auto* h = host()) h->capture(*this, false);
        if (clickable == was && onSelect) onSelect(was);
        return true;
    }
    default: return false;
    }
}

draw::Cursor Stepper::cursor(draw::Point pt) {
    const int at = stepAt(pt);
    return at >= 0 && at < current_ ? draw::Cursor::Hand : draw::Cursor::Arrow;
}

}  // namespace stardraft::kit
