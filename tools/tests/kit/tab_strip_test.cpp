#include <utility>
#include <vector>

#include "kit_check.h"
#include "tab_strip.h"

using namespace stardraft;
using namespace stardraft::kit::test;

static std::string C(draw::Color c) { return RecordingCanvas::c(c); }

// In the Tabs look, a tab of one letter is 90 px wide (the minimum): tab i spans [90 i, 90 i + 90). Its x is at
// 90 i + 69 .. 90 i + 85 (the close), centered at y 15 in a 30 px strip.

KIT_TEST(tab_strip_paints_tabs_and_selects) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> selected;
    kit::TabStrip ts;
    ts.onSelect = [&](int i) { selected.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    CHECK(ts.count() == 3 && ts.selected() == 0);
    auto c = host.paint();
    CHECK(c.texts() == std::vector<std::string>({"A", "B", "C"}));
    CHECK(c.drew("fill 0,29,400,30 " + C(kit::theme().border) + " 255"));  // the line under the flat tabs
    host.click(100, 15);  // tab 1
    CHECK(ts.selected() == 1 && selected == std::vector<int>({1}));
    host.click(100, 15);  // the chosen tab again: no event
    CHECK(selected.size() == 1);
    host.click(250, 15);  // tab 2
    CHECK(ts.selected() == 2 && selected.back() == 2);
    CHECK(host.router.focused() == &ts);
    // The keys: Left and Right move (no wrap at the ends), Home and End go to the ends.
    CHECK(host.press(draw::Key::Left) && ts.selected() == 1);
    CHECK(host.press(draw::Key::Left) && ts.selected() == 0);
    CHECK(host.press(draw::Key::Left) && ts.selected() == 0);
    CHECK(host.press(draw::Key::Right) && ts.selected() == 1);
    CHECK(host.press(draw::Key::End) && ts.selected() == 2);
    CHECK(host.press(draw::Key::Right) && ts.selected() == 2);
    CHECK(host.press(draw::Key::Home) && ts.selected() == 0);
    CHECK(!host.press(draw::Key::Enter));
    CHECK(selected.back() == 0);
}

KIT_TEST(tab_strip_before_first_paint_has_no_tabs_to_hit) {
    draw::View root;
    FakeHost host(root);
    int events = 0;
    kit::TabStrip ts;
    ts.onSelect = [&](int) { ++events; };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}});
    CHECK(ts.tip({100, 15}) == L"");
    host.click(100, 15);  // the places come with the paint
    CHECK(events == 0 && ts.selected() == 0);
}

KIT_TEST(tab_strip_close_and_middle_click) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> closed;
    kit::TabStrip ts;
    ts.onClose = [&](int i) { closed.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A", -1, true}, {L"B", -1, true}, {L"C"}});
    host.paint();
    host.click(167, 15);  // the x of tab 1
    CHECK(closed == std::vector<int>({1}));
    CHECK(ts.selected() == 0);  // closing a tab does not choose it
    host.click(77, 15);  // the x of tab 0
    CHECK(closed == std::vector<int>({1, 0}));
    host.click(257, 15);  // tab 2 has no x: a click chooses it
    CHECK(closed.size() == 2 && ts.selected() == 2);
    host.paint();
    host.down(20, 15, 2), host.up(20, 15, 2);  // a middle click on a closable tab closes it
    CHECK(closed == std::vector<int>({1, 0, 0}));
}

KIT_TEST(tab_strip_middle_click_closes_closable_only) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> closed;
    kit::TabStrip ts;
    ts.onClose = [&](int i) { closed.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B", -1, true}});
    host.paint();
    host.down(20, 15, 2), host.up(20, 15, 2);  // A is not closable
    CHECK(closed.empty());
    host.down(100, 15, 2), host.up(100, 15, 2);  // B is
    CHECK(closed == std::vector<int>({1}));
    // Pressing the x and letting go on another tab is no close.
    host.down(167, 15), host.up(20, 15);
    CHECK(closed.size() == 1);
}

