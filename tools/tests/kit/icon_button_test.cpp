#include "icon_button.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

KIT_TEST(icon_button_click_and_keys) {
    draw::View root;
    FakeHost host(root);
    int n = 0;
    kit::IconButton b(kit::IconGlyph::ofIcon(7), kit::IconStyle::Bare, L"Open", [&] { ++n; });
    root.add(b, {0, 0, 24, 24});
    CHECK(b.focusable() && b.enabled());
    host.click(12, 12);
    CHECK(n == 1);
    CHECK(host.router.focused() == &b);  // a click gives it the focus
    CHECK(host.press(draw::Key::Enter) && n == 2);
    CHECK(host.press(draw::Key::Space) && n == 3);
    host.down(12, 12), host.up(50, 50);  // let go outside: no click
    CHECK(n == 3);
    host.click(12, 12);
    CHECK(n == 4);
    host.down(12, 12, 1), host.up(12, 12, 1);  // a right button does not click it
    CHECK(n == 4);
}

KIT_TEST(icon_button_disabled_and_tip) {
    draw::View root;
    FakeHost host(root);
    int n = 0;
    kit::IconButton b(kit::IconGlyph::ofIcon(7), kit::IconStyle::Bare, L"Open", [&] { ++n; });
    root.add(b, {0, 0, 24, 24});
    CHECK(b.tip({0, 0}) == L"Open");
    b.setTip(L"Save");
    CHECK(b.tip({5, 5}) == L"Save");
    CHECK(b.cursor({0, 0}) == draw::Cursor::Hand);
    b.setEnabled(false);
    CHECK(!b.focusable());
    CHECK(b.cursor({0, 0}) == draw::Cursor::Arrow);
    host.click(12, 12);
    CHECK(n == 0 && host.router.focused() == nullptr);
    CHECK(!host.press(draw::Key::Enter) && n == 0);
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(kit::theme().faint)));
    b.setEnabled(true);
    CHECK(b.focusable());
}

KIT_TEST(icon_button_bare_look) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::IconButton b(kit::IconGlyph::ofIcon(7), kit::IconStyle::Bare, L"Open", nullptr);
    root.add(b, {0, 0, 24, 24});
    // At rest: the glyph in the dim tone, no box. Under the mouse: the box and the text color.
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.dim)));
    CHECK(!host.paint().drew("roundBox"));
    host.move(12, 12);
    auto hot = host.paint();
    CHECK(hot.drew("roundBox 0,0,24,24 " + C(th.ctlHot) + " none 4 1"));
    CHECK(hot.drew("icon 0,0,24,24 7 " + C(th.text)));
    host.move(100, 100);
    b.setOn(true);  // a toggle that is on: the accent, lit under the mouse
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.accent)));
    host.move(12, 12);
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.accentBright)));
    host.move(100, 100);
    b.setOn(false);
    b.setTone(kit::Tone::Primary);
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.accent)));
    b.setTone(kit::Tone::Danger);
    CHECK(!host.paint().drew("icon 0,0,24,24 7 " + C(th.bad)));  // at rest: dim
    host.move(12, 12);
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.bad)));  // under the mouse: red
    host.move(100, 100);
    b.setTone(kit::Tone::Quiet);
    b.setOn(true);  // a quiet toggle that is on is amber (a block that is turned off)
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.warn)));
    host.move(12, 12);
    CHECK(host.paint().drew("icon 0,0,24,24 7 " + C(th.warn)));
}

KIT_TEST(icon_button_boxed_look) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::IconButton b(kit::IconGlyph::ofIcon(9), kit::IconStyle::Boxed, L"Play", nullptr);
    root.add(b, {0, 0, 24, 24});
    // Flat at rest: no box, the glyph dim.
    CHECK(!host.paint().drew("roundBox"));
    CHECK(host.paint().drew("icon 0,0,24,24 9 " + C(th.dim)));
    host.move(12, 12);
    CHECK(host.paint().drew("roundBox 0,0,24,24 " + C(th.ctlHot) + " none 4 1"));
    b.setOn(true);  // on: the accent box, and the lit glyph
    auto on = host.paint();
    CHECK(on.drew("roundBox 0,0,24,24 " + C(th.sel) + " " + C(th.accentLine) + " 4 1"));
    CHECK(on.drew("icon 0,0,24,24 9 " + C(th.accentBright)));
    b.setOn(false);
    b.setTone(kit::Tone::Danger);
    CHECK(host.paint().drew("icon 0,0,24,24 9 " + C(th.bad)));
}

KIT_TEST(icon_button_glyph_from_glyphs_face) {
    draw::View root;
    FakeHost host(root);
    const wchar_t g = wchar_t(0xE70F);  // a Segoe MDL2 Assets code point
    kit::IconButton b(kit::IconGlyph::ofGlyph(g), kit::IconStyle::Bare, L"Edit", nullptr);
    root.add(b, {0, 0, 24, 24});
    auto c = host.paint();
    CHECK(c.texts().size() == 1 && c.texts()[0] == utf8(std::wstring(1, g)));
    bool iconDrawn = false;
    for (const auto& o : c.ops) iconDrawn = iconDrawn || o.rfind("icon ", 0) == 0;
    CHECK(!iconDrawn);  // a glyph is text in the Glyphs face, not a UI icon
}

KIT_TEST(icon_button_focus_ring) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::IconButton b(kit::IconGlyph::ofIcon(7), kit::IconStyle::Bare, L"Open", nullptr);
    root.add(b, {0, 0, 24, 24});
    b.focus();
    CHECK(host.paint().drew("roundBox 0,0,24,24 none " + C(th.accent) + " 4 1"));
}

KIT_TEST(icon_button_square_in_place) {
    draw::View root;
    FakeHost host(root);
    kit::IconButton b(kit::IconGlyph::ofIcon(7), kit::IconStyle::Bare, L"Open", nullptr);
    root.add(b, {0, 0, 30, 20});  // a wide place: the icon is the square in its middle
    CHECK(host.paint().drew("icon 5,0,25,20 7 "));
    CHECK(kit::IconButton::preferredSize() == 24);
}
