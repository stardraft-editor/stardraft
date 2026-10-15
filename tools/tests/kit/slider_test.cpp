#include "kit_check.h"
#include "slider.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

// A slider 200 px wide on the root: the knob's centers run from x 7 to x 193 (the knob's radius at each end).
KIT_TEST(slider_click_jumps_and_drag) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> got;
    kit::Slider s(0, 10, 0);
    s.onChange = [&](int v) { got.push_back(v); };
    root.add(s, {0, 0, 200, 16});
    host.click(100, 8);  // (100 - 7) / 186 of the range: 5
    CHECK(s.value() == 5);
    CHECK(got.size() == 1 && got[0] == 5);
    host.down(7, 8);  // the left end
    CHECK(s.value() == 0 && got.size() == 2);
    host.move(193, 8);  // dragged to the right end
    CHECK(s.value() == 10 && got.size() == 3);
    host.move(300, 8);  // past it: stays at the end, no change
    CHECK(s.value() == 10 && got.size() == 3);
    host.up(300, 8);  // let go
    host.move(50, 8);  // not dragging any more: no change
    CHECK(s.value() == 10 && got.size() == 3);
    CHECK(host.router.focused() == &s);
}

KIT_TEST(slider_step_snaps_and_ends) {
    kit::Slider s(0, 10, 0, 3);
    CHECK(s.step() == 3);
    s.setValue(7);  // nearest step: 6
    CHECK(s.value() == 6);
    s.setValue(100);  // the ends are always reached
    CHECK(s.value() == 10);
    s.setValue(-5);
    CHECK(s.value() == 0);
    s.setStep(0);  // at least 1
    CHECK(s.step() == 1);
    s.setValue(7);
    CHECK(s.value() == 7);
}

KIT_TEST(slider_keys) {
    draw::View root;
    FakeHost host(root);
    int events = 0;
    kit::Slider s(0, 100, 50);
    s.onChange = [&](int) { ++events; };
    root.add(s, {0, 0, 200, 16});
    s.focus();
    CHECK(host.press(draw::Key::Right) && s.value() == 51);
    CHECK(host.press(draw::Key::Left) && s.value() == 50);
    CHECK(host.press(draw::Key::Up) && s.value() == 51);
    CHECK(host.press(draw::Key::Down) && s.value() == 50);
    CHECK(host.press(draw::Key::PageUp) && s.value() == 60);  // a tenth of the range
    CHECK(host.press(draw::Key::PageDown) && s.value() == 50);
    CHECK(host.press(draw::Key::End) && s.value() == 100);
    CHECK(host.press(draw::Key::Right) && s.value() == 100);  // at the end: used, no change
    CHECK(host.press(draw::Key::Home) && s.value() == 0);
    CHECK(host.press(draw::Key::PageDown) && s.value() == 0);
    CHECK(!host.press(draw::Key::Enter));  // not a slider key
    CHECK(events == 8);                    // every change, and no event for the keys at the ends
}

KIT_TEST(slider_page_step_is_at_least_one_step) {
    draw::View root;
    FakeHost host(root);
    kit::Slider s(0, 10, 5, 2);
    root.add(s, {0, 0, 200, 16});
    s.focus();
    CHECK(host.press(draw::Key::PageUp));  // a tenth of 10 is 1, so the step (2) is the page
    CHECK(s.value() == 8);  // 7 is halfway between the steps 6 and 8: up
    s.setValue(4);
    CHECK(host.press(draw::Key::PageDown) && s.value() == 2);
}

KIT_TEST(slider_wheel) {
    draw::View root;
    FakeHost host(root);
    int events = 0;
    kit::Slider s(0, 10, 5);
    s.onChange = [&](int) { ++events; };
    root.add(s, {0, 0, 200, 16});
    host.wheel(50, 8, 1.0f);
    CHECK(s.value() == 6 && events == 1);
    host.wheel(50, 8, -2.0f);  // two notches down: two changes
    CHECK(s.value() == 4 && events == 3);
    host.wheel(50, 8, 0.0f);  // no movement, no change
    CHECK(s.value() == 4 && events == 3);
    s.setValue(10);
    host.wheel(50, 8, 1.0f);  // at the end: no change
    CHECK(s.value() == 10 && events == 3);
}

KIT_TEST(slider_disabled) {
    draw::View root;
    FakeHost host(root);
    int events = 0;
    kit::Slider s(0, 10, 5);
    s.onChange = [&](int) { ++events; };
    root.add(s, {0, 0, 200, 16});
    s.setEnabled(false);
    CHECK(!s.focusable());
    host.click(150, 8);
    CHECK(s.value() == 5 && events == 0);
    host.wheel(50, 8, 1.0f);
    CHECK(s.value() == 5);
    s.setValue(3);  // a program can still set it
    CHECK(s.value() == 3 && events == 0);
}

KIT_TEST(slider_range_changes_keep_value_in_range) {
    kit::Slider s(0, 10, 8);
    int events = 0;
    s.onChange = [&](int) { ++events; };
    s.setRange(0, 5);
    CHECK(s.value() == 5 && events == 0);
    s.setRange(7, 3);  // the high end below the low one: the range is just 7
    CHECK(s.minValue() == 7 && s.maxValue() == 7 && s.value() == 7 && events == 0);
}

KIT_TEST(slider_look) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Slider s(0, 10, 5);
    root.add(s, {0, 0, 200, 16});
    auto c = host.paint();
    // The track (its height 4 px, centered), the part up to the value in the accent, and the knob (radius 7).
    CHECK(c.drew("roundBox 7,6,193,10 " + C(th.ctl) + " " + C(th.border) + " 2 1"));
    CHECK(c.drew("fill 7,6,100,10 " + C(th.accent) + " 255"));
    CHECK(c.drew("smoothRoundOutline 93,1,107,15 " + C(th.accent)));
    s.focus();
    CHECK(host.paint().drew("smoothRoundOutline 93,1,107,15 " + C(th.accentBright)));
    s.setEnabled(false);
    CHECK(host.paint().drew("fill 7,6,100,10 " + C(th.faint) + " 255"));
    CHECK(kit::Slider::preferredHeight() == 16);
}
