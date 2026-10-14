#pragma once
// A virtual list (docs/dev/CONTROLS.md, "List row" and "Table"): the rows are painted only while they are visible, from
// the callbacks. Rows are `rowHeight()` px high (the default: the list row of ui::kRowH); a header row with columns is
// optional. Single or multiple selection; the keyboard moves the cursor, type-ahead finds a row by its label's start,
// Enter and a double click activate it, a right click asks for the row's menu. The mouse wheel scrolls three rows a
// notch; the scroll bar (scroll_bar.h) shows at the right edge when the rows do not all fit.
//
// Selection, as Windows lists: a click selects one row; Ctrl+click toggles a row; Shift+click selects the rows from the
// anchor (the last row clicked without Shift); Ctrl+A selects all (multiple only); the arrows move the cursor and select
// the row they reach; Shift+arrows extend the selection from the anchor; Ctrl+arrows move the cursor only; Space toggles
// the cursor's row. Single mode ignores Ctrl and Shift.

#include <functional>
#include <string>
#include <vector>

#include "scroll_bar.h"
#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class SelectMode { Single, Multiple };
enum class ColAlign { Left, Center, Right };

// A column of the header. `width` at 100 %; 0: the column takes what the fixed ones leave (shared equally).
struct Column {
    std::wstring name;
    int width = 0;
    ColAlign align = ColAlign::Left;
};

// What a row looks like: selected; under the mouse; the cursor's row while the list has the focus.
struct RowState {
    bool selected = false, hot = false, focused = false;
};

class ListView : public draw::View {
public:
    ListView();
    ~ListView() override;

    // The text of row `i` (the first column; the default painter and type-ahead use it).
    std::function<std::wstring(int)> label;
    // The text of row `i` in column `k` (for the columns after the first, and the first when there is no label).
    std::function<std::wstring(int, size_t)> cell;
    // Paints row `i` in `row` (the list's coordinates, already scrolled). Replaces the default look of the row.
    std::function<void(draw::Canvas&, int, draw::Rect, RowState)> paintRow;
    // Enter, or a double click on row `i`.
    std::function<void(int)> onActivate;
    // A right click at `pt` (the list's coordinates); `i` is the row under it, or -1. The row is selected first, unless
    // it already is.
    std::function<void(int, draw::Point)> onContext;
    // The user changed the selection (not for the calls below).
    std::function<void()> onSelect;

    // The rows. Rows keep their selection when the count changes; the ones past the new count are dropped.
    void setCount(int n);
    int count() const { return count_; }
    // The row height at 100 % (px() scales it).
    void setRowHeight(int h100);
    int rowHeight() const;
    void setColumns(std::vector<Column> cols);
    const std::vector<Column>& columns() const { return cols_; }
    // The text in the middle of an empty list (dim, wrapped to the width).
    void setEmptyHint(std::wstring t);
    void setMode(SelectMode m);
    SelectMode mode() const { return mode_; }

    // The selection (programmatic: no onSelect). The cursor is the row the keyboard works from, -1 for none.
    bool isSelected(int i) const;
    std::vector<int> selection() const;
    int cursor() const { return cursor_; }
    void selectOnly(int i);
    void toggle(int i);  // Single mode: selects only i
    void selectAll();    // Multiple only
    void clearSelection();

    // Scrolling: `scrollTop()` in pixels from the first row's top; clamped.
    int scrollTop() const { return top_; }
    void setScrollTop(int px);
    void scrollBy(int px) { setScrollTop(top_ + px); }
    void ensureVisible(int i);
    // The row at `pt` (the list's coordinates), -1 for none (the header, below the last row, the scroll bar).
    int rowAt(draw::Point pt) const;
    // Row `i`'s rectangle in the list's coordinates (its place when scrolled to; may be outside the list).
    draw::Rect rowRect(int i) const;
    // The scroll bar (for tests and for callers that want its state).
    const ScrollBar& scrollBar() const { return bar_; }

    bool focusable() const override { return true; }
    void focusChanged(bool) override { invalidate(); }
    void resized() override { layout(); }
    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    bool text(const draw::TextEvent& e) override;

private:
    // Sizes in pixels.
    int headerH() const;
    int viewH() const;
    int rowPx() const;
    int contentH() const { return count_ * rowPx(); }
    int maxTop() const;
    int rowsRight() const;  // the rows' right edge: the scroll bar takes its width when it shows

    void layout();
    void userScroll(int px);  // from the scroll bar
    void apply(const std::function<void()>& change);  // a user's change: reports onSelect when the selection changed
    void click(int row, unsigned mods);
    void navigate(int target, unsigned mods);
    int moveTarget(draw::Key k) const;
    void selectSpan(int a, int b, bool keep);
    void typeAhead(wchar_t ch);
    void armTypeTimer();
    std::wstring cellText(int i, size_t k) const;
    draw::Rect colRect(draw::Rect row, size_t k) const;
    void paintHeader(draw::Canvas& c, int rw, int hh);
    void paintRowAt(draw::Canvas& c, int i, draw::Rect row);

    std::vector<Column> cols_;
    std::wstring hint_;
    int count_ = 0;
    int rowH_ = kRowH;  // at 100 %
    SelectMode mode_ = SelectMode::Single;
    std::vector<bool> sel_;
    int cursor_ = -1, anchor_ = -1, hot_ = -1, top_ = 0;
    std::wstring typed_;
    int typeTimer_ = 0;
    bool typeTimerOn_ = false;
    ScrollBar bar_{Axis::Vertical};
};

}  // namespace stardraft::kit
