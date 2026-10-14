#pragma once
// A view that shows a content view larger than itself: a vertical scroll bar (and a horizontal one when asked for),
// the wheel, the arrow and page keys, and scrollTo() to bring a part of the content into view. The position is kept
// when the content's size changes: it stays where it is, clamped to the new size.
//
// The content is a child of an inner viewport, so it is clipped to the visible part; the bars are beside the
// viewport, so they are not. The view sets the content's frame: its width is the viewport's (unless the view has a
// horizontal bar, then the content keeps its own width), its height is its own. The bars look like ui_kit.inc's
// scrollFace: a thin line at rest, wider with the mouse on them or while one is dragged.

#include <memory>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

constexpr int kScrollBarW = 12;  // a bar's thickness, at 100 %
constexpr int kWheelRows = 3;    // rows one wheel notch scrolls (a row is kRowH)

class ScrollView : public draw::View {
public:
    ScrollView();
    ~ScrollView() override;

    // The content (not owned: it must outlive this view or be removed first). Null: none.
    void setContent(draw::View* c);
    draw::View* content() const { return content_; }
    // A horizontal bar as well. The content keeps its own width then; else it is as wide as the visible part.
    void setHorizontal(bool on);
    bool horizontal() const { return horizontal_; }

    // The position: the content's point that is at the visible part's top left corner.
    int offsetX() const { return offX_; }
    int offsetY() const { return offY_; }
    void setOffset(int x, int y);  // clamped to the content
    void scrollBy(int dx, int dy);
    // Brings `r` (in the content's coordinates) into view: its top when it is taller than the visible part, else
    // the nearest edge that is outside.
    void scrollTo(draw::Rect r);
    // Brings `v` (a child of the content, or inside it) into view.
    void scrollTo(const draw::View& v);

    // The visible part, without the bars, and the largest offsets (0 when the content fits).
    int viewportWidth() const { return vw_; }
    int viewportHeight() const { return vh_; }
    int maxOffsetX() const;
    int maxOffsetY() const;
    bool hasVerticalBar() const;
    bool hasHorizontalBar() const;

    // Reads the content's size and fits the position and the bars to it. Every event and paint calls it; a view
    // that changes the content's height needs nothing else.
    void refresh();

    void paint(draw::Canvas&) override;
    void paintOver(draw::Canvas&) override;
    void resized() override { refresh(); }
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    // Tab stops on the view only when nothing inside it takes the focus, so keys can scroll it.
    bool focusable() const override;

private:
    class Port;  // the viewport: clips the content
    class Bar;   // a scroll bar
    friend class Bar;

    int axisView(bool vertical) const { return vertical ? vh_ : vw_; }
    int axisTotal(bool vertical) const;
    int axisOffset(bool vertical) const { return vertical ? offY_ : offX_; }
    void setAxis(bool vertical, int off);
    void scrollPage(bool vertical, int dir);

    draw::View* content_ = nullptr;
    std::unique_ptr<Port> port_;
    std::unique_ptr<Bar> vBar_, hBar_;
    bool horizontal_ = false;
    int vw_ = 0, vh_ = 0;    // the visible part, as the last refresh() fitted it
    int offX_ = 0, offY_ = 0;
};

}  // namespace stardraft::kit
