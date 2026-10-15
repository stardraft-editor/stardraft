#include <string>
#include <vector>

#include "kit_check.h"
#include "rich_field.h"
#include "theme.h"

using namespace stardraft;
using namespace stardraft::kit;
using draw::Key;
using draw::kCtrl;
using draw::kShift;

// Letters are their ASCII capitals in draw::Key.
constexpr Key KA = static_cast<Key>('A'), KC = static_cast<Key>('C'), KV = static_cast<Key>('V'), KX = static_cast<Key>('X'),
              KY = static_cast<Key>('Y'), KZ = static_cast<Key>('Z');

namespace {

// A field in a host, at (0, 0) of the root, `w` by `h` px. Text is 7 px a character, lines 16 px high (the test canvas).
struct Rig {
    draw::View root;
    test::FakeHost host;
    RichField field;
    Rig(int w = 200, int h = 24) : host(root) { root.add(field, {0, 0, w, h}); }
};

RichChip chip(std::wstring label, int kind = 0, uint64_t id = 0) {
    RichChip c;
    c.label = std::move(label);
    c.kind = kind;
    c.id = id;
    return c;
}

// The width of a chip whose label is `n` characters: the label's 7 px a character, 6 px each side and a 2 px gap.
int chipW(int n) { return 7 * n + px(14) + px(2); }

// The caret's model position after a click at the far right of a single line.
void toEnd(Rig& r) {
    r.host.paint();
    r.host.click(190, 10);
}

}  // namespace

KIT_TEST(rich_field_plain_text_edits_and_undoes) {
    Rig r;
    int changes = 0;
    r.field.onChange = [&] { ++changes; };
    r.host.click(20, 10);
    CHECK(r.host.router.focused() == &r.field);
    CHECK(r.host.textInputOn);
    r.host.type("ab");
    CHECK(r.field.plainText() == L"ab" && r.field.caret() == 2);
    CHECK(changes == 1);
    r.host.press(Key::Backspace);
    CHECK(r.field.plainText() == L"a");
    CHECK(changes == 2);
    r.host.press(KZ, kCtrl);
    CHECK(r.field.plainText() == L"ab");
    CHECK(changes == 3);
    r.host.press(KY, kCtrl);
    CHECK(r.field.plainText() == L"a");
    CHECK(changes == 4);
}

KIT_TEST(rich_field_set_parts_is_no_change_and_clears_undo) {
    Rig r;
    int changes = 0;
    r.field.onChange = [&] { ++changes; };
    r.field.setText(L"hello");
    CHECK(changes == 0);
    r.host.click(20, 10);
    r.host.press(KZ, kCtrl);  // nothing to undo
    CHECK(r.field.plainText() == L"hello");
    CHECK(changes == 0);
}

KIT_TEST(rich_field_private_use_characters_are_not_text) {
    Rig r;
    r.field.setText(L"ab");
    CHECK(r.field.plainText() == L"ab");
    CHECK(r.field.chipCount() == 0);
    r.host.click(20, 10);
    r.host.type("\xEE\x80\x80" "c");  // U+E000 typed: dropped
    CHECK(r.field.plainText() == L"abc");
    CHECK(r.field.chipCount() == 0);
}

KIT_TEST(rich_field_insert_chip_counts_one_character) {
    Rig r;
    r.field.setText(L"ab");
    toEnd(r);
    int changes = 0;
    r.field.onChange = [&] { ++changes; };
    CHECK(r.field.insertChip(chip(L"Ally", 1, 42)));
    CHECK(changes == 1);
    CHECK(r.field.plainText() == L"abAlly");
    CHECK(r.field.chipCount() == 1);
    CHECK(r.field.chip(0).id == 42 && r.field.chip(0).kind == 1 && r.field.chip(0).label == L"Ally");
    CHECK(r.field.caret() == 3);  // the chip is one position
    r.host.paint();
    r.host.press(Key::Left);
    CHECK(r.field.caret() == 2);  // before the chip, in one step
    r.host.press(Key::Left);
    CHECK(r.field.caret() == 1);
    r.host.press(Key::Right);
    r.host.press(Key::Right);
    CHECK(r.field.caret() == 3);
    r.host.press(Key::Backspace);  // the whole chip goes
    CHECK(r.field.plainText() == L"ab" && r.field.chipCount() == 0);
    CHECK(r.field.caret() == 2);
    r.host.press(KZ, kCtrl);  // undo brings the chip back, the same chip
    CHECK(r.field.chipCount() == 1 && r.field.chip(0).id == 42);
}

