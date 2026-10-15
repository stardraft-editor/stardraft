#include <memory>
#include <vector>

#include "check_box.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

KIT_TEST(check_box_click_and_space_toggle) {
    draw::View root;
    FakeHost host(root);
    std::vector<bool> got;
    kit::CheckBox cb(L"Show", false, [&](bool v) { got.push_back(v); });
    root.add(cb, {0, 0, 100, 20});
    host.click(5, 5);
    CHECK(cb.on());
    CHECK(got.size() == 1 && got[0]);
    host.click(5, 5);
    CHECK(!cb.on());
    CHECK(got.size() == 2 && !got[1]);
    CHECK(host.router.focused() == &cb);  // a click gives it the focus
    CHECK(host.press(draw::Key::Space));
    CHECK(cb.on() && got.size() == 3);
    CHECK(host.press(draw::Key::Enter) == false);  // Enter is not the check box's key
    CHECK(got.size() == 3);
}

KIT_TEST(check_box_release_outside_is_no_click) {
    draw::View root;
    FakeHost host(root);
    int changes = 0;
    kit::CheckBox cb(L"Show", false, [&](bool) { ++changes; });
    root.add(cb, {0, 0, 100, 20});
    host.down(5, 5), host.up(300, 300);
    CHECK(changes == 0 && !cb.on());
    host.down(5, 5, 1);  // a right button does nothing here
    host.up(5, 5, 1);
    CHECK(changes == 0);
}

KIT_TEST(check_box_setOn_has_no_event) {
    kit::CheckBox cb(L"Show");
    int changes = 0;
    cb.onChange = [&](bool) { ++changes; };
    cb.setOn(true);
    CHECK(cb.on() && changes == 0);
}

KIT_TEST(check_box_look_and_states) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::CheckBox cb(L"Show");
    root.add(cb, {0, 0, 100, 20});
    // The box: 14 px, centered up and down; the label after 8 px of space.
    CHECK(host.paint().drew("roundBox 0,3,14,17 " + C(th.ctl) + " " + C(th.borderStrong) + " 3 1"));
    CHECK(host.paint().drew("text 22,0,100,20 'Show' " + C(th.dim) + " 12"));
    cb.setChanged(true);  // a changed setting: the label is green
    CHECK(host.paint().drew("text 22,0,100,20 'Show' " + C(th.good) + " 12"));
    cb.setChanged(false);
    cb.setOn(true);  // on: the box is filled, and the check mark is drawn
    auto on = host.paint();
    CHECK(on.drew("roundBox 0,3,14,17 " + C(th.accentFill) + " " + C(th.accent) + " 3 1"));
    CHECK(on.drew("polyline 3 " + C(th.onText)));
    CHECK(on.drew("text 22,0,100,20 'Show' " + C(th.text) + " 12"));
    cb.setEnabled(false);
    CHECK(host.paint().drew("text 22,0,100,20 'Show' " + C(th.faint) + " 12"));
    CHECK(!cb.focusable());
}

KIT_TEST(check_box_focus_ring_and_disabled) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::CheckBox cb(L"Show");
    root.add(cb, {0, 0, 100, 20});
    cb.focus();
    CHECK(host.paint().drew("roundBox 0,3,14,17 " + C(th.ctl) + " " + C(th.accent) + " 3 1"));
    cb.setEnabled(false);
    CHECK(!cb.focusable());
    host.click(5, 5);  // disabled: no change, and no focus
    CHECK(!cb.on());
    CHECK(!host.press(draw::Key::Space));
    CHECK(!cb.on());
}

KIT_TEST(radio_group_chooses_one) {
    draw::View root;
    FakeHost host(root);
    kit::RadioGroup group;
    std::vector<std::wstring> chosen;
    group.onChange = [&](kit::RadioButton& b) { chosen.push_back(b.label()); };
    kit::RadioButton a(L"A", group, true), b(L"B", group), c(L"C", group);
    root.add(a, {0, 0, 100, 20}), root.add(b, {0, 20, 100, 40}), root.add(c, {0, 40, 100, 60});
    CHECK(group.selected() == &a);
    CHECK(group.buttons().size() == 3);
    host.click(5, 25);  // B
    CHECK(group.selected() == &b);
    CHECK(b.on() && !a.on() && !c.on());
    CHECK(chosen.size() == 1 && chosen[0] == L"B");
    host.click(5, 25);  // the chosen one again: nothing changes, no event
    CHECK(group.selected() == &b && chosen.size() == 1);
    host.click(5, 45);  // C
    CHECK(group.selected() == &c && !b.on());
    CHECK(chosen.size() == 2 && chosen[1] == L"C");
}

