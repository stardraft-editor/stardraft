#include "label.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

KIT_TEST(label_text_and_tone) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Label l(L"Hello", kit::LabelTone::Dim);
    root.add(l, {10, 10, 110, 30});
    auto c = host.paint();
    CHECK(c.texts().size() == 1 && c.texts()[0] == "Hello");
    // One line, cut with "..." at its end, in the dim tone.
    CHECK(c.drew("text 10,10,110,30 'Hello' " + C(th.dim) + " 12"));
    l.setTone(kit::LabelTone::Good);
    CHECK(host.paint().drew("'Hello' " + C(th.good) + " 12"));
    l.setTone(kit::LabelTone::Faint);
    CHECK(host.paint().drew("'Hello' " + C(th.faint) + " 12"));
    l.setTone(kit::LabelTone::Warn);
    CHECK(host.paint().drew("'Hello' " + C(th.warn) + " 12"));
    l.setTone(kit::LabelTone::Bad);
    CHECK(host.paint().drew("'Hello' " + C(th.bad) + " 12"));
    l.setTone(kit::LabelTone::Normal);
    CHECK(host.paint().drew("'Hello' " + C(th.text) + " 12"));
    CHECK(l.tone() == kit::LabelTone::Normal);
}

KIT_TEST(label_wrap_height_and_paint) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Label l(L"abcdefghij");  // 10 characters: 70 px in the test canvas
    root.add(l, {0, 0, 40, 100});
    RecordingCanvas c;
    CHECK(l.preferredHeight(c, 35) == 16);  // one line, whatever the width: not wrapped
    l.setWrap(true);
    CHECK(l.preferredHeight(c, 35) == 32);  // two lines in 35 px
    CHECK(l.preferredHeight(c, 100) == 16);
    CHECK(l.preferredHeight(c, 70) == 16);
    l.setWrap(false);
    CHECK(l.preferredHeight(c, 35) == 16);
    l.setWrap(true);
    // Wrapped text is drawn from the top (no middle flag), and wraps the way the canvas does.
    CHECK(host.paint().drew("text 0,0,40,100 'abcdefghij' " + C(th.text) + " 16"));
}

KIT_TEST(label_icon_and_width) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Label l(L"abc");
    l.setIcon(5);
    root.add(l, {0, 0, 200, 30});
    auto c = host.paint();
    // The icon is 16 px, centered up and down, then 8 px of space; the text starts after them.
    CHECK(c.drew("icon 0,7,16,23 5 " + C(th.text)));
    CHECK(c.drew("text 24,0,200,30 'abc' " + C(th.text) + " 12"));
    RecordingCanvas m;
    CHECK(l.preferredWidth(m) == 24 + 21);
    CHECK(l.preferredHeight(m, 100) == 16);
    l.setIcon(-1);
    CHECK(l.preferredWidth(m) == 21);
    CHECK(host.paint().drew("text 0,0,200,30 'abc' " + C(th.text) + " 12"));
}

KIT_TEST(label_takes_no_input) {
    draw::View root;
    FakeHost host(root);
    kit::Label l(L"Text");
    root.add(l, {10, 10, 110, 30});
    CHECK(!l.focusable());
    host.click(20, 20);  // nothing takes it: no focus
    CHECK(host.router.focused() == nullptr);
}

KIT_TEST(label_changes_repaint) {
    draw::View root;
    FakeHost host(root);
    kit::Label l(L"a");
    root.add(l, {0, 0, 50, 20});
    const int before = host.invalidations;
    l.setText(L"b");
    CHECK(host.invalidations > before);
    CHECK(host.paint().texts()[0] == "b");
}
