#include "label.h"

#include <algorithm>

namespace stardraft::kit {

namespace {
constexpr int kIconSize = 16;  // at 100 %, as the icons in the rows
constexpr int kIconGap = 8;    // between the icon and the text
}  // namespace

Color labelColor(LabelTone t) {
    const Theme& th = theme();
    switch (t) {
    case LabelTone::Dim: return th.dim;
    case LabelTone::Faint: return th.faint;
    case LabelTone::Good: return th.good;
    case LabelTone::Warn: return th.warn;
    case LabelTone::Bad: return th.bad;
    case LabelTone::Normal: break;
    }
    return th.text;
}

int Label::textLeft() const { return icon_ >= 0 ? px(kIconSize) + px(kIconGap) : 0; }

int Label::preferredHeight(draw::Canvas& c, int width) const {
    const int tw = std::max(0, width - textLeft());
    const int h = wrap_ ? c.measureBox(text_, tw, draw::kWrap).y : c.measureBox(text_, 0, draw::kMiddle | draw::kEllipsis).y;
    return icon_ >= 0 ? std::max(h, px(kIconSize)) : h;
}

int Label::preferredWidth(draw::Canvas& c) const { return textLeft() + c.measure(text_); }

void Label::paint(draw::Canvas& c) {
    const Color col = labelColor(tone_);
    const int h = height();
    if (icon_ >= 0) {
        const int s = px(kIconSize), top = (h - s) / 2;
        c.icon({0, top, s, top + s}, icon_, col, s);
    }
    const draw::Rect tr{textLeft(), 0, width(), h};
    if (wrap_) c.text(tr, text_, col, draw::kWrap);
    else c.text(tr, text_, col, draw::kMiddle | draw::kEllipsis);
}

}  // namespace stardraft::kit
