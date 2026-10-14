#include <initializer_list>
#include <string>
#include <vector>

#include "list_view.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit::test;

namespace {

// The rows are 26 px (kRowH). A list 104 px high shows four rows; the taller ones show ten.
constexpr int kRow = 26;
constexpr auto kKeyA = static_cast<draw::Key>('A');  // letters are their ASCII capitals

std::vector<int> ints(std::initializer_list<int> l) { return l; }

std::vector<std::wstring> sampleNames() { return {L"Apple", L"Apricot", L"Banana", L"Blueberry", L"Cherry", L"apple pie"}; }

}  // namespace

KIT_TEST(list_single_selection) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 260});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(10);
    int changes = 0;
    list.onSelect = [&] { ++changes; };
    host.click(10, 2 * kRow + 5);
    CHECK(list.selection() == ints({2}));
    CHECK(list.cursor() == 2);
    CHECK(changes == 1);
    host.click(10, 2 * kRow + 5);  // the same row again: no change
    CHECK(changes == 1);
    host.click(10, 4 * kRow + 5, draw::kCtrl);  // single mode: Ctrl selects only, as a click does
    CHECK(list.selection() == ints({4}));
    CHECK(changes == 2);
    host.click(10, 3 * kRow + 5);
    CHECK(list.selection() == ints({3}));
}

KIT_TEST(list_multiple_selection_rules) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 260});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setMode(kit::SelectMode::Multiple);
    list.setCount(10);
    host.click(10, 1 * kRow + 5);
    CHECK(list.selection() == ints({1}));
    host.click(10, 3 * kRow + 5, draw::kCtrl);  // Ctrl+click adds
    CHECK(list.selection() == ints({1, 3}));
    host.click(10, 3 * kRow + 5, draw::kCtrl);  // and toggles off
    CHECK(list.selection() == ints({1}));
    host.click(10, 3 * kRow + 5, draw::kCtrl);  // the anchor is row 3 (the last Ctrl+click)
    CHECK(list.selection() == ints({1, 3}));
    host.click(10, 5 * kRow + 5, draw::kShift);  // Shift+click: the rows from the anchor (3) to 5; the rest goes
    CHECK(list.selection() == ints({3, 4, 5}));
    host.click(10, 0 * kRow + 5, draw::kShift);  // the anchor stays at 3
    CHECK(list.selection() == ints({0, 1, 2, 3}));
    host.click(10, 7 * kRow + 5, draw::kCtrl | draw::kShift);  // Ctrl+Shift keeps the others
    CHECK(list.selection() == ints({0, 1, 2, 3, 4, 5, 6, 7}));
    host.click(10, 9 * kRow + 5);  // a plain click: one row
    CHECK(list.selection() == ints({9}));
    CHECK(host.press(kKeyA, draw::kCtrl));  // Ctrl+A: all rows
    CHECK(list.selection().size() == 10);
}

KIT_TEST(list_ctrl_a_only_in_multiple) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(10);
    host.click(10, 5);
    CHECK(!host.press(kKeyA, draw::kCtrl));
    CHECK(list.selection() == ints({0}));
}

KIT_TEST(list_keyboard_moves_and_selects) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    std::vector<int> acts;
    list.onActivate = [&](int i) { acts.push_back(i); };
    list.setCount(10);
    host.click(10, 5);  // row 0; the list has the focus
    CHECK(host.router.focused() == &list);
    CHECK(host.press(draw::Key::Down));
    CHECK(list.selection() == ints({1}));
    host.press(draw::Key::Down);
    CHECK(list.selection() == ints({2}) && list.cursor() == 2);
    host.press(draw::Key::End);
    CHECK(list.selection() == ints({9}));
    host.press(draw::Key::Home);
    CHECK(list.selection() == ints({0}));
    host.press(draw::Key::PageDown);  // a page is the visible rows: 104 / 26 = 4
    CHECK(list.selection() == ints({4}));
    host.press(draw::Key::PageUp);
    CHECK(list.selection() == ints({0}));
    host.press(draw::Key::Up);  // stops at the first row
    CHECK(list.selection() == ints({0}));
    host.press(draw::Key::Down, draw::kCtrl);  // single mode: Ctrl changes nothing
    CHECK(list.selection() == ints({1}));
    CHECK(host.press(draw::Key::Enter));
    CHECK(acts == ints({1}));
}