KIT_TEST(rich_field_delete_removes_whole_chip) {
    Rig r;
    r.field.setParts({RichPart::plain(L"xy"), RichPart::of(chip(L"Zed", 0, 3))});
    toEnd(r);
    CHECK(r.field.caret() == 3);
    r.host.press(Key::Home);
    CHECK(r.field.caret() == 0);
    r.host.press(Key::Right);
    r.host.press(Key::Right);
    CHECK(r.field.caret() == 2);
    r.host.press(Key::Delete);
    CHECK(r.field.plainText() == L"xy" && r.field.chipCount() == 0);
}

KIT_TEST(rich_field_selection_copy_takes_labels) {
    Rig r;
    r.field.setParts({RichPart::plain(L"a"), RichPart::of(chip(L"Name", 0, 7)), RichPart::plain(L"b")});
    toEnd(r);
    CHECK(r.field.caret() == 3);
    r.host.press(Key::Left, kShift);  // selects "b"
    r.host.press(Key::Left, kShift);  // and the chip: one position each
    CHECK(r.field.hasSelection() && r.field.anchor() == 3 && r.field.caret() == 1);
    r.host.press(KC, kCtrl);
    CHECK(r.host.clipboard == "Nameb");
    CHECK(r.field.chipCount() == 1);  // Copy changes nothing
}

KIT_TEST(rich_field_cut_and_paste_bring_chips_back_within_the_program) {
    Rig r;
    r.field.setParts({RichPart::plain(L"a"), RichPart::of(chip(L"Name", 0, 7)), RichPart::plain(L"b")});
    toEnd(r);
    r.host.press(Key::Left, kShift);
    r.host.press(Key::Left, kShift);
    r.host.press(KX, kCtrl);
    CHECK(r.field.plainText() == L"a" && r.field.chipCount() == 0);
    CHECK(r.host.clipboard == "Nameb");
    toEnd(r);
    r.host.press(KV, kCtrl);
    CHECK(r.field.plainText() == L"aNameb");
    CHECK(r.field.chipCount() == 1);
    CHECK(r.field.chip(0).id == 7 && r.field.chip(0).label == L"Name");
    // Other text from outside: plain.
    r.host.clipboard = "Zed";
    r.host.press(KV, kCtrl);
    CHECK(r.field.plainText() == L"aNamebZed");
    CHECK(r.field.chipCount() == 1);
}

KIT_TEST(rich_field_paste_of_text_with_a_chip_label_is_text_when_changed) {
    Rig r;
    r.field.setParts({RichPart::of(chip(L"Q", 0, 1))});
    r.host.paint();
    r.host.click(190, 10);  // focus, the caret at the end
    r.host.press(KA, kCtrl);
    r.host.press(KC, kCtrl);
    CHECK(r.host.clipboard == "Q");
    r.host.clipboard = "Q!";  // the clipboard changed: no private form
    toEnd(r);
    r.host.press(KV, kCtrl);
    CHECK(r.field.plainText() == L"QQ!");
    CHECK(r.field.chipCount() == 1);
}

KIT_TEST(rich_field_undo_restores_chip_with_its_id) {
    Rig r;
    r.field.setParts({RichPart::plain(L"x"), RichPart::of(chip(L"Ann", 0, 9))});
    toEnd(r);
    r.host.press(Key::Backspace);
    CHECK(r.field.chipCount() == 0);
    r.host.press(KZ, kCtrl);
    CHECK(r.field.chipCount() == 1 && r.field.chip(0).id == 9);
    r.host.press(KY, kCtrl);
    CHECK(r.field.chipCount() == 0);
}

