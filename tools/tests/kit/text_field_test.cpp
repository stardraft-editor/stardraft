#include <sstream>
#include <string>
#include <vector>

#include "button.h"
#include "kit_check.h"
#include "text_field.h"

using namespace stardraft;
using namespace stardraft::kit;
using draw::Key;
using draw::kCtrl;
using draw::kShift;

// Letters are their ASCII capitals in draw::Key.
constexpr Key KA = static_cast<Key>('A'), KC = static_cast<Key>('C'), KV = static_cast<Key>('V'), KX = static_cast<Key>('X'),
              KY = static_cast<Key>('Y'), KZ = static_cast<Key>('Z');

namespace {

struct Box {
    int l = 0, t = 0, r = 0, b = 0;
};

// The 1 px carets painted in the text color (a fill of width 1 in that color).
std::vector<Box> carets(const test::RecordingCanvas& c) {
    std::vector<Box> out;
    const std::string tail = " " + test::RecordingCanvas::c(theme().text) + " 255";
    for (const std::string& op : c.ops) {
        if (op.rfind("fill ", 0) != 0 || op.size() < tail.size() || op.compare(op.size() - tail.size(), tail.size(), tail) != 0) continue;
        Box b;
        std::istringstream in(op.substr(5));  // "l,t,r,b color alpha"
        char comma = 0;
        in >> b.l >> comma >> b.t >> comma >> b.r >> comma >> b.b;
        if (!in.fail() && b.r - b.l == 1) out.push_back(b);
    }
    return out;
}

// A field in a host, at (0, 0) of the root, `w` by `h` px.
struct Rig {
    draw::View root;
    test::FakeHost host;
    TextField field;
    Rig(int w = 200, int h = 24) : host(root) { root.add(field, {0, 0, w, h}); }
};

// What the field's text looks like to a mouse, in px: x of character `i` of a single line (7 px each, 6 px inset).
int xOf(int i) { return 6 + 7 * i; }

}  // namespace

KIT_TEST(text_field_types_and_undoes) {
    Rig r;
    int changes = 0;
    r.field.onChange = [&] { ++changes; };
    r.host.click(20, 10);
    CHECK(r.host.router.focused() == &r.field);
    CHECK(r.host.textInputOn);
    r.host.type("abc");
    CHECK(r.field.text() == L"abc" && r.field.model().caret() == 3);
    CHECK(changes == 1);
    r.host.press(Key::Backspace);
    CHECK(r.field.text() == L"ab");
    r.host.press(Key::Left);
    r.host.type("X");
    CHECK(r.field.text() == L"aXb" && r.field.model().caret() == 2);
    CHECK(changes == 3);
    r.host.press(KZ, kCtrl);  // undo the X
    CHECK(r.field.text() == L"ab");
    r.host.press(KZ, kCtrl);  // undo the Backspace
    CHECK(r.field.text() == L"abc");
    r.host.press(KZ, kCtrl);  // undo the typing
    CHECK(r.field.text().empty());
    CHECK(r.host.press(KZ, kCtrl | kShift));  // redo: Ctrl+Shift+Z
    CHECK(r.field.text() == L"abc");
    CHECK(r.host.press(KY, kCtrl));  // redo: Ctrl+Y
    CHECK(r.field.text() == L"ab");
    CHECK(changes == 8);
}

KIT_TEST(text_field_set_text_is_no_change) {
    Rig r;
    int changes = 0;
    r.field.onChange = [&] { ++changes; };
    r.field.setText(L"hello");
    CHECK(changes == 0);
    r.host.click(20, 10);
    r.host.press(KZ, kCtrl);  // nothing to undo: the history was cleared
    CHECK(r.field.text() == L"hello");
    CHECK(changes == 0);
}

KIT_TEST(text_field_placeholder) {
    Rig r;
    r.field.setPlaceholder(L"Name");
    auto c = r.host.paint();
    CHECK(c.texts().size() == 1 && c.texts()[0] == "Name");
    CHECK(c.drew(test::RecordingCanvas::c(theme().faint)));
    r.host.click(20, 10);
    r.host.type("A");
    c = r.host.paint();
    for (const auto& t : c.texts()) CHECK(t != "Name");
}

