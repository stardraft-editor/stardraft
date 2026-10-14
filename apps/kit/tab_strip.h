#pragma once
// A row of tabs (docs/dev/CONTROLS.md, "Tabs"). Two looks: Segmented, one box with a line between the segments and the
// chosen one filled (the mode buttons of the top bar); Tabs, flat tabs with a bar under the chosen one (the document
// tabs and the panel tabs). A tab can have an icon and a close (x): a click on the x, or a middle click, closes it. A
// tab with unsaved changes shows a dot where the x is, until the mouse is over it. Drag a tab along the row to move it.
// Left and Right (Home, End) choose the tabs.
//
// The strip does not own the documents: it shows `tabs` and tells the owner what the user did. onSelect and onClose
// give the index; onMove gives (from, to), and the strip has already moved the tab (and its selection) to there, so the
// owner moves its own list the same way. A tab's place is known after a paint: a click before the first paint finds no
// tab.

#include <functional>
#include <string>
#include <vector>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class TabLook { Segmented, Tabs };

struct Tab {
    std::wstring text;
    int icon = -1;           // a UI icon (an id of apps/editor/res/ui_icons.h) before the text; -1: none
    bool closable = false;   // a close (x) at its right; a middle click closes it too
    bool changed = false;    // a dot where the x is (unsaved), while the mouse is not on the tab
    std::wstring tip;        // the tooltip; empty: none
};

class TabStrip : public draw::View {
public:
    explicit TabStrip(TabLook look = TabLook::Tabs) : look_(look) {}

    std::function<void(int index)> onSelect;
    std::function<void(int index)> onClose;
    std::function<void(int from, int to)> onMove;

    TabLook look() const { return look_; }
    void setLook(TabLook l) {
        look_ = l;
        dirty_ = true;
        invalidate();
    }
    // Replaces the tabs. The selection stays at its index (kept in the range).
    void setTabs(std::vector<Tab> tabs);
    const std::vector<Tab>& tabs() const { return tabs_; }
    int count() const { return int(tabs_.size()); }
    // Sets the unsaved dot of tab `i`.
    void setChanged(int i, bool changed);
    // The chosen tab, or -1 when there are no tabs.
    int selected() const { return sel_; }
    // Chooses tab `i` without an event (out of range: nothing).
    void select(int i);

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point p) override;
    std::wstring tip(draw::Point p) override;
    bool focusable() const override { return true; }
    void resized() override { dirty_ = true; }

private:
    struct Hit {
        int index = -1;
        bool close = false;  // on the tab's x (or dot)
    };
    void measureWidths(draw::Canvas& c);
    draw::Rect tabRect(int i) const;
    draw::Rect closeRect(int i) const;
    Hit hitAt(draw::Point p) const;
    void drawTab(draw::Canvas& c, int i, const draw::Rect& r, draw::Color col) const;
    void selectByUser(int i);
    void moveTab(int from, int to);
    void dragTo(int x);
    void setCapture(bool on);

    std::vector<Tab> tabs_;
    std::vector<int> widths_;  // each tab's width, in the order of tabs_ (from the last paint)
    TabLook look_;
    bool dirty_ = true;        // the widths are to be measured at the next paint
    int sel_ = -1;
    int hot_ = -1;
    bool hotClose_ = false;
    int pressed_ = -1;         // the tab the mouse went down on (-1: none)
    bool pressedClose_ = false;
    int drag_ = -1;            // the tab being dragged (-1: none); its place follows the mouse
    bool dragging_ = false;    // moved far enough to be a drag, not a click
    int dragX_ = 0;
};

}  // namespace stardraft::kit
