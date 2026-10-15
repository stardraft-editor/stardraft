#pragma once
// A rich text field (docs/dev/CONTROLS.md, "Fields"): text with chips. A chip is one atomic piece of the text, drawn as
// a value chip (the look of the editor's trigger values): it has a label (the text it shows, and what Copy takes as
// text), a kind and a payload that the owner gives (an id). A chip counts as one character for the caret, the
// selection, Backspace, Delete, the maximum length and undo. The plain text around the chips edits as a TextField does:
// one line that scrolls sideways, or several lines (wrapped at the width, or not), the input method, undo and redo.
//
// Copy writes the chips' labels as text, and a private form too: a paste inside the program that finds the same text
// in the clipboard puts the chips back. Colors (code: the game's text colors) are spans over the plain text.
//
// Layout is measured with the Canvas while painting. Mouse and vertical moves use the last painted layout.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "text_field.h"
#include "text_model.h"
#include "theme.h"
#include "view.h"

namespace stardraft::kit {

struct RichChip {
    std::wstring label;   // the text the chip shows (and what Copy takes as text)
    int kind = 0;         // the owner's kind of chip (the field does not draw by it)
    uint64_t id = 0;      // the owner's payload: an id, or 0
};

// A piece of the content: plain text, or a chip.
struct RichPart {
    bool chip = false;
    std::wstring text;    // when !chip
    RichChip value;       // when chip

    static RichPart plain(std::wstring t) {
        RichPart p;
        p.text = std::move(t);
        return p;
    }
    static RichPart of(RichChip c) {
        RichPart p;
        p.chip = true;
        p.value = std::move(c);
        return p;
    }
};

class RichField : public draw::View {
public:
    // The most chips a field holds between two setParts (each chip is one private-use character of the text).
    static constexpr size_t kMaxChips = 6400;

    RichField();
    ~RichField() override;

    // The content. plainText: the plain text with each chip as its label. parts: the whole content in order.
    std::wstring plainText() const;
    std::vector<RichPart> parts() const;
    // Replaces the content (no onChange; the undo history is cleared; the caret goes to the end). Private-use
    // characters in plain text are dropped; chips past kMaxChips become their labels.
    void setParts(std::vector<RichPart> parts);
    void setText(std::wstring plain) { setParts({RichPart::plain(std::move(plain))}); }
    // The chips in the text, in reading order; chip(index) is empty when there is none.
    size_t chipCount() const;
    RichChip chip(size_t index) const;
    // Inserts a chip at the caret (replacing the selection). false: read-only, or the field has kMaxChips chips.
    bool insertChip(const RichChip& c);
    // Replaces the chip at `index` by `c`, as one undo step. false: no such chip, read-only, or kMaxChips reached.
    bool replaceChip(size_t index, const RichChip& c);

    // Several lines (Enter makes a new line; Ctrl+Enter runs onEnter) or one (Enter runs onEnter).
    void setMultiline(bool on);
    bool multiline() const { return model_.multiline(); }
    // Several lines: wrap at the width (or not, and scroll sideways).
    void setWrap(bool on);
    bool wrap() const { return wrap_; }
    void setReadOnly(bool on);
    bool readOnly() const { return model_.readOnly(); }
    // The maximum length, in characters (a chip counts as one). 0: none.
    void setMaxLength(size_t n) { model_.setMaxLength(n); }
    void setPlaceholder(std::wstring t);
    const std::wstring& placeholder() const { return placeholder_; }
    // Colors and weights of the plain text. Positions are the field's: a chip is one character.
    void setSpans(std::vector<Span> spans);
    // The font: Face::Ui by default, px 0 is the UI's size.
    void setFont(draw::FontDesc f);
    // Several lines that wrap: each paragraph's alignment (the text between two line ends), in order: 0 left, 1 center,
    // 2 right. A paragraph past the list's end is left. (The game's text codes 13 and 12; the owner keeps them as the
    // text changes.)
    void setLineAligns(std::vector<uint8_t> aligns);
    // Plain: the field draws no box of its own (the owner draws the box around it); the text keeps its padding.
    void setPlain(bool on);
    // The paragraph the caret is in (0 is the first).
    size_t caretParagraph() const;

    // The caret and the selection, as positions of the text (a chip is one position).
    size_t caret() const { return model_.caret(); }
    size_t anchor() const { return model_.anchor(); }
    bool hasSelection() const { return model_.hasSelection(); }
    void selectAll();

    // As last painted: the caret's rectangle and the scroll offsets, in the view's coordinates.
    draw::Rect caretRect() const { return caretRect_; }
    int scrollX() const { return scrollX_; }
    int scrollY() const { return scrollY_; }