KIT_TEST(text_field_click_places_caret) {
    Rig r;
    r.field.setText(L"hello world");
    r.host.paint();
    r.host.click(xOf(2), 10);
    CHECK(r.field.model().caret() == 2 && !r.field.model().hasSelection());
    r.host.click(190, 10);  // past the end
    CHECK(r.field.model().caret() == 11);
    r.host.click(0, 10);  // before the start (the inset)
    CHECK(r.field.model().caret() == 0);
    r.host.paint();
    auto cs = carets(r.host.paint());
    CHECK(cs.size() == 1 && cs[0].l == xOf(0));
}

KIT_TEST(text_field_drag_selects_with_capture) {
    Rig r;
    r.field.setText(L"hello world");
    r.host.paint();
    r.host.down(xOf(0), 10);
    r.host.move(xOf(4), 10);
    r.host.up(xOf(4), 10);
    CHECK(r.field.model().selectedText() == L"hell");
    CHECK(r.field.model().anchor() == 0 && r.field.model().caret() == 4);
    // Held and dragged out of the field: still the same drag, to the end.
    r.host.down(xOf(2), 10);
    r.host.move(900, 10);
    CHECK(r.field.model().selectedText() == L"llo world");
    r.host.up(900, 10);
    r.host.move(900, 10);  // not held any more: no change
    CHECK(r.field.model().selectedText() == L"llo world");
    CHECK(r.field.cursor({1, 1}) == draw::Cursor::IBeam);
}

KIT_TEST(text_field_shift_click_extends) {
    Rig r;
    r.field.setText(L"hello world");
    r.host.paint();
    r.host.click(xOf(0), 10);
    r.host.click(xOf(4), 10, kShift);
    CHECK(r.field.model().selectedText() == L"hell");
}

KIT_TEST(text_field_double_click_selects_word) {
    Rig r;
    r.field.setText(L"foo bar");
    r.host.paint();
    r.host.doubleClick(xOf(5), 10);
    CHECK(r.field.model().selectedText() == L"bar");
    r.host.doubleClick(xOf(1), 10);
    CHECK(r.field.model().selectedText() == L"foo");
}

KIT_TEST(text_field_triple_click_selects_line) {
    Rig r(200, 80);
    r.field.setMultiline(true);
    r.field.setText(L"one\ntwo\nthree");
    r.host.paint();
    const int y = 25;  // the second line: its top is 19
    r.host.doubleClick(xOf(1), y);
    CHECK(r.field.model().selectedText() == L"two");
    r.host.down(xOf(1), y);  // the third click, at the same spot, right after
    r.host.up(xOf(1), y);
    CHECK(r.field.model().selectedText() == L"two");
    CHECK(r.field.model().selectionStart() == 4 && r.field.model().selectionEnd() == 7);
    r.host.click(xOf(1), y);  // a fresh click after a triple places the caret
    CHECK(!r.field.model().hasSelection());
}

KIT_TEST(text_field_ctrl_arrows_and_home_end) {
    Rig r;
    r.field.setText(L"foo bar baz");
    r.host.paint();
    r.host.click(190, 10);  // focuses it; past the end: the caret goes to the end
    CHECK(r.field.model().caret() == 11);
    r.host.press(Key::Left, kCtrl);
    CHECK(r.field.model().caret() == 8);
    r.host.press(Key::Left, kCtrl);
    CHECK(r.field.model().caret() == 4);
    r.host.press(Key::Left, kCtrl);
    CHECK(r.field.model().caret() == 0);
    r.host.press(Key::Right, kCtrl);
    CHECK(r.field.model().caret() == 4);
    r.host.press(Key::End);
    CHECK(r.field.model().caret() == 11);
    r.host.press(Key::Home);
    CHECK(r.field.model().caret() == 0);
    r.host.press(Key::End, kShift);
    CHECK(r.field.model().selectedText() == L"foo bar baz");
    r.host.press(Key::Left);  // without Shift: the selection goes to its start
    CHECK(r.field.model().caret() == 0 && !r.field.model().hasSelection());
    r.host.press(Key::Right, kShift | kCtrl);
    CHECK(r.field.model().selectedText() == L"foo ");
    r.host.press(Key::Home, kCtrl);
    CHECK(r.field.model().caret() == 0);
    r.host.press(Key::End, kCtrl);
    CHECK(r.field.model().caret() == 11);
}

