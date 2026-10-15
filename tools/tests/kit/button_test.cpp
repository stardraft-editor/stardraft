#include "button.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

KIT_TEST(button) {
    draw::View root;
    FakeHost host(root);
    int clicks = 0;
    kit::Button b(L"OK", kit::Tone::Primary, [&] { ++clicks; });
    root.add(b, {10, 10, 98, 38});
    host.click(20, 20);
    CHECK(clicks == 1);
    host.click(200, 200);  // outside: nothing
    CHECK(clicks == 1);
    host.down(20, 20), host.up(300, 300);  // pressed, then let go outside: no click
    CHECK(clicks == 1);
    CHECK(host.router.focused() == &b);  // a click gives it the focus
    CHECK(host.press(draw::Key::Space));
    CHECK(clicks == 2);
    auto c = host.paint();
    CHECK(c.texts().size() == 1 && c.texts()[0] == "OK");
    b.setEnabled(false);
    host.click(20, 20);
    CHECK(clicks == 2);
}

KIT_TEST(focus_chain) {
    draw::View root;
    FakeHost host(root);
    kit::Button a(L"A", kit::Tone::Normal, nullptr), b(L"B", kit::Tone::Normal, nullptr), c(L"C", kit::Tone::Normal, nullptr);
    root.add(a, {0, 0, 50, 20}), root.add(b, {60, 0, 110, 20}), root.add(c, {120, 0, 170, 20});
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &a);
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &b);
    host.press(draw::Key::Tab, draw::kShift);
    CHECK(host.router.focused() == &a);
    b.setVisible(false);
    host.press(draw::Key::Tab);
    CHECK(host.router.focused() == &c);
    root.remove(c);  // a view that leaves: the focus forgets it
    CHECK(host.router.focused() == nullptr);
}