KIT_TEST(list_keyboard_multiple) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setMode(kit::SelectMode::Multiple);
    list.setCount(10);
    host.click(10, kRow + 5);  // row 1
    host.press(draw::Key::Down, draw::kShift);  // extends from the anchor
    CHECK(list.selection() == ints({1, 2}));
    host.press(draw::Key::End, draw::kShift);
    CHECK(list.selection().size() == 9 && list.cursor() == 9);
    host.press(draw::Key::Home, draw::kCtrl);  // Ctrl: the cursor moves, the selection stays
    CHECK(list.cursor() == 0);
    CHECK(list.selection().size() == 9);
    CHECK(host.press(draw::Key::Space));  // Space toggles the cursor's row
    CHECK(list.selection().size() == 10);
    host.press(draw::Key::Space);
    CHECK(list.selection().size() == 9 && !list.isSelected(0));
}

KIT_TEST(list_empty_keys_do_nothing) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    int acts = 0;
    list.onActivate = [&](int) { ++acts; };
    list.setCount(0);
    list.focus();
    CHECK(!host.press(draw::Key::Down));
    CHECK(!host.press(draw::Key::Enter));
    CHECK(!host.press(draw::Key::Space));
    host.click(10, 10);  // nothing to click
    CHECK(list.selection().empty() && list.cursor() == -1 && acts == 0);
}

KIT_TEST(list_type_ahead) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    const auto names = sampleNames();
    list.label = [&](int i) { return names[size_t(i)]; };
    list.setCount(int(names.size()));
    list.focus();
    host.type("b");  // no cursor yet: the search starts at the first row
    CHECK(list.selection() == ints({2}));
    host.type("l");  // "bl": Blueberry
    CHECK(list.selection() == ints({3}));
    host.type("u");  // "blu": still Blueberry
    CHECK(list.selection() == ints({3}));
    host.type("x");  // "blux": no row; nothing moves
    CHECK(list.selection() == ints({3}));
    host.runTimers();  // a pause ends the search
    host.type("a");  // "a" from Blueberry on: "apple pie"
    CHECK(list.selection() == ints({5}));
    host.runTimers();
    host.press(draw::Key::Home);
    host.type("APR");  // case does not matter: Apricot
    CHECK(list.selection() == ints({1}));
    host.runTimers();
    host.press(draw::Key::Home);
    host.type("apple");  // "Apple" first
    CHECK(list.selection() == ints({0}));
    host.press(draw::Key::Space);  // a space in the search: "apple " is "apple pie"
    CHECK(list.selection() == ints({5}));
}

KIT_TEST(list_type_ahead_cycles_one_letter) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    const auto names = sampleNames();
    list.label = [&](int i) { return names[size_t(i)]; };
    list.setCount(int(names.size()));
    list.focus();
    host.type("a");  // Apple
    CHECK(list.selection() == ints({0}));
    host.type("a");  // "aa": the next row that starts with a: Apricot
    CHECK(list.selection() == ints({1}));
    host.type("a");  // then "apple pie"
    CHECK(list.selection() == ints({5}));
    host.type("a");  // and back to the first
    CHECK(list.selection() == ints({0}));
}

KIT_TEST(list_type_ahead_timer_resets_search) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    const auto names = sampleNames();
    list.label = [&](int i) { return names[size_t(i)]; };
    list.setCount(int(names.size()));
    list.focus();
    host.type("b");
    CHECK(list.selection() == ints({2}));
    host.runTimers();  // the buffer is cleared: "a" alone, from Banana on, finds "apple pie"
    host.type("a");
    CHECK(list.selection() == ints({5}));
}

KIT_TEST(list_type_ahead_none_when_empty) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.setCount(0);
    list.focus();
    CHECK(!host.router.text({"a"}));
    list.setCount(3);  // with rows (no label: they match nothing) the letter is taken
    CHECK(host.router.text({"a"}));
}

