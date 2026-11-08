#include "badge.h"
#include "card.h"
#include "kit_check.h"
#include "stepper.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

KIT_TEST(badge_tones_and_width) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Badge b(L"Found", kit::BadgeTone::Good);
    root.add(b, {0, 0, 200, 22});
    RecordingCanvas m;
    CHECK(b.preferredWidth(m) == 5 * 7 + 16);  // the text and 8 px on each side
    auto c = host.paint();
    // As wide as its text, at the left; the tone's fill, line and text.
    CHECK(c.drew("roundBox 0,0,51,22 " + C(th.goodFill) + " " + C(th.goodLine)));
    CHECK(c.drew("'Found' " + C(th.good)));
    b.setTone(kit::BadgeTone::Warn);
    CHECK(host.paint().drew("'Found' " + C(th.warn)));
    b.setTone(kit::BadgeTone::Bad);
    CHECK(host.paint().drew("roundBox 0,0,51,22 " + C(th.badFill) + " " + C(th.bad)));
    b.setTone(kit::BadgeTone::Neutral);
    CHECK(host.paint().drew("'Found' " + C(th.dim)));
    // At the right of its frame.
    b.setAlignRight(true);
    CHECK(host.paint().drew("roundBox 149,0,200,22"));
    // No text: nothing.
    b.setText(L"");
    CHECK(!host.paint().drew("roundBox"));
}

KIT_TEST(card_paints_a_box_under_its_children) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Card card;
    kit::Badge inside(L"x", kit::BadgeTone::Neutral);
    root.add(card, {10, 10, 110, 60});
    card.add(inside, {8, 8, 40, 28});
    auto c = host.paint();
    CHECK(c.drew("roundBox 10,10,110,60 " + C(th.panel) + " " + C(th.border) + " " + std::to_string(kit::px(kit::kRadius))));
    CHECK(c.texts().size() == 1 && c.texts()[0] == "x");  // (the child paints on it)
}

KIT_TEST(stepper_marks_and_back) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::Stepper s;
    s.setSteps({{L"One", L"a"}, {L"Two", L"b"}, {L"Three", L"c"}});
    s.setCurrent(1);
    root.add(s, {0, 0, 180, 3 * kit::kStepH});
    auto c = host.paint();
    // The done step: a check mark (no number); the current one: its number on the accent; the later one: an outline.
    CHECK(c.drew("polyline 3 " + C(th.onText)));
    CHECK(c.drew("'2' " + C(th.onText)) && c.drew("'3' " + C(th.dim)) && !c.drew("'1' "));
    CHECK(c.drew("ellipse 0,120,26,146 " + C(th.panel) + " " + C(th.borderStrong)));
    CHECK(c.drew("'Two' " + C(th.text)) && c.drew("'Three' " + C(th.dim)));
    int picked = -1;
    s.onSelect = [&](int i) { picked = i; };
    host.click(60, 10 + 2 * kit::kStepH);  // a later step: nothing
    CHECK(picked == -1);
    host.click(60, 10 + kit::kStepH);  // the current step: nothing
    CHECK(picked == -1);
    host.click(60, 10);  // the done step: back to it
    CHECK(picked == 0);
    CHECK(s.cursor({60, 10}) == draw::Cursor::Hand && s.cursor({60, 10 + 2 * kit::kStepH}) == draw::Cursor::Arrow);
    CHECK(s.stepAt({60, 10 + 3 * kit::kStepH}) == -1);
}
