#pragma once
// A slider for a whole number in a range (docs/dev/CONTROLS.md, "Sliders"): a track with the part up to the value in
// the accent and a round knob. Drag, or click to jump. The arrow keys move by a step, PageUp and PageDown by a tenth
// of the range, Home and End go to the ends; the wheel moves by a step. The value is snapped to the step (the ends
// are always reached).

#include <functional>
#include <string>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

class Slider : public draw::View {
public:
    Slider() = default;
    // The value `v` in the range lo..hi, moved by `step` (at least 1).
    Slider(int lo, int hi, int v, int step = 1);

    // Called with the new value after the user changed it (not for setValue, setRange or setStep).
    std::function<void(int)> onChange;

    int value() const { return value_; }
    int minValue() const { return minValue_; }
    int maxValue() const { return maxValue_; }
    int step() const { return step_; }
    // Sets the value, snapped to the step and kept in the range.
    void setValue(int v) { setInternal(snap(v), false); }
    // The range (hi below lo: hi becomes lo). The value is kept in it.
    void setRange(int lo, int hi);
    // At least 1.
    void setStep(int step);
    void setEnabled(bool e) {
        enabled_ = e;
        invalidate();
    }
    bool enabled() const { return enabled_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }

    // The height a slider needs (the knob with its room).
    static int preferredHeight();

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return draw::Cursor::Arrow; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return enabled_; }

private:
    int snap(int v) const;       // into the range, to the step
    int valueAt(int x) const;    // the value under x (the view's coordinates)
    int pageStep() const;        // PageUp and PageDown
    float fraction() const;      // the value's place on the track, 0 to 1
    int trackLeft() const;       // the knob's centers at the ends of the track
    int trackRight() const;
    void setInternal(int v, bool notify);

    int minValue_ = 0, maxValue_ = 100, step_ = 1, value_ = 0;
    bool enabled_ = true, hot_ = false, dragging_ = false;
    std::wstring tip_;
};

}  // namespace stardraft::kit
