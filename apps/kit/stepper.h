#pragma once
// A stepper (docs/dev/CONTROLS.md, "Steppers"): the steps of a dialog that goes step by step, one under the other, each
// a circle with its number, its name and a dim note under it. A step that is done has a check mark in an accent circle,
// the step that shows has its number in an accent circle, a later step its number in an outlined circle; lines join
// the circles. A click on a done step goes back to it (onSelect); the dialog's Back and Next do the same, so the stepper
// takes no keys and no focus.

#include <functional>
#include <string>
#include <vector>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

constexpr int kStepH = 60;  // at 100 %: a step's height

struct Step {
    std::wstring name, note;
};

class Stepper : public draw::View {
public:
    void setSteps(std::vector<Step> s) {
        steps_ = std::move(s);
        invalidate();
    }
    const std::vector<Step>& steps() const { return steps_; }
    // The step that shows; the ones before it are done.
    void setCurrent(int i) {
        if (i == current_) return;
        current_ = i;
        invalidate();
    }
    int current() const { return current_; }
    // Called with a done step that the user clicked.
    std::function<void(int)> onSelect;

    // The step under a point (-1: none).
    int stepAt(draw::Point pt) const;

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override;
    void captureLost() override { down_ = -1; }

private:
    std::vector<Step> steps_;
    int current_ = 0, hot_ = -1, down_ = -1;
};

}  // namespace stardraft::kit
