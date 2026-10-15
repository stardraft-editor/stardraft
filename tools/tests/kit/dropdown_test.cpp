#include <string>
#include <vector>

#include "dropdown.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;
using draw::Key;
using draw::MouseEvent;
using draw::MouseKind;

namespace {

kit::DropdownItem item(std::wstring label, int number = -1, int icon = -1) {
    kit::DropdownItem d;
    d.label = std::move(label), d.number = number, d.icon = icon;
    return d;
}
std::vector<kit::DropdownItem> colours() { return {item(L"Alpha"), item(L"Beta", 3), item(L"Gamma", 7)}; }
MouseEvent ev(MouseKind k, int x, int y, float wheel = 0) {
    MouseEvent e;
    e.kind = k, e.pt = {x, y}, e.wheel = wheel;
    return e;
}
// The list's content view, in its own coordinates.
draw::View& list(FakeHost& h) { return *h.windows.at(0).content; }

}  // namespace

KIT_TEST(dropdown_click_opens_list_and_enter_selects) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    int changes = 0, last = -2;
    dd.onChange = [&](int i) { ++changes, last = i; };
    root.add(dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(dd.listOpen());
    CHECK(host.windows.size() == 1);
    CHECK(host.windows[0].content != &dd);
    CHECK(host.router.focused() == &list(host));  // the keys go to the list
    const auto c = host.paintWindow(0);
    CHECK((c.texts() == std::vector<std::string>{"Alpha", "Beta", "3", "Gamma", "7"}));
    host.press(Key::Down);  // the list starts on the current value: Down is Beta
    host.press(Key::Enter);
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    CHECK(dd.value() == 1);
    CHECK(changes == 1 && last == 1);
    CHECK(host.router.focused() == &dd);  // the field has the keys again
    host.runTimers();
}

KIT_TEST(dropdown_click_on_row_picks_it) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(dd.listOpen());
    // The list's rows: 2 px from the top, 26 px each. Gamma is the third.
    list(host).mouse(ev(MouseKind::Move, 20, 2 + 26 * 2 + 5));
    list(host).mouse(ev(MouseKind::Down, 20, 2 + 26 * 2 + 5));
    list(host).mouse(ev(MouseKind::Up, 20, 2 + 26 * 2 + 5));
    CHECK(dd.value() == 2);
    CHECK(!dd.listOpen());
    host.runTimers();
}

KIT_TEST(dropdown_escape_keeps_the_value) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    int changes = 0;
    dd.onChange = [&](int) { ++changes; };
    root.add(dd, {10, 10, 210, 34});
    dd.focus();
    host.press(Key::Enter);  // Enter opens the list
    CHECK(dd.listOpen());
    host.press(Key::Down);
    host.press(Key::Down);
    host.press(Key::Escape);
    CHECK(!dd.listOpen());
    CHECK(dd.value() == 0);
    CHECK(changes == 0);
    CHECK(host.router.focused() == &dd);
    host.runTimers();
}

KIT_TEST(dropdown_up_and_down_change_value_without_opening) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    int changes = 0;
    dd.onChange = [&](int) { ++changes; };
    root.add(dd, {10, 10, 210, 34});
    dd.focus();
    host.press(Key::Down);
    CHECK(dd.value() == 1 && changes == 1);
    CHECK(host.windows.empty());
    host.press(Key::Down);
    CHECK(dd.value() == 2 && changes == 2);
    host.press(Key::Down);  // the last item: no wrap, no change
    CHECK(dd.value() == 2 && changes == 2);
    host.press(Key::Up);
    CHECK(dd.value() == 1 && changes == 3);
    host.press(Key::Down, draw::kAlt);  // Alt+Down opens the list
    CHECK(dd.listOpen());
    CHECK(host.windows.size() == 1);
    host.press(Key::Escape);
    CHECK(dd.value() == 1);
    host.runTimers();
}

KIT_TEST(dropdown_up_from_none_goes_to_the_last_and_down_to_the_first) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours());  // no value
    root.add(dd, {10, 10, 210, 34});
    CHECK(dd.value() == -1);
    dd.focus();
    host.press(Key::Up);
    CHECK(dd.value() == 2);
    dd.setValue(-1);
    host.press(Key::Down);
    CHECK(dd.value() == 0);
    host.runTimers();
}

KIT_TEST(dropdown_space_opens_the_list) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 2);
    root.add(dd, {10, 10, 210, 34});
    dd.focus();
    CHECK(host.press(Key::Space));
    CHECK(dd.listOpen());
    host.press(Key::Up);
    host.press(Key::Enter);
    CHECK(dd.value() == 1);
    host.runTimers();
}