KIT_TEST(radio_group_setOn_and_no_event_from_select) {
    kit::RadioGroup group;
    int events = 0;
    group.onChange = [&](kit::RadioButton&) { ++events; };
    kit::RadioButton a(L"A", group, true), b(L"B", group);
    b.setOn(true);  // the others go off, with no event
    CHECK(!a.on() && b.on() && events == 0);
    group.select(a);
    CHECK(group.selected() == &a && events == 0);
    b.setOn(false);  // off: only this one; nothing is chosen now
    CHECK(group.selected() == &a);
    a.setOn(false);
    CHECK(group.selected() == nullptr);
}

KIT_TEST(radio_group_arrow_keys) {
    draw::View root;
    FakeHost host(root);
    kit::RadioGroup group;
    kit::RadioButton a(L"A", group, true), b(L"B", group), c(L"C", group);
    root.add(a, {0, 0, 100, 20}), root.add(b, {0, 20, 100, 40}), root.add(c, {0, 40, 100, 60});
    a.focus();
    CHECK(host.press(draw::Key::Down));
    CHECK(host.router.focused() == &b && group.selected() == &b);
    CHECK(host.press(draw::Key::Right));
    CHECK(host.router.focused() == &c && group.selected() == &c);
    CHECK(host.press(draw::Key::Down));  // the end: used, nothing moves
    CHECK(host.router.focused() == &c && group.selected() == &c);
    CHECK(host.press(draw::Key::Up));
    CHECK(host.router.focused() == &b && group.selected() == &b);
    CHECK(host.press(draw::Key::Left));
    CHECK(host.router.focused() == &a && group.selected() == &a);
    CHECK(host.press(draw::Key::Up) && group.selected() == &a);  // the start: nothing moves
    // A disabled button is passed over.
    b.setEnabled(false);
    CHECK(host.press(draw::Key::Down));
    CHECK(host.router.focused() == &c && group.selected() == &c);
}

KIT_TEST(radio_button_leaves_group_on_destruction) {
    kit::RadioGroup group;
    kit::RadioButton a(L"A", group, true);
    {
        kit::RadioButton d(L"D", group);
        CHECK(group.buttons().size() == 2);
    }
    CHECK(group.buttons().size() == 1 && group.selected() == &a);
}

KIT_TEST(radio_group_destroyed_first_unlinks_buttons) {
    auto group = std::make_unique<kit::RadioGroup>();
    auto b = std::make_unique<kit::RadioButton>(L"B", *group, true);
    CHECK(b->group() == group.get());
    group.reset();
    CHECK(b->group() == nullptr);
    CHECK(b->on());
    b->setOn(false);  // with no group, a radio button is only on or off
    CHECK(!b->on());
    b->setOn(true);
    CHECK(b->on());
}

KIT_TEST(radio_mark_look) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::RadioGroup group;
    kit::RadioButton a(L"A", group, true);
    root.add(a, {0, 0, 100, 20});
    auto c = host.paint();
    // The circle, and the dot inside it (3/10 of the circle in from each side) in the accent.
    CHECK(c.drew("ellipse 0,3,14,17 " + C(th.ctl) + " " + C(th.accent)));
    CHECK(c.drew("ellipse 4,7,11,14 " + C(th.accent) + " none"));
    group.select(a);
    kit::RadioButton b(L"B", group);
    root.add(b, {0, 20, 100, 40});
    c = host.paint();
    CHECK(c.drew("ellipse 0,23,14,37 " + C(th.ctl) + " " + C(th.borderStrong)) == true);
    CHECK(!c.drew("ellipse 4,27,11,34"));
    CHECK(c.drew("text 22,20,100,40 'B' " + C(th.dim) + " 12"));
}