KIT_TEST(tab_strip_changed_dot_and_x_on_hover) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A", -1, true}, {L"B"}});
    ts.setChanged(0, true);
    auto c = host.paint();
    // Unsaved: a dot (8 px, centered in the x's place) in the tab's text color (the tab is chosen).
    CHECK(c.drew("ellipse 73,11,82,20 " + C(th.text) + " none"));
    CHECK(!c.drew("'" + utf8(L"\u00d7") + "'"));
    host.move(20, 15);  // the mouse on the tab: the x shows instead of the dot
    c = host.paint();
    CHECK(!c.drew("ellipse"));
    CHECK(c.drew("'" + utf8(L"\u00d7") + "' " + C(th.dim) + " 5"));
    host.move(77, 15);  // on the x: its box lights up
    CHECK(host.paint().drew("roundBox 69,7,85,23 " + C(th.hot) + " none 4 1"));
    ts.setChanged(0, false);
    CHECK(host.paint().drew("'" + utf8(L"\u00d7") + "' " + C(th.text) + " 5"));
    ts.setChanged(5, true);  // no such tab: nothing
    CHECK(ts.count() == 2);
}

KIT_TEST(tab_strip_drag_moves_tabs) {
    draw::View root;
    FakeHost host(root);
    std::vector<std::pair<int, int>> moves;
    kit::TabStrip ts;
    ts.onMove = [&](int from, int to) { moves.push_back({from, to}); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    host.paint();
    host.down(20, 15);  // A
    host.move(140, 15);  // past the middle of B (135): A moves after B
    CHECK(moves.size() == 1 && moves[0] == std::make_pair(0, 1));
    CHECK(ts.selected() == 1);  // the selection stays on A
    host.move(150, 15);  // still in its place
    CHECK(moves.size() == 1);
    host.move(40, 15);  // past the middle of B's new place: A moves before B
    CHECK(moves.size() == 2 && moves[1] == std::make_pair(1, 0));
    host.up(40, 15);
    CHECK(ts.selected() == 0);
    CHECK(host.paint().texts() == std::vector<std::string>({"A", "B", "C"}));
    // Moving after the release: nothing.
    host.move(300, 15);
    CHECK(moves.size() == 2);
}

KIT_TEST(tab_strip_drag_moves_the_order_of_the_tabs) {
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    host.paint();
    host.down(250, 15);  // C, from the last place
    host.move(30, 15);  // to the front
    host.up(30, 15);
    CHECK(host.paint().texts() == std::vector<std::string>({"C", "A", "B"}));
    CHECK(ts.selected() == 0);
}

KIT_TEST(tab_strip_small_move_is_a_click) {
    draw::View root;
    FakeHost host(root);
    int moves = 0;
    kit::TabStrip ts;
    ts.onMove = [&](int, int) { ++moves; };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}});
    host.paint();
    host.down(20, 15), host.move(23, 15), host.up(23, 15);  // within the slop: no drag
    CHECK(moves == 0 && ts.selected() == 0);
    host.down(100, 15), host.move(20, 15), host.up(20, 15);  // B dragged past the middle of A: B moves before A
    CHECK(moves == 1);
    CHECK(host.paint().texts() == std::vector<std::string>({"B", "A"}));
    CHECK(ts.selected() == 0);
}

