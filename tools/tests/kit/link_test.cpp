#include "kit_check.h"
#include "link.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

KIT_TEST(link_text_in_accent_and_underline) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Link l(L"Units", nullptr);
    root.add(l, {10, 10, 110, 30});
    auto c = host.paint();
    CHECK(c.drew("text 10,10,110,30 'Units' " + C(th.accent) + " 12"));
    CHECK(!c.drew("line "));  // no line at rest
    host.move(50, 20);
    auto hot = host.paint();
    // Under the mouse: a line at the text's baseline (center 20 px, descent 4 px, one pixel down), as wide as the text.
    CHECK(hot.drew("line 10,25,45,25 " + C(th.accent)));
    CHECK(hot.drew("text 10,10,110,30 'Units' " + C(th.accent) + " 12"));
}

KIT_TEST(link_quiet_tone_and_colors) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Link l(L"Rename", nullptr, kit::Tone::Quiet);
    root.add(l, {0, 0, 100, 20});
    CHECK(host.paint().drew("'Rename' " + C(th.dim) + " 12"));
    host.move(5, 5);
    auto hot = host.paint();
    CHECK(hot.drew("'Rename' " + C(th.text) + " 12"));
    CHECK(!hot.drew("line "));  // quiet: no line
    l.setTone(kit::Tone::Danger);
    CHECK(host.paint().drew("'Rename' " + C(th.bad) + " 12"));
    l.setTone(kit::Tone::Normal);
    CHECK(host.paint().drew("'Rename' " + C(th.text) + " 12"));
}

KIT_TEST(link_click_and_keys) {
    draw::View root;
    FakeHost host(root);
    int n = 0;
    kit::Link l(L"Units", [&] { ++n; });
    root.add(l, {0, 0, 100, 20});
    host.click(5, 5);
    CHECK(n == 1);
    CHECK(host.router.focused() == &l);
    CHECK(host.press(draw::Key::Enter) && n == 2);
    CHECK(host.press(draw::Key::Space) && n == 3);
    host.down(5, 5), host.up(80, 80);  // released outside: no click
    CHECK(n == 3);
    host.down(5, 5, 1), host.up(5, 5, 1);  // a right button does nothing
    CHECK(n == 3);
    CHECK(l.focusable());
    CHECK(l.cursor({0, 0}) == draw::Cursor::Hand);
}

KIT_TEST(link_text_setters_and_tip) {
    draw::View root;
    FakeHost host(root);
    kit::Link l(L"A", nullptr);
    root.add(l, {0, 0, 100, 20});
    RecordingCanvas m;
    CHECK(l.preferredWidth(m) == 7);
    l.setText(L"Hello");
    CHECK(l.preferredWidth(m) == 35);
    CHECK(l.text() == L"Hello");
    CHECK(l.tone() == kit::Tone::Primary);
    l.setTip(L"Go to the unit");
    CHECK(l.tip({1, 1}) == L"Go to the unit");
    CHECK(host.paint().texts()[0] == "Hello");
}
