#include "badge.h"

#include <algorithm>

namespace stardraft::kit {

namespace {
constexpr int kPadX = 8;  // at 100 %: the text's space to the box's sides
}

int Badge::preferredWidth(draw::Canvas& c) const { return text_.empty() ? 0 : c.measure(text_) + 2 * px(kPadX); }

void Badge::paint(draw::Canvas& c) {
    if (text_.empty()) return;
    const Theme& th = theme();
    Color fill = th.chipDim, line = th.border, text = th.dim;
    if (tone_ == BadgeTone::Good) fill = th.goodFill, line = th.goodLine, text = th.good;
    else if (tone_ == BadgeTone::Warn) line = th.warn, text = th.warn;
    else if (tone_ == BadgeTone::Bad) fill = th.badFill, line = th.bad, text = th.bad;
    const int w = std::min(width(), preferredWidth(c));
    const draw::Rect r = right_ ? draw::Rect{width() - w, 0, width(), height()} : draw::Rect{0, 0, w, height()};
    c.roundBox(r, fill, line, float(px(kRadius)));
    c.text({r.left + px(kPadX), r.top, r.right - px(kPadX), r.bottom}, text_, text, draw::kCenter | draw::kMiddle | draw::kEllipsis);
}

}  // namespace stardraft::kit
