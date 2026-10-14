#include "text_model.h"
#include "kit_check.h"

using namespace stardraft;
using namespace stardraft::kit;

namespace {

// The wide string of a code point, as the platform's wchar_t holds it (a pair of units where wchar_t has 16 bits).
std::wstring codePoint(uint32_t cp) {
    std::wstring w;
    if (cp > 0xFFFF && sizeof(wchar_t) == 2) {
        cp -= 0x10000;
        w += wchar_t(0xD800 + (cp >> 10));
        w += wchar_t(0xDC00 + (cp & 0x3FF));
    } else {
        w += wchar_t(cp);
    }
    return w;
}

}  // namespace

KIT_TEST(text_model_insert_erase) {
    TextModel m;
    CHECK(m.insert(L"hello"));
    CHECK(m.text() == L"hello" && m.caret() == 5 && !m.hasSelection());
    CHECK(m.insert(L"") == false);
    m.setCaret(0);
    CHECK(m.insert(L"> "));
    CHECK(m.text() == L"> hello" && m.caret() == 2);
    m.setCaret(m.size());
    CHECK(m.backspace());
    CHECK(m.text() == L"> hell");
    m.setCaret(0);
    CHECK(m.deleteForward());
    CHECK(m.text() == L" hell");
    m.setCaret(0);
    CHECK(!m.backspace());
    CHECK(m.erase(1, 3) && m.text() == L" ll");
    CHECK(!m.erase(2, 2));
    m.setCaret(m.size());
    CHECK(!m.deleteForward());
}

KIT_TEST(text_model_selection_replace) {
    TextModel m;
    m.setText(L"abcdef");
    m.setSelection(1, 4);  // anchor 1, caret 4: "bcd"
    CHECK(m.hasSelection() && m.selectionStart() == 1 && m.selectionEnd() == 4);
    CHECK(m.selectedText() == L"bcd");
    CHECK(m.insert(L"X"));
    CHECK(m.text() == L"aXef" && m.caret() == 2 && !m.hasSelection());
    m.setSelection(0, 3);
    CHECK(m.backspace() && m.text() == L"f");
    m.setText(L"xyz");
    m.setCaret(1);
    m.setCaret(3, true);  // extends from 1
    CHECK(m.selectedText() == L"yz");
    CHECK(m.deleteSelection() && m.text() == L"x");
    m.selectAll();
    CHECK(m.selectedText() == L"x");
    m.setText(L"xy");
    m.setCaret(2);
    m.setCaret(0, true);
    CHECK(m.selectionStart() == 0 && m.selectionEnd() == 2);
}

KIT_TEST(text_model_caret_clamps) {
    TextModel m;
    m.setText(L"ab");
    m.setCaret(99);
    CHECK(m.caret() == 2);
    m.setSelection(50, 60);
    CHECK(m.anchor() == 2 && m.caret() == 2);
}

KIT_TEST(text_model_words) {
    TextModel m;
    m.setText(L"foo bar_baz, qux");  // f0 o1 o2 _3 b4 .. z10 ,11 _12 q13 u14 x15
    // Ctrl+Left: to the start of the word, the punctuation run or the spaces before the caret.
    CHECK(m.wordStartBefore(16) == 13);
    CHECK(m.wordStartBefore(13) == 11);  // the space, then the comma
    CHECK(m.wordStartBefore(11) == 4);   // "bar_baz" is one word: letters, digits and '_'
    CHECK(m.wordStartBefore(4) == 0);
    CHECK(m.wordStartBefore(0) == 0);
    // Ctrl+Right: to the start of the next word.
    CHECK(m.wordEndAfter(0) == 4);
    CHECK(m.wordEndAfter(3) == 4);
    CHECK(m.wordEndAfter(4) == 11);
    CHECK(m.wordEndAfter(11) == 13);  // the comma, then the space
    CHECK(m.wordEndAfter(15) == 16);
    CHECK(m.wordEndAfter(16) == 16);
    size_t a = 0, b = 0;
    m.wordAt(5, a, b);  // inside "bar_baz"
    CHECK(a == 4 && b == 11);
    m.wordAt(3, a, b);  // the space
    CHECK(a == 3 && b == 4);
    m.wordAt(11, a, b);  // the comma
    CHECK(a == 11 && b == 12);
    m.wordAt(15, a, b);  // inside "qux"
    CHECK(a == 13 && b == 16);
    m.wordAt(16, a, b);  // at the end: the character before
    CHECK(a == 13 && b == 16);
}