KIT_TEST(list_scroll_by_wheel_three_rows_a_notch) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(40);  // 1040 px of rows in 104 px: the largest scroll is 936
    CHECK(list.scrollBar().scrollable());
    host.wheel(50, 50, -1);  // down a notch
    CHECK(list.scrollTop() == 3 * kRow);
    CHECK(list.scrollBar().pos() == 3 * kRow);  // the bar follows
    host.wheel(50, 50, 1);  // up
    CHECK(list.scrollTop() == 0);
    host.wheel(50, 50, -100);  // stops at the end
    CHECK(list.scrollTop() == 936);
    host.wheel(50, 50, 1);
    CHECK(list.scrollTop() == 936 - 3 * kRow);
    CHECK(list.selection().empty());  // the wheel selects nothing
    host.wheel(50, 50, -1);
    CHECK(list.scrollTop() == 936);
    host.wheel(50, 50, 0.5f);  // half a notch up: 39 px
    CHECK(list.scrollTop() == 936 - 39);
}

KIT_TEST(list_ensure_visible) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(40);
    list.ensureVisible(20);  // row 20 is 520..546: the bottom edge shows it
    CHECK(list.scrollTop() == 546 - 104);
    list.ensureVisible(19);  // already in view: nothing moves
    CHECK(list.scrollTop() == 546 - 104);
    list.ensureVisible(3);  // above: the top edge
    CHECK(list.scrollTop() == 3 * kRow);
    list.ensureVisible(-1);  // out of range: nothing
    list.ensureVisible(99);
    CHECK(list.scrollTop() == 3 * kRow);
}

KIT_TEST(list_keyboard_end_scrolls_to_cursor) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(40);
    list.focus();
    host.press(draw::Key::End);
    CHECK(list.cursor() == 39);
    CHECK(list.scrollTop() == 936);
    CHECK(list.rowAt({10, 100}) == 39);
}

KIT_TEST(list_scroll_bar_drag_and_track) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(40);
    // The bar is at 188..200; the thumb is 24 px (the share, 10 px, is under the minimum); the travel is 80 px.
    host.down(194, 10);  // on the thumb: a drag from its top
    host.move(194, 50);  // the thumb's start moves 40 px: 40 * 936 / 80 = 468
    CHECK(list.scrollTop() == 468);
    host.up(194, 50);
    list.setScrollTop(0);
    host.click(194, 90);  // below the thumb (0..24): one page of 104 px
    CHECK(list.scrollTop() == 104);
    CHECK(list.scrollBar().pos() == 104);
    CHECK(list.selection().empty());  // the bar takes the click, not a row
}

KIT_TEST(list_scroll_bar_hidden_when_it_fits) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(3);
    CHECK(!list.scrollBar().scrollable());
    CHECK(list.rowRect(0).right == 200);  // the rows use the whole width
    host.wheel(50, 50, -1);
    CHECK(list.scrollTop() == 0);
}

KIT_TEST(list_hit_testing_after_scroll) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {20, 30, 220, 134});  // the list's origin is (20, 30) in the root
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(40);
    list.setScrollTop(104);  // row 4 is at the top now
    CHECK(list.rowAt({10, 5}) == 4);
    CHECK(list.rowAt({10, 30}) == 5);
    CHECK(list.rowRect(4).top == 0 && list.rowRect(4).bottom == kRow);
    CHECK(list.rowRect(4).right == 188);  // the scroll bar's width is taken off
    host.click(30, 30 + 5);  // root (30, 35) is the list's (10, 5): row 4
    CHECK(list.selection() == ints({4}));
    list.setScrollTop(10);  // row 0 is partly above the top edge
    CHECK(list.rowAt({10, 5}) == 0);
    CHECK(list.rowAt({10, 17}) == 1);
    CHECK(list.rowAt({10, 110}) == -1);  // below the list
    CHECK(list.rowAt({10, 500}) == -1);
}