KIT_TEST(text_field_ctrl_delete_words) {
    Rig r;
    r.host.click(20, 10);
    r.host.type("foo bar");
    r.host.press(Key::Backspace, kCtrl);
    CHECK(r.field.text() == L"foo ");
    r.host.press(Key::Backspace, kCtrl);
    CHECK(r.field.text().empty());
    r.host.type("a b");
    r.host.press(Key::Home);
    r.host.press(Key::Delete, kCtrl);
    CHECK(r.field.text() == L"b");
}

KIT_TEST(text_field_enter_and_escape_single_line) {
    Rig r;
    int enters = 0, escapes = 0;
    r.field.setText(L"abc");
    r.host.click(20, 10);
    CHECK(r.host.press(Key::Escape) == false);  // no onEscape: the parents get it
    r.field.onEnter = [&] { ++enters; };
    r.field.onEscape = [&] { ++escapes; };
    CHECK(r.host.press(Key::Enter));
    CHECK(enters == 1 && r.field.text() == L"abc");
    CHECK(r.host.press(Key::Escape));
    CHECK(escapes == 1 && r.field.text() == L"abc");
    r.host.type("\n");  // a newline does not get into a line
    CHECK(r.field.text() == L"abc");
}

KIT_TEST(text_field_tab_leaves_single_line) {
    draw::View root;
    test::FakeHost host(root);
    TextField field;
    kit::Button after(L"OK", kit::Tone::Normal, nullptr);
    root.add(field, {0, 0, 100, 24});
    root.add(after, {110, 0, 200, 24});
    host.click(10, 10);
    CHECK(host.router.focused() == &field);
    CHECK(host.press(Key::Tab));
    CHECK(host.router.focused() == &after);
}

KIT_TEST(text_field_multiline_enter_and_vertical_keys) {
    Rig r(200, 100);
    r.field.setMultiline(true);
    int enters = 0;
    r.field.onEnter = [&] { ++enters; };
    r.host.click(20, 10);
    r.host.type("abcdef");
    r.host.press(Key::Enter);
    r.host.type("gh");
    CHECK(r.field.text() == L"abcdef\ngh" && enters == 0);
    r.host.paint();
    CHECK(r.field.model().caret() == 9);
    r.host.press(Key::Up);  // column 2 (x of "gh|")
    CHECK(r.field.model().caret() == 2);
    r.host.press(Key::Down);
    CHECK(r.field.model().caret() == 9);
    r.host.press(Key::Down);  // the last line: to the end
    CHECK(r.field.model().caret() == 9);
    r.host.press(Key::Up);
    r.host.press(Key::Up);  // the first line: to the start
    CHECK(r.field.model().caret() == 0);
}

KIT_TEST(text_field_vertical_keys_fall_back_without_layout) {
    Rig r(200, 100);
    r.field.setMultiline(true);
    r.field.setText(L"abcdef\nxy");
    r.host.click(20, 10);  // paints nothing: the layout is not there yet
    r.host.press(Key::End, kCtrl);
    CHECK(r.field.model().caret() == 9);
    r.host.press(Key::Up);  // the column 2 of "xy" (9 - 7), in the line before
    CHECK(r.field.model().caret() == 2);
}