KIT_TEST(rich_field_chip_click_calls_owner_with_index_and_rect) {
    Rig r;
    r.field.setParts({RichPart::plain(L"ab"), RichPart::of(chip(L"Ann", 1, 5)), RichPart::plain(L"cd")});
    r.host.paint();
    int calls = 0;
    size_t gotIndex = 99;
    draw::Rect gotRect{};
    r.field.onChipClick = [&](size_t index, draw::Rect rect) {
        ++calls;
        gotIndex = index;
        gotRect = rect;
    };
    r.host.click(30, 10);  // the chip: x 20 to 57
    CHECK(calls == 1 && gotIndex == 0);
    // left = 6 (padding) + 14 (ab) + 1; top = 4 (centered line) + 2; right = 20 + chipW(3) - 1; bottom = 4 + 16 - 2.
    CHECK(gotRect.left == 21 && gotRect.right == 20 + chipW(3) - 1 && gotRect.top == 6 && gotRect.bottom == 18);
    CHECK(r.field.caret() == 3);  // after the chip
    r.host.click(8, 10);  // plain text: no call
    CHECK(calls == 1);
}

KIT_TEST(rich_field_chip_index_counts_chips_in_reading_order) {
    Rig r;
    r.field.setParts({RichPart::of(chip(L"A", 0, 1)), RichPart::plain(L"-"), RichPart::of(chip(L"BB", 0, 2))});
    r.host.paint();
    size_t got = 99;
    r.field.onChipClick = [&](size_t index, draw::Rect) { got = index; };
    const int x2 = 6 + chipW(1) + 7 + 4;  // inside the second chip
    r.host.click(x2, 10);
    CHECK(got == 1);
    CHECK(r.field.chip(1).id == 2 && r.field.chip(0).id == 1);
    CHECK(r.field.chip(2).label.empty());  // none there
}

KIT_TEST(rich_field_click_places_caret_by_x) {
    Rig r;
    r.field.setText(L"hello world");
    r.host.paint();
    r.host.click(20, 10);
    CHECK(r.field.caret() == 2 && !r.field.hasSelection());
    r.host.click(190, 10);
    CHECK(r.field.caret() == 11);
    r.host.click(0, 10);
    CHECK(r.field.caret() == 0);
}

KIT_TEST(rich_field_double_click_selects_word) {
    Rig r;
    r.field.setText(L"hello world");
    r.host.paint();
    r.host.doubleClick(20, 10);
    CHECK(r.field.anchor() == 0 && r.field.caret() == 5);
}

KIT_TEST(rich_field_drag_selects_across_chip) {
    Rig r;
    r.field.setParts({RichPart::plain(L"ab"), RichPart::of(chip(L"Ann", 0, 5)), RichPart::plain(L"cd")});
    r.host.paint();
    r.host.down(8, 10);  // before "a"
    r.host.move(190, 10);  // past the end
    CHECK(r.field.anchor() == 0 && r.field.caret() == 5);
    r.host.up(190, 10);
    r.host.move(190, 10);  // the mouse is up: no extension
    CHECK(r.field.caret() == 5);
}

KIT_TEST(rich_field_replace_chip_is_one_undo_step) {
    Rig r;
    r.field.setParts({RichPart::of(chip(L"A", 0, 1))});
    CHECK(r.field.replaceChip(0, chip(L"Bee", 2, 9)));
    CHECK(r.field.chipCount() == 1 && r.field.chip(0).label == L"Bee" && r.field.chip(0).id == 9);
    CHECK(!r.field.replaceChip(3, chip(L"x")));  // no such chip
    r.host.click(20, 10);
    r.host.paint();
    r.host.press(KZ, kCtrl);
    CHECK(r.field.chip(0).label == L"A" && r.field.chip(0).id == 1);
}

KIT_TEST(rich_field_insert_chip_replaces_selection) {
    Rig r;
    r.field.setText(L"abc");
    r.host.paint();
    r.host.click(20, 10);
    r.host.press(KA, kCtrl);
    CHECK(r.field.insertChip(chip(L"Q", 0, 4)));
    CHECK(r.field.plainText() == L"Q" && r.field.chipCount() == 1);
}

KIT_TEST(rich_field_chip_limit) {
    Rig r;
    r.field.setText(L"");
    r.host.click(20, 10);
    for (size_t i = 0; i < RichField::kMaxChips; ++i)
        if (!r.field.insertChip(chip(L"c", 0, i))) {
            CHECK(false);
            break;
        }
    CHECK(r.field.chipCount() == RichField::kMaxChips);
    CHECK(!r.field.insertChip(chip(L"over")));
    CHECK(r.field.chipCount() == RichField::kMaxChips);
}

