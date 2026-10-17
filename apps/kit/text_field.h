#pragma once
// A text field (docs/dev/CONTROLS.md, "Fields"): one line that scrolls sideways to keep the caret in view, or many lines
// that wrap at the width (or not) and scroll with the wheel, a painted scroll bar at the right edge when the text is
// taller than the field. The box is the editor's field box (Normal, Hot, Focus, Changed, ReadOnly). The text itself
// is a TextModel; this view draws it, takes the mouse and the keyboard, and talks to the input method.
//
// Layout is measured with the Canvas while painting, so widths follow the font. Mouse and vertical moves use the last
// painted layout; until the next paint after a change they fall back to the lines of the text (Up and Down keep the
// column in characters).

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "menu.h"
#include "scroll_bar.h"
#include "text_model.h"
#include "theme.h"
#include "view.h"

namespace stardraft::kit {

// A run of text drawn in its own color and weight (code: keywords, strings). Spans must not overlap.
struct Span {
    size_t start = 0, length = 0;
    draw::Color color = draw::kNone;  // kNone: the text color
    bool bold = false;
    bool strike = false, dotted = false;  // (RichField) crossed out; a dotted line under it
};

class TextField : public draw::View {
public:
    TextField();
    ~TextField() override;

    // The text and its editing rules (caret, selection, undo, maximum length, read-only, filter).
    TextModel& model() { return model_; }
    const TextModel& model() const { return model_; }
    const std::wstring& text() const { return model_.text(); }
    // Replaces the text (no onChange; the undo history is cleared; the caret goes to the end).
    void setText(std::wstring t);
    void setPlaceholder(std::wstring t);
    const std::wstring& placeholder() const { return placeholder_; }
    // Several lines (Enter makes a new line) or one (Enter runs onEnter). Several lines: wrap at the width or not.
    void setMultiline(bool on);
    bool multiline() const { return model_.multiline(); }
    void setWrap(bool on);
    // Room at the right edge, in px, that the text never goes into (a search box's x). The mouse there is not the
    // field's: the event goes on to the parent, and the pointer is an arrow.
    void setTrailing(int px) { trailing_ = px; }
    int trailing() const { return trailing_; }
    bool wrap() const { return wrap_; }
    void setReadOnly(bool on);
    bool readOnly() const { return model_.readOnly(); }
    // With nothing selected, Copy (and Ctrl+C) takes the whole text (a read-only code view, as the old edit box did).
    void setCopyWhole(bool on) { copyWhole_ = on; }
    // Off: the field is not a Tab stop (a disabled edit box, as the Win32 page had for a unit without shields).
    void setTabStop(bool on) { tabStop_ = on; }
    void setMaxLength(size_t n) { model_.setMaxLength(n); }
    void setFilter(std::function<bool(wchar_t)> f) { model_.setFilter(std::move(f)); }
    // The green Changed look (a value that differs from the saved one).
    void setChanged(bool on);
    bool changed() const { return changed_; }
    // Colored runs (code). Sorted by start here.
    void setSpans(std::vector<Span> spans);
    // The font: Face::Ui by default, px 0 is the UI's size.
    void setFont(draw::FontDesc f);
    const draw::FontDesc& fontDesc() const { return fontDesc_; }
    // Plain: the field draws no box of its own (the host draws the box around it, as the tool strip's number boxes do),
    // and the text takes the whole frame's height. Align right: a one-line text shorter than the field sits at its right.
    void setPlain(bool on) { plain_ = on; }
    void setAlignRight(bool on) { alignRight_ = on; }

    // As last painted: the caret's rectangle and the scroll offsets, in the view's coordinates.
    draw::Rect caretRect() const { return caretRect_; }
    // As last painted: where the drawn text of the caret's line ends (the view's x; 0: none drawn). With the caret at
    // the end of its line, the caret is there (a test: the caret and the drawn letters match).
    int textRight() const { return textRight_; }
    // An input method's text that is not committed yet (shown at the caret, not in the text).
    const std::wstring& composition() const { return comp_; }
    int scrollX() const { return scrollX_; }
    int scrollY() const { return scrollY_; }
    // Several lines: line `line` of the text (a paragraph, from 0) goes to the top at the next paint, as far as the
    // text allows (a code view that shows a function). The height of a line as last painted.
    void scrollToLine(size_t line) {
        scrollToLine_ = line;
        invalidate();
    }
    int lineHeight() const { return lineH_; }