KIT_TEST(tab_strip_segmented_look) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    std::vector<int> selected;
    kit::TabStrip ts(kit::TabLook::Segmented);
    ts.onSelect = [&](int i) { selected.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    CHECK(ts.look() == kit::TabLook::Segmented);
    auto c = host.paint();
    // Buttons 4 px apart (CONTROLS.md): the inner 392 px in three parts (130, 130, 132), each with the gap after it
    // except the last. The chosen one is Primary (the accent fill and outline); the others are normal buttons.
    CHECK(c.drew("roundBox 0,0,130,30 " + C(th.sel) + " " + C(th.accentLine) + " 4 1"));
    CHECK(c.drew("roundBox 134,0,264,30 " + C(th.ctl) + " " + C(th.border) + " 4 1"));
    CHECK(c.drew("roundBox 268,0,400,30 " + C(th.ctl) + " " + C(th.border) + " 4 1"));
    CHECK(!c.drew("fill "));  // no line between the buttons
    CHECK(c.texts() == std::vector<std::string>({"A", "B", "C"}));
    CHECK(c.drew("text 10,0,120,30 'A' " + C(th.accent)));  // the chosen one's name in the accent
    host.click(200, 15);  // the middle segment
    CHECK(selected == std::vector<int>({1}));
    c = host.paint();
    CHECK(c.drew("roundBox 134,0,264,30 " + C(th.sel) + " " + C(th.accentLine) + " 4 1"));
    CHECK(c.drew("roundBox 0,0,130,30 " + C(th.ctl) + " " + C(th.border) + " 4 1"));
    host.click(350, 15);  // the last
    CHECK(selected.back() == 2);
}

KIT_TEST(tab_strip_icon_and_text_place) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    kit::Tab t;
    t.text = L"A";
    t.icon = 5;
    t.closable = true;
    ts.setTabs({t});
    auto c = host.paint();
    // A tab with an x: the icon at the tab's left, 16 px, centered up and down; the text after 5 px; the text ends
    // before the x's place.
    CHECK(c.drew("icon 10,7,26,23 5 " + C(th.text)));
    CHECK(c.drew("text 31,0,67,30 'A' " + C(th.text) + " 12"));
}

KIT_TEST(tab_strip_text_centered_without_close) {
    const auto& th = kit::theme();
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}});
    auto c = host.paint();
    // A tab with no x and no dot (a fixed page: Program options): its text in the middle of its whole box (90 px wide).
    CHECK(c.drew("text 10,0,80,30 'A' " + C(th.text) + " " + std::to_string(draw::kCenter | draw::kMiddle | draw::kEllipsis)));
    CHECK(c.drew("text 100,0,170,30 'B' " + C(th.dim) + " " + std::to_string(draw::kCenter | draw::kMiddle | draw::kEllipsis)));
}

KIT_TEST(tab_strip_cursor_and_tip) {
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    kit::Tab a;
    a.text = L"A";
    a.tip = L"Doc one";
    ts.setTabs({a, {L"B"}});
    host.paint();
    CHECK(ts.cursor({20, 15}) == draw::Cursor::Hand);
    CHECK(ts.cursor({300, 15}) == draw::Cursor::Arrow);
    CHECK(ts.tip({20, 15}) == L"Doc one");
    CHECK(ts.tip({100, 15}) == L"");
    CHECK(ts.tip({300, 15}) == L"");
}

KIT_TEST(tab_strip_setTabs_keeps_selection_in_range) {
    draw::View root;
    FakeHost host(root);
    int events = 0;
    kit::TabStrip ts;
    ts.onSelect = [&](int) { ++events; };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    CHECK(ts.selected() == 0);
    ts.select(2);
    CHECK(ts.selected() == 2 && events == 0);
    ts.select(7);  // no such tab
    CHECK(ts.selected() == 2);
    ts.setTabs({{L"A"}, {L"B"}});
    CHECK(ts.selected() == 1);
    ts.setTabs({});
    CHECK(ts.selected() == -1 && ts.count() == 0);
    host.paint();
    CHECK(!host.press(draw::Key::Left));  // no tabs: the keys are not for it
    CHECK(events == 0);
}

KIT_TEST(tab_strip_squeezed_when_too_many) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> selected;
    kit::TabStrip ts;
    ts.onSelect = [&](int i) { selected.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    std::vector<kit::Tab> tabs;
    for (int i = 0; i < 8; ++i) tabs.push_back({L"Document name"});
    ts.setTabs(tabs);
    host.paint();
    // Eight tabs do not fit in 400 px: each is 50 px (400 / 8), so tab 2 spans [100, 150).
    host.click(120, 15);
    CHECK(ts.selected() == 2 && selected == std::vector<int>({2}));
}
