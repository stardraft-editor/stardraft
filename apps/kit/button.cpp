#include "button.h"

#include <algorithm>

namespace stardraft::kit {

Color toneText(Tone t) {
    const Theme& th = theme();
    return t == Tone::Primary ? th.accent : t == Tone::Danger ? th.bad : t == Tone::Quiet ? th.dim : th.text;
}

int Button::preferredWidth(draw::Canvas& c) const { return std::max(px(kButtonMinW), c.measure(text_) + px(28)); }

void Button::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const draw::Rect r{0, 0, width(), height()};
    Color fill = th.ctl, line = th.border;
    if (tone_ == Tone::Primary) fill = th.sel, line = th.accentLine;
    if (enabled_ && hot_) fill = tone_ == Tone::Primary ? th.selHot : th.ctlHot, line = tone_ == Tone::Primary ? th.accent : th.borderHot;
    if (enabled_ && hot_ && down_) fill = tone_ == Tone::Primary ? th.sel : th.ctlDown;
    if (!enabled_) fill = th.bg, line = th.border;
    if (hasFocus() && enabled_) line = th.accent;
    c.roundBox(r, fill, line, px(kRadius));
    c.text(r, text_, !enabled_ ? th.faint : toneText(tone_ == Tone::Quiet ? Tone::Normal : tone_), draw::kMiddle | draw::kCenter | draw::kEllipsis);
}

bool Button::mouse(const draw::MouseEvent& e) {
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
        if (e.button != 0 || !enabled_) return enabled_;
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
        if (inside && enabled_ && onClick) onClick();
        return true;
    default: return false;
    }
}

bool Button::key(const draw::KeyEvent& e) {
    if (!e.down || !enabled_ || (e.key != draw::Key::Space && e.key != draw::Key::Enter)) return false;
    if (onClick) onClick();
    return true;
}

}  // namespace stardraft::kit