    // Called after the user changed the content (typing, paste, cut, undo, redo, a chip inserted or replaced); not after
    // setParts.
    std::function<void()> onChange;
    // Single-line: Enter. Multi-line: Ctrl+Enter (Enter makes a new line). Consumed only when set.
    std::function<void()> onEnter;
    // Escape, when set (else Escape goes on to the parents).
    std::function<void()> onEscape;
    // The focus goes elsewhere.
    std::function<void()> onFocusLost;
    // A click on a chip: its index in reading order (as chip(index)), and its rectangle in this field's coordinates.
    // The caret goes after the chip first.
    std::function<void(size_t index, draw::Rect rect)> onChipClick;

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    bool text(const draw::TextEvent& e) override;
    bool compose(const draw::CompositionEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override { return chipAt(pt) != kNone ? draw::Cursor::Hand : draw::Cursor::IBeam; }
    bool focusable() const override { return true; }
    bool typesText() const override { return true; }
    void focusChanged(bool has) override;
    void captureLost() override;

private:
    static constexpr size_t kNone = size_t(-1);

    // What a unit of the layout is: a character (one or two UTF-16 units), a chip, a line break, or a composition's
    // character (shown, not in the text).
    enum : uint8_t { kChar, kChip, kNewline, kComp };
    struct Unit {
        size_t from = 0, to = 0;  // the text's positions (a composition's units: both at the composition's start)
        int x = 0, w = 0;         // x from the start of the text (px); w: the width
        uint8_t kind = kChar;
        bool bold = false;
        size_t aux = 0;           // a chip: its table index; a composition's unit: its index in the composition
    };
    struct Line {
        size_t first = 0, last = 0;  // units [first, last); the line's newline (if any) is its last unit
        bool hard = false;           // it ends with a newline
        int x0 = 0;                  // x of its first unit
        int width = 0;               // the width of its units (without a newline)
        int off = 0;                 // its paragraph's alignment: how far right of the left edge the line starts
    };
    struct Attr {
        draw::Color color = draw::kNone;
        bool bold = false, selected = false, comp = false, strike = false, dotted = false;
    };
    struct LayoutKey {
        unsigned long long rev = 0;
        unsigned style = 0;
        int width = -1;
        bool operator==(const LayoutKey& o) const { return rev == o.rev && style == o.style && width == o.width; }
    };
    bool composing() const { return !comp_.empty(); }
    bool fresh() const { return laidOut_ && layoutKey_ == LayoutKey{model_.revision(), style_, width()}; }
    const Span* spanAt(size_t m) const;
    Attr attrOf(const Unit& u) const;
    void relayout(draw::Canvas& c);
    void buildLines(int vw);
    size_t lineOf(size_t unit) const;
    size_t lineAtY(int y) const;
    size_t posInLine(size_t L, int absX) const;   // the unit index for an x in the line's text
    size_t modelAt(size_t unit) const;            // the text's position of a unit boundary
    size_t caretIndex() const;                    // the unit the caret is before
    size_t caretLine() const;
    int caretX() const;                           // the caret's x from the start of the text
    size_t hitCaret(draw::Point pt, bool& brk) const;  // the text's position at pt; brk: the end of a wrapped line
    size_t chipAt(draw::Point pt) const;          // the chip's unit under pt, or kNone
    size_t ordinalOf(size_t unit) const;          // the chip's number in reading order
    draw::Rect chipRect(size_t unit) const;       // in this field's coordinates
    int padX() const { return px(6); }
    int padY() const { return px(3); }
    draw::Rect inner() const;
    int lineTop(size_t L) const;
    void clampScroll();
    void ensureCaretVisible();
    void updateCaretRect();
    void paintLine(draw::Canvas& c, size_t L, const draw::Rect& in);
    void drawChip(draw::Canvas& c, size_t unit);
    void setCaretAt(draw::Point pt, bool extend);

    void moveTo(size_t m, bool extend);
    void vertical(int dir, bool extend);
    std::vector<RichPart> partsOf(size_t from, size_t to) const;
    bool insertParts(const std::vector<RichPart>& parts);
    void copySelection();
    void paste();
    void afterAction();
    void syncInput();
    size_t addChip(const RichChip& c);  // a table entry; kNone when full

    // Runs an edit, then the after-action work, then onChange (last: the callback may destroy the field).
    template <class F>
    bool edit(F&& f) {
        const auto before = model_.revision();
        const bool r = f();
        const bool changed = model_.revision() != before;
        goalX_ = -1;
        afterAction();
        if (changed && onChange) {
            const auto cb = onChange;  // a copy: it lives through the call even if the field is destroyed
            cb();
        }
        return r;
    }

    TextModel model_;
    std::vector<RichChip> chips_;  // the table: a chip's text character is kChipBase + its index
    std::wstring placeholder_, comp_;
    size_t compCur_ = 0;   // the composition's caret, in characters
    size_t compStart_ = 0; // the unit the composition starts at (last layout)
    std::vector<Span> spans_;
    draw::FontDesc fontDesc_;
    bool wrap_ = true;
    bool plain_ = false;
    std::vector<uint8_t> aligns_;  // each paragraph's alignment (setLineAligns)
    bool focused_ = false, dragging_ = false, hot_ = false;
    bool atBreak_ = false;  // the caret is at the end of a wrapped line (shown on that line)
    size_t hotChip_ = kNone;
    unsigned style_ = 0;

    // The layout of the last paint.
    std::vector<Unit> units_;
    std::vector<Line> lines_;
    int totalW_ = 0, lineH_ = 16, contentW_ = 0;
    bool laidOut_ = false;
    LayoutKey layoutKey_;

    int scrollX_ = 0, scrollY_ = 0;
    unsigned long long ensuredRev_ = ~0ull;
    size_t ensuredCaret_ = kNone, ensuredAnchor_ = kNone;
    unsigned ensuredStyle_ = ~0u;
    draw::Rect caretRect_;
    int goalX_ = -1;  // the x Up and Down keep; -1: none
};

}  // namespace stardraft::kit
