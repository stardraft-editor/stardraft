#pragma once
// A progress bar (docs/dev/CONTROLS.md, "Progress"): a thin track, filled in the accent up to the value (0 to 1). Busy:
// a segment runs along the track for work whose length is not known. The bar animates with a host timer while it is
// busy and shown; the timer stops when the bar is not busy, leaves its window, or is destroyed.

#include <memory>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

class ProgressBar : public draw::View {
public:
    ProgressBar() = default;
    ~ProgressBar() override;
    ProgressBar(const ProgressBar&) = delete;
    ProgressBar& operator=(const ProgressBar&) = delete;

    // The part done, 0 to 1 (clamped). Setting a value does not end busy mode.
    void setValue(float v);
    float value() const { return value_; }
    // Busy: the segment runs along the track instead of the fill.
    void setBusy(bool busy);
    bool busy() const { return busy_; }

    void paint(draw::Canvas& c) override;

private:
    void startTimer();
    void stopTimer();
    void tick();

    float value_ = 0.0f;
    float phase_ = 0.0f;  // where the busy segment is, 0 to 1 (its left end runs from off the track to off the other end)
    bool busy_ = false;
    int timer_ = 0;       // the host's timer for the next animation step (0: none)
    // Dies with the bar: a timer that outlives the bar finds the token gone and does nothing.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace stardraft::kit