KIT_TEST(list_header_columns) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.setColumns({{L"Name", 0, kit::ColAlign::Left}, {L"Size", 60, kit::ColAlign::Right}, {L"Kind", 80, kit::ColAlign::Center}});
    list.label = [](int i) { return L"n" + std::to_wstring(i); };
    list.cell = [](int i, size_t k) { return k == 1 ? std::to_wstring(i * 10) : std::wstring(L"kind"); };
    list.setCount(3);
    // The header is 26 px. The first column takes what the others leave: 200 - 12 - 60 - 80 = 48 px.
    CHECK(list.rowRect(0).top == 26 && list.rowRect(0).bottom == 52);
    CHECK(list.rowAt({10, 10}) == -1);  // the header is not a row
    CHECK(list.rowAt({10, 30}) == 0);
    auto c = host.paint();
    CHECK(c.drew("text 10,0,50,26 'Name' "));  // the first column is 6..54, its text 4 px in
    CHECK(c.drew("text 58,0,110,26 'Size' "));  // the second is 54..114
    CHECK(c.drew("fill 0,25,200,26 "));  // the header's line
    CHECK(c.drew("text 58,26,110,52 '0' "));  // row 0, column Size (from the cell callback)
    CHECK(c.drew("text 58,52,110,78 '10' "));
    CHECK(c.drew("text 10,26,50,52 'n0' "));  // column Name: the label
    host.click(10, 10);  // the header: no row
    CHECK(list.selection().empty());
    host.click(10, 30);
    CHECK(list.selection() == ints({0}));
}

KIT_TEST(list_header_scrolls_under_rows) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.setColumns({{L"A", 0, kit::ColAlign::Left}});
    list.label = [](int i) { return std::to_wstring(i); };
    list.setCount(20);
    list.setScrollTop(2 * kRow);  // the header stays; the rows move under it
    CHECK(list.rowRect(2).top == 26);  // row 2 is first in the rows' area, right under the header
    CHECK(list.rowAt({10, 26}) == 2);
    CHECK(list.rowAt({10, 10}) == -1);
}

KIT_TEST(list_empty_hint) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setEmptyHint(L"No files yet");
    auto c = host.paint();
    CHECK(c.texts() == std::vector<std::string>{"No files yet"});
    // The hint's box: 16 px in from the sides, 16 px high and centered: (104 - 16) / 2 = 44.
    CHECK(c.drew("text 16,44,184,60 'No files yet' "));
    CHECK(c.drew("0x949494"));  // dim
    list.setCount(2);
    CHECK(!host.paint().drew("No files yet"));
    list.setCount(0);
    CHECK(host.paint().drew("'No files yet' "));
    list.setEmptyHint(L"");
    CHECK(host.paint().texts().empty());
}

KIT_TEST(list_empty_hint_below_header) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.setColumns({{L"Name", 0, kit::ColAlign::Left}});
    list.setEmptyHint(L"No files yet");
    // The area under the 26 px header is 78 px: (78 - 16) / 2 = 31, so the box is 57..73.
    CHECK(host.paint().drew("text 16,57,184,73 'No files yet' "));
}

KIT_TEST(list_default_row_look) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(2);
    host.click(10, 5);  // row 0 selected: the fill and the 2 px accent bar
    host.move(10, 40);  // row 1 under the mouse
    auto c = host.paint();
    CHECK(c.drew("fill 0,0,200,26 "));
    CHECK(c.drew("fill 0,0,2,26 "));
    CHECK(c.drew("fill 0,26,200,52 "));  // the hot row
    CHECK(c.drew("text 8,0,192,26 'Row 0' "));  // the text starts 8 px in
    CHECK(c.drew("text 8,26,192,52 'Row 1' "));
}

KIT_TEST(list_custom_row_paint_and_states) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.setCount(10);
    std::vector<std::string> seen;
    list.paintRow = [&](draw::Canvas&, int i, draw::Rect, kit::RowState s) {
        seen.push_back(std::to_string(i) + (s.selected ? "s" : "") + (s.hot ? "h" : "") + (s.focused ? "f" : ""));
    };
    host.click(10, kRow + 5);  // row 1 selected: the cursor, and the list has the focus
    host.move(10, 3 * kRow + 5);  // row 3 under the mouse
    seen.clear();
    auto c = host.paint();
    CHECK(c.texts().empty());  // the custom paint replaces the default text
    auto has = [&](const std::string& s) {
        for (const auto& x : seen)
            if (x == s) return true;
        return false;
    };
    CHECK(has("1sf"));
    CHECK(has("3h"));
    CHECK(has("0"));
    CHECK(!has("1sh") && !has("2h") && !has("3sf"));
    CHECK(seen.size() == 4);  // the four rows that show
}

