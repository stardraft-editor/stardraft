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

// The host outlives the dropdowns in these tests, as it does in a program: the views are declared after the host.

KIT_TEST(dropdown_removed_while_its_list_is_open_closes_the_list_on_pick) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(host.windows.size() == 1);
    root.remove(dd);  // the form took it out; the list is still on the screen
    CHECK(host.windows.size() == 1);
    host.press(Key::Enter);  // the list has the keys: its pick closes it, though the field is gone
    CHECK(host.windows.empty());
    CHECK(!dd.listOpen());
    host.runTimers();
}

KIT_TEST(dropdown_deleted_while_its_list_is_open_closes_the_list) {
    draw::View root;
    FakeHost host(root);
    auto* dd = new kit::Dropdown(colours(), 0);
    root.add(*dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(host.windows.size() == 1);
    delete dd;  // the list goes with it: the host must not keep it
    CHECK(host.windows.empty());
    CHECK(host.router.focused() == nullptr);
    host.paint();
    host.runTimers();
}

KIT_TEST(dropdown_onchange_that_deletes_the_dropdown_is_safe) {
    draw::View root;
    FakeHost host(root);
    auto* dd = new kit::Dropdown(colours(), 0);
    root.add(*dd, {10, 10, 210, 34});
    dd->onChange = [&](int) { delete dd; };  // a form that rebuilds its rows deletes the field
    host.click(50, 20);
    host.press(Key::Down);
    host.press(Key::Enter);  // picks Beta: the field goes while its list handles the key
    CHECK(host.windows.empty());
    host.runTimers();  // the list is deleted here, after its event
}

KIT_TEST(dropdown_onchange_that_reopens_the_list_is_safe) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    dd.onChange = [&](int) { dd.openList(); };
    host.click(50, 20);
    host.press(Key::Down);
    host.press(Key::Enter);  // picks Beta, then the callback opens the list again
    CHECK(dd.value() == 1);
    CHECK(dd.listOpen());
    CHECK(host.windows.size() == 1);
    host.press(Key::Escape);
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    host.runTimers();
}

KIT_TEST(dropdown_outside_close_gives_the_keys_back_to_the_field) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    host.click(50, 20);
    CHECK(host.router.focused() == &list(host));
    // A click outside: the host closes the list and tells the field.
    auto closed = host.windows[0].closed;
    host.closeWindow(list(host));
    if (closed) closed();
    CHECK(!dd.listOpen());
    CHECK(host.router.focused() == &dd);
    host.runTimers();
}

KIT_TEST(dropdown_disabled_while_open_closes_without_taking_the_focus) {
    draw::View root;
    FakeHost host(root);
    kit::Dropdown dd(colours(), 0);
    root.add(dd, {10, 10, 210, 34});
    dd.openList();
    CHECK(dd.listOpen());
    dd.setEnabled(false);
    CHECK(!dd.listOpen());
    CHECK(host.windows.empty());
    CHECK(host.router.focused() != &dd);  // a disabled field cannot have the keys
    host.runTimers();
}

KIT_TEST(dropdown_fractional_wheel_scrolls_the_open_list_when_it_adds_up) {
    draw::View root;
    FakeHost host(root);
    std::vector<kit::DropdownItem> many;
    for (int i = 0; i < 20; ++i) many.push_back(item(L"Item " + std::to_wstring(i)));
    kit::Dropdown dd(many, 0);
    root.add(dd, {10, 10, 210, 34});
    dd.openList();
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, -0.25f));
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, -0.25f));
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, -0.25f));
    CHECK(host.paintWindow(0).drew("'Item 0'"));  // three quarters of a notch: not yet
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, -0.25f));
    CHECK(host.paintWindow(0).drew("'Item 1'"));  // the fourth quarter makes the notch
    CHECK(!host.paintWindow(0).drew("'Item 0'"));
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, 0.5f));
    list(host).mouse(ev(MouseKind::Wheel, 20, 20, 0.5f));
    CHECK(host.paintWindow(0).drew("'Item 0'"));  // two halves scroll back up
    host.runTimers();
}