    // Called after the user changed the text (typing, paste, cut, undo, redo); not after setText.
    std::function<void()> onChange;
    // Single-line: Enter. Multi-line: Ctrl+Enter (the others make a new line). Consumed only when set.
    std::function<void()> onEnter;
    // Escape, when set (else Escape goes on to the parents).
    std::function<void()> onEscape;
    // The focus goes elsewhere.
    std::function<void()> onFocusLost;
    // The mouse left the field (the field gets that event itself; a parent that draws over the field needs it too).
    std::function<void()> onHoverEnd;

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    bool text(const draw::TextEvent& e) override;
    bool compose(const draw::CompositionEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override { return inTrailing(pt) ? draw::Cursor::Arrow : draw::Cursor::IBeam; }
    bool focusable() const override { return tabStop_; }
    bool typesText() const override { return true; }
    void focusChanged(bool has) override;
    void tabbedTo() override;
    void captureLost() override;

private:
    // A right click opens the edit commands (Undo, Cut, Copy, Paste, Delete, Select all), as the native edit box did;
    // a read-only field: Copy (Copy all with setCopyWhole and no selection) and Select all, as the Windows code pane.
    std::vector<MenuItem> editMenu();
    void openEditMenu(draw::Point at);
    MenuLayout menuLayout_;  // measured while painting (the menu's size)

    struct Line {
        size_t start = 0, end = 0;  // in the shown text; the newline (if any) is not in the line
        bool last = true;           // the last line of its paragraph
    };
    struct Attr {
        draw::Color color = draw::kNone;
        bool bold = false, selected = false, comp = false;
    };
    struct Run {
        size_t from = 0, to = 0;
        Attr attr;
    };
    struct LayoutKey {
        unsigned long long rev = 0;
        unsigned style = 0;
        int width = -1;
        bool operator==(const LayoutKey& o) const { return rev == o.rev && style == o.style && width == o.width; }
    };
    struct EnsureKey {
        unsigned long long rev = ~0ull;
        size_t caret = ~size_t(0), anchor = ~size_t(0), display = ~size_t(0);
        bool operator==(const EnsureKey& o) const { return rev == o.rev && caret == o.caret && anchor == o.anchor && display == o.display; }
    };

    // The shown text: the buffer, with the composition in place of the selection.
    bool composing() const { return !comp_.empty(); }
    size_t displayOf(size_t m) const;
    size_t modelOf(size_t d) const;
    size_t displayCaret() const;
    size_t caretLine() const;  // the line the caret is on (see breakAt_)
    Attr attrAt(size_t d) const;
    const Span* spanAt(size_t m) const;
    std::vector<Run> runs(size_t from, size_t to) const;

    bool fresh() const { return laidOut_ && layoutKey_ == LayoutKey{model_.revision(), style_, width()}; }
    void relayout(draw::Canvas& c);
    void buildLines(int vw);
    size_t lineOf(size_t d) const;
    size_t posAtX(size_t line, int x) const;  // x: from the line's start
    size_t caretFromPoint(draw::Point pt) const;
    int padX() const { return px(plain_ ? 2 : 6); }
    int padY() const { return plain_ ? 0 : px(3); }
    int alignOff() const;  // the text's shift to the right (align right), in px
    draw::Rect inner() const;
    bool inTrailing(draw::Point pt) const { return trailing_ > 0 && pt.x >= width() - trailing_; }
    int lineTop(size_t line) const;
    void ensureCaretVisible();
    void clampScroll();
    void updateCaretRect();
    void syncBar();   // the scroll bar's place, range and position for the last layout
    void syncHBar();  // the same for the bar at the bottom

    void moveTo(size_t m, bool extend, bool keepGoal = false);
    void vertical(int dir, bool extend, int count);
    // The model position of the start or end of the caret's visual line; wrapEnd: the end of a wrapped line.
    size_t visualEdge(bool end, bool& wrapEnd) const;
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
    void copySelection();
    void paste();
    void afterAction();
    void syncInput();
    void armBlink();
    void stopBlink();
    void resetBlink();

    TextModel model_;
    std::wstring placeholder_, comp_;
    size_t compCur_ = 0;  // the composition's caret, in units
    // End on a wrapped line puts the caret after the line's last character. That position is also the next line's
    // start, so the caret stays on the line it ended (this is the model position it applies to; ~0: none).
    size_t breakAt_ = ~size_t(0);
    bool focused_ = false;  // the field's own focus, as focusChanged reports it (the host can drop it while the router keeps it)
    ScrollBar bar_;         // several lines taller than the field: the painted bar at the right edge
    int barTotal_ = -1, barPage_ = -1;
    ScrollBar hbar_{Axis::Horizontal};  // several lines that do not wrap and are wider than the field: the bar at the bottom
    int hbarTotal_ = -1, hbarPage_ = -1;
    std::vector<Span> spans_;
    draw::FontDesc fontDesc_;
    bool wrap_ = true, changed_ = false, copyWhole_ = false, tabStop_ = true;
    bool plain_ = false, alignRight_ = false;
    int trailing_ = 0;
    unsigned style_ = 0;  // changes with what the layout depends on besides the text and the width

    // The layout of the last paint.
    std::vector<Line> lines_;
    std::vector<uint8_t> piece_;  // 1 where a piece of the layout starts (relayout: each is measured and drawn as one string)
    std::vector<int> px_;  // x of each index of the shown text (px_[n] is the end); a surrogate's second unit: as the first
    std::wstring shown_;
    int lineH_ = 16, contentW_ = 0;
    size_t scrollToLine_ = ~size_t(0);  // scrollToLine's line until the next paint
    bool laidOut_ = false, barShown_ = false, hbarShown_ = false;
    LayoutKey layoutKey_;

    int scrollX_ = 0, scrollY_ = 0;
    EnsureKey ensured_;
    draw::Rect caretRect_;
    int textRight_ = 0;  // textRight
    int goalX_ = -1;  // the column Up and Down keep; -1: none

    bool hot_ = false, dragging_ = false, caretOn_ = true;
    int clicks_ = 0;  // 1: a click; 2: a double click (a click that comes right after it makes a triple click)
    std::chrono::steady_clock::time_point doubleAt_;
    draw::Point doublePt_;
    int blinkId_ = 0;
    std::shared_ptr<bool> alive_;  // false once destroyed (the blink's timer may still run)
};

}  // namespace stardraft::kit