KIT_TEST(rich_field_max_length_counts_a_chip_as_one) {
    Rig r;
    r.field.setMaxLength(2);
    r.host.click(20, 10);
    CHECK(r.field.insertChip(chip(L"Qqqq", 0, 1)));
    r.host.type("ab");
    CHECK(r.field.plainText() == L"Qqqqa");
    CHECK(r.field.chipCount() == 1);
    CHECK(!r.field.insertChip(chip(L"no")));  // the field is full
}

KIT_TEST(rich_field_read_only_takes_no_edits_but_copies) {
    Rig r;
    r.field.setParts({RichPart::plain(L"a"), RichPart::of(chip(L"Q", 0, 3))});
    r.field.setReadOnly(true);
    CHECK(r.field.readOnly());
    r.host.click(20, 10);
    r.host.type("x");
    r.host.press(Key::Backspace);
    r.host.press(Key::Delete);
    CHECK(!r.field.insertChip(chip(L"w")));
    CHECK(!r.field.replaceChip(0, chip(L"w")));
    CHECK(r.field.plainText() == L"aQ");
    r.host.press(KA, kCtrl);
    r.host.press(KC, kCtrl);
    CHECK(r.host.clipboard == "aQ");
}

KIT_TEST(rich_field_enter_in_single_and_multi_line) {
    Rig r;
    int enters = 0;
    r.field.onEnter = [&] { ++enters; };
    r.host.click(20, 10);
    r.host.press(Key::Enter);  // one line: runs onEnter
    CHECK(enters == 1 && r.field.plainText().empty());

    r.field.setMultiline(true);
    r.host.type("a");
    r.host.press(Key::Enter);  // a new line
    CHECK(enters == 1 && r.field.plainText() == L"a\n");
    r.host.press(Key::Enter, kCtrl);  // Ctrl+Enter runs onEnter
    CHECK(enters == 2 && r.field.plainText() == L"a\n");
}

KIT_TEST(rich_field_escape_and_placeholder) {
    Rig r;
    int escapes = 0;
    r.field.onEscape = [&] { ++escapes; };
    r.field.setPlaceholder(L"Name");
    auto c = r.host.paint();
    CHECK(c.texts().size() == 1 && c.texts()[0] == "Name");
    r.host.click(20, 10);
    r.host.press(Key::Escape);
    CHECK(escapes == 1);
    r.host.type("A");
    c = r.host.paint();
    for (const auto& t : c.texts()) CHECK(t != "Name");
}

KIT_TEST(rich_field_up_down_keep_the_column) {
    Rig r(200, 60);
    r.field.setMultiline(true);
    r.field.setText(L"ab\ncd");
    r.host.paint();
    r.host.click(20, 10);  // focus: line 0
    r.host.paint();
    r.host.press(Key::Home, kCtrl);
    CHECK(r.field.caret() == 0);
    r.host.press(Key::Down);
    CHECK(r.field.caret() == 3);  // column 0 of the second line
    r.host.press(Key::End);
    CHECK(r.field.caret() == 5);
    r.host.press(Key::Up);  // the end of the first line, before its newline (the column was reset by End)
    CHECK(r.field.caret() == 2);
    r.host.press(Key::Down, kShift);
    CHECK(r.field.anchor() == 2 && r.field.caret() == 5);
}

KIT_TEST(rich_field_home_and_end_of_a_line) {
    Rig r;
    r.field.setText(L"hello");
    toEnd(r);
    CHECK(r.field.caret() == 5);
    r.host.press(Key::Home);
    CHECK(r.field.caret() == 0);
    r.host.press(Key::End);
    CHECK(r.field.caret() == 5);
    r.host.press(Key::Left, kCtrl);
    CHECK(r.field.caret() == 0);
}

KIT_TEST(rich_field_wraps_at_the_width_and_breaks_after_spaces) {
    Rig r(200, 100);  // all three lines in view (the caret at the end would scroll a shorter field)
    r.field.setMultiline(true);
    r.field.setText(L"one two three four five six seven eight nine ten eleven twelve");
    const auto c = r.host.paint();
    // The text is 188 px wide inside the field's padding: 26 characters a line, broken after a space.
    const std::vector<std::string> want = {"one two three four five ", "six seven eight nine ten ", "eleven twelve"};
    const auto got = c.texts();
    CHECK(got.size() == want.size());
    for (size_t i = 0; i < want.size() && i < got.size(); ++i) CHECK(got[i] == want[i]);
}

