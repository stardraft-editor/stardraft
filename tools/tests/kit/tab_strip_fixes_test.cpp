#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kit_check.h"
#include "tab_strip.h"

using namespace stardraft;
using namespace stardraft::kit::test;

// Segmented, three buttons over 400 px: the parts are 130, 130 and 132, with a gap of 4 px after the first two. So the
// boxes are [0, 130), [134, 264) and [268, 400).

KIT_TEST(tab_strip_segmented_text_is_centered_in_its_button) {
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts(kit::TabLook::Segmented);
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    auto c = host.paint();
    // No close mark on a segment: the text box is the button less its 10 px sides. The middle button's text box is 144
    // to 254, centered at 199, the middle of its button (134 to 264).
    CHECK(c.drew("text 144,0,254,30 'B'"));
    CHECK(c.drew("text 10,0,120,30 'A'"));
    // A closable segment keeps room for its x: the x is 16 px, 5 px in from the box's end (243 to 259 for the box 134
    // to 264), and the text ends 2 px before it, at 241.
    kit::Tab closable;
    closable.text = L"B";
    closable.closable = true;
    ts.setTabs({{L"A"}, closable, {L"C"}});
    c = host.paint();
    CHECK(c.drew("text 144,0,241,30 'B'"));
    CHECK(!c.drew("text 144,0,254,30 'B'"));
}

KIT_TEST(tab_strip_segmented_every_button_clickable) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> selected;
    kit::TabStrip ts(kit::TabLook::Segmented);
    ts.onSelect = [&](int i) { selected.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    host.paint();
    host.click(136, 15);  // the second button's first pixel
    host.click(131, 15);  // the gap after the first button: it belongs to the first
    host.click(399, 15);  // the last pixel
    CHECK(selected == std::vector<int>({1, 0, 2}));
    host.click(264, 15);  // the gap after the second button: the second, which is not chosen now
    CHECK(selected.back() == 1);
}

KIT_TEST(tab_strip_every_tab_reachable_when_squeezed) {
    draw::View root;
    FakeHost host(root);
    std::vector<int> selected;
    kit::TabStrip ts;
    ts.onSelect = [&](int i) { selected.push_back(i); };
    root.add(ts, {0, 0, 400, 30});
    std::vector<kit::Tab> tabs;
    for (int i = 0; i < 12; ++i) tabs.push_back({L"Document name"});
    ts.setTabs(tabs);
    host.paint();
    // Twelve tabs in 400 px: each 33 px, no scrolling, so none is clipped. Tab 11 spans [363, 396), tab 10 [330, 363).
    host.click(380, 15);
    CHECK(ts.selected() == 11 && selected == std::vector<int>({11}));
    host.click(340, 15);
    CHECK(ts.selected() == 10 && selected.back() == 10);
    host.click(396, 15);  // past the last tab: nothing
    CHECK(ts.selected() == 10);
}

// The handlers below destroy the strip that holds them, so they capture nothing: a closure lives in the strip and must
// not be read after the strip is gone. The state they use is static.

KIT_TEST(tab_strip_destroyed_by_its_close_handler) {
    static kit::TabStrip* strip = nullptr;
    static int closed = 0;
    strip = new kit::TabStrip;
    closed = 0;
    draw::View root;
    FakeHost host(root);
    strip->onClose = [](int) {
        ++closed;
        delete strip;  // the owner closed its last document: the strip goes with it
        strip = nullptr;
    };
    root.add(*strip, {0, 0, 400, 30});
    strip->setTabs({{L"A", -1, true}, {L"B"}});
    host.paint();
    host.click(77, 15);  // the x of tab 0: the release calls onClose, which destroys the strip
    CHECK(closed == 1 && strip == nullptr);
    CHECK(host.router.captured() == nullptr);
    host.move(20, 15);  // nothing is left to take the event
    CHECK(closed == 1);
}

