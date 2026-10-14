#include "check_box.h"

#include <algorithm>

namespace stardraft::kit {

namespace {
constexpr int kBoxSize = 14;  // at 100 %
constexpr int kGap = 8;       // between the box and the label
}  // namespace

// --- ChoiceBox ---------------------------------------------------------------------------------------------------

int ChoiceBox::preferredWidth(draw::Canvas& c) const { return px(kBoxSize) + px(kGap) + c.measure(label_); }

void ChoiceBox::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const int s = px(kBoxSize), top = (height() - s) / 2;
    const bool over = enabled_ && hot_;
    drawMark(c, {0, top, s, top + s}, over, enabled_ && hasFocus());
    const Color tc = !enabled_ ? th.faint : changed_ ? th.good : on_ || over ? th.text : th.dim;
    c.text({s + px(kGap), 0, width(), height()}, label_, tc, draw::kMiddle | draw::kEllipsis);
}

bool ChoiceBox::mouse(const draw::MouseEvent& e) {
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
        if (inside && enabled_) activate();  // a release outside is no click
        return true;
    default: return false;
    }
}

bool ChoiceBox::key(const draw::KeyEvent& e) {
    if (!e.down || !enabled_ || e.key != draw::Key::Space) return false;
    activate();
    return true;
}

// --- CheckBox ----------------------------------------------------------------------------------------------------

void CheckBox::activate() {
    on_ = !on_;
    invalidate();
    if (onChange) onChange(on_);
}

void CheckBox::drawMark(draw::Canvas& c, draw::Rect b, bool over, bool focused) const {
    const Theme& th = theme();
    const Color line = focused || on_ ? th.accent : over ? th.borderHot : th.borderStrong;
    c.roundBox(b, on_ ? th.accentFill : th.ctl, line, px(3));
    if (!on_) return;
    const int w = b.width(), h = b.height();
    const draw::Point mark[3] = {{b.left + w * 22 / 100, b.top + h * 52 / 100},
                                 {b.left + w * 42 / 100, b.top + h * 72 / 100},
                                 {b.left + w * 78 / 100, b.top + h * 30 / 100}};
    c.polyline(mark, 3, enabled() ? th.onText : th.dim, std::max(1, px(2)));
}

// --- RadioGroup --------------------------------------------------------------------------------------------------

RadioGroup::~RadioGroup() {
    for (RadioButton* b : buttons_) b->group_ = nullptr;
}

RadioButton* RadioGroup::selected() const {
    for (RadioButton* b : buttons_)
        if (b->on()) return b;
    return nullptr;
}

void RadioGroup::select(RadioButton& chosen) {
    for (RadioButton* b : buttons_) b->setState(b == &chosen);
}

// --- RadioButton -------------------------------------------------------------------------------------------------

RadioButton::RadioButton(std::wstring label, RadioGroup& group, bool on) : ChoiceBox(std::move(label), false), group_(&group) {
    group.buttons_.push_back(this);
    if (on) group.select(*this);
}

RadioButton::~RadioButton() {
    if (!group_) return;
    auto& bs = group_->buttons_;
    bs.erase(std::remove(bs.begin(), bs.end(), this), bs.end());
}

void RadioButton::setState(bool on) {
    if (on_ == on) return;
    on_ = on;
    invalidate();
}

void RadioButton::setOn(bool on) {
    if (on && group_) group_->select(*this);
    else setState(on);
}

void RadioButton::activate() {
    if (on_) return;  // a radio button is not turned off by a click
    if (!group_) return setState(true);
    group_->select(*this);
    if (group_->onChange) group_->onChange(*this);
}

bool RadioButton::key(const draw::KeyEvent& e) {
    if (!e.down || !enabled() || !group_) return ChoiceBox::key(e);
    int step = 0;
    switch (e.key) {
    case draw::Key::Left:
    case draw::Key::Up: step = -1; break;
    case draw::Key::Right:
    case draw::Key::Down: step = 1; break;
    default: return ChoiceBox::key(e);
    }
    const auto& bs = group_->buttons_;
    const auto me = std::find(bs.begin(), bs.end(), this);
    if (me == bs.end()) return true;
    // The next enabled button that way; at the end of the group the key is used and nothing moves.
    for (long i = me - bs.begin() + step; i >= 0 && i < long(bs.size()); i += step)
        if (bs[size_t(i)]->enabled()) {
            bs[size_t(i)]->focus();
            bs[size_t(i)]->activate();
            break;
        }
    return true;
}

void RadioButton::drawMark(draw::Canvas& c, draw::Rect b, bool over, bool focused) const {
    const Theme& th = theme();
    c.ellipse(b, th.ctl, focused || on_ ? th.accent : over ? th.borderHot : th.borderStrong);
    if (!on_) return;
    const int in = b.width() * 3 / 10;
    c.ellipse({b.left + in, b.top + in, b.right - in + 1, b.bottom - in + 1}, th.accent, draw::kNone);
}

}  // namespace stardraft::kit
