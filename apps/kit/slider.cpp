#include "slider.h"

#include <algorithm>
#include <cmath>

namespace stardraft::kit {

namespace {
constexpr int kKnobR = 7;   // the knob's radius at 100 %
constexpr int kTrackH = 2;  // the track's half height at 100 %
}  // namespace

Slider::Slider(int lo, int hi, int v, int step) {
    minValue_ = lo;
    maxValue_ = std::max(lo, hi);
    step_ = std::max(1, step);
    value_ = snap(v);
}

int Slider::preferredHeight() { return px(2 * kKnobR + 2); }

void Slider::setRange(int lo, int hi) {
    minValue_ = lo;
    maxValue_ = std::max(lo, hi);
    setInternal(snap(value_), false);
}

void Slider::setStep(int step) {
    step_ = std::max(1, step);
    setInternal(snap(value_), false);
}

// The arithmetic is in 64 bits: the range may span all of int, and a sum or difference of two ints may not fit in one.
int Slider::snap(long long v) const {
    if (v <= minValue_) return minValue_;
    if (v >= maxValue_) return maxValue_;
    const long long k = (v - minValue_ + step_ / 2) / step_;  // the nearest step (v is above the minimum here)
    return int(std::min<long long>(maxValue_, minValue_ + k * step_));
}

// The grid is the minimum plus whole steps, and the maximum always ends it. From a value that is off the grid, the
// step goes to the grid point next to it (the top of a range 0..10 in steps of 3 goes to 9, then 6).
int Slider::stepDown(int v) const {
    if (v <= minValue_) return minValue_;
    const long long k = (static_cast<long long>(v) - minValue_ - 1) / step_;  // the grid points below v
    return int(std::max<long long>(minValue_, minValue_ + k * step_));
}

int Slider::stepUp(int v) const {
    if (v >= maxValue_) return maxValue_;
    const long long k = (static_cast<long long>(v) - minValue_) / step_ + 1;  // the grid points up to v, and one more
    return int(std::min<long long>(maxValue_, minValue_ + k * step_));
}

int Slider::trackLeft() const { return px(kKnobR); }
int Slider::trackRight() const { return std::max(trackLeft(), width() - px(kKnobR)); }

int Slider::valueAt(int x) const {
    const int a = trackLeft(), b = trackRight();
    if (b <= a || maxValue_ <= minValue_) return minValue_;
    const double f = std::clamp(double(x - a) / double(b - a), 0.0, 1.0);
    const double span = double(maxValue_) - double(minValue_);
    return snap(static_cast<long long>(std::llround(double(minValue_) + f * span)));
}

int Slider::pageStep() const {
    const long long tenth = (static_cast<long long>(maxValue_) - minValue_) / 10;
    return int(std::max<long long>(step_, tenth));
}

float Slider::fraction() const {
    if (maxValue_ <= minValue_) return 0.0f;
    const double span = double(maxValue_) - double(minValue_);
    return float((double(value_) - double(minValue_)) / span);
}

void Slider::setInternal(int v, bool notify) {
    if (v == value_) return;
    value_ = v;
    invalidate();
    if (notify && onChange) onChange(v);
}

void Slider::captureLost() {
    if (!dragging_) return;
    dragging_ = false;
    hot_ = false;
    invalidate();
}

void Slider::paint(draw::Canvas& c) {
    const Theme& th = theme();
    const int cy = height() / 2, a = trackLeft(), b = trackRight(), th2 = px(kTrackH), kr = px(kKnobR);
    const draw::Rect track{a, cy - th2, b, cy + th2};
    c.roundBox(track, th.ctl, th.border, px(2));
    const int kx = a + int(std::lround(fraction() * float(b - a)));
    c.fill({a, cy - th2, kx, cy + th2}, enabled_ ? th.accent : th.faint);
    const bool lit = enabled_ && (hot_ || dragging_ || hasFocus());
    const Color knob = !enabled_ ? th.faint : lit ? th.accentBright : th.accent;
    c.smoothRoundOutline({kx - kr, cy - kr, kx + kr, cy + kr}, float(kr), knob, 2.0f);
}

bool Slider::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    const bool inside = e.pt.x >= 0 && e.pt.y >= 0 && e.pt.x < width() && e.pt.y < height();
    switch (e.kind) {
    case MouseKind::Move:
        if (dragging_) {
            setInternal(valueAt(e.pt.x), true);
            return true;
        }
        if (inside != hot_) hot_ = inside, invalidate();
        return true;
    case MouseKind::Leave:
        if (hot_ && !dragging_) hot_ = false, invalidate();
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
        if (e.button != 0) return false;
        if (!enabled_) return true;
        dragging_ = true;
        if (auto* h = host()) h->capture(*this, true);
        setInternal(valueAt(e.pt.x), true);
        invalidate();
        return true;
    case MouseKind::Up:
        if (e.button != 0 || !dragging_) return false;
        dragging_ = false;
        if (auto* h = host()) h->capture(*this, false);
        hot_ = inside;
        invalidate();
        return true;
    case MouseKind::Wheel: {
        // Fractions add up across events (a touchpad sends tenths of a notch): a step each whole notch.
        if (!enabled_ || e.horizontal || !std::isfinite(e.wheel) || e.wheel == 0) return false;
        if ((e.wheel > 0) != (wheelAcc_ > 0)) wheelAcc_ = 0;  // a turn the other way starts again
        wheelAcc_ += e.wheel;
        const float whole = std::trunc(wheelAcc_);
        wheelAcc_ -= whole;
        const int n = int(std::min(std::fabs(whole), 1000.0f));
        for (int i = 0; i < n; ++i) setInternal(whole > 0 ? stepUp(value_) : stepDown(value_), true);
        return true;
    }
    default: return false;
    }
}

bool Slider::key(const draw::KeyEvent& e) {
    if (!e.down || !enabled_) return false;
    switch (e.key) {
    case draw::Key::Left:
    case draw::Key::Down: setInternal(stepDown(value_), true); return true;
    case draw::Key::Right:
    case draw::Key::Up: setInternal(stepUp(value_), true); return true;
    case draw::Key::PageDown: setInternal(snap(static_cast<long long>(value_) - pageStep()), true); return true;
    case draw::Key::PageUp: setInternal(snap(static_cast<long long>(value_) + pageStep()), true); return true;
    case draw::Key::Home: setInternal(minValue_, true); return true;
    case draw::Key::End: setInternal(maxValue_, true); return true;
    default: return false;
    }
}

}  // namespace stardraft::kit