KIT_TEST(dropdown_wheel_steps_the_closed_field) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    int changes = 0;
    dd.onChange = [&](int) { ++changes; };
    root.add(dd, {10, 10, 210, 34});
    host.wheel(50, 20, -1);  // toward the user: the next item
    CHECK(dd.value() == 1 && changes == 1);
    host.wheel(50, 20, 1);  // away from the user: the previous item
    CHECK(dd.value() == 0 && changes == 2);
    host.wheel(50, 20, 1);  // the first item: no change
    CHECK(dd.value() == 0 && changes == 2);
    host.wheel(50, 20, -0.5f);  // a half notch is not a step yet
    CHECK(dd.value() == 0);
    host.wheel(50, 20, -0.5f);
    CHECK(dd.value() == 1 && changes == 3);
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(dropdown_wheel_does_nothing_when_disabled_or_open) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    dd.setEnabled(false);
    host.wheel(50, 20, -1);
    CHECK(dd.value() == 0);
    dd.setEnabled(true);
    dd.openList();
    CHECK(dd.listOpen());
    host.wheel(50, 20, -1);
    CHECK(dd.value() == 0);
    host.runTimers();
}

KIT_TEST(dropdown_list_scrolls_with_the_wheel_and_keys) {
    draw::View root;
    FakeHost host(root);
    std::vector<kit::DropdownItem> many;
    for (int i = 0; i < 20; ++i) many.push_back(item(L"Item " + std::to_wstring(i)));
    kit::Dropdown dd(many, 0);
    root.add(dd, {10, 10, 210, 34});
    dd.openList();
    CHECK(host.windows.size() == 1);
    CHECK(host.paintWindow(0).drew("'Item 11'"));
    CHECK(!host.paintWindow(0).drew("'Item 12'"));  // twelve rows show
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, -3));  // scrolls down three rows
    CHECK(host.paintWindow(0).drew("'Item 3'"));
    CHECK(!host.paintWindow(0).drew("'Item 2'"));
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, -100));  // the end is the end
    CHECK(host.paintWindow(0).drew("'Item 19'"));
    CHECK(!host.paintWindow(0).drew("'Item 7'"));
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, 100));
    CHECK(host.paintWindow(0).drew("'Item 0'"));
    for (int i = 0; i < 19; ++i) host.press(Key::Down);  // the keys scroll to the highlighted row
    CHECK(host.paintWindow(0).drew("'Item 19'"));
    host.press(Key::Enter);
    CHECK(dd.value() == 19);
    host.runTimers();
}

KIT_TEST(dropdown_host_closing_the_list_does_not_reopen_on_its_press) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(dd.listOpen());
    // A click outside: the host closes the list and tells the field.
    auto closed = host.windows[0].closed;
    host.closeWindow(list(host));
    if (closed) closed();
    CHECK(!dd.listOpen());
    host.click(50, 20);  // the press that closed it does not open it again
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    host.click(50, 20);
    CHECK(dd.listOpen());
    host.runTimers();
}

KIT_TEST(dropdown_click_on_open_field_closes_it) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(dd.listOpen());
    host.click(50, 20);
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    CHECK(dd.value() == 0);
    host.runTimers();
}

KIT_TEST(dropdown_paints_value_number_icon_and_chevron) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd({item(L"Alpha", -1, 9), item(L"Beta", 3, 9)}, 1);
    root.add(dd, {10, 10, 210, 34});
    auto c = host.paint();
    CHECK(c.drew("'Beta' 0xe0e0e0"));
    CHECK(c.drew("'3' 0x949494"));  // the number in dim
    CHECK(c.drew(" 9 0x"));  // the item's icon
    CHECK(c.drew(" 82 0x"));  // the chevron down (UiIcon::Dropdown)
    CHECK(!c.drew("'Alpha'"));
    dd.setValue(0);
    c = host.paint();
    CHECK(c.drew("'Alpha' 0xe0e0e0"));
    CHECK(!c.drew("'Beta'"));
}

KIT_TEST(dropdown_disabled_is_faint_and_does_not_open) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    dd.setTip(L"Colour");
    root.add(dd, {10, 10, 210, 34});
    dd.setEnabled(false);
    CHECK(!dd.focusable());
    host.click(50, 20);
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    dd.setEnabled(true);
    CHECK(dd.focusable());
    CHECK(dd.tip(draw::Point{1, 1}) == L"Colour");
    dd.setEnabled(false);
    CHECK(host.paint().drew("'Alpha' 0x605a5a"));
    CHECK(!host.paint().drew(" 82 0x"));  // no chevron when disabled
    CHECK(!dd.key(draw::KeyEvent{Key::Down, true, false, 0}));
    host.runTimers();
}

KIT_TEST(dropdown_set_items_closes_the_list_and_keeps_a_valid_value) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 2);
    root.add(dd, {10, 10, 210, 34});
    dd.openList();
    CHECK(dd.listOpen());
    dd.setItems({item(L"Only")});  // Gamma is gone: the value is none
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    CHECK(dd.value() == -1);
    dd.setValue(0);
    CHECK(dd.value() == 0);
    dd.setValue(5);  // not an item: none
    CHECK(dd.value() == -1);
    host.runTimers();
}