KIT_TEST(text_model_word_delete) {
    TextModel m;
    m.setText(L"one two  three");
    CHECK(m.backspaceWord() && m.text() == L"one two  ");
    CHECK(m.backspaceWord() && m.text() == L"one ");
    m.setCaret(0);
    CHECK(m.deleteWordForward() && m.text().empty());  // "one "
    CHECK(!m.deleteWordForward());
}

KIT_TEST(text_model_lines) {
    TextModel m;
    m.setMultiline(true);
    m.setText(L"ab\ncd\n\nef");
    CHECK(m.lineStart(0) == 0 && m.lineEnd(0) == 2);
    CHECK(m.lineStart(2) == 0 && m.lineEnd(2) == 2);  // at the newline: the line before it
    CHECK(m.lineStart(4) == 3 && m.lineEnd(4) == 5);
    CHECK(m.lineStart(6) == 6 && m.lineEnd(6) == 6);  // the empty line
    CHECK(m.lineStart(9) == 7 && m.lineEnd(9) == 9);
    CHECK(m.lineEnd(99) == 9);
}

KIT_TEST(text_model_newlines_by_mode) {
    TextModel m;
    m.setText(L"a\r\nb\rc\td");  // single-line: newlines and tabs are dropped
    CHECK(m.text() == L"abcd");
    m.setMultiline(true);
    m.setText(L"a\r\nb\rc\td");  // multi-line: \r\n and \r become \n; the tab stays
    CHECK(m.text() == L"a\nb\nc\td");
    m.setText(L"x\x01y");  // control characters never go in
    CHECK(m.text() == L"xy");
    m.setCaret(m.size());
    CHECK(m.insert(L"\r\n"));
    CHECK(m.text() == L"xy\n");
}

KIT_TEST(text_model_undo_groups_typing) {
    TextModel m;
    m.insert(L"a", true);
    m.insert(L"b", true);
    m.insert(L"c", true);
    CHECK(m.text() == L"abc");
    CHECK(m.undo());  // one step for the three characters
    CHECK(m.text().empty() && m.caret() == 0);
    CHECK(!m.canUndo());
    CHECK(m.redo());
    CHECK(m.text() == L"abc" && m.caret() == 3);
    CHECK(!m.canRedo());
}

KIT_TEST(text_model_undo_breaks) {
    TextModel m;
    m.insert(L"ab", true);
    m.setCaret(0);  // a caret move ends the step
    m.insert(L"x", true);
    CHECK(m.text() == L"xab");
    CHECK(m.undo() && m.text() == L"ab");
    CHECK(m.undo() && m.text().empty());
    // A paste is its own step, and so is a non-typing edit.
    TextModel p;
    p.insert(L"a", true);
    p.insert(L"PASTE", false);
    p.insert(L"b", true);
    CHECK(p.text() == L"aPASTEb");
    CHECK(p.undo() && p.text() == L"aPASTE");
    CHECK(p.undo() && p.text() == L"a");
    CHECK(p.undo() && p.text().empty());
}

KIT_TEST(text_model_undo_groups_deletes) {
    TextModel m;
    m.setText(L"abcd");
    m.breakUndo();
    CHECK(m.backspace() && m.backspace());  // "ab"
    CHECK(m.text() == L"ab");
    CHECK(m.undo() && m.text() == L"abcd");  // one step for both
    CHECK(!m.undo() || m.text() == L"abcd");

    TextModel f;
    f.setText(L"abcd");
    f.setCaret(0);
    CHECK(f.deleteForward() && f.deleteForward());  // "cd"
    CHECK(f.text() == L"cd");
    CHECK(f.undo() && f.text() == L"abcd" && f.caret() == 0);
}