KIT_TEST(list_focus_frame_on_cursor) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setMode(kit::SelectMode::Multiple);
    list.setCount(4);
    host.click(10, 5);  // row 0 selected
    host.press(draw::Key::Down, draw::kCtrl);  // the cursor moves to row 1; the selection stays
    CHECK(list.selection() == ints({0}));
    CHECK(list.cursor() == 1);
    CHECK(host.paint().drew("frame 0,26,200,52 "));  // the focus ring is on the cursor's row
    list.setRowHeight(20);
    CHECK(list.rowHeight() == 20);
    CHECK(list.rowRect(1).top == 20 && list.rowRect(1).bottom == 40);
    CHECK(list.rowAt({10, 25}) == 1);
}

KIT_TEST(list_context_menu_right_click) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {20, 30, 220, 134});  // the list's origin is (20, 30) in the root
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(10);
    int row = -2;
    draw::Point at{-1, -1};
    list.onContext = [&](int i, draw::Point p) { row = i, at = p; };
    host.down(30, 30 + 3 * kRow + 5, 1);
    host.up(30, 30 + 3 * kRow + 5, 1);
    CHECK(row == 3);
    CHECK(at.x == 10 && at.y == 3 * kRow + 5);  // in the list's coordinates
    CHECK(list.selection() == ints({3}));  // it was not selected: it is now
    list.setMode(kit::SelectMode::Multiple);
    host.click(30, 30 + 5);  // row 0
    host.click(30, 30 + 2 * kRow + 5, draw::kCtrl);  // rows 0 and 2
    host.down(30, 30 + 2 * kRow + 5, 1);  // a right click on a selected row keeps the selection
    CHECK(list.selection() == ints({0, 2}));
    host.down(30, 30 + 3 * kRow + 5, 1);  // an unselected row: selected alone
    CHECK(list.selection() == ints({3}));
    list.setCount(2);
    host.down(30, 30 + 100, 1);  // below the rows
    CHECK(row == -1);
}

KIT_TEST(list_double_click_and_ctrl_pair) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 260});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setMode(kit::SelectMode::Multiple);
    list.setCount(10);
    std::vector<int> acts;
    list.onActivate = [&](int i) { acts.push_back(i); };
    host.doubleClick(10, 2 * kRow + 5);
    CHECK(acts == ints({2}));
    CHECK(list.selection() == ints({2}));
    // A Ctrl double click: the first click toggles row 4 on; the second must not toggle it back.
    host.down(10, 4 * kRow + 5, 0, draw::kCtrl);
    host.up(10, 4 * kRow + 5, 0, draw::kCtrl);
    draw::MouseEvent e;
    e.kind = draw::MouseKind::DoubleClick;
    e.pt = {10, 4 * kRow + 5};
    e.mods = draw::kCtrl;
    host.router.mouse(e);
    CHECK(acts.back() == 4);
    CHECK(list.selection() == ints({2, 4}));
}

KIT_TEST(list_enter_activates_cursor) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(5);
    std::vector<int> acts;
    list.onActivate = [&](int i) { acts.push_back(i); };
    CHECK(!host.press(draw::Key::Enter));  // no cursor yet
    host.click(10, 3 * kRow + 5);
    CHECK(host.press(draw::Key::Enter));
    CHECK(acts == ints({3}));
}

KIT_TEST(list_set_count_keeps_and_drops) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setCount(10);
    list.selectOnly(5);
    list.setCount(20);  // growing keeps the selection
    CHECK(list.selection() == ints({5}));
    list.setCount(3);  // the rows past the count go; the cursor moves to the last row
    CHECK(list.selection().empty());
    CHECK(list.cursor() == 2);
}

KIT_TEST(list_mode_switch_keeps_one) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setMode(kit::SelectMode::Multiple);
    list.setCount(10);
    list.selectAll();
    list.toggle(3);  // off
    list.toggle(3);  // and on again: the cursor's row is selected
    list.setMode(kit::SelectMode::Single);
    CHECK(list.selection() == ints({3}));
    list.clearSelection();
    CHECK(list.selection().empty());
}

KIT_TEST(list_empty_paints_nothing) {
    draw::View root;
    FakeHost host(root);
    kit::ListView list;
    root.add(list, {0, 0, 200, 104});
    list.label = [](int i) { return L"Row " + std::to_wstring(i); };
    list.setRowHeight(20);
    CHECK(list.rowHeight() == 20);
    list.setCount(0);
    CHECK(host.paint().texts().empty());  // no hint and no rows
}
