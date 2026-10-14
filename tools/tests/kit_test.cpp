// The control kit's widgets (apps/kit) without a window: each is put in a FakeHost, gets events and is painted on a
// RecordingCanvas. Runs on every system. Prints "kit_test: passed" or the first check that fails.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "button.h"
#include "test_support.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static int gFailed = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::printf("kit_test: FAILED %s (line %d)\n", #x, __LINE__);    \
            ++gFailed;                                                        \
        }                                                                     \
    } while (0)

static void buttonTest() {
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

static void focusChainTest() {
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

int main() {
    buttonTest();
    focusChainTest();
    if (gFailed) return std::printf("kit_test: %d failed\n", gFailed), 1;
    std::printf("kit_test: passed\n");
    return 0;
}
