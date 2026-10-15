#include "icon_button.h"

#include <algorithm>

namespace stardraft::kit {

namespace {
constexpr int kIconPx = 16;      // a UI icon, at 100 %
constexpr int kBareGlyph = 12;   // a glyph beside fields and in rows (kGlyph)
constexpr int kBoxedGlyph = 14;  // a glyph in a button's box
}  // namespace

int IconButton::preferredSize() { return px(kIconBox); }

// The largest square that fits `r`, centered: every icon button is square, whatever place it gets.
static draw::Rect squareIn(draw::Rect r) {
    const int w = r.width(), h = r.height(), s = std::min(w, h);
    const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
    return {x, y, x + s, y + s};
}

// `size` is at 100 %; the glyph's pixel size is scaled here, once.
void IconButton::drawGlyph(draw::Canvas& c, draw::Rect r, draw::Color col, int size) const {
    if (glyph_.icon >= 0) return c.icon(r, glyph_.icon, col, px(kIconPx));
    if (glyph_.glyph == 0) return;
    draw::FontDesc d;
    d.face = draw::Face::Glyphs;
    d.px = px(size);
    const draw::Font of = c.setFont(c.font(d));
    const wchar_t g[1] = {glyph_.glyph};
    c.text(r, std::wstring_view(g, 1), col, draw::kMiddle | draw::kCenter);
    c.setFont(of);
}

void IconButton::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const draw::Rect r = squareIn({0, 0, width(), height()});
    const bool over = enabled_ && hot_;
    const bool focused = enabled_ && hasFocus();
    const int rad = px(kRadius);
    if (style_ == IconStyle::Face) {  // a button's box (Button's colors and states); the icon in its text color
        Color fill = th.ctl, line = th.border;
        if (on_) fill = th.sel, line = th.accentLine;
        if (enabled_ && hot_) fill = on_ ? th.selHot : th.ctlHot, line = on_ ? th.accent : th.borderHot;
        if (enabled_ && hot_ && down_) fill = on_ ? th.sel : th.ctlDown, line = on_ ? th.accentLine : th.border;  // the pressed face keeps the resting outline
        if (!enabled_) fill = th.bg, line = th.border;
        c.roundBox(r, fill, line, rad);
        drawGlyph(c, r, !enabled_ ? th.faint : on_ ? draw::rgb(255, 255, 255) : th.text, kBoxedGlyph);  // the on face's icon is pure white (as the old Primary button)
        return;
    }
    if (style_ == IconStyle::Boxed) {  // flat: a fill only when on (the accent) or under the mouse
        if (on_) c.roundBox(r, th.sel, th.accentLine, rad);
        else if (over) c.roundBox(r, th.ctlHot, draw::kNone, rad);
        Color col = !enabled_ ? th.faint : on_ ? th.accentBright : tone_ == Tone::Danger ? th.bad : over ? th.text : th.dim;
        drawGlyph(c, r, col, kBoxedGlyph);
    } else {
        if (over) c.roundBox(r, th.ctlHot, draw::kNone, rad);
        const Color rest = tone_ == Tone::Quiet && on_ ? th.warn : on_ || tone_ == Tone::Primary ? th.accent : th.dim;
        const Color lit = tone_ == Tone::Danger ? th.bad : tone_ == Tone::Quiet && on_ ? th.warn : tone_ == Tone::Primary || on_ ? th.accentBright : th.text;
        drawGlyph(c, r, !enabled_ ? th.faint : over || focused ? lit : rest, kBareGlyph);
    }
    if (focused) c.roundBox(r, draw::kNone, th.accent, rad);
}

bool IconButton::mouse(const draw::MouseEvent& e) {
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
        if (!enabled_) return true;
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

bool IconButton::key(const draw::KeyEvent& e) {
    if (!e.down || !enabled_ || (e.key != draw::Key::Space && e.key != draw::Key::Enter)) return false;
    if (onClick) onClick();
    return true;
}

}  // namespace stardraft::kit
