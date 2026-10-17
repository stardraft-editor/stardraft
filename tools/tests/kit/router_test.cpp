#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

namespace {
// A view that counts its presses (it takes a press, not a double click), or its double clicks too (`doubles`).
struct Presses final : draw::View {
    bool doubles = false;
    int downs = 0, doubleClicks = 0;
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down) return ++downs, true;
        if (e.kind == draw::MouseKind::DoubleClick && doubles) return ++doubleClicks, true;
        return e.kind == draw::MouseKind::Up;
    }
};
}  // namespace

// The system counts quick presses: the second and each next quick press is a double click. A view that does not take
// a double click gets it as a press, so each quick press acts; a view that takes double clicks gets them as they are.
KIT_TEST(router_double_click_is_a_press_for_a_view_without_one) {
    draw::View root;
    FakeHost host(root);
    Presses plain, own;
    own.doubles = true;
    root.add(plain, {0, 0, 50, 50});
    root.add(own, {60, 0, 110, 50});
    for (int k = 0; k < 4; ++k) host.doubleClick(20, 20);  // (a click, then a double click: eight quick presses)
    CHECK(plain.downs == 8);
    for (int k = 0; k < 4; ++k) host.doubleClick(80, 20);
    CHECK(own.downs == 4 && own.doubleClicks == 4);
}

// A double click that the view under the mouse does not take goes to it as a press before its parents see it.
KIT_TEST(router_double_click_press_goes_to_the_view_first) {
    draw::View root;
    FakeHost host(root);
    Presses parent, child;
    parent.doubles = true;
    root.add(parent, {0, 0, 100, 100});
    parent.add(child, {10, 10, 50, 50});
    host.doubleClick(20, 20);
    CHECK(child.downs == 2 && parent.doubleClicks == 0);
}
