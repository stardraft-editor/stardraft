#include <climits>
#include <string>

#include "kit_check.h"
#include "slider.h"

using namespace stardraft;
using namespace stardraft::kit::test;

// A slider 200 px wide on the root, its track from x 7 to 193.

KIT_TEST(slider_wheel_fractions_add_up_to_a_step) {
    draw::View root;
    FakeHost host(root);
    kit::Slider s(0, 100, 50, 1);
    root.add(s, {0, 0, 200, 20});
    // A touchpad sends a quarter notch at a time: four of them are one step, not four.
    host.wheel(100, 10, 0.25f);
    host.wheel(100, 10, 0.25f);
    host.wheel(100, 10, 0.25f);
    CHECK(s.value() == 50);
    host.wheel(100, 10, 0.25f);
    CHECK(s.value() == 51);
    // Down again: the fraction starts over in the other direction.
    host.wheel(100, 10, -0.5f);
    CHECK(s.value() == 51);
    host.wheel(100, 10, -0.5f);
    CHECK(s.value() == 50);
    host.wheel(100, 10, 2.0f);  // whole notches: a step each
    CHECK(s.value() == 52);
}

KIT_TEST(slider_wheel_partial_notch_is_no_step) {
    draw::View root;
    FakeHost host(root);
    kit::Slider s(0, 100, 50, 1);
    root.add(s, {0, 0, 200, 20});
    host.wheel(100, 10, 0.1f);  // one touchpad event of a tenth of a notch
    CHECK(s.value() == 50);
    host.wheel(100, 10, 0.95f);  // the partial part adds up: 1.05 notches make one step
    CHECK(s.value() == 51);
}

KIT_TEST(slider_left_from_off_grid_top_goes_to_the_next_grid_point) {
    draw::View root;
    FakeHost host(root);
    kit::Slider s(0, 10, 10, 3);  // the grid 0, 3, 6, 9 and the maximum 10
    root.add(s, {0, 0, 200, 20});
    CHECK(s.value() == 10);
    s.focus();
    CHECK(host.press(draw::Key::Left) && s.value() == 9);  // not 6
    CHECK(host.press(draw::Key::Left) && s.value() == 6);
    CHECK(host.press(draw::Key::Down) && s.value() == 3);
    CHECK(host.press(draw::Key::Left) && s.value() == 0);
    CHECK(host.press(draw::Key::Left) && s.value() == 0);
    CHECK(host.press(draw::Key::Right) && s.value() == 3);
    CHECK(host.press(draw::Key::Up) && s.value() == 6);
    CHECK(host.press(draw::Key::Right) && s.value() == 9);
    CHECK(host.press(draw::Key::Right) && s.value() == 10);  // the maximum ends the grid
    CHECK(host.press(draw::Key::Right) && s.value() == 10);
}

KIT_TEST(slider_extreme_range_does_not_overflow) {
    draw::View root;
    FakeHost host(root);
    kit::Slider s(INT_MIN, INT_MAX, 0);  // the range spans all of int
    root.add(s, {0, 0, 200, 20});
    CHECK(s.value() == 0);
    s.focus();
    CHECK(host.press(draw::Key::Right) && s.value() == 1);
    CHECK(host.press(draw::Key::Left) && s.value() == 0);
    CHECK(host.press(draw::Key::End) && s.value() == INT_MAX);
    CHECK(host.press(draw::Key::Right) && s.value() == INT_MAX);  // no wrap
    CHECK(host.press(draw::Key::PageDown) && s.value() < INT_MAX);
    CHECK(host.press(draw::Key::Home) && s.value() == INT_MIN);
    CHECK(host.press(draw::Key::Left) && s.value() == INT_MIN);
    CHECK(host.press(draw::Key::PageUp) && s.value() > INT_MIN);
    s.setValue(INT_MAX);
    CHECK(s.value() == INT_MAX);
    s.setStep(INT_MAX);
    CHECK(s.value() == INT_MAX);
    CHECK(host.paint().drew("smoothRoundOutline "));
}

KIT_TEST(slider_drag_ends_when_the_capture_is_lost) {
    draw::View root;
    FakeHost host(root);
    kit::Slider s(0, 100, 0, 1);
    root.add(s, {0, 0, 200, 20});
    host.down(100, 10);  // dragging: the value follows the mouse
    CHECK(s.value() > 0);
    const int at = s.value();
    host.router.setCapture(nullptr);  // the system took the mouse
    host.move(190, 10);  // a plain move: the knob stays
    CHECK(s.value() == at);
}