KIT_TEST(text_model_undo_restores_caret_and_selection) {
    TextModel m;
    m.setText(L"hello world");
    m.setSelection(6, 11);
    CHECK(m.insert(L"there"));
    CHECK(m.text() == L"hello there");
    CHECK(m.undo());
    CHECK(m.text() == L"hello world");
    CHECK(m.selectionStart() == 6 && m.selectionEnd() == 11);
    CHECK(m.redo() && m.text() == L"hello there" && m.caret() == 11);
}

KIT_TEST(text_model_redo_cleared_by_edit) {
    TextModel m;
    m.insert(L"a", false);
    m.insert(L"b", false);
    CHECK(m.undo() && m.text() == L"a");
    CHECK(m.canRedo());
    m.insert(L"c", false);
    CHECK(!m.canRedo());
    CHECK(m.text() == L"ac");
}

KIT_TEST(text_model_setText_clears_history) {
    TextModel m;
    m.insert(L"abc", true);
    m.setText(L"new");
    CHECK(!m.canUndo() && !m.canRedo());
    CHECK(m.caret() == 3);
    CHECK(!m.undo());
}

KIT_TEST(text_model_revision) {
    TextModel m;
    const auto r0 = m.revision();
    m.setCaret(0);
    CHECK(m.revision() == r0);  // moving the caret is not a change
    m.insert(L"a");
    CHECK(m.revision() != r0);
    const auto r1 = m.revision();
    m.undo();
    CHECK(m.revision() != r1);
}

KIT_TEST(text_model_max_length) {
    TextModel m;
    m.setMaxLength(4);
    CHECK(m.insert(L"abcdef"));
    CHECK(m.text() == L"abcd");
    CHECK(!m.insert(L"x"));  // full
    m.setSelection(1, 3);
    CHECK(m.insert(L"XY"));  // the selection's room counts
    CHECK(m.text() == L"aXYd");
    m.setSelection(0, 2);
    CHECK(m.insert(L"123456") && m.text() == L"12Yd");  // the selection's two units make the room: only "12" goes in
    m.setText(L"0123456789");
    CHECK(m.text() == L"0123");
    m.setMaxLength(0);  // no limit
    CHECK(m.insert(L"xyz"));
    CHECK(m.text() == L"0123xyz");
}

KIT_TEST(text_model_max_length_keeps_surrogate_pairs) {
    if constexpr (sizeof(wchar_t) != 2) return;  // one unit per code point: nothing to split
    TextModel m;
    m.setMaxLength(3);
    CHECK(m.insert(L"ab" + codePoint(0x1F600)));  // a, b, then the pair does not fit whole
    CHECK(m.text() == L"ab");
    m.setMaxLength(0);
    CHECK(m.insert(codePoint(0x1F600)));
    m.setMaxLength(3);
    CHECK(!m.insert(L"z"));
}

KIT_TEST(text_model_read_only) {
    TextModel m;
    m.setText(L"abc");
    m.setReadOnly(true);
    CHECK(!m.insert(L"x"));
    CHECK(!m.backspace());
    CHECK(!m.deleteForward());
    CHECK(!m.backspaceWord());
    CHECK(!m.deleteSelection());
    CHECK(!m.erase(0, 1));
    CHECK(m.text() == L"abc");
    CHECK(!m.undo() && !m.redo());
    m.selectAll();  // selection and caret still work
    CHECK(m.selectedText() == L"abc");
    m.setReadOnly(false);
    CHECK(m.insert(L"!"));
    CHECK(m.text() == L"!");
}

KIT_TEST(text_model_filter) {
    TextModel m;
    m.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });  // digits only
    CHECK(m.insert(L"1a2-b3"));
    CHECK(m.text() == L"123");
    CHECK(!m.insert(L"xyz"));
    m.setText(L"4x5");  // setText sanitizes too
    CHECK(m.text() == L"45");
    m.setFilter(nullptr);
    CHECK(m.insert(L"x"));
    CHECK(m.text() == L"45x");
}

KIT_TEST(text_model_surrogate_pairs_move_whole) {
    TextModel m;
    m.setText(codePoint(0x1F600) + L"b");
    const size_t pair = sizeof(wchar_t) == 2 ? 2 : 1;
    CHECK(m.nextPos(0) == pair);
    CHECK(m.prevPos(pair) == 0);
    CHECK(m.nextPos(pair) == pair + 1);
    m.setCaret(pair);
    CHECK(m.backspace() && m.text() == L"b");
    CHECK(m.undo() && m.text() == codePoint(0x1F600) + L"b");
    m.setCaret(0);
    CHECK(m.deleteForward() && m.text() == L"b");
}