KIT_TEST(rich_field_no_wrap_scrolls_sideways) {
    Rig r;
    r.field.setMultiline(true);
    r.field.setWrap(false);
    r.field.setText(L"0123456789012345678901234567890123456789");  // 280 px: wider than the field's 188 px
    r.host.paint();
    r.host.click(20, 10);
    r.host.press(Key::End);
    r.host.paint();
    // The caret at the end is in view: scrolled by 280 + 2 - 188 = 94 px, the caret is at 6 - 94 + 280 = 192.
    CHECK(r.field.scrollX() == 94);
    CHECK(r.host.caret.left == 192 && r.host.caret.right == 193);
}

KIT_TEST(rich_field_wheel_scrolls_lines) {
    Rig r(200, 24);
    r.field.setMultiline(true);
    r.field.setText(L"l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9");
    r.host.paint();  // setText puts the caret at the end, and the view scrolls to it
    r.host.click(20, 10);
    r.host.press(Key::Home, kCtrl);  // the start of the text: the view scrolls back to the top
    auto c = r.host.paint();
    CHECK(c.drew("'l0'"));
    r.host.wheel(20, 10, -1.0f);  // down: three lines
    c = r.host.paint();
    CHECK(!c.drew("'l0'"));
    CHECK(c.drew("'l3'"));
}

KIT_TEST(rich_field_spans_color_plain_text_only) {
    Rig r;
    r.field.setText(L"abcdef");
    r.field.setSpans({Span{0, 3, draw::rgb(255, 0, 0), false}});
    auto c = r.host.paint();
    CHECK(c.drew("'abc' " + test::RecordingCanvas::c(draw::rgb(255, 0, 0))));
    CHECK(c.drew("'def' " + test::RecordingCanvas::c(theme().text)));
}

KIT_TEST(rich_field_chip_look_and_label) {
    Rig r;
    r.field.setParts({RichPart::of(chip(L"Ann", 0, 1))});
    auto c = r.host.paint();
    const Theme& th = theme();
    CHECK(c.drew("roundBox 7,6,42,18"));  // the chip's box, after the field's own
    CHECK(c.drew(test::RecordingCanvas::c(th.chip)));
    CHECK(c.drew(test::RecordingCanvas::c(th.chipLine)));
    bool label = false;
    for (const auto& t : c.texts())
        if (t == "Ann") label = true;
    CHECK(label);
}

KIT_TEST(rich_field_ime_composition_then_commit) {
    Rig r;
    r.field.setText(L"a");
    r.host.click(190, 10);
    r.host.compose("ni", 2);
    auto c = r.host.paint();
    bool shown = false;
    for (const auto& t : c.texts())
        if (t == "ni") shown = true;
    CHECK(shown);
    CHECK(r.field.plainText() == L"a");  // not in the text until it is committed
    r.host.compose("", 0);
    r.host.type("\xE4\xBD\xA0");  // U+4F60, committed
    CHECK(r.field.plainText() == L"a你");
}

KIT_TEST(rich_field_compose_with_a_chip_before_it) {
    Rig r;
    r.field.setParts({RichPart::of(chip(L"Q", 0, 1))});
    r.host.click(190, 10);
    r.host.compose("x", 1);
    r.host.paint();
    CHECK(r.field.plainText() == L"Q");
    r.host.compose("", 0);
    r.host.type("y");
    CHECK(r.field.plainText() == L"Qy" && r.field.chipCount() == 1);
}

KIT_TEST(rich_field_set_parts_round_trip) {
    Rig r;
    std::vector<RichPart> in = {RichPart::plain(L"a "), RichPart::of(chip(L"B", 3, 77)), RichPart::plain(L" c")};
    r.field.setParts(in);
    const auto out = r.field.parts();
    CHECK(out.size() == 3);
    if (out.size() == 3) {
        CHECK(!out[0].chip && out[0].text == L"a ");
        CHECK(out[1].chip && out[1].value.kind == 3 && out[1].value.id == 77 && out[1].value.label == L"B");
        CHECK(!out[2].chip && out[2].text == L" c");
    }
    CHECK(r.field.plainText() == L"a B c");
}

KIT_TEST(rich_field_undo_is_not_across_setParts) {
    Rig r;
    r.field.setParts({RichPart::of(chip(L"A", 0, 1))});
    r.host.click(190, 10);
    r.host.press(KZ, kCtrl);
    CHECK(r.field.chipCount() == 1);
}