KIT_TEST(tab_strip_destroyed_by_its_middle_click_handler) {
    static kit::TabStrip* strip = nullptr;
    static int closed = 0;
    strip = new kit::TabStrip;
    closed = 0;
    draw::View root;
    FakeHost host(root);
    strip->onClose = [](int) {
        ++closed;
        delete strip;
        strip = nullptr;
    };
    root.add(*strip, {0, 0, 400, 30});
    strip->setTabs({{L"A", -1, true}});
    host.paint();
    host.down(20, 15, 2), host.up(20, 15, 2);
    CHECK(closed == 1 && strip == nullptr);
}

KIT_TEST(tab_strip_destroyed_by_its_select_handler) {
    static kit::TabStrip* strip = nullptr;
    static int selected = 0;
    strip = new kit::TabStrip;
    selected = 0;
    draw::View root;
    FakeHost host(root);
    strip->onSelect = [](int) {
        ++selected;
        delete strip;
        strip = nullptr;
    };
    root.add(*strip, {0, 0, 400, 30});
    strip->setTabs({{L"A"}, {L"B"}});
    host.paint();
    host.click(120, 15);  // tab 1: the press chooses it, and the owner destroys the strip
    CHECK(selected == 1 && strip == nullptr);
    host.up(120, 15);
    host.move(20, 15);
    CHECK(host.router.captured() == nullptr);
}

KIT_TEST(tab_strip_drag_ends_when_the_capture_is_lost) {
    draw::View root;
    FakeHost host(root);
    std::vector<std::pair<int, int>> moves;
    kit::TabStrip ts;
    ts.onMove = [&](int from, int to) { moves.push_back({from, to}); };
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    host.paint();
    host.down(20, 15);  // A is pressed
    host.router.setCapture(nullptr);  // the system took the mouse (Alt+Tab): the release never comes to the strip
    host.move(250, 15);  // a plain move over the strip: no drag
    host.move(40, 15);
    CHECK(moves.empty());
    CHECK(host.paint().texts() == std::vector<std::string>({"A", "B", "C"}));
    host.up(40, 15);  // the late release does nothing
    CHECK(moves.empty() && ts.selected() == 0);
    host.click(100, 15);  // a new press works as before
    CHECK(ts.selected() == 1);
}

KIT_TEST(tab_strip_owner_reorder_in_onSelect_ends_the_drag) {
    draw::View root;
    FakeHost host(root);
    int moves = 0;
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    // An owner that lists the tabs most recently used first: it reorders them when a tab is chosen.
    ts.onSelect = [&](int i) {
        std::vector<kit::Tab> t = ts.tabs();
        std::rotate(t.begin(), t.begin() + i, t.begin() + i + 1);
        ts.setTabs(t);
        ts.select(0);  // the chosen tab is now the first
    };
    ts.onMove = [&](int, int) { ++moves; };
    ts.setTabs({{L"A"}, {L"B"}, {L"C"}});
    host.paint();
    host.down(250, 15);  // C is chosen, and the owner moves it to the front
    host.paint();
    host.move(40, 15);  // the mouse moves on: nothing is dragged (the tab now at index 2 is B)
    host.up(40, 15);
    CHECK(moves == 0);
    CHECK(host.paint().texts() == std::vector<std::string>({"C", "A", "B"}));
    CHECK(ts.selected() == 0);
}

KIT_TEST(tab_strip_close_mark_is_the_times_sign_in_utf8) {
    draw::View root;
    FakeHost host(root);
    kit::TabStrip ts;
    root.add(ts, {0, 0, 400, 30});
    ts.setTabs({{L"A", -1, true}});
    auto c = host.paint();
    // U+00D7 as UTF-8 (C3 97): the mark is written as an escape, so no compiler reads the source in another encoding.
    CHECK(c.drew("'\xC3\x97'"));
    CHECK(c.drew("'" + utf8(L"\u00d7") + "'"));
}
