#pragma once
// A painted scroll bar (docs/dev/CONTROLS.md, "Scroll bar (painted)"): the look of ui_kit.inc's scrollFace. At rest the
// thumb is a thin line at the bar's far edge; under the mouse or while it is dragged, the thumb is wider on its track.
// Drag the thumb; click the track to page by one page. The position is in the units of setRange(): a list uses pixels.

#include <functional>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class Axis { Vertical, Horizontal };

constexpr int kScrollBarW = 12;      // at 100 %: the bar's thickness (a list's bar)
constexpr int kScrollInset = 2;      // at 100 %: the thumb's space from the bar's edges
constexpr int kScrollMinThumb = 24;  // at 100 %: the shortest thumb

class ScrollBar : public draw::View {
public:
    explicit ScrollBar(Axis axis = Axis::Vertical) : axis_(axis) {}

    // Called when the user moves the position (a drag, a click on the track) with the new position. Not called by setPos.
    std::function<void(int)> onScroll;

    // `total`: the content's length; `page`: how much of it is visible (both in the units of the position).
    void setRange(int total, int page);
    // Moves the thumb without calling onScroll (for a list that scrolls by its wheel or keys). Clamped.
    void setPos(int pos);
    int pos() const { return pos_; }
    int maxPos() const { return total_ > page_ ? total_ - page_ : 0; }
    // False when all of the content is visible: nothing is drawn and the bar takes no clicks.
    bool scrollable() const { return maxPos() > 0; }
    // The thumb's place in the bar's coordinates, as it is when lit (the full width).
    draw::Rect thumb() const;
    // True under the mouse or while dragging: the track shows and the thumb is wider.
    bool lit() const { return hot_ || dragging_; }
    Axis axis() const { return axis_; }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;

private:
    void moveTo(int pos);  // a user's move: clamps, sets and reports
    void drag(draw::Point p);
    int length() const { return axis_ == Axis::Vertical ? height() : width(); }
    int along(draw::Point p) const { return axis_ == Axis::Vertical ? p.y : p.x; }

    Axis axis_;
    int total_ = 0, page_ = 0, pos_ = 0;
    bool hot_ = false, dragging_ = false;
    int grab_ = 0;  // while dragging: where on the thumb the mouse took it
};

}  // namespace stardraft::kit