KIT_TEST(text_model_utf8) {
    CHECK(fromUtf8("abc") == L"abc");
    CHECK(fromUtf8("\xC3\xA9") == std::wstring(1, wchar_t(0xE9)));           // e with acute
    CHECK(fromUtf8("\xE4\xBD\xA0") == std::wstring(1, wchar_t(0x4F60)));       // a CJK character
    CHECK(fromUtf8("\xF0\x9F\x98\x80") == codePoint(0x1F600));  // one code point, a pair where wchar_t is 16 bits
    CHECK(fromUtf8("\xF0\x9F\x98\x80").size() == (sizeof(wchar_t) == 2 ? 2u : 1u));
    const std::wstring badByte = {L'a', wchar_t(0xFFFD), L'b'};
    CHECK(fromUtf8("a\xFF" "b") == badByte);        // a bad byte
    const std::wstring cutOff = {L'x', wchar_t(0xFFFD)};
    CHECK(fromUtf8("x\xE4\xBD") == cutOff);         // cut off at the end
    const std::string round = "h\xC3\xA9 \xF0\x9F\x98\x80 \xE4\xBD\xA0";
    CHECK(toUtf8(fromUtf8(round)) == round);
    CHECK(toUtf8(std::wstring(L"x") + wchar_t(0xD800)) == "x\xEF\xBF\xBD");  // a lone half
}

KIT_TEST(text_model_utf8_bad_bytes_keep_the_rest) {
    // A sequence cut off by the next byte: one U+FFFD for the bytes before it, and the text after it stays.
    CHECK(fromUtf8("ab\xE4" "c") == L"ab\xFFFD" L"c");
    CHECK(fromUtf8("a\xC3") == L"a\xFFFD");  // cut off at the end
    CHECK(fromUtf8("\xC3\xA4") == L"\xE4");  // a valid two-byte sequence
}

KIT_TEST(text_model_utf8_rejects_surrogates_and_big_code_points) {
    // Encoded surrogates (ED A0 80) and code points above U+10FFFF (F4 90 80 80) are not text: each byte is U+FFFD.
    CHECK(fromUtf8("\xED\xA0\x80") == L"\xFFFD\xFFFD\xFFFD");
    CHECK(fromUtf8("\xF4\x90\x80\x80") == L"\xFFFD\xFFFD\xFFFD\xFFFD");
    CHECK(fromUtf8("\xC0\xAF") == L"\xFFFD\xFFFD");  // an overlong form
    for (wchar_t c : fromUtf8("\xED\xA0\x80\xF4\x90\x80\x80")) CHECK(!isHighSurrogate(c) && !isLowSurrogate(c));
}

KIT_TEST(text_model_caret_never_inside_a_pair) {
    TextModel m;
    m.setText(L"\xD83D\xDE00" L"a");  // a pair, then 'a'
    m.setCaret(1);                    // inside the pair: the start of the pair
    CHECK(m.caret() == 0);
    m.setSelection(1, 2);
    CHECK(m.anchor() == 0 && m.caret() == 2);
    m.setCaret(3);
    CHECK(m.caret() == 3);
}

KIT_TEST(text_model_newlines_are_words_of_their_own) {
    TextModel m;
    m.setMultiline(true);  // newlines are kept only in a multi-line text
    m.setText(L"abc\n\n\ndef");  // newlines at 3, 4 and 5
    size_t from = 0, to = 0;
    m.wordAt(4, from, to);  // a double click on the middle newline selects that newline only
    CHECK(from == 4 && to == 5);
    CHECK(m.wordStartBefore(6) == 5);  // Ctrl+Left at the start of "def": one newline
    CHECK(m.wordEndAfter(3) == 4);
    m.setCaret(6);
    CHECK(m.backspaceWord());  // Ctrl+Backspace: one newline goes
    CHECK(m.text() == L"abc\n\ndef");
}
