#pragma once
// The text of a field, without a view: the buffer, the caret and the selection, edits with undo and redo, and the
// rules of editing (word and line ends, a maximum length, read-only, a filter for what may be typed). TextField draws
// and handles events with it; a test or a tool can edit a text with it alone.
//
// Positions are indexes into the buffer in UTF-16 units (wchar_t on Windows). A caret never stops inside a surrogate
// pair. Typing one character after another is one undo step; so is each run of Backspace or of Delete.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace stardraft::kit {

// UTF-8 to a wide string (surrogate pairs as two units where wchar_t is 16 bits). Bad bytes become U+FFFD.
std::wstring fromUtf8(std::string_view utf8);
// The wide string as UTF-8 (a surrogate pair becomes one 4-byte sequence).
std::string toUtf8(std::wstring_view w);

// Surrogate halves (UTF-16 units of a pair; never present where wchar_t holds whole code points).
inline bool isHighSurrogate(wchar_t c) { return uint32_t(c) >= 0xD800 && uint32_t(c) <= 0xDBFF; }
inline bool isLowSurrogate(wchar_t c) { return uint32_t(c) >= 0xDC00 && uint32_t(c) <= 0xDFFF; }

class TextModel {
public:
    TextModel() = default;

    // The buffer. setText sanitizes it like typed text (newlines, the filter, the maximum length) and clears the
    // undo history; the caret goes to the end.
    const std::wstring& text() const { return text_; }
    size_t size() const { return text_.size(); }
    void setText(std::wstring t);

    // Changes on every edit (also undo and redo); a view compares it to see whether its layout is still good.
    unsigned long long revision() const { return revision_; }

    // The caret and the anchor (the other end of the selection). Without a selection they are equal.
    size_t caret() const { return caret_; }
    size_t anchor() const { return anchor_; }
    bool hasSelection() const { return caret_ != anchor_; }
    size_t selectionStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
    size_t selectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
    std::wstring selectedText() const { return text_.substr(selectionStart(), selectionEnd() - selectionStart()); }
    // Moves the caret (clamped). `extend`: the anchor stays, so the text between them is selected; else both go there.
    void setCaret(size_t pos, bool extend = false);
    void setSelection(size_t anchor, size_t caret);
    void selectAll() { setSelection(0, text_.size()); }

    // Edits. Each returns true when the text changed. Insert replaces the selection; the text is sanitized first
    // (see setMultiline, setFilter) and cut at the maximum length. `typing`: a character typed (grouped for undo).
    bool insert(std::wstring_view s, bool typing = false);
    bool erase(size_t from, size_t to);
    bool deleteSelection() { return hasSelection() && erase(selectionStart(), selectionEnd()); }
    bool backspace();        // the selection, else the character before the caret
    bool deleteForward();    // the selection, else the character after the caret
    bool backspaceWord();    // the selection, else back to the start of the word before the caret
    bool deleteWordForward();

    // Word and line positions. Words: runs of letters and digits (and other non-ASCII characters), runs of spaces,
    // runs of punctuation; a newline is a word of its own. Lines are the parts between newlines.
    size_t wordStartBefore(size_t pos) const;  // Ctrl+Left
    size_t wordEndAfter(size_t pos) const;     // Ctrl+Right
    void wordAt(size_t pos, size_t& from, size_t& to) const;  // a double click
    size_t lineStart(size_t pos) const;
    size_t lineEnd(size_t pos) const;  // at the newline, or the end of the text

    // Undo and redo. A step ends at any caret move (setCaret, setSelection, selectAll) and at any step that is not
    // typing or deleting a character.
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void breakUndo() { groupBreak_ = true; }

    // Rules of editing. 0 maximum length: none. A filter gets each UTF-16 unit that is typed or pasted and returns
    // false to drop it (digits only: [](wchar_t c) { return c >= '0' && c <= '9'; }).
    void setMaxLength(size_t n) { maxLength_ = n; }
    size_t maxLength() const { return maxLength_; }
    void setReadOnly(bool on) { readOnly_ = on; }
    bool readOnly() const { return readOnly_; }
    void setFilter(std::function<bool(wchar_t)> f) { filter_ = std::move(f); }
    // Single-line: newlines are dropped from what goes in. Multi-line: newlines are kept (\r\n and \r become \n).
    void setMultiline(bool on) { multiline_ = on; }
    bool multiline() const { return multiline_; }

    // The position one character before or after `pos` (a surrogate pair counts as one).
    size_t prevPos(size_t pos) const;
    size_t nextPos(size_t pos) const;

private:
    enum class Kind { None, Typing, Back, Forward };
    struct Edit {
        size_t pos = 0;
        std::wstring removed, inserted;
        size_t caretBefore = 0, anchorBefore = 0, caretAfter = 0, anchorAfter = 0;
        Kind kind = Kind::None;
    };
    // Replaces [from, to) with `ins` (already sanitized), puts the caret and the anchor at `caret`, and records the step.
    bool replace(size_t from, size_t to, std::wstring_view ins, Kind kind, size_t caret);
    std::wstring sanitize(std::wstring_view s, size_t room) const;
    size_t boundary(size_t pos) const;  // clamped; never inside a surrogate pair

    std::wstring text_;
    size_t caret_ = 0, anchor_ = 0;
    unsigned long long revision_ = 0;
    std::vector<Edit> undo_, redo_;
    bool groupBreak_ = true;
    size_t maxLength_ = 0;
    bool readOnly_ = false, multiline_ = false;
    std::function<bool(wchar_t)> filter_;
};

}  // namespace stardraft::kit
