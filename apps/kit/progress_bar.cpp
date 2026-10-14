#include "progress_bar.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace stardraft::kit {

namespace {
constexpr int kTrackH = 4;        // at 100 %
constexpr int kFrameMs = 50;      // one animation step
constexpr float kStep = 0.02f;    // the segment's move per step: about 2.5 s across the track
constexpr float kSegment = 0.3f;  // the segment's length, of the track
}  // namespace

ProgressBar::~ProgressBar() { stopTimer(); }

void ProgressBar::setValue(float v) {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if (c == value_) return;
    value_ = c;
    invalidate();
}

void ProgressBar::setBusy(bool busy) {
    if (busy == busy_) return;
    busy_ = busy;
    if (busy_) startTimer();
    else stopTimer();
    invalidate();
}

void ProgressBar::startTimer() {
    auto* h = host();
    if (!h || timer_ != 0) return;
    const std::weak_ptr<int> alive = alive_;
    timer_ = h->timer(kFrameMs, [this, alive] {
        if (alive.lock()) tick();
    });
}

void ProgressBar::stopTimer() {
    if (timer_ == 0) return;
    if (auto* h = host()) h->cancelTimer(timer_);
    timer_ = 0;
}

void ProgressBar::tick() {
    timer_ = 0;  // this step has run
    if (!busy_) return;
    phase_ += kStep;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    invalidate();
    startTimer();  // the next step (not while the bar is off the window)
}

void ProgressBar::paint(draw::Canvas& c) {
    const Theme& th = theme();
    if (busy_ && timer_ == 0) startTimer();  // shown while busy: the animation starts (or resumes after a move)
    const int w = width(), h = height(), th2 = px(kTrackH) / 2, cy = h / 2;
    if (w <= 0) return;
    const draw::Rect track{0, cy - th2, w, cy - th2 + px(kTrackH)};
    c.roundBox(track, th.ctl, th.border, px(2));
    if (busy_) {
        const int seg = int(std::lround(kSegment * float(w)));
        const int x0 = int(std::lround((float(w) + float(seg)) * phase_)) - seg;
        c.pushClip(track);
        c.roundBox({x0, track.top, x0 + seg, track.bottom}, th.accent, draw::kNone, px(2));
        c.popClip();
    } else if (value_ > 0.0f) {
        const int fw = std::max(1, int(std::lround(value_ * float(w))));
        c.roundBox({0, track.top, std::min(w, fw), track.bottom}, th.accent, draw::kNone, px(2));
    }
}

}  // namespace stardraft::kit
