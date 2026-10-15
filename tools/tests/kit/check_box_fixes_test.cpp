#include <vector>

#include "check_box.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

// A check box's box: 14 px at the left, centered in a 20 px row, so y 3 to 17.

KIT_TEST(radio_group_is_one_tab_stop) {
    draw::View root;
    FakeHost host(root);
    kit::RadioGroup g;
    kit::RadioButton a(L"A", g), b(L"B", g), c(L"C", g, true), d(L"D", g), e(L"E", g);
    root.add(a, {0, 0, 100, 20});
    root.add(b, {0, 20, 100, 40});
    root.add(c, {0, 40, 100, 60});
    root.add(d, {0, 60, 100, 80});
    root.add(e, {0, 80, 100, 100});
    std::vector<draw::View*> chain;
    root.focusChain(chain);
    CHECK(chain.size() == 1 && chain[0] == &c);  // the chosen button: the only one Tab reaches
    // Tab from before the group goes to the chosen button, and Tab after it leaves the group.
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &c);
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &c);  // no other focusable view: it stays
    // The arrow keys still walk the group.
    CHECK(host.press(draw::Key::Down) && g.selected() == &d && host.router.focused() == &d);
}

KIT_TEST(radio_group_tab_stop_is_the_first_usable_button_when_the_choice_is_hidden) {
    draw::View root;
    FakeHost host(root);
    kit::RadioGroup g;
    kit::RadioButton a(L"A", g), b(L"B", g, true);
    root.add(a, {0, 0, 100, 20});
    root.add(b, {0, 20, 100, 40});
    b.setVisible(false);  // the chosen one is not on the screen
    std::vector<draw::View*> chain;
    root.focusChain(chain);
    CHECK(chain.size() == 1 && chain[0] == &a);
}

KIT_TEST(radio_arrow_keys_skip_a_hidden_button) {
    draw::View root;
    FakeHost host(root);
    kit::RadioGroup g;
    kit::RadioButton a(L"A", g, true), b(L"B", g), c(L"C", g);
    root.add(a, {0, 0, 100, 20});
    root.add(b, {0, 20, 100, 40});
    root.add(c, {0, 40, 100, 60});
    c.setVisible(false);  // a hidden option: the user cannot see it
    b.focus();
    CHECK(host.press(draw::Key::Down));  // the key is used, but C is not chosen
    CHECK(!c.on() && g.selected() == &a);
    CHECK(b.on() == false);
    CHECK(host.press(draw::Key::Up) && g.selected() == &a);  // (up from B: A)
    CHECK(host.press(draw::Key::Down) && g.selected() == &b);
}

KIT_TEST(check_box_click_shows_no_focus_ring) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::CheckBox cb(L"Option");
    root.add(cb, {0, 0, 120, 20});
    host.click(5, 10);  // on, and the box has the focus from the click
    CHECK(cb.on() && cb.hasFocus());
    host.click(5, 10);  // off again
    CHECK(!cb.on());
    host.move(200, 200);  // the mouse leaves: no hover
    // Off, with the mouse away: the outline is the normal border, not the accent (a focus ring would show as accent).
    CHECK(host.paint().drew("roundBox 0,3,14,17 " + C(th.ctl) + " " + C(th.borderStrong) + " 3 1"));
}

KIT_TEST(check_box_keyboard_focus_shows_the_ring) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::CheckBox cb(L"Option");
    root.add(cb, {0, 0, 120, 20});
    host.click(5, 10);  // on, by the mouse
    host.move(200, 200);
    CHECK(host.press(draw::Key::Space) && !cb.on());  // Space is the keyboard's: off, and the ring shows
    CHECK(host.paint().drew("roundBox 0,3,14,17 " + C(th.ctl) + " " + C(th.accent) + " 3 1"));
}

KIT_TEST(check_box_drag_off_its_row_is_no_click_after_capture_lost) {
    draw::View root;
    FakeHost host(root);
    int changes = 0;
    kit::CheckBox cb(L"Option", false, [&](bool) { ++changes; });
    root.add(cb, {0, 0, 120, 20});
    host.down(5, 10);  // pressed
    host.router.setCapture(nullptr);  // the system took the mouse: the release never comes
    host.up(5, 10);
    CHECK(changes == 0 && !cb.on());
    host.click(5, 10);  // a whole click works again
    CHECK(changes == 1 && cb.on());
}