KIT_TEST(text_field_page_keys_and_shift_vertical) {
    Rig r(200, 100);  // 94 px inside: 5 lines of 16
    r.field.setMultiline(true);
    r.field.setText(L"l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    r.host.paint();
    r.host.click(6, 10);  // focuses it; the first line's start
    r.host.press(Key::Home, kCtrl);
    CHECK(r.field.model().caret() == 0);
    r.host.press(Key::PageDown);
    CHECK(r.field.model().caret() == 15);  // line 5 starts at 15 (each line is 3 units with its newline)
    r.host.press(Key::PageUp);
    CHECK(r.field.model().caret() == 0);
    r.host.press(Key::Down, kShift);
    CHECK(r.field.model().selectedText() == L"l0\n");
    r.host.press(Key::PageUp, kShift);  // at the top already: to the start, the selection stays
    CHECK(r.field.model().caret() == 0 && r.field.model().anchor() == 0);
}

KIT_TEST(text_field_wrap_at_words) {
    Rig r(61, 100);  // 49 px inside: 7 characters
    r.field.setMultiline(true);
    r.field.setText(L"hello world foo");
    auto c = r.host.paint();
    CHECK(c.texts().size() == 3);
    CHECK(c.texts()[0] == "hello " && c.texts()[1] == "world " && c.texts()[2] == "foo");
    r.host.click(xOf(0), 3 + 2 * 16 + 4);  // the start of "foo": the line below the second
    CHECK(r.field.model().caret() == 12);
    auto cs = carets(r.host.paint());
    CHECK(cs.size() == 1 && cs[0].l == 6 && cs[0].t == 3 + 2 * 16);
}

KIT_TEST(text_field_wrap_follows_width) {
    Rig r(61, 100);
    r.field.setMultiline(true);
    r.field.setText(L"hello world foo");
    CHECK(r.host.paint().texts().size() == 3);
    r.field.setSize(200, 100);  // the layout is made again at the new width
    CHECK(r.host.paint().texts().size() == 1);
}

KIT_TEST(text_field_wrap_off_scrolls_sideways) {
    Rig r(61, 100);
    r.field.setMultiline(true);
    r.field.setWrap(false);
    r.field.setText(L"hello world foo");
    r.field.model().setCaret(0);
    auto c = r.host.paint();
    CHECK(c.texts().size() == 1 && c.texts()[0] == "hello world foo");
    CHECK(r.field.scrollX() == 0);
    r.host.click(50, 10);  // inside the field, at the start of the text's far end
    r.host.press(Key::End);
    CHECK(r.field.model().caret() == 15);
    r.host.paint();
    CHECK(r.field.scrollX() == 7 * 15 + 2 - 49);  // the caret's own px and 2 px of room
}

KIT_TEST(text_field_single_line_scrolls_to_caret) {
    Rig r(60, 24);  // 48 px inside: 6 characters
    r.host.click(20, 10);
    r.host.type(std::string(30, 'a'));
    auto c = r.host.paint();
    CHECK(r.field.scrollX() == 7 * 30 + 2 - 48);
    auto cs = carets(c);
    CHECK(cs.size() == 1 && cs[0].l == 6 + 7 * 30 - r.field.scrollX());
    CHECK(cs[0].l >= 6 && cs[0].l < 54);
    r.host.press(Key::Home);
    c = r.host.paint();
    CHECK(r.field.scrollX() == 0);
    CHECK(carets(c).size() == 1 && carets(c)[0].l == 6);
    // The wheel does not take the caret back out of view while it stays where it is.
    r.host.wheel(20, 10, -3);
    CHECK(r.field.scrollX() == 0);
}

KIT_TEST(text_field_multiline_scrolls_to_caret_and_wheel) {
    Rig r(200, 60);  // 54 px inside: 3 lines
    r.field.setMultiline(true);
    r.host.click(20, 10);
    r.field.setText(L"l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    r.host.press(Key::End, kCtrl);
    CHECK(r.field.model().caret() == 29);
    r.host.paint();
    CHECK(r.field.scrollY() == 9 * 16 + 16 - 54);
    r.host.wheel(50, 20, 2);
    CHECK(r.field.scrollY() == 106 - 96);
    r.host.paint();  // the caret did not move: the wheel's place stays
    CHECK(r.field.scrollY() == 10);
    r.host.wheel(50, 20, -100);
    CHECK(r.field.scrollY() == 106);
    r.host.wheel(50, 20, -1);  // at the end: nothing to scroll
    CHECK(r.field.scrollY() == 106);
    r.host.wheel(50, 20, 100);
    CHECK(r.field.scrollY() == 0);
}

KIT_TEST(text_field_scroll_bar_only_when_overflowing) {
    Rig r(200, 60);
    r.field.setMultiline(true);
    r.field.setText(L"a\nb");
    CHECK(!r.host.paint().drew(test::RecordingCanvas::c(theme().thumb)));
    r.field.setText(L"l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    CHECK(r.host.paint().drew(test::RecordingCanvas::c(theme().thumb)));
}

KIT_TEST(text_field_read_only) {
    Rig r;
    r.field.setText(L"abc");
    r.field.setReadOnly(true);
    CHECK(r.host.paint().drew(test::RecordingCanvas::c(theme().readOnly)));
    r.host.click(20, 10);
    r.host.type("x");
    r.host.press(Key::Backspace);
    r.host.press(Key::Delete);
    r.host.press(KV, kCtrl);
    CHECK(r.field.text() == L"abc");
    r.host.press(KA, kCtrl);
    CHECK(r.field.model().selectedText() == L"abc");
    r.host.press(KC, kCtrl);
    CHECK(r.host.clipboard == "abc");
    r.host.press(KX, kCtrl);
    CHECK(r.field.text() == L"abc" && r.host.clipboard == "abc");
    r.host.press(KZ, kCtrl);
    CHECK(r.field.text() == L"abc");
}

KIT_TEST(text_field_max_length_and_filter) {
    Rig r;
    r.field.setMaxLength(3);
    r.host.click(20, 10);
    r.host.type("abcdef");
    CHECK(r.field.text() == L"abc");
    r.field.setMaxLength(0);
    r.field.setText(L"");
    r.field.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
    r.host.type("1a2");
    CHECK(r.field.text() == L"12");
    r.host.clipboard = "x9y8";
    r.host.press(KV, kCtrl);  // a paste goes through the filter too
    CHECK(r.field.text() == L"1298");
}

KIT_TEST(text_field_clipboard) {
    Rig r;
    r.field.setText(L"one two");
    r.host.click(20, 10);
    r.host.press(KA, kCtrl);
    r.host.press(KC, kCtrl);
    CHECK(r.host.clipboard == "one two");
    r.field.model().setSelection(4, 7);
    r.host.clipboard = "NEW";
    r.host.press(KV, kCtrl);
    CHECK(r.field.text() == L"one NEW" && r.field.model().caret() == 7);
    r.field.model().setSelection(4, 7);
    r.host.press(KX, kCtrl);
    CHECK(r.field.text() == L"one " && r.host.clipboard == "NEW");
    r.host.clipboard = "\xC3\xA9\xF0\x9F\x98\x80";  // e acute and a code point outside the BMP
    r.host.press(KV, kCtrl);
    CHECK(r.field.text() == fromUtf8("one \xC3\xA9\xF0\x9F\x98\x80"));
    r.host.press(Key::Backspace);  // the whole code point goes at once
    CHECK(r.field.text() == fromUtf8("one \xC3\xA9"));
    r.host.press(KZ, kCtrl);  // undo the Backspace
    CHECK(r.field.text() == fromUtf8("one \xC3\xA9\xF0\x9F\x98\x80"));
    r.host.press(KZ, kCtrl);  // undo the paste: one step
    CHECK(r.field.text() == L"one ");
}

KIT_TEST(text_field_paste_newlines_by_mode) {
    Rig r(200, 100);
    r.field.setMultiline(true);
    r.host.click(20, 10);
    r.host.clipboard = "a\r\nb";
    r.host.press(KV, kCtrl);
    CHECK(r.field.text() == L"a\nb");
    r.field.setMultiline(false);
    r.field.setText(L"");
    r.host.press(KV, kCtrl);
    CHECK(r.field.text() == L"ab");
}

KIT_TEST(text_field_ime_composition) {
    Rig r;
    r.field.setText(L"ab");
    r.host.click(xOf(2), 10);
    r.host.paint();
    r.host.compose("ni", 2);
    CHECK(r.field.text() == L"ab");  // not in the buffer
    auto c = r.host.paint();
    CHECK(c.drew("'ni'"));
    CHECK(carets(c).size() == 1 && carets(c)[0].l == xOf(4));  // after the composition
    r.host.compose("\xE4\xBD\xA0", 3);  // one character, the caret after it
    c = r.host.paint();
    CHECK(r.field.text() == L"ab");
    CHECK(carets(c).size() == 1 && carets(c)[0].l == xOf(3));
    // The commit comes as text: it is the buffer's, and undo does not know the composition.
    r.host.compose("", 0);
    r.host.type("\xE4\xBD\xA0");
    CHECK(r.field.text() == fromUtf8("ab\xE4\xBD\xA0"));
    c = r.host.paint();
    CHECK(!c.drew("'ni'"));
    CHECK(r.host.press(KZ, kCtrl));
    CHECK(r.field.text() == L"ab");
}

KIT_TEST(text_field_ime_cancel_and_focus) {
    Rig r;
    r.host.click(20, 10);
    CHECK(r.host.textInputOn);
    r.host.compose("ka", 2);
    CHECK(r.host.paint().drew("'ka'"));
    r.host.compose("", 0);  // the composition ended without a commit
    CHECK(!r.host.paint().drew("'ka'") && r.field.text().empty());
    r.host.compose("kb", 2);
    r.field.focusChanged(false);  // the focus goes: the composition goes too
    CHECK(!r.host.textInputOn);
}

KIT_TEST(text_field_spans) {
    Rig r;
    r.field.setText(L"abc def");
    const draw::Color red = draw::rgb(255, 0, 0), blue = draw::rgb(0, 0, 255);
    r.field.setSpans({Span{4, 3, blue, true}, Span{0, 3, red, false}});  // given out of order: sorted
    auto c = r.host.paint();
    CHECK(c.texts().size() == 3);
    CHECK(c.texts()[0] == "abc" && c.texts()[1] == " " && c.texts()[2] == "def");
    CHECK(c.drew("'abc' " + test::RecordingCanvas::c(red)));
    CHECK(c.drew("'def' " + test::RecordingCanvas::c(blue)));
    CHECK(!c.drew("' ' " + test::RecordingCanvas::c(red)));
    r.field.setSpans({});
    CHECK(!r.host.paint().drew(test::RecordingCanvas::c(red)));
}

KIT_TEST(text_field_selection_color_and_fill) {
    Rig r;
    r.field.setText(L"hello");
    r.host.paint();
    r.field.model().selectAll();
    auto c = r.host.paint();
    CHECK(c.drew("fill 6,4,41,20 " + test::RecordingCanvas::c(theme().sel)));  // the five letters, 7 px each
    CHECK(c.drew("'hello' " + test::RecordingCanvas::c(theme().onText)));
}

KIT_TEST(text_field_caret_blinks) {
    Rig r;
    r.field.setText(L"x");
    r.host.click(20, 10);
    CHECK(carets(r.host.paint()).size() == 1);
    r.host.runTimers();  // off
    CHECK(carets(r.host.paint()).empty());
    r.host.runTimers();  // on
    CHECK(carets(r.host.paint()).size() == 1);
    r.host.runTimers();  // off, and a key shows it on again
    r.host.type("y");
    CHECK(carets(r.host.paint()).size() == 1);
}

KIT_TEST(text_field_focus_lost_callback) {
    draw::View root;
    test::FakeHost host(root);
    TextField field;
    kit::Button other(L"Other", kit::Tone::Normal, nullptr);
    root.add(field, {0, 0, 90, 24});
    root.add(other, {100, 0, 180, 24});
    int lost = 0;
    field.onFocusLost = [&] { ++lost; };
    host.click(10, 10);
    CHECK(host.router.focused() == &field && host.textInputOn);
    host.click(120, 10);
    CHECK(host.router.focused() == &other);
    CHECK(lost == 1);
    CHECK(!host.textInputOn);
    CHECK(carets(host.paint()).empty());
}

KIT_TEST(text_field_changed_look) {
    Rig r;
    r.field.setText(L"x");
    CHECK(!r.host.paint().drew(test::RecordingCanvas::c(theme().goodFill)));
    r.field.setChanged(true);
    CHECK(r.host.paint().drew(test::RecordingCanvas::c(theme().goodFill)));
}

KIT_TEST(text_field_offset_in_parent) {
    draw::View root;
    test::FakeHost host(root);
    TextField field;
    root.add(field, {10, 20, 210, 44});
    field.setText(L"hello");
    host.paint();
    host.click(10 + 6 + 14, 20 + 10);  // the caret at 2, in the root's coordinates
    CHECK(field.model().caret() == 2);
    CHECK(host.router.focused() == &field);
}

KIT_TEST(text_field_destroyed_with_blink_pending) {
    draw::View root;
    test::FakeHost host(root);
    {
        TextField field;
        root.add(field, {0, 0, 100, 24});
        host.click(10, 10);  // the caret's timer is running
    }
    host.runTimers();  // the timer finds nothing to do
    CHECK(host.router.focused() == nullptr);
}

KIT_TEST(text_field_surrogate_caret_keeps_pairs) {
    Rig r;
    r.host.click(20, 10);
    r.host.type("\xF0\x9F\x98\x80" "b");  // one code point, then b
    CHECK(r.field.text().size() == (sizeof(wchar_t) == 2 ? 3u : 2u));
    r.host.press(Key::Left);
    r.host.press(Key::Left);
    CHECK(r.field.model().caret() == 0);
    r.host.paint();
    r.host.press(Key::Right);
    CHECK(r.field.model().caret() == (sizeof(wchar_t) == 2 ? 2u : 1u));
}
